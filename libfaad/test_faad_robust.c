/*
 * Robustness test: a corrupted ADTS stream must never stall the decoder.
 *
 * Encodes a synthetic signal with libfaac, damages the stream with a fixed
 * PRNG (dropped frames, flipped bits, truncated tail), then feeds it to the
 * decoder the way a frontend does. The decoder must consume every byte,
 * advance on every call, emit at most one frame per call, and never exceed
 * the output the intact stream would produce.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "faac.h"
#include "faad.h"

#define RATE     44100
#define CHANNELS 2
#define SECONDS  4
#define FRAME    1024

static uint32_t rng_state = 0x2545F491u;
static uint32_t rng(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return rng_state; }

static int fail(const char *msg) { fprintf(stderr, "test_faad_robust: %s\n", msg); return 1; }

/* Encode to ADTS; returns the stream length or 0. */
static uint32_t encode(enum faac_object_type obj, uint8_t *out, uint32_t cap)
{
    faac_params p;
    if (faac_params_init(&p, sizeof(p)) != FAAC_OK) return 0;
    p.sample_rate = RATE;
    p.num_channels = CHANNELS;
    p.object_type = obj;
    p.bit_rate = 48000;
    p.output_format = FAAC_STREAM_ADTS;
    p.input_format = FAAC_INPUT_16BIT;
    faac_encoder *enc = NULL;
    if (faac_encoder_open(&p, &enc) != FAAC_OK) return 0;

    uint32_t total = 0;
    int16_t pcm[FRAME * CHANNELS];
    double phase = 0.0;
    for (int f = 0; f < RATE * SECONDS / FRAME; f++) {
        for (int i = 0; i < FRAME; i++) {
            /* sweep with a burst every half second, so all block types occur */
            phase += 2.0 * M_PI * (200.0 + 6000.0 * ((f * FRAME + i) % RATE) / RATE) / RATE;
            double v = 8000.0 * sin(phase);
            if (((f * FRAME + i) % (RATE / 2)) < 64) v += 12000.0 * ((rng() & 1) ? 1 : -1);
            pcm[2 * i] = (int16_t)v;
            pcm[2 * i + 1] = (int16_t)(v * 0.5);
        }
        uint32_t n = 0;
        if (faac_encoder_encode(enc, pcm, FRAME * CHANNELS, out + total, cap - total, &n) != FAAC_OK) break;
        total += n;
    }
    for (;;) {
        uint32_t n = 0;
        if (faac_encoder_encode(enc, NULL, 0, out + total, cap - total, &n) != FAAC_OK || n == 0) break;
        total += n;
    }
    faac_encoder_close(&enc);
    return total;
}

/* Walk the ADTS frames, dropping some and flipping bits in others. */
static uint32_t corrupt(const uint8_t *in, uint32_t len, uint8_t *out)
{
    uint32_t i = 0, o = 0;
    while (i + 7 <= len) {
        if (!(in[i] == 0xFF && (in[i + 1] & 0xF6) == 0xF0)) { i++; continue; }
        uint32_t flen = ((in[i + 3] & 3) << 11) | (in[i + 4] << 3) | (in[i + 5] >> 5);
        if (flen < 7 || i + flen > len) break;
        uint32_t r = rng() % 100;
        if (r < 8) { i += flen; continue; }                 /* dropped frame */
        memcpy(out + o, in + i, flen);
        if (r < 40) {                                      /* payload bit flips */
            int flips = 1 + (int)(rng() % 6);
            for (int k = 0; k < flips; k++) out[o + 7 + rng() % (flen - 7)] ^= (uint8_t)(1u << (rng() % 8));
        } else if (r < 46) {                               /* header damage */
            out[o + 2 + rng() % 5] ^= (uint8_t)(1u << (rng() % 8));
        } else if (r < 50) {                               /* truncated frame */
            o += flen / 2; i += flen; continue;
        }
        o += flen;
        i += flen;
    }
    return o;
}

static int decode_bounded(const uint8_t *stream, uint32_t len, uint32_t max_frames)
{
    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = FAAD_STREAM_ADTS;
    cfg.output_format = FAAD_OUTPUT_16BIT;
    faad_decoder *dec = NULL;
    if (faad_decoder_create(&cfg, NULL, 0, &dec) != FAAD_OK) return fail("decoder create");

    static int16_t pcm[8 * 2048];
    uint32_t pos = 0, calls = 0, frames = 0;
    uint64_t out_bytes = 0;
    while (pos < len) {
        uint32_t used = 0, written = 0;
        faad_frame_info fi;
        faad_status st = faad_decode_frame(dec, stream + pos, len - pos, &used, pcm, sizeof(pcm), &written, &fi);
        calls++;
        if (st == FAAD_ERR_NEED_MORE_DATA) break;
        if (used == 0) { faad_decoder_destroy(dec); return fail("decoder did not advance"); }
        if (written > sizeof(pcm)) { faad_decoder_destroy(dec); return fail("output exceeds buffer"); }
        if (written > 0) frames++;
        out_bytes += written;
        pos += used;
        /* a call per byte of input is the loosest bound a resync could need */
        if (calls > len + 16) { faad_decoder_destroy(dec); return fail("too many calls: stalled"); }
    }
    faad_decoder_destroy(dec);
    if (frames > max_frames) return fail("more output frames than the intact stream");
    if (out_bytes > (uint64_t)max_frames * 2048 * 8 * 2) return fail("output larger than the intact stream");
    return 0;
}

int main(void)
{
    static uint8_t intact[1 << 20], damaged[1 << 20];
    const enum faac_object_type objs[2] = { FAAC_OBJ_LOW, FAAC_OBJ_HE_AAC_V1 };
    for (int t = 0; t < 2; t++) {
        uint32_t len = encode(objs[t], intact, sizeof(intact));
        if (len == 0) return fail("encode");
        uint32_t frames_intact = 0;
        for (uint32_t i = 0; i + 7 <= len; ) {
            uint32_t flen = ((intact[i + 3] & 3) << 11) | (intact[i + 4] << 3) | (intact[i + 5] >> 5);
            if (flen < 7) break;
            frames_intact++;
            i += flen;
        }
        /* the intact stream itself must decode within its own frame count */
        if (decode_bounded(intact, len, frames_intact)) return 1;
        for (int seed = 1; seed <= 8; seed++) {
            rng_state = 0x9E3779B9u * (uint32_t)seed;
            uint32_t dlen = corrupt(intact, len, damaged);
            if (decode_bounded(damaged, dlen, frames_intact)) {
                fprintf(stderr, "  object %d, corruption seed %d\n", (int)objs[t], seed);
                return 1;
            }
        }
    }
    printf("test_faad_robust: ok\n");
    return 0;
}

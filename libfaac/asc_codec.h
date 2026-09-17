/*
 * Shared AudioSpecificConfig (ISO/IEC 14496-3 1.6) codec.
 *
 * Parses/builds the core-LC-plus-implicit-SBR/PS-sync-extension bitstream
 * that faac's own encoder emits (see SbrContextGetASC() in sbr.c, which
 * calls asc_codec_build() directly) and that libfaad's decoder and libfaam's
 * demuxer/muxer both need to independently read back or reconstruct.
 *
 * Header-only with static-inline internal linkage on purpose: libfaac,
 * libfaad and libfaam each compile their own private copy of this code from
 * source. No library links against another here; only CLI frontends combine
 * them (see libfaac/meson.build's libfaac_common_static comment for the
 * general pattern this deliberately does NOT use, and why).
 */

#ifndef FAAC_ASC_CODEC_H
#define FAAC_ASC_CODEC_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define ASC_SYNC_EXTENSION_SBR 0x2b7u
#define ASC_SYNC_EXTENSION_PS  0x548u

/* ISO/IEC 14496-3 Table 1.16 sampling_frequency_index. */
static const uint32_t asc_codec_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

static inline uint8_t asc_codec_sr_idx(uint32_t rate)
{
    for (uint8_t i = 0; i < 13; i++) {
        if (asc_codec_sample_rates[i] == rate) return i;
    }
    return 4; /* default 44100 */
}

typedef struct {
    uint8_t  object_type;     /* core AOT (2 = AAC-LC) */
    uint32_t sample_rate;     /* core sample rate, Hz */
    uint8_t  num_channels;
    bool     sbr_present;
    uint32_t sbr_sample_rate; /* post-SBR (extension) rate, Hz; == 2*sample_rate if not explicitly signaled */
    bool     ps_present;
} AscInfo;

typedef struct { const uint8_t *buf; uint32_t len_bits; uint32_t pos; } asc_bitreader;

static inline uint32_t asc_br_get(asc_bitreader *br, uint32_t n)
{
    uint32_t val = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bitpos = br->pos + i;
        uint32_t bit = 0;
        if (bitpos < br->len_bits) {
            bit = (br->buf[bitpos >> 3] >> (7 - (bitpos & 7))) & 1;
        }
        val = (val << 1) | bit;
    }
    br->pos += n;
    return val;
}

static inline uint32_t asc_br_remaining(const asc_bitreader *br)
{
    return br->pos < br->len_bits ? br->len_bits - br->pos : 0;
}

static inline uint32_t asc_parse_sample_rate(asc_bitreader *br)
{
    uint32_t idx = asc_br_get(br, 4);
    return idx == 15 ? asc_br_get(br, 24) : asc_codec_sample_rates[idx];
}

/*
 * Parses aot/sample_rate/channels, then either the "explicit" nested-AOT
 * HE-AAC form (top-level AOT==5) or, for a plain-LC ASC, walks
 * GASpecificConfig (frameLengthFlag/dependsOnCoreCoder[/coreCoderDelay]/
 * extensionFlag[/extensionFlag3]) before scanning for the "implicit"
 * trailing 0x2b7/0x548 sync-extension -- the form real MP4/M4A muxers
 * (including this project's own) actually write. asc_len is in bytes.
 *
 * PCE parsing (needed only when channelConfiguration==0, i.e. a
 * program_config_element carries the real channel layout) is intentionally
 * not implemented here; num_channels is left 0 and the implicit-extension
 * scan is skipped in that case, matching each caller's own prior behavior.
 */
static inline void asc_codec_parse(const uint8_t *buf, uint32_t len, AscInfo *out)
{
    memset(out, 0, sizeof(*out));
    if (!buf || len < 2) return;

    asc_bitreader br = { buf, len * 8, 0 };

    uint32_t aot = asc_br_get(&br, 5);
    if (aot == 31) aot = 32 + asc_br_get(&br, 6);
    out->object_type = (uint8_t)aot;

    out->sample_rate = asc_parse_sample_rate(&br);
    out->num_channels = (uint8_t)asc_br_get(&br, 4);

    if (aot == 5) {
        /* Explicit form: SBR config directly nested at the top level. */
        out->sbr_present = true;
        out->sbr_sample_rate = asc_parse_sample_rate(&br);
        uint32_t real_aot = asc_br_get(&br, 5);
        if (real_aot == 31) real_aot = 32 + asc_br_get(&br, 6);
        out->object_type = (uint8_t)real_aot;
        return;
    }

    out->sbr_sample_rate = out->sample_rate * 2;

    if (aot != 2) return; /* GASpecificConfig only walked here for AAC-LC */

    asc_br_get(&br, 1); /* frameLengthFlag */
    if (asc_br_get(&br, 1)) {
        asc_br_get(&br, 14); /* coreCoderDelay, iff dependsOnCoreCoder */
    }
    bool extension_flag = asc_br_get(&br, 1) != 0;
    if (out->num_channels == 0) return; /* PCE follows; not handled here */
    if (extension_flag) {
        asc_br_get(&br, 1); /* extensionFlag3 (reserved for AOT LC) */
    }

    if (asc_br_remaining(&br) >= 16 && asc_br_get(&br, 11) == ASC_SYNC_EXTENSION_SBR) {
        uint32_t ext_aot = asc_br_get(&br, 5);
        if (ext_aot == 5) {
            out->sbr_present = true;
            asc_br_get(&br, 1); /* sbrPresentFlag: implied by the sync-extension itself */
            out->sbr_sample_rate = asc_parse_sample_rate(&br);
            if (asc_br_remaining(&br) >= 12 && asc_br_get(&br, 11) == ASC_SYNC_EXTENSION_PS) {
                out->ps_present = asc_br_get(&br, 1) != 0;
            }
        }
    }
}

typedef struct { uint8_t *buf; uint32_t cap_bits; uint32_t pos; } asc_bitwriter;

static inline void asc_bw_put(asc_bitwriter *bw, uint32_t val, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bitpos = bw->pos + i;
        if (bitpos >= bw->cap_bits) break;
        uint32_t bit = (val >> (n - 1 - i)) & 1;
        uint8_t *byte = &bw->buf[bitpos >> 3];
        uint8_t mask = (uint8_t)(1u << (7 - (bitpos & 7)));
        if (bit) *byte |= mask; else *byte &= (uint8_t)~mask;
    }
    bw->pos += n;
}

typedef struct {
    uint8_t object_type; /* core AOT, e.g. 2 = AAC-LC */
    uint8_t sr_idx;      /* Table 1.16 index for the core rate */
    uint8_t channels;
    bool    sbr_present;
    uint8_t sbr_sr_idx;  /* Table 1.16 index for the post-SBR (output) rate; ignored if !sbr_present */
    bool    ps_signaled; /* emit the PS sync-extension block at all (only meaningful if sbr_present) */
    bool    ps_present;  /* psPresentFlag value inside that block, if ps_signaled */
} AscBuildInfo;

/*
 * Builds a core-LC ASC, with an implicit SBR sync-extension (and, if
 * ps_signaled, a nested PS sync-extension) appended when sbr_present is
 * set. Returns the number of bytes written, or 0 if out_cap is too small.
 */
static inline uint32_t asc_codec_build(const AscBuildInfo *info, uint8_t *out, uint32_t out_cap)
{
    if (!info || !out || out_cap < 2) return 0;

    memset(out, 0, out_cap);
    asc_bitwriter bw = { out, out_cap * 8, 0 };

    uint8_t obj = info->object_type;
    if (obj >= 32) {
        /* AOT-escape form, mirroring asc_codec_parse()'s aot==31 read above. */
        asc_bw_put(&bw, 31, 5);
        asc_bw_put(&bw, obj - 32, 6);
    } else {
        asc_bw_put(&bw, obj, 5);
    }
    asc_bw_put(&bw, info->sr_idx, 4);
    asc_bw_put(&bw, info->channels & 0x0F, 4);
    asc_bw_put(&bw, 0, 3); /* frameLengthFlag, dependsOnCoreCoder, extensionFlag */

    if (info->sbr_present) {
        asc_bw_put(&bw, ASC_SYNC_EXTENSION_SBR, 11);
        asc_bw_put(&bw, 5, 5); /* extensionAudioObjectType = SBR */
        asc_bw_put(&bw, 1, 1); /* sbrPresentFlag */
        asc_bw_put(&bw, info->sbr_sr_idx, 4);
        if (info->ps_signaled) {
            asc_bw_put(&bw, ASC_SYNC_EXTENSION_PS, 11);
            asc_bw_put(&bw, info->ps_present ? 1 : 0, 1);
        }
    }

    return (bw.pos + 7) / 8;
}

#endif /* FAAC_ASC_CODEC_H */

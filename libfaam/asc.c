/*
 * AudioSpecificConfig (ASC) Utilities for libfaam
 *
 * Implements the ISO/IEC 14496-3 "implicit backward-compatible" HE-AAC/PS
 * signaling: a plain AAC-LC AudioSpecificConfig followed by an SBR/PS
 * sync-extension. This is the form used inside MP4/M4A (as opposed to the
 * "explicit" top-level audioObjectType==5 form used e.g. on raw ADTS
 * streams, which libfaad's independent ASC parser handles).
 */

#include "libfaam_internal.h"

/* ISO/IEC 14496-3 Table 1.16 sampling_frequency_index */
static const uint32_t sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

#define SYNC_EXTENSION_SBR 0x2b7u
#define SYNC_EXTENSION_PS  0x548u

typedef struct {
    const uint8_t *buf;
    uint32_t len_bits;
    uint32_t pos;
} bitreader;

static uint32_t br_get(bitreader *br, uint32_t n)
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

static uint32_t br_remaining(const bitreader *br)
{
    return br->pos < br->len_bits ? br->len_bits - br->pos : 0;
}

static uint32_t parse_sample_rate(bitreader *br)
{
    uint32_t idx = br_get(br, 4);
    return idx == 15 ? br_get(br, 24) : sample_rates[idx];
}

typedef struct {
    uint8_t *buf;
    uint32_t cap_bits;
    uint32_t pos;
} bitwriter;

static void bw_put(bitwriter *bw, uint32_t val, uint32_t n)
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

static uint8_t sample_rate_index(uint32_t rate)
{
    for (uint8_t i = 0; i < 13; i++) {
        if (sample_rates[i] == rate) return i;
    }
    return 4; /* Default 44100 */
}

faam_status faam_asc_parse(const uint8_t *asc_buf, uint32_t asc_len, faam_asc_info *out_info)
{
    if (!asc_buf || asc_len < 2 || !out_info) return FAAM_ERR_INVALID_ARG;

    memset(out_info, 0, sizeof(*out_info));
    bitreader br = { asc_buf, asc_len * 8, 0 };

    uint32_t aot = br_get(&br, 5);
    if (aot == 31) aot = 32 + br_get(&br, 6);
    out_info->object_type = (uint8_t)aot;

    out_info->sample_rate = parse_sample_rate(&br);
    out_info->channels = (uint8_t)br_get(&br, 4);

    /* Trailing SBR/PS sync-extension, as written by faam_asc_build() below. */
    if (br_remaining(&br) >= 16 && br_get(&br, 11) == SYNC_EXTENSION_SBR) {
        uint32_t ext_aot = br_get(&br, 5);
        if (ext_aot == 5) {
            out_info->sbr_present = true;
            (void)br_get(&br, 1); /* sbrPresentFlag, always 1 here */
            (void)parse_sample_rate(&br); /* extensionSamplingFrequency */
            if (br_remaining(&br) >= 11 && br_get(&br, 11) == SYNC_EXTENSION_PS) {
                out_info->ps_present = br_get(&br, 1) != 0;
            }
        }
    }

    return FAAM_OK;
}

faam_status faam_asc_build(const faam_asc_info *info, uint8_t *out_asc, uint32_t asc_cap, uint32_t *out_len)
{
    if (!info || !out_asc || asc_cap < 2 || !out_len) return FAAM_ERR_INVALID_ARG;

    uint8_t obj = info->object_type ? info->object_type : 2; /* Default AAC-LC */
    uint32_t sr = info->sample_rate ? info->sample_rate : 44100;
    uint8_t ch = info->channels ? info->channels : 2;
    uint8_t sr_idx = sample_rate_index(sr);

    memset(out_asc, 0, asc_cap);
    bitwriter bw = { out_asc, asc_cap * 8, 0 };

    bw_put(&bw, obj & 0x1F, 5);
    bw_put(&bw, sr_idx, 4);
    bw_put(&bw, ch & 0x0F, 4);

    if (info->sbr_present) {
        /* Extension sampling frequency runs at (nominally) 2x the core rate,
         * which lands ~3 slots earlier in the descending sample_rates table. */
        uint32_t ext_sr_idx = sr_idx > 3 ? sr_idx - 3 : sr_idx;
        bw_put(&bw, SYNC_EXTENSION_SBR, 11);
        bw_put(&bw, 5, 5); /* extensionAudioObjectType = SBR */
        bw_put(&bw, 1, 1); /* sbrPresentFlag */
        bw_put(&bw, ext_sr_idx, 4);
        if (info->ps_present) {
            bw_put(&bw, SYNC_EXTENSION_PS, 11);
            bw_put(&bw, 1, 1); /* psPresentFlag */
        }
    }

    *out_len = (bw.pos + 7) / 8;
    return FAAM_OK;
}

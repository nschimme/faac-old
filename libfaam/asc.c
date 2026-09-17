/*
 * AudioSpecificConfig (ASC) Utilities for libfaam
 */

#include "libfaam_internal.h"

static const uint32_t sample_rates[] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

faam_status faam_asc_parse(const uint8_t *asc_buf, uint32_t asc_len, faam_asc_info *out_info)
{
    if (!asc_buf || asc_len < 2 || !out_info) return FAAM_ERR_INVALID_ARG;

    memset(out_info, 0, sizeof(*out_info));

    uint32_t bits = ((uint32_t)asc_buf[0] << 24) | ((uint32_t)asc_buf[1] << 16);
    if (asc_len >= 3) bits |= ((uint32_t)asc_buf[2] << 8);
    if (asc_len >= 4) bits |= (uint32_t)asc_buf[3];

    uint8_t obj_type = (bits >> 27) & 0x1F;
    uint8_t sr_idx = (bits >> 23) & 0x0F;

    if (obj_type == 31 && asc_len >= 3) {
        /* Escape object type */
        obj_type = 32 + ((bits >> 21) & 0x3F);
        sr_idx = (bits >> 17) & 0x0F;
    }

    out_info->object_type = obj_type;

    if (sr_idx < 13) {
        out_info->sample_rate = sample_rates[sr_idx];
    } else {
        out_info->sample_rate = 44100;
    }

    uint8_t ch_config = (bits >> 19) & 0x0F;
    out_info->channels = ch_config;

    /* Check SBR / PS presence */
    if (obj_type == 5 || obj_type == 29) {
        out_info->sbr_present = true;
        if (obj_type == 29) out_info->ps_present = true;
    }

    return FAAM_OK;
}

faam_status faam_asc_build(const faam_asc_info *info, uint8_t *out_asc, uint32_t asc_cap, uint32_t *out_len)
{
    if (!info || !out_asc || asc_cap < 2 || !out_len) return FAAM_ERR_INVALID_ARG;

    uint8_t obj = info->object_type ? info->object_type : 2; /* Default AAC-LC */
    uint32_t sr = info->sample_rate ? info->sample_rate : 44100;
    uint8_t ch = info->channels ? info->channels : 2;

    uint8_t sr_idx = 4; /* Default 44100 */
    for (uint8_t i = 0; i < 13; i++) {
        if (sample_rates[i] == sr) {
            sr_idx = i;
            break;
        }
    }

    uint32_t bits = ((uint32_t)(obj & 0x1F) << 27) | ((uint32_t)(sr_idx & 0x0F) << 23) | ((uint32_t)(ch & 0x0F) << 19);

    out_asc[0] = (uint8_t)(bits >> 24);
    out_asc[1] = (uint8_t)(bits >> 16);
    *out_len = 2;

    if (info->sbr_present) {
        /* Extension sampling frequency and object type SBR */
        uint32_t ext_sr_idx = sr_idx > 3 ? sr_idx - 3 : sr_idx;
        uint32_t ext_bits = ((uint32_t)0x2B7 << 11) | ((uint32_t)5 << 6) | ((uint32_t)1 << 5) | ((uint32_t)ext_sr_idx << 1);
        out_asc[2] = (uint8_t)(ext_bits >> 16);
        out_asc[3] = (uint8_t)(ext_bits >> 8);
        out_asc[4] = (uint8_t)ext_bits;
        *out_len = 5;
    }

    return FAAM_OK;
}

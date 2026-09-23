/*
 * AudioSpecificConfig and ADTS Header Parsing
 */

#include "faad_internal.h"
#include "asc_codec.h"

const uint32_t faad_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

faad_status asc_decode(BitReader *bs, AudioSpecificConfig *asc)
{
    /* asc_decode() is always called on a BitReader freshly bits_init()'d over
     * exactly the ASC bytes (see faad_decoder_init()), so the shared codec
     * can parse straight from its underlying buffer. */
    AscInfo info;
    asc_codec_parse(bs->buffer, bs->len, &info);

    memset(asc, 0, sizeof(*asc));
    asc->object_type = (enum faad_object_type)info.object_type;
    asc->sample_rate = info.sample_rate;
    asc->num_channels = info.num_channels;
    asc->is_sbr = info.sbr_present;
    asc->sbr_sample_rate = info.sbr_sample_rate;
    asc->is_ps = info.ps_present;

    /* Set default if AOT == LC */
    if (asc->object_type == FAAD_OBJ_LC || asc->object_type == FAAD_OBJ_HE_AAC_V1 || asc->object_type == FAAD_OBJ_NULL) {
        asc->object_type = FAAD_OBJ_LC;
    }

    return FAAD_OK;
}

faad_status adts_decode_header(BitReader *bs, AudioSpecificConfig *asc, uint32_t *frame_length)
{
    uint32_t sync = bits_get(bs, 12);
    if (sync != 0xFFF) {
        return FAAD_ERR_SYNC_LOST;
    }
    bits_skip(bs, 1);
    bits_skip(bs, 2);
    uint32_t protection_absent = bits_get(bs, 1);

    uint32_t profile = bits_get(bs, 2);
    uint32_t sr_idx = bits_get(bs, 4);
    bits_skip(bs, 1);
    uint32_t channel_config = bits_get(bs, 3);
    bits_skip(bs, 4);

    uint32_t flen = bits_get(bs, 13);
    bits_skip(bs, 11);
    bits_skip(bs, 2);

    if (protection_absent == 0) {
        bits_skip(bs, 16);
    }

    uint32_t min_hdr = (protection_absent == 0) ? 9 : 7;
    if (sr_idx >= 12 || faad_sample_rates[sr_idx] == 0 || flen < min_hdr) {
        return FAAD_ERR_DECODE_FAILED;
    }

    if (asc) {
        memset(asc, 0, sizeof(*asc));
        asc->object_type = (enum faad_object_type)(profile + 1);
        asc->sample_rate = faad_sample_rates[sr_idx];
        asc->num_channels = channel_config;
    }

    if (frame_length) {
        *frame_length = flen;
    }

    return FAAD_OK;
}

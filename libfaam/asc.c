/*
 * AudioSpecificConfig (ASC) Utilities for libfaam
 *
 * Implements the ISO/IEC 14496-3 "implicit backward-compatible" HE-AAC/PS
 * signaling: a plain AAC-LC AudioSpecificConfig followed by an SBR/PS
 * sync-extension. This is the form used inside MP4/M4A (as opposed to the
 * "explicit" top-level audioObjectType==5 form used e.g. on raw ADTS
 * streams, which libfaad's independent ASC parser handles).
 *
 * Thin wrapper over the shared codec in libfaac/asc_codec.h (see that
 * header's comment for why this is a private per-library copy rather than a
 * link dependency on libfaac).
 */

#include "libfaam_internal.h"
#include "asc_codec.h"

faam_status faam_asc_parse(const uint8_t *asc_buf, uint32_t asc_len, faam_asc_info *out_info)
{
    if (!asc_buf || asc_len < 2 || !out_info) return FAAM_ERR_INVALID_ARG;

    AscInfo info;
    asc_codec_parse(asc_buf, asc_len, &info);

    memset(out_info, 0, sizeof(*out_info));
    out_info->object_type = info.object_type;
    out_info->sample_rate = info.sample_rate; /* core rate; see faam_asc_info's own doc comment */
    out_info->channels = info.num_channels;
    out_info->sbr_present = info.sbr_present;
    out_info->ps_present = info.ps_present;

    return FAAM_OK;
}

faam_status faam_asc_build(const faam_asc_info *info, uint8_t *out_asc, uint32_t asc_cap, uint32_t *out_len)
{
    if (!info || !out_asc || asc_cap < 2 || !out_len) return FAAM_ERR_INVALID_ARG;

    uint32_t sr = info->sample_rate ? info->sample_rate : 44100;
    uint8_t sr_idx = asc_codec_sr_idx(sr);

    AscBuildInfo build = {0};
    build.object_type = info->object_type ? info->object_type : 2; /* Default AAC-LC */
    build.sr_idx = sr_idx;
    build.channels = info->channels ? info->channels : 2;
    build.sbr_present = info->sbr_present;
    if (info->sbr_present) {
        /* We only know the nominal core rate here, not a real independently
         * negotiated output rate, so approximate the (nominally 2x) extension
         * rate: the sample-rate table is arranged so that lands ~3 slots
         * earlier for every standard rate pair. */
        build.sbr_sr_idx = sr_idx > 3 ? sr_idx - 3 : sr_idx;
        build.ps_signaled = info->ps_present;
        build.ps_present = info->ps_present;
    }

    *out_len = asc_codec_build(&build, out_asc, asc_cap);
    return *out_len ? FAAM_OK : FAAM_ERR_INVALID_ARG;
}

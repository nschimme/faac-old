/*
 * FAAD3 Engine Implementation
 */

#include "faad_internal.h"

#ifdef FAAD_STATS
#include <stdio.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

FAADAPI faad_status faad_get_library_info(faad_library_info *out)
{
    if (!out || out->struct_size < sizeof(faad_library_info)) {
        return FAAD_ERR_INVALID_ARGUMENT;
    }

    out->struct_size = sizeof(faad_library_info);
    out->version = "3.0.0";
    out->copyright = "Copyright (C) 2026 FAAD Project";
    out->max_channels = MAX_CHANNELS;
#ifndef FAAD_DISABLE_SBR
    out->sbr_supported = true;
#else
    out->sbr_supported = false;
#endif
#ifndef FAAD_DISABLE_PS
    out->ps_supported = true;
#else
    out->ps_supported = false;
#endif

    return FAAD_OK;
}

FAADAPI faad_status faad_config_init(faad_config *cfg, uint32_t caller_size)
{
    if (!cfg || caller_size < sizeof(faad_config)) {
        return FAAD_ERR_INVALID_ARGUMENT;
    }
    memset(cfg, 0, sizeof(faad_config));
    cfg->struct_size = sizeof(faad_config);
    cfg->stream_format = FAAD_STREAM_ADTS;
    cfg->output_format = FAAD_OUTPUT_16BIT;
    cfg->downmix_mode = FAAD_DOWNMIX_NONE;
    return FAAD_OK;
}

FAADAPI faad_status faad_get_state_size(const faad_config *cfg, uint32_t *state_bytes_out)
{
    (void)cfg;
    if (!state_bytes_out) return FAAD_ERR_INVALID_ARGUMENT;
    *state_bytes_out = (uint32_t)sizeof(faad_decoder);
    return FAAD_OK;
}

static void faad_init_global_tables_impl(void)
{
    extern void init_dequant_tables(void);
    extern void init_huffman_luts(void);
    extern void init_windows(void);
    extern void init_qmf_twiddles(void);

    init_dequant_tables();
    init_huffman_luts();
    init_windows();
    init_qmf_twiddles();
}

#if defined(_WIN32)
static BOOL CALLBACK faad_init_global_tables_cb(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)param; (void)ctx;
    faad_init_global_tables_impl();
    return TRUE;
}

void faad_init_global_tables(void)
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, faad_init_global_tables_cb, NULL, NULL);
}
#else
void faad_init_global_tables(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, faad_init_global_tables_impl);
}
#endif

FAADAPI faad_status faad_decoder_init(void *mem_buf, uint32_t mem_size,
                                      const faad_config *cfg,
                                      const uint8_t *asc_buf, uint32_t asc_len,
                                      faad_decoder **out_dec)
{
    if (!mem_buf || mem_size < sizeof(faad_decoder) || !out_dec) {
        return FAAD_ERR_INVALID_ARGUMENT;
    }

    faad_init_global_tables();

    faad_decoder *dec = (faad_decoder *)mem_buf;
    memset(dec, 0, sizeof(faad_decoder));

    if (cfg) {
        dec->config = *cfg;
    } else {
        faad_config_init(&dec->config, sizeof(faad_config));
    }

    dec->pns_seed = 0x12345678;

    if (asc_buf && asc_len > 0) {
        BitReader bs;
        bits_init(&bs, asc_buf, asc_len);
        faad_status st = asc_decode(&bs, &dec->asc);
        if (st != FAAD_OK) return st;

        dec->num_channels = dec->asc.num_channels ? dec->asc.num_channels : 2;
        dec->core_sample_rate = dec->asc.sample_rate ? dec->asc.sample_rate : 44100;
        dec->sample_rate = dec->asc.is_sbr ? dec->asc.sbr_sample_rate : dec->core_sample_rate;
        dec->frame_samples = dec->asc.is_sbr ? 2048 : 1024;
        dec->asc_parsed = true;
    } else {
        dec->num_channels = 2;
        dec->sample_rate = 44100;
        dec->core_sample_rate = 44100;
        dec->frame_samples = 1024;
    }

    *out_dec = dec;
    return FAAD_OK;
}

FAADAPI faad_status faad_decoder_create(const faad_config *cfg,
                                        const uint8_t *asc_buf, uint32_t asc_len,
                                        faad_decoder **out_dec)
{
    if (!out_dec) return FAAD_ERR_INVALID_ARGUMENT;
    *out_dec = NULL;

    uint32_t state_size = 0;
    faad_get_state_size(cfg, &state_size);

    void *mem = calloc(1, state_size);
    if (!mem) return FAAD_ERR_INSUFFICIENT_MEM;

    faad_status st = faad_decoder_init(mem, state_size, cfg, asc_buf, asc_len, out_dec);
    if (st != FAAD_OK) {
        free(mem);
        return st;
    }

    (*out_dec)->is_heap_allocated = true;
    return FAAD_OK;
}

#ifdef FAAD_STATS
static void faad_print_stats(const struct faad_decoder *dec)
{
    const FaadDecStats *s = &dec->stats;
    if (s->totalFrames == 0) return;

    double tns_pct = s->icsCount > 0 ? 100.0 * s->tnsActiveFrames / s->icsCount : 0.0;
    double sbr_pct = s->totalFrames > 0 ? 100.0 * s->sbrActiveFrames / s->totalFrames : 0.0;
    double short_pct = s->icsCount > 0 ? 100.0 * s->shortBlockIcsCount / s->icsCount : 0.0;
    double pad_avg = s->fillElementCount > 0 ? (double)s->fillElementPadBitsSum / s->fillElementCount : 0.0;

    fprintf(stderr, "\n--- Decoder Diagnostics ---\n");
    fprintf(stderr, " Frames              : %u (non-END termination: %u)\n",
            s->totalFrames, s->nonEndTermination);
    fprintf(stderr, " Elements            : SCE=%u CPE=%u CCE=%u LFE=%u DSE=%u PCE=%u FIL=%u END=%u\n",
            s->elementCounts[0], s->elementCounts[1], s->elementCounts[2], s->elementCounts[3],
            s->elementCounts[4], s->elementCounts[5], s->elementCounts[6], s->elementCounts[7]);
    if (s->minChannels == s->maxChannels) {
        fprintf(stderr, " Channels            : %u (constant, 0 changes)\n", s->minChannels);
    } else {
        fprintf(stderr, " Channels            : varied %u-%u (%u changes)\n",
                s->minChannels, s->maxChannels, s->channelCountChanges);
    }
    fprintf(stderr, " TNS                 : active in %u/%u ics (%.1f%%)\n",
            s->tnsActiveFrames, s->icsCount, tns_pct);
    fprintf(stderr, " SBR                 : active in %u/%u frames (%.1f%%)\n",
            s->sbrActiveFrames, s->totalFrames, sbr_pct);
    fprintf(stderr, " Short blocks        : %u/%u ics (%.1f%%)\n",
            s->shortBlockIcsCount, s->icsCount, short_pct);

    fprintf(stderr, " Huffman escapes     : ");
    bool any_esc = false;
    for (int b = 1; b <= 12; b++) {
        if (s->huffEscapeHits[b] > 0) {
            fprintf(stderr, "%sbook%02u=%u", any_esc ? " " : "", b, s->huffEscapeHits[b]);
            any_esc = true;
        }
    }
    if (!any_esc) fprintf(stderr, "none");
    fprintf(stderr, " (table misses: %u)\n", s->huffEscapeMisses);

    fprintf(stderr, " ESCBOOK magnitude   : %u values needed the >=16 escape path\n",
            s->escbookMagnitudeEscapes);
    fprintf(stderr, " Fill-elt pad-align  : %u elements, avg %.1f bits, max %u bits\n",
            s->fillElementCount, pad_avg, s->fillElementMaxPad);
    fprintf(stderr, " Error concealment   : %u frames\n", s->errorConcealmentFrames);
    fprintf(stderr, "---------------------------\n");
}
#endif

FAADAPI void faad_decoder_destroy(faad_decoder *dec)
{
    if (!dec) return;
#ifdef FAAD_STATS
    faad_print_stats(dec);
#endif
    if (dec->is_heap_allocated) {
        free(dec);
    }
}

FAADAPI faad_status faad_decoder_get_info(const faad_decoder *dec, faad_stream_info *out_info)
{
    if (!dec || !out_info) return FAAD_ERR_INVALID_ARGUMENT;

    out_info->sample_rate = dec->sample_rate;
    out_info->channels = dec->num_channels;
    out_info->object_type = dec->asc.is_sbr ? FAAD_OBJ_HE_AAC_V1 : FAAD_OBJ_LC;
    out_info->delay_samples = dec->asc.is_sbr ? 3041 : 1024;

    return FAAD_OK;
}

FAADAPI faad_status faad_decoder_flush(faad_decoder *dec)
{
    if (!dec) return FAAD_ERR_INVALID_ARGUMENT;

    memset(dec->overlap, 0, sizeof(dec->overlap));
    memset(dec->sbr, 0, sizeof(dec->sbr));
    memset(&dec->ps, 0, sizeof(dec->ps));
    dec->sbr_present = false;
    dec->ps_present = false;

    return FAAD_OK;
}

FAADAPI faad_status faad_decode_frame(faad_decoder *dec,
                                      const uint8_t *in_buf, uint32_t in_bytes,
                                      uint32_t *bytes_consumed,
                                      void *out_pcm, uint32_t out_cap_bytes,
                                      uint32_t *bytes_written,
                                      faad_frame_info *frame_info)
{
    if (!dec || !in_buf || !bytes_consumed || !out_pcm || !bytes_written) {
        return FAAD_ERR_INVALID_ARGUMENT;
    }

    if (in_bytes == 0) {
        return FAAD_ERR_NEED_MORE_DATA;
    }

#ifdef FAAD_STATS
    dec->stats.totalFrames++;
#endif

    memset(dec->spec, 0, sizeof(dec->spec));

    BitReader bs;
    bits_init(&bs, in_buf, in_bytes);

    uint32_t adts_frame_len = 0;
    bool decode_success = true;
    if (dec->config.stream_format == FAAD_STREAM_ADTS) {
        faad_status st = adts_decode_header(&bs, &dec->asc, &adts_frame_len);
        if (st != FAAD_OK) {
            decode_success = false;
        } else if (adts_frame_len > in_bytes) {
            return FAAD_ERR_NEED_MORE_DATA;
        } else {
            dec->num_channels = dec->asc.num_channels ? dec->asc.num_channels : 2;
            dec->sample_rate = dec->asc.sample_rate ? dec->asc.sample_rate : 44100;
            dec->core_sample_rate = dec->sample_rate; /* adts_decode_header() doesn't detect SBR */
            bs.len = adts_frame_len;
        }
    } else {
        adts_frame_len = in_bytes;
    }

    uint32_t ch_idx = 0;
    ICSInfo ics_list[MAX_CHANNELS];
    memset(ics_list, 0, sizeof(ics_list));

#ifdef FAAD_STATS
    bool saw_end = false;
#endif
    if (decode_success) {
        while (bits_get_consumed(&bs) + 3 <= bs.len * 8 && ch_idx < MAX_CHANNELS) {
            uint32_t syntax_id = bits_get(&bs, 3);
#ifdef FAAD_STATS
            dec->stats.elementCounts[syntax_id]++;
#endif
            if (syntax_id == ID_END) {
#ifdef FAAD_STATS
                saw_end = true;
#endif
                break;
            } else if (syntax_id == ID_SCE || syntax_id == ID_LFE) {
                decode_sce(&bs, dec, &ics_list[ch_idx], ch_idx);
                dequantize_spectrum(&ics_list[ch_idx], dec->spec[ch_idx]);
                apply_pns(&ics_list[ch_idx], dec->spec[ch_idx], &dec->pns_seed);
                apply_tns(&ics_list[ch_idx], dec->spec[ch_idx]);
                ch_idx += 1;
            } else if (syntax_id == ID_CPE) {
                if (ch_idx + 1 >= MAX_CHANNELS) break;
                CPEInfo cpe;
                memset(&cpe, 0, sizeof(cpe));
                decode_cpe(&bs, dec, &cpe, ch_idx);
                ics_list[ch_idx] = cpe.ics[0];
                ics_list[ch_idx + 1] = cpe.ics[1];

                dequantize_spectrum(&cpe.ics[0], dec->spec[ch_idx]);
                dequantize_spectrum(&cpe.ics[1], dec->spec[ch_idx + 1]);
                apply_pns(&cpe.ics[0], dec->spec[ch_idx], &dec->pns_seed);
                apply_pns(&cpe.ics[1], dec->spec[ch_idx + 1], &dec->pns_seed);
                apply_ms_stereo(&cpe, dec->spec[ch_idx], dec->spec[ch_idx + 1]);
                apply_is_stereo(&cpe, dec->spec[ch_idx], dec->spec[ch_idx + 1]);
                apply_tns(&cpe.ics[0], dec->spec[ch_idx]);
                apply_tns(&cpe.ics[1], dec->spec[ch_idx + 1]);

                if (dec->config.downmix_mode == FAAD_DOWNMIX_MONO && cpe.common_window) {
                    apply_freq_downmix_mono(dec->spec[ch_idx], dec->spec[ch_idx + 1]);
                    memset(dec->spec[ch_idx + 1], 0, sizeof(float) * FRAME_LEN_LONG);
                    ch_idx += 1;
                } else {
                    ch_idx += 2;
                }
            } else if (syntax_id == ID_DSE) {
                decode_dse(&bs);
            } else if (syntax_id == ID_PCE) {
                decode_pce(&bs, dec);
            } else if (syntax_id == ID_FIL) {
                uint32_t count = bits_get(&bs, 4);
                if (count == 15) count += bits_get(&bs, 8) - 1;
                uint32_t fill_end = bits_get_consumed(&bs) + count * 8;
                uint32_t ext_type = bits_get(&bs, 4);
                if (ext_type == SBR_EXTENSION_DATA || ext_type == SBR_EXTENSION_DATA_CRC) {
                    uint32_t elem_type = (ch_idx > 1) ? ID_CPE : ID_SCE;
                    sbr_decode_extension(dec, &bs, (ch_idx > 0) ? (ch_idx - 1) : 0, elem_type);
                    /* libfaac's encoder (SbrWrite() in sbr_bitstream.c) pads
                     * the SBR payload out to this fill element's declared
                     * byte count; sbr_decode_extension() doesn't consume
                     * that trailing padding itself, so force-align here
                     * rather than let the outer loop misread it as the next
                     * syntax element. */
                    uint32_t consumed = bits_get_consumed(&bs);
#ifdef FAAD_STATS
                    dec->stats.fillElementCount++;
#endif
                    if (consumed < fill_end) {
                        uint32_t pad = fill_end - consumed;
                        bits_skip(&bs, pad);
#ifdef FAAD_STATS
                        dec->stats.fillElementPadBitsSum += pad;
                        if (pad > dec->stats.fillElementMaxPad) {
                            dec->stats.fillElementMaxPad = pad;
                        }
#endif
                    }
                } else {
                    bits_skip(&bs, (count - 1) * 8 + 4);
                }
            }
        }
#ifdef FAAD_STATS
        if (!saw_end) {
            dec->stats.nonEndTermination++;
        }
#endif
    }

    /* Error Concealment & Fade-Out Fading Mechanism */
    if (decode_success && ch_idx > 0) {
        dec->consecutive_errors = 0;
        memcpy(dec->prev_spec, dec->spec, sizeof(dec->spec));
        dec->num_channels = ch_idx;
#ifdef FAAD_STATS
        if (!dec->stats.haveLastChannels) {
            dec->stats.haveLastChannels = true;
            dec->stats.lastChannels = ch_idx;
            dec->stats.minChannels = ch_idx;
            dec->stats.maxChannels = ch_idx;
        } else {
            if (ch_idx != dec->stats.lastChannels) {
                dec->stats.channelCountChanges++;
                dec->stats.lastChannels = ch_idx;
            }
            if (ch_idx < dec->stats.minChannels) dec->stats.minChannels = ch_idx;
            if (ch_idx > dec->stats.maxChannels) dec->stats.maxChannels = ch_idx;
        }
#endif
    } else {
#ifdef FAAD_STATS
        dec->stats.errorConcealmentFrames++;
#endif
        dec->consecutive_errors++;
        float fade = 0.0f;
        if (dec->consecutive_errors <= 5) {
            fade = powf(0.8f, (float)dec->consecutive_errors);
        }
        for (uint32_t c = 0; c < dec->num_channels; c++) {
            for (int i = 0; i < FRAME_LEN_LONG; i++) {
                dec->spec[c][i] = dec->prev_spec[c][i] * fade;
            }
        }
    }

    float pcm_float[MAX_CHANNELS * FRAME_LEN_LONG];
    for (uint32_t c = 0; c < dec->num_channels; c++) {
        imdct_and_window(dec, c, &ics_list[c], dec->spec[c], pcm_float + c * FRAME_LEN_LONG);
    }

    float pcm_final[MAX_CHANNELS * 2048];
    if (dec->asc.is_sbr || dec->sbr_present) {
        dec->frame_samples = 2048;
        sbr_apply(dec, dec->num_channels, pcm_float, pcm_final);
    } else {
        dec->frame_samples = 1024;
        memcpy(pcm_final, pcm_float, dec->num_channels * 1024 * sizeof(float));
    }

    uint32_t total_samples = dec->frame_samples * dec->num_channels;
    uint32_t required_bytes = total_samples * ((dec->config.output_format == FAAD_OUTPUT_16BIT) ? 2 : 4);

    if (out_cap_bytes < required_bytes) {
        return FAAD_ERR_OUTPUT_TOO_SMALL;
    }

    if (dec->config.output_format == FAAD_OUTPUT_16BIT) {
        int16_t *out_int16 = (int16_t *)out_pcm;
        for (uint32_t i = 0; i < dec->frame_samples; i++) {
            for (uint32_t c = 0; c < dec->num_channels; c++) {
                float val = pcm_final[c * dec->frame_samples + i] * 32768.0f;
                if (val > 32767.0f) val = 32767.0f;
                if (val < -32768.0f) val = -32768.0f;
                out_int16[i * dec->num_channels + c] = (int16_t)val;
            }
        }
    } else {
        float *out_f32 = (float *)out_pcm;
        for (uint32_t i = 0; i < dec->frame_samples; i++) {
            for (uint32_t c = 0; c < dec->num_channels; c++) {
                out_f32[i * dec->num_channels + c] = pcm_final[c * dec->frame_samples + i];
            }
        }
    }

    *bytes_consumed = adts_frame_len;
    *bytes_written = required_bytes;

    bool sbr_active = dec->sbr_present || dec->asc.is_sbr;
#ifdef FAAD_STATS
    if (sbr_active) {
        dec->stats.sbrActiveFrames++;
    }
#endif

    if (frame_info) {
        frame_info->sample_rate = dec->sample_rate;
        frame_info->samples_per_ch = dec->frame_samples;
        frame_info->channels = (uint8_t)dec->num_channels;
        frame_info->sbr_active = sbr_active;
        frame_info->ps_active = dec->ps_present || dec->asc.is_ps;
    }

    return FAAD_OK;
}

FAADAPI const char *faad_strerror(faad_status status)
{
    switch (status) {
        case FAAD_OK:                   return "Success";
        case FAAD_ERR_INVALID_ARGUMENT: return "Invalid argument";
        case FAAD_ERR_UNSUPPORTED:      return "Unsupported configuration";
        case FAAD_ERR_INSUFFICIENT_MEM: return "Insufficient memory allocated";
        case FAAD_ERR_OUTPUT_TOO_SMALL: return "Output buffer too small";
        case FAAD_ERR_NEED_MORE_DATA:   return "Need more input data";
        case FAAD_ERR_DECODE_FAILED:    return "Decoding failed";
        case FAAD_ERR_SYNC_LOST:        return "Lost syncword alignment";
        default:                        return "Unknown status";
    }
}

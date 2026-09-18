/*
 * Spectral Band Replication (SBR) & Parametric Stereo (PS) Decoder Engine
 */

#include "faad_internal.h"
#include "sfb_tables.h"
#include "sbr_tables.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int sbr_clamp_int(int val, int min_val, int max_val) {
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static int sbr_compute_num_bands(uint32_t sample_rate, uint32_t start_freq, uint32_t stop_freq) {
    if (sample_rate == 0) sample_rate = 44100;
    int sr_row = (sample_rate <= 16000) ? 0 : (sample_rate <= 22050) ? 1 : (sample_rate <= 24000) ? 2 : (sample_rate <= 32000) ? 3 : (sample_rate <= 64000) ? 4 : 5;
    int temp = (sample_rate < 32000) ? 3000 : (sample_rate < 64000) ? 4000 : 5000;
    int start_min = ((temp << 7) + (int)(sample_rate >> 1)) / (int)sample_rate;
    int kx = sbr_clamp_int(start_min + sbr_offset[sr_row][start_freq & 15], 1, 63);
    int k2 = 64;
    if (stop_freq < 14) {
        int stop_min = ((temp << 8) + (int)(sample_rate >> 1)) / (int)sample_rate;
        k2 = sbr_clamp_int(stop_min + sbr_offset[sr_row][stop_freq & 15], kx + 1, 64);
    }
    int num_bands = k2 - kx;
    if (num_bands < 1) num_bands = 1;
    if (num_bands > 48) num_bands = 48;
    return num_bands;
}

#include "fft.h"

static float qmf_ana_cos_lut[32][32];
static float qmf_ana_sin_lut[32][32];

static float qmf_rot_cos[64];
static float qmf_rot_sin[64];

static float qmf_post_cos[64];
static float qmf_post_sin[64];

static bool qmf_twiddles_init = false;

void init_qmf_twiddles(void)
{
    if (qmf_twiddles_init) return;

    for (int n = 0; n < 64; n++) {
        float angle = (float)M_PI * (n - 0.25f) / 128.0f;
        qmf_rot_cos[n] = cosf(angle);
        qmf_rot_sin[n] = sinf(angle);

        float angle_post = (float)M_PI * (2 * n + 1) / 128.0f;
        qmf_post_cos[n] = cosf(angle_post);
        qmf_post_sin[n] = sinf(angle_post);
    }

    for (int k = 0; k < 32; k++) {
        for (int n = 0; n < 32; n++) {
            float angle = (float)M_PI * (k + 0.5f) * (n - 0.5f) / 32.0f;
            qmf_ana_cos_lut[k][n] = cosf(angle);
            qmf_ana_sin_lut[k][n] = sinf(angle);
        }
    }

    qmf_twiddles_init = true;
}

static const float ps_iid_scale_lut[15] = {
    0.000f, 0.125f, 0.250f, 0.375f, 0.500f, 0.625f, 0.750f, 0.875f,
    1.000f, 1.125f, 1.250f, 1.375f, 1.500f, 1.750f, 2.000f
};

static const float ps_icc_scale_lut[8] = {
    1.000f, 0.937f, 0.841f, 0.600f, 0.367f, 0.000f, -0.589f, -1.000f
};

static int sbr_decode_huffman_env_delta(BitReader *bs, const SBRHuffEntry *table, int nsyms, int offset)
{
    uint32_t val = 0;
    int len = 0;
    while (len < 20) {
        val = (val << 1) | bits_get(bs, 1);
        len++;
        for (int i = 0; i < nsyms; i++) {
            if (table[i].len == len && table[i].code == val) {
                return i - offset;
            }
        }
    }
    return 0;
}

static void ps_decode_payload(struct faad_decoder *dec, BitReader *bs)
{
    PSState *ps = &dec->ps;
    dec->ps_present = true;
#ifdef FAAD_STATS
    dec->stats.psActiveFrames++;
#endif

    ps->enable_iid = bits_get(bs, 1);
    if (ps->enable_iid) {
        bool iid_mode = bits_get(bs, 1);
        int bands = iid_mode ? 20 : 10;
#ifdef FAAD_STATS
        dec->stats.psIidBandsSum += bands;
#endif
        for (int b = 0; b < bands; b++) {
            int val = bits_get(bs, 4);
            ps->iid_idx[b] = (int8_t)(val - 7);
        }
    }

    ps->enable_icc = bits_get(bs, 1);
    if (ps->enable_icc) {
        bool icc_mode = bits_get(bs, 1);
        int bands = icc_mode ? 20 : 10;
#ifdef FAAD_STATS
        dec->stats.psIccBandsSum += bands;
#endif
        for (int b = 0; b < bands; b++) {
            int val = bits_get(bs, 3);
            ps->icc_idx[b] = (int8_t)val;
        }
    }

    /* ISO/IEC 14496-3 Section 8.6.4: Compute PS spatial mixing gains H11, H22, H12, H21 */
    for (int b = 0; b < SBR_PS_BANDS; b++) {
        int iid = ps->iid_idx[b] + 7;
        if (iid < 0) iid = 0;
        if (iid > 14) iid = 14;

        int icc = ps->icc_idx[b];
        if (icc < 0) icc = 0;
        if (icc > 7) icc = 7;

        float c = ps_iid_scale_lut[iid];
        float rho = ps_icc_scale_lut[icc];

        float cos_alpha = sqrtf(2.0f / (1.0f + c * c));
        float sin_alpha = c * cos_alpha;
        float gamma = 0.5f * acosf(rho);

        ps->h11[b] = cos_alpha * cosf(gamma);
        ps->h22[b] = sin_alpha * cosf(gamma);
        ps->h12[b] = -sin_alpha * sinf(gamma);
        ps->h21[b] = cos_alpha * sinf(gamma);
    }
}

faad_status sbr_decode_extension(struct faad_decoder *dec, BitReader *bs, uint32_t ch0, uint32_t syntax_id)
{
#ifndef FAAD_DISABLE_SBR
    int nch = (syntax_id == ID_CPE) ? 2 : 1;
    if (ch0 + nch > MAX_CHANNELS) return FAAD_ERR_INVALID_ARGUMENT;
    dec->sbr_present = true;

    /* ISO/IEC 14496-3 Section 4.6.18.5 sbr_extension_data(): 1-bit header_flag */
    bool sbr_header_flag = bits_get(bs, 1);
    if (sbr_header_flag) {
#ifdef FAAD_STATS
        dec->stats.sbrHeaderCount++;
#endif
        bool amp_res = bits_get(bs, 1);
        uint32_t start_freq = bits_get(bs, 4);
        uint32_t stop_freq = bits_get(bs, 4);
        uint32_t xover_band = bits_get(bs, 3);
        for (int c = 0; c < nch; c++) {
            SBRState *sbr = &dec->sbr[ch0 + c];
            sbr->header_present = true;
            sbr->bs_amp_res = amp_res;
            sbr->bs_start_freq = start_freq;
            sbr->bs_stop_freq = stop_freq;
            sbr->bs_xover_band = xover_band;
        }
        bits_skip(bs, 2); /* bs_reserved */
        bool header_extra_1 = bits_get(bs, 1);
        bool header_extra_2 = bits_get(bs, 1);
        if (header_extra_1) {
            bits_skip(bs, 2); /* bs_freq_scale */
            bits_skip(bs, 1); /* bs_alter_scale */
            bits_skip(bs, 2); /* bs_noise_bands */
        }
        if (header_extra_2) {
            bits_skip(bs, 2); /* bs_limiter_bands */
            bits_skip(bs, 2); /* bs_limiter_gains */
            bits_skip(bs, 1); /* bs_interpol_freq */
            bits_skip(bs, 1); /* bs_smoothing_mode */
        }
    }

    /* Lead coupling / reserved bits per ISO/IEC 14496-3 Section 4.6.18.5 */
    if (syntax_id == ID_CPE) {
        bool bs_coupling = bits_get(bs, 1);
        if (!bs_coupling) {
            bits_skip(bs, 1); /* bs_reserved */
        } else {
            /* Coupe mode handling: bs_coupling_mode (1 bit) */
            bits_skip(bs, 1);
        }
    } else {
        bits_skip(bs, 1); /* bs_reserved */
    }

    /* SBR Frame Grid Decoding for each channel in element */
    for (int c = 0; c < nch; c++) {
        SBRState *sbr = &dec->sbr[ch0 + c];
        sbr->bs_frame_class = bits_get(bs, 2);
        if (sbr->bs_frame_class == 0) { /* FIXFIX */
            sbr->bs_num_env = bits_get(bs, 2) + 1;
            if (sbr->bs_num_env == 3) sbr->bs_num_env = 4;
            bits_skip(bs, 1); /* bs_freq_res */
        } else if (sbr->bs_frame_class == 1) { /* FIXVAR */
            bits_skip(bs, 2); /* bs_var_bord_1 */
            sbr->bs_num_env = bits_get(bs, 2) + 1;
            for (int i = 0; i < sbr->bs_num_env - 1; i++) bits_skip(bs, 2);
            int ptr_len = (sbr->bs_num_env > 4) ? 3 : (sbr->bs_num_env > 2) ? 2 : (sbr->bs_num_env > 1) ? 1 : 0;
            if (ptr_len > 0) bits_skip(bs, ptr_len);
            for (int i = 0; i < sbr->bs_num_env; i++) bits_skip(bs, 1);
        } else if (sbr->bs_frame_class == 2) { /* VARFIX */
            bits_skip(bs, 2); /* bs_var_bord_0 */
            sbr->bs_num_env = bits_get(bs, 2) + 1;
            for (int i = 0; i < sbr->bs_num_env - 1; i++) bits_skip(bs, 2);
            int ptr_len = (sbr->bs_num_env > 4) ? 3 : (sbr->bs_num_env > 2) ? 2 : (sbr->bs_num_env > 1) ? 1 : 0;
            if (ptr_len > 0) bits_skip(bs, ptr_len);
            for (int i = 0; i < sbr->bs_num_env; i++) bits_skip(bs, 1);
        } else { /* VARVAR */
            bits_skip(bs, 2); /* bs_var_bord_0 */
            bits_skip(bs, 2); /* bs_var_bord_1 */
            uint32_t rel_0 = bits_get(bs, 2);
            uint32_t rel_1 = bits_get(bs, 2);
            sbr->bs_num_env = rel_0 + rel_1 + 1;
            for (uint32_t i = 0; i < rel_0; i++) bits_skip(bs, 2);
            for (uint32_t i = 0; i < rel_1; i++) bits_skip(bs, 2);
            int ptr_len = (sbr->bs_num_env > 4) ? 3 : (sbr->bs_num_env > 2) ? 2 : (sbr->bs_num_env > 1) ? 1 : 0;
            if (ptr_len > 0) bits_skip(bs, ptr_len);
            for (int i = 0; i < sbr->bs_num_env; i++) bits_skip(bs, 1);
        }
    }

    /* sbr_dtdf (delta coding direction flags) for each channel */
    bool bs_df_env[2][8] = {{0}};
    bool bs_df_noise[2][2] = {{0}};
    for (int c = 0; c < nch; c++) {
        SBRState *sbr = &dec->sbr[ch0 + c];
        for (int env = 0; env < sbr->bs_num_env && env < 8; env++) {
            bs_df_env[c][env] = bits_get(bs, 1);
        }
        int n_q = (sbr->bs_num_env > 1) ? 2 : 1;
        for (int n = 0; n < n_q && n < 2; n++) {
            bs_df_noise[c][n] = bits_get(bs, 1);
        }
    }

    /* SBR Inverse Filtering Mode for each channel: 2 bits per channel */
    for (int c = 0; c < nch; c++) {
        bits_skip(bs, 2);
    }

    /* SBR Envelope Data (E_orig) for each channel */
    for (int c = 0; c < nch; c++) {
        SBRState *sbr = &dec->sbr[ch0 + c];
#ifdef FAAD_STATS
        if (c == 0) dec->stats.sbrEnvelopeSum += sbr->bs_num_env;
#endif
        bool bs_amp_res = sbr->bs_amp_res;
        /* ISO/IEC 14496-3 Section 4.6.18.3 & libfaac encoder (sbr_bitstream.c):
         * bs_amp_res = 0 -> 1.5 dB resolution (7 bits first val, f_huff_env_1_5dB)
         * bs_amp_res = 1 -> 3.0 dB resolution (6 bits first val, f_huff_env_3_0dB) */
        const SBRHuffEntry *huff_tab = bs_amp_res ? f_huff_env_3_0dB : f_huff_env_1_5dB;
        int huff_nsyms = bs_amp_res ? F_HUFF_ENV_3_0DB_NSYMS : F_HUFF_ENV_1_5DB_NSYMS;
        int huff_offset = bs_amp_res ? F_HUFF_ENV_3_0DB_OFFSET : F_HUFF_ENV_1_5DB_OFFSET;

        int num_bands = sbr_compute_num_bands(dec->asc.sbr_sample_rate > 0 ? dec->asc.sbr_sample_rate : 2 * dec->core_sample_rate, sbr->bs_start_freq, sbr->bs_stop_freq);
        for (int env = 0; env < sbr->bs_num_env && env < 8; env++) {
            bool df = bs_df_env[c][env];
            int prev_val = bs_amp_res ? 30 : 60;

            for (int band = 0; band < num_bands; band++) {
                if (!df) {
                    if (band == 0) {
                        prev_val = bits_get(bs, bs_amp_res ? 6 : 7);
                    } else {
                        int delta = sbr_decode_huffman_env_delta(bs, huff_tab, huff_nsyms, huff_offset);
                        prev_val += delta;
                    }
                } else {
                    int delta = sbr_decode_huffman_env_delta(bs, huff_tab, huff_nsyms, huff_offset);
                    prev_val = sbr->E_orig[env == 0 ? 0 : env - 1][band] + delta;
                }

                sbr->E_orig[env][band] = (int8_t)prev_val;
            }
        }
    }

    /* SBR Noise Floor Data (Q_orig) for each channel per ISO/IEC 14496-3 Section 4.6.18.3 */
    for (int c = 0; c < nch; c++) {
        SBRState *sbr = &dec->sbr[ch0 + c];
        sbr->bs_num_noise = (sbr->bs_num_env > 1) ? 2 : 1;
        for (int n = 0; n < sbr->bs_num_noise && n < 8; n++) {
            bool df = bs_df_noise[c][n];
            int prev_val = 30;
            for (int band = 0; band < 5; band++) {
                if (n == 0 && !df) {
                    if (band == 0) {
                        prev_val = bits_get(bs, 5);
                    } else {
                        int delta = sbr_decode_huffman_env_delta(bs, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET);
                        prev_val += delta;
                    }
                } else if (df) {
                    int delta = sbr_decode_huffman_env_delta(bs, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET);
                    prev_val = sbr->Q_orig[n == 0 ? 0 : n - 1][band] + delta;
                } else {
                    int delta = sbr_decode_huffman_env_delta(bs, f_huff_env_3_0dB, F_HUFF_ENV_3_0DB_NSYMS, F_HUFF_ENV_3_0DB_OFFSET);
                    prev_val += delta;
                }
                sbr->Q_orig[n][band] = (int8_t)prev_val;
            }
        }
    }

    /* SBR Synthetics / Harmonics per ISO/IEC 14496-3 Section 4.6.18.3 */
    for (int c = 0; c < nch; c++) {
        SBRState *sbr = &dec->sbr[ch0 + c];
        bool bs_add_harmonic_flag = bits_get(bs, 1);
        if (bs_add_harmonic_flag) {
            int num_bands = sbr_compute_num_bands(dec->asc.sbr_sample_rate > 0 ? dec->asc.sbr_sample_rate : 2 * dec->core_sample_rate, sbr->bs_start_freq, sbr->bs_stop_freq);
            for (int band = 0; band < num_bands; band++) {
                sbr->bs_add_harmonic[band] = bits_get(bs, 1);
            }
        }
    }

    /* Extended data flag: 1 bit per ISO/IEC 14496-3 Section 4.6.18.5 */
    bool bs_extended_data = bits_get(bs, 1);
    if (bs_extended_data) {
        uint32_t ext_len = bits_get(bs, 4);
        if (ext_len == 15) ext_len += bits_get(bs, 8);
        uint32_t ext_start = bits_get_consumed(bs);
        uint32_t ext_end = ext_start + ext_len * 8;

        uint32_t bs_extension_id = bits_get(bs, 2);
#ifndef FAAD_DISABLE_PS
        if (bs_extension_id == 2) { /* EXTENSION_ID_PS */
            bool ps_header = bits_get(bs, 1);
            if (ps_header) {
                ps_decode_payload(dec, bs);
            }
        }
#else
        (void)bs_extension_id;
#endif
        uint32_t consumed = bits_get_consumed(bs);
        if (consumed < ext_end) {
            bits_skip(bs, ext_end - consumed);
        }
    }

    return FAAD_OK;
#else
    (void)dec; (void)bs; (void)ch; (void)syntax_id;
    return FAAD_OK;
#endif
}

/* 32-subband QMF analysis filterbank with 320-tap prototype windowing and persistent state */
static void qmf_analysis_320(SBRState *sbr, const float *in, float qmf_real[32][32], float qmf_imag[32][32])
{
    init_qmf_twiddles();
    float *ovl = sbr ? sbr->qmf_ana_ovl : NULL;
    float local_ovl[320];
    if (!ovl) {
        memset(local_ovl, 0, sizeof(local_ovl));
        ovl = local_ovl;
    }

    for (int t = 0; t < 32; t++) {
        memmove(&ovl[0], &ovl[32], 288 * sizeof(float));
        for (int n = 0; n < 32; n++) {
            ovl[288 + n] = in[t * 32 + n];
        }

        float samples[32];
        const float * restrict win_ptr = qmf_c;
        const float * restrict ovl_ptr = ovl;

        for (int n = 0; n < 32; n++) {
            float sample = 0.0f;
            for (int j = 0; j < 10; j++) {
                int idx = j * 64 + 2 * n;
                sample += ovl_ptr[j * 32 + n] * win_ptr[idx];
            }
            samples[n] = sample;
        }

        for (int k = 0; k < 32; k++) {
            float sum_r = 0.0f;
            float sum_i = 0.0f;
            const float * restrict cos_row = qmf_ana_cos_lut[k];
            const float * restrict sin_row = qmf_ana_sin_lut[k];
            const float * restrict smp_ptr = samples;

            for (int n = 0; n < 32; n++) {
                float s = smp_ptr[n];
                sum_r += s * cos_row[n];
                sum_i += s * sin_row[n];
            }
            qmf_real[t][k] = sum_r * 0.03125f;
            qmf_imag[t][k] = sum_i * 0.03125f;
        }
    }
}

/* 64-subband QMF synthesis filterbank with 640-sample overlapping delay line history */
static void qmf_synthesis_640(SBRState *sbr, float qmf_real[32][64], float qmf_imag[32][64], float *out)
{
    init_qmf_twiddles();
    FFT_Tables fft_tbl;
    fft_initialize(&fft_tbl);

    for (int t = 0; t < 32; t++) {
        /* Shift 640-sample QMF delay line history by 64 samples */
        memmove(&sbr->qmf_ovl[0], &sbr->qmf_ovl[64], 576 * sizeof(float));

        /* Fast FFT-accelerated 64-point QMF synthesis IDFT */
        const float * restrict re_ptr = qmf_real[t];
        const float * restrict im_ptr = qmf_imag[t];
        const float * restrict rot_c = qmf_rot_cos;
        const float * restrict rot_s = qmf_rot_sin;
        float xr[64], xi[64];

        for (int k = 0; k < 64; k++) {
            float c = rot_c[k];
            float s = rot_s[k];
            xr[k] = re_ptr[k] * c + im_ptr[k] * s;
            xi[k] = im_ptr[k] * c - re_ptr[k] * s;
        }

        fft(&fft_tbl, xr, xi, 6);

        const float * restrict post_c = qmf_post_cos;
        const float * restrict post_s = qmf_post_sin;
        float * restrict ovl_dst = sbr->qmf_ovl + 576;

        for (int n = 0; n < 64; n++) {
            float r = xr[n];
            float i = xi[n];
            float post_r = r * post_c[n] - i * post_s[n];
            ovl_dst[n] = post_r * 0.03125f;
        }

        /* Extract 64 time-domain output samples with unit-stride auto-vectorizable inner loop */
        const float * restrict ovl_ptr = sbr->qmf_ovl;
        const float * restrict win_ptr = qmf_c;
        float * restrict out_ptr = out + t * 64;

        for (int n = 0; n < 64; n++) {
            float sample = ovl_ptr[n] * win_ptr[n]
                         + ovl_ptr[64 + n] * win_ptr[64 + n]
                         + ovl_ptr[128 + n] * win_ptr[128 + n]
                         + ovl_ptr[192 + n] * win_ptr[192 + n]
                         + ovl_ptr[256 + n] * win_ptr[256 + n]
                         + ovl_ptr[320 + n] * win_ptr[320 + n]
                         + ovl_ptr[384 + n] * win_ptr[384 + n]
                         + ovl_ptr[448 + n] * win_ptr[448 + n]
                         + ovl_ptr[512 + n] * win_ptr[512 + n]
                         + ovl_ptr[576 + n] * win_ptr[576 + n];
            out_ptr[n] = sample;
        }
    }
}

void sbr_apply(struct faad_decoder *dec, uint32_t num_ch, float *pcm_in, float *pcm_out)
{
#ifndef FAAD_DISABLE_SBR
    if (!dec->sbr_present) {
        for (uint32_t ch = 0; ch < num_ch; ch++) {
            float prev = pcm_in[ch * FRAME_LEN_LONG];
            for (uint32_t i = 0; i < FRAME_LEN_LONG; i++) {
                float sample = pcm_in[ch * FRAME_LEN_LONG + i];
                pcm_out[ch * 2048 + i * 2]     = 0.5f * (prev + sample);
                pcm_out[ch * 2048 + i * 2 + 1] = sample;
                prev = sample;
            }
        }
        return;
    }

    /* Parametric Stereo (HE-AAC v2): Synthesize stereo L/R channels from Mono baseband */
    if (dec->ps_present && num_ch == 1) {
        dec->num_channels = 2;
        float qmf_ana_r[32][32];
        float qmf_ana_i[32][32];
        float qmf_left_r[32][64], qmf_left_i[32][64];
        float qmf_right_r[32][64], qmf_right_i[32][64];

        memset(qmf_left_r, 0, sizeof(qmf_left_r));
        memset(qmf_left_i, 0, sizeof(qmf_left_i));
        memset(qmf_right_r, 0, sizeof(qmf_right_r));
        memset(qmf_right_i, 0, sizeof(qmf_right_i));

        qmf_analysis_320(&dec->sbr[0], pcm_in, qmf_ana_r, qmf_ana_i);

        static const float ps_allpass_a = 0.43f;
        PSState *ps = &dec->ps;

        for (int t = 0; t < 32; t++) {
            for (int k = 0; k < 64; k++) {
                int band = (k * SBR_PS_BANDS) / 64;
                float src_r = (k < 32) ? qmf_ana_r[t][k] : qmf_ana_r[t][k - 32];
                float src_i = (k < 32) ? qmf_ana_i[t][k] : qmf_ana_i[t][k - 32];

                /* PS All-Pass QMF Decorrelation Filterbank */
                float d_r = ps->delay_r[2][k];
                float d_i = ps->delay_i[2][k];
                ps->delay_r[2][k] = ps->delay_r[1][k];
                ps->delay_i[2][k] = ps->delay_i[1][k];
                ps->delay_r[1][k] = ps->delay_r[0][k];
                ps->delay_i[1][k] = ps->delay_i[0][k];
                ps->delay_r[0][k] = src_r;
                ps->delay_i[0][k] = src_i;

                float dec_r = -ps_allpass_a * src_r + d_r;
                float dec_i = -ps_allpass_a * src_i + d_i;

                /* PS Spatial Matrix Mixing */
                qmf_left_r[t][k]  = src_r * ps->h11[band] + dec_r * ps->h12[band];
                qmf_left_i[t][k]  = src_i * ps->h11[band] + dec_i * ps->h12[band];
                qmf_right_r[t][k] = src_r * ps->h21[band] + dec_r * ps->h22[band];
                qmf_right_i[t][k] = src_i * ps->h21[band] + dec_i * ps->h22[band];
            }
        }

        qmf_synthesis_640(&dec->sbr[0], qmf_left_r, qmf_left_i, pcm_out);
        qmf_synthesis_640(&dec->sbr[1], qmf_right_r, qmf_right_i, pcm_out + 2048);
        return;
    }

    /* Standard HE-AAC v1 SBR Synthesis */
    for (uint32_t ch = 0; ch < num_ch; ch++) {
        SBRState *sbr = &dec->sbr[ch];
        float qmf_ana_r[32][32];
        float qmf_ana_i[32][32];
        float qmf_syn_r[32][64];
        float qmf_syn_i[32][64];

        memset(qmf_syn_r, 0, sizeof(qmf_syn_r));
        memset(qmf_syn_i, 0, sizeof(qmf_syn_i));

        qmf_analysis_320(sbr, pcm_in + ch * FRAME_LEN_LONG, qmf_ana_r, qmf_ana_i);

        uint32_t sbr_sr = dec->asc.sbr_sample_rate > 0 ? dec->asc.sbr_sample_rate : 2 * dec->core_sample_rate;
        int num_bands = sbr_compute_num_bands(sbr_sr, sbr->bs_start_freq, sbr->bs_stop_freq);

        int sr_row = (sbr_sr <= 16000) ? 0 : (sbr_sr <= 22050) ? 1 : (sbr_sr <= 24000) ? 2 : (sbr_sr <= 32000) ? 3 : (sbr_sr <= 64000) ? 4 : 5;
        int temp = (sbr_sr < 32000) ? 3000 : (sbr_sr < 64000) ? 4000 : 5000;
        int start_min = ((temp << 7) + (int)(sbr_sr >> 1)) / (int)sbr_sr;
        int kx = sbr_clamp_int(start_min + sbr_offset[sr_row][sbr->bs_start_freq & 15], 1, 63);
        int k2 = sbr_clamp_int(kx + num_bands, kx + 1, 64);

        for (int t = 0; t < 32; t++) {
            /* Copy baseband subbands up to start frequency kx */
            int base_subbands = kx < 32 ? kx : 32;
            memcpy(qmf_syn_r[t], qmf_ana_r[t], base_subbands * sizeof(float));
            memcpy(qmf_syn_i[t], qmf_ana_i[t], base_subbands * sizeof(float));
        }

        /* High Frequency Reconstruction with ISO Time-Slot Envelope Gain Interpolation */
        int num_env = (sbr->bs_num_env > 0 && sbr->bs_num_env <= 8) ? sbr->bs_num_env : 1;
        int step = 32 / num_env;

        for (int t = 0; t < 32; t++) {
            int env_curr = (t * num_env) / 32;
            int env_next = (env_curr + 1 < num_env) ? env_curr + 1 : env_curr;
            if (env_curr >= 8) env_curr = 7;
            if (env_next >= 8) env_next = 7;

            float alpha = (float)(t % step) / (float)step;

            for (int k = kx; k < k2; k++) {
                int band_idx = (k - kx) * num_bands / (k2 - kx);
                if (band_idx >= num_bands) band_idx = num_bands - 1;

                int base_k = (kx < 32) ? kx : 32;
                if (base_k < 1) base_k = 1;
                int src_k = (k - kx) % base_k;
                if (src_k < 0) src_k = 0;
                if (src_k >= 32) src_k = 31;

                int e_curr = sbr->E_orig[env_curr][band_idx];
                int e_next = sbr->E_orig[env_next][band_idx];

                float g_curr = powf(2.0f, 0.25f * (e_curr - 20));
                float g_next = powf(2.0f, 0.25f * (e_next - 20));
                float gain = (1.0f - alpha) * g_curr + alpha * g_next;

                qmf_syn_r[t][k] = qmf_ana_r[t][src_k] * gain;
                qmf_syn_i[t][k] = qmf_ana_i[t][src_k] * gain;
            }
        }

        qmf_synthesis_640(sbr, qmf_syn_r, qmf_syn_i, pcm_out + ch * 2048);
    }
#else
    for (uint32_t ch = 0; ch < num_ch; ch++) {
        float prev = pcm_in[ch * FRAME_LEN_LONG];
        for (uint32_t i = 0; i < FRAME_LEN_LONG; i++) {
            float sample = pcm_in[ch * FRAME_LEN_LONG + i];
            pcm_out[ch * 2048 + i * 2]     = 0.5f * (prev + sample);
            pcm_out[ch * 2048 + i * 2 + 1] = sample;
            prev = sample;
        }
    }
#endif
}

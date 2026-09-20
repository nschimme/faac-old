/*
 * Spectral Band Replication decoder, ISO/IEC 14496-3 §4.6.18 (high-quality
 * complex path), plus the parametric-stereo payload parser and mixer.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include "faad_internal.h"
#include "sbr_tables.h"
#include "fft.h"

#ifndef FAAD_DISABLE_SBR
#define SBR_NOISE_FLOOR_OFFSET 6

/* Limiter gains (§4.6.18.7.5), amplitude domain: -3 dB, 0 dB, +3 dB, none. */
static const float sbr_lim_gain[4] = { 0.70794578f, 1.0f, 1.41253754f, 1e10f };

/* Gain smoothing window (§4.6.18.7.6). */
static const float sbr_h_smooth[5] = {
    0.33333333333333f, 0.30150283239582f, 0.21816949906249f,
    0.11516383427084f, 0.03183050093751f
};

/* ------------------------------------------------------------------------ */
/* Twiddles for the FFT-based QMF banks                                      */
/* ------------------------------------------------------------------------ */

static float ana_pre_c[64], ana_pre_s[64];   /* exp(+j*pi*n/64) */
static float ana_post_c[32], ana_post_s[32]; /* exp(-j*pi*(k+1/2)/128) */
static float syn_pre_c[64], syn_pre_s[64];   /* exp(-j*255*pi*k/128) */
static float syn_post_c[128], syn_post_s[128]; /* exp(+j*pi*(2n-255)/256) */
#ifdef FAAD_D_SBR
static float ds_pre_c[32], ds_pre_s[32];     /* exp(-j*127.5*pi*k/64) */
static float ds_post_c[64], ds_post_s[64];   /* exp(+j*pi*(2n-127.5)/128) */
#endif
static FFT_Tables sbr_fft;
static bool qmf_twiddles_init = false;

void init_qmf_twiddles(void)
{
    if (qmf_twiddles_init) return;
    fft_initialize(&sbr_fft);
    for (int n = 0; n < 64; n++) {
        ana_pre_c[n] = (float)cos(M_PI * n / 64.0);
        ana_pre_s[n] = (float)sin(M_PI * n / 64.0);
        syn_pre_c[n] = (float)cos(-255.0 * M_PI * n / 128.0);
        syn_pre_s[n] = (float)sin(-255.0 * M_PI * n / 128.0);
    }
    for (int k = 0; k < 32; k++) {
        ana_post_c[k] = (float)cos(-0.5 * M_PI * (k + 0.5) / 64.0);
        ana_post_s[k] = (float)sin(-0.5 * M_PI * (k + 0.5) / 64.0);
    }
    for (int n = 0; n < 128; n++) {
        syn_post_c[n] = (float)cos(M_PI * (2 * n - 255) / 256.0);
        syn_post_s[n] = (float)sin(M_PI * (2 * n - 255) / 256.0);
    }
#ifdef FAAD_D_SBR
    for (int k = 0; k < 32; k++) {
        ds_pre_c[k] = (float)cos(-127.5 * M_PI * k / 64.0);
        ds_pre_s[k] = (float)sin(-127.5 * M_PI * k / 64.0);
    }
    for (int n = 0; n < 64; n++) {
        ds_post_c[n] = (float)cos(M_PI * (2 * n - 127.5) / 128.0);
        ds_post_s[n] = (float)sin(M_PI * (2 * n - 127.5) / 128.0);
    }
#endif
    qmf_twiddles_init = true;
}

/* ------------------------------------------------------------------------ */
/* QMF banks (§4.6.18.4, §4.6.18.8)                                          */
/* ------------------------------------------------------------------------ */

/* 32-band analysis of one slot (32 new samples). The decimated bank is the
 * lower half of the 64-band one, so its phase origin sits a quarter sample
 * in: X(k) = sum_n u(n) exp(j*pi/64*(k+1/2)(2n-1/2)), an unnormalised
 * 64-point inverse DFT of u(n)*exp(j*pi*n/64), rotated. */
static void qmf_analysis_slot(SBRChannel *ch, const float *in, float out[32][2])
{
    float *x = ch->qmf_x;
    memmove(x + 32, x, 288 * sizeof(float));
    for (int n = 0; n < 32; n++) x[n] = in[31 - n];

    /* The decimated prototype c(2n) halves the passband gain of the full
     * 64-band bank the encoder's energies refer to; the factor 2 restores it. */
    float u[64];
    for (int n = 0; n < 64; n++) {
        float acc = 0.0f;
        for (int j = 0; j < 5; j++) acc += x[n + 64 * j] * qmf_c[2 * (n + 64 * j)];
        u[n] = 2.0f * acc;
    }
    /* Inverse DFT through the forward transform: IDFT(a) = conj(FFT(conj(a))). */
    float re[64], im[64];
    for (int n = 0; n < 64; n++) {
        re[n] = u[n] * ana_pre_c[n];
        im[n] = -(u[n] * ana_pre_s[n]);
    }
    fft(&sbr_fft, re, im, 6);
    for (int k = 0; k < 32; k++) {
        float ar = re[k], ai = -im[k];
        out[k][0] = ar * ana_post_c[k] - ai * ana_post_s[k];
        out[k][1] = ar * ana_post_s[k] + ai * ana_post_c[k];
    }
}

#ifndef FAAD_D_SBR
/* 64-band synthesis of one slot: 64 output samples. */
static void qmf_synthesis_slot(SBRChannel *ch, float X[64][2], float *out)
{
    float *v = ch->qmf_v;
    memmove(v + 128, v, 1152 * sizeof(float));

    /* v(n) = 1/64 Re{ exp(j*pi*(2n-255)/256) * IDFT128(X(k) exp(-j*255*pi*k/128)) } */
    float re[128], im[128];
    for (int k = 0; k < 64; k++) {
        float br = X[k][0] * syn_pre_c[k] - X[k][1] * syn_pre_s[k];
        float bi = X[k][0] * syn_pre_s[k] + X[k][1] * syn_pre_c[k];
        re[k] = br;
        im[k] = -bi;
    }
    memset(re + 64, 0, 64 * sizeof(float));
    memset(im + 64, 0, 64 * sizeof(float));
    fft(&sbr_fft, re, im, 7);
    for (int n = 0; n < 128; n++) {
        float cr = re[n], ci = -im[n];
        v[n] = (cr * syn_post_c[n] - ci * syn_post_s[n]) * (1.0f / 64.0f);
    }

    for (int n = 0; n < 64; n++) {
        float acc = 0.0f;
        for (int i = 0; i < 5; i++) {
            acc += v[256 * i + n]       * qmf_c[128 * i + n];
            acc += v[256 * i + 192 + n] * qmf_c[128 * i + 64 + n];
        }
        out[n] = acc;
    }
}
#endif

#ifdef FAAD_D_SBR
/* 32-band synthesis of one slot at the core rate (§4.6.18.8.2.3): only the
 * lower half of X is used, with the decimated prototype. */
static void qmf_synthesis_slot_ds(SBRChannel *ch, float X[64][2], float *out)
{
    float *v = ch->qmf_v;
    memmove(v + 64, v, 576 * sizeof(float));

    float re[64], im[64];
    for (int k = 0; k < 32; k++) {
        float br = X[k][0] * ds_pre_c[k] - X[k][1] * ds_pre_s[k];
        float bi = X[k][0] * ds_pre_s[k] + X[k][1] * ds_pre_c[k];
        re[k] = br;
        im[k] = -bi;
    }
    memset(re + 32, 0, 32 * sizeof(float));
    memset(im + 32, 0, 32 * sizeof(float));
    fft(&sbr_fft, re, im, 6);
    for (int n = 0; n < 64; n++) {
        float cr = re[n], ci = -im[n];
        v[n] = (cr * ds_post_c[n] - ci * ds_post_s[n]) * (1.0f / 64.0f);
    }
    for (int n = 0; n < 32; n++) {
        float acc = 0.0f;
        for (int i = 0; i < 5; i++) {
            acc += v[128 * i + n]      * qmf_c[2 * (64 * i + n)];
            acc += v[128 * i + 96 + n] * qmf_c[2 * (64 * i + 32 + n)];
        }
        out[n] = acc;
    }
}
#endif

/* ------------------------------------------------------------------------ */
/* Frequency band tables (§4.6.18.3.2) and patches (§4.6.18.6.3)            */
/* ------------------------------------------------------------------------ */

static int cmp_u8(const void *a, const void *b) { return (int)*(const uint8_t *)a - (int)*(const uint8_t *)b; }

static int sbr_round(double v) { return (int)floor(v + 0.5); }

static int sbr_start_band(uint32_t sr, int start_freq)
{
    int row = (sr <= 16000) ? 0 : (sr <= 22050) ? 1 : (sr <= 24000) ? 2 : (sr <= 32000) ? 3 : (sr <= 64000) ? 4 : 5;
    int temp = (sr < 32000) ? 3000 : (sr < 64000) ? 4000 : 5000;
    int start_min = (int)(((temp << 7) + (sr >> 1)) / sr);
    int k0 = start_min + sbr_offset[row][start_freq & 15];
    return k0 < 1 ? 1 : k0 > 63 ? 63 : k0;
}

static int sbr_stop_band(uint32_t sr, int k0, int stop_freq)
{
    if (stop_freq == 15) return k0 * 3 < 64 ? k0 * 3 : 64;
    if (stop_freq == 14) return k0 * 2 < 64 ? k0 * 2 : 64;
    int temp = (sr < 32000) ? 3000 : (sr < 64000) ? 4000 : 5000;
    int stop_min = (int)(((temp << 8) + (sr >> 1)) / sr);
    uint8_t dk[13];
    int prev = stop_min;
    for (int i = 0; i < 13; i++) {
        int cur = sbr_round(stop_min * pow(64.0 / stop_min, (i + 1) / 13.0));
        dk[i] = (uint8_t)(cur - prev);
        prev = cur;
    }
    qsort(dk, 13, 1, cmp_u8);
    int k2 = stop_min;
    for (int i = 0; i < stop_freq; i++) k2 += dk[i];
    return k2 < 64 ? k2 : 64;
}

/* Master table for bs_freq_scale > 0: log-spaced bands, two regions above
 * a 2.2449 ratio, the upper one warped by alter_scale. */
static int sbr_master_log(SBRElement *el, int k0, int k2)
{
    int bands = 14 - 2 * el->freq_scale; /* 12, 10, 8 */
    double warp = el->alter_scale ? 1.3 : 1.0;
    int two_regions = ((double)k2 / k0 > 2.2449);
    int k1 = two_regions ? 2 * k0 : k2;

    int nb0 = 2 * sbr_round(bands * log2((double)k1 / k0) / 2.0);
    if (nb0 < 1 || nb0 > SBR_MAX_BANDS) return -1;
    uint8_t dk0[SBR_MAX_BANDS];
    for (int i = 0; i < nb0; i++)
        dk0[i] = (uint8_t)(sbr_round(k0 * pow((double)k1 / k0, (i + 1.0) / nb0)) - sbr_round(k0 * pow((double)k1 / k0, (double)i / nb0)));
    qsort(dk0, nb0, 1, cmp_u8);
    el->f_master[0] = (uint8_t)k0;
    for (int i = 0; i < nb0; i++) el->f_master[i + 1] = (uint8_t)(el->f_master[i] + dk0[i]);
    int n = nb0;

    if (two_regions) {
        int nb1 = 2 * sbr_round(bands * log2((double)k2 / k1) / (2.0 * warp));
        if (nb1 < 1 || n + nb1 > SBR_MAX_BANDS) return -1;
        uint8_t dk1[SBR_MAX_BANDS];
        for (int i = 0; i < nb1; i++)
            dk1[i] = (uint8_t)(sbr_round(k1 * pow((double)k2 / k1, (i + 1.0) / nb1)) - sbr_round(k1 * pow((double)k2 / k1, (double)i / nb1)));
        qsort(dk1, nb1, 1, cmp_u8);
        if (dk1[0] < dk0[nb0 - 1]) {
            int change = dk0[nb0 - 1] - dk1[0];
            int half = (dk1[nb1 - 1] - dk1[0]) / 2;
            if (change > half) change = half;
            dk1[0] = (uint8_t)(dk1[0] + change);
            dk1[nb1 - 1] = (uint8_t)(dk1[nb1 - 1] - change);
        }
        for (int i = 0; i < nb1; i++) el->f_master[n + i + 1] = (uint8_t)(el->f_master[n + i] + dk1[i]);
        n += nb1;
    }
    return n;
}

/* Master table for bs_freq_scale == 0: uniform spacing, the remainder folded
 * into the outermost bands. */
static int sbr_master_linear(SBRElement *el, int k0, int k2)
{
    int dk = el->alter_scale ? 2 : 1;
    int nb = el->alter_scale ? 2 * sbr_round((k2 - k0) / 4.0) : 2 * ((k2 - k0) / 2);
    if (nb < 1 || nb > SBR_MAX_BANDS) return -1;
    int diff = k2 - (k0 + nb * dk);
    uint8_t vdk[SBR_MAX_BANDS];
    for (int i = 0; i < nb; i++) vdk[i] = (uint8_t)dk;
    int k = diff < 0 ? 0 : nb - 1, incr = diff < 0 ? 1 : -1;
    while (diff != 0) {
        vdk[k] = (uint8_t)(vdk[k] - incr);
        k += incr;
        diff += incr;
    }
    el->f_master[0] = (uint8_t)k0;
    for (int i = 0; i < nb; i++) el->f_master[i + 1] = (uint8_t)(el->f_master[i] + vdk[i]);
    return nb;
}

static bool sbr_build_patches(SBRElement *el, uint32_t sr)
{
    int k0 = el->k0, kx = el->kx, M = el->M;
    int msb = k0, usb = kx, np = 0;
    int goal_sb = (int)((2048000 + (sr >> 1)) / sr);
    int k;
    if (goal_sb < kx + M) {
        for (k = 0; el->f_master[k] < goal_sb; k++) ;
    } else {
        k = el->n_master;
    }
    int sb;
    do {
        int j = k + 1, odd;
        do {
            j--;
            sb = el->f_master[j];
            odd = (sb - 2 + k0) & 1;
        } while (sb > k0 - 1 + msb - odd);

        if (np >= SBR_MAX_PATCHES) return false;
        int num = sb - usb > 0 ? sb - usb : 0;
        el->patch_num[np] = (uint8_t)num;
        el->patch_start[np] = (uint8_t)(k0 - odd - num);
        if (num > 0) {
            usb = sb;
            msb = sb;
            np++;
        } else {
            msb = kx;
        }
        if (el->f_master[k] - sb < 3) k = el->n_master;
    } while (sb != kx + M);

    if (np > 1 && el->patch_num[np - 1] < 3) np--;
    el->num_patches = (uint8_t)np;
    return np > 0;
}

static bool sbr_build_limiter(SBRElement *el)
{
    if (el->limiter_bands == 0) {
        el->f_lim[0] = el->f_low[0];
        el->f_lim[1] = el->f_low[el->n_low];
        el->n_lim = 1;
        return true;
    }
    static const double lim_bands[3] = { 1.2, 2.0, 3.0 };
    double lb = lim_bands[el->limiter_bands - 1];

    uint8_t borders[SBR_MAX_PATCHES + 1];
    int nb = 0;
    borders[nb++] = el->kx;
    for (int i = 0; i < el->num_patches; i++) borders[nb] = (uint8_t)(borders[nb - 1] + el->patch_num[i]), nb++;

    uint8_t lim[SBR_MAX_LIM + 1];
    int n = 0;
    for (int i = 0; i <= el->n_low; i++) lim[n++] = el->f_low[i];
    for (int i = 0; i < nb; i++) lim[n++] = borders[i];
    qsort(lim, n, 1, cmp_u8);

    int k = 1;
    while (k < n) {
        double oct = log2((double)lim[k] / lim[k - 1]);
        if (oct * lb < 0.49) {
            /* Too narrow: drop the border that is not a patch boundary;
             * two adjacent patch boundaries both stay. */
            int rm;
            if (lim[k] == lim[k - 1]) rm = k;
            else {
                bool kb = false, kb1 = false;
                for (int i = 0; i < nb; i++) { kb |= (borders[i] == lim[k]); kb1 |= (borders[i] == lim[k - 1]); }
                rm = !kb ? k : !kb1 ? k - 1 : -1;
            }
            if (rm < 0) { k++; continue; }
            memmove(lim + rm, lim + rm + 1, (size_t)(n - rm - 1));
            n--;
        } else {
            k++;
        }
    }
    el->n_lim = (uint8_t)(n - 1);
    memcpy(el->f_lim, lim, (size_t)n);
    return el->n_lim >= 1;
}

static bool sbr_build_tables(SBRElement *el, uint32_t sr)
{
    int k0 = sbr_start_band(sr, el->start_freq);
    int k2 = sbr_stop_band(sr, k0, el->stop_freq);
    int max_span = (sr <= 32000) ? 48 : (sr <= 44100) ? 35 : 32;
    if (k2 <= k0 || k2 - k0 > max_span) return false;
    el->k0 = (uint8_t)k0;
    el->k2 = (uint8_t)k2;

    int n = el->freq_scale ? sbr_master_log(el, k0, k2) : sbr_master_linear(el, k0, k2);
    if (n < 1 || el->xover_band >= n) return false;
    el->n_master = (uint8_t)n;

    el->n_high = (uint8_t)(n - el->xover_band);
    for (int i = 0; i <= el->n_high; i++) el->f_high[i] = el->f_master[el->xover_band + i];
    el->kx = el->f_high[0];
    el->M = (uint8_t)(el->f_high[el->n_high] - el->kx);
    if (el->kx > 32 || el->kx + el->M > 64) return false;

    el->n_low = (uint8_t)((el->n_high + 1) / 2);
    int odd = el->n_high & 1;
    el->f_low[0] = el->f_high[0];
    for (int i = 1; i <= el->n_low; i++) el->f_low[i] = el->f_high[2 * i - odd];

    int nq = el->noise_bands ? sbr_round(el->noise_bands * log2((double)k2 / el->kx)) : 1;
    if (nq < 1) nq = 1;
    if (nq > SBR_MAX_NQ) nq = SBR_MAX_NQ;
    el->n_q = (uint8_t)nq;
    el->f_noise[0] = el->f_low[0];
    int idx = 0;
    for (int k = 1; k <= nq; k++) {
        idx += (el->n_low - idx) / (nq + 1 - k);
        el->f_noise[k] = el->f_low[idx];
    }

    if (!sbr_build_patches(el, sr)) return false;
    return sbr_build_limiter(el);
}

static void sbr_reset_channel(SBRChannel *ch)
{
    memset(ch->x_low_tail, 0, sizeof(ch->x_low_tail));
    memset(ch->y_tail, 0, sizeof(ch->y_tail));
    memset(ch->bw_array, 0, sizeof(ch->bw_array));
    memset(ch->invf_mode_prev, 0, sizeof(ch->invf_mode_prev));
    memset(ch->E_prev, 0, sizeof(ch->E_prev));
    memset(ch->Q_prev, 0, sizeof(ch->Q_prev));
    memset(ch->s_index_prev, 0, sizeof(ch->s_index_prev));
    memset(ch->g_hist, 0, sizeof(ch->g_hist));
    memset(ch->q_hist, 0, sizeof(ch->q_hist));
    ch->freq_res_prev = 1;
    ch->l_A_prev = -1;
    ch->L_E_prev = 0;
    ch->t_E_end_prev = 0;
    ch->index_noise = 0;
    ch->index_sine = 0;
    ch->have_frame = false;
    ch->primed = false;
    ch->kx_prev = 0;
    ch->M_prev = 0;
}

/* ------------------------------------------------------------------------ */
/* Bitstream: header, grid, envelopes (§4.6.18.3)                           */
/* ------------------------------------------------------------------------ */

static int sbr_huff(BitReader *bs, const SBRHuffEntry *tab, int nsyms, int offset)
{
    uint32_t code = 0;
    for (int len = 1; len <= 20; len++) {
        code = (code << 1) | bits_get_1(bs);
        for (int i = 0; i < nsyms; i++)
            if (tab[i].len == (uint32_t)len && tab[i].code == code) return i - offset;
    }
    return 0;
}

static const uint8_t sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static bool sbr_read_grid(BitReader *bs, SBRChannel *ch)
{
    ch->frame_class = (uint8_t)bits_get(bs, 2);
    int L_E;
    switch (ch->frame_class) {
    case 0: { /* FIXFIX */
        int v = (int)(bits_get(bs, 2) & 3);
        L_E = 1 << v;
        if (L_E > 4) return false;
        uint8_t fr = (uint8_t)bits_get(bs, 1);
        ch->t_E[0] = 0;
        for (int i = 1; i <= L_E; i++) ch->t_E[i] = (uint8_t)(i * 16 / L_E);
        for (int i = 0; i < L_E; i++) ch->freq_res[i] = fr;
        ch->bs_pointer = 0;
        break;
    }
    case 1: { /* FIXVAR */
        int end = 16 + (int)(bits_get(bs, 2) & 3);
        L_E = (int)(bits_get(bs, 2) & 3) + 1;
        if (L_E > 4) return false;
        ch->t_E[0] = 0;
        ch->t_E[L_E] = (uint8_t)end;
        for (int i = 0; i < L_E - 1; i++) {
            int rel = 2 * (int)(bits_get(bs, 2) & 3) + 2;
            ch->t_E[L_E - 1 - i] = (uint8_t)(ch->t_E[L_E - i] - rel);
        }
        ch->bs_pointer = (uint8_t)bits_get(bs, sbr_ceil_log2[L_E]);
        for (int i = 0; i < L_E; i++) ch->freq_res[L_E - 1 - i] = (uint8_t)bits_get(bs, 1);
        break;
    }
    case 2: { /* VARFIX */
        int start = (int)(bits_get(bs, 2) & 3);
        L_E = (int)(bits_get(bs, 2) & 3) + 1;
        if (L_E > 4) return false;
        ch->t_E[0] = (uint8_t)start;
        for (int i = 0; i < L_E - 1; i++) {
            int rel = 2 * (int)(bits_get(bs, 2) & 3) + 2;
            ch->t_E[i + 1] = (uint8_t)(ch->t_E[i] + rel);
        }
        ch->t_E[L_E] = 16;
        ch->bs_pointer = (uint8_t)bits_get(bs, sbr_ceil_log2[L_E]);
        for (int i = 0; i < L_E; i++) ch->freq_res[i] = (uint8_t)bits_get(bs, 1);
        break;
    }
    default: { /* VARVAR */
        int start = (int)(bits_get(bs, 2) & 3);
        int end = 16 + (int)(bits_get(bs, 2) & 3);
        int rel0 = (int)(bits_get(bs, 2) & 3);
        int rel1 = (int)(bits_get(bs, 2) & 3);
        L_E = rel0 + rel1 + 1;
        if (L_E > SBR_MAX_ENV) return false;
        ch->t_E[0] = (uint8_t)start;
        for (int i = 0; i < rel0; i++) {
            int rel = 2 * (int)(bits_get(bs, 2) & 3) + 2;
            ch->t_E[i + 1] = (uint8_t)(ch->t_E[i] + rel);
        }
        ch->t_E[L_E] = (uint8_t)end;
        for (int i = 0; i < rel1; i++) {
            int rel = 2 * (int)(bits_get(bs, 2) & 3) + 2;
            ch->t_E[L_E - 1 - i] = (uint8_t)(ch->t_E[L_E - i] - rel);
        }
        ch->bs_pointer = (uint8_t)bits_get(bs, sbr_ceil_log2[L_E]);
        for (int i = 0; i < L_E; i++) ch->freq_res[i] = (uint8_t)bits_get(bs, 1);
        break;
    }
    }
    ch->L_E = (uint8_t)L_E;
    for (int i = 0; i < L_E; i++)
        if (ch->t_E[i + 1] <= ch->t_E[i] || ch->t_E[i + 1] > 16 + 3) return false;
    if (ch->bs_pointer > L_E) return false;

    /* Transient envelope l_A and the noise floor borders (§4.6.18.3.3). */
    int p = ch->bs_pointer, mid;
    switch (ch->frame_class) {
    case 0:  ch->l_A = -1; mid = L_E / 2; break;
    case 2:  ch->l_A = (int8_t)(p ? p - 1 : -1); mid = (p == 0) ? 1 : (p == 1) ? L_E - 1 : p - 1; break;
    default: ch->l_A = (int8_t)(p ? L_E + 1 - p : -1); mid = (p > 1) ? L_E + 1 - p : L_E - 1; break;
    }
    if (mid < 0) mid = 0;
    ch->L_Q = (uint8_t)(L_E > 1 ? 2 : 1);
    ch->t_Q[0] = ch->t_E[0];
    if (ch->L_Q == 1) {
        ch->t_Q[1] = ch->t_E[L_E];
    } else {
        ch->t_Q[1] = ch->t_E[mid];
        ch->t_Q[2] = ch->t_E[L_E];
    }
    return true;
}

static void sbr_read_envelope(BitReader *bs, const SBRElement *el, SBRChannel *ch, bool balance)
{
    const SBRHuffEntry *t_tab, *f_tab;
    int t_n, t_off, f_n, f_off, start_bits;
    if (ch->amp_res) {
        if (balance) {
            t_tab = t_huff_env_bal_3_0dB; t_n = T_HUFF_ENV_BAL_3_0DB_NSYMS; t_off = T_HUFF_ENV_BAL_3_0DB_OFFSET;
            f_tab = f_huff_env_bal_3_0dB; f_n = F_HUFF_ENV_BAL_3_0DB_NSYMS; f_off = F_HUFF_ENV_BAL_3_0DB_OFFSET;
            start_bits = 5;
        } else {
            t_tab = t_huff_env_3_0dB; t_n = T_HUFF_ENV_3_0DB_NSYMS; t_off = T_HUFF_ENV_3_0DB_OFFSET;
            f_tab = f_huff_env_3_0dB; f_n = F_HUFF_ENV_3_0DB_NSYMS; f_off = F_HUFF_ENV_3_0DB_OFFSET;
            start_bits = 6;
        }
    } else {
        if (balance) {
            t_tab = t_huff_env_bal_1_5dB; t_n = T_HUFF_ENV_BAL_1_5DB_NSYMS; t_off = T_HUFF_ENV_BAL_1_5DB_OFFSET;
            f_tab = f_huff_env_bal_1_5dB; f_n = F_HUFF_ENV_BAL_1_5DB_NSYMS; f_off = F_HUFF_ENV_BAL_1_5DB_OFFSET;
            start_bits = 6;
        } else {
            t_tab = t_huff_env_1_5dB; t_n = T_HUFF_ENV_1_5DB_NSYMS; t_off = T_HUFF_ENV_1_5DB_OFFSET;
            f_tab = f_huff_env_1_5dB; f_n = F_HUFF_ENV_1_5DB_NSYMS; f_off = F_HUFF_ENV_1_5DB_OFFSET;
            start_bits = 7;
        }
    }

    /* The balance channel is coded at half resolution: its values count double. */
    int scale = balance ? 2 : 1;
    for (int l = 0; l < ch->L_E; l++) {
        int nb = ch->freq_res[l] ? el->n_high : el->n_low;
        if (ch->df_env[l] == 0) {
            ch->E[l][0] = (int16_t)(scale * (int)bits_get(bs, start_bits));
            for (int k = 1; k < nb; k++) {
                int d = scale * sbr_huff(bs, f_tab, f_n, f_off);
                ch->E[l][k] = (int16_t)(ch->E[l][k - 1] + d);
            }
        } else {
            /* Time deltas against the previous envelope, mapped across a
             * resolution change (§4.6.18.6.2). */
            const int16_t *prev = (l == 0) ? ch->E_prev : ch->E[l - 1];
            int r_prev = (l == 0) ? ch->freq_res_prev : ch->freq_res[l - 1];
            for (int k = 0; k < nb; k++) {
                int d = scale * sbr_huff(bs, t_tab, t_n, t_off);
                int ref;
                if (r_prev == ch->freq_res[l]) {
                    ref = prev[k];
                } else if (ch->freq_res[l]) {
                    int i = 0;
                    while (i + 1 < el->n_low && el->f_high[k] >= el->f_low[i + 1]) i++;
                    ref = prev[i];
                } else {
                    int i = 0;
                    while (i < el->n_high && el->f_high[i] != el->f_low[k]) i++;
                    ref = prev[i < el->n_high ? i : el->n_high - 1];
                }
                ch->E[l][k] = (int16_t)(ref + d);
            }
        }
    }
}

static void sbr_read_noise(BitReader *bs, const SBRElement *el, SBRChannel *ch, bool balance)
{
    const SBRHuffEntry *t_tab = balance ? t_huff_noise_bal_3_0dB : t_huff_noise_3_0dB;
    int t_n = balance ? T_HUFF_NOISE_BAL_3_0DB_NSYMS : T_HUFF_NOISE_3_0DB_NSYMS;
    int t_off = balance ? T_HUFF_NOISE_BAL_3_0DB_OFFSET : T_HUFF_NOISE_3_0DB_OFFSET;
    const SBRHuffEntry *f_tab = balance ? f_huff_env_bal_3_0dB : f_huff_env_3_0dB;
    int f_n = balance ? F_HUFF_ENV_BAL_3_0DB_NSYMS : F_HUFF_ENV_3_0DB_NSYMS;
    int f_off = balance ? F_HUFF_ENV_BAL_3_0DB_OFFSET : F_HUFF_ENV_3_0DB_OFFSET;

    int scale = balance ? 2 : 1;
    for (int l = 0; l < ch->L_Q; l++) {
        if (ch->df_noise[l] == 0) {
            ch->Q[l][0] = (int16_t)(scale * (int)bits_get(bs, 5));
            for (int k = 1; k < el->n_q; k++) {
                int d = scale * sbr_huff(bs, f_tab, f_n, f_off);
                ch->Q[l][k] = (int16_t)(ch->Q[l][k - 1] + d);
            }
        } else {
            const int16_t *prev = (l == 0) ? ch->Q_prev : ch->Q[l - 1];
            for (int k = 0; k < el->n_q; k++) {
                int d = scale * sbr_huff(bs, t_tab, t_n, t_off);
                ch->Q[l][k] = (int16_t)(prev[k] + d);
            }
        }
    }
}

faad_status sbr_decode_extension(struct faad_decoder *dec, BitReader *bs, uint32_t ch0, uint32_t syntax_id, bool crc)
{
    int nch = (syntax_id == ID_CPE) ? 2 : 1;
    if (ch0 + nch > MAX_CHANNELS) return FAAD_ERR_INVALID_ARGUMENT;
    SBRElement *el = &dec->sbr_el[ch0];
    if (crc) bits_skip(bs, 10); /* bs_sbr_crc_bits, not verified */
    uint32_t sbr_sr = dec->asc.sbr_sample_rate > 0 ? dec->asc.sbr_sample_rate : 2 * dec->core_sample_rate;

    if (bits_get(bs, 1)) { /* bs_header_flag */
        SBRElement h = *el;
        h.amp_res    = bits_get(bs, 1);
        h.start_freq = (uint8_t)bits_get(bs, 4);
        h.stop_freq  = (uint8_t)bits_get(bs, 4);
        h.xover_band = (uint8_t)bits_get(bs, 3);
        bits_skip(bs, 2);
        bool extra1 = bits_get(bs, 1);
        bool extra2 = bits_get(bs, 1);
        if (extra1) {
            h.freq_scale  = (uint8_t)bits_get(bs, 2);
            h.alter_scale = (uint8_t)bits_get(bs, 1);
            h.noise_bands = (uint8_t)bits_get(bs, 2);
        } else {
            h.freq_scale = 2; h.alter_scale = 1; h.noise_bands = 2;
        }
        if (extra2) {
            h.limiter_bands  = (uint8_t)bits_get(bs, 2);
            h.limiter_gains  = (uint8_t)bits_get(bs, 2);
            h.interpol_freq  = (uint8_t)bits_get(bs, 1);
            h.smoothing_mode = (uint8_t)bits_get(bs, 1);
        } else {
            h.limiter_bands = 2; h.limiter_gains = 2; h.interpol_freq = 1; h.smoothing_mode = 1;
        }
#ifdef FAAD_STATS
        dec->stats.sbrHeaderCount++;
#endif
        /* A change to anything the tables depend on resets the element. */
        bool reset = !el->header_present || h.start_freq != el->start_freq || h.stop_freq != el->stop_freq ||
                     h.xover_band != el->xover_band || h.freq_scale != el->freq_scale ||
                     h.alter_scale != el->alter_scale || h.noise_bands != el->noise_bands;
        *el = h;
        el->header_present = true;
        if (reset) {
            if (!sbr_build_tables(el, sbr_sr)) {
                el->header_present = false;
                return FAAD_ERR_DECODE_FAILED;
            }
            for (int c = 0; c < nch; c++) sbr_reset_channel(&dec->sbr[ch0 + c]);
        }
    }
    if (!el->header_present) return FAAD_OK; /* nothing to reconstruct against yet */

    SBRChannel *chs[2] = { &dec->sbr[ch0], &dec->sbr[ch0 + (nch > 1 ? 1 : 0)] };
    el->nch = (uint8_t)nch;

    /* sbr_single_channel_element / sbr_channel_pair_element (§4.6.18.3.4) */
    bool data_extra = bits_get(bs, 1);
    el->coupling = (nch == 2) ? bits_get(bs, 1) : false;
    if (data_extra) bits_skip(bs, 4 * (uint32_t)nch); /* bs_reserved */

    if (!sbr_read_grid(bs, chs[0])) return FAAD_ERR_DECODE_FAILED;
    if (nch == 2) {
        if (el->coupling) {
            SBRChannel *a = chs[0], *b = chs[1];
            b->frame_class = a->frame_class; b->L_E = a->L_E; b->L_Q = a->L_Q;
            b->bs_pointer = a->bs_pointer; b->l_A = a->l_A;
            memcpy(b->t_E, a->t_E, sizeof(b->t_E));
            memcpy(b->t_Q, a->t_Q, sizeof(b->t_Q));
            memcpy(b->freq_res, a->freq_res, sizeof(b->freq_res));
        } else if (!sbr_read_grid(bs, chs[1])) {
            return FAAD_ERR_DECODE_FAILED;
        }
    }
    for (int c = 0; c < nch; c++) {
        SBRChannel *ch = chs[c];
        /* a single FIXFIX envelope is always coded at 1.5 dB */
        ch->amp_res = (ch->frame_class == 0 && ch->L_E == 1) ? false : el->amp_res;
    }
    for (int c = 0; c < nch; c++) {
        SBRChannel *ch = chs[c];
        for (int l = 0; l < ch->L_E && l < SBR_MAX_ENV; l++) ch->df_env[l] = (uint8_t)bits_get(bs, 1);
        for (int l = 0; l < ch->L_Q && l < 2; l++) ch->df_noise[l] = (uint8_t)bits_get(bs, 1);
    }
    for (int k = 0; k < el->n_q && k < SBR_MAX_NQ; k++) chs[0]->invf_mode[k] = (uint8_t)bits_get(bs, 2);
    if (nch == 2) {
        if (el->coupling) memcpy(chs[1]->invf_mode, chs[0]->invf_mode, sizeof(chs[0]->invf_mode));
        else for (int k = 0; k < el->n_q && k < SBR_MAX_NQ; k++) chs[1]->invf_mode[k] = (uint8_t)bits_get(bs, 2);
    }
    if (el->coupling) {
        sbr_read_envelope(bs, el, chs[0], false);
        sbr_read_noise(bs, el, chs[0], false);
        sbr_read_envelope(bs, el, chs[1], true);
        sbr_read_noise(bs, el, chs[1], true);
    } else {
        for (int c = 0; c < nch; c++) sbr_read_envelope(bs, el, chs[c], false);
        for (int c = 0; c < nch; c++) sbr_read_noise(bs, el, chs[c], false);
    }
    for (int c = 0; c < nch; c++) {
        SBRChannel *ch = chs[c];
        ch->add_harmonic_flag = bits_get(bs, 1);
        memset(ch->add_harmonic, 0, sizeof(ch->add_harmonic));
        if (ch->add_harmonic_flag)
            for (int k = 0; k < el->n_high; k++) ch->add_harmonic[k] = (uint8_t)bits_get(bs, 1);
    }
#ifdef FAAD_STATS
    dec->stats.sbrEnvelopeSum += chs[0]->L_E;
#endif

    if (bits_get(bs, 1)) { /* bs_extended_data */
        uint32_t cnt = bits_get(bs, 4);
        if (cnt == 15) cnt += bits_get(bs, 8);
        uint32_t end = bits_get_consumed(bs) + cnt * 8;
        while (bits_get_consumed(bs) + 7 < end) {
            uint32_t id = bits_get(bs, 2);
#ifndef FAAD_DISABLE_PS
            if (id == PS_EXTENSION_DATA) {
                uint32_t here = bits_get_consumed(bs);
                ps_read_data(dec, bs, end > here ? end - here : 0);
                break;
            }
#endif
            (void)id;
            break;
        }
        uint32_t consumed = bits_get_consumed(bs);
        if (consumed < end) bits_skip(bs, end - consumed);
    }

    for (int c = 0; c < nch; c++) chs[c]->have_frame = true;
    dec->sbr_present = true;
    return FAAD_OK;
}

/* ------------------------------------------------------------------------ */
/* HF generation (§4.6.18.6)                                                 */
/* ------------------------------------------------------------------------ */

/* Chirp factors from the inverse filtering modes (§4.6.18.6.1). */
static void sbr_chirp(const SBRElement *el, SBRChannel *ch)
{
    static const float new_bw_tab[4] = { 0.0f, 0.75f, 0.9f, 0.98f };
    for (int k = 0; k < el->n_q; k++) {
        /* a switch between OFF and LOW in either direction lands on 0.6 */
        float nb = (ch->invf_mode[k] + ch->invf_mode_prev[k] == 1) ? 0.6f : new_bw_tab[ch->invf_mode[k] & 3];
        float prev = ch->bw_array[k];
        float t = (nb < prev) ? 0.75f * nb + 0.25f * prev : 0.90625f * nb + 0.09375f * prev;
        if (t < 0.015625f) t = 0.0f;
        if (t >= 0.99609375f) t = 0.99609375f;
        ch->bw_array[k] = t;
        ch->invf_mode_prev[k] = ch->invf_mode[k];
    }
}

/* Second-order covariance LPC of one low band over the whole buffer. */
static void sbr_lpc(float x[SBR_BUF_SLOTS][2], float a0[2], float a1[2])
{
    float p01r = 0, p01i = 0, p02r = 0, p02i = 0, p11 = 0, p12r = 0, p12i = 0, p22 = 0;
    for (int n = SBR_T_HFADJ; n < SBR_BUF_SLOTS; n++) {
        float x0r = x[n][0], x0i = x[n][1];
        float x1r = x[n - 1][0], x1i = x[n - 1][1];
        float x2r = x[n - 2][0], x2i = x[n - 2][1];
        /* phi(i,j) = sum x(n-i) conj(x(n-j)) */
        p01r += x0r * x1r + x0i * x1i; p01i += x0i * x1r - x0r * x1i;
        p02r += x0r * x2r + x0i * x2i; p02i += x0i * x2r - x0r * x2i;
        p12r += x1r * x2r + x1i * x2i; p12i += x1i * x2r - x1r * x2i;
        p11 += x1r * x1r + x1i * x1i;
        p22 += x2r * x2r + x2i * x2i;
    }
    float d = p22 * p11 - (p12r * p12r + p12i * p12i) * (1.0f / 1.000001f);
    float a1r = 0, a1i = 0, a0r = 0, a0i = 0;
    if (d != 0.0f) {
        /* alpha1 = (phi01 phi12 - phi02 phi11) / d */
        float inv = 1.0f / d;
        a1r = (p01r * p12r - p01i * p12i - p02r * p11) * inv;
        a1i = (p01r * p12i + p01i * p12r - p02i * p11) * inv;
    }
    if (p11 != 0.0f) {
        /* alpha0 = -(phi01 + alpha1 conj(phi12)) / phi11 */
        float inv = 1.0f / p11;
        a0r = -(p01r + a1r * p12r + a1i * p12i) * inv;
        a0i = -(p01i + a1i * p12r - a1r * p12i) * inv;
    }
    if (a0r * a0r + a0i * a0i >= 16.0f || a1r * a1r + a1i * a1i >= 16.0f) {
        a0r = a0i = a1r = a1i = 0.0f;
    }
    a0[0] = a0r; a0[1] = a0i;
    a1[0] = a1r; a1[1] = a1i;
}

static void sbr_hf_generate(const SBRElement *el, SBRChannel *ch, SBRScratch *sc)
{
    float a0[32][2], a1[32][2];
    for (int p = 0; p < el->k0; p++) sbr_lpc(sc->x_low[p], a0[p], a1[p]);

    int start = 2 * ch->t_E[0] + SBR_T_HFADJ;
    int end = 2 * ch->t_E[ch->L_E] + SBR_T_HFADJ;
    int k = el->kx, g = 0;
    for (int i = 0; i < el->num_patches; i++) {
        for (int x = 0; x < el->patch_num[i]; x++, k++) {
            int p = el->patch_start[i] + x;
            while (g < el->n_q && k >= el->f_noise[g + 1]) g++;
            float bw = ch->bw_array[g], bw2 = bw * bw;
            float c0r = a0[p][0] * bw, c0i = a0[p][1] * bw;
            float c1r = a1[p][0] * bw2, c1i = a1[p][1] * bw2;
            float (*xl)[2] = sc->x_low[p];
            float (*xh)[2] = sc->x_high[k];
            for (int n = start; n < end; n++) {
                xh[n][0] = xl[n][0] + c0r * xl[n - 1][0] - c0i * xl[n - 1][1] + c1r * xl[n - 2][0] - c1i * xl[n - 2][1];
                xh[n][1] = xl[n][1] + c0r * xl[n - 1][1] + c0i * xl[n - 1][0] + c1r * xl[n - 2][1] + c1i * xl[n - 2][0];
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Envelope adjustment (§4.6.18.7)                                           */
/* ------------------------------------------------------------------------ */

static void sbr_dequant(const SBRElement *el, SBRChannel *c0, SBRChannel *c1,
                        float E0[SBR_MAX_ENV][SBR_MAX_BANDS], float Q0[2][SBR_MAX_NQ],
                        float E1[SBR_MAX_ENV][SBR_MAX_BANDS], float Q1[2][SBR_MAX_NQ])
{
    if (!el->coupling || !c1) {
        SBRChannel *chs[2] = { c0, c1 };
        float (*Es[2])[SBR_MAX_BANDS] = { E0, E1 };
        float (*Qs[2])[SBR_MAX_NQ] = { Q0, Q1 };
        for (int c = 0; c < 2 && chs[c]; c++) {
            SBRChannel *ch = chs[c];
            float a = ch->amp_res ? 1.0f : 0.5f;
            for (int l = 0; l < ch->L_E; l++) {
                int nb = ch->freq_res[l] ? el->n_high : el->n_low;
                for (int k = 0; k < nb; k++) Es[c][l][k] = 64.0f * powf(2.0f, a * ch->E[l][k]);
            }
            for (int l = 0; l < ch->L_Q; l++)
                for (int k = 0; k < el->n_q; k++) Qs[c][l][k] = powf(2.0f, (float)(SBR_NOISE_FLOOR_OFFSET - ch->Q[l][k]));
        }
        return;
    }
    /* Coupled pair: channel 0 carries the level, channel 1 the balance. */
    float a = c0->amp_res ? 1.0f : 0.5f;
    int pan = c0->amp_res ? 12 : 24;
    for (int l = 0; l < c0->L_E; l++) {
        int nb = c0->freq_res[l] ? el->n_high : el->n_low;
        for (int k = 0; k < nb; k++) {
            float e = 64.0f * powf(2.0f, a * c0->E[l][k]);
            float b = powf(2.0f, a * (pan - c1->E[l][k]));
            E0[l][k] = 2.0f * e / (1.0f + b);
            E1[l][k] = 2.0f * e / (1.0f + 1.0f / b);
        }
    }
    for (int l = 0; l < c0->L_Q; l++) {
        for (int k = 0; k < el->n_q; k++) {
            float q = powf(2.0f, (float)(SBR_NOISE_FLOOR_OFFSET - c0->Q[l][k]));
            float b = powf(2.0f, (float)(12 - c1->Q[l][k]));
            Q0[l][k] = 2.0f * q / (1.0f + b);
            Q1[l][k] = 2.0f * q / (1.0f + 1.0f / b);
        }
    }
}

static void sbr_hf_adjust(const SBRElement *el, SBRChannel *ch, SBRScratch *sc,
                          float E[SBR_MAX_ENV][SBR_MAX_BANDS], float Q[2][SBR_MAX_NQ])
{
    int kx = el->kx, M = el->M;
    float e_orig[SBR_MAX_BANDS], q_map[SBR_MAX_BANDS], e_curr[SBR_MAX_BANDS];
    float gain[SBR_MAX_BANDS], q_m[SBR_MAX_BANDS], s_m[SBR_MAX_BANDS];
    uint8_t s_index[SBR_MAX_BANDS], s_mapped[SBR_MAX_BANDS];
    /* Smoothing history is primed with the first envelope after a reset. */
    bool prime_hist = !ch->primed;
    ch->primed = true;

    for (int l = 0; l < ch->L_E; l++) {
        bool hi = ch->freq_res[l];
        const uint8_t *tab = hi ? el->f_high : el->f_low;
        int nb = hi ? el->n_high : el->n_low;
        int q_env = (ch->L_Q > 1 && ch->t_E[l] >= ch->t_Q[1]) ? 1 : 0;
        int slot0 = 2 * ch->t_E[l] + SBR_T_HFADJ, slot1 = 2 * ch->t_E[l + 1] + SBR_T_HFADJ;
        bool transient = (l == ch->l_A) || (l == 0 && ch->l_A_prev == (int)ch->L_E_prev && ch->L_E_prev > 0);

        /* --- mapping (§4.6.18.7.2) --- */
        for (int i = 0; i < nb; i++)
            for (int k = tab[i]; k < tab[i + 1]; k++) e_orig[k - kx] = E[l][i];
        for (int i = 0; i < el->n_q; i++)
            for (int k = el->f_noise[i]; k < el->f_noise[i + 1]; k++) q_map[k - kx] = Q[q_env][i];
        memset(s_index, 0, (size_t)M);
        memset(s_mapped, 0, (size_t)M);
        if (ch->add_harmonic_flag) {
            for (int i = 0; i < el->n_high; i++) {
                if (!ch->add_harmonic[i]) continue;
                int mid = (el->f_high[i] + el->f_high[i + 1]) >> 1;
                if (l >= ch->l_A || ch->s_index_prev[mid - kx]) s_index[mid - kx] = 1;
            }
        }
        for (int i = 0; i < nb; i++) {
            int any = 0;
            for (int k = tab[i]; k < tab[i + 1]; k++) any |= s_index[k - kx];
            for (int k = tab[i]; k < tab[i + 1]; k++) s_mapped[k - kx] = (uint8_t)any;
        }

        /* --- E_curr estimation (§4.6.18.7.3) --- */
        float inv_slots = 1.0f / (float)(slot1 - slot0);
        if (el->interpol_freq) {
            for (int m = 0; m < M; m++) {
                float (*xh)[2] = sc->x_high[kx + m];
                float acc = 0.0f;
                for (int n = slot0; n < slot1; n++) acc += xh[n][0] * xh[n][0] + xh[n][1] * xh[n][1];
                e_curr[m] = acc * inv_slots;
            }
        } else {
            for (int i = 0; i < nb; i++) {
                float acc = 0.0f;
                for (int k = tab[i]; k < tab[i + 1]; k++) {
                    float (*xh)[2] = sc->x_high[k];
                    for (int n = slot0; n < slot1; n++) acc += xh[n][0] * xh[n][0] + xh[n][1] * xh[n][1];
                }
                acc *= inv_slots / (float)(tab[i + 1] - tab[i]);
                for (int k = tab[i]; k < tab[i + 1]; k++) e_curr[k - kx] = acc;
            }
        }

        /* --- gains (§4.6.18.7.5) --- */
        for (int m = 0; m < M; m++) {
            float eo = e_orig[m], q = q_map[m], ec = e_curr[m];
            q_m[m] = sqrtf(eo * q / (1.0f + q));
            s_m[m] = s_index[m] ? sqrtf(eo / (1.0f + q)) : 0.0f;
            if (!s_mapped[m])
                gain[m] = sqrtf(eo / ((1.0f + ec) * (1.0f + (transient ? 0.0f : q))));
            else
                gain[m] = sqrtf(eo * q / ((1.0f + ec) * (1.0f + q)));
        }

        /* limiter and boost per limiter band */
        for (int i = 0; i < el->n_lim; i++) {
            int m0 = el->f_lim[i] - kx, m1 = el->f_lim[i + 1] - kx;
            if (m0 < 0) m0 = 0;
            if (m1 > M) m1 = M;
            if (m1 <= m0) continue;
            float so = 1e-12f, sc_ = 1e-12f;
            for (int m = m0; m < m1; m++) { so += e_orig[m]; sc_ += e_curr[m]; }
            float g_max = sqrtf(so / sc_) * sbr_lim_gain[el->limiter_gains];
            if (g_max > 1e5f) g_max = 1e5f;
            for (int m = m0; m < m1; m++) {
                if (gain[m] > g_max) {
                    q_m[m] *= g_max / gain[m];
                    gain[m] = g_max;
                }
            }
            float den = 1e-12f;
            for (int m = m0; m < m1; m++) {
                den += e_curr[m] * gain[m] * gain[m] + s_m[m] * s_m[m];
                if (!transient && s_m[m] == 0.0f) den += q_m[m] * q_m[m];
            }
            float boost = sqrtf(so / den);
            if (boost > 1.584893192f) boost = 1.584893192f;
            for (int m = m0; m < m1; m++) {
                gain[m] *= boost;
                q_m[m] *= boost;
                s_m[m] *= boost;
            }
        }

        if (prime_hist) {
            for (int h = 0; h < 4; h++) {
                memcpy(ch->g_hist[h], gain, sizeof(float) * (size_t)M);
                memcpy(ch->q_hist[h], q_m, sizeof(float) * (size_t)M);
            }
            prime_hist = false;
        }

        /* --- assembly (§4.6.18.7.6) --- */
        float g_filt[SBR_MAX_BANDS], q_filt[SBR_MAX_BANDS];
        bool smooth = (el->smoothing_mode == 0) && !transient;
        for (int m = 0; m < M; m++) {
            if (smooth) {
                float g = gain[m] * sbr_h_smooth[0], q = q_m[m] * sbr_h_smooth[0];
                for (int h = 0; h < 4; h++) {
                    g += ch->g_hist[3 - h][m] * sbr_h_smooth[h + 1];
                    q += ch->q_hist[3 - h][m] * sbr_h_smooth[h + 1];
                }
                g_filt[m] = g;
                q_filt[m] = q;
            } else {
                g_filt[m] = gain[m];
                q_filt[m] = transient ? 0.0f : q_m[m];
            }
        }
        /* history: oldest at [0] */
        memmove(ch->g_hist[0], ch->g_hist[1], sizeof(float) * SBR_MAX_BANDS * 3);
        memmove(ch->q_hist[0], ch->q_hist[1], sizeof(float) * SBR_MAX_BANDS * 3);
        memcpy(ch->g_hist[3], gain, sizeof(float) * (size_t)M);
        memcpy(ch->q_hist[3], q_m, sizeof(float) * (size_t)M);

        for (int n = slot0; n < slot1; n++) {
            ch->index_noise = (uint16_t)((ch->index_noise + 1) & 511);
            int phase = ch->index_sine;
            ch->index_sine = (uint8_t)((ch->index_sine + 1) & 3);
            /* (-1)^k sign alternation of the sinusoid's imaginary part */
            float sign = (kx & 1) ? -1.0f : 1.0f;
            for (int m = 0; m < M; m++) {
                const float *xh = sc->x_high[kx + m][n];
                float yr = xh[0] * g_filt[m], yi = xh[1] * g_filt[m];
                if (s_m[m] != 0.0f) {
                    /* cos(pi/2 idx) real, sin(pi/2 idx) imaginary */
                    static const float ph_c[4] = { 1.0f, 0.0f, -1.0f, 0.0f };
                    static const float ph_s[4] = { 0.0f, 1.0f, 0.0f, -1.0f };
                    yr += s_m[m] * ph_c[phase];
                    yi += s_m[m] * ph_s[phase] * sign;
                } else {
                    int idx = (ch->index_noise + m) & 511;
                    yr += q_filt[m] * sbr_noise_table[idx][0];
                    yi += q_filt[m] * sbr_noise_table[idx][1];
                }
                sign = -sign;
                sc->y[kx + m][n][0] = yr;
                sc->y[kx + m][n][1] = yi;
            }
            ch->index_noise = (uint16_t)((ch->index_noise + M - 1) & 511);
        }
        if (l == ch->L_E - 1) memcpy(ch->s_index_prev, s_index, (size_t)M);
    }
}

/* ------------------------------------------------------------------------ */
/* Frame processing                                                          */
/* ------------------------------------------------------------------------ */

/* Run the analysis bank on one channel's core PCM, filling the scratch
 * X_low buffer (previous tail + 32 new slots). */
static void sbr_analyse(SBRChannel *ch, SBRScratch *sc, const float *pcm)
{
    for (int k = 0; k < 32; k++)
        memcpy(sc->x_low[k], ch->x_low_tail[k], sizeof(ch->x_low_tail[k]));
    float slot[32][2];
    for (int t = 0; t < SBR_SLOTS; t++) {
        qmf_analysis_slot(ch, pcm + t * 32, slot);
        for (int k = 0; k < 32; k++) {
            sc->x_low[k][SBR_T_HFGEN + t][0] = slot[k][0];
            sc->x_low[k][SBR_T_HFGEN + t][1] = slot[k][1];
        }
    }
    for (int k = 0; k < 32; k++)
        memcpy(ch->x_low_tail[k], &sc->x_low[k][SBR_SLOTS], sizeof(ch->x_low_tail[k]));
}

/* Assemble X for the 32 output slots from the low band and the adjusted HF,
 * honouring the previous frame's band split where its last envelope reaches
 * into this frame (§4.6.18.7.6 / 4.6.18.8.1). */
static void sbr_assemble(const SBRElement *el, SBRChannel *ch, SBRScratch *sc, bool have_hf, int nslots)
{
    int i_temp = have_hf ? (int)ch->t_E_end_prev - SBR_SLOTS : 0;
    if (i_temp < 0) i_temp = 0;
    for (int i = 0; i < nslots; i++) {
        int n = i + SBR_T_HFADJ;
        int kx = have_hf ? ((i < i_temp) ? ch->kx_prev : el->kx) : 32;
        int kend = have_hf ? ((i < i_temp) ? ch->kx_prev + ch->M_prev : el->kx + el->M) : 32;
        if (i >= SBR_SLOTS) kend = kx; /* look-ahead slots: low band only */
        for (int k = 0; k < kx && k < 32; k++) {
            sc->x[i][k][0] = sc->x_low[k][n][0];
            sc->x[i][k][1] = sc->x_low[k][n][1];
        }
        for (int k = kx; k < kend; k++) {
            sc->x[i][k][0] = sc->y[k][n][0];
            sc->x[i][k][1] = sc->y[k][n][1];
        }
        for (int k = kend; k < 64; k++) sc->x[i][k][0] = sc->x[i][k][1] = 0.0f;
    }
}

static void sbr_process_channel(const SBRElement *el, SBRChannel *ch, SBRScratch *sc, const float *pcm,
                                float E[SBR_MAX_ENV][SBR_MAX_BANDS], float Q[2][SBR_MAX_NQ], bool have_hf, int nslots)
{
    sbr_analyse(ch, sc, pcm);

    /* Y carries the previous frame's tail; the region this frame adjusts is
     * written below and the rest stays zero. */
    memset(sc->y, 0, sizeof(sc->y));
    for (int k = 0; k < SBR_MAX_BANDS; k++)
        memcpy(sc->y[k], ch->y_tail[k], sizeof(ch->y_tail[k]));

    if (have_hf) {
        sbr_chirp(el, ch);
        sbr_hf_generate(el, ch, sc);
        sbr_hf_adjust(el, ch, sc, E, Q);
    }

    sbr_assemble(el, ch, sc, have_hf, nslots);

    for (int k = 0; k < SBR_MAX_BANDS; k++)
        memcpy(ch->y_tail[k], &sc->y[k][SBR_SLOTS], sizeof(ch->y_tail[k]));

    if (have_hf) {
        /* remember what the next frame's leading slots and deltas refer to */
        int last = ch->L_E - 1;
        int nb = ch->freq_res[last] ? el->n_high : el->n_low;
        memcpy(ch->E_prev, ch->E[last], sizeof(int16_t) * (size_t)nb);
        ch->freq_res_prev = ch->freq_res[last];
        memcpy(ch->Q_prev, ch->Q[ch->L_Q - 1], sizeof(int16_t) * el->n_q);
        ch->l_A_prev = ch->l_A;
        ch->L_E_prev = ch->L_E;
        ch->t_E_end_prev = (uint8_t)(2 * ch->t_E[ch->L_E]);
        ch->kx_prev = el->kx;
        ch->M_prev = el->M;
    }
}

#else /* FAAD_DISABLE_SBR */

void init_qmf_twiddles(void) {}

faad_status sbr_decode_extension(struct faad_decoder *dec, BitReader *bs, uint32_t ch0, uint32_t syntax_id, bool crc)
{
    (void)dec; (void)bs; (void)ch0; (void)syntax_id; (void)crc;
    return FAAD_OK;
}
#endif /* FAAD_DISABLE_SBR */

/* ------------------------------------------------------------------------ */
/* Entry point                                                               */
/* ------------------------------------------------------------------------ */

void sbr_apply(struct faad_decoder *dec, uint32_t num_ch, float *pcm_in, float *pcm_out)
{
#ifndef FAAD_DISABLE_SBR
    SBRScratch *sc = &dec->sbr_scratch;
    float E0[SBR_MAX_ENV][SBR_MAX_BANDS], E1[SBR_MAX_ENV][SBR_MAX_BANDS];
    float Q0[2][SBR_MAX_NQ], Q1[2][SBR_MAX_NQ];

    for (uint32_t ch = 0; ch < num_ch; ) {
        SBRElement *el = &dec->sbr_el[ch];
        bool pair = el->header_present && el->nch == 2 && (ch + 1 < num_ch);
        int nch = pair ? 2 : 1;
        bool have_hf = el->header_present && dec->sbr[ch].have_frame;

        if (have_hf) sbr_dequant(el, &dec->sbr[ch], pair ? &dec->sbr[ch + 1] : NULL, E0, Q0, E1, Q1);

#ifndef FAAD_DISABLE_PS
        if (dec->ps_present && num_ch == 1) {
            sbr_process_channel(el, &dec->sbr[0], sc, pcm_in, E0, Q0, have_hf, PS_IN_SLOTS);
            dec->num_channels = 2;
            float (*L)[64][2] = sc->ps_out[0], (*R)[64][2] = sc->ps_out[1];
            if (dec->ps.start) {
                ps_apply(dec, sc->x, L, R, have_hf ? el->kx + el->M : 32);
            } else {
                memcpy(L, sc->x, sizeof(sc->ps_out[0]));
                memcpy(R, sc->x, sizeof(sc->ps_out[0]));
            }
            for (int t = 0; t < SBR_SLOTS; t++) {
#ifdef FAAD_D_SBR
                qmf_synthesis_slot_ds(&dec->sbr[0], L[t], pcm_out + t * 32);
                qmf_synthesis_slot_ds(&dec->sbr[1], R[t], pcm_out + 1024 + t * 32);
#else
                qmf_synthesis_slot(&dec->sbr[0], L[t], pcm_out + t * 64);
                qmf_synthesis_slot(&dec->sbr[1], R[t], pcm_out + 2048 + t * 64);
#endif
            }
            return;
        }
#endif
        for (int c = 0; c < nch; c++) {
            SBRChannel *sch = &dec->sbr[ch + c];
            sbr_process_channel(el, sch, sc, pcm_in + (ch + c) * FRAME_LEN_LONG, c ? E1 : E0, c ? Q1 : Q0, have_hf, SBR_SLOTS);
#ifdef FAAD_D_SBR
            for (int t = 0; t < SBR_SLOTS; t++)
                qmf_synthesis_slot_ds(sch, sc->x[t], pcm_out + (ch + c) * 1024 + t * 32);
#else
            for (int t = 0; t < SBR_SLOTS; t++)
                qmf_synthesis_slot(sch, sc->x[t], pcm_out + (ch + c) * 2048 + t * 64);
#endif
        }
        ch += (uint32_t)nch;
    }
#else
    (void)dec;
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

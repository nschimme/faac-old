/*
 * Parametric stereo decoder, ISO/IEC 14496-3 §8.6 (HE-AAC v2).
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

#ifndef FAAD_DISABLE_PS

/* Band layout for the 20- and 34-parameter configurations. */
static const int ps_nr_par_bands[2]    = { 20, 34 };
static const int ps_nr_ipdopd_bands[2] = { 11, 17 };
static const int ps_nr_bands[2]        = { 71, 91 };
static const int ps_decay_cutoff[2]    = { 10, 32 };
static const int ps_nr_allpass[2]      = { 30, 50 };
static const int ps_short_delay_band[2] = { 42, 62 };
#define PS_DECAY_SLOPE 0.05f

static const uint8_t ps_nr_iidicc_par[6] = { 10, 20, 34, 10, 20, 34 };
static const uint8_t ps_nr_ipdopd_par[6] = { 5, 11, 17, 5, 11, 17 };
static const uint8_t ps_num_env_tab[2][4] = { { 0, 1, 2, 4 }, { 1, 2, 3, 4 } };

/* Table 8.48 / 8.49: parameter band b(k) of each hybrid sub-band k. */
static const int8_t ps_k_to_b_20[71] = {
     1,  0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 14, 15,
    15, 15, 16, 16, 16, 16, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19
};
static const int8_t ps_k_to_b_34[91] = {
     0,  1,  2,  3,  4,  5,  6,  6,  7,  2,  1,  0, 10, 10,  4,  5,  6,  7,  8,
     9, 10, 11, 12,  9, 14, 11, 12, 13, 14, 15, 16, 13, 16, 17, 18, 19, 20, 21,
    22, 22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 27, 28, 28, 28, 29, 29, 29,
    30, 30, 30, 31, 31, 31, 31, 32, 32, 32, 32, 33, 33, 33, 33, 33, 33, 33, 33,
    33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33
};

/* Hybrid filter prototypes (§8.6.4.3), first 7 of 13 symmetric taps. */
static const float ps_g0_q8[7]  = { 0.00746082949812f, 0.02270420949825f, 0.04546865930473f, 0.07266113929591f, 0.09885108575264f, 0.11793710567217f, 0.125f };
static const float ps_g0_q12[7] = { 0.04081179924692f, 0.03812810994926f, 0.05144908135699f, 0.06399831151592f, 0.07428313801106f, 0.08100347892914f, 0.08333333333333f };
static const float ps_g1_q8[7]  = { 0.01565675600122f, 0.03752716391991f, 0.05417891378782f, 0.08417044116767f, 0.10307344158036f, 0.12222452249753f, 0.125f };
static const float ps_g2_q4[7]  = { -0.05908211155639f, -0.04871498374946f, 0.0f, 0.07778723915851f, 0.16486303567403f, 0.23279856662996f, 0.25f };
static const float ps_g1_q2[7]  = { 0.0f, 0.01899487526049f, 0.0f, -0.07293139167538f, 0.0f, 0.30596630545168f, 0.5f };

/* IID quantisation levels in dB (Tables 8.24 / 8.25) and ICC values (Table 8.26). */
static const int8_t ps_iid_db_default[15] = { -25, -18, -14, -10, -7, -4, -2, 0, 2, 4, 7, 10, 14, 18, 25 };
static const int8_t ps_iid_db_fine[31] = { -50, -45, -40, -35, -30, -25, -22, -19, -16, -13, -10, -8, -6, -4, -2, 0,
                                           2, 4, 6, 8, 10, 13, 16, 19, 22, 25, 30, 35, 40, 45, 50 };
static const float ps_icc_invq[8] = { 1.0f, 0.937f, 0.84118f, 0.60092f, 0.36764f, 0.0f, -0.589f, -1.0f };

/* Sub-band centre frequencies in QMF-band units, hybrid bands only. */
static const int8_t ps_f_center_20[10] = { -3, -1, 1, 3, 5, 7, 10, 14, 18, 22 };            /* /8  */
static const int8_t ps_f_center_34[32] = { 2, 6, 10, 14, 18, 22, 26, 30, 34, -10, -6, -2, 51, 57, 15, 21,
                                           27, 33, 39, 45, 54, 66, 78, 42, 102, 66, 78, 90, 102, 114, 126, 90 }; /* /24 */

/* ---- derived tables ---- */
static float f20_0_8[8][7][2], f34_0_12[12][7][2], f34_1_8[8][7][2], f34_2_4[4][7][2];
static float ps_phi_fract[2][50][2];
static float ps_q_fract[2][50][3][2];
static float ps_HA[46][8][4], ps_HB[46][8][4];
static bool ps_tables_init = false;

static void ps_make_filter(float (*f)[7][2], const float *proto, int bands)
{
    for (int q = 0; q < bands; q++)
        for (int n = 0; n < 7; n++) {
            double theta = 2.0 * M_PI * (q + 0.5) * (n - 6) / bands;
            f[q][n][0] = (float)(proto[n] * cos(theta));
            f[q][n][1] = (float)(proto[n] * -sin(theta));
        }
}

void init_ps_tables(void)
{
    if (ps_tables_init) return;
    static const double links[3] = { 0.43, 0.75, 0.347 };
    const double gain = 0.39;

    ps_make_filter(f20_0_8, ps_g0_q8, 8);
    ps_make_filter(f34_0_12, ps_g0_q12, 12);
    ps_make_filter(f34_1_8, ps_g1_q8, 8);
    ps_make_filter(f34_2_4, ps_g2_q4, 4);

    for (int is34 = 0; is34 < 2; is34++) {
        for (int k = 0; k < ps_nr_allpass[is34]; k++) {
            double fc;
            if (!is34) fc = (k < 10) ? ps_f_center_20[k] / 8.0 : k - 6.5;
            else       fc = (k < 32) ? ps_f_center_34[k] / 24.0 : k - 26.5;
            for (int m = 0; m < 3; m++) {
                double th = -M_PI * links[m] * fc;
                ps_q_fract[is34][k][m][0] = (float)cos(th);
                ps_q_fract[is34][k][m][1] = (float)sin(th);
            }
            double th = -M_PI * gain * fc;
            ps_phi_fract[is34][k][0] = (float)cos(th);
            ps_phi_fract[is34][k][1] = (float)sin(th);
        }
    }

    /* Mixing matrices (§8.6.4.6.2 type A, §8.6.4.6.3 type B) for every IID/ICC index. */
    for (int i = 0; i < 46; i++) {
        double db = (i < 15) ? ps_iid_db_default[i] : ps_iid_db_fine[i - 15];
        double c = pow(10.0, db / 20.0);
        double c1 = sqrt(2.0) / sqrt(1.0 + c * c);
        double c2 = c * c1;
        for (int icc = 0; icc < 8; icc++) {
            double alpha = 0.5 * acos(ps_icc_invq[icc]);
            double beta = alpha * (c1 - c2) / sqrt(2.0);
            ps_HA[i][icc][0] = (float)(c2 * cos(beta + alpha));
            ps_HA[i][icc][1] = (float)(c1 * cos(beta - alpha));
            ps_HA[i][icc][2] = (float)(c2 * sin(beta + alpha));
            ps_HA[i][icc][3] = (float)(c1 * sin(beta - alpha));

            double rho = ps_icc_invq[icc] > 0.05 ? ps_icc_invq[icc] : 0.05;
            double a = 0.5 * atan2(2.0 * c * rho, c * c - 1.0);
            double mu = c + 1.0 / c;
            mu = sqrt(1.0 + (4.0 * rho * rho - 4.0) / (mu * mu));
            double gamma = atan(sqrt((1.0 - mu) / (1.0 + mu)));
            if (a < 0) a += M_PI / 2;
            ps_HB[i][icc][0] = (float)( sqrt(2.0) * cos(a) * cos(gamma));
            ps_HB[i][icc][1] = (float)( sqrt(2.0) * sin(a) * cos(gamma));
            ps_HB[i][icc][2] = (float)(-sqrt(2.0) * sin(a) * sin(gamma));
            ps_HB[i][icc][3] = (float)( sqrt(2.0) * cos(a) * sin(gamma));
        }
    }
    ps_tables_init = true;
}

/* ------------------------------------------------------------------------ */
/* Bitstream (§8.6.2)                                                        */
/* ------------------------------------------------------------------------ */

static int ps_huff(BitReader *bs, const SBRHuffEntry *tab, int nsyms, int offset)
{
    uint32_t code = 0;
    for (int len = 1; len <= 20; len++) {
        code = (code << 1) | bits_get_1(bs);
        for (int i = 0; i < nsyms; i++)
            if (tab[i].len == (uint32_t)len && tab[i].code == code) return i - offset;
    }
    return 0;
}

/* Deltas along frequency (df) or against the previous envelope (dt). */
static bool ps_read_par(BitReader *bs, PSState *ps, int8_t par[PS_MAX_ENV][PS_NR_PAR], int num, int e, bool dt,
                        const SBRHuffEntry *tab, int nsyms, int offset, int mask, int limit)
{
    if (dt) {
        int e_prev = e ? e - 1 : (int)ps->num_env_old - 1;
        if (e_prev < 0) e_prev = 0;
        for (int b = 0; b < num; b++) {
            int v = par[e_prev][b] + ps_huff(bs, tab, nsyms, offset);
            if (mask) v &= mask;
            par[e][b] = (int8_t)v;
            if (limit && (v > limit || v < -limit)) return false;
        }
    } else {
        int v = 0;
        for (int b = 0; b < num; b++) {
            v += ps_huff(bs, tab, nsyms, offset);
            if (mask) v &= mask;
            par[e][b] = (int8_t)v;
            if (limit && (v > limit || v < -limit)) return false;
        }
    }
    return true;
}

static void ps_clear_params(PSState *ps)
{
    memset(ps->iid_par, 0, sizeof(ps->iid_par));
    memset(ps->icc_par, 0, sizeof(ps->icc_par));
    memset(ps->ipd_par, 0, sizeof(ps->ipd_par));
    memset(ps->opd_par, 0, sizeof(ps->opd_par));
}

/* ps_data(): bits_left is the extension payload still available. */
void ps_read_data(struct faad_decoder *dec, BitReader *bs, uint32_t bits_left)
{
    PSState *ps = &dec->ps;
    uint32_t start_pos = bits_get_consumed(bs);
    bool ok = true;

    if (bits_get(bs, 1)) { /* enable_ps_header */
        ps->enable_iid = bits_get(bs, 1);
        if (ps->enable_iid) {
            int mode = (int)bits_get(bs, 3);
            if (mode > 5) { ok = false; mode = 0; }
            ps->nr_iid_par = ps_nr_iidicc_par[mode];
            ps->iid_quant = mode > 2;
            ps->nr_ipdopd_par = ps_nr_ipdopd_par[mode];
        }
        ps->enable_icc = bits_get(bs, 1);
        if (ps->enable_icc) {
            int mode = (int)bits_get(bs, 3);
            if (mode > 5) { ok = false; mode = 0; }
            ps->icc_mode = (uint8_t)mode;
            ps->nr_icc_par = ps_nr_iidicc_par[mode];
        }
        ps->enable_ext = bits_get(bs, 1);
        ps->start = true;
    }

    ps->frame_class = (uint8_t)bits_get(bs, 1);
    ps->num_env_old = ps->num_env;
    int num_env = ps_num_env_tab[ps->frame_class & 1][bits_get(bs, 2) & 3];
    ps->num_env = (uint8_t)num_env;
    ps->border[0] = -1;
    if (ps->frame_class) {
        for (int e = 1; e <= num_env && e < PS_MAX_ENV; e++) {
            ps->border[e] = (int8_t)bits_get(bs, 5);
            if (ps->border[e] < ps->border[e - 1]) ok = false;
        }
    } else {
        for (int e = 1; e <= num_env && e < PS_MAX_ENV; e++) {
            int shift = (ps->num_env == 4) ? 2 : (ps->num_env == 2) ? 1 : 0;
            ps->border[e] = (int8_t)(((e * 32) >> shift) - 1);
        }
    }

    if (ps->enable_iid) {
        int limit = ps->iid_quant ? 15 : 7;
        for (int e = 0; e < ps->num_env && ok; e++) {
            bool dt = bits_get(bs, 1);
            const SBRHuffEntry *tab; int n, off;
            if (ps->iid_quant) {
                tab = dt ? ps_huff_iid_dt_fine : ps_huff_iid_df_fine;
                n = PS_HUFF_IID_DT_FINE_NSYMS; off = PS_HUFF_IID_DT_FINE_OFFSET;
            } else {
                tab = dt ? ps_huff_iid_dt : ps_huff_iid_df;
                n = PS_HUFF_IID_DT_NSYMS; off = PS_HUFF_IID_DT_OFFSET;
            }
            ok = ps_read_par(bs, ps, ps->iid_par, ps->nr_iid_par, e, dt, tab, n, off, 0, limit);
        }
    } else {
        memset(ps->iid_par, 0, sizeof(ps->iid_par));
    }
    if (ps->enable_icc) {
        for (int e = 0; e < ps->num_env && ok; e++) {
            bool dt = bits_get(bs, 1);
            ok = ps_read_par(bs, ps, ps->icc_par, ps->nr_icc_par, e, dt,
                             dt ? ps_huff_icc_dt : ps_huff_icc_df, PS_HUFF_ICC_DT_NSYMS, PS_HUFF_ICC_DT_OFFSET, 0, 0);
            for (int b = 0; b < ps->nr_icc_par; b++) if ((unsigned)ps->icc_par[e][b] > 7) ok = false;
        }
    } else {
        memset(ps->icc_par, 0, sizeof(ps->icc_par));
    }

    if (ps->enable_ext && ok) {
        int cnt = (int)bits_get(bs, 4);
        if (cnt == 15) cnt += (int)bits_get(bs, 8);
        cnt *= 8;
        while (cnt > 7) {
            uint32_t before = bits_get_consumed(bs);
            int id = (int)bits_get(bs, 2);
            if (id == 0) {
                ps->enable_ipdopd = bits_get(bs, 1);
                if (ps->enable_ipdopd) {
                    for (int e = 0; e < ps->num_env; e++) {
                        bool dt = bits_get(bs, 1);
                        ps_read_par(bs, ps, ps->ipd_par, ps->nr_ipdopd_par, e, dt,
                                    dt ? ps_huff_ipd_dt : ps_huff_ipd_df, PS_HUFF_IPD_DT_NSYMS, 0, 7, 0);
                        dt = bits_get(bs, 1);
                        ps_read_par(bs, ps, ps->opd_par, ps->nr_ipdopd_par, e, dt,
                                    dt ? ps_huff_opd_dt : ps_huff_opd_df, PS_HUFF_OPD_DT_NSYMS, 0, 7, 0);
                    }
                }
                bits_skip(bs, 1); /* reserved_ps */
            }
            cnt -= (int)(bits_get_consumed(bs) - before);
        }
        if (cnt < 0) ok = false;
        else bits_skip(bs, (uint32_t)cnt);
    }

    if (ok) {
        /* A frame whose last border falls short of the frame end, or that
         * carries no envelope, gets one more envelope repeating the last
         * parameters up to slot 31 (§8.6.4.4). */
        if (ps->num_env == 0 || ps->border[ps->num_env] < 31) {
            int source = ps->num_env ? ps->num_env - 1 : (int)ps->num_env_old - 1;
            if (source >= 0 && source != ps->num_env) {
                memcpy(ps->iid_par[ps->num_env], ps->iid_par[source], sizeof(ps->iid_par[0]));
                memcpy(ps->icc_par[ps->num_env], ps->icc_par[source], sizeof(ps->icc_par[0]));
                memcpy(ps->ipd_par[ps->num_env], ps->ipd_par[source], sizeof(ps->ipd_par[0]));
                memcpy(ps->opd_par[ps->num_env], ps->opd_par[source], sizeof(ps->opd_par[0]));
            }
            ps->num_env++;
            ps->border[ps->num_env] = 31;
        }
        ps->is34_old = ps->is34;
        if (ps->enable_iid || ps->enable_icc)
            ps->is34 = (ps->enable_iid && ps->nr_iid_par == 34) || (ps->enable_icc && ps->nr_icc_par == 34);
        if (!ps->enable_ipdopd) {
            memset(ps->ipd_par, 0, sizeof(ps->ipd_par));
            memset(ps->opd_par, 0, sizeof(ps->opd_par));
        }
        dec->ps_present = true;
#ifdef FAAD_STATS
        dec->stats.psActiveFrames++;
#endif
    } else {
        ps->start = false;
        ps_clear_params(ps);
    }

    uint32_t used = bits_get_consumed(bs) - start_pos;
    if (used < bits_left) bits_skip(bs, bits_left - used);
}

/* ------------------------------------------------------------------------ */
/* Hybrid filterbank (§8.6.4.3)                                              */
/* ------------------------------------------------------------------------ */

/* 13-tap complex split of one QMF band; in[] holds slots n..n+12. */
static void ps_hybrid_cx(float out[][PS_QMF_SLOTS][2], float (*in)[2], float (*f)[7][2], int bands, int stride_out)
{
    for (int n = 0; n < PS_QMF_SLOTS; n++, in++) {
        for (int q = 0; q < bands; q++) {
            float sr = f[q][6][0] * in[6][0], si = f[q][6][0] * in[6][1];
            for (int j = 0; j < 6; j++) {
                float r0 = in[j][0] + in[12 - j][0], r1 = in[j][1] - in[12 - j][1];
                float i0 = in[j][1] + in[12 - j][1], i1 = in[j][0] - in[12 - j][0];
                sr += f[q][j][0] * r0 - f[q][j][1] * r1;
                si += f[q][j][0] * i0 + f[q][j][1] * i1;
            }
            out[q * stride_out][n][0] = sr;
            out[q * stride_out][n][1] = si;
        }
    }
}

/* 8-way split of QMF band 0 folded to 6 sub-bands in frequency order. */
static void ps_hybrid6(float out[][PS_QMF_SLOTS][2], float (*in)[2])
{
    float tmp[8][PS_QMF_SLOTS][2];
    ps_hybrid_cx(tmp, in, f20_0_8, 8, 1);
    for (int n = 0; n < PS_QMF_SLOTS; n++) {
        out[0][n][0] = tmp[6][n][0]; out[0][n][1] = tmp[6][n][1];
        out[1][n][0] = tmp[7][n][0]; out[1][n][1] = tmp[7][n][1];
        out[2][n][0] = tmp[0][n][0]; out[2][n][1] = tmp[0][n][1];
        out[3][n][0] = tmp[1][n][0]; out[3][n][1] = tmp[1][n][1];
        out[4][n][0] = tmp[2][n][0] + tmp[5][n][0]; out[4][n][1] = tmp[2][n][1] + tmp[5][n][1];
        out[5][n][0] = tmp[3][n][0] + tmp[4][n][0]; out[5][n][1] = tmp[3][n][1] + tmp[4][n][1];
    }
}

/* Two-way split with the real, odd-tap-only prototype g1_Q2. */
static void ps_hybrid2(float out[][PS_QMF_SLOTS][2], float (*in)[2], int reverse)
{
    for (int n = 0; n < PS_QMF_SLOTS; n++, in++) {
        float re_in = ps_g1_q2[6] * in[6][0], im_in = ps_g1_q2[6] * in[6][1];
        float re_op = 0.0f, im_op = 0.0f;
        for (int j = 0; j < 6; j += 2) {
            re_op += ps_g1_q2[j + 1] * (in[j + 1][0] + in[11 - j][0]);
            im_op += ps_g1_q2[j + 1] * (in[j + 1][1] + in[11 - j][1]);
        }
        out[reverse][n][0] = re_in + re_op;  out[reverse][n][1] = im_in + im_op;
        out[!reverse][n][0] = re_in - re_op; out[!reverse][n][1] = im_in - im_op;
    }
}

static void ps_hybrid_analysis(PSState *ps, float out[PS_NR_BANDS][PS_QMF_SLOTS][2], float X[PS_IN_SLOTS][64][2], bool is34)
{
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < PS_IN_SLOTS; j++) {
            ps->in_buf[i][j + 6][0] = X[j][i][0];
            ps->in_buf[i][j + 6][1] = X[j][i][1];
        }
    if (is34) {
        ps_hybrid_cx(out,      ps->in_buf[0], f34_0_12, 12, 1);
        ps_hybrid_cx(out + 12, ps->in_buf[1], f34_1_8,   8, 1);
        ps_hybrid_cx(out + 20, ps->in_buf[2], f34_2_4,   4, 1);
        ps_hybrid_cx(out + 24, ps->in_buf[3], f34_2_4,   4, 1);
        ps_hybrid_cx(out + 28, ps->in_buf[4], f34_2_4,   4, 1);
        for (int k = 5; k < 64; k++)
            for (int n = 0; n < PS_QMF_SLOTS; n++) { out[k + 27][n][0] = X[n][k][0]; out[k + 27][n][1] = X[n][k][1]; }
    } else {
        ps_hybrid6(out, ps->in_buf[0]);
        ps_hybrid2(out + 6, ps->in_buf[1], 1);
        ps_hybrid2(out + 8, ps->in_buf[2], 0);
        for (int k = 3; k < 64; k++)
            for (int n = 0; n < PS_QMF_SLOTS; n++) { out[k + 7][n][0] = X[n][k][0]; out[k + 7][n][1] = X[n][k][1]; }
    }
    for (int i = 0; i < 5; i++)
        memmove(ps->in_buf[i], ps->in_buf[i] + PS_QMF_SLOTS, 6 * sizeof(ps->in_buf[i][0]));
}

/* Hybrid synthesis of one slot: the sub-bands of the split QMF bands sum
 * back, the others pass through. */
void ps_synthesis_slot(struct faad_decoder *dec, int side, int n, float out[64][2])
{
    static const uint8_t counts34[5] = { 12, 8, 4, 4, 4 };
    float (*in)[PS_QMF_SLOTS][2] = side ? dec->sbr_scratch.ps_r : dec->sbr_scratch.ps_l;
    if (dec->ps.is34) {
        int k = 0;
        for (int b = 0; b < 5; b++) {
            float sr = 0.0f, si = 0.0f;
            for (int q = 0; q < counts34[b]; q++, k++) { sr += in[k][n][0]; si += in[k][n][1]; }
            out[b][0] = sr; out[b][1] = si;
        }
        for (int b = 5; b < 64; b++) { out[b][0] = in[b + 27][n][0]; out[b][1] = in[b + 27][n][1]; }
    } else {
        float sr = 0.0f, si = 0.0f;
        for (int q = 0; q < 6; q++) { sr += in[q][n][0]; si += in[q][n][1]; }
        out[0][0] = sr; out[0][1] = si;
        out[1][0] = in[6][n][0] + in[7][n][0]; out[1][1] = in[6][n][1] + in[7][n][1];
        out[2][0] = in[8][n][0] + in[9][n][0]; out[2][1] = in[8][n][1] + in[9][n][1];
        for (int b = 3; b < 64; b++) { out[b][0] = in[b + 7][n][0]; out[b][1] = in[b + 7][n][1]; }
    }
}

/* ------------------------------------------------------------------------ */
/* Decorrelation (§8.6.4.6.1)                                                */
/* ------------------------------------------------------------------------ */

/* Decorrelation (§8.6.4.6.3). Each all-pass band runs
 *   H_k(z) = z^-2 phi_k prod_m (Q_m z^-d_m - a_m g_k) / (1 - a_m g_k Q_m z^-d_m)
 * with link delays d = 3, 4, 5, each link in direct form II so its state is
 * d_m samples; the delay bands are a plain 14- or 1-slot delay. The band's
 * history is loaded into a slot-contiguous working line, the frame is run,
 * and the tail is stored back, so the state is the history alone. The
 * transient detector scales every band of a parameter band by G_tr(b, n). */
static void ps_transient_gain(PSState *ps, float gain[PS_NR_PAR][PS_QMF_SLOTS], float s[PS_NR_BANDS][PS_QMF_SLOTS][2], bool is34)
{
    const float peak_decay = 0.76592833836465f, impact = 1.5f, alpha = 0.25f;
    const int8_t *k_to_b = is34 ? ps_k_to_b_34 : ps_k_to_b_20;
    float power[PS_NR_PAR][PS_QMF_SLOTS];

    memset(power, 0, sizeof(power));
    for (int k = 0; k < ps_nr_bands[is34]; k++) {
        float *pw = power[k_to_b[k]];
        for (int n = 0; n < PS_QMF_SLOTS; n++) pw[n] += s[k][n][0] * s[k][n][0] + s[k][n][1] * s[k][n][1];
    }
    for (int b = 0; b < ps_nr_par_bands[is34]; b++) {
        float peak = ps->peak_decay_nrg[b], smooth = ps->power_smooth[b], diff = ps->peak_decay_diff_smooth[b];
        for (int n = 0; n < PS_QMF_SLOTS; n++) {
            float pw = power[b][n];
            peak = peak * peak_decay;
            if (peak < pw) peak = pw;
            smooth += alpha * (pw - smooth);
            diff += alpha * (peak - pw - diff);
            float thr = impact * diff;
            gain[b][n] = (thr > smooth) ? smooth / thr : 1.0f;
        }
        ps->peak_decay_nrg[b] = peak;
        ps->power_smooth[b] = smooth;
        ps->peak_decay_diff_smooth[b] = diff;
    }
}

static void ps_decorrelate(PSState *ps, float out[PS_NR_BANDS][PS_QMF_SLOTS][2], float s[PS_NR_BANDS][PS_QMF_SLOTS][2], bool is34)
{
    static const float a[3] = { 0.65143905753106f, 0.56471812200776f, 0.48954165955695f };
    static const int link_delay[3] = { 3, 4, 5 };
    const int8_t *k_to_b = is34 ? ps_k_to_b_34 : ps_k_to_b_20;
    const int nr_allpass = ps_nr_allpass[is34];
    float gain[PS_NR_PAR][PS_QMF_SLOTS];

    if (is34 != ps->is34_old) {
        memset(ps->peak_decay_nrg, 0, sizeof(ps->peak_decay_nrg));
        memset(ps->power_smooth, 0, sizeof(ps->power_smooth));
        memset(ps->peak_decay_diff_smooth, 0, sizeof(ps->peak_decay_diff_smooth));
        memset(ps->dc_in, 0, sizeof(ps->dc_in));
        memset(ps->dc_ap, 0, sizeof(ps->dc_ap));
        memset(ps->dc_delay, 0, sizeof(ps->dc_delay));
    }
    ps_transient_gain(ps, gain, s, is34);

    int k = 0;
    for (; k < nr_allpass; k++) {
        const float *g = gain[k_to_b[k]];
        float slope = 1.0f - PS_DECAY_SLOPE * (float)(k - ps_decay_cutoff[is34]);
        if (slope < 0.0f) slope = 0.0f;
        if (slope > 1.0f) slope = 1.0f;
        const float phi_r = ps_phi_fract[is34][k][0], phi_i = ps_phi_fract[is34][k][1];

        /* u: the band delayed two slots and rotated by phi_k */
        float u[PS_QMF_SLOTS][2];
        float in[PS_QMF_SLOTS + 2][2];
        memcpy(in, ps->dc_in[k], sizeof(ps->dc_in[k]));
        memcpy(in + 2, s[k], sizeof(u));
        memcpy(ps->dc_in[k], in + PS_QMF_SLOTS, sizeof(ps->dc_in[k]));
        for (int n = 0; n < PS_QMF_SLOTS; n++) {
            u[n][0] = in[n][0] * phi_r - in[n][1] * phi_i;
            u[n][1] = in[n][0] * phi_i + in[n][1] * phi_r;
        }

        float *state = ps->dc_ap[k][0];
        for (int m = 0; m < 3; m++) {
            const int d = link_delay[m];
            const float ag = a[m] * slope;
            const float qr = ps_q_fract[is34][k][m][0], qi = ps_q_fract[is34][k][m][1];
            /* v(n) = u(n) + ag Q v(n-d);  u'(n) = Q v(n-d) - ag v(n) */
            float v[PS_QMF_SLOTS + 5][2];
            memcpy(v, state, (size_t)d * sizeof(v[0]));
            for (int n = 0; n < PS_QMF_SLOTS; n++) {
                float vr = v[n][0] * qr - v[n][1] * qi; /* Q v(n-d) */
                float vi = v[n][0] * qi + v[n][1] * qr;
                float nr = u[n][0] + ag * vr, ni = u[n][1] + ag * vi;
                v[n + d][0] = nr;
                v[n + d][1] = ni;
                u[n][0] = vr - ag * nr;
                u[n][1] = vi - ag * ni;
            }
            memcpy(state, v + PS_QMF_SLOTS, (size_t)d * sizeof(v[0]));
            state += 2 * d;
        }
        for (int n = 0; n < PS_QMF_SLOTS; n++) {
            out[k][n][0] = g[n] * u[n][0];
            out[k][n][1] = g[n] * u[n][1];
        }
    }
    for (; k < ps_nr_bands[is34]; k++) {
        const float *g = gain[k_to_b[k]];
        const int d = (k < ps_short_delay_band[is34]) ? PS_MAX_DELAY : 1;
        float (*hist)[2] = ps->dc_delay[k - nr_allpass];
        float line[PS_QMF_SLOTS + PS_MAX_DELAY][2];
        memcpy(line, hist, sizeof(ps->dc_delay[0]));
        memcpy(line + PS_MAX_DELAY, s[k], sizeof(s[k]));
        memcpy(hist, line + PS_QMF_SLOTS, sizeof(ps->dc_delay[0]));
        for (int n = 0; n < PS_QMF_SLOTS; n++) {
            out[k][n][0] = g[n] * line[PS_MAX_DELAY - d + n][0];
            out[k][n][1] = g[n] * line[PS_MAX_DELAY - d + n][1];
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Parameter band remapping                                                  */
/* ------------------------------------------------------------------------ */

static void ps_map_10_to_20(int8_t *dst, const int8_t *src, bool full)
{
    int b = full ? 9 : 4;
    if (!full) dst[10] = 0;
    for (; b >= 0; b--) dst[2 * b + 1] = dst[2 * b] = src[b];
}

static void ps_map_34_to_20(int8_t *dst, const int8_t *src, bool full)
{
    dst[0] = (int8_t)((2 * src[0] + src[1]) / 3);   dst[1] = (int8_t)((src[1] + 2 * src[2]) / 3);
    dst[2] = (int8_t)((2 * src[3] + src[4]) / 3);   dst[3] = (int8_t)((src[4] + 2 * src[5]) / 3);
    dst[4] = (int8_t)((src[6] + src[7]) / 2);       dst[5] = (int8_t)((src[8] + src[9]) / 2);
    dst[6] = src[10];                               dst[7] = src[11];
    dst[8] = (int8_t)((src[12] + src[13]) / 2);     dst[9] = (int8_t)((src[14] + src[15]) / 2);
    dst[10] = src[16];
    if (!full) return;
    dst[11] = src[17]; dst[12] = src[18]; dst[13] = src[19];
    dst[14] = (int8_t)((src[20] + src[21]) / 2);    dst[15] = (int8_t)((src[22] + src[23]) / 2);
    dst[16] = (int8_t)((src[24] + src[25]) / 2);    dst[17] = (int8_t)((src[26] + src[27]) / 2);
    dst[18] = (int8_t)((src[28] + src[29] + src[30] + src[31]) / 4);
    dst[19] = (int8_t)((src[32] + src[33]) / 2);
}

static void ps_map_10_to_34(int8_t *dst, const int8_t *src, bool full)
{
    static const int8_t lo[17] = { 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 3, 3, 4, 4, 4, 4, -1 };
    static const int8_t hi[34] = { 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9 };
    if (full) { for (int i = 0; i < 34; i++) dst[i] = src[hi[i]]; }
    else { for (int i = 0; i < 16; i++) dst[i] = src[lo[i]]; dst[16] = 0; }
}

static void ps_map_20_to_34(int8_t *dst, const int8_t *src, bool full)
{
    if (full) {
        dst[33] = src[19]; dst[32] = src[19]; dst[31] = src[18]; dst[30] = src[18]; dst[29] = src[18]; dst[28] = src[18];
        dst[27] = src[17]; dst[26] = src[17]; dst[25] = src[16]; dst[24] = src[16]; dst[23] = src[15]; dst[22] = src[15];
        dst[21] = src[14]; dst[20] = src[14]; dst[19] = src[13]; dst[18] = src[12]; dst[17] = src[11];
    }
    dst[16] = src[10]; dst[15] = src[9]; dst[14] = src[9]; dst[13] = src[8]; dst[12] = src[8]; dst[11] = src[7];
    dst[10] = src[6]; dst[9] = src[5]; dst[8] = src[5]; dst[7] = src[4]; dst[6] = src[4]; dst[5] = src[3];
    dst[4] = (int8_t)((src[2] + src[3]) / 2); dst[3] = src[2]; dst[2] = src[1];
    dst[1] = (int8_t)((src[0] + src[1]) / 2); dst[0] = src[0];
}

static void ps_remap(int8_t dst[PS_MAX_ENV][PS_NR_PAR], int8_t src[PS_MAX_ENV][PS_NR_PAR], int num_par, int num_env, bool full, bool is34)
{
    for (int e = 0; e < num_env; e++) {
        if (is34) {
            if (num_par == 20 || num_par == 11) ps_map_20_to_34(dst[e], src[e], full);
            else if (num_par == 10 || num_par == 5) ps_map_10_to_34(dst[e], src[e], full);
            else memcpy(dst[e], src[e], PS_NR_PAR);
        } else {
            if (num_par == 34 || num_par == 17) ps_map_34_to_20(dst[e], src[e], full);
            else if (num_par == 10 || num_par == 5) ps_map_10_to_20(dst[e], src[e], full);
            else memcpy(dst[e], src[e], PS_NR_PAR);
        }
    }
}

/* Carry the previous frame's mixing matrix across a band-count change. */
static void ps_map_val_34_to_20(float *p)
{
    p[0] = (2 * p[0] + p[1]) / 3; p[1] = (p[1] + 2 * p[2]) / 3; p[2] = (2 * p[3] + p[4]) / 3; p[3] = (p[4] + 2 * p[5]) / 3;
    p[4] = 0.5f * (p[6] + p[7]); p[5] = 0.5f * (p[8] + p[9]); p[6] = p[10]; p[7] = p[11];
    p[8] = 0.5f * (p[12] + p[13]); p[9] = 0.5f * (p[14] + p[15]); p[10] = p[16]; p[11] = p[17]; p[12] = p[18]; p[13] = p[19];
    p[14] = 0.5f * (p[20] + p[21]); p[15] = 0.5f * (p[22] + p[23]); p[16] = 0.5f * (p[24] + p[25]); p[17] = 0.5f * (p[26] + p[27]);
    p[18] = 0.25f * (p[28] + p[29] + p[30] + p[31]); p[19] = 0.5f * (p[32] + p[33]);
}

static void ps_map_val_20_to_34(float *p)
{
    p[33] = p[19]; p[32] = p[19]; p[31] = p[18]; p[30] = p[18]; p[29] = p[18]; p[28] = p[18]; p[27] = p[17]; p[26] = p[17];
    p[25] = p[16]; p[24] = p[16]; p[23] = p[15]; p[22] = p[15]; p[21] = p[14]; p[20] = p[14]; p[19] = p[13]; p[18] = p[12];
    p[17] = p[11]; p[16] = p[10]; p[15] = p[9]; p[14] = p[9]; p[13] = p[8]; p[12] = p[8]; p[11] = p[7]; p[10] = p[6];
    p[9] = p[5]; p[8] = p[5]; p[7] = p[4]; p[6] = p[4]; p[5] = p[3]; p[4] = 0.5f * (p[2] + p[3]); p[3] = p[2]; p[2] = p[1];
    p[1] = 0.5f * (p[0] + p[1]);
}

/* ------------------------------------------------------------------------ */
/* Stereo processing (§8.6.4.6.2 - §8.6.4.6.4)                               */
/* ------------------------------------------------------------------------ */

/* Smoothed phase over the current and two previous values (weights 1, 1/2, 1/4). */
static void ps_pd_smooth(int pd0, int pd1, int pd2, float *re, float *im)
{
    static const float c[8] = { 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f };
    static const float s[8] = { 0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f };
    float r = 0.25f * c[pd0] + 0.5f * c[pd1] + c[pd2];
    float i = 0.25f * s[pd0] + 0.5f * s[pd1] + s[pd2];
    float mag = 1.0f / hypotf(r, i);
    *re = r * mag;
    *im = i * mag;
}

static void ps_stereo(PSState *ps, float l[PS_NR_BANDS][PS_QMF_SLOTS][2], float r[PS_NR_BANDS][PS_QMF_SLOTS][2], bool is34)
{
    const int8_t *k_to_b = is34 ? ps_k_to_b_34 : ps_k_to_b_20;
    float (*H_LUT)[8][4] = (ps->icc_mode < 3) ? ps_HA : ps_HB;
    int8_t iid_m[PS_MAX_ENV][PS_NR_PAR], icc_m[PS_MAX_ENV][PS_NR_PAR], ipd_m[PS_MAX_ENV][PS_NR_PAR], opd_m[PS_MAX_ENV][PS_NR_PAR];
    float (*H)[2][PS_MAX_ENV + 1][PS_NR_PAR] = ps->H; /* [matrix entry][re/im][env][band] */

    /* the last envelope of the previous frame is this frame's starting point */
    if (ps->num_env_old) {
        for (int i = 0; i < 4; i++)
            for (int c = 0; c < 2; c++)
                memcpy(H[i][c][0], H[i][c][ps->num_env_old], sizeof(H[i][c][0]));
    }
    ps_remap(iid_m, ps->iid_par, ps->nr_iid_par, ps->num_env, true, is34);
    ps_remap(icc_m, ps->icc_par, ps->nr_icc_par, ps->num_env, true, is34);
    if (ps->enable_ipdopd) {
        ps_remap(ipd_m, ps->ipd_par, ps->nr_ipdopd_par, ps->num_env, false, is34);
        ps_remap(opd_m, ps->opd_par, ps->nr_ipdopd_par, ps->num_env, false, is34);
    }
    if (is34 != ps->is34_old) {
        for (int i = 0; i < 4; i++)
            for (int c = 0; c < 2; c++) {
                if (is34) ps_map_val_20_to_34(H[i][c][0]);
                else ps_map_val_34_to_20(H[i][c][0]);
            }
        memset(ps->ipd_hist, 0, sizeof(ps->ipd_hist));
        memset(ps->opd_hist, 0, sizeof(ps->opd_hist));
    }

    for (int e = 0; e < ps->num_env; e++) {
        for (int b = 0; b < ps_nr_par_bands[is34]; b++) {
            int iid = iid_m[e][b] + 7 + 23 * (ps->iid_quant ? 1 : 0);
            int icc = icc_m[e][b] & 7;
            if (iid < 0) iid = 0;
            if (iid > 45) iid = 45;
            float h[4] = { H_LUT[iid][icc][0], H_LUT[iid][icc][1], H_LUT[iid][icc][2], H_LUT[iid][icc][3] };
            float hi[4] = { 0, 0, 0, 0 };
            if (ps->enable_ipdopd && b < ps_nr_ipdopd_bands[is34]) {
                int opd = opd_m[e][b] & 7, ipd = ipd_m[e][b] & 7;
                float opd_re, opd_im, ipd_re, ipd_im;
                ps_pd_smooth(ps->opd_hist[b] >> 3, ps->opd_hist[b] & 7, opd, &opd_re, &opd_im);
                ps_pd_smooth(ps->ipd_hist[b] >> 3, ps->ipd_hist[b] & 7, ipd, &ipd_re, &ipd_im);
                ps->opd_hist[b] = (int8_t)(((ps->opd_hist[b] & 7) << 3) | opd);
                ps->ipd_hist[b] = (int8_t)(((ps->ipd_hist[b] & 7) << 3) | ipd);
                float adj_re = opd_re * ipd_re + opd_im * ipd_im;
                float adj_im = opd_im * ipd_re - opd_re * ipd_im;
                hi[0] = h[0] * opd_im; h[0] *= opd_re;
                hi[1] = h[1] * adj_im; h[1] *= adj_re;
                hi[2] = h[2] * opd_im; h[2] *= opd_re;
                hi[3] = h[3] * adj_im; h[3] *= adj_re;
            }
            for (int i = 0; i < 4; i++) { H[i][0][e + 1][b] = h[i]; H[i][1][e + 1][b] = hi[i]; }
        }

        int start = ps->border[e], stop = ps->border[e + 1];
        if (stop <= start) continue;
        float width = 1.0f / (float)(stop - start);
        for (int k = 0; k < ps_nr_bands[is34]; k++) {
            int b = k_to_b[k];
            float h[4], hs[4], hI[4], hIs[4];
            bool flip = ps->enable_ipdopd && ((is34 && k >= 9 && k <= 13) || (!is34 && k <= 1));
            for (int i = 0; i < 4; i++) {
                h[i] = H[i][0][e][b];
                hs[i] = (H[i][0][e + 1][b] - h[i]) * width;
                hI[i] = flip ? -H[i][1][e][b] : H[i][1][e][b];
                hIs[i] = (H[i][1][e + 1][b] - hI[i]) * width;
            }
            if (!ps->enable_ipdopd) {
                for (int n = start + 1; n <= stop; n++) {
                    float lr = l[k][n][0], li = l[k][n][1], rr = r[k][n][0], ri = r[k][n][1];
                    for (int i = 0; i < 4; i++) h[i] += hs[i];
                    l[k][n][0] = h[0] * lr + h[2] * rr; l[k][n][1] = h[0] * li + h[2] * ri;
                    r[k][n][0] = h[1] * lr + h[3] * rr; r[k][n][1] = h[1] * li + h[3] * ri;
                }
            } else {
                for (int n = start + 1; n <= stop; n++) {
                    float lr = l[k][n][0], li = l[k][n][1], rr = r[k][n][0], ri = r[k][n][1];
                    for (int i = 0; i < 4; i++) { h[i] += hs[i]; hI[i] += hIs[i]; }
                    l[k][n][0] = h[0] * lr + h[2] * rr - hI[0] * li - hI[2] * ri;
                    l[k][n][1] = h[0] * li + h[2] * ri + hI[0] * lr + hI[2] * rr;
                    r[k][n][0] = h[1] * lr + h[3] * rr - hI[1] * li - hI[3] * ri;
                    r[k][n][1] = h[1] * li + h[3] * ri + hI[1] * lr + hI[3] * rr;
                }
            }
        }
    }
}

/* Mono X (38 slots, the last six a low-band look-ahead) to a stereo pair of 32 slots. */
/* Runs the frame up to the hybrid domain; ps_synthesis_slot() then yields
 * each output slot, so no frame-sized QMF-domain output is kept. */
void ps_apply(struct faad_decoder *dec, float X[PS_IN_SLOTS][64][2], int top)
{
    PSState *ps = &dec->ps;
    bool is34 = ps->is34;
    float (*lb)[PS_QMF_SLOTS][2] = dec->sbr_scratch.ps_l;
    float (*rb)[PS_QMF_SLOTS][2] = dec->sbr_scratch.ps_r;

    init_ps_tables();
    /* bands above the SBR range carry nothing: keep their delay lines silent */
    int top_k = top + ps_nr_bands[is34] - 64;
    if (top_k < 0) top_k = 0;
    {
        int na = ps_nr_allpass[is34], nb = ps_nr_bands[is34];
        if (top_k < na) {
            memset(ps->dc_in + top_k, 0, sizeof(ps->dc_in[0]) * (size_t)(na - top_k));
            memset(ps->dc_ap + top_k, 0, sizeof(ps->dc_ap[0]) * (size_t)(na - top_k));
        }
        int td = top_k > na ? top_k - na : 0;
        if (td < nb - na) memset(ps->dc_delay + td, 0, sizeof(ps->dc_delay[0]) * (size_t)(nb - na - td));
    }

    ps_hybrid_analysis(ps, lb, X, is34);
    ps_decorrelate(ps, rb, lb, is34);
    ps_stereo(ps, lb, rb, is34);
}

#endif /* FAAD_DISABLE_PS */

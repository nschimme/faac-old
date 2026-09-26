/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
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

#include "sbr.h"
#include "sbr_analysis.h"
#include "sbr_internal.h"
#include "util.h"
#include <string.h>

static int sbr_even_clamp(int x, int lo, int hi)
{
    x = clamp_int(x, lo, hi);
    return x & ~1;
}

static void sbr_set_pointer(SbrGrid *grid, int transient)
{
    int p = 0;
    while (p + 1 < grid->numEnvelopes && transient >= grid->tEnv[p + 1]) p++;
    grid->bsPointer = p;
}

/* A late attack needs a border beyond this frame; its successor starts there. */
static void sbr_choose_grid(SignalAnalysisChannel *ac, int numEnvFixFix, int numSlots)
{
    SbrGrid grid = { 0 };
    int t = sbr_even_clamp(ac->transientSlot * SBR_NUM_TIME_SLOTS / numSlots, 0, 14);
    int carry = ac->trailingBorder > SBR_NUM_TIME_SLOTS ?
                ac->trailingBorder - SBR_NUM_TIME_SLOTS : 0;
    int tail = t >= 4; /* The final 3/4 leaves room for the attack's trailing envelope. */
    /* 3.7 early / 3.05 late: an early split needs a 5.7 dB peak; a trailing attack has no later border. */
    int transient = ac->transientStrength > (tail ? 3.05f : 3.7f);
    tail &= transient;
    /* 4.5/7.5: 6.5/8.8 dB peaks need one/two extra level changes. */
    int n = 2 + (ac->transientStrength > 4.5f) + (ac->transientStrength > 7.5f);

    if (!transient) {
        if (carry) {
            grid.frameClass = SBR_FRAME_CLASS_VARFIX;
            grid.numEnvelopes = 2;
            grid.tEnv[0] = carry; grid.tEnv[1] = 8; grid.tEnv[2] = SBR_NUM_TIME_SLOTS;
        } else {
            grid.frameClass = SBR_FRAME_CLASS_FIXFIX;
            grid.numEnvelopes = numEnvFixFix;
            for (int e = 0; e <= grid.numEnvelopes; e++)
                grid.tEnv[e] = e * SBR_NUM_TIME_SLOTS / grid.numEnvelopes;
        }
    } else if (tail && carry) {
        grid.frameClass = SBR_FRAME_CLASS_VARVAR;
        /* 12: a 10.8 dB peak is the rare case that warrants all five envelopes. */
        grid.numEnvelopes = n + (ac->transientStrength > 12.0f);
        grid.tEnv[0] = 2;
        if (grid.numEnvelopes == 2) grid.tEnv[1] = 10;
        else if (grid.numEnvelopes == 3) { grid.tEnv[1] = 8; grid.tEnv[2] = 14; }
        else if (grid.numEnvelopes == 4) { grid.tEnv[1] = 6; grid.tEnv[2] = 10; grid.tEnv[3] = 14; }
        else { grid.tEnv[1] = 4; grid.tEnv[2] = 8; grid.tEnv[3] = 12; grid.tEnv[4] = 16; }
        grid.tEnv[grid.numEnvelopes] = 18;
    } else if (tail) {
        grid.frameClass = SBR_FRAME_CLASS_FIXVAR;
        grid.numEnvelopes = n;
        grid.tEnv[0] = 0;
        if (n == 2) grid.tEnv[1] = sbr_even_clamp(t, 10, 16);
        else if (n == 3) { grid.tEnv[1] = 8; grid.tEnv[2] = sbr_even_clamp(t, 10, 16); }
        else { grid.tEnv[1] = 4; grid.tEnv[2] = 8; grid.tEnv[3] = sbr_even_clamp(t, 10, 16); }
        grid.tEnv[n] = 18;
    } else {
        int start = carry ? 2 : 0;
        grid.frameClass = SBR_FRAME_CLASS_VARFIX;
        grid.numEnvelopes = n;
        grid.tEnv[0] = start;
        if (n == 2) grid.tEnv[1] = sbr_even_clamp(t, start + 2, 8);
        else if (n == 3) {
            grid.tEnv[1] = sbr_even_clamp(t, start + 2, 6);
            grid.tEnv[2] = grid.tEnv[1] + 4;
        } else {
            grid.tEnv[1] = sbr_even_clamp(t, start + 2, 6);
            grid.tEnv[2] = grid.tEnv[1] + 2;
            grid.tEnv[3] = grid.tEnv[1] + 6;
        }
        grid.tEnv[n] = SBR_NUM_TIME_SLOTS;
    }

    for (int e = 0; e < grid.numEnvelopes; e++)
        /* Four slots is the shortest interval whose high-band detail repays its extra codes. */
        grid.freqRes[e] = grid.tEnv[e + 1] - grid.tEnv[e] > 4;
    if (grid.frameClass == SBR_FRAME_CLASS_FIXFIX) {
        for (int e = 0; e < grid.numEnvelopes; e++)
            grid.freqRes[e] = 1;
    }
    sbr_set_pointer(&grid, t);
    ac->grid = grid;
    ac->trailingBorder = grid.tEnv[grid.numEnvelopes];
}

/* Multi-pass signal analysis: transient detection, temporal grid selection,
 * and subband energy accumulation. */
void SbrAnalyze(SignalAnalysis *sa, float *fullPtrs[], int nch, const bool *isLfe, int numSamples, struct SBRInfo *sbr)
{
    int num_slots = numSamples / SBR_QMF_BANDS_64;
    int sampled = (num_slots - 1) / FAAC_SBR_DECIMATION + 1;
    float workspace[SBR_QMF_HIST_LEN + 2 * FRAME_LEN];

    sa->numSlots = num_slots;
    sa->sampled = sampled;

    /* Pass 1: Time-domain transient detection. Identifies the temporal position
     * and strength of transients across all channels. */
    for (int ch = 0; ch < nch; ch++) {
        float smax = 0.0f, ssum = 0.0f;
        int smax_idx = 0;
        for (int slot = 0; slot < num_slots; slot++) {
            /* The analysed frame starts SBR_ANALYSIS_DELAY back, in the saved history. */
            int pos = slot * SBR_QMF_BANDS_64 - SBR_ANALYSIS_DELAY;
            const float * restrict p_in = (pos < 0) ? sbr->ch[ch].qmfOvl64 + SBR_QMF_HIST_LEN + pos
                                                    : fullPtrs[ch] + pos;
            float stot = 0.0f;
            for (int n = 0; n < SBR_QMF_BANDS_64; n += 4) {
                float v0 = p_in[0], v1 = p_in[1], v2 = p_in[2], v3 = p_in[3];
                stot += v0 * v0 + v1 * v1 + v2 * v2 + v3 * v3;
                p_in += 4;
            }

            if (stot > smax) {
                smax = stot;
                smax_idx = slot;
            }
            ssum += stot;
        }

        sa->ch[ch].transientStrength = smax * (float)num_slots / (ssum + SBR_ENERGY_FLOOR);
        sa->ch[ch].transientSlot = smax_idx;
    }

    /* Each channel keeps its own border continuity. */
    for (int ch = 0; ch < nch; ch++)
        sbr_choose_grid(&sa->ch[ch], sbr->numEnvFixFix, num_slots);
    if (nch == 2 && !isLfe[0] && !isLfe[1]) {
        SignalAnalysisChannel *left = &sa->ch[0], *right = &sa->ch[1];
        int d = left->transientSlot - right->transientSlot;
        float lo = left->transientStrength < right->transientStrength ? left->transientStrength : right->transientStrength;
        float hi = left->transientStrength > right->transientStrength ? left->transientStrength : right->transientStrength;
        /* Two SBR slots and 3 dB describe one stereo attack, preserving coupling when it is unambiguous. */
        if (d >= -4 && d <= 4 && lo * 2.0f >= hi) {
            if (right->transientStrength > left->transientStrength) left->grid = right->grid;
            else right->grid = left->grid;
            left->trailingBorder = right->trailingBorder = left->grid.tEnv[left->grid.numEnvelopes];
        }
    }

    /* Each channel bins against its own grid. */
    for (int ch = 0; ch < nch; ch++) {
        SbrGrid *grid = &sa->ch[ch].grid;
        int envStart[SBR_MAX_ENVELOPES + 1];
        int slot_env[32];
        for (int e = 0; e <= grid->numEnvelopes; e++)
            envStart[e] = grid->tEnv[e] * num_slots / SBR_NUM_TIME_SLOTS;

        for (int e = 0; e < grid->numEnvelopes; e++) sa->ch[ch].envSampled[e] = 0;

        int e_idx = 0;
        for (int slot = 0; slot < num_slots; slot++) {
            while (e_idx + 1 < grid->numEnvelopes && slot >= envStart[e_idx + 1])
                e_idx++;
            slot_env[slot] = e_idx;
#if FAAC_SBR_DECIMATION > 1
            if (slot % FAAC_SBR_DECIMATION == 0)
#endif
                sa->ch[ch].envSampled[e_idx]++;
        }
        for (int e = 0; e < grid->numEnvelopes; e++)
            if (sa->ch[ch].envSampled[e] < 1) sa->ch[ch].envSampled[e] = 1;

        /* Pass 2: subband analysis, accumulating QMF band energy per envelope.
         * Only [kx, k2) feeds the quantizer, so skip bands below kx. */
        if (!sbr || isLfe[ch]) continue;
        int kx = sbr->kx;
        int kEnd = sbr->k2;
        memset(sa->bandE[ch], 0, sizeof(sa->bandE[ch]));

        memcpy(workspace, sbr->ch[ch].qmfOvl64, SBR_QMF_HIST_LEN * sizeof(float));
        memcpy(workspace + SBR_QMF_HIST_LEN, fullPtrs[ch], numSamples * sizeof(float));

        for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
            if (slot % FAAC_SBR_DECIMATION == 0)
#endif
            {
                int e = slot_env[slot];
                SbrQmfAnalysis(sbr, workspace + slot * SBR_QMF_BANDS_64, sa->bandE[ch][e], kx, kEnd);
            }
        }
    }
}

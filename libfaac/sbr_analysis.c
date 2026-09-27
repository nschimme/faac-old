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

/* Which envelope a QMF slot falls in; slots before tEnv[0] fold into
 * envelope 0 rather than dropping their energy. */
static inline int sbr_env_of_slot(int numEnvelopes, const int *envStart, int slot)
{
    int e = 0;
    while (e + 1 < numEnvelopes && slot >= envStart[e + 1]) e++;
    return e;
}

/* Multi-pass signal analysis: transient detection, temporal grid selection,
 * and subband energy accumulation. */
#include "blockswitch.h"
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

    /* Choose the temporal grid based on the strongest transient. Synchronizes
     * envelope borders across all channels to maintain spatial imaging. The
     * LFE carries no SBR, so it gets no vote. */
    float frameStrength = 0.0f;
    int frameSlot = 0;
    for (int ch = 0; ch < nch; ch++) {
        if (isLfe[ch]) continue;
        if (sa->ch[ch].transientStrength > frameStrength) {
            frameStrength = sa->ch[ch].transientStrength;
            frameSlot = sa->ch[ch].transientSlot;
        }
    }

    /* Option A: Direct PCM Time-Domain High-Pass Transient Detection */
    int primary_ch = 0;
    for (int ch = 0; ch < nch; ch++) {
        if (!isLfe[ch]) { primary_ch = ch; break; }
    }

    float max_pcm_ratio = 0.0f;
    int max_pcm_slot = 0;

    for (int ch = 0; ch < nch; ch++) {
        if (isLfe[ch]) continue;
        const float *pcm = fullPtrs[ch];
        float level = sbr->ch[ch].pcmLevel;
        if (level < 1e-6f) level = 1e-6f;

        /* Analyze high-pass energy across SBR_NUM_TIME_SLOTS (16 sub-blocks) */
        int samples_per_slot = numSamples / SBR_NUM_TIME_SLOTS;
        for (int s = 0; s < SBR_NUM_TIME_SLOTS; s++) {
            int start_n = s * samples_per_slot;
            float e = 0.0f;
            for (int n = start_n; n < start_n + samples_per_slot; n++) {
                float d = (n > 0) ? (pcm[n] - pcm[n - 1]) : pcm[n];
                e += d * d;
            }
            float ratio = e / level;
            if (ratio > max_pcm_ratio) {
                max_pcm_ratio = ratio;
                max_pcm_slot = s;
            }
            level = 0.3f * e + 0.7f * level;
        }
        sbr->ch[ch].pcmLevel = level;
    }

    /* Attack threshold: PCM high-pass energy jump > 40.0x running level for conservative, optimal MOS transient splitting */
    bool attack = (max_pcm_ratio > 40.0f);
    int A = max_pcm_slot;

    SbrFrameClass prev_c = sbr->ch[primary_ch].prev_class;
    int spread = sbr->ch[primary_ch].spread;
    SbrFrameClass curr_c;

    /* Transition Table Rules */
    if (prev_c == SBR_FRAME_CLASS_FIXFIX) {
        curr_c = attack ? SBR_FRAME_CLASS_FIXVAR : SBR_FRAME_CLASS_FIXFIX;
    } else if (prev_c == SBR_FRAME_CLASS_FIXVAR) {
        if (attack) {
            curr_c = SBR_FRAME_CLASS_VARVAR;
            spread = 0;
        } else {
            curr_c = spread ? SBR_FRAME_CLASS_VARVAR : SBR_FRAME_CLASS_VARFIX;
            spread = 0;
        }
    } else if (prev_c == SBR_FRAME_CLASS_VARFIX) {
        curr_c = attack ? SBR_FRAME_CLASS_FIXVAR : SBR_FRAME_CLASS_FIXFIX;
    } else { /* VARVAR */
        if (attack) {
            curr_c = SBR_FRAME_CLASS_VARVAR;
            spread = 0;
        } else {
            curr_c = spread ? SBR_FRAME_CLASS_VARVAR : SBR_FRAME_CLASS_VARFIX;
            spread = 0;
        }
    }

    int tEnvPrev_next = SBR_NUM_TIME_SLOTS;

    if (curr_c == SBR_FRAME_CLASS_FIXFIX) {
        int ne = sbr->numEnvFixFix;
        sa->numEnvelopes = ne;
        sa->frameClass = SBR_FRAME_CLASS_FIXFIX;
        for (int e = 0; e <= ne; e++)
            sa->tEnv[e] = e * SBR_NUM_TIME_SLOTS / ne;
        sa->bsPointer = 0;
        tEnvPrev_next = SBR_NUM_TIME_SLOTS;
    } else if (curr_c == SBR_FRAME_CLASS_FIXVAR) {
        sa->frameClass = SBR_FRAME_CLASS_FIXVAR;
        sa->numEnvelopes = 2;
        sa->tEnv[0] = 0;
        int b1 = clamp_int(A, 2, 14);
        sa->tEnv[1] = b1;
        sa->tEnv[2] = SBR_NUM_TIME_SLOTS;
        sa->bsPointer = 0;
        tEnvPrev_next = SBR_NUM_TIME_SLOTS;
    } else if (curr_c == SBR_FRAME_CLASS_VARFIX) {
        sa->frameClass = SBR_FRAME_CLASS_VARFIX;
        int t0 = (sbr->ch[primary_ch].tEnvPrev > SBR_NUM_TIME_SLOTS) ? (sbr->ch[primary_ch].tEnvPrev - SBR_NUM_TIME_SLOTS) : 0;
        t0 = clamp_int(t0, 0, 12);
        sa->numEnvelopes = 2;
        sa->tEnv[0] = t0;
        sa->tEnv[1] = (t0 + SBR_NUM_TIME_SLOTS) / 2;
        sa->tEnv[2] = SBR_NUM_TIME_SLOTS;
        sa->bsPointer = 0;
        tEnvPrev_next = SBR_NUM_TIME_SLOTS;
    } else { /* VARVAR */
        sa->frameClass = SBR_FRAME_CLASS_VARVAR;
        int t0 = (sbr->ch[primary_ch].tEnvPrev > SBR_NUM_TIME_SLOTS) ? (sbr->ch[primary_ch].tEnvPrev - SBR_NUM_TIME_SLOTS) : 0;
        t0 = clamp_int(t0, 0, 8);
        sa->numEnvelopes = 4;
        int b1 = clamp_int(A, t0 + 2, 12);
        int b2 = clamp_int(b1 + 2, b1 + 1, 14);
        int b3 = clamp_int(b2 + 2, b2 + 1, 15);
        sa->tEnv[0] = t0;
        sa->tEnv[1] = b1;
        sa->tEnv[2] = b2;
        sa->tEnv[3] = b3;
        sa->tEnv[4] = SBR_NUM_TIME_SLOTS;
        sa->bsPointer = 0;
        tEnvPrev_next = SBR_NUM_TIME_SLOTS;
    }

    /* Synchronize channel state across all non-LFE channels */
    for (int ch = 0; ch < nch; ch++) {
        if (!isLfe[ch]) {
            sbr->ch[ch].prev_class = curr_c;
            sbr->ch[ch].spread = spread;
            sbr->ch[ch].tEnvPrev = tEnvPrev_next;
        }
    }

    /* Envelope borders in QMF slots, for binning the per-slot energies below. */
    int envStart[SBR_MAX_ENVELOPES + 1];
    for (int e = 0; e <= sa->numEnvelopes; e++)
        envStart[e] = sa->tEnv[e] * num_slots / SBR_NUM_TIME_SLOTS;

    /* Count slots per envelope for power normalization. */
    for (int e = 0; e < sa->numEnvelopes; e++) sa->envSampled[e] = 0;
    for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
        if (slot % FAAC_SBR_DECIMATION != 0) continue;
#endif
        sa->envSampled[sbr_env_of_slot(sa->numEnvelopes, envStart, slot)]++;
    }
    for (int e = 0; e < sa->numEnvelopes; e++)
        if (sa->envSampled[e] < 1) sa->envSampled[e] = 1;

    /* Pass 2: subband analysis, accumulating QMF band energy per envelope.
     * Only [kx, k2) feeds the quantizer, so skip bands below kx. */
    if (sbr) {
        int kx = sbr->kx;
        int kEnd = sbr->k2;
        for (int ch = 0; ch < nch; ch++) {
            if (isLfe[ch]) continue;
            memset(sa->bandE[ch], 0, sizeof(sa->bandE[ch]));

            memcpy(workspace, sbr->ch[ch].qmfOvl64, SBR_QMF_HIST_LEN * sizeof(float));
            memcpy(workspace + SBR_QMF_HIST_LEN, fullPtrs[ch], numSamples * sizeof(float));

            for (int slot = 0; slot < num_slots; slot++) {
#if FAAC_SBR_DECIMATION > 1
                if (slot % FAAC_SBR_DECIMATION == 0)
#endif
                {
                    int e = sbr_env_of_slot(sa->numEnvelopes, envStart, slot);
                    SbrQmfAnalysis(sbr, workspace + slot * SBR_QMF_BANDS_64, sa->bandE[ch][e], kx, kEnd);
                }
            }
        }
    }
}

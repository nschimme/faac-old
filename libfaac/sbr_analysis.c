/* HE-AAC v1 SBR envelope-grid selection and transient analysis */
#include "sbr.h"
#include "sbr_analysis.h"
#include "sbr_internal.h"
#include "util.h"
#include <math.h>
#include <string.h>

void SbrAnalyzeFrame(SbrAnalysisFrame *f, float *in[], int nch, const bool *lfe, int samples, struct SBRInfo *sbr)
{
    float work[SBR_QMF_HIST_LEN + 2 * FRAME_LEN];
    int slots = samples / SBR_QMF_BANDS_64;
    memset(f, 0, sizeof(*f));
    f->numSlots = slots;
    for (int ch = 0; ch < nch; ch++) {
        memcpy(work, sbr->ch[ch].qmfOvl64, SBR_QMF_HIST_LEN * sizeof(float));
        memcpy(work + SBR_QMF_HIST_LEN, in[ch], samples * sizeof(float));
        for (int slot = 0; slot < slots; slot++) {
            int pos = slot * SBR_QMF_BANDS_64 - SBR_ANALYSIS_DELAY;
            const float *p = pos < 0 ? sbr->ch[ch].qmfOvl64 + SBR_QMF_HIST_LEN + pos : in[ch] + pos;
            for (int k = 0; k < SBR_QMF_BANDS_64; k++) f->totalE[ch][slot] += p[k] * p[k];
#if FAAC_SBR_DECIMATION > 1
            if (slot % FAAC_SBR_DECIMATION) continue;
#endif
            f->sampled[slot] = 1;
            if (!lfe[ch]) SbrQmfAnalysis(sbr, work + slot * SBR_QMF_BANDS_64, f->bandE[ch][slot], sbr->kx, sbr->k2);
        }
        memcpy(sbr->ch[ch].qmfOvl64, in[ch] + samples - SBR_QMF_HIST_LEN, SBR_QMF_HIST_LEN * sizeof(float));
    }
}

static void choose_grid(SignalAnalysisChannel *ac, int numEnvFixFix, int slots, int attack, int pos, int split, int fixedRightBorder)
{
    SbrGrid g = { 0 };
    int T = slots;
    int det_offset = (T == 18) ? 8 : 4;
    int min_join = (T == 9) ? 2 : 4;
    int max_join = (T == 9) ? 8 : (T == 18) ? 15 : 12;
    int ovl_cap = (T == 16) ? ((pos < 4) ? 6 : (pos <= 5) ? 4 : 8) :
                  (T == 15) ? ((pos < 4) ? 5 : (pos <= 5) ? 3 : 7) : 8;
    int cap_post = (max_join < ovl_cap) ? max_join : ovl_cap;

    /* Fixed right border request suppresses attack flag and clears spread BEFORE applying transition table */
    if (fixedRightBorder) {
        attack = 0;
        ac->spread = false;
    }

    /* Low bitrate HE-AAC (< 17 kbps/ch, numEnvFixFix == 1): use stable FIXFIX grid [0, T] to preserve core bit budget */
    if (numEnvFixFix == 1) {
        ac->spread = false;
        ac->followUp.numBorders = 0;
        ac->prevClass = SBR_FRAME_CLASS_FIXFIX;
        g.frameClass = SBR_FRAME_CLASS_FIXFIX;
        g.numEnvelopes = 1;
        g.tEnv[0] = 0;
        g.tEnv[1] = T;
        g.freqRes[0] = 1;
        g.bsPointer = 0;
        ac->grid = g;
        return;
    }

    /* Frame Class Transition Table */
    SbrFrameClass prev = ac->prevClass;
    SbrFrameClass curr = SBR_FRAME_CLASS_FIXFIX;

    if (prev == SBR_FRAME_CLASS_FIXFIX) {
        curr = attack ? SBR_FRAME_CLASS_FIXVAR : SBR_FRAME_CLASS_FIXFIX;
    } else if (prev == SBR_FRAME_CLASS_FIXVAR) {
        if (attack) {
            curr = SBR_FRAME_CLASS_VARVAR;
            ac->spread = false;
        } else if (ac->spread) {
            curr = SBR_FRAME_CLASS_VARVAR;
        } else {
            curr = SBR_FRAME_CLASS_VARFIX;
        }
    } else if (prev == SBR_FRAME_CLASS_VARFIX) {
        curr = attack ? SBR_FRAME_CLASS_FIXVAR : SBR_FRAME_CLASS_FIXFIX;
    } else if (prev == SBR_FRAME_CLASS_VARVAR) {
        if (attack) {
            curr = SBR_FRAME_CLASS_VARVAR;
            ac->spread = false;
        } else if (ac->spread) {
            curr = SBR_FRAME_CLASS_VARVAR;
        } else {
            curr = SBR_FRAME_CLASS_VARFIX;
        }
    }

    ac->prevClass = curr;
    g.frameClass = curr;

    if (curr == SBR_FRAME_CLASS_FIXFIX) {
        ac->spread = false;
        ac->followUp.numBorders = 0;
        int n_env = (split || numEnvFixFix == 2) ? 2 : 1;
        g.numEnvelopes = n_env;
        g.tEnv[0] = 0;
        if (n_env == 1) {
            g.tEnv[1] = T;
            g.freqRes[0] = 1;
        } else {
            int mid = (T + 1) / 2;
            g.tEnv[1] = mid;
            g.tEnv[2] = T;
            g.freqRes[0] = 1;
            g.freqRes[1] = 1;
        }
        g.bsPointer = 0;
    } else if (curr == SBR_FRAME_CLASS_FIXVAR) {
        int A = pos + det_offset;
        int rel = (A < T && A > 0) ? A : (T / 2);
        rel = (rel + 1) & ~1;
        if (rel < 2) rel = 2;
        if (rel > T - 2) rel = T - 2;

        g.numEnvelopes = 2;
        g.tEnv[0] = 0;
        g.tEnv[1] = rel;
        g.tEnv[2] = T;
        g.freqRes[0] = 1;
        g.freqRes[1] = 1;
        g.bsPointer = 1;
        ac->spread = false;

        ac->followUp.numBorders = 0;
        ac->followUp.transientIdx = -1;
    } else if (curr == SBR_FRAME_CLASS_VARFIX) {
        int leading = (ac->followUp.numBorders > 0) ? ac->followUp.borders[0] : 0;
        g.numEnvelopes = 2;
        g.tEnv[0] = leading;
        g.tEnv[1] = (leading + T) / 2;
        g.tEnv[2] = T;
        g.freqRes[0] = 1;
        g.freqRes[1] = 1;
        g.bsPointer = 0;
        ac->spread = false;
        ac->followUp.numBorders = 0;
        ac->followUp.transientIdx = -1;
    } else if (curr == SBR_FRAME_CLASS_VARVAR) {
        if (!attack && ac->spread) {
            ac->spread = false;
            int wb[SBR_MAX_ENVELOPES + 10];
            int freqRes[SBR_MAX_ENVELOPES + 10];
            int nwb = 0;

            for (int i = 0; i < ac->followUp.numBorders; i++) {
                wb[nwb] = ac->followUp.borders[i];
                if (i > 0) freqRes[nwb - 1] = ac->followUp.freqRes[i - 1];
                nwb++;
                if (wb[nwb - 1] >= T) break;
            }
            if (nwb == 0) {
                wb[0] = 0; wb[1] = T;
                freqRes[0] = 1;
                nwb = 2;
            } else if (wb[nwb - 1] < T) {
                wb[nwb] = T;
                freqRes[nwb - 1] = 1;
                nwb++;
            }

            g.numEnvelopes = nwb - 1;
            for (int i = 0; i < nwb; i++) g.tEnv[i] = wb[i];
            for (int i = 0; i < g.numEnvelopes; i++) g.freqRes[i] = freqRes[i];
            g.bsPointer = 0;

            int comm_border = wb[nwb - 1];
            ac->followUp.borders[0] = comm_border - T;
            ac->followUp.numBorders = 1;
            ac->followUp.freqRes[0] = 1;
            ac->followUp.transientIdx = -1;
        } else {
            ac->spread = false;
            int A = pos + det_offset;
            int wb[SBR_MAX_ENVELOPES + 10];
            int freqRes[SBR_MAX_ENVELOPES + 10];
            int nwb = 0;

            int leading = (ac->followUp.numBorders > 0) ? ac->followUp.borders[0] : 0;
            wb[nwb++] = leading;
            for (int i = 1; i < ac->followUp.numBorders; i++) {
                if (ac->followUp.borders[i] + min_join <= A) {
                    wb[nwb] = ac->followUp.borders[i];
                    freqRes[nwb - 1] = ac->followUp.freqRes[i - 1];
                    nwb++;
                }
            }

            int last_b = wb[nwb - 1];
            if (A - last_b > max_join) {
                int gap = A - last_b;
                int segs = (gap + max_join - 1) / max_join;
                int step = ((gap / segs) + 1) & ~1;
                if (step < 2) step = 2;
                int cur = last_b;
                while (cur + step < A) {
                    cur += step;
                    wb[nwb] = cur;
                    freqRes[nwb - 1] = 1;
                    nwb++;
                }
            }

            wb[nwb] = A; freqRes[nwb - 1] = 1; nwb++;
            int trans_idx = nwb - 1;
            wb[nwb] = A + 2; freqRes[nwb - 1] = 1; nwb++;
            wb[nwb] = A + 6; freqRes[nwb - 1] = 1; nwb++;

            int cur = A + 6;
            int target_end = T + 12;
            int fill_step = (cap_post + 1) & ~1;
            if (fill_step < 2) fill_step = 2;
            while (cur < target_end) {
                cur += fill_step;
                wb[nwb] = cur;
                freqRes[nwb - 1] = 1;
                nwb++;
            }

            int comm_idx = 1;
            while (comm_idx < nwb && wb[comm_idx] < T) comm_idx++;

            int dist = wb[comm_idx] - wb[comm_idx - 1];
            if (dist > 0 && dist < min_join) {
                wb[comm_idx] = wb[comm_idx] + 8;
                ac->spread = true;
            }

            g.numEnvelopes = comm_idx;
            for (int i = 0; i <= comm_idx; i++) g.tEnv[i] = wb[i];
            for (int i = 0; i < comm_idx; i++) g.freqRes[i] = freqRes[i];

            if (trans_idx > 0 && trans_idx <= comm_idx) {
                g.bsPointer = trans_idx;
            } else {
                g.bsPointer = 0;
            }

            ac->followUp.numBorders = 0;
            for (int i = comm_idx; i < nwb; i++) {
                int rel_b = wb[i] - T;
                ac->followUp.borders[ac->followUp.numBorders] = rel_b;
                if (i > comm_idx) {
                    ac->followUp.freqRes[ac->followUp.numBorders - 1] = freqRes[i - 1];
                }
                ac->followUp.numBorders++;
            }
            if (trans_idx > comm_idx) {
                ac->followUp.transientIdx = trans_idx - comm_idx;
            } else {
                ac->followUp.transientIdx = -1;
            }
        }
    }

    ac->grid = g;
}

static void measure(SignalAnalysisChannel *ac, const SbrAnalysisFrame *f, const SbrAnalysisFrame *next, const SbrAnalysisFrame *ahead, int ch, int *attack, int *pos, int *split)
{
    int slots = f->numSlots;
    float scores[32] = {0.0f};

    memset(ac->bE, 0, sizeof(ac->bE));

    for (int s = 0; s < slots; s++) {
        float tot = f->totalE[ch][s];
        for (int k = 0; k < SBR_QMF_BANDS_64; k++)
            ac->bE[s][k] = f->bandE[ch][s][k] > 0.0f ? f->bandE[ch][s][k] : (tot / 64.0f);
    }
    for (int s = 0; s < next->numSlots; s++) {
        float tot = next->totalE[ch][s];
        for (int k = 0; k < SBR_QMF_BANDS_64; k++)
            ac->bE[slots + s][k] = next->bandE[ch][s][k] > 0.0f ? next->bandE[ch][s][k] : (tot / 64.0f);
    }
    for (int s = 0; s < ahead->numSlots; s++) {
        float tot = ahead->totalE[ch][s];
        for (int k = 0; k < SBR_QMF_BANDS_64; k++)
            ac->bE[slots + next->numSlots + s][k] = ahead->bandE[ch][s][k] > 0.0f ? ahead->bandE[ch][s][k] : (tot / 64.0f);
    }

    for (int k = 0; k < SBR_QMF_BANDS_64; k++) {
        float mean = 0.0f, var = 0.0f;
        for (int s = 0; s < slots; s++) mean += ac->bE[s][k];
        mean /= (float)slots;
        for (int s = 0; s < slots; s++) {
            float d = ac->bE[s][k] - mean;
            var += d * d;
        }
        float stddev = sqrtf(var / (float)slots);
        float thresh = 0.34f * stddev + 1e-6f;

        for (int s = 1; s < slots; s++) {
            float prev1 = ac->bE[s - 1][k];
            float prev2 = (s >= 2) ? ac->bE[s - 2][k] : prev1;
            float next1 = ac->bE[s + 1][k];
            float d1 = ac->bE[s][k] - prev1;
            float d2 = 0.5f * ((ac->bE[s][k] + next1) - (prev1 + prev2));
            float max_d = (d1 > d2) ? d1 : d2;
            if (max_d > thresh) {
                scores[s] += (max_d - thresh);
            }
        }
    }

    float trig_thresh = 15.0f;
    int at = -1;
    for (int s = 1; s < slots - 1; s++) {
        if (scores[s] > trig_thresh && scores[s + 1] < 0.90f * scores[s]) {
            at = s;
            break;
        }
    }

    if (at >= 0) {
        *attack = 1;
        *pos = at;
        ac->transientPos = at;
        ac->transientStrength = scores[at];
        *split = 0;
    } else {
        *attack = 0;
        *pos = 0;
        ac->transientPos = 0;
        ac->transientStrength = 0.0f;

        int mid = (slots + 1) / 2;
        float e_left = 0.0f, e_right = 0.0f;
        for (int s = 0; s < mid; s++) e_left += f->totalE[ch][s];
        for (int s = mid; s < slots; s++) e_right += f->totalE[ch][s];
        float ratio = (e_left > e_right) ? (e_left / (e_right + SBR_ENERGY_FLOOR)) : (e_right / (e_left + SBR_ENERGY_FLOOR));
        float total_e = e_left + e_right;
        *split = (ratio > 3.0f) && (total_e > SBR_ENERGY_FLOOR * 1000.0f);
    }
}

void SbrFinalizeFrame(SignalAnalysis *sa, const SbrAnalysisFrame *f, const SbrAnalysisFrame *next, const SbrAnalysisFrame *ahead, int nch, const bool *lfe, const int *coreBlockType, struct SBRInfo *sbr, struct SbrFrameData *fd)
{
    int attack[MAX_CHANNELS] = {0};
    int pos[MAX_CHANNELS] = {0};
    int split[MAX_CHANNELS] = {0};

    sa->numSlots = f->numSlots;

    for (int ch = 0; ch < nch; ch++) {
        if (lfe[ch]) continue;
        measure(&sa->ch[ch], f, next, ahead, ch, &attack[ch], &pos[ch], &split[ch]);
        if (coreBlockType && (coreBlockType[ch] == ONLY_SHORT_WINDOW || coreBlockType[ch] == LONG_SHORT_WINDOW || coreBlockType[ch] == SHORT_LONG_WINDOW)) {
            attack[ch] = 1;
            if (pos[ch] == 0) pos[ch] = 2;
        }
    }

    if (nch == 2 && !lfe[0] && !lfe[1]) {
        int shared_attack = attack[0] || attack[1];
        int shared_pos = (attack[0] && attack[1]) ? ((pos[0] < pos[1]) ? pos[0] : pos[1]) : (attack[0] ? pos[0] : pos[1]);
        int shared_split = !shared_attack && (split[0] || split[1]);
        choose_grid(&sa->ch[0], sbr->numEnvFixFix, f->numSlots, shared_attack, shared_pos, shared_split, 0);
        sa->ch[1].grid = sa->ch[0].grid;
        sa->ch[1].prevClass = sa->ch[0].prevClass;
        sa->ch[1].spread = sa->ch[0].spread;
        sa->ch[1].followUp = sa->ch[0].followUp;
    } else {
        for (int ch = 0; ch < nch; ch++) {
            if (lfe[ch]) continue;
            choose_grid(&sa->ch[ch], sbr->numEnvFixFix, f->numSlots, attack[ch], pos[ch], split[ch], 0);
        }
    }

    for (int ch = 0; ch < nch; ch++) {
        fd->ch[ch].grid = sa->ch[ch].grid;
        fd->ch[ch].eff_amp_res = (sa->ch[ch].grid.frameClass == SBR_FRAME_CLASS_FIXFIX && sa->ch[ch].grid.numEnvelopes == 1) ? 0 : SBR_AMP_RES;
    }
}

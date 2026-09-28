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
    (void)coreBlockType;
    int attack[MAX_CHANNELS] = {0};
    int pos[MAX_CHANNELS] = {0};
    int split[MAX_CHANNELS] = {0};

    sa->numSlots = f->numSlots;

    for (int ch = 0; ch < nch; ch++) {
        if (lfe[ch]) continue;
        measure(&sa->ch[ch], f, next, ahead, ch, &attack[ch], &pos[ch], &split[ch]);
        bool pcm_attack = coreBlockType && (coreBlockType[ch] == ONLY_SHORT_WINDOW);
        /* Require both PCM short window and high transient strength to trigger SBR grid splits,
         * preventing false transient splits on steady-state music at mid/high bitrates. */
        if (pcm_attack && (sa->ch[ch].transientStrength >= 25.0f || (sbr->numEnvFixFix == 1 && attack[ch]))) {
            attack[ch] = 1;
            if (pos[ch] == 0) pos[ch] = 2;
        } else {
            attack[ch] = 0;
        }
    }

    if (nch == 2 && !lfe[0] && !lfe[1]) {
        int shared_attack = attack[0] || attack[1];
        int shared_pos = (attack[0] && attack[1]) ? ((pos[0] < pos[1]) ? pos[0] : pos[1]) : (attack[0] ? pos[0] : pos[1]);
        int shared_split = !shared_attack && (split[0] || split[1]);
        sbr_grid_next(&sa->ch[0].state, f->numSlots, shared_attack, shared_pos, shared_split, 0, sbr->numEnvFixFix, 1, &sa->ch[0].grid);
        sa->ch[1].grid = sa->ch[0].grid;
        sa->ch[1].state = sa->ch[0].state;
    } else {
        for (int ch = 0; ch < nch; ch++) {
            if (lfe[ch]) continue;
            sbr_grid_next(&sa->ch[ch].state, f->numSlots, attack[ch], pos[ch], split[ch], 0, sbr->numEnvFixFix, 1, &sa->ch[ch].grid);
        }
    }

    for (int ch = 0; ch < nch; ch++) {
        fd->ch[ch].grid = sa->ch[ch].grid;
        fd->ch[ch].eff_amp_res = (sa->ch[ch].grid.frameClass == SBR_FRAME_CLASS_FIXFIX && sa->ch[ch].grid.numEnvelopes == 1) ? 0 : SBR_AMP_RES;
    }
}

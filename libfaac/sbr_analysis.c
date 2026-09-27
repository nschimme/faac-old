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

    /* Fixed right border request suppresses attack flag and clears spread BEFORE applying transition table */
    if (fixedRightBorder) {
        attack = 0;
        ac->spread = false;
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
        int rel = (leading + T) / 2;
        rel = (rel + 1) & ~1;
        if (rel <= leading) rel = leading + 2;
        if (rel >= T) rel = T - 2;

        g.numEnvelopes = 2;
        g.tEnv[0] = leading;
        g.tEnv[1] = rel;
        g.tEnv[2] = T;
        g.freqRes[0] = 1;
        g.freqRes[1] = 1;
        g.bsPointer = 0;
        ac->spread = false;

        ac->followUp.numBorders = 0;
        ac->followUp.transientIdx = -1;
    } else if (curr == SBR_FRAME_CLASS_VARVAR) {
        ac->spread = false;
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

        ac->followUp.numBorders = 0;
        ac->followUp.transientIdx = -1;
    }

    ac->grid = g;
}

static void measure(SignalAnalysisChannel *ac, const SbrAnalysisFrame *f, const SbrAnalysisFrame *next, const SbrAnalysisFrame *ahead, int ch, int *attack, int *pos, int *split)
{
    (void)next; (void)ahead;
    int slots = f->numSlots;
    float mean_e = 0.0f;
    for (int s = 0; s < slots; s++) mean_e += f->totalE[ch][s];
    mean_e /= (float)(slots ? slots : 1);

    int at = -1;
    float max_jump = 0.0f;

    for (int s = 1; s < slots; s++) {
        float prev = f->totalE[ch][s - 1];
        float curr = f->totalE[ch][s];
        float ratio = curr / (prev + SBR_ENERGY_FLOOR);
        float jump = (curr - prev) / (mean_e + SBR_ENERGY_FLOOR);

        if (ratio > 8.0f && jump > 8.0f && jump > max_jump) {
            max_jump = jump;
            at = s;
        }
    }

    if (at >= 0) {
        *attack = 1;
        *pos = at;
        ac->transientPos = at;
        ac->transientStrength = max_jump;
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
        *split = (ratio > 5.0f) && (total_e > SBR_ENERGY_FLOOR * 1000.0f);
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

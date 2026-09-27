/* Stateful delayed SBR analysis: retain slot energies, not PCM. */
#include "sbr.h"
#include "sbr_analysis.h"
#include "sbr_internal.h"
#include "util.h"
#include <string.h>

static int even_clamp(int x, int lo, int hi) { return clamp_int(x, lo, hi) & ~1; }
static void set_pointer(SbrGrid *g, int t) { int p = 0; while (p + 1 < g->numEnvelopes && t >= g->tEnv[p + 1]) p++; g->bsPointer = p; }

/* The sole state carried between finalized frames is the legal trailing
 * border.  It makes an attack decay a state transition rather than an
 * independent single-frame classification. */
static void choose_grid(SignalAnalysisChannel *ac, int fixed, int slots, int attack, int tail)
{
    SbrGrid g = { 0 };
    int t = even_clamp(ac->transientSlot * SBR_NUM_TIME_SLOTS / slots, 0, 14);
    int carry = ac->trailingBorder > SBR_NUM_TIME_SLOTS ? ac->trailingBorder - SBR_NUM_TIME_SLOTS : 0;
    int n = 2 + (ac->transientStrength > 4.5f) + (ac->transientStrength > 7.5f);
    if (fixed == 1) {
        g.frameClass = SBR_FRAME_CLASS_FIXFIX; g.numEnvelopes = 1; g.tEnv[1] = SBR_NUM_TIME_SLOTS; g.freqRes[0] = 1;
        if (attack) { int rel = clamp_int((t - 2) / 2, 0, 3); g.frameClass = SBR_FRAME_CLASS_VARFIX; g.numEnvelopes = 2; g.tEnv[1] = 2 * rel + 2; g.tEnv[2] = SBR_NUM_TIME_SLOTS; g.freqRes[1] = 1; }
    } else if (!attack) {
        if (carry) { g.frameClass = SBR_FRAME_CLASS_VARFIX; g.numEnvelopes = 2; g.tEnv[0] = carry; g.tEnv[1] = 8; g.tEnv[2] = SBR_NUM_TIME_SLOTS; }
        else { g.frameClass = SBR_FRAME_CLASS_FIXFIX; g.numEnvelopes = fixed; for (int e = 0; e <= fixed; e++) g.tEnv[e] = e * SBR_NUM_TIME_SLOTS / fixed; }
    } else if (tail && carry) {
        g.frameClass = SBR_FRAME_CLASS_VARVAR; g.numEnvelopes = n + (ac->transientStrength > 12.0f); g.tEnv[0] = 2;
        if (g.numEnvelopes == 2) g.tEnv[1] = 10;
        else if (g.numEnvelopes == 3) { g.tEnv[1] = 8; g.tEnv[2] = 14; }
        else if (g.numEnvelopes == 4) { g.tEnv[1] = 6; g.tEnv[2] = 10; g.tEnv[3] = 14; }
        else { g.tEnv[1] = 4; g.tEnv[2] = 8; g.tEnv[3] = 12; g.tEnv[4] = 16; }
        g.tEnv[g.numEnvelopes] = 18;
    } else if (tail) {
        g.frameClass = SBR_FRAME_CLASS_FIXVAR; g.numEnvelopes = n; g.tEnv[0] = 0;
        if (n == 2) g.tEnv[1] = even_clamp(t, 10, 16);
        else if (n == 3) { g.tEnv[1] = 8; g.tEnv[2] = even_clamp(t, 10, 16); }
        else { g.tEnv[1] = 4; g.tEnv[2] = 8; g.tEnv[3] = even_clamp(t, 10, 16); }
        g.tEnv[n] = 18;
    } else {
        int start = carry ? 2 : 0;
        g.frameClass = SBR_FRAME_CLASS_VARFIX; g.numEnvelopes = n; g.tEnv[0] = start;
        if (n == 2) g.tEnv[1] = even_clamp(t, start + 2, 8);
        else if (n == 3) { g.tEnv[1] = even_clamp(t, start + 2, 6); g.tEnv[2] = g.tEnv[1] + 4; }
        else { g.tEnv[1] = even_clamp(t, start + 2, 6); g.tEnv[2] = g.tEnv[1] + 2; g.tEnv[3] = g.tEnv[1] + 6; }
        g.tEnv[n] = SBR_NUM_TIME_SLOTS;
    }
    for (int e = 0; e < g.numEnvelopes; e++) g.freqRes[e] = g.tEnv[e + 1] - g.tEnv[e] > 4;
    if (g.frameClass == SBR_FRAME_CLASS_FIXFIX) g.freqRes[0] = 1;
    set_pointer(&g, t); ac->grid = g; ac->trailingBorder = g.tEnv[g.numEnvelopes];
}

void SbrAnalyzeFrame(SbrAnalysisFrame *f, float *in[], int nch, const bool *lfe, int samples, struct SBRInfo *sbr)
{
    float work[SBR_QMF_HIST_LEN + 2 * FRAME_LEN];
    int slots = samples / SBR_QMF_BANDS_64;
    memset(f, 0, sizeof(*f)); f->numSlots = slots;
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

static void measure(SignalAnalysisChannel *ac, const SbrAnalysisFrame *f, const SbrAnalysisFrame *next, const SbrAnalysisFrame *ahead, int ch, int *attack, int *tail)
{
    float peak = 0, sum = 0, future = 0, base = 0; int at = 0;
    for (int s = 0; s < f->numSlots; s++) { float e = f->totalE[ch][s]; sum += e; if (e > peak) { peak = e; at = s; } }
    for (int s = 0; s < next->numSlots; s++) if (next->totalE[ch][s] > future) future = next->totalE[ch][s];
    for (int s = 0; s < ahead->numSlots; s++) if (ahead->totalE[ch][s] > future) future = ahead->totalE[ch][s];
    int first = at > 4 ? at - 4 : 0;
    for (int s = first; s < at; s++) base += f->totalE[ch][s];
    base /= (float)(at - first ? at - first : 1);
    ac->transientSlot = at; ac->transientStrength = peak / (sum / (float)f->numSlots + SBR_ENERGY_FLOOR);
    *attack = ac->transientStrength > SBR_TRANSIENT_THRESH_DEFAULT && peak / (base + SBR_ENERGY_FLOOR) > 2.0f;
    *tail = *attack && (at >= f->numSlots - 8 || (at >= f->numSlots - 12 && future > peak * .45f));
}

void SbrFinalizeFrame(SignalAnalysis *sa, const SbrAnalysisFrame *f, const SbrAnalysisFrame *next, const SbrAnalysisFrame *ahead, int nch, const bool *lfe, struct SBRInfo *sbr, struct SbrFrameData *fd)
{
    int attack[MAX_CHANNELS] = {0}, tail[MAX_CHANNELS] = {0}; sa->numSlots = f->numSlots;
    for (int ch = 0; ch < nch; ch++) measure(&sa->ch[ch], f, next, ahead, ch, &attack[ch], &tail[ch]);
    /* Couple only compatible, independently-detected stereo attacks. */
    if (nch == 2 && !lfe[0] && !lfe[1] && attack[0] && attack[1]) {
        SignalAnalysisChannel *a = &sa->ch[0], *b = &sa->ch[1]; int d = a->transientSlot - b->transientSlot;
        float lo = a->transientStrength < b->transientStrength ? a->transientStrength : b->transientStrength, hi = a->transientStrength > b->transientStrength ? a->transientStrength : b->transientStrength;
        if (d >= -4 && d <= 4 && lo * 2 >= hi) { if (b->transientStrength > a->transientStrength) { a->transientSlot = b->transientSlot; a->transientStrength = b->transientStrength; tail[0] = tail[1]; } else { b->transientSlot = a->transientSlot; b->transientStrength = a->transientStrength; tail[1] = tail[0]; } }
    }
    for (int ch = 0; ch < nch; ch++) { choose_grid(&sa->ch[ch], sbr->numEnvFixFix, f->numSlots, attack[ch], tail[ch]); fd->ch[ch].grid = sa->ch[ch].grid; fd->ch[ch].eff_amp_res = (sa->ch[ch].grid.frameClass == SBR_FRAME_CLASS_FIXFIX && sa->ch[ch].grid.numEnvelopes == 1) ? 0 : SBR_AMP_RES; }
}

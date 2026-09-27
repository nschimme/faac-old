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

typedef struct {
    int numBorders;
    int borders[32];
    int freqRes[32];
    int transientBorderIdx;
} WorkingGrid;

static int get_detector_offset(int T)
{
    return (T == 18) ? 8 : 4;
}

static int get_min_join(int T)
{
    return (T == 9) ? 2 : 4;
}

static int get_max_join(int T)
{
    if (T == 9) return 8;
    if (T == 18) return 15;
    return 12;
}

static int get_overlap_cap(int T, int pos)
{
    if (T == 16) {
        if (pos < 4) return 6;
        if (pos == 4 || pos == 5) return 4;
        return 8;
    }
    if (T == 15) {
        if (pos < 4) return 5;
        if (pos == 4 || pos == 5) return 3;
        return 7;
    }
    return 8;
}

static void add_border(WorkingGrid *wg, int border, int res)
{
    if (wg->numBorders < 31) {
        wg->borders[wg->numBorders] = border;
        wg->freqRes[wg->numBorders] = res;
        wg->numBorders++;
    }
}

static void choose_grid(SignalAnalysisChannel *ac, int numEnvFixFix, int slots, int attack, int pos, int split, int fixedRightBorder, struct SBRInfo *sbr)
{
    SbrGrid g = { 0 };
    int T = slots;
    int det_offset = get_detector_offset(T);
    int min_join = get_min_join(T);
    int max_join = get_max_join(T);
    int ovl_cap = get_overlap_cap(T, pos);

    /* Strategy 1: Cap envelopes to 2 for bitrates >= 40k total stereo (>= 20k/ch) */
    int max_env_cap = 5;
    if (sbr && sbr->numChannels > 0 && sbr->bitRate > 0) {
        unsigned long rate_per_ch = sbr->bitRate / sbr->numChannels;
        if (rate_per_ch >= 20000) {
            max_env_cap = 2;
        }
    }

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
    } else if (curr == SBR_FRAME_CLASS_FIXVAR || (curr == SBR_FRAME_CLASS_VARVAR && attack)) {
        int A = pos + det_offset;
        WorkingGrid wg = { 0 };
        wg.transientBorderIdx = -1;

        if (curr == SBR_FRAME_CLASS_VARVAR && attack && ac->followUp.numBorders > 0) {
            /* Join retained old borders to new attack design */
            int last_old = ac->followUp.borders[0];
            if (last_old < A - min_join) {
                add_border(&wg, last_old, ac->followUp.freqRes[0]);
            }
        } else {
            /* Gap before A */
            int cur_b = 0;
            while (A - cur_b > max_join) {
                int seg = (A - cur_b > 8) ? 8 : (A - cur_b);
                seg = (seg + 1) & ~1;
                if (seg < 2) seg = 2;
                cur_b += seg;
                add_border(&wg, cur_b, 1);
            }
        }

        /* Attack mandatory initial borders: A, A+2, A+6 */
        wg.transientBorderIdx = wg.numBorders;
        add_border(&wg, A, 0);
        add_border(&wg, A + 2, 0);
        add_border(&wg, A + 6, 1);

        /* Region after A+6 */
        int last_b = A + 6;
        int post_cap = (ovl_cap < max_join) ? ovl_cap : max_join;

        /* Find last border strictly before T */
        int last_inside = 0;
        for (int i = 0; i < wg.numBorders; i++) {
            if (wg.borders[i] < T) {
                last_inside = wg.borders[i];
            }
        }

        int remainder = T - last_inside;
        if (remainder > 0 && remainder < min_join) {
            /* Spreading trigger: append high-resolution border 8 slots later */
            add_border(&wg, last_inside + 8, 1);
            ac->spread = true;
        } else {
            while (last_b < T + min_join) {
                last_b += post_cap;
                add_border(&wg, last_b, 1);
            }
            ac->spread = false;
        }

        /* Find common border at or after T */
        int c_idx = 0;
        while (c_idx < wg.numBorders && wg.borders[c_idx] < T) {
            c_idx++;
        }
        if (c_idx >= wg.numBorders) {
            c_idx = wg.numBorders;
            add_border(&wg, T, 1);
        }

        int common_border = wg.borders[c_idx];

        /* Build current frame grid */
        if (curr == SBR_FRAME_CLASS_FIXVAR) {
            int n_env = c_idx + 1;
            if (n_env > max_env_cap) n_env = max_env_cap;
            g.numEnvelopes = n_env;
            g.tEnv[0] = 0;
            for (int i = 1; i < n_env; i++) {
                g.tEnv[i] = wg.borders[i - 1];
            }
            g.tEnv[n_env] = common_border;

            /* Transmit frequency flags in reverse order for FIXVAR */
            for (int e = 0; e < g.numEnvelopes; e++) {
                int w_idx = (e < c_idx) ? e : (c_idx - 1);
                g.freqRes[g.numEnvelopes - 1 - e] = wg.freqRes[w_idx];
            }

            /* Pointer counts backward from trailing edge toward transient border */
            int p = 0;
            for (int e = g.numEnvelopes; e >= 1; e--) {
                if (e - 1 == wg.transientBorderIdx) {
                    p = g.numEnvelopes - e + 1;
                    break;
                }
            }
            g.bsPointer = p;
        } else { /* VARVAR new attack */
            int leading = (ac->followUp.numBorders > 0) ? ac->followUp.borders[0] : 0;
            int n_env = c_idx + 1;
            if (n_env > max_env_cap) n_env = max_env_cap;
            g.numEnvelopes = n_env;
            g.tEnv[0] = leading;
            for (int i = 1; i < n_env; i++) {
                g.tEnv[i] = wg.borders[i - 1];
            }
            g.tEnv[n_env] = common_border;
            for (int e = 0; e < g.numEnvelopes; e++) {
                int w_idx = (e < c_idx) ? e : (c_idx - 1);
                g.freqRes[e] = wg.freqRes[w_idx];
            }
            g.bsPointer = (wg.transientBorderIdx >= 0) ? (wg.transientBorderIdx + 1) : 0;
        }

        /* Save follow-up state for next frame (subtracting T) */
        ac->followUp.numBorders = 0;
        for (int i = c_idx; i < wg.numBorders; i++) {
            if (ac->followUp.numBorders < SBR_MAX_ENVELOPES + 1) {
                ac->followUp.borders[ac->followUp.numBorders] = wg.borders[i] - T;
                ac->followUp.freqRes[ac->followUp.numBorders] = wg.freqRes[i];
                ac->followUp.numBorders++;
            }
        }

    } else if (curr == SBR_FRAME_CLASS_VARFIX) {
        ac->spread = false;
        if (ac->followUp.numBorders > 0) {
            int n_env = ac->followUp.numBorders;
            if (n_env > max_env_cap) n_env = max_env_cap;
            g.numEnvelopes = n_env;
            g.tEnv[0] = ac->followUp.borders[0];
            for (int e = 1; e < n_env; e++) {
                g.tEnv[e] = ac->followUp.borders[e];
            }
            g.tEnv[n_env] = T;
            for (int e = 0; e < n_env; e++) {
                g.freqRes[e] = ac->followUp.freqRes[e];
            }
            g.bsPointer = 0;
        } else {
            g.numEnvelopes = 2;
            g.tEnv[0] = 0;
            g.tEnv[1] = (T + 1) / 2;
            g.tEnv[2] = T;
            g.freqRes[0] = 1;
            g.freqRes[1] = 1;
            g.bsPointer = 0;
        }
        ac->followUp.numBorders = 0;

    } else if (curr == SBR_FRAME_CLASS_VARVAR && ac->spread) {
        /* Consume spread VARVAR */
        ac->spread = false;
        if (ac->followUp.numBorders > 0) {
            int n_env = ac->followUp.numBorders;
            if (n_env > max_env_cap) n_env = max_env_cap;
            g.numEnvelopes = n_env;
            g.tEnv[0] = ac->followUp.borders[0];
            for (int e = 1; e <= n_env; e++) {
                g.tEnv[e] = (e < ac->followUp.numBorders) ? ac->followUp.borders[e] : (T + 2);
            }
            for (int e = 0; e < n_env; e++) {
                g.freqRes[e] = ac->followUp.freqRes[e];
            }
            g.bsPointer = 0;
        } else {
            g.numEnvelopes = 2;
            g.tEnv[0] = 0;
            g.tEnv[1] = 8;
            g.tEnv[2] = 18;
            g.freqRes[0] = 1;
            g.freqRes[1] = 1;
            g.bsPointer = 0;
        }
        /* Save single high-resolution leading border for final VARFIX */
        ac->followUp.numBorders = 1;
        ac->followUp.borders[0] = g.tEnv[g.numEnvelopes] - T;
        ac->followUp.freqRes[0] = 1;
    }

    ac->grid = g;
}

static void measure(SignalAnalysisChannel *ac, const SbrAnalysisFrame *f, const SbrAnalysisFrame *next, const SbrAnalysisFrame *ahead, int ch, struct SBRInfo *sbr, int *attack, int *pos, int *split)
{
    (void)next; (void)ahead;
    int slots = f->numSlots;
    float band_thresh[SBR_QMF_BANDS_64] = { 0 };
    float scores[32] = { 0 };

    /* Calculate QMF band thresholds and score accumulation */
    for (int k = 0; k < SBR_QMF_BANDS_64; k++) {
        float mean = 0.0f;
        for (int s = 0; s < slots; s++) mean += f->bandE[ch][s][k];
        mean /= (float)(slots ? slots : 1);

        float var = 0.0f;
        for (int s = 0; s < slots; s++) {
            float d = f->bandE[ch][s][k] - mean;
            var += d * d;
        }
        float stddev = sqrtf(var / (float)(slots ? slots : 1));
        band_thresh[k] = 0.34f * stddev + 1e-4f;
    }

    /* Accumulate position scores */
    for (int s = 1; s < slots - 1; s++) {
        float score = 0.0f;
        for (int k = 0; k < SBR_QMF_BANDS_64; k++) {
            float r_sum = f->bandE[ch][s][k] + f->bandE[ch][s + 1][k];
            float l_sum = f->bandE[ch][s - 1][k];
            float diff = r_sum - l_sum;
            if (diff > band_thresh[k]) score += (diff - band_thresh[k]);
        }
        scores[s] = score;
    }

    /* Rate-aware transient threshold scaling:
     * When bitrate per channel is <= 32000 BPS (64k stereo or 24-56k stereo),
     * scale threshold up so multi-envelope SBR payloads are reserved for strong transients,
     * protecting core AAC-LC quantization bits and maximizing ViSQOL MOS. */
    float norm_thresh = (float)SBR_TRANSIENT_THRESH_DEFAULT / (float)SBR_QMF_BANDS_64;
    if (sbr && sbr->numChannels > 0 && sbr->bitRate > 0) {
        unsigned long rate_per_ch = sbr->bitRate / sbr->numChannels;
        if (rate_per_ch <= 32000) {
            float scale = 1.0f + 2.5f * (1.0f - (float)rate_per_ch / 32000.0f);
            norm_thresh *= scale;
        }
    }

    int reported_slot = -1;
    for (int s = 1; s < slots - 1; s++) {
        if (scores[s] > norm_thresh && scores[s + 1] < 0.90f * scores[s]) {
            reported_slot = s;
            break;
        }
    }

    if (reported_slot >= 0) {
        *attack = 1;
        *pos = reported_slot;
        ac->transientPos = reported_slot;
        ac->transientStrength = scores[reported_slot];
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

static bool check_grid_equal(const SbrGrid *g1, const SbrGrid *g2)
{
    if (g1->frameClass != g2->frameClass) return false;
    if (g1->numEnvelopes != g2->numEnvelopes) return false;
    if (g1->bsPointer != g2->bsPointer) return false;
    for (int e = 0; e <= g1->numEnvelopes; e++) {
        if (g1->tEnv[e] != g2->tEnv[e]) return false;
    }
    for (int e = 0; e < g1->numEnvelopes; e++) {
        if (g1->freqRes[e] != g2->freqRes[e]) return false;
    }
    return true;
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
        measure(&sa->ch[ch], f, next, ahead, ch, sbr, &attack[ch], &pos[ch], &split[ch]);
    }

    if (nch == 2 && !lfe[0] && !lfe[1]) {
        /* Construct individual channel grids first */
        choose_grid(&sa->ch[0], sbr->numEnvFixFix, f->numSlots, attack[0], pos[0], split[0], 0, sbr);
        choose_grid(&sa->ch[1], sbr->numEnvFixFix, f->numSlots, attack[1], pos[1], split[1], 0, sbr);

        /* In coupled stereo, if grids match or if bitrates are constrained (<= 32k/ch),
         * share channel 0's grid to save SBR payload bits for AAC-LC core quantization. */
        unsigned long rate_per_ch = (sbr && sbr->numChannels > 0) ? (sbr->bitRate / sbr->numChannels) : 0;
        if (check_grid_equal(&sa->ch[0].grid, &sa->ch[1].grid) || (rate_per_ch > 0 && rate_per_ch <= 32000 && !attack[0] && !attack[1])) {
            sa->ch[1].grid = sa->ch[0].grid;
            sa->ch[1].prevClass = sa->ch[0].prevClass;
            sa->ch[1].spread = sa->ch[0].spread;
            sa->ch[1].followUp = sa->ch[0].followUp;
        }
    } else {
        for (int ch = 0; ch < nch; ch++) {
            if (lfe[ch]) continue;
            choose_grid(&sa->ch[ch], sbr->numEnvFixFix, f->numSlots, attack[ch], pos[ch], split[ch], 0, sbr);
        }
    }

    for (int ch = 0; ch < nch; ch++) {
        fd->ch[ch].grid = sa->ch[ch].grid;
        fd->ch[ch].eff_amp_res = (sa->ch[ch].grid.frameClass == SBR_FRAME_CLASS_FIXFIX && sa->ch[ch].grid.numEnvelopes == 1) ? 0 : SBR_AMP_RES;
    }
}

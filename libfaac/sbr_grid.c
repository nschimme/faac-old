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

#include "sbr_grid.h"
#include <assert.h>
#include <string.h>

void sbr_grid_state_init(SbrGridState *st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    st->prevClass = SBR_FRAME_CLASS_FIXFIX;
    st->spread = false;
    st->transientIdx = -1;
    st->firstFillIdx = -1;
}

bool sbr_grid_equal(const SbrGrid *g1, const SbrGrid *g2)
{
    if (!g1 || !g2) return false;
    if (g1->frameClass != g2->frameClass) return false;
    if (g1->numEnvelopes != g2->numEnvelopes) return false;
    if (g1->bsPointer != g2->bsPointer) return false;
    for (int i = 0; i <= g1->numEnvelopes; i++) {
        if (g1->tEnv[i] != g2->tEnv[i]) return false;
    }
    for (int i = 0; i < g1->numEnvelopes; i++) {
        if (g1->freqRes[i] != g2->freqRes[i]) return false;
    }
    return true;
}

void sbr_grid_next(SbrGridState *st,
                   int T,
                   int attack,
                   int pos,
                   int split,
                   int fixRight,
                   int numEnvFixFix,
                   int fixfixFreqRes,
                   SbrGrid *out)
{
    assert(st != NULL);
    assert(out != NULL);

    int det_offset = (T == 18) ? 8 : 4;
    int min_join = (T == 9) ? 2 : 4;
    int max_join = (T == 9) ? 8 : (T == 18) ? 15 : 12;

    int ovl_cap = (T == 16) ? ((pos < 4) ? 6 : (pos <= 5) ? 4 : 8) :
                  (T == 15) ? ((pos < 4) ? 5 : (pos <= 5) ? 3 : 7) : 8;

    if (fixRight) {
        attack = 0;
        st->spread = false;
    }

    SbrFrameClass prev = st->prevClass;
    SbrFrameClass curr = SBR_FRAME_CLASS_FIXFIX;

    if (prev == SBR_FRAME_CLASS_FIXFIX) {
        curr = attack ? SBR_FRAME_CLASS_FIXVAR : SBR_FRAME_CLASS_FIXFIX;
    } else if (prev == SBR_FRAME_CLASS_FIXVAR) {
        if (attack) {
            curr = SBR_FRAME_CLASS_VARVAR;
            st->spread = false;
        } else if (st->spread) {
            curr = SBR_FRAME_CLASS_VARVAR;
        } else {
            curr = SBR_FRAME_CLASS_VARFIX;
        }
    } else if (prev == SBR_FRAME_CLASS_VARFIX) {
        curr = attack ? SBR_FRAME_CLASS_FIXVAR : SBR_FRAME_CLASS_FIXFIX;
    } else if (prev == SBR_FRAME_CLASS_VARVAR) {
        if (attack) {
            curr = SBR_FRAME_CLASS_VARVAR;
            st->spread = false;
        } else if (st->spread) {
            curr = SBR_FRAME_CLASS_VARVAR;
        } else {
            curr = SBR_FRAME_CLASS_VARFIX;
        }
    }

    st->prevClass = curr;
    out->frameClass = curr;

    if (curr == SBR_FRAME_CLASS_FIXFIX) {
        st->spread = false;
        st->numFollowUpBorders = 0;
        st->transientIdx = -1;
        st->firstFillIdx = -1;

        int n_env = (split || numEnvFixFix == 2) ? 2 : 1;
        out->numEnvelopes = n_env;
        out->tEnv[0] = 0;
        if (n_env == 1) {
            out->tEnv[1] = T;
            out->freqRes[0] = fixfixFreqRes;
        } else {
            int mid = (T == 15) ? 8 : (T + 1) / 2;
            out->tEnv[1] = mid;
            out->tEnv[2] = T;
            out->freqRes[0] = fixfixFreqRes;
            out->freqRes[1] = fixfixFreqRes;
        }
        out->bsPointer = 0;
    } else {
        int wb[32];
        int freqRes[32];
        int nwb = 0;

        int trans_idx = -1;
        int first_fill_idx = -1;

        if (curr == SBR_FRAME_CLASS_FIXVAR) {
            st->spread = false;
            int A = pos + det_offset;

            wb[nwb++] = 0;

            if (A > max_join) {
                int gap = A;
                int segs = (gap + max_join - 1) / max_join;
                int step = ((gap / segs) + 1) & ~1;
                if (step < 2) step = 2;
                if (step > 8) step = 8;
                int cur = 0;
                while (cur + step < A) {
                    cur += step;
                    wb[nwb] = cur;
                    freqRes[nwb - 1] = 1;
                    nwb++;
                }
            }

            wb[nwb] = A; freqRes[nwb - 1] = 1; nwb++;
            trans_idx = nwb - 1;
            wb[nwb] = A + 2; freqRes[nwb - 1] = 0; nwb++;
            wb[nwb] = A + 6; freqRes[nwb - 1] = 0; nwb++;

            int cur = A + 6;
            int cap = (max_join < ovl_cap) ? max_join : ovl_cap;
            int step = (cap / 2) * 2;
            if (step < 2) step = 2;

            first_fill_idx = nwb;
            while (cur < T + max_join + 8) {
                cur += step;
                wb[nwb] = cur;
                freqRes[nwb - 1] = 1;
                nwb++;
            }
        } else if (curr == SBR_FRAME_CLASS_VARFIX) {
            st->spread = false;
            if (st->numFollowUpBorders > 0) {
                for (int i = 0; i < st->numFollowUpBorders; i++) {
                    if (st->followUpBorders[i] >= T) break;
                    wb[nwb] = st->followUpBorders[i];
                    if (i > 0) freqRes[nwb - 1] = st->followUpFreqRes[i - 1];
                    nwb++;
                }
            }
            if (nwb == 0) {
                wb[0] = 0;
                wb[1] = T;
                freqRes[0] = 1;
                nwb = 2;
            } else if (wb[nwb - 1] < T) {
                wb[nwb] = T;
                freqRes[nwb - 1] = 1;
                nwb++;
            }
        } else if (curr == SBR_FRAME_CLASS_VARVAR && !attack) {
            /* Spread-only VARVAR */
            st->spread = false;
            for (int i = 0; i < st->numFollowUpBorders; i++) {
                wb[nwb] = st->followUpBorders[i];
                if (i > 0) freqRes[nwb - 1] = st->followUpFreqRes[i - 1];
                nwb++;
            }
            if (nwb == 0) {
                wb[0] = 0; wb[1] = T;
                freqRes[0] = 1;
                nwb = 2;
            }
        } else if (curr == SBR_FRAME_CLASS_VARVAR) {
            st->spread = false;
            int A = pos + det_offset;

            int retained_count = 0;
            int fill_bound = (st->firstFillIdx >= 0) ? st->firstFillIdx : st->numFollowUpBorders;
            if (fill_bound > st->numFollowUpBorders) fill_bound = st->numFollowUpBorders;

            for (int i = 0; i < fill_bound; i++) {
                if (st->followUpBorders[i] + min_join <= A) {
                    retained_count = i + 1;
                } else {
                    break;
                }
            }

            for (int i = 0; i < retained_count; i++) {
                wb[nwb] = st->followUpBorders[i];
                if (i > 0) freqRes[nwb - 1] = st->followUpFreqRes[i - 1];
                nwb++;
            }

            if (nwb == 0) {
                wb[nwb++] = 0;
            }

            int last_retained = wb[nwb - 1];
            if (A - last_retained > max_join) {
                int gap = A - last_retained;
                int segs = (gap + max_join - 1) / max_join;
                int step = ((gap / segs) + 1) & ~1;
                if (step < 2) step = 2;
                if (step > 8) step = 8;
                int cur = last_retained;
                while (cur + step < A) {
                    cur += step;
                    wb[nwb] = cur;
                    freqRes[nwb - 1] = 1;
                    nwb++;
                }
            }

            wb[nwb] = A; freqRes[nwb - 1] = 1; nwb++;
            trans_idx = nwb - 1;
            wb[nwb] = A + 2; freqRes[nwb - 1] = 0; nwb++;
            wb[nwb] = A + 6; freqRes[nwb - 1] = 0; nwb++;

            int cur = A + 6;
            int cap = (max_join < ovl_cap) ? max_join : ovl_cap;
            int step = (cap / 2) * 2;
            if (step < 2) step = 2;

            first_fill_idx = nwb;
            while (cur < T + max_join + 8) {
                cur += step;
                wb[nwb] = cur;
                freqRes[nwb - 1] = 1;
                nwb++;
            }
        }

        int comm_idx = 1;
        while (comm_idx < nwb && wb[comm_idx] < T) comm_idx++;
        if (comm_idx >= nwb) comm_idx = nwb - 1;

        if (curr == SBR_FRAME_CLASS_VARVAR && !attack) {
            /* Spread-only VARVAR */
            out->numEnvelopes = comm_idx;
            for (int i = 0; i <= comm_idx; i++) out->tEnv[i] = wb[i];
            for (int i = 0; i < comm_idx; i++) out->freqRes[i] = freqRes[i];
            out->bsPointer = 0;

            st->numFollowUpBorders = 1;
            st->followUpBorders[0] = wb[comm_idx] - T;
            st->followUpFreqRes[0] = 1;
            st->transientIdx = -1;
            st->firstFillIdx = -1;
            st->spread = false;
        } else {
            st->spread = false;
            int post_rem = wb[comm_idx] - T;
            if (post_rem > 0 && post_rem < min_join) {
                wb[comm_idx] += 8;
                freqRes[comm_idx - 1] = 1;
                st->spread = true;
            }

            out->numEnvelopes = comm_idx;
            for (int i = 0; i <= comm_idx; i++) out->tEnv[i] = wb[i];
            for (int i = 0; i < comm_idx; i++) out->freqRes[i] = freqRes[i];

            if (curr == SBR_FRAME_CLASS_FIXVAR) {
                if (trans_idx > 0 && trans_idx <= comm_idx) {
                    out->bsPointer = comm_idx - trans_idx + 1;
                } else {
                    out->bsPointer = 0;
                }
            } else if (curr == SBR_FRAME_CLASS_VARFIX) {
                if (st->transientIdx > 0 && st->transientIdx <= comm_idx) {
                    out->bsPointer = st->transientIdx;
                } else {
                    out->bsPointer = 0;
                }
            } else if (curr == SBR_FRAME_CLASS_VARVAR) {
                if (trans_idx > 0 && trans_idx <= comm_idx) {
                    out->bsPointer = trans_idx;
                } else {
                    out->bsPointer = 0;
                }
            }

            st->numFollowUpBorders = 0;
            for (int i = comm_idx; i < nwb; i++) {
                st->followUpBorders[st->numFollowUpBorders] = wb[i] - T;
                if (i > comm_idx) {
                    st->followUpFreqRes[st->numFollowUpBorders - 1] = freqRes[i - 1];
                }
                st->numFollowUpBorders++;
            }

            if (trans_idx > comm_idx) {
                st->transientIdx = trans_idx - comm_idx;
            } else {
                st->transientIdx = -1;
            }

            if (first_fill_idx > comm_idx) {
                st->firstFillIdx = first_fill_idx - comm_idx;
            } else {
                st->firstFillIdx = -1;
            }
        }
    }

    assert(out->numEnvelopes >= 1);
    assert(out->frameClass == SBR_FRAME_CLASS_VARVAR ? out->numEnvelopes <= 5 : out->numEnvelopes <= 4);
    assert(out->bsPointer >= 0 && out->bsPointer <= out->numEnvelopes);
}

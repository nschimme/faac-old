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

#ifndef SBR_GRID_H
#define SBR_GRID_H

#include <stdbool.h>
#include "sbr.h"

#ifndef SBR_MAX_ENVELOPES
#define SBR_MAX_ENVELOPES 5
#endif

typedef struct SbrGrid {
    SbrFrameClass frameClass;
    int numEnvelopes;
    int tEnv[SBR_MAX_ENVELOPES + 1];
    int bsPointer;
    int freqRes[SBR_MAX_ENVELOPES];
} SbrGrid;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SbrGridState {
    SbrFrameClass prevClass;
    bool spread;
    int numFollowUpBorders;
    int followUpBorders[SBR_MAX_ENVELOPES + 4];
    int followUpFreqRes[SBR_MAX_ENVELOPES + 4];
    int transientIdx;
    int firstFillIdx;
} SbrGridState;

void sbr_grid_state_init(SbrGridState *st);

void sbr_grid_next(SbrGridState *st,
                   int T,
                   int attack,
                   int pos,
                   int split,
                   int fixRight,
                   int numEnvFixFix,
                   int fixfixFreqRes,
                   SbrGrid *out);

bool sbr_grid_equal(const SbrGrid *g1, const SbrGrid *g2);

#ifdef __cplusplus
}
#endif

#endif

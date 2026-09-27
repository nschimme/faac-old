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

#ifndef SBR_ANALYSIS_H
#define SBR_ANALYSIS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SBR_QMF_BANDS_64
#define SBR_QMF_BANDS_64 64
#endif

#ifndef SBR_MAX_ENVELOPES
#define SBR_MAX_ENVELOPES 5
#endif

struct SBRInfo;
struct SbrFrameData;

typedef struct {
    SbrFrameClass frameClass;
    int numEnvelopes;
    int tEnv[SBR_MAX_ENVELOPES + 1];
    int bsPointer;
    int freqRes[SBR_MAX_ENVELOPES];
} SbrGrid;

typedef struct SbrFollowUpState {
    int numBorders;
    int borders[SBR_MAX_ENVELOPES + 2];
    int freqRes[SBR_MAX_ENVELOPES + 1];
    int transientIdx;
    int firstFillIdx;
} SbrFollowUpState;

typedef struct SignalAnalysisChannel {
    SbrFrameClass prevClass;        /* Previous emitted class (initialized to FIXFIX) */
    bool          spread;           /* Spread flag (initialized to false) */
    SbrFollowUpState followUp;      /* Saved follow-up borders & metadata */

    int   transientSlot;            /* Slot index on grid axis */
    int   transientPos;             /* Reported QMF position offset */
    float transientStrength;
    int   attack;                   /* Transient detected flag */
    int   split;                    /* Stable frame split flag */

    SbrGrid grid;
    int envSampled[SBR_MAX_ENVELOPES];

    float bE[64][SBR_QMF_BANDS_64];  /* Scratch buffer to keep 16 KB off stack */
} SignalAnalysisChannel;

/* Raw SBR measurements for one input frame.  These are deliberately slot
 * energies, rather than PCM or QMF samples: grid selection happens two frames
 * later, when enough context exists, and can then re-bin them without an
 * allocation or a second QMF transform. */
typedef struct SbrAnalysisFrame {
    int numSlots;
    int sampled[32];
    float totalE[MAX_CHANNELS][32];
    float bandE[MAX_CHANNELS][32][SBR_QMF_BANDS_64];
} SbrAnalysisFrame;

typedef struct SignalAnalysis {
    int numSlots;
    int sampled;

    /* Block switching needs a decision for every core channel, so pass 1 runs
       full width. */
    SignalAnalysisChannel ch[MAX_CHANNELS];

} SignalAnalysis;

void SbrAnalyzeFrame(SbrAnalysisFrame *frame, float *fullPtrs[], int nch,
                     const bool *isLfe, int numSamples, struct SBRInfo *sbr);
void SbrFinalizeFrame(SignalAnalysis *sa, const SbrAnalysisFrame *frame,
                      const SbrAnalysisFrame *next, const SbrAnalysisFrame *ahead,
                      int nch, const bool *isLfe, const int *coreBlockType,
                      struct SBRInfo *sbr, struct SbrFrameData *fd);

#ifdef __cplusplus
}
#endif

#endif

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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "sbr.h"
#include "sbr_tables.h"
#include "util.h"
#include "sbr_analysis.h"
#include "resample.h"
#include "bitstream.h"
#include "sbr_internal.h"
#include "faac_internal.h"
#include "channels.h"
#include "stats.h"

static int compute_kx(int sampleRate, int bs_start_freq)
{
    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int start_min = ((temp << 7) + (sampleRate >> 1)) / sampleRate;
    int row = (sampleRate <= 16000) ? 0 : (sampleRate <= 22050) ? 1 : (sampleRate <= 24000) ? 2 : (sampleRate <= 32000) ? 3 : (sampleRate <= 64000) ? 4 : 5;
    return clamp_int(start_min + sbr_offset[row][bs_start_freq & 15], 1, 63);
}

static int cmp_int16(const void *a, const void *b) { return (int)(*(const short *)a) - (int)(*(const short *)b); }
static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static int compute_k2(int sampleRate, int bs_stop_freq)
{
    if (bs_stop_freq == 14 || bs_stop_freq == 15) return 64;
    int temp = (sampleRate < 32000) ? 3000 : (sampleRate < 64000) ? 4000 : 5000;
    int stop_min = ((temp << 8) + (sampleRate >> 1)) / sampleRate;
    int k2;
    if (bs_stop_freq < 14) {
        short stop_dk[13];
        float prod = (float)stop_min;
        int prev = stop_min;
        float base = powf(64.0f / (float)stop_min, (float)(1.0f / 13.0f));
        for (int i = 0; i < 12; i++) {
            prod *= base;
            int present = (int)lrintf(prod);
            stop_dk[i] = (short)(present - prev);
            prev = present;
        }
        stop_dk[12] = (short)(64 - prev);
        qsort(stop_dk, 13, sizeof(short), cmp_int16);
        k2 = stop_min;
        for (int i = 0; i < bs_stop_freq; i++) k2 += stop_dk[i];
    } else {
        k2 = 64;
    }

    return k2;
}

static int max_sbr_span(int sampleRate)
{
    return (sampleRate <= 32000) ? 48 : (sampleRate <= 44100) ? 35 : 32;
}

static int pick_stop_freq(int sampleRate, int kx, int targetHz)
{
    int best = SBR_STOP_FREQ_MIN;
    for (int sf = SBR_STOP_FREQ_MIN; sf <= SBR_STOP_FREQ_MAX; sf++) {
        int k2 = compute_k2(sampleRate, sf);
        if (k2 - kx > max_sbr_span(sampleRate)) break;
        best = sf;
        if ((long)k2 * sampleRate / (2 * SBR_QMF_BANDS_64) >= targetHz) break;
    }
    return best;
}

static int build_freq_table(SBRInfo *sbr)
{
    int kx = sbr->kx, k2 = sbr->k2;
    int *edges = sbr->bandEdges;
    int n_master;

    int prev = kx;
    int bands_per_octave = 14 - 2 * sbr->bs_freq_scale;
    n_master = 2 * (int)(bands_per_octave * log2f((float)k2 / (float)kx) / 2.0f + 0.5f);
    n_master = clamp_int(n_master, 1, SBR_MAX_BANDS);
    for (int k = 0; k < n_master; k++) {
        int edge = (int)(kx * powf((float)k2 / (float)kx, (float)(k + 1) / (float)n_master) + 0.5f);
        edges[1 + k] = edge - prev;
        prev = edge;
    }
    qsort(edges + 1, n_master, sizeof(int), cmp_int);
    edges[0] = kx;
    for (int k = 1; k <= n_master; k++) edges[k] += edges[k - 1];
    sbr->numBands = n_master;

    int n_low = (n_master + 1) >> 1;
    sbr->numBandsLow = n_low;
    int odd = n_master & 1;
    sbr->bandEdgesLow[0] = edges[0];
    for (int b = 1; b <= n_low; b++)
        sbr->bandEdgesLow[b] = edges[2 * b - odd];

    return n_master;
}

SBRInfo *SbrInit(int channels, int sampleRate, unsigned long bitRate)
{
    SBRInfo *sbr = (SBRInfo *)AllocMemory(sizeof(SBRInfo));
    if (!sbr) return NULL;
    SetMemory(sbr, 0, sizeof(SBRInfo));
    sbr->sbrPresent = 1;
    sbr->numChannels = channels;
    sbr->sampleRate = sampleRate;

    for (int m = 0; m < SBR_QMF_BANDS_64; m++) {
        sbr->twidCos[m] = (float)cos(M_PI_DOUBLE * m / 64.0);
        sbr->twidSin[m] = (float)sin(M_PI_DOUBLE * m / 64.0);
        sbr->oddCos[m] = (float)cos(M_PI_DOUBLE * (2 * m + 1) / 128.0);
        sbr->oddSin[m] = (float)sin(M_PI_DOUBLE * (2 * m + 1) / 128.0);
    }
    SbrUpdate(sbr, bitRate);
    return sbr;
}

void SbrUpdate(SBRInfo *sbr, unsigned long bitRate)
{
    sbr->bitRate = bitRate;
    int sampleRate = sbr->sampleRate;
    unsigned long rate_per_ch = bitRate / sbr->numChannels;
    sbr->numEnvFixFix = (rate_per_ch >= SBR_TWO_ENV_BITRATE_BPS) ? 2 : 1;
    sbr->bs_start_freq = 15;
    sbr->bs_freq_scale = (rate_per_ch >= SBR_FREQ_SCALE_FINE_BPS) ? 1
                       : (rate_per_ch >= SBR_FREQ_SCALE_COARSE_BPS) ? 3 : 2;
    sbr->bs_alter_scale = 0;
    sbr->bs_freq_res = 1;
    sbr->bs_xover_band = 0;
    sbr->kx = compute_kx(sampleRate, sbr->bs_start_freq);

    sbr->bs_stop_freq = pick_stop_freq(sampleRate, sbr->kx, SBR_STOP_FREQ_TARGET_HZ);
    sbr->k2 = compute_k2(sampleRate, sbr->bs_stop_freq);

    build_freq_table(sbr);
}

void SbrEnd(SBRInfo *sbr)
{
    if (!sbr) return;
    FreeMemory(sbr);
}

static void sbr_frame_silence(SbrFrameData *fd)
{
    SetMemory(fd, 0, sizeof(*fd));
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        SbrGrid *grid = &fd->ch[ch].grid;
        fd->ch[ch].eff_amp_res = 0;
        grid->frameClass = SBR_FRAME_CLASS_FIXFIX;
        grid->numEnvelopes = 1;
        grid->tEnv[0] = 0;
        grid->tEnv[1] = SBR_NUM_TIME_SLOTS;
        grid->freqRes[0] = 1;
    }
}

SBRContext *SbrContextInit(int channels)
{
    SBRContext *sbrCtx = (SBRContext *)AllocMemory(sizeof(SBRContext));
    if (sbrCtx) {
        SetMemory(sbrCtx, 0, sizeof(SBRContext));
        sbrCtx->resampler = ResampleInit(channels);
        if (!sbrCtx->resampler) {
            FreeMemory(sbrCtx);
            return NULL;
        }
        for (int i = 0; i < SBR_FRAME_FIFO; i++)
            sbr_frame_silence(&sbrCtx->frameFIFO[i]);
    }
    return sbrCtx;
}

void SbrContextEnd(SBRContext *sbrCtx)
{
    if (!sbrCtx) return;
    if (sbrCtx->sbrInfo) {
        SbrEnd(sbrCtx->sbrInfo);
    }
    if (sbrCtx->resampler) {
        ResampleEnd(sbrCtx->resampler);
    }
    FreeMemory(sbrCtx);
}

int SbrContextGetASC(SBRContext *sbrCtx, int coreSRIdx, int channels, unsigned char** ppBuffer, unsigned long* pSize)
{
    const int signalPS = (channels == 1);
    const unsigned long size = signalPS ? 7 : 5;

    unsigned char *buf = (unsigned char *)malloc(size);
    if (buf == NULL) return -3;

    BitStream bs;
    InitBitStream(&bs, buf, (uint32_t)size);

    BitAccumulator a;
    AccumBegin(&a, &bs);
    AccumPutBits(&a, LOW,       5);
    AccumPutBits(&a, coreSRIdx, 4);
    AccumPutBits(&a, GetChannelConfig(channels), 4);
    AccumPutBits(&a, 0,         3);
    AccumPutBits(&a, 0x2b7,    11);
    AccumPutBits(&a, HE_V1,     5);
    AccumPutBits(&a, 1,         1);
    AccumPutBits(&a, sbrCtx->fullSampleRateIdx, 4);
    if (signalPS) {
        AccumPutBits(&a, 0x548, 11);
        AccumPutBits(&a, 0,      1);
    }
    AccumEnd(&a);

    *ppBuffer = buf;
    *pSize = size;
    return 0;
}

unsigned int SbrContextGetXOverBandwidth(SBRContext *sbrCtx)
{
    if (!sbrCtx || !sbrCtx->sbrInfo) return 0;
    return (unsigned int)((sbrCtx->sbrInfo->kx * sbrCtx->fullSampleRate) /
                           (2 * SBR_QMF_BANDS_64));
}

void SbrContextUpdateConfig(SBRContext *sCtx, int channels, unsigned long bitrate)
{
    if (!sCtx) return;
    if (!sCtx->sbrInfo)
        sCtx->sbrInfo = SbrInit(channels, sCtx->fullSampleRate, bitrate);
    else
        SbrUpdate(sCtx->sbrInfo, bitrate);
}

void SbrContextProcessFrame(SBRContext *sCtx, int numChannels, const bool *isLfe, const int *coreBlockType, int realPerCh, int flushTick, float *inputFifo[MAX_CHANNELS], float *heHalfRate[MAX_CHANNELS])
{
    unsigned int channel;
    Resampler *rs = sCtx->resampler;
    float *fullPtrs[MAX_CHANNELS];
    (void)flushTick;

    sCtx->frameHead = (sCtx->frameHead + 1) % SBR_FRAME_FIFO;
    SbrFrameData *fd = &sCtx->frameFIFO[sCtx->frameHead];
    sCtx->sbrInfo->headerDecided = 0;

    {
        for (channel = 0; channel < (unsigned int)numChannels; channel++) {
            float *fullRate = rs->fullRate[channel];
            fullPtrs[channel] = fullRate;
            if (realPerCh)
                memcpy(fullRate, inputFifo[channel], realPerCh * sizeof(float));
            if (realPerCh < 2 * FRAME_LEN)
                memset(fullRate + realPerCh, 0, (2 * FRAME_LEN - realPerCh) * sizeof(float));
            heHalfRate[channel] = rs->halfRate[channel];
        }

        sCtx->analysisHead = (sCtx->analysisHead + 1) % (LOOKAHEAD_DEPTH + 1);
        SbrAnalyzeFrame(&sCtx->analysisFIFO[sCtx->analysisHead], fullPtrs, numChannels,
                        isLfe, 2 * FRAME_LEN, sCtx->sbrInfo);
        if (sCtx->analysisCount < LOOKAHEAD_DEPTH + 1) sCtx->analysisCount++;
        if (sCtx->analysisCount == LOOKAHEAD_DEPTH + 1) {
            int target = (sCtx->analysisHead + 1) % (LOOKAHEAD_DEPTH + 1);
            int next = (target + 1) % (LOOKAHEAD_DEPTH + 1);
            SbrFrameData *targetFd = &sCtx->frameFIFO[(sCtx->frameHead + SBR_FRAME_FIFO - LOOKAHEAD_DEPTH) % SBR_FRAME_FIFO];
            SbrFinalizeFrame(&sCtx->signalAnalysis, &sCtx->analysisFIFO[target],
                             &sCtx->analysisFIFO[next], &sCtx->analysisFIFO[sCtx->analysisHead],
                             numChannels, isLfe, coreBlockType, sCtx->sbrInfo, targetFd);
            SbrEncode(sCtx->sbrInfo, numChannels, isLfe, &sCtx->analysisFIFO[target], targetFd);
        } else {
            sbr_frame_silence(fd);
        }
        Resample(rs, 2 * FRAME_LEN);
    }
}

void SbrContextRestoreRate(SBRContext *sCtx, unsigned long *sampleRate, unsigned int *sampleRateIdx, SR_INFO **srInfoPtr)
{
    if (sCtx && sCtx->fullSampleRate > 0) {
        *sampleRate    = sCtx->fullSampleRate;
        *sampleRateIdx = sCtx->fullSampleRateIdx;
        *srInfoPtr     = &srInfo[*sampleRateIdx];
        sCtx->fullSampleRate = 0;
    }
}

unsigned long SbrContextGetFullRate(SBRContext *sCtx, unsigned long defaultRate)
{
    return (sCtx && sCtx->fullSampleRate) ? sCtx->fullSampleRate : defaultRate;
}

void SbrContextResolveRate(SBRContext *sCtx, unsigned long *sampleRate, unsigned int *sampleRateIdx, SR_INFO **srInfoPtr)
{
    if (sCtx->fullSampleRate == 0) {
        sCtx->fullSampleRate     = *sampleRate;
        sCtx->fullSampleRateIdx  = *sampleRateIdx;
        *sampleRate         = *sampleRate / 2;
        *sampleRateIdx      = GetSRIndex(*sampleRate);
        *srInfoPtr          = &srInfo[*sampleRateIdx];
    }
}

int SbrContextIsPresent(SBRContext *sCtx)
{
    return (sCtx && sCtx->sbrInfo) ? 1 : 0;
}

#define FAST_LOG2_A         1.3424f
#define FAST_LOG2_B         0.3427f
#define FAST_LOG2_MANT_NORM (1.0f / (1 << 23))
static inline float fast_log2(float x)
{
    union { float f; int32_t i; } vx;
    vx.f = (float)x;
    int32_t exp = (vx.i >> 23) & 0xFF;
    float m = (float)(vx.i & 0x7FFFFF) * FAST_LOG2_MANT_NORM;
    return (float)(exp - 127) + (float)(m * (FAST_LOG2_A - FAST_LOG2_B * m));
}

void SbrQmfAnalysis(SBRInfo *sbr, const float * restrict ovl_pos, float * restrict energy, int kx, int k2)
{
    float x[128], y[128];
    float * restrict xr = x, * restrict xi = x + 64;
    const float * restrict yr = y, * restrict yi = y + 64;
    const sbrfloat * restrict p0 = qmf_c;
    for (int m = 0; m < 64; m++) {
        int n0 = 2 * m;
        float a = p0[0]   * ovl_pos[639 - n0]
                    + p0[128] * ovl_pos[511 - n0]
                    + p0[256] * ovl_pos[383 - n0]
                    + p0[384] * ovl_pos[255 - n0]
                    + p0[512] * ovl_pos[127 - n0];
        float b = p0[1]   * ovl_pos[638 - n0]
                    + p0[129] * ovl_pos[510 - n0]
                    + p0[257] * ovl_pos[382 - n0]
                    + p0[385] * ovl_pos[254 - n0]
                    + p0[513] * ovl_pos[126 - n0];
        xr[m] = a * sbr->twidCos[m] - b * sbr->twidSin[m];
        xi[m] = -(a * sbr->twidSin[m] + b * sbr->twidCos[m]);
        p0 += 2;
    }
    fft(x, y, FFT_LOGM_SHORT);
    for (int k = kx; k < k2; k++) {
        int kr = 63 - k;
        float Ar = 0.5f * (yr[k] + yr[kr]);
        float Ai = 0.5f * (yi[kr] - yi[k]);
        float Br = -0.5f * (yi[k] + yi[kr]);
        float Bi = 0.5f * (yr[kr] - yr[k]);
        float wr = sbr->oddCos[k];
        float wi = sbr->oddSin[k];
        float Sr = Ar + wr * Br - wi * Bi;
        float Si = Ai + wr * Bi + wi * Br;
        energy[k] += Sr * Sr + Si * Si;
    }
}

static void sbr_quantize_envelopes(const SBRInfo *sbr, int nch, const bool *isLfe,
                                   const SbrAnalysisFrame *frame, SbrFrameData *fd)
{
    for (int ch = 0; ch < nch; ch++) {
        if (isLfe[ch]) continue;
        const SbrGrid *grid = &fd->ch[ch].grid;
        int eff_amp_res = fd->ch[ch].eff_amp_res;
        int max_d = eff_amp_res ? 30 : 60;
        for (int e = 0; e < grid->numEnvelopes; e++) {
            int nb = sbr_env_bands(sbr, grid, e);
            const int *edges = sbr_env_edges(sbr, grid, e);
            int start = grid->tEnv[e] * frame->numSlots / SBR_NUM_TIME_SLOTS;
            int end = grid->tEnv[e + 1] * frame->numSlots / SBR_NUM_TIME_SLOTS;
            start = clamp_int(start, 0, frame->numSlots);
            end = clamp_int(end, 0, frame->numSlots);
            int e_slots = 0;
            for (int slot = start; slot < end; slot++) e_slots += frame->sampled[slot];
            for (int b = 0; b < nb; b++) {
                int k_lo = edges[b], k_hi = edges[b+1];
                float E = 0.0f;
                for (int slot = start; slot < end; slot++) {
                    if (!frame->sampled[slot]) continue;
                    for (int k = k_lo; k < k_hi; k++) E += frame->bandE[ch][slot][k];
                }
                E /= (float)((e_slots ? e_slots : 1) * (k_hi - k_lo));
                float factor = eff_amp_res ? 1.0f : 2.0f;
                int level = lrintf(factor * (fast_log2(E + SBR_LOG_ENERGY_FLOOR) - SBR_ENV_LEVEL_LOG2_OFFSET));
                int raw_level = clamp_int(level, 0, eff_amp_res ? 63 : 127);
                if (b > 0) {
                    raw_level = clamp_int(raw_level, fd->ch[ch].envData[e][b - 1] - max_d, fd->ch[ch].envData[e][b - 1] + max_d);
                }
                fd->ch[ch].envData[e][b] = raw_level;
            }
        }
    }
}

void SbrEncode(SBRInfo *sbr, int numChannels, const bool *isLfe,
               const SbrAnalysisFrame *frame, SbrFrameData *fd)
{
    sbr_quantize_envelopes(sbr, numChannels, isLfe, frame, fd);

#ifdef FAAC_STATS
    g_faacStats.sbrFrames++;
    if (fd->ch[0].grid.frameClass != SBR_FRAME_CLASS_FIXFIX) {
        g_faacStats.sbrTransientFrames++;
    }
    for (int ch = 0; ch < numChannels; ch++) {
        if (isLfe[ch]) continue;
        g_faacStats.sbrInvfSum += SBR_INVF_MODE;
        g_faacStats.sbrInvfCount++;
    }
#endif
}

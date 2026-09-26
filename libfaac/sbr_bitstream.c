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

#include <assert.h>
#include <limits.h>
#include <string.h>

#include "sbr.h"
#include "sbr_internal.h"
#include "sbr_tables.h"
#include "bitstream.h"
#include "channels.h"
#include "util.h"
#include "faac_internal.h"

typedef struct {
    unsigned envDt[2];
    unsigned noiseDt[2];
    int coupled, noiseLinked, linked, bits;
} SbrDecision;

static void emit_sbr_header(const SBRInfo *sbr, BitStream *bs)
{
    /* ISO 14496-3:2009 §4.6.18.5 sbr_header() (21 bits) */
    PutBit(bs, SBR_AMP_RES, 1); PutBit(bs, sbr->bs_start_freq, 4);
    PutBit(bs, sbr->bs_stop_freq, 4); PutBit(bs, sbr->bs_xover_band, 3);
    PutBit(bs, 0, 2); PutBit(bs, 1, 1); PutBit(bs, 0, 1);
    PutBit(bs, sbr->bs_freq_scale, 2); PutBit(bs, sbr->bs_alter_scale, 1);
    PutBit(bs, 0, 2);
}

/* Width of the transient pointer field, indexed by number of envelopes. */
static const int sbr_ceil_log2[] = { 0, 1, 2, 2, 3, 3 };

static int sbr_grid_bits(const SbrFrameData *fd)
{
    int num_env = fd->numEnvelopes;
    int bits = 2;
    if (fd->frameClass == SBR_FRAME_CLASS_VARFIX) {
        bits += 4 + 2 * (num_env - 1) + sbr_ceil_log2[num_env] + num_env;
    } else bits += 3;
    return bits;
}

static void emit_sbr_grid(const SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs)
{
    int num_env = fd->numEnvelopes;
    PutBit(bs, fd->frameClass, 2);
    if (fd->frameClass == SBR_FRAME_CLASS_VARFIX) {
        PutBit(bs, fd->tEnv[0], 2); PutBit(bs, num_env - 1, 2);
        for (int i = 0; i < num_env - 1; i++) PutBit(bs, (fd->tEnv[i + 1] - fd->tEnv[i] - 2) / 2, 2);
        PutBit(bs, fd->bsPointer, sbr_ceil_log2[num_env]);
        for (int i = 0; i < num_env; i++) PutBit(bs, sbr->bs_freq_res, 1);
    } else { PutBit(bs, num_env > 1, 2); PutBit(bs, sbr->bs_freq_res, 1); }
}

/* bs_df_env from the choices the sizing pass cached; bs_df_noise whenever a
 * reference exists (see write_sbr_noise). */
static void emit_sbr_dtdf(const SbrFrameData *fd, const SbrDecision *d, int ch, BitStream *bs)
{
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    for (int e = 0; e < fd->numEnvelopes; e++) PutBit(bs, (d->envDt[ch] >> e) & 1, 1);
    for (int q = 0; q < n_q; q++) PutBit(bs, (d->noiseDt[ch] >> q) & 1, 1);
}

static void emit_sbr_invf(BitStream *bs)
{
    PutBit(bs, SBR_INVF_MODE, 2);
}

typedef struct { const SBRHuffEntry *f, *t; int lav, start; } SbrDeltaBook;

static const SbrDeltaBook sbr_env_books[2][2] = {
    { { f_huff_env_1_5dB, t_huff_env_1_5dB, F_HUFF_ENV_1_5DB_OFFSET, 7 }, { f_huff_env_3_0dB, t_huff_env_3_0dB, F_HUFF_ENV_3_0DB_OFFSET, 6 } },
    { { f_huff_env_bal_1_5dB, t_huff_env_bal_1_5dB, F_HUFF_ENV_BAL_1_5DB_OFFSET, 6 }, { f_huff_env_bal_3_0dB, t_huff_env_bal_3_0dB, F_HUFF_ENV_BAL_3_0DB_OFFSET, 5 } },
};

/* NULL tables describe the fixed-width noise floor codes. */
static int code_deltas(const int *values, const int *previous, int count, int time,
                       const SbrDeltaBook *book, BitStream *bs)
{
    const SBRHuffEntry *tab = time ? book->t : book->f;
    int lav = time ? T_HUFF_ENV_LAV : book->lav;
    int bits = time ? 0 : book->start, i = time ? 0 : 1;
    if (!time && bs) PutBit(bs, values[0], book->start);
    for (; i < count; i++) {
        int d = values[i] - (time ? previous[i] : values[i - 1]);
        if (!tab) { if (bs) PutBit(bs, 0, 1); return bits + 1; }
        if (d < -lav || d > lav) return INT_MAX;
        bits += tab[d + lav] & 31;
        if (bs) PutBit(bs, tab[d + lav] >> 5, tab[d + lav] & 31);
    }
    return bits;
}

/* Each envelope takes the cheaper of frequency and time deltas; a frame's
 * first envelope refers to the channel's last written one, when linked. The
 * sizing pass (write false) makes the choice and caches it for the write pass,
 * which always follows it with the same frame. */
static int choose_sbr_envelope(const SBRInfo *sbr, const SbrFrameData *fd, const int (*env)[SBR_MAX_BANDS],
                               int bal, int linked, int ch, unsigned *chosen)
{
    int bits = 0;

    for (int e = 0; e < fd->numEnvelopes; e++) {
        const int *ref = e ? env[e - 1] : sbr->ch[ch].ref[~sbr->frameCount & 1].env;
        int best = INT_MAX;
        for (int t = 0; t <= (e || linked); t++) {
            int n = code_deltas(env[e], ref, sbr_env_bands(sbr, fd), t, &sbr_env_books[bal][fd->eff_amp_res], NULL);
            if (n < best) { best = n; *chosen = (*chosen & ~(1u << e)) | ((unsigned)t << e); }
        }
        if (best == INT_MAX) { bits = INT_MAX; break; }
        bits += best;
    }
    return bits;
}

static void emit_sbr_envelope(const SBRInfo *sbr, const SbrFrameData *fd, const int (*env)[SBR_MAX_BANDS],
                              int bal, int ch, unsigned dt, BitStream *bs)
{
    int nb = sbr_env_bands(sbr, fd);
    for (int e = 0; e < fd->numEnvelopes; e++) {
        const int *ref = e ? env[e - 1] : sbr->ch[ch].ref[~sbr->frameCount & 1].env;
        code_deltas(env[e], ref, nb, dt >> e & 1, &sbr_env_books[bal][fd->eff_amp_res], bs);
    }
}

/* One noise band at a constant level: 5-bit absolute (the level, or for a
 * coupled balance channel the centre 6), or the time delta 0, whose code is
 * the single bit 0 -- always the cheaper once a reference exists. */
static void emit_sbr_noise(const SbrFrameData *fd, int value, unsigned dt, BitStream *bs)
{
    static const SbrDeltaBook noise = { NULL, NULL, 0, 5 };
    int n_q = fd->numEnvelopes > 1 ? 2 : 1;
    for (int q = 0; q < n_q; q++) code_deltas(&value, &value, 1, dt >> q & 1, &noise, bs);
}

/* Coupled rendition of a pair from the channels' own levels: the level of
 * (L + R) / 2, and the balance L - R at half resolution around its centre. */
static void couple_envelopes(SBRInfo *sbr, const SbrFrameData *fd, int ch0)
{
    int amp = fd->eff_amp_res;
    int pan = amp ? 12 : 24;
    int nb = sbr_env_bands(sbr, fd);

    for (int e = 0; e < fd->numEnvelopes; e++) {
        const int *restrict left = fd->ch[ch0].envData[e], *restrict right = fd->ch[ch0 + 1].envData[e];
        int *restrict level = sbr->cplEnv[0][e], *restrict balance = sbr->cplEnv[1][e];
        for (int b = 0; b < nb; b++) {
            int l = left[b], r = right[b];
            int d = l > r ? l - r : r - l;
            /* the louder side plus log2((1 + 2^-d) / 2) in level steps, rounded */
            level[b] = (l > r ? l : r) - (d >= 2) - (!amp && d >= 5);
            balance[b] = (clamp_int(pan + l - r, 0, 2 * pan) + 1) >> 1;
        }
    }
}

static int choose_sbr_channels(SBRInfo *sbr, const SbrFrameData *fd, int nch, int ch0,
                               int sendHeader, SbrDecision *d)
{
    int bits = (nch == 2) ? 5 : 3; /* data_extra, coupling, add_harmonic flags, ext data flag */
    /* Time deltas need the previous frame's envelope in the same layout;
     * header frames stay self-contained so a decoder can start there. */
    const SBRChannel *sc0 = &sbr->ch[ch0];
    const SbrEnvRef *prev = &sc0->ref[~sbr->frameCount & 1];
    d->noiseLinked = !sendHeader && prev->nb && prev->coupled == d->coupled;
    d->linked = d->noiseLinked && prev->nb == sbr_env_bands(sbr, fd) && prev->ampRes == fd->eff_amp_res;
    const int (*env[2])[SBR_MAX_BANDS];
    int ngrid = d->coupled ? 1 : nch;

    for (int ch = 0; ch < nch; ch++)
        env[ch] = d->coupled ? (const int (*)[SBR_MAX_BANDS])sbr->cplEnv[ch]
                          : (const int (*)[SBR_MAX_BANDS])fd->ch[ch0 + ch].envData;

    for (int ch = 0; ch < ngrid; ch++)
        bits += sbr_grid_bits(fd);
    for (int ch = 0; ch < nch; ch++)
        bits += fd->numEnvelopes + (fd->numEnvelopes > 1 ? 2 : 1);
    for (int ch = 0; ch < ngrid; ch++)
        bits += 2;
    /* Coupled: envelope and noise per channel in turn; otherwise all
     * envelopes, then all noise floors. */
    for (int k = 0; k < 2 * nch; k++) {
        int ch = d->coupled ? k >> 1 : k % nch;
        if (d->coupled ? k & 1 : k >= nch) {
            int n_q = fd->numEnvelopes > 1 ? 2 : 1;
            d->noiseDt[ch] = 0;
            for (int q = 0; q < n_q; q++) d->noiseDt[ch] |= (unsigned)(q || d->noiseLinked) << q;
            for (int q = 0; q < n_q; q++) bits += (d->noiseDt[ch] >> q & 1) ? 1 : 5;
        } else {
            d->envDt[ch] = 0;
            int n = choose_sbr_envelope(sbr, fd, env[ch], d->coupled && ch, d->linked, ch0 + ch, &d->envDt[ch]);
            if (n == INT_MAX) return INT_MAX;
            bits += n;
        }
    }
    return bits;
}

static int emit_sbr_channels(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int nch, int ch0, const SbrDecision *d)
{
    int ngrid = d->coupled ? 1 : nch;
    const int (*env[2])[SBR_MAX_BANDS];
    PutBit(bs, d->coupled, nch);
    for (int ch = 0; ch < nch; ch++) env[ch] = d->coupled ? (const int (*)[SBR_MAX_BANDS])sbr->cplEnv[ch] : (const int (*)[SBR_MAX_BANDS])fd->ch[ch0 + ch].envData;
    for (int ch = 0; ch < ngrid; ch++) emit_sbr_grid(sbr, fd, bs);
    for (int ch = 0; ch < nch; ch++) emit_sbr_dtdf(fd, d, ch, bs);
    for (int ch = 0; ch < ngrid; ch++) emit_sbr_invf(bs);
    for (int k = 0; k < 2 * nch; k++) {
        int ch = d->coupled ? k >> 1 : k % nch;
        if (d->coupled ? k & 1 : k >= nch) emit_sbr_noise(fd, d->coupled && ch ? 6 : SBR_NOISE_LEVEL_DEFAULT, d->noiseDt[ch], bs);
        else emit_sbr_envelope(sbr, fd, env[ch], d->coupled && ch, ch0 + ch, d->envDt[ch], bs);
    }
    PutBit(bs, 0, nch + 1);
    for (int ch = 0, nb = sbr_env_bands(sbr, fd); ch < nch; ch++) {
        SbrEnvRef *cur = &sbr->ch[ch0 + ch].ref[sbr->frameCount & 1];
        memcpy(cur->env, env[ch][fd->numEnvelopes - 1], nb * sizeof(int)); cur->nb = nb; cur->ampRes = fd->eff_amp_res; cur->coupled = d->coupled;
    }
    return d->bits;
}

/* A pair is coupled when that codes smaller. The sizing pass costs both
 * layouts and ends on the chosen one, so the channel caches match it. */
static int choose_sbr_data(SBRInfo *sbr, const SbrFrameData *fd, int id_aac, int ch0, int sendHeader, SbrDecision *d)
{
    int nch = (id_aac == ID_CPE) ? 2 : 1;
    if (nch != 2) {
        d->coupled = 0;
        d->bits = choose_sbr_channels(sbr, fd, nch, ch0, sendHeader, d);
        return d->bits;
    }
    /* Keep both decisions: the old third pass only rebuilt the coupled one. */
    SbrDecision coupled = { .coupled = 1 }, independent = { .coupled = 0 };
    couple_envelopes(sbr, fd, ch0);
    coupled.bits = choose_sbr_channels(sbr, fd, nch, ch0, sendHeader, &coupled);
    independent.bits = choose_sbr_channels(sbr, fd, nch, ch0, sendHeader, &independent);
    *d = independent.bits >= coupled.bits ? coupled : independent;
    return d->bits;
}

/* Emit the full extension_payload body for EXT_SBR_DATA: the 4-bit extension
 * type, the 1-bit header flag, the optional header, and the channel data. */
static int choose_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, int id_aac, int ch0, int sendHeader, SbrDecision *d)
{
    int bits = 5;
    if (sendHeader) bits += 21;
    bits += choose_sbr_data(sbr, fd, id_aac, ch0, sendHeader, d);
    return bits;
}

static int emit_sbr_payload(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0, int sendHeader, const SbrDecision *d)
{
    PutBit(bs, (SBR_EXT_TYPE_SBR << 1) | (sendHeader & 1), 5);
    if (sendHeader) emit_sbr_header(sbr, bs);
    return 5 + (sendHeader ? 21 : 0) + emit_sbr_channels(sbr, fd, bs, id_aac == ID_CPE ? 2 : 1, ch0, d);
}

static int SbrWrite(SBRInfo *sbr, const SbrFrameData *fd, BitStream *bs, int id_aac, int ch0)
{
    if (!sbr || !sbr->sbrPresent) return 0;

    int sendHeader = sbr->sendHeaderThisFrame;

    /* The fill_element's cnt field must precede the payload in the bitstream,
     * so its size is needed before anything is written. Re-deriving it with a
     * dry (write=false) pass is cheap -- a few hundred fixed-width/Huffman
     * fields, not a hot loop -- re-deriving it from sbr's already-quantized
     * envelope/noise data. */
    SbrDecision decision = {0};
    int payloadBits = choose_sbr_payload(sbr, fd, id_aac, ch0, sendHeader, &decision);
    int fillBytes = (payloadBits + 7) / 8;
    int padBits = fillBytes * 8 - payloadBits;

    /* The fill_element count escapes through an 8-bit field, so a single
     * extension_payload tops out at 15 + 255 - 1 = 269 bytes. A larger SBR
     * payload would silently truncate esc_count and corrupt the boundary. */
    assert(fillBytes <= 14 + 255);

    int totalBits;
    /* fill_element(): id, then 4-bit count with optional 8-bit escape.
     * The decoder reconstructs cnt = 15 + esc_count - 1, hence
     * esc_count = N - 14. */
    PutBit(bs, ID_FIL, 3);
    if (fillBytes < 15) {
        PutBit(bs, fillBytes, 4);
        totalBits = 7;
    } else {
        PutBit(bs, 15, 4);
        PutBit(bs, fillBytes - 14, 8);
        totalBits = 15;
    }
    uint32_t payloadStart = bs->currentBit;
    int emittedBits = emit_sbr_payload(sbr, fd, bs, id_aac, ch0, sendHeader, &decision);
    assert(payloadBits == emittedBits);
    assert(bs->currentBit - payloadStart == (uint32_t)payloadBits);
    if (padBits > 0) PutBit(bs, 0, padBits);

    return totalBits + payloadBits + padBits;
}

int SbrContextGetBits(SBRContext *sCtx, BitStream *bs, const AACElement *elem, int aacObjectType)
{
    if (aacObjectType == HE_V1 && sCtx && elem->type != ID_LFE) {
        if (sCtx->sbrInfo) {
            int id_aac = (elem->type == ID_CPE) ? ID_CPE : ID_SCE;
            /* One step past the newest slot is the oldest: the payload whose
             * audio this access unit's core carries. See SBR_FRAME_FIFO. */
            const SbrFrameData *fd = &sCtx->frameFIFO[(sCtx->frameHead + 1) % SBR_FRAME_FIFO];
            SBRInfo *sbr = sCtx->sbrInfo;
            if (!sbr->headerDecided) {
                sbr->sendHeaderThisFrame = (sbr->frameCount++ % SBR_HEADER_PERIOD == 0);
                sbr->headerDecided = 1;
            }
            return SbrWrite(sbr, fd, bs, id_aac, elem->channels[0]);
        }
    }
    return 0;
}

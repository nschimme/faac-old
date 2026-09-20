/*
 * FAAC - Freeware Advanced Audio Coder
 * SBR tables reproduced from ISO/IEC 14496-3 (non-copyrightable facts)
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

/* SBR tables: QMF prototype filter, frequency-band offsets, Huffman tables.
 * All values are normative data from ISO/IEC 14496-3:2005. */

#ifndef SBR_TABLES_H
#define SBR_TABLES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef float sbrfloat;

typedef struct {
    uint32_t code : 24;
    uint32_t len  : 8;
} SBRHuffEntry;

#define F_HUFF_ENV_1_5DB_OFFSET  60
#define F_HUFF_ENV_1_5DB_NSYMS   121
#define F_HUFF_ENV_3_0DB_OFFSET  31
#define F_HUFF_ENV_3_0DB_NSYMS   63

extern const sbrfloat qmf_c[640];
extern const int8_t sbr_offset[6][16];
extern const SBRHuffEntry f_huff_env_1_5dB[F_HUFF_ENV_1_5DB_NSYMS];
extern const SBRHuffEntry f_huff_env_3_0dB[F_HUFF_ENV_3_0DB_NSYMS];

/* Decoder-side tables: time-delta, balance (coupling) and noise codebooks. */
#define T_HUFF_ENV_1_5DB_OFFSET  60
#define T_HUFF_ENV_1_5DB_NSYMS   121
extern const SBRHuffEntry t_huff_env_1_5dB[T_HUFF_ENV_1_5DB_NSYMS];
#define T_HUFF_ENV_BAL_1_5DB_OFFSET  24
#define T_HUFF_ENV_BAL_1_5DB_NSYMS   49
extern const SBRHuffEntry t_huff_env_bal_1_5dB[T_HUFF_ENV_BAL_1_5DB_NSYMS];
#define F_HUFF_ENV_BAL_1_5DB_OFFSET  24
#define F_HUFF_ENV_BAL_1_5DB_NSYMS   49
extern const SBRHuffEntry f_huff_env_bal_1_5dB[F_HUFF_ENV_BAL_1_5DB_NSYMS];
#define T_HUFF_ENV_3_0DB_OFFSET  31
#define T_HUFF_ENV_3_0DB_NSYMS   63
extern const SBRHuffEntry t_huff_env_3_0dB[T_HUFF_ENV_3_0DB_NSYMS];
#define T_HUFF_ENV_BAL_3_0DB_OFFSET  12
#define T_HUFF_ENV_BAL_3_0DB_NSYMS   25
extern const SBRHuffEntry t_huff_env_bal_3_0dB[T_HUFF_ENV_BAL_3_0DB_NSYMS];
#define F_HUFF_ENV_BAL_3_0DB_OFFSET  12
#define F_HUFF_ENV_BAL_3_0DB_NSYMS   25
extern const SBRHuffEntry f_huff_env_bal_3_0dB[F_HUFF_ENV_BAL_3_0DB_NSYMS];
#define T_HUFF_NOISE_3_0DB_OFFSET  31
#define T_HUFF_NOISE_3_0DB_NSYMS   63
extern const SBRHuffEntry t_huff_noise_3_0dB[T_HUFF_NOISE_3_0DB_NSYMS];
#define T_HUFF_NOISE_BAL_3_0DB_OFFSET  12
#define T_HUFF_NOISE_BAL_3_0DB_NSYMS   25
extern const SBRHuffEntry t_huff_noise_bal_3_0dB[T_HUFF_NOISE_BAL_3_0DB_NSYMS];

extern const float sbr_noise_table[512][2];

/* Parametric stereo codebooks (ISO/IEC 14496-3 §8.6). */
#define PS_HUFF_IID_DF_FINE_OFFSET 30
#define PS_HUFF_IID_DF_FINE_NSYMS  61
extern const SBRHuffEntry ps_huff_iid_df_fine[PS_HUFF_IID_DF_FINE_NSYMS];
#define PS_HUFF_IID_DT_FINE_OFFSET 30
#define PS_HUFF_IID_DT_FINE_NSYMS  61
extern const SBRHuffEntry ps_huff_iid_dt_fine[PS_HUFF_IID_DT_FINE_NSYMS];
#define PS_HUFF_IID_DF_OFFSET 14
#define PS_HUFF_IID_DF_NSYMS  29
extern const SBRHuffEntry ps_huff_iid_df[PS_HUFF_IID_DF_NSYMS];
#define PS_HUFF_IID_DT_OFFSET 14
#define PS_HUFF_IID_DT_NSYMS  29
extern const SBRHuffEntry ps_huff_iid_dt[PS_HUFF_IID_DT_NSYMS];
#define PS_HUFF_ICC_DF_OFFSET 7
#define PS_HUFF_ICC_DF_NSYMS  15
extern const SBRHuffEntry ps_huff_icc_df[PS_HUFF_ICC_DF_NSYMS];
#define PS_HUFF_ICC_DT_OFFSET 7
#define PS_HUFF_ICC_DT_NSYMS  15
extern const SBRHuffEntry ps_huff_icc_dt[PS_HUFF_ICC_DT_NSYMS];
#define PS_HUFF_IPD_DF_OFFSET 0
#define PS_HUFF_IPD_DF_NSYMS  8
extern const SBRHuffEntry ps_huff_ipd_df[PS_HUFF_IPD_DF_NSYMS];
#define PS_HUFF_IPD_DT_OFFSET 0
#define PS_HUFF_IPD_DT_NSYMS  8
extern const SBRHuffEntry ps_huff_ipd_dt[PS_HUFF_IPD_DT_NSYMS];
#define PS_HUFF_OPD_DF_OFFSET 0
#define PS_HUFF_OPD_DF_NSYMS  8
extern const SBRHuffEntry ps_huff_opd_df[PS_HUFF_OPD_DF_NSYMS];
#define PS_HUFF_OPD_DT_OFFSET 0
#define PS_HUFF_OPD_DT_NSYMS  8
extern const SBRHuffEntry ps_huff_opd_dt[PS_HUFF_OPD_DT_NSYMS];

#ifdef __cplusplus
}
#endif

#endif /* SBR_TABLES_H */

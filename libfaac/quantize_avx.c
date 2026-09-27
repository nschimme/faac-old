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

#include <immintrin.h>
#include <math.h>
#include "quantize.h"

int quantize_avx2(const float * __restrict xr, int * __restrict xi, int len, float sfacfix)
{
    const __m256 sfac = _mm256_set1_ps(sfacfix);
    const __m256 magic = _mm256_set1_ps(MAGIC_NUMBER);
    const __m256 abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
    __m256i max_vec0 = _mm256_setzero_si256();
    __m256i max_vec1 = _mm256_setzero_si256();
    __m128i max128;
    int cnt = 0;

    // Main loop: 2x unrolled AVX2 processing 16 elements (4 quads) per iteration.
    // Overlaps multi-cycle sqrt latency and FMA execution ports.
    for (; cnt + 16 <= len; cnt += 16)
    {
        __m256 x0_orig = _mm256_loadu_ps(&xr[cnt]);
        __m256 x1_orig = _mm256_loadu_ps(&xr[cnt + 8]);

        __m256 x0 = _mm256_and_ps(_mm256_mul_ps(x0_orig, sfac), abs_mask);
        __m256 x1 = _mm256_and_ps(_mm256_mul_ps(x1_orig, sfac), abs_mask);

        // x^0.75 = sqrt(x * sqrt(x))
        x0 = _mm256_mul_ps(x0, _mm256_sqrt_ps(x0));
        x1 = _mm256_mul_ps(x1, _mm256_sqrt_ps(x1));

        x0 = _mm256_sqrt_ps(x0);
        x1 = _mm256_sqrt_ps(x1);

        x0 = _mm256_add_ps(x0, magic);
        x1 = _mm256_add_ps(x1, magic);

        __m256i q0 = _mm256_cvttps_epi32(x0);
        __m256i q1 = _mm256_cvttps_epi32(x1);

        max_vec0 = _mm256_max_epi32(max_vec0, q0);
        max_vec1 = _mm256_max_epi32(max_vec1, q1);

        __m256i mask0 = _mm256_srai_epi32(_mm256_castps_si256(x0_orig), 31);
        __m256i mask1 = _mm256_srai_epi32(_mm256_castps_si256(x1_orig), 31);

        q0 = _mm256_sub_epi32(_mm256_xor_si256(q0, mask0), mask0);
        q1 = _mm256_sub_epi32(_mm256_xor_si256(q1, mask1), mask1);

        _mm256_storeu_si256((__m256i*)&xi[cnt], q0);
        _mm256_storeu_si256((__m256i*)&xi[cnt + 8], q1);
    }

    max_vec0 = _mm256_max_epi32(max_vec0, max_vec1);

    // Single 8-element AVX2 iteration if 8 elements remain
    if (cnt + 8 <= len)
    {
        __m256 x_orig = _mm256_loadu_ps(&xr[cnt]);
        __m256 x = _mm256_and_ps(_mm256_mul_ps(x_orig, sfac), abs_mask);

        x = _mm256_mul_ps(x, _mm256_sqrt_ps(x));
        x = _mm256_sqrt_ps(x);
        x = _mm256_add_ps(x, magic);

        __m256i q = _mm256_cvttps_epi32(x);
        max_vec0 = _mm256_max_epi32(max_vec0, q);

        __m256i mask = _mm256_srai_epi32(_mm256_castps_si256(x_orig), 31);
        q = _mm256_sub_epi32(_mm256_xor_si256(q, mask), mask);

        _mm256_storeu_si256((__m256i*)&xi[cnt], q);
        cnt += 8;
    }

    // Fold 256-bit max_vec0 into 128-bit
    max128 = _mm_max_epi32(_mm256_castsi256_si128(max_vec0), _mm256_extracti128_si256(max_vec0, 1));

    // Tail processing for remaining 4 elements if total is odd number of quads
    if (cnt < len)
    {
        __m128 sfac128 = _mm256_castps256_ps128(sfac);
        __m128 magic128 = _mm256_castps256_ps128(magic);
        __m128 abs_mask128 = _mm256_castps256_ps128(abs_mask);

        __m128 x_orig = _mm_loadu_ps(&xr[cnt]);
        __m128 x = _mm_and_ps(_mm_mul_ps(x_orig, sfac128), abs_mask128);

        x = _mm_mul_ps(x, _mm_sqrt_ps(x));
        x = _mm_sqrt_ps(x);
        x = _mm_add_ps(x, magic128);

        __m128i q = _mm_cvttps_epi32(x);
        max128 = _mm_max_epi32(max128, q);

        __m128i mask = _mm_srai_epi32(_mm_castps_si128(x_orig), 31);
        q = _mm_sub_epi32(_mm_xor_si128(q, mask), mask);

        _mm_storeu_si128((__m128i*)&xi[cnt], q);
    }

    // Horizontal max reduction of 128-bit max128
    max128 = _mm_max_epi32(max128, _mm_shuffle_epi32(max128, _MM_SHUFFLE(1, 0, 3, 2)));
    max128 = _mm_max_epi32(max128, _mm_shuffle_epi32(max128, _MM_SHUFFLE(0, 1, 0, 1)));

    return _mm_cvtsi128_si32(max128);
}

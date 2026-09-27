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

int quantize_avx2(const float * __restrict xr, int * __restrict xi, int n4, float sfacfix)
{
    const __m256 sfac = _mm256_set1_ps(sfacfix);
    const __m256 magic = _mm256_set1_ps(MAGIC_NUMBER);
    const __m256 abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7FFFFFFF));
    __m256i max_vec = _mm256_setzero_si256();
    __m128i max128;
    int total = 4 * n4;
    int cnt;

    // Process 8 elements (2 quads) per iteration
    for (cnt = 0; cnt + 8 <= total; cnt += 8)
    {
        __m256 x_orig = _mm256_loadu_ps(&xr[cnt]);
        // Absolute value of the scaled input
        __m256 x = _mm256_and_ps(_mm256_mul_ps(x_orig, sfac), abs_mask);

        // Math: (x * sfac)^0.75 + magic
        // Logic: sqrt( (x*sfac) * sqrt(x*sfac) )
        x = _mm256_mul_ps(x, _mm256_sqrt_ps(x));
        x = _mm256_sqrt_ps(x);
        x = _mm256_add_ps(x, magic);

        // Convert to integer
        __m256i q = _mm256_cvttps_epi32(x);
        max_vec = _mm256_max_epi32(max_vec, q);

        // Bitwise Sign Fix: (val ^ mask) - mask, mask = sign bit of the input
        __m256i mask = _mm256_srai_epi32(_mm256_castps_si256(x_orig), 31);
        q = _mm256_sub_epi32(_mm256_xor_si256(q, mask), mask);

        _mm256_storeu_si256((__m256i*)&xi[cnt], q);
    }

    // Fold 256-bit max_vec into 128-bit
    max128 = _mm_max_epi32(_mm256_castsi256_si128(max_vec), _mm256_extracti128_si256(max_vec, 1));

    // Tail processing for remaining 4 elements if total is odd number of quads
    if (cnt < total)
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

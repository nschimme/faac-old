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

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#include <math.h>
#include "quantize.h"

int quantize_neon(const float * __restrict xr, int * __restrict xi, int n4, float sfacfix)
{
    const float32x4_t sfac = vdupq_n_f32(sfacfix);
    const float32x4_t magic = vdupq_n_f32(MAGIC_NUMBER);
    int32x4_t max_vec = vdupq_n_s32(0);
    int maxq_arr[4];
    int cnt, maxq;

    // Process 4 elements per iteration; band widths are multiples of 4
    for (cnt = 0; cnt < 4 * n4; cnt += 4)
    {
        float32x4_t x_orig = vld1q_f32(&xr[cnt]);
        // Absolute value of the scaled input
        float32x4_t x = vabsq_f32(vmulq_f32(x_orig, sfac));
        int32x4_t q, mask;

        // Math: (x * sfac)^0.75 + magic
        // Logic: sqrt( (x*sfac) * sqrt(x*sfac) )
        x = vmulq_f32(x, vsqrtq_f32(x));
        x = vsqrtq_f32(x);
        x = vaddq_f32(x, magic);

        // Convert to integer
        q = vcvtq_s32_f32(x);
        max_vec = vmaxq_s32(max_vec, q);

        // Bitwise Sign Fix: (val ^ mask) - mask, mask = sign bit of the input
        mask = vshrq_n_s32(vreinterpretq_s32_f32(x_orig), 31);
        q = vsubq_s32(veorq_s32(q, mask), mask);

        vst1q_s32(&xi[cnt], q);
    }

    vst1q_s32(maxq_arr, max_vec);
    maxq = maxq_arr[0];
    if (maxq_arr[1] > maxq) maxq = maxq_arr[1];
    if (maxq_arr[2] > maxq) maxq = maxq_arr[2];
    if (maxq_arr[3] > maxq) maxq = maxq_arr[3];

    return maxq;
}
#endif

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
    const float32x4_t t_sfac = vdupq_n_f32(sfacfix);
    const float32x4_t magic  = vdupq_n_f32(MAGIC_NUMBER);

    int32x4_t max0 = vdupq_n_s32(0);
    int32x4_t max1 = vdupq_n_s32(0);
    int32x4_t max2 = vdupq_n_s32(0);
    int32x4_t max3 = vdupq_n_s32(0);

    int cnt = 0;
    int limit = n4 * 4;

    // Process 16 elements (4 vectors) per iteration to hide vsqrt latency
    for (; cnt <= limit - 16; cnt += 16)
    {
        // 1. Load 4 vectors
        float32x4_t x0 = vld1q_f32(&xr[cnt]);
        float32x4_t x1 = vld1q_f32(&xr[cnt + 4]);
        float32x4_t x2 = vld1q_f32(&xr[cnt + 8]);
        float32x4_t x3 = vld1q_f32(&xr[cnt + 12]);

        // 2. Absolute value & scaling
        float32x4_t a0 = vabsq_f32(vmulq_f32(x0, t_sfac));
        float32x4_t a1 = vabsq_f32(vmulq_f32(x1, t_sfac));
        float32x4_t a2 = vabsq_f32(vmulq_f32(x2, t_sfac));
        float32x4_t a3 = vabsq_f32(vmulq_f32(x3, t_sfac));

        // 3. Math: x^0.75 = sqrt(x * sqrt(x))
        a0 = vmulq_f32(a0, vsqrtq_f32(a0));
        a1 = vmulq_f32(a1, vsqrtq_f32(a1));
        a2 = vmulq_f32(a2, vsqrtq_f32(a2));
        a3 = vmulq_f32(a3, vsqrtq_f32(a3));

        a0 = vsqrtq_f32(a0);
        a1 = vsqrtq_f32(a1);
        a2 = vsqrtq_f32(a2);
        a3 = vsqrtq_f32(a3);

        // 4. Convert to int with magic offset
        int32x4_t q0 = vcvtq_s32_f32(vaddq_f32(a0, magic));
        int32x4_t q1 = vcvtq_s32_f32(vaddq_f32(a1, magic));
        int32x4_t q2 = vcvtq_s32_f32(vaddq_f32(a2, magic));
        int32x4_t q3 = vcvtq_s32_f32(vaddq_f32(a3, magic));

        max0 = vmaxq_s32(max0, q0);
        max1 = vmaxq_s32(max1, q1);
        max2 = vmaxq_s32(max2, q2);
        max3 = vmaxq_s32(max3, q3);

        // 5. Restore sign
        int32x4_t mask0 = vshrq_n_s32(vreinterpretq_s32_f32(x0), 31);
        int32x4_t mask1 = vshrq_n_s32(vreinterpretq_s32_f32(x1), 31);
        int32x4_t mask2 = vshrq_n_s32(vreinterpretq_s32_f32(x2), 31);
        int32x4_t mask3 = vshrq_n_s32(vreinterpretq_s32_f32(x3), 31);

        q0 = vsubq_s32(veorq_s32(q0, mask0), mask0);
        q1 = vsubq_s32(veorq_s32(q1, mask1), mask1);
        q2 = vsubq_s32(veorq_s32(q2, mask2), mask2);
        q3 = vsubq_s32(veorq_s32(q3, mask3), mask3);

        vst1q_s32(&xi[cnt],      q0);
        vst1q_s32(&xi[cnt + 4],  q1);
        vst1q_s32(&xi[cnt + 8],  q2);
        vst1q_s32(&xi[cnt + 12], q3);
    }

    // Handle tail loop for remaining 4-element chunks if any
    for (; cnt < limit; cnt += 4)
    {
        float32x4_t x0 = vld1q_f32(&xr[cnt]);
        float32x4_t a0 = vabsq_f32(vmulq_f32(x0, t_sfac));
        a0 = vmulq_f32(a0, vsqrtq_f32(a0));
        a0 = vsqrtq_f32(a0);
        int32x4_t q0 = vcvtq_s32_f32(vaddq_f32(a0, magic));
        max0 = vmaxq_s32(max0, q0);
        int32x4_t mask0 = vshrq_n_s32(vreinterpretq_s32_f32(x0), 31);
        q0 = vsubq_s32(veorq_s32(q0, mask0), mask0);
        vst1q_s32(&xi[cnt], q0);
    }

    // Reduce max registers using horizontal max reduction instruction
    int32x4_t max_vec = vmaxq_s32(vmaxq_s32(max0, max1), vmaxq_s32(max2, max3));
    return vmaxvq_s32(max_vec);
}
#endif

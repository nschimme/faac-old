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
    int32x4_t max_vec0 = vdupq_n_s32(0);
    int32x4_t max_vec1 = vdupq_n_s32(0);
    int total = 4 * n4;
    int cnt = 0;

    // 2x unrolled NEON loop: 8 floats per iteration
    for (; cnt + 8 <= total; cnt += 8)
    {
        float32x4_t x0_orig = vld1q_f32(&xr[cnt]);
        float32x4_t x1_orig = vld1q_f32(&xr[cnt + 4]);

        float32x4_t x0 = vabsq_f32(vmulq_f32(x0_orig, sfac));
        float32x4_t x1 = vabsq_f32(vmulq_f32(x1_orig, sfac));

        x0 = vmulq_f32(x0, vsqrtq_f32(x0));
        x1 = vmulq_f32(x1, vsqrtq_f32(x1));

        x0 = vsqrtq_f32(x0);
        x1 = vsqrtq_f32(x1);

        x0 = vaddq_f32(x0, magic);
        x1 = vaddq_f32(x1, magic);

        int32x4_t q0 = vcvtq_s32_f32(x0);
        int32x4_t q1 = vcvtq_s32_f32(x1);

        max_vec0 = vmaxq_s32(max_vec0, q0);
        max_vec1 = vmaxq_s32(max_vec1, q1);

        int32x4_t mask0 = vreinterpretq_s32_u32(vcltq_f32(x0_orig, vdupq_n_f32(0.0f)));
        int32x4_t mask1 = vreinterpretq_s32_u32(vcltq_f32(x1_orig, vdupq_n_f32(0.0f)));

        q0 = vsubq_s32(veorq_s32(q0, mask0), mask0);
        q1 = vsubq_s32(veorq_s32(q1, mask1), mask1);

        vst1q_s32(&xi[cnt], q0);
        vst1q_s32(&xi[cnt + 4], q1);
    }

    max_vec0 = vmaxq_s32(max_vec0, max_vec1);

    if (cnt < total)
    {
        float32x4_t x_orig = vld1q_f32(&xr[cnt]);
        float32x4_t x = vabsq_f32(vmulq_f32(x_orig, sfac));

        x = vmulq_f32(x, vsqrtq_f32(x));
        x = vsqrtq_f32(x);
        x = vaddq_f32(x, magic);

        int32x4_t q = vcvtq_s32_f32(x);
        max_vec0 = vmaxq_s32(max_vec0, q);

        int32x4_t mask = vreinterpretq_s32_u32(vcltq_f32(x_orig, vdupq_n_f32(0.0f)));
        q = vsubq_s32(veorq_s32(q, mask), mask);

        vst1q_s32(&xi[cnt], q);
    }

    return vmaxvq_s32(max_vec0);
}
#endif

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

#if defined(HAVE_ARM_SVE) || defined(__ARM_FEATURE_SVE)
#include <arm_sve.h>
#include <math.h>
#include "quantize.h"

int quantize_sve(const float * __restrict xr, int * __restrict xi, int len, float sfacfix)
{
    int i = 0;
    svbool_t pg_all = svptrue_b32();
    svbool_t pg = svwhilelt_b32(i, len);
    svfloat32_t sfac = svdup_n_f32(sfacfix);
    svfloat32_t magic = svdup_n_f32(MAGIC_NUMBER);
    svint32_t max_vec = svdup_n_s32(0);

    while (svptest_any(pg_all, pg))
    {
        svfloat32_t x_orig = svld1_f32(pg, &xr[i]);
        svfloat32_t x = svabs_f32_x(pg, svmul_f32_x(pg, x_orig, sfac));

        x = svsqrt_f32_x(pg, svmul_f32_x(pg, x, svsqrt_f32_x(pg, x)));
        x = svadd_f32_x(pg, x, magic);

        svint32_t q = svcvt_s32_f32_x(pg, x);
        max_vec = svmax_s32_m(pg, max_vec, q);

        svint32_t orig_i = svreinterpret_s32_f32(x_orig);
        svint32_t mask = svasr_n_s32_x(pg, orig_i, 31);
        q = svsub_s32_x(pg, sveor_s32_x(pg, q, mask), mask);

        svst1_s32(pg, &xi[i], q);

        i += (int)svcntw();
        pg = svwhilelt_b32(i, len);
    }

    return svmaxv_s32(pg_all, max_vec);
}
#endif

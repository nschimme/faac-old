/*
 * Clean C11 Math & Data Abstractions for FAAD3
 * Supports both floating-point (default) and opt-in fixed-point
 * arithmetic without inner-loop #ifdef clutter.
 */

#ifndef FAAD_MATH_H
#define FAAD_MATH_H

#include "config.h"
#include <stdint.h>
#include <math.h>

#if FAAD_FIXED_POINT

#define REAL_BITS 14
#define REAL_SCALE (1 << REAL_BITS)

typedef int32_t real_t;

static inline real_t mul_real(real_t a, real_t b) {
    return (real_t)(((int64_t)a * b + (1 << (REAL_BITS - 1))) >> REAL_BITS);
}

static inline real_t div_real(real_t a, real_t b) {
    if (b == 0) return 0;
    return (real_t)((((int64_t)a) << REAL_BITS) / b);
}

static inline real_t add_real(real_t a, real_t b) {
    return a + b;
}

static inline real_t sub_real(real_t a, real_t b) {
    return a - b;
}

static inline real_t float_to_real(float v) {
    return (real_t)(v * (float)REAL_SCALE + (v >= 0.0f ? 0.5f : -0.5f));
}

static inline float real_to_float(real_t v) {
    return (float)v / (float)REAL_SCALE;
}

static inline int real_to_int(real_t v) {
    return (int)(v >> REAL_BITS);
}

static inline real_t int_to_real(int v) {
    return (real_t)(v << REAL_BITS);
}

static inline real_t sqrt_real_fixed(real_t x) {
    if (x <= 0) return 0;
    int64_t op = ((int64_t)x) << REAL_BITS;
    int64_t res = 0;
    int64_t one = (int64_t)1 << 62;
    while (one > op) one >>= 2;
    while (one != 0) {
        if (op >= res + one) {
            op -= res + one;
            res = (res >> 1) + one;
        } else {
            res >>= 1;
        }
        one >>= 2;
    }
    return (real_t)res;
}

#define REAL_CONST(v) float_to_real((float)(v))
#define MUL_REAL(a, b) mul_real((a), (b))
#define DIV_REAL(a, b) div_real((a), (b))
#define ADD_REAL(a, b) add_real((a), (b))
#define SUB_REAL(a, b) sub_real((a), (b))
#define REAL_TO_INT(v) real_to_int(v)
#define INT_TO_REAL(v) int_to_real(v)
#define SQRT_REAL(v) sqrt_real_fixed(v)
#define SIN_REAL(v) float_to_real(sinf(real_to_float(v)))
#define COS_REAL(v) float_to_real(cosf(real_to_float(v)))

#else

typedef float real_t;

#define REAL_CONST(v) ((float)(v))
#define MUL_REAL(a, b) ((a) * (b))
#define ADD_REAL(a, b) ((a) + (b))
#define SUB_REAL(a, b) ((a) - (b))
#define REAL_TO_INT(v) ((int)(v))
#define INT_TO_REAL(v) ((float)(v))
#define SQRT_REAL(v) sqrtf(v)
#define SIN_REAL(v) sinf(v)
#define COS_REAL(v) cosf(v)

static inline float float_to_real(float v) { return v; }
static inline float real_to_float(real_t v) { return v; }

#endif /* FAAD_FIXED_POINT */

#endif /* FAAD_MATH_H */

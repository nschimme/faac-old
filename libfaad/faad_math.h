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
    return (real_t)(((int64_t)a * b) >> REAL_BITS);
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

#define REAL_CONST(v) float_to_real((float)(v))
#define MUL_REAL(a, b) mul_real((a), (b))
#define ADD_REAL(a, b) add_real((a), (b))
#define SUB_REAL(a, b) sub_real((a), (b))
#define REAL_TO_INT(v) real_to_int(v)
#define INT_TO_REAL(v) int_to_real(v)
#define SQRT_REAL(v) float_to_real(sqrtf(real_to_float(v)))
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

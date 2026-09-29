/*
 * Fast FFT-based IMDCT and Windowing
 */

#include "faad_internal.h"
#include "fft.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static real_t kbd_window_2048[1024];
static real_t sine_window_2048[1024];
static real_t kbd_window_256[128];
static real_t sine_window_256[128];


/* DCT-IV twiddles exp(-j*pi*(n + 1/8)/M) for M = 1024 and 128; the same
 * table serves the pre- and post-rotation. */
static real_t dct4_cos_1024[512];
static real_t dct4_sin_1024[512];
static real_t dct4_cos_128[64];
static real_t dct4_sin_128[64];

static bool tables_init = false;

/* Zeroth-order modified Bessel function, power series. */
static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0, q = x * x / 4.0;
    for (int k = 1; k < 60; k++) {
        term *= q / ((double)k * k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

/* Kaiser-Bessel-derived window, first half (ISO/IEC 14496-3 §4.6.11.3.2):
 * the cumulative Kaiser kernel, normalised, under a square root. */
static void kbd_window(real_t *w, int n, double alpha)
{
    double sum = 0.0, run = 0.0;
    for (int i = 0; i <= n; i++) {
        double v = (2.0 * i / n) - 1.0;
        sum += bessel_i0(M_PI * alpha * sqrt(1.0 - v * v));
    }
    for (int i = 0; i < n; i++) {
        double v = (2.0 * i / n) - 1.0;
        run += bessel_i0(M_PI * alpha * sqrt(1.0 - v * v));
        w[i] = float_to_real((float)sqrt(run / sum));
    }
}

void init_windows(void)
{
    if (tables_init) return;

    fft_init();

    for (int i = 0; i < 1024; i++) {
        sine_window_2048[i] = float_to_real(sinf((float)M_PI * (i + 0.5f) / 2048.0f));
    }
    for (int i = 0; i < 128; i++) {
        sine_window_256[i] = float_to_real(sinf((float)M_PI * (i + 0.5f) / 256.0f));
    }
    kbd_window(kbd_window_2048, 1024, 4.0);
    kbd_window(kbd_window_256, 128, 6.0);

    for (int k = 0; k < 512; k++) {
        double ang = -M_PI * (k + 0.125) / 1024.0;
        dct4_cos_1024[k] = float_to_real((float)cos(ang));
        dct4_sin_1024[k] = float_to_real((float)sin(ang));
    }
    for (int k = 0; k < 64; k++) {
        double ang = -M_PI * (k + 0.125) / 128.0;
        dct4_cos_128[k] = float_to_real((float)cos(ang));
        dct4_sin_128[k] = float_to_real((float)sin(ang));
    }

    tables_init = true;
}

/* DCT-IV of length M through an M/2-point complex FFT: pack even-index
 * inputs against reversed odd-index inputs, rotate, transform, rotate again
 * and unzip. */
static void dct4(const real_t *in, real_t *u, int M)
{
    int K = M / 2;
    int logm = (M == 1024) ? 9 : 6;
    float z[1024], w[1024];

    const real_t *cos_tbl = (M == 1024) ? dct4_cos_1024 : dct4_cos_128;
    const real_t *sin_tbl = (M == 1024) ? dct4_sin_1024 : dct4_sin_128;
    float *zr = z, *zi = z + K;

    for (int n = 0; n < K; n++) {
        real_t a = in[2 * n], b = in[M - 1 - 2 * n];
        real_t zr_r = SUB_REAL(MUL_REAL(a, cos_tbl[n]), MUL_REAL(b, sin_tbl[n]));
        real_t zi_r = ADD_REAL(MUL_REAL(a, sin_tbl[n]), MUL_REAL(b, cos_tbl[n]));
        zr[n] = real_to_float(zr_r);
        zi[n] = real_to_float(zi_r);
    }
    fft(z, w, logm);
    const float *wr = w, *wi = w + K;
    for (int k = 0; k < K; k++) {
        real_t wr_r = float_to_real(wr[k]);
        real_t wi_r = float_to_real(wi[k]);
        real_t u0 = SUB_REAL(MUL_REAL(wr_r, cos_tbl[k]), MUL_REAL(wi_r, sin_tbl[k]));
        real_t u1 = -ADD_REAL(MUL_REAL(wr_r, sin_tbl[k]), MUL_REAL(wi_r, cos_tbl[k]));
        u[2 * k]         = u0;
        u[M - 1 - 2 * k] = u1;
    }
}

/* IMDCT (ISO/IEC 14496-3 §4.6.11.3.1): n0 = N/4 + 1/2 makes the transform a
 * DCT-IV of the coefficients, folded out with its odd/even symmetries. */
static void fast_imdct(const real_t *in, real_t *out, int n)
{
    int M = n / 2, H = M / 2;
    real_t u[1024];
    real_t scale = float_to_real(2.0f / (float)n);

    dct4(in, u, M);
    for (int i = 0; i < H; i++) {
        out[i]             =  MUL_REAL(u[H + i], scale);
        out[M + H + i]     = -MUL_REAL(u[i], scale);
    }
    for (int i = H; i < M + H; i++)
        out[i] = -MUL_REAL(u[M + H - 1 - i], scale);
}

/* The 2048-sample IMDCT output, folded out of the 1024-point DCT-IV u (see
 * fast_imdct), sample i, scaled. */
static inline real_t imdct_sample(const real_t *u, int i, real_t scale)
{
    if (i < 512) return MUL_REAL(u[512 + i], scale);
    if (i < 1536) return -MUL_REAL(u[1535 - i], scale);
    return -MUL_REAL(u[i - 1536], scale);
}

/* Left half: window, add the previous frame's overlap, emit. Right half:
 * window into the overlap for the next frame. */
static inline void imdct_emit(real_t * restrict out_pcm, real_t * restrict overlap, const real_t *u, real_t scale,
                              int i0, int i1, const real_t * restrict wl, int wl_dir, real_t wflat)
{
    /* window sample i is wl[i - i0] (wl_dir > 0), wl[i1 - 1 - i] (< 0) or wflat (wl == NULL) */
    for (int i = i0; i < i1; i++) {
        real_t r_w = wl ? (wl_dir > 0 ? wl[i - i0] : wl[i1 - 1 - i]) : wflat;
        real_t x = MUL_REAL(imdct_sample(u, i, scale), r_w);
        if (i < FRAME_LEN_LONG) out_pcm[i] = ADD_REAL(x, overlap[i]);
        else overlap[i - FRAME_LEN_LONG] = x;
    }
}

void imdct_and_window(struct faad_decoder *dec, uint32_t ch, ICSInfo *ics, real_t * restrict spec, real_t * restrict out_pcm)
{
    /* ISO/IEC 14496-3 §4.6.11.3.2: the left half of the window uses the
     * previous block's shape, the right half this block's. */
    uint8_t prev_shape = dec->prev_window_shape[ch];
    const real_t * restrict win_long_l = (prev_shape == KBD_WINDOW) ? kbd_window_2048 : sine_window_2048;
    const real_t * restrict win_short_l = (prev_shape == KBD_WINDOW) ? kbd_window_256 : sine_window_256;
    const real_t * restrict win_long = (ics->window_shape == KBD_WINDOW) ? kbd_window_2048 : sine_window_2048;
    const real_t * restrict win_short = (ics->window_shape == KBD_WINDOW) ? kbd_window_256 : sine_window_256;
    real_t * restrict overlap = dec->overlap[ch];
    dec->prev_window_shape[ch] = ics->window_shape;

    if (ics->window_sequence == EIGHT_SHORT_SEQUENCE) {
        /* eight 256-sample blocks hopping by 128 cover samples 448..1599 */
        real_t acc[1152];
        memset(acc, 0, sizeof(acc));
        for (int w = 0; w < 8; w++) {
            real_t block[256];
            fast_imdct(spec + w * 128, block, 256);
            const real_t * restrict wl = (w == 0) ? win_short_l : win_short;
            real_t *dst = acc + w * 128;
            for (int i = 0; i < 128; i++) {
                dst[i]       = ADD_REAL(dst[i], MUL_REAL(block[i], wl[i]));
                dst[255 - i] = ADD_REAL(dst[255 - i], MUL_REAL(block[255 - i], win_short[i]));
            }
        }
        for (int i = 0; i < 448; i++) out_pcm[i] = overlap[i];
        for (int i = 448; i < FRAME_LEN_LONG; i++) out_pcm[i] = ADD_REAL(acc[i - 448], overlap[i]);
        for (int i = 0; i < 576; i++) overlap[i] = acc[i + 576];
        memset(overlap + 576, 0, sizeof(real_t) * 448);
        return;
    }

    real_t u[1024];
    const real_t scale = float_to_real(2.0f / 2048.0f);
    dct4(spec, u, 1024);

    if (ics->window_sequence == LONG_STOP_SEQUENCE) {
        /* zero, the short window's rise, then flat */
        for (int i = 0; i < 448; i++) out_pcm[i] = overlap[i];
        imdct_emit(out_pcm, overlap, u, scale, 448, 576, win_short_l, 1, REAL_CONST(0.0));
        imdct_emit(out_pcm, overlap, u, scale, 576, 1024, NULL, 0, REAL_CONST(1.0));
    } else {
        imdct_emit(out_pcm, overlap, u, scale, 0, 1024, win_long_l, 1, REAL_CONST(0.0));
    }
    if (ics->window_sequence == LONG_START_SEQUENCE) {
        /* flat, the short window's fall, then zero */
        imdct_emit(out_pcm, overlap, u, scale, 1024, 1472, NULL, 0, REAL_CONST(1.0));
        imdct_emit(out_pcm, overlap, u, scale, 1472, 1600, win_short, -1, REAL_CONST(0.0));
        memset(overlap + 576, 0, sizeof(real_t) * 448);
    } else {
        imdct_emit(out_pcm, overlap, u, scale, 1024, 2048, win_long, -1, REAL_CONST(0.0));
    }
}


/*
 * Fast FFT-based IMDCT and Windowing
 */

#include "faad_internal.h"
#include "fft.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float kbd_window_2048[1024];
static float sine_window_2048[1024];
static float kbd_window_256[128];
static float sine_window_256[128];


/* DCT-IV twiddles exp(-j*pi*(n + 1/8)/M) for M = 1024 and 128; the same
 * table serves the pre- and post-rotation. */
static float dct4_cos_1024[512];
static float dct4_sin_1024[512];
static float dct4_cos_128[64];
static float dct4_sin_128[64];

static FFT_Tables fft_tbl;

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
static void kbd_window(float *w, int n, double alpha)
{
    double sum = 0.0, run = 0.0;
    for (int i = 0; i <= n; i++) {
        double v = (2.0 * i / n) - 1.0;
        sum += bessel_i0(M_PI * alpha * sqrt(1.0 - v * v));
    }
    for (int i = 0; i < n; i++) {
        double v = (2.0 * i / n) - 1.0;
        run += bessel_i0(M_PI * alpha * sqrt(1.0 - v * v));
        w[i] = (float)sqrt(run / sum);
    }
}

void init_windows(void)
{
    if (tables_init) return;

    fft_initialize(&fft_tbl);

    for (int i = 0; i < 1024; i++) {
        sine_window_2048[i] = sinf((float)M_PI * (i + 0.5f) / 2048.0f);
    }
    for (int i = 0; i < 128; i++) {
        sine_window_256[i] = sinf((float)M_PI * (i + 0.5f) / 256.0f);
    }
    kbd_window(kbd_window_2048, 1024, 4.0);
    kbd_window(kbd_window_256, 128, 6.0);

    for (int k = 0; k < 512; k++) {
        double ang = -M_PI * (k + 0.125) / 1024.0;
        dct4_cos_1024[k] = (float)cos(ang);
        dct4_sin_1024[k] = (float)sin(ang);
    }
    for (int k = 0; k < 64; k++) {
        double ang = -M_PI * (k + 0.125) / 128.0;
        dct4_cos_128[k] = (float)cos(ang);
        dct4_sin_128[k] = (float)sin(ang);
    }

    tables_init = true;
}

/* DCT-IV of length M through an M/2-point complex FFT: pack even-index
 * inputs against reversed odd-index inputs, rotate, transform, rotate again
 * and unzip. */
static void dct4(const float *in, float *u, int M)
{
    int K = M / 2;
    int logm = (M == 1024) ? 9 : 6;
    const float *cs = (M == 1024) ? dct4_cos_1024 : dct4_cos_128;
    const float *sn = (M == 1024) ? dct4_sin_1024 : dct4_sin_128;
    float zr[512], zi[512];

    for (int n = 0; n < K; n++) {
        float a = in[2 * n], b = in[M - 1 - 2 * n];
        zr[n] = a * cs[n] - b * sn[n];
        zi[n] = a * sn[n] + b * cs[n];
    }
    fft(&fft_tbl, zr, zi, logm);
    for (int k = 0; k < K; k++) {
        u[2 * k]         =  zr[k] * cs[k] - zi[k] * sn[k];
        u[M - 1 - 2 * k] = -(zr[k] * sn[k] + zi[k] * cs[k]);
    }
}

/* IMDCT (ISO/IEC 14496-3 §4.6.11.3.1): n0 = N/4 + 1/2 makes the transform a
 * DCT-IV of the coefficients, folded out with its odd/even symmetries. */
static void fast_imdct(const float *in, float *out, int n)
{
    int M = n / 2, H = M / 2;
    float u[1024];
    float scale = 2.0f / (float)n;

    dct4(in, u, M);
    for (int i = 0; i < H; i++) {
        out[i]             =  u[H + i] * scale;
        out[M + H + i]     = -u[i] * scale;
    }
    for (int i = H; i < M + H; i++)
        out[i] = -u[M + H - 1 - i] * scale;
}

/* The 2048-sample IMDCT output, folded out of the 1024-point DCT-IV u (see
 * fast_imdct), sample i, scaled. */
static inline float imdct_sample(const float *u, int i, float scale)
{
    if (i < 512) return u[512 + i] * scale;
    if (i < 1536) return -u[1535 - i] * scale;
    return -u[i - 1536] * scale;
}

/* Left half: window, add the previous frame's overlap, emit. Right half:
 * window into the overlap for the next frame. */
static inline void imdct_emit(float * restrict out_pcm, float * restrict overlap, const float *u, float scale,
                              int i0, int i1, const float * restrict wl, int wl_dir, float wflat)
{
    /* window sample i is wl[i - i0] (wl_dir > 0), wl[i1 - 1 - i] (< 0) or wflat (wl == NULL) */
    for (int i = i0; i < i1; i++) {
        float w = wl ? (wl_dir > 0 ? wl[i - i0] : wl[i1 - 1 - i]) : wflat;
        float x = imdct_sample(u, i, scale) * w;
        if (i < FRAME_LEN_LONG) out_pcm[i] = x + overlap[i];
        else overlap[i - FRAME_LEN_LONG] = x;
    }
}

void imdct_and_window(struct faad_decoder *dec, uint32_t ch, ICSInfo *ics, float * restrict spec, float * restrict out_pcm)
{
    /* ISO/IEC 14496-3 §4.6.11.3.2: the left half of the window uses the
     * previous block's shape, the right half this block's. */
    uint8_t prev_shape = dec->prev_window_shape[ch];
    const float * restrict win_long_l = (prev_shape == KBD_WINDOW) ? kbd_window_2048 : sine_window_2048;
    const float * restrict win_short_l = (prev_shape == KBD_WINDOW) ? kbd_window_256 : sine_window_256;
    const float * restrict win_long = (ics->window_shape == KBD_WINDOW) ? kbd_window_2048 : sine_window_2048;
    const float * restrict win_short = (ics->window_shape == KBD_WINDOW) ? kbd_window_256 : sine_window_256;
    float * restrict overlap = dec->overlap[ch];
    dec->prev_window_shape[ch] = ics->window_shape;

    if (ics->window_sequence == EIGHT_SHORT_SEQUENCE) {
        /* eight 256-sample blocks hopping by 128 cover samples 448..1599 */
        float acc[1152];
        memset(acc, 0, sizeof(acc));
        for (int w = 0; w < 8; w++) {
            float block[256];
            fast_imdct(spec + w * 128, block, 256);
            const float * restrict wl = (w == 0) ? win_short_l : win_short;
            float *dst = acc + w * 128;
            for (int i = 0; i < 128; i++) {
                dst[i]       += block[i] * wl[i];
                dst[255 - i] += block[255 - i] * win_short[i];
            }
        }
        for (int i = 0; i < 448; i++) out_pcm[i] = overlap[i];
        for (int i = 448; i < FRAME_LEN_LONG; i++) out_pcm[i] = acc[i - 448] + overlap[i];
        for (int i = 0; i < 576; i++) overlap[i] = acc[i + 576];
        memset(overlap + 576, 0, sizeof(float) * 448);
        return;
    }

    float u[1024];
    const float scale = 2.0f / 2048.0f;
    dct4(spec, u, 1024);

    if (ics->window_sequence == LONG_STOP_SEQUENCE) {
        /* zero, the short window's rise, then flat */
        for (int i = 0; i < 448; i++) out_pcm[i] = overlap[i];
        imdct_emit(out_pcm, overlap, u, scale, 448, 576, win_short_l, 1, 0.0f);
        imdct_emit(out_pcm, overlap, u, scale, 576, 1024, NULL, 0, 1.0f);
    } else {
        imdct_emit(out_pcm, overlap, u, scale, 0, 1024, win_long_l, 1, 0.0f);
    }
    if (ics->window_sequence == LONG_START_SEQUENCE) {
        /* flat, the short window's fall, then zero */
        imdct_emit(out_pcm, overlap, u, scale, 1024, 1472, NULL, 0, 1.0f);
        imdct_emit(out_pcm, overlap, u, scale, 1472, 1600, win_short, -1, 0.0f);
        memset(overlap + 576, 0, sizeof(float) * 448);
    } else {
        imdct_emit(out_pcm, overlap, u, scale, 1024, 2048, win_long, -1, 0.0f);
    }
}


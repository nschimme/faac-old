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

/* Kaiser-Bessel-derived window, first half (ISO/IEC 14496-3 §4.6.11.3.2). */
static void kbd_window(float *w, int n, double alpha)
{
    double sum = 0.0, run = 0.0;
    for (int i = 0; i <= n; i++) {
        double v = (2.0 * i / n) - 1.0;
        sum += cosh(M_PI * alpha * sqrt(1.0 - v * v));
    }
    for (int i = 0; i < n; i++) {
        double v = (2.0 * i / n) - 1.0;
        run += cosh(M_PI * alpha * sqrt(1.0 - v * v));
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

void imdct_and_window(struct faad_decoder *dec, uint32_t ch, ICSInfo *ics, float * restrict spec, float * restrict out_pcm)
{
    float imdct_out[FRAME_LEN_LONG * 2];

    /* ISO/IEC 14496-3 §4.6.11.3.2: the left half of the window uses the
     * previous block's shape, the right half this block's. */
    uint8_t prev_shape = dec->prev_window_shape[ch];
    const float * restrict win_long_l = (prev_shape == KBD_WINDOW) ? kbd_window_2048 : sine_window_2048;
    const float * restrict win_short_l = (prev_shape == KBD_WINDOW) ? kbd_window_256 : sine_window_256;
    const float * restrict win_long = (ics->window_shape == KBD_WINDOW) ? kbd_window_2048 : sine_window_2048;
    const float * restrict win_short = (ics->window_shape == KBD_WINDOW) ? kbd_window_256 : sine_window_256;
    float * restrict overlap_ch = dec->overlap[ch];
    dec->prev_window_shape[ch] = ics->window_shape;

    if (ics->window_sequence == EIGHT_SHORT_SEQUENCE) {
        memset(imdct_out, 0, sizeof(imdct_out));
        float short_out[256];
        for (int w = 0; w < 8; w++) {
            fast_imdct(spec + w * 128, short_out, 256);
            const float * restrict wl = (w == 0) ? win_short_l : win_short;
            for (int i = 0; i < 128; i++) {
                short_out[i] *= wl[i];
                short_out[255 - i] *= win_short[i];
            }
            int offset = 448 + w * 128;
            for (int i = 0; i < 256; i++) {
                imdct_out[offset + i] += short_out[i];
            }
        }
    } else {
        fast_imdct(spec, imdct_out, 2048);
        if (ics->window_sequence == ONLY_LONG_SEQUENCE) {
            for (int i = 0; i < 1024; i++) {
                imdct_out[i] *= win_long_l[i];
                imdct_out[2047 - i] *= win_long[i];
            }
        } else if (ics->window_sequence == LONG_START_SEQUENCE) {
            for (int i = 0; i < 1024; i++) {
                imdct_out[i] *= win_long_l[i];
            }
            /* 1024..1471: flat 1.0 */
            for (int i = 1472; i < 1600; i++) {
                imdct_out[i] *= win_short[1599 - i]; /* falling half of short window */
            }
            for (int i = 1600; i < 2048; i++) {
                imdct_out[i] = 0.0f;
            }
        } else if (ics->window_sequence == LONG_STOP_SEQUENCE) {
            for (int i = 0; i < 448; i++) {
                imdct_out[i] = 0.0f;
            }
            for (int i = 448; i < 576; i++) {
                imdct_out[i] *= win_short_l[i - 448]; /* rising half of short window */
            }
            /* 576..1023: flat 1.0 */
            for (int i = 1024; i < 2048; i++) {
                imdct_out[i] *= win_long[2047 - i];
            }
        }
    }

    /* Overlap-add with previous frame overlap buffer */
    for (int i = 0; i < FRAME_LEN_LONG; i++) {
        out_pcm[i] = imdct_out[i] + overlap_ch[i];
        overlap_ch[i] = imdct_out[FRAME_LEN_LONG + i];
    }
}

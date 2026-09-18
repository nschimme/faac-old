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

static inline float get_win_2048(const float *win, int i) {
    return (i < 1024) ? win[i] : win[2047 - i];
}

static inline float get_win_256(const float *win, int i) {
    return (i < 128) ? win[i] : win[255 - i];
}

/* Precomputed twiddle tables for fast IMDCT */
static float imdct_cos_2048[512];
static float imdct_sin_2048[512];
static float imdct_post_cos_2048[512];
static float imdct_post_sin_2048[512];

static float imdct_cos_256[64];
static float imdct_sin_256[64];
static float imdct_post_cos_256[64];
static float imdct_post_sin_256[64];

static FFT_Tables fft_tbl;

static bool tables_init = false;

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

    double sum = 0.0;
    double alpha = 4.0;
    for (int i = 0; i < 1024; i++) {
        double v = (2.0 * i / 1024.0) - 1.0;
        double term = cosh(alpha * sqrt(1.0 - v * v));
        sum += term;
    }
    double run_sum = 0.0;
    for (int i = 0; i < 1024; i++) {
        double v = (2.0 * i / 1024.0) - 1.0;
        run_sum += cosh(alpha * sqrt(1.0 - v * v));
        kbd_window_2048[i] = sqrt(run_sum / sum);
    }

    sum = 0.0;
    for (int i = 0; i < 128; i++) {
        double v = (2.0 * i / 128.0) - 1.0;
        double term = cosh(alpha * sqrt(1.0 - v * v));
        sum += term;
    }
    run_sum = 0.0;
    for (int i = 0; i < 128; i++) {
        double v = (2.0 * i / 128.0) - 1.0;
        run_sum += cosh(alpha * sqrt(1.0 - v * v));
        kbd_window_256[i] = sqrt(run_sum / sum);
    }

    /* Precompute IMDCT pre- and post-twiddle tables for N=2048 */
    for (int k = 0; k < 512; k++) {
        float angle_pre = (float)M_PI * (2 * k + 0.5f) / 2048.0f;
        imdct_cos_2048[k] = cosf(angle_pre);
        imdct_sin_2048[k] = sinf(angle_pre);

        float angle_post = (float)M_PI * (2 * k + 0.5f + 1024.0f) / 4096.0f;
        imdct_post_cos_2048[k] = cosf(angle_post);
        imdct_post_sin_2048[k] = sinf(angle_post);
    }

    /* Precompute IMDCT pre- and post-twiddle tables for N=256 */
    for (int k = 0; k < 64; k++) {
        float angle_pre = (float)M_PI * (2 * k + 0.5f) / 256.0f;
        imdct_cos_256[k] = cosf(angle_pre);
        imdct_sin_256[k] = sinf(angle_pre);

        float angle_post = (float)M_PI * (2 * k + 0.5f + 128.0f) / 512.0f;
        imdct_post_cos_256[k] = cosf(angle_post);
        imdct_post_sin_256[k] = sinf(angle_post);
    }

    tables_init = true;
}

static void fast_imdct(const float *in, float *out, int n)
{
    int n2 = n / 2;
    int n4 = n / 4;
    int logm = 0;
    while ((1 << logm) < n2) logm++;

    float xr[1024], xi[1024];

    const float *cos_pre = (n == 2048) ? imdct_cos_2048 : imdct_cos_256;
    const float *sin_pre = (n == 2048) ? imdct_sin_2048 : imdct_sin_256;
    const float *cos_post = (n == 2048) ? imdct_post_cos_2048 : imdct_post_cos_256;
    const float *sin_post = (n == 2048) ? imdct_post_sin_2048 : imdct_post_sin_256;

    /* Fast table-driven Pre-twiddle */
    for (int k = 0; k < n4; k++) {
        float re = in[2 * k];
        float im = in[n2 - 1 - 2 * k];
        float c = cos_pre[k];
        float s = sin_pre[k];
        xr[k] = re * c + im * s;
        xi[k] = im * c - re * s;
    }

    fft(&fft_tbl, xr, xi, logm - 1);

    /* Fast table-driven Post-twiddle and mirror with 2.0 / n scaling */
    float scale = 2.0f / (float)n;
    for (int k = 0; k < n4; k++) {
        float c = cos_post[k];
        float s = sin_post[k];
        float re = (xr[k] * c - xi[k] * s) * scale;
        float im = (xi[k] * c + xr[k] * s) * scale;

        out[2 * k] = -re;
        out[n2 - 1 - 2 * k] = im;
        out[n2 + 2 * k] = im;
        out[n - 1 - 2 * k] = re;
    }
}

void imdct_and_window(struct faad_decoder *dec, uint32_t ch, ICSInfo *ics, float * restrict spec, float * restrict out_pcm)
{
    float imdct_out[FRAME_LEN_LONG * 2];
    memset(imdct_out, 0, sizeof(imdct_out));

    const float * restrict win_long = (ics->window_shape == KBD_WINDOW) ? kbd_window_2048 : sine_window_2048;
    const float * restrict win_short = (ics->window_shape == KBD_WINDOW) ? kbd_window_256 : sine_window_256;
    float * restrict overlap_ch = dec->overlap[ch];

    if (ics->window_sequence == EIGHT_SHORT_SEQUENCE) {
        float short_out[256];
        for (int w = 0; w < 8; w++) {
            fast_imdct(spec + w * 128, short_out, 256);
            for (int i = 0; i < 256; i++) {
                short_out[i] *= get_win_256(win_short, i);
            }
            int offset = 448 + w * 128;
            for (int i = 0; i < 256; i++) {
                imdct_out[offset + i] += short_out[i];
            }
        }
    } else {
        fast_imdct(spec, imdct_out, 2048);
        if (ics->window_sequence == ONLY_LONG_SEQUENCE) {
            for (int i = 0; i < 2048; i++) {
                imdct_out[i] *= get_win_2048(win_long, i);
            }
        } else if (ics->window_sequence == LONG_START_SEQUENCE) {
            for (int i = 0; i < 1024; i++) {
                imdct_out[i] *= get_win_2048(win_long, i);
            }
            /* 1024..1447: flat 1.0 */
            for (int i = 1448; i < 1576; i++) {
                imdct_out[i] *= get_win_256(win_short, i - 1448 + 128); /* falling half of short window */
            }
            for (int i = 1576; i < 2048; i++) {
                imdct_out[i] = 0.0f;
            }
        } else if (ics->window_sequence == LONG_STOP_SEQUENCE) {
            for (int i = 0; i < 448; i++) {
                imdct_out[i] = 0.0f;
            }
            for (int i = 448; i < 576; i++) {
                imdct_out[i] *= get_win_256(win_short, i - 448); /* rising half of short window */
            }
            /* 576..1023: flat 1.0 */
            for (int i = 1024; i < 2048; i++) {
                imdct_out[i] *= get_win_2048(win_long, i);
            }
        }
    }

    /* Overlap-add with previous frame overlap buffer */
    for (int i = 0; i < FRAME_LEN_LONG; i++) {
        out_pcm[i] = imdct_out[i] + overlap_ch[i];
        overlap_ch[i] = imdct_out[FRAME_LEN_LONG + i];
    }
}

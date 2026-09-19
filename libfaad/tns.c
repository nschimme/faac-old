/*
 * Temporal Noise Shaping (TNS) Decoder
 */

#include "faad_internal.h"
#include "sfb_tables.h"

/* TNS_MAX_BANDS (ISO/IEC 14496-3 Table 4.139), indexed by sampling_frequency_index. */
static const uint8_t tns_max_bands_long[12]  = { 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };
static const uint8_t tns_max_bands_short[12] = {  9,  9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static int tns_max_bands_for(int sr_idx, bool is_short)
{
    if (sr_idx < 0 || sr_idx > 11) sr_idx = 4;
    return is_short ? tns_max_bands_short[sr_idx] : tns_max_bands_long[sr_idx];
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void tns_ar_filter(float * restrict spec, int length, int dir, const float * restrict lpc, int order)
{
    if (length <= 0 || order <= 0) return;

    if (!dir) {
        for (int i = 0; i < length; i++) {
            float sum = spec[i];
            int limit = (i < order) ? i : order;
            for (int j = 1; j <= limit; j++) {
                sum -= lpc[j - 1] * spec[i - j];
            }
            spec[i] = sum;
        }
    } else {
        for (int i = length - 1; i >= 0; i--) {
            float sum = spec[i];
            int limit = (length - 1 - i < order) ? (length - 1 - i) : order;
            for (int j = 1; j <= limit; j++) {
                sum -= lpc[j - 1] * spec[i + j];
            }
            spec[i] = sum;
        }
    }
}

void apply_tns(ICSInfo *ics, float *spec)
{
#ifndef FAAD_DISABLE_TNS
    if (!ics->tns_data_present) return;

    bool is_short = (ics->window_sequence == EIGHT_SHORT_SEQUENCE);
    int tns_max_bands = tns_max_bands_for(ics->sample_rate_index, is_short);
    int max_order = is_short ? 7 : 12;

    for (int w = 0; w < ics->num_windows; w++) {
        float *window_spec = spec + w * 128;
        int limit = ics->max_sfb < tns_max_bands ? ics->max_sfb : tns_max_bands;
        int bottom = ics->num_sfbs;

        for (int f = 0; f < ics->tns_n_filt[w]; f++) {
            int order = ics->tns_order[w][f];
            int length = ics->tns_length[w][f];
            int dir = ics->tns_direction[w][f];
            if (order > max_order) order = max_order;

            /* Filter regions stack downwards from the top of the sfb table;
             * only afterwards is each clipped to max_sfb / TNS_MAX_BANDS. */
            int top = bottom;
            bottom = (top > length) ? (top - length) : 0;
            if (order == 0) continue;

            int start_line = ics->sfb_offsets[bottom < limit ? bottom : limit];
            int end_line = ics->sfb_offsets[top < limit ? top : limit];
            int num_lines = end_line - start_line;

            if (num_lines <= 0) continue;

            /* Convert quantized Reflection Coefficients (parcor) to LPC coefficients via Levinson-Durbin step-down */
            float rc[32];
            float lpc[32];
            float lpc_tmp[32];
            /* §4.6.9.3: the quantiser is asymmetric, one more step on the
             * negative side: iqfac = (2^(bits-1) -/+ 0.5) / (pi/2). */
            float half = (ics->tns_coef_res[w] == 1) ? 8.0f : 4.0f;
            float iqfac   = (half - 0.5f) / (float)(M_PI / 2.0);
            float iqfac_m = (half + 0.5f) / (float)(M_PI / 2.0);

            for (int i = 0; i < order; i++) {
                int8_t val = ics->tns_coef[w][f][i];
                rc[i] = sinf((float)val / (val >= 0 ? iqfac : iqfac_m));
            }

            for (int m = 0; m < order; m++) {
                lpc[m] = rc[m];
                for (int i = 0; i < m; i++) {
                    lpc_tmp[i] = lpc[i] + rc[m] * lpc[m - 1 - i];
                }
                for (int i = 0; i < m; i++) {
                    lpc[i] = lpc_tmp[i];
                }
            }

            tns_ar_filter(window_spec + start_line, num_lines, dir, lpc, order);
        }
    }
#else
    (void)ics; (void)spec;
#endif
}

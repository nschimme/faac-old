/*
 * Inverse Quantization and PNS
 */

#include "faad_internal.h"

static float pow_4_3_lut[128];
static float sf_scale_lut[256];

static bool dequant_tables_init = false;

void init_dequant_tables(void)
{
    if (dequant_tables_init) return;

    for (int i = 0; i < 128; i++) {
        pow_4_3_lut[i] = powf((float)i, 4.0f / 3.0f);
    }
    for (int i = 0; i < 256; i++) {
        sf_scale_lut[i] = powf(2.0f, 0.25f * (i - 100));
    }

    dequant_tables_init = true;
}

static inline float pow_4_3_fast(int x)
{
    int abs_x = abs(x);
    if (abs_x < 128) {
        float val = pow_4_3_lut[abs_x];
        return (x < 0) ? -val : val;
    }
    float val = powf((float)abs_x, 4.0f / 3.0f);
    return (x < 0) ? -val : val;
}

void dequantize_spectrum(ICSInfo *ics, float *spec)
{
    int window_offset = 0;
    for (int g = 0; g < ics->num_window_groups && g < 8; g++) {
        for (int i = 0; i < ics->num_sections[g] && i < 64; i++) {
            int cb = ics->sect_cb[g][i];
            if (cb == 0 || cb == 13) continue;

            int start_sfb = ics->sect_start[g][i];
            int end_sfb = ics->sect_end[g][i];
            if (start_sfb < 0 || start_sfb >= ics->num_sfbs || start_sfb >= 64) continue;
            if (end_sfb > ics->num_sfbs) end_sfb = ics->num_sfbs;
            if (end_sfb > 64) end_sfb = 64;

            for (int sfb = start_sfb; sfb < end_sfb && (sfb + 1) <= ics->num_sfbs && (sfb + 1) < 68; sfb++) {
                int sf = ics->scalefactors[g][sfb];
                float scale = (sf >= 0 && sf < 256) ? sf_scale_lut[sf] : powf(2.0f, 0.25f * (sf - 100));

                int start_k = ics->sfb_offsets[sfb];
                int end_k = ics->sfb_offsets[sfb + 1];
                if (start_k >= FRAME_LEN_LONG) continue;
                if (end_k > FRAME_LEN_LONG) end_k = FRAME_LEN_LONG;

                for (int w = 0; w < ics->window_group_length[g]; w++) {
                    float * restrict ptr = spec + (window_offset + w) * 128 + start_k;
                    int len = end_k - start_k;
                    for (int k = 0; k < len; k++) {
                        float quant = ptr[k];
                        if (quant == 0.0f) continue;
                        int abs_val = (int)fabsf(quant);
                        float dequant_val = (abs_val < 128) ? pow_4_3_lut[abs_val] : powf((float)abs_val, 4.0f / 3.0f);
                        ptr[k] = (quant < 0.0f ? -dequant_val : dequant_val) * scale;
                    }
                }
            }
        }
        window_offset += ics->window_group_length[g];
    }
}

void apply_pns(ICSInfo *ics, float *spec, uint32_t *pns_seed)
{
    int window_offset = 0;
    uint32_t seed = *pns_seed;

    for (int g = 0; g < ics->num_window_groups && g < 8; g++) {
        for (int sfb = 0; sfb < ics->num_sfbs && (sfb + 1) <= ics->num_sfbs && (sfb + 1) < 68; sfb++) {
            if (ics->pns_used[g][sfb]) {
                int sf = ics->scalefactors[g][sfb];
                float scale = (sf >= 0 && sf < 256) ? sf_scale_lut[sf] : powf(2.0f, 0.25f * (sf - 100));

                int start_k = ics->sfb_offsets[sfb];
                int end_k = ics->sfb_offsets[sfb + 1];
                if (start_k >= FRAME_LEN_LONG) continue;
                if (end_k > FRAME_LEN_LONG) end_k = FRAME_LEN_LONG;
                int len = end_k - start_k;

                for (int w = 0; w < ics->window_group_length[g]; w++) {
                    int win_offset_k = (window_offset + w) * 128 + start_k;
                    if (win_offset_k < 0 || win_offset_k + len > FRAME_LEN_LONG) continue;
                    float * restrict ptr = spec + win_offset_k;
                    float energy = 0.0f;

                    for (int k = 0; k < len; k++) {
                        seed = (seed * 1664525U) + 1013904223U;
                        float noise = ((float)(int32_t)seed) * (1.0f / 2147483648.0f);
                        ptr[k] = noise;
                        energy += noise * noise;
                    }

                    if (energy > 0.0f) {
                        float norm = scale / sqrtf(energy);
                        for (int k = 0; k < len; k++) {
                            ptr[k] *= norm;
                        }
                    }
                }
            }
        }
        window_offset += ics->window_group_length[g];
    }
    *pns_seed = seed;
}

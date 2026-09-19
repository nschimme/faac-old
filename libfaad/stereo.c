/*
 * Mid/Side and Intensity Stereo Decoding
 */

#include "faad_internal.h"

/* 0.5^(is_position/4) for is_position in [-IS_POS_RANGE, IS_POS_RANGE]. */
#define IS_POS_RANGE 128
static float is_scale_lut[2 * IS_POS_RANGE + 1];
static bool is_tables_init = false;

static void init_is_tables(void)
{
    if (is_tables_init) return;
    for (int p = -IS_POS_RANGE; p <= IS_POS_RANGE; p++) {
        is_scale_lut[p + IS_POS_RANGE] = powf(0.5f, 0.25f * (float)p);
    }
    is_tables_init = true;
}

void apply_ms_stereo(CPEInfo *cpe, float * restrict spec_l, float * restrict spec_r)
{
    ICSInfo *ics = &cpe->ics[0];

    int window_offset = 0;
    for (int g = 0; g < ics->num_window_groups && g < 8; g++) {
        for (int sfb = 0; sfb < ics->max_sfb && sfb < 64; sfb++) {
            bool ms_flag = false;
            if (cpe->ms_mask_present == 1) {
                ms_flag = (cpe->ms_used[g][sfb] != 0);
            } else if (cpe->ms_mask_present != 0) {
                ms_flag = true;
            }

            if (!ms_flag) continue;
            bool pns_l = ics->pns_used[g][sfb], pns_r = cpe->ics[1].pns_used[g][sfb];
            if (pns_l != pns_r) continue;

            int start_k = ics->sfb_offsets[sfb];
            int end_k = ics->sfb_offsets[sfb + 1];
            if (start_k >= FRAME_LEN_LONG) continue;
            if (end_k > FRAME_LEN_LONG) end_k = FRAME_LEN_LONG;
            int len = end_k - start_k;

            for (int w = 0; w < ics->window_group_length[g]; w++) {
                int win_offset_k = (window_offset + w) * 128 + start_k;
                if (win_offset_k < 0 || win_offset_k + len > FRAME_LEN_LONG) continue;
                float * restrict l_ptr = spec_l + win_offset_k;
                float * restrict r_ptr = spec_r + win_offset_k;

                if (pns_l) {
                    /* §4.6.13.3: ms_used on a PNS band means correlated noise:
                     * the right channel reuses the left vector at its own level. */
                    float gain = powf(2.0f, 0.25f * (float)(cpe->ics[1].scalefactors[g][sfb] - ics->scalefactors[g][sfb]));
                    for (int k = 0; k < len; k++) r_ptr[k] = l_ptr[k] * gain;
                } else {
                    for (int k = 0; k < len; k++) {
                        float m = l_ptr[k];
                        float s = r_ptr[k];
                        l_ptr[k] = m + s;
                        r_ptr[k] = m - s;
                    }
                }
            }
        }
        window_offset += ics->window_group_length[g];
    }
}

void apply_is_stereo(CPEInfo *cpe, float * restrict spec_l, float * restrict spec_r)
{
    init_is_tables();
    ICSInfo *ics_r = &cpe->ics[1];

    int window_offset = 0;
    for (int g = 0; g < ics_r->num_window_groups && g < 8; g++) {
        for (int i = 0; i < ics_r->num_sections[g] && i < 64; i++) {
            int cb = ics_r->sect_cb[g][i];
            if (cb == 14 || cb == 15) {
                int start_sfb = ics_r->sect_start[g][i];
                int end_sfb = ics_r->sect_end[g][i];

                for (int sfb = start_sfb; sfb < end_sfb && sfb < 64; sfb++) {
                    int pos = ics_r->scalefactors[g][sfb];
                    if (pos < -IS_POS_RANGE) pos = -IS_POS_RANGE;
                    if (pos > IS_POS_RANGE) pos = IS_POS_RANGE;
                    float scale = is_scale_lut[pos + IS_POS_RANGE];
                    if (cb == 14) scale = -scale; /* INTENSITY_HCB2: out of phase */
                    /* §4.6.8.2.3: an ms_used flag on an intensity band flips its sign. */
                    if (cpe->ms_mask_present == 2 || (cpe->ms_mask_present == 1 && cpe->ms_used[g][sfb]))
                        scale = -scale;

                    int start_k = ics_r->sfb_offsets[sfb];
                    int end_k = ics_r->sfb_offsets[sfb + 1];
                    if (start_k >= FRAME_LEN_LONG) continue;
                    if (end_k > FRAME_LEN_LONG) end_k = FRAME_LEN_LONG;
                    int len = end_k - start_k;

                    for (int w = 0; w < ics_r->window_group_length[g]; w++) {
                        int win_idx = window_offset + w;
                        const float * restrict l_ptr = spec_l + win_idx * 128 + start_k;
                        float * restrict r_ptr = spec_r + win_idx * 128 + start_k;

                        for (int k = 0; k < len; k++) {
                            r_ptr[k] = l_ptr[k] * scale;
                        }
                    }
                }
            }
        }
        window_offset += ics_r->window_group_length[g];
    }
}

void apply_freq_downmix_mono(float *spec_l, const float *spec_r)
{
    for (int i = 0; i < FRAME_LEN_LONG; i++) {
        spec_l[i] = 0.5f * (spec_l[i] + spec_r[i]);
    }
}

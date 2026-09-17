/*
 * Mid/Side and Intensity Stereo Decoding
 */

#include "faad_internal.h"

void apply_ms_stereo(CPEInfo *cpe, float *spec_l, float *spec_r)
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

            if (ms_flag && !ics->pns_used[g][sfb]) {
                int start_k = ics->sfb_offsets[sfb];
                int end_k = ics->sfb_offsets[sfb + 1];

                for (int w = 0; w < ics->window_group_length[g]; w++) {
                    int win_idx = window_offset + w;
                    float *l_ptr = spec_l + win_idx * 128 + start_k;
                    float *r_ptr = spec_r + win_idx * 128 + start_k;

                    for (int k = start_k; k < end_k && (start_k + (k - start_k)) < FRAME_LEN_LONG; k++) {
                        float m = *l_ptr;
                        float s = *r_ptr;
                        *l_ptr++ = m + s;
                        *r_ptr++ = m - s;
                    }
                }
            }
        }
        window_offset += ics->window_group_length[g];
    }
}

void apply_is_stereo(CPEInfo *cpe, float *spec_l, float *spec_r)
{
    ICSInfo *ics_r = &cpe->ics[1];

    int window_offset = 0;
    for (int g = 0; g < ics_r->num_window_groups && g < 8; g++) {
        for (int i = 0; i < ics_r->num_sections[g] && i < 64; i++) {
            int cb = ics_r->sect_cb[g][i];
            if (cb == 14 || cb == 15) {
                int start_sfb = ics_r->sect_start[g][i];
                int end_sfb = ics_r->sect_end[g][i];

                for (int sfb = start_sfb; sfb < end_sfb && sfb < 64; sfb++) {
                    int sf = ics_r->scalefactors[g][sfb];
                    float scale = powf(0.5f, 0.25f * sf);
                    if (cb == 15) scale = -scale;

                    int start_k = ics_r->sfb_offsets[sfb];
                    int end_k = ics_r->sfb_offsets[sfb + 1];

                    for (int w = 0; w < ics_r->window_group_length[g]; w++) {
                        int win_idx = window_offset + w;
                        float *l_ptr = spec_l + win_idx * 128 + start_k;
                        float *r_ptr = spec_r + win_idx * 128 + start_k;

                        for (int k = start_k; k < end_k && (start_k + (k - start_k)) < FRAME_LEN_LONG; k++) {
                            *r_ptr++ = (*l_ptr++) * scale;
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

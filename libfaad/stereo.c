/*
 * Mid/Side and Intensity Stereo Decoding
 */

#include "faad_internal.h"

/* 0.5^(is_position/4) for is_position in [-IS_POS_RANGE, IS_POS_RANGE]. */
#define IS_POS_RANGE 128
static const float is_scale_lut[2 * IS_POS_RANGE + 1] = {
    4.294967296e+09f, 3.611622603e+09f, 3.037000500e+09f, 2.553802834e+09f, 2.147483648e+09f, 1.805811301e+09f, 1.518500250e+09f, 1.276901417e+09f,
    1.073741824e+09f, 9.029056507e+08f, 7.592501250e+08f, 6.384507084e+08f, 5.368709120e+08f, 4.514528254e+08f, 3.796250625e+08f, 3.192253542e+08f,
    2.684354560e+08f, 2.257264127e+08f, 1.898125312e+08f, 1.596126771e+08f, 1.342177280e+08f, 1.128632063e+08f, 9.490626562e+07f, 7.980633855e+07f,
    6.710886400e+07f, 5.643160317e+07f, 4.745313281e+07f, 3.990316927e+07f, 3.355443200e+07f, 2.821580158e+07f, 2.372656641e+07f, 1.995158464e+07f,
    1.677721600e+07f, 1.410790079e+07f, 1.186328320e+07f, 9.975792319e+06f, 8.388608000e+06f, 7.053950396e+06f, 5.931641602e+06f, 4.987896159e+06f,
    4.194304000e+06f, 3.526975198e+06f, 2.965820801e+06f, 2.493948080e+06f, 2.097152000e+06f, 1.763487599e+06f, 1.482910400e+06f, 1.246974040e+06f,
    1.048576000e+06f, 8.817437995e+05f, 7.414552002e+05f, 6.234870199e+05f, 5.242880000e+05f, 4.408718998e+05f, 3.707276001e+05f, 3.117435100e+05f,
    2.621440000e+05f, 2.204359499e+05f, 1.853638000e+05f, 1.558717550e+05f, 1.310720000e+05f, 1.102179749e+05f, 9.268190002e+04f, 7.793587749e+04f,
    6.553600000e+04f, 5.510898747e+04f, 4.634095001e+04f, 3.896793874e+04f, 3.276800000e+04f, 2.755449374e+04f, 2.317047501e+04f, 1.948396937e+04f,
    1.638400000e+04f, 1.377724687e+04f, 1.158523750e+04f, 9.741984686e+03f, 8.192000000e+03f, 6.888623434e+03f, 5.792618751e+03f, 4.870992343e+03f,
    4.096000000e+03f, 3.444311717e+03f, 2.896309376e+03f, 2.435496172e+03f, 2.048000000e+03f, 1.722155858e+03f, 1.448154688e+03f, 1.217748086e+03f,
    1.024000000e+03f, 8.610779292e+02f, 7.240773439e+02f, 6.088740429e+02f, 5.120000000e+02f, 4.305389646e+02f, 3.620386720e+02f, 3.044370214e+02f,
    2.560000000e+02f, 2.152694823e+02f, 1.810193360e+02f, 1.522185107e+02f, 1.280000000e+02f, 1.076347412e+02f, 9.050966799e+01f, 7.610925536e+01f,
    6.400000000e+01f, 5.381737058e+01f, 4.525483400e+01f, 3.805462768e+01f, 3.200000000e+01f, 2.690868529e+01f, 2.262741700e+01f, 1.902731384e+01f,
    1.600000000e+01f, 1.345434264e+01f, 1.131370850e+01f, 9.513656920e+00f, 8.000000000e+00f, 6.727171322e+00f, 5.656854249e+00f, 4.756828460e+00f,
    4.000000000e+00f, 3.363585661e+00f, 2.828427125e+00f, 2.378414230e+00f, 2.000000000e+00f, 1.681792831e+00f, 1.414213562e+00f, 1.189207115e+00f,
    1.000000000e+00f, 8.408964153e-01f, 7.071067812e-01f, 5.946035575e-01f, 5.000000000e-01f, 4.204482076e-01f, 3.535533906e-01f, 2.973017788e-01f,
    2.500000000e-01f, 2.102241038e-01f, 1.767766953e-01f, 1.486508894e-01f, 1.250000000e-01f, 1.051120519e-01f, 8.838834765e-02f, 7.432544469e-02f,
    6.250000000e-02f, 5.255602595e-02f, 4.419417382e-02f, 3.716272234e-02f, 3.125000000e-02f, 2.627801298e-02f, 2.209708691e-02f, 1.858136117e-02f,
    1.562500000e-02f, 1.313900649e-02f, 1.104854346e-02f, 9.290680586e-03f, 7.812500000e-03f, 6.569503244e-03f, 5.524271728e-03f, 4.645340293e-03f,
    3.906250000e-03f, 3.284751622e-03f, 2.762135864e-03f, 2.322670146e-03f, 1.953125000e-03f, 1.642375811e-03f, 1.381067932e-03f, 1.161335073e-03f,
    9.765625000e-04f, 8.211879055e-04f, 6.905339660e-04f, 5.806675366e-04f, 4.882812500e-04f, 4.105939528e-04f, 3.452669830e-04f, 2.903337683e-04f,
    2.441406250e-04f, 2.052969764e-04f, 1.726334915e-04f, 1.451668842e-04f, 1.220703125e-04f, 1.026484882e-04f, 8.631674575e-05f, 7.258344208e-05f,
    6.103515625e-05f, 5.132424410e-05f, 4.315837288e-05f, 3.629172104e-05f, 3.051757812e-05f, 2.566212205e-05f, 2.157918644e-05f, 1.814586052e-05f,
    1.525878906e-05f, 1.283106102e-05f, 1.078959322e-05f, 9.072930260e-06f, 7.629394531e-06f, 6.415530512e-06f, 5.394796609e-06f, 4.536465130e-06f,
    3.814697266e-06f, 3.207765256e-06f, 2.697398305e-06f, 2.268232565e-06f, 1.907348633e-06f, 1.603882628e-06f, 1.348699152e-06f, 1.134116282e-06f,
    9.536743164e-07f, 8.019413140e-07f, 6.743495762e-07f, 5.670581412e-07f, 4.768371582e-07f, 4.009706570e-07f, 3.371747881e-07f, 2.835290706e-07f,
    2.384185791e-07f, 2.004853285e-07f, 1.685873940e-07f, 1.417645353e-07f, 1.192092896e-07f, 1.002426642e-07f, 8.429369702e-08f, 7.088226765e-08f,
    5.960464478e-08f, 5.012133212e-08f, 4.214684851e-08f, 3.544113383e-08f, 2.980232239e-08f, 2.506066606e-08f, 2.107342426e-08f, 1.772056691e-08f,
    1.490116119e-08f, 1.253033303e-08f, 1.053671213e-08f, 8.860283457e-09f, 7.450580597e-09f, 6.265166516e-09f, 5.268356064e-09f, 4.430141728e-09f,
    3.725290298e-09f, 3.132583258e-09f, 2.634178032e-09f, 2.215070864e-09f, 1.862645149e-09f, 1.566291629e-09f, 1.317089016e-09f, 1.107535432e-09f,
    9.313225746e-10f, 7.831458144e-10f, 6.585445080e-10f, 5.537677160e-10f, 4.656612873e-10f, 3.915729072e-10f, 3.292722540e-10f, 2.768838580e-10f,
    2.328306437e-10f,
};

void apply_ms_stereo(CPEInfo *cpe, float * restrict spec_l, float * restrict spec_r
#ifdef FAAD_STATS
    , FaadDecStats *stats
#endif
)
{
    ICSInfo *ics = &cpe->ics[0];

    int window_offset = 0;
    for (int g = 0; g < ics->num_window_groups && g < 8; g++) {
        for (int sfb = 0; sfb < ics->max_sfb && sfb < MAX_SFB; sfb++) {
            bool ms_flag = false;
            if (cpe->ms_mask_present == 1) {
                ms_flag = (cpe->ms_used[g][sfb] != 0);
            } else if (cpe->ms_mask_present != 0) {
                ms_flag = true;
            }

            if (!ms_flag) continue;
            bool pns_l = ics->sfb_cb[g][sfb] == 13, pns_r = cpe->ics[1].sfb_cb[g][sfb] == 13;
            if (pns_l != pns_r) continue;

#ifdef FAAD_STATS
            /* Counted per channel slot, same granularity as totalBands, so
             * msBands/totalBands lines up with libfaac's own ratio. */
            stats->msBands += 2;
#endif

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
                    float gain = get_sf_scale(cpe->ics[1].scalefactors[g][sfb] - ics->scalefactors[g][sfb] + 100);
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
    ICSInfo *ics_r = &cpe->ics[1];

    int window_offset = 0;
    for (int g = 0; g < ics_r->num_window_groups && g < 8; g++) {
        for (int sfb = 0; sfb < ics_r->max_sfb && sfb < MAX_SFB; sfb++) {
            int cb = ics_r->sfb_cb[g][sfb];
            if (cb == 14 || cb == 15) {
                {
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

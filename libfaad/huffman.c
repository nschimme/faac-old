/*
 * Huffman decoder for spectral coefficients and scalefactors
 */

#include "faad_internal.h"
#include "sfb_tables.h"

void setup_sfb_offsets(ICSInfo *ics, uint32_t sample_rate)
{
    memset(ics->sfb_offsets, 0, sizeof(ics->sfb_offsets));
    int sr_idx = get_sr_index(sample_rate);
    if (ics->window_sequence == EIGHT_SHORT_SEQUENCE) {
        ics->num_sfbs = num_sfbs_128[sr_idx];
        const uint16_t *offsets = sfb_offsets_128[sr_idx];
        for (int i = 0; i <= ics->num_sfbs && i < 68; i++) {
            ics->sfb_offsets[i] = offsets[i];
        }
    } else {
        ics->num_sfbs = num_sfbs_1024[sr_idx];
        const uint16_t *offsets = sfb_offsets_1024[sr_idx];
        for (int i = 0; i <= ics->num_sfbs && i < 68; i++) {
            ics->sfb_offsets[i] = offsets[i];
        }
    }
}

static const hcode16_t * const huffbook_tables[] = {
    NULL, book01, book02, book03, book04, book05, book06, book07, book08, book09, book10, book11
};

static const uint16_t huffbook_sizes[] = {
    0, 81, 81, 81, 81, 81, 81, 64, 64, 169, 169, 289
};


/* Direct 11-bit LUT entry: bits 0..3 = len (0..11), bits 4..15 = symbol index (0..288) */
typedef uint16_t HuffLutEntry;

static HuffLutEntry huff_lut_11bit[13][2048];

typedef struct {
    uint8_t len;
    uint16_t data;
    uint16_t sym;
} HuffEscEntry;

static HuffEscEntry huff_esc_table[13][64];
static uint8_t huff_esc_count[13];

static bool huff_luts_initialized = false;

void init_huffman_luts(void)
{
    if (huff_luts_initialized) return;

    for (int b = 1; b <= 11; b++) {
        const hcode16_t *table = huffbook_tables[b];
        int size = huffbook_sizes[b];
        if (!table) continue;

        for (int cw = 0; cw < 2048; cw++) {
            huff_lut_11bit[b][cw] = 0;

            for (uint32_t len = 1; len <= 11; len++) {
                uint32_t prefix = cw >> (11 - len);
                for (int i = 0; i < size; i++) {
                    if (table[i].len == len && table[i].data == prefix) {
                        huff_lut_11bit[b][cw] = (uint16_t)(len | ((uint32_t)i << 4));
                        goto found_sym;
                    }
                }
            }
            found_sym:;
        }

        /* Build compact escape table for codewords >= 12 bits */
        huff_esc_count[b] = 0;
        for (int i = 0; i < size; i++) {
            if (table[i].len >= 12 && huff_esc_count[b] < 64) {
                huff_esc_table[b][huff_esc_count[b]].len = (uint8_t)table[i].len;
                huff_esc_table[b][huff_esc_count[b]].data = table[i].data;
                huff_esc_table[b][huff_esc_count[b]].sym = (uint16_t)i;
                huff_esc_count[b]++;
            }
        }
    }

    /* Book 12 (Scalefactors) LUT */
    for (int cw = 0; cw < 2048; cw++) {
        huff_lut_11bit[12][cw] = 0;

        for (uint32_t len = 1; len <= 11; len++) {
            uint32_t prefix = cw >> (11 - len);
            for (int i = 0; i < 121; i++) {
                if (book12[i].len == len && book12[i].data == prefix) {
                    huff_lut_11bit[12][cw] = (uint16_t)(len | ((uint32_t)i << 4));
                    goto found_sf;
                }
            }
        }
        found_sf:;
    }

    huff_esc_count[12] = 0;
    for (int i = 0; i < 121; i++) {
        if (book12[i].len >= 12 && huff_esc_count[12] < 64) {
            huff_esc_table[12][huff_esc_count[12]].len = (uint8_t)book12[i].len;
            huff_esc_table[12][huff_esc_count[12]].data = book12[i].data;
            huff_esc_table[12][huff_esc_count[12]].sym = (uint16_t)i;
            huff_esc_count[12]++;
        }
    }

    huff_luts_initialized = true;
}

static inline int decode_huffman_symbol(BitReader *bs, int book)
{
    if (book < 1 || book > 11) return 0;

    uint32_t cw11 = bits_show(bs, 11);
    HuffLutEntry lut = huff_lut_11bit[book][cw11];
    uint32_t len = lut & 0x0F;
    if (len > 0) {
        bits_skip(bs, len);
        return (int)(lut >> 4);
    }

    int esc_cnt = huff_esc_count[book];
    const HuffEscEntry *esc_tab = huff_esc_table[book];
    for (int i = 0; i < esc_cnt; i++) {
        uint32_t l = esc_tab[i].len;
        if (bits_show(bs, l) == esc_tab[i].data) {
            bits_skip(bs, l);
            return esc_tab[i].sym;
        }
    }
    return 0;
}

static inline int decode_huffman_scalefactor(BitReader *bs)
{
    uint32_t cw11 = bits_show(bs, 11);
    HuffLutEntry lut = huff_lut_11bit[12][cw11];
    uint32_t len = lut & 0x0F;
    if (len > 0) {
        bits_skip(bs, len);
        return (int)(lut >> 4);
    }

    int esc_cnt = huff_esc_count[12];
    const HuffEscEntry *esc_tab = huff_esc_table[12];
    for (int i = 0; i < esc_cnt; i++) {
        uint32_t l = esc_tab[i].len;
        if (bits_show(bs, l) == esc_tab[i].data) {
            bits_skip(bs, l);
            return esc_tab[i].sym;
        }
    }
    return 0;
}


static inline void decode_quad(BitReader *bs, int book, int *v, int *w, int *x, int *y)
{
    int idx = decode_huffman_symbol(bs, book);
    *v = idx / 27;
    idx %= 27;
    *w = idx / 9;
    idx %= 9;
    *x = idx / 3;
    *y = idx % 3;

    if (book == 1 || book == 2) {
        /* Signed 4-tuple: values in {-1, 0, 1} */
        *v -= 1; *w -= 1; *x -= 1; *y -= 1;
    } else if (book == 3 || book == 4) {
        /* Unsigned 4-tuple: read sign bit for non-zero values */
        if (*v) if (bits_get(bs, 1)) *v = -*v;
        if (*w) if (bits_get(bs, 1)) *w = -*w;
        if (*x) if (bits_get(bs, 1)) *x = -*x;
        if (*y) if (bits_get(bs, 1)) *y = -*y;
    }
}

static inline void decode_pair(BitReader *bs, int book, int *x, int *y)
{
    int idx = decode_huffman_symbol(bs, book);
    int base = 17;
    if (book == 5 || book == 6) base = 9;
    else if (book == 7 || book == 8) base = 8;
    else if (book == 9 || book == 10) base = 13;

    *x = idx / base;
    *y = idx % base;

    if (book == 6) {
        /* Codebook 6: Signed 2-tuple in [-4, 4] directly encoded via offset +4 */
        *x -= 4;
        *y -= 4;
    } else if (book == 11) {
        /* Codebook 11 (ESCBOOK): decode escape sequence FIRST for max magnitude (16) */
        int abs_x = *x;
        int abs_y = *y;
        if (abs_x == 16) {
            int prefix = 0;
            while (bits_get(bs, 1) == 1) prefix++;
            abs_x = (1 << (prefix + 4)) + bits_get(bs, prefix + 4);
        }
        if (abs_y == 16) {
            int prefix = 0;
            while (bits_get(bs, 1) == 1) prefix++;
            abs_y = (1 << (prefix + 4)) + bits_get(bs, prefix + 4);
        }
        /* Read sign bits AFTER escape sequence per ISO/IEC 14496-3 Section 4.6.3 */
        if (abs_x) {
            if (bits_get(bs, 1)) abs_x = -abs_x;
        }
        if (abs_y) {
            if (bits_get(bs, 1)) abs_y = -abs_y;
        }
        *x = abs_x;
        *y = abs_y;
    } else if (book == 5 || (book >= 7 && book <= 10)) {
        /* Unsigned 2-tuple: read sign bit for non-zero values */
        if (*x) if (bits_get(bs, 1)) *x = -*x;
        if (*y) if (bits_get(bs, 1)) *y = -*y;
    }
}

faad_status decode_scale_factor_data(BitReader *bs, ICSInfo *ics, uint32_t sample_rate)
{
    setup_sfb_offsets(ics, sample_rate);

    int sf = ics->global_gain;
    int is_pos = 0;
    int pns_energy = sf - 60;
    bool is_first_pns = true;

    for (int g = 0; g < ics->num_window_groups && g < 8; g++) {
        for (int i = 0; i < ics->num_sections[g] && i < 64; i++) {
            int cb = ics->sect_cb[g][i];
            int start_sfb = ics->sect_start[g][i];
            int end_sfb = ics->sect_end[g][i];

            if (cb == 0) {
                continue;
            } else if (cb == 13) { /* PNS */
                for (int sfb = start_sfb; sfb < end_sfb && sfb < ics->num_sfbs && sfb < 64; sfb++) {
                    if (is_first_pns) {
                        pns_energy = (int)bits_get(bs, 9) - 256;
                        is_first_pns = false;
                    } else {
                        int dpns = decode_huffman_scalefactor(bs);
                        pns_energy += dpns - 60;
                    }
                    ics->scalefactors[g][sfb] = pns_energy;
                    ics->pns_used[g][sfb] = true;
                }
            } else if (cb == 14 || cb == 15) { /* Intensity stereo */
                for (int sfb = start_sfb; sfb < end_sfb && sfb < ics->num_sfbs && sfb < 64; sfb++) {
                    int dis = decode_huffman_scalefactor(bs);
                    is_pos += dis - 60;
                    ics->scalefactors[g][sfb] = is_pos;
                }
            } else {
                for (int sfb = start_sfb; sfb < end_sfb && sfb < ics->num_sfbs && sfb < 64; sfb++) {
                    int dsf = decode_huffman_scalefactor(bs);
                    sf += dsf - 60;
                    ics->scalefactors[g][sfb] = sf;
                    ics->sfb_cb[g][sfb] = cb;
                }
            }
        }
    }
    return FAAD_OK;
}

faad_status decode_spectral_data(BitReader *bs, ICSInfo *ics, float *spec)
{
    int window_offset = 0;
    for (int g = 0; g < ics->num_window_groups && g < 8; g++) {
        for (int i = 0; i < ics->num_sections[g] && i < 64; i++) {
            int cb = ics->sect_cb[g][i];
            int start_sfb = ics->sect_start[g][i];
            int end_sfb = ics->sect_end[g][i];

            if (cb == 0 || cb == 13 || cb == 14 || cb == 15) continue;

            for (int sfb = start_sfb; sfb < end_sfb && sfb < ics->num_sfbs && sfb < 64; sfb++) {
                int start_k = ics->sfb_offsets[sfb];
                int end_k = ics->sfb_offsets[sfb + 1];
                if (start_k >= FRAME_LEN_LONG) continue;
                if (end_k > FRAME_LEN_LONG) end_k = FRAME_LEN_LONG;

                for (int w = 0; w < ics->window_group_length[g]; w++) {
                    float *ptr = spec + (window_offset + w) * 128 + start_k;
                    int k = start_k;
                    while (k < end_k) {
                        if (cb <= 4) {
                            int v, w_val, x, y;
                            decode_quad(bs, cb, &v, &w_val, &x, &y);
                            ptr[0] = (float)v;
                            ptr[1] = (float)w_val;
                            ptr[2] = (float)x;
                            ptr[3] = (float)y;
                            ptr += 4;
                            k += 4;
                        } else {
                            int x, y;
                            decode_pair(bs, cb, &x, &y);
                            ptr[0] = (float)x;
                            ptr[1] = (float)y;
                            ptr += 2;
                            k += 2;
                        }
                    }
                }
            }
        }
        window_offset += ics->window_group_length[g];
    }
    return FAAD_OK;
}

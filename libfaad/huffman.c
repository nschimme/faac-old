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

static HuffLutEntry huff_lut_11bit[12][2048];

typedef struct {
    uint8_t len;
    uint16_t data;
    uint16_t sym;
} HuffEscEntry;

/* Codewords >= 12 bits fall through the direct 11-bit LUT into this table,
 * searched linearly. 128 slots covers the real maximum (88) with headroom. */
#define HUFF_ESC_TABLE_CAP 128
static HuffEscEntry huff_esc_table[12][HUFF_ESC_TABLE_CAP];
static uint8_t huff_esc_count[12];

static int8_t quad_lut[81][4];
static const int book_base[12] = { 0, 81, 81, 81, 81, 9, 9, 8, 8, 13, 13, 17 };

static bool huff_luts_initialized = false;

void init_huffman_luts(void)
{
    if (huff_luts_initialized) return;

    for (int i = 0; i < 81; i++) {
        int idx = i;
        quad_lut[i][0] = (int8_t)(idx / 27);
        idx %= 27;
        quad_lut[i][1] = (int8_t)(idx / 9);
        idx %= 9;
        quad_lut[i][2] = (int8_t)(idx / 3);
        quad_lut[i][3] = (int8_t)(idx % 3);
    }

    for (int b = 1; b <= 11; b++) {
        int b_idx = b - 1;
        const hcode16_t *table = huffbook_tables[b];
        int size = huffbook_sizes[b];
        if (!table) continue;

        for (int cw = 0; cw < 2048; cw++) {
            huff_lut_11bit[b_idx][cw] = 0;

            for (uint32_t len = 1; len <= 11; len++) {
                uint32_t prefix = cw >> (11 - len);
                for (int i = 0; i < size; i++) {
                    if (table[i].len == len && table[i].data == prefix) {
                        huff_lut_11bit[b_idx][cw] = (uint16_t)(len | ((uint32_t)i << 4));
                        goto found_sym;
                    }
                }
            }
            found_sym:;
        }

        /* Build compact escape table for codewords >= 12 bits */
        huff_esc_count[b_idx] = 0;
        for (int i = 0; i < size; i++) {
            if (table[i].len >= 12 && huff_esc_count[b_idx] < HUFF_ESC_TABLE_CAP) {
                huff_esc_table[b_idx][huff_esc_count[b_idx]].len = (uint8_t)table[i].len;
                huff_esc_table[b_idx][huff_esc_count[b_idx]].data = table[i].data;
                huff_esc_table[b_idx][huff_esc_count[b_idx]].sym = (uint16_t)i;
                huff_esc_count[b_idx]++;
            }
        }
    }

    /* Book 12 (Scalefactors) LUT mapped to b_idx = 11 */
    int b12_idx = 11;
    for (int cw = 0; cw < 2048; cw++) {
        huff_lut_11bit[b12_idx][cw] = 0;

        for (uint32_t len = 1; len <= 11; len++) {
            uint32_t prefix = cw >> (11 - len);
            for (int i = 0; i < 121; i++) {
                if (book12[i].len == len && book12[i].data == prefix) {
                    huff_lut_11bit[b12_idx][cw] = (uint16_t)(len | ((uint32_t)i << 4));
                    goto found_sf;
                }
            }
        }
        found_sf:;
    }

    huff_esc_count[b12_idx] = 0;
    for (int i = 0; i < 121; i++) {
        if (book12[i].len >= 12 && huff_esc_count[b12_idx] < HUFF_ESC_TABLE_CAP) {
            huff_esc_table[b12_idx][huff_esc_count[b12_idx]].len = (uint8_t)book12[i].len;
            huff_esc_table[b12_idx][huff_esc_count[b12_idx]].data = book12[i].data;
            huff_esc_table[b12_idx][huff_esc_count[b12_idx]].sym = (uint16_t)i;
            huff_esc_count[b12_idx]++;
        }
    }

    huff_luts_initialized = true;
}

static inline int decode_huffman_symbol(BitReader *bs, int book
#ifdef FAAD_STATS
    , FaadDecStats *stats
#endif
)
{
    if (book < 1 || book > 11) return 0;
    int b_idx = book - 1;

    uint32_t cw11 = bits_show(bs, 11);
    HuffLutEntry lut = huff_lut_11bit[b_idx][cw11];
    uint32_t len = lut & 0x0F;
    if (len > 0) {
        bits_skip(bs, len);
        return (int)(lut >> 4);
    }

    int esc_cnt = huff_esc_count[b_idx];
    const HuffEscEntry *esc_tab = huff_esc_table[b_idx];
    for (int i = 0; i < esc_cnt; i++) {
        uint32_t l = esc_tab[i].len;
        if (bits_show(bs, l) == esc_tab[i].data) {
            bits_skip(bs, l);
#ifdef FAAD_STATS
            if (stats) stats->huffEscapeHits[book]++;
#endif
            return esc_tab[i].sym;
        }
    }
#ifdef FAAD_STATS
    if (stats) stats->huffEscapeMisses++;
#endif
    return 0;
}

static inline int decode_huffman_scalefactor(BitReader *bs
#ifdef FAAD_STATS
    , FaadDecStats *stats
#endif
)
{
    int b12_idx = 11;
    uint32_t cw11 = bits_show(bs, 11);
    HuffLutEntry lut = huff_lut_11bit[b12_idx][cw11];
    uint32_t len = lut & 0x0F;
    if (len > 0) {
        bits_skip(bs, len);
        return (int)(lut >> 4);
    }

    int esc_cnt = huff_esc_count[b12_idx];
    const HuffEscEntry *esc_tab = huff_esc_table[b12_idx];
    for (int i = 0; i < esc_cnt; i++) {
        uint32_t l = esc_tab[i].len;
        if (bits_show(bs, l) == esc_tab[i].data) {
            bits_skip(bs, l);
#ifdef FAAD_STATS
            if (stats) stats->huffEscapeHits[12]++;
#endif
            return esc_tab[i].sym;
        }
    }
#ifdef FAAD_STATS
    if (stats) stats->huffEscapeMisses++;
#endif
    return 0;
}


/* Convenience wrappers so call sites don't need to spell out the #ifdef at
 * every call -- they assume a `stats` variable (possibly NULL) is in scope
 * under FAAD_STATS, matching the parameter name used throughout this file. */
#ifdef FAAD_STATS
#define DECODE_HUFF_SF(bs) decode_huffman_scalefactor((bs), stats)
#else
#define DECODE_HUFF_SF(bs) decode_huffman_scalefactor((bs))
#endif

static inline void decode_quad(BitReader *bs, int book, int *v, int *w, int *x, int *y
#ifdef FAAD_STATS
    , FaadDecStats *stats
#endif
)
{
    int idx = decode_huffman_symbol(bs, book
#ifdef FAAD_STATS
        , stats
#endif
    );
    if (idx < 0) idx = 0;
    if (idx > 80) idx = 80;

    const int8_t *q = quad_lut[idx];
    int v_val = q[0];
    int w_val = q[1];
    int x_val = q[2];
    int y_val = q[3];

    if (book <= 2) {
        /* Signed 4-tuple: values in {-1, 0, 1} */
        *v = v_val - 1; *w = w_val - 1; *x = x_val - 1; *y = y_val - 1;
    } else {
        /* Unsigned 4-tuple: read sign bit for non-zero values */
        if (v_val) if (bits_get_1(bs)) v_val = -v_val;
        if (w_val) if (bits_get_1(bs)) w_val = -w_val;
        if (x_val) if (bits_get_1(bs)) x_val = -x_val;
        if (y_val) if (bits_get_1(bs)) y_val = -y_val;
        *v = v_val; *w = w_val; *x = x_val; *y = y_val;
    }
}

static inline void decode_pair(BitReader *bs, int book, int *x, int *y
#ifdef FAAD_STATS
    , FaadDecStats *stats
#endif
)
{
    int idx = decode_huffman_symbol(bs, book
#ifdef FAAD_STATS
        , stats
#endif
    );
    int base = (book >= 1 && book <= 11) ? book_base[book] : 17;

    *x = idx / base;
    *y = idx % base;

    if (book == 6) {
        /* Codebook 6: Signed 2-tuple in [-4, 4] directly encoded via offset +4 */
        *x -= 4;
        *y -= 4;
    } else if (book == 11) {
        /* Codebook 11 (ESCBOOK): sign bits for both values come immediately
         * after the Huffman codeword -- matching how libfaac's encoder
         * actually packs them (HCB_ESC in huff2.c appends both sign bits to
         * the codeword's own bit pattern before separately appending any
         * escape-sequence data) -- not after the escape sequence. Reading
         * escape data first only desyncs when a value actually needs
         * escaping (magnitude >= 16), which is why this went unnoticed on
         * quieter content. */
        int abs_x = *x;
        int abs_y = *y;
        bool neg_x = abs_x && bits_get_1(bs);
        bool neg_y = abs_y && bits_get_1(bs);

        if (abs_x == 16) {
#ifdef FAAD_STATS
            if (stats) stats->escbookMagnitudeEscapes++;
#endif
            int prefix = 0;
            while (bits_get_1(bs) == 1) prefix++;
            abs_x = (1 << (prefix + 4)) + bits_get_fast(bs, prefix + 4);
        }
        if (abs_y == 16) {
#ifdef FAAD_STATS
            if (stats) stats->escbookMagnitudeEscapes++;
#endif
            int prefix = 0;
            while (bits_get_1(bs) == 1) prefix++;
            abs_y = (1 << (prefix + 4)) + bits_get_fast(bs, prefix + 4);
        }
        *x = neg_x ? -abs_x : abs_x;
        *y = neg_y ? -abs_y : abs_y;
    } else if (book == 5 || (book >= 7 && book <= 10)) {
        /* Unsigned 2-tuple: read sign bit for non-zero values */
        if (*x) if (bits_get_1(bs)) *x = -*x;
        if (*y) if (bits_get_1(bs)) *y = -*y;
    }
}

faad_status decode_scale_factor_data(BitReader *bs, ICSInfo *ics, uint32_t sample_rate
#ifdef FAAD_STATS
    , FaadDecStats *stats
#endif
)
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
                for (int sfb = start_sfb; sfb < end_sfb && sfb < ics->max_sfb && sfb < ics->num_sfbs && sfb < 64; sfb++) {
                    if (is_first_pns) {
                        pns_energy = (int)bits_get(bs, 9) - 256 + (sf - 90);
                        is_first_pns = false;
                    } else {
                        int dpns = DECODE_HUFF_SF(bs);
                        pns_energy += dpns - 60;
                    }
                    if (pns_energy < 0) pns_energy = 0;
                    if (pns_energy > 255) pns_energy = 255;
                    ics->scalefactors[g][sfb] = pns_energy;
                    ics->pns_used[g][sfb] = true;
                }
            } else if (cb == 14 || cb == 15) { /* Intensity stereo */
                for (int sfb = start_sfb; sfb < end_sfb && sfb < ics->max_sfb && sfb < ics->num_sfbs && sfb < 64; sfb++) {
                    int dis = DECODE_HUFF_SF(bs);
                    is_pos += dis - 60;
                    if (is_pos < 0) is_pos = 0;
                    if (is_pos > 255) is_pos = 255;
                    ics->scalefactors[g][sfb] = is_pos;
                }
            } else {
                for (int sfb = start_sfb; sfb < end_sfb && sfb < ics->max_sfb && sfb < ics->num_sfbs && sfb < 64; sfb++) {
                    int dsf = DECODE_HUFF_SF(bs);
                    sf += dsf - 60;
                    if (sf < 0) sf = 0;
                    if (sf > 255) sf = 255;
                    ics->scalefactors[g][sfb] = sf;
                    ics->sfb_cb[g][sfb] = cb;
                }
            }
        }
    }
    return FAAD_OK;
}

faad_status decode_spectral_data(BitReader *bs, ICSInfo *ics, float *spec
#ifdef FAAD_STATS
    , FaadDecStats *stats
#endif
)
{
    int window_offset = 0;
    for (int g = 0; g < ics->num_window_groups && g < 8; g++) {
        for (int i = 0; i < ics->num_sections[g] && i < 64; i++) {
            int cb = ics->sect_cb[g][i];
            int start_sfb = ics->sect_start[g][i];
            int end_sfb = ics->sect_end[g][i];

            if (cb == 0 || cb == 13 || cb == 14 || cb == 15) continue;

            for (int sfb = start_sfb; sfb < end_sfb && sfb < ics->max_sfb && sfb < ics->num_sfbs && sfb < 64; sfb++) {
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
                            decode_quad(bs, cb, &v, &w_val, &x, &y
#ifdef FAAD_STATS
                                , stats
#endif
                            );
                            ptr[0] = (float)v;
                            ptr[1] = (float)w_val;
                            ptr[2] = (float)x;
                            ptr[3] = (float)y;
                            ptr += 4;
                            k += 4;
                        } else {
                            int x, y;
                            decode_pair(bs, cb, &x, &y
#ifdef FAAD_STATS
                                , stats
#endif
                            );
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

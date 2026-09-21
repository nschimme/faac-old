/*
 * Huffman decoder for spectral coefficients and scalefactors
 */

#include "faad_internal.h"
#include "sfb_tables.h"

void setup_sfb_offsets(ICSInfo *ics, uint32_t sample_rate)
{
    memset(ics->sfb_offsets, 0, sizeof(ics->sfb_offsets));
    int sr_idx = get_sr_index(sample_rate);
    ics->sample_rate_index = (int8_t)sr_idx;
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

static const hcode16_t * const huffbook_tables[12] = {
    NULL, book01, book02, book03, book04, book05, book06, book07, book08, book09, book10, book11
};
static const uint16_t huffbook_sizes[12] = {
    0, 81, 81, 81, 81, 81, 81, 64, 64, 169, 169, 289
};
/* pair books: index = x * base + y */
static const uint8_t book_base[12] = { 0, 0, 0, 0, 0, 9, 9, 8, 8, 13, 13, 17 };

/* Decoding is a lookup on the next 8 bits, which resolves the short codes
 * that carry most of the symbols directly; a longer code's 8-bit prefix
 * leads to a second table covering the rest of its subtree, so every code
 * takes at most two lookups. A lookup yields the decoded tuple, not a
 * symbol index: four 2-bit magnitudes for a quad book, two 6-bit values
 * for a pair book (the escape magnitude 16 included), the index itself
 * for the scalefactor book. Rows 0..10 are the spectral books 1..11,
 * row 11 the scalefactor book. */
#define HUFF_LUT_BITS 8
#define HUFF_MAX_LEN  19
#define HUFF_SUBTREES 200  /* 8-bit prefixes shared by longer codes, all books */
#define HUFF_SUB_ENTRIES 3206 /* their second-level entries */
typedef uint16_t HuffEntry; /* len (bits beyond the prefix at level two) | tuple << 4; level one: 0 | subtree << 4 */
static HuffEntry huff_lut[12][1 << HUFF_LUT_BITS];
static HuffEntry huff_sub[HUFF_SUB_ENTRIES];
static struct { uint16_t start; uint8_t depth; } huff_subtree[HUFF_SUBTREES];
static bool huff_luts_initialized = false;

static uint32_t huff_tuple(int book, int sym)
{
    if (book == 12) return (uint32_t)sym;
    if (book <= 4) { /* v w x y, base 3 */
        return (uint32_t)((sym / 27) | ((sym / 9 % 3) << 2) | ((sym / 3 % 3) << 4) | ((sym % 3) << 6));
    }
    int base = book_base[book];
    return (uint32_t)((sym / base) | ((sym % base) << 6));
}

void init_huffman_luts(void)
{
    if (huff_luts_initialized) return;
    int n_sub = 0, n_entries = 0;
    for (int book = 1; book <= 12; book++) {
        /* the scalefactor book's codes exceed 16 bits and sit in a wider entry */
        const hcode16_t *tab16 = (book == 12) ? NULL : huffbook_tables[book];
        int n = (book == 12) ? 121 : huffbook_sizes[book];
#define HUFF_LEN(i)  (int)(tab16 ? tab16[i].len : book12[i].len)
#define HUFF_CODE(i) (tab16 ? (uint32_t)tab16[i].data : (uint32_t)book12[i].data)
        HuffEntry *lut = huff_lut[book - 1];
        memset(lut, 0, sizeof(huff_lut[0]));

        /* short codes fill their share of the first level; each longer
         * code's prefix gets a subtree as deep as its longest code */
        for (int i = 0; i < n; i++) {
            int len = HUFF_LEN(i);
            if (len == 0) continue;
            if (len <= HUFF_LUT_BITS) {
                uint32_t start = HUFF_CODE(i) << (HUFF_LUT_BITS - len);
                for (uint32_t k = 0; k < (1U << (HUFF_LUT_BITS - len)); k++)
                    lut[start + k] = (HuffEntry)(len | (huff_tuple(book, i) << 4));
            } else {
                uint32_t prefix = HUFF_CODE(i) >> (len - HUFF_LUT_BITS);
                if (lut[prefix] == 0) {
                    lut[prefix] = (HuffEntry)(n_sub << 4);
                    huff_subtree[n_sub].depth = 0;
                    n_sub++;
                }
                int t = lut[prefix] >> 4;
                if (len - HUFF_LUT_BITS > huff_subtree[t].depth) huff_subtree[t].depth = (uint8_t)(len - HUFF_LUT_BITS);
            }
        }
        for (int i = 0; i < n; i++) {
            int len = HUFF_LEN(i);
            if (len <= HUFF_LUT_BITS) continue;
            uint32_t prefix = HUFF_CODE(i) >> (len - HUFF_LUT_BITS);
            int t = lut[prefix] >> 4;
            if (!(huff_subtree[t].depth & 0x80)) {
                /* first code of this subtree: allocate it */
                huff_subtree[t].start = (uint16_t)n_entries;
                n_entries += 1 << huff_subtree[t].depth;
                memset(huff_sub + huff_subtree[t].start, 0, sizeof(HuffEntry) << huff_subtree[t].depth);
                huff_subtree[t].depth |= 0x80; /* allocated */
            }
            int depth = huff_subtree[t].depth & 0x7F;
            int rest = len - HUFF_LUT_BITS;
            uint32_t tail = HUFF_CODE(i) & ((1U << rest) - 1);
            uint32_t start = huff_subtree[t].start + (tail << (depth - rest));
            for (uint32_t k = 0; k < (1U << (depth - rest)); k++)
                huff_sub[start + k] = (HuffEntry)(rest | (huff_tuple(book, i) << 4));
        }
#undef HUFF_LEN
#undef HUFF_CODE
    }
    for (int t = 0; t < n_sub; t++) huff_subtree[t].depth &= 0x7F;
    huff_luts_initialized = true;
}

/* One codeword of book (1..12): the decoded tuple. */
static inline uint32_t huff_decode(BitReader *bs, int book)
{
    HuffEntry e = huff_lut[book - 1][bits_show_fast(bs, HUFF_LUT_BITS)];
    if (e & 15) {
        bits_skip(bs, e & 15);
        return e >> 4;
    }
    int t = e >> 4, depth = huff_subtree[t].depth;
    uint32_t rest = bits_show(bs, HUFF_LUT_BITS + depth) & ((1U << depth) - 1);
    e = huff_sub[huff_subtree[t].start + rest];
    bits_skip(bs, HUFF_LUT_BITS + (e & 15));
    return e >> 4;
}

#ifdef FAAD_STATS
#define DECODE_HUFF_SF(bs) ((int)huff_decode((bs), 12))
#else
#define DECODE_HUFF_SF(bs) ((int)huff_decode((bs), 12))
#endif

static inline void decode_quad(BitReader *bs, int book, int *v, int *w, int *x, int *y
#ifdef FAAD_STATS
    , FaadDecStats *stats
#endif
)
{
#ifdef FAAD_STATS
    (void)stats;
#endif
    uint32_t t = huff_decode(bs, book);
    int v_val = (int)(t & 3), w_val = (int)((t >> 2) & 3), x_val = (int)((t >> 4) & 3), y_val = (int)((t >> 6) & 3);
    if (book <= 2) {
        /* signed 4-tuple: values in {-1, 0, 1} */
        *v = v_val - 1; *w = w_val - 1; *x = x_val - 1; *y = y_val - 1;
    } else {
        /* unsigned 4-tuple: a sign bit follows for each non-zero value */
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
#ifdef FAAD_STATS
    (void)stats;
#endif
    uint32_t t = huff_decode(bs, book);
    *x = (int)(t & 63);
    *y = (int)(t >> 6);

    if (book == 5 || book == 6) {
        /* signed 2-tuples in [-4, 4], no sign bits */
        *x -= 4;
        *y -= 4;
    } else if (book == 11) {
        /* the sign bits of both values come right after the codeword, the
         * escape sequences (for a magnitude of 16) after those */
        int abs_x = *x, abs_y = *y;
        bool neg_x = abs_x && bits_get_1(bs);
        bool neg_y = abs_y && bits_get_1(bs);
        if (abs_x == 16) {
            int prefix = 0;
            while (bits_get_1(bs) == 1) prefix++;
            abs_x = (1 << (prefix + 4)) + bits_get_fast(bs, prefix + 4);
        }
        if (abs_y == 16) {
            int prefix = 0;
            while (bits_get_1(bs) == 1) prefix++;
            abs_y = (1 << (prefix + 4)) + bits_get_fast(bs, prefix + 4);
        }
        *x = neg_x ? -abs_x : abs_x;
        *y = neg_y ? -abs_y : abs_y;
    } else {
        /* unsigned 2-tuple: a sign bit follows for each non-zero value */
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
    int pns_energy = ics->global_gain - 90; /* §4.6.13.3: noise_nrg starts from global_gain, not the running sf */
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
                        pns_energy += (int)bits_get(bs, 9) - 256;
                        is_first_pns = false;
                    } else {
                        int dpns = DECODE_HUFF_SF(bs);
                        pns_energy += dpns - 60;
                    }
                    ics->scalefactors[g][sfb] = pns_energy;
                    ics->pns_used[g][sfb] = true;
                }
            } else if (cb == 14 || cb == 15) { /* Intensity stereo */
                for (int sfb = start_sfb; sfb < end_sfb && sfb < ics->max_sfb && sfb < ics->num_sfbs && sfb < 64; sfb++) {
                    int dis = DECODE_HUFF_SF(bs);
                    is_pos += dis - 60; /* signed: negative positions boost the right channel */
                    ics->scalefactors[g][sfb] = (int16_t)is_pos;
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
    const uint16_t * restrict sfb_offsets = ics->sfb_offsets;
    int max_sfb = ics->max_sfb < ics->num_sfbs ? ics->max_sfb : ics->num_sfbs;
    if (max_sfb > 64) max_sfb = 64;

    for (int g = 0; g < ics->num_window_groups && g < 8; g++) {
        int win_group_len = ics->window_group_length[g];
        int num_sects = ics->num_sections[g];
        if (num_sects > 64) num_sects = 64;

        for (int i = 0; i < num_sects; i++) {
            int cb = ics->sect_cb[g][i];
            if (cb == 0 || cb == 13 || cb == 14 || cb == 15) continue;

            int start_sfb = ics->sect_start[g][i];
            int end_sfb = ics->sect_end[g][i];
            if (end_sfb > max_sfb) end_sfb = max_sfb;

            for (int sfb = start_sfb; sfb < end_sfb; sfb++) {
                int start_k = sfb_offsets[sfb];
                int end_k = sfb_offsets[sfb + 1];
                if (start_k >= FRAME_LEN_LONG) continue;
                if (end_k > FRAME_LEN_LONG) end_k = FRAME_LEN_LONG;

                for (int w = 0; w < win_group_len; w++) {
                    float * restrict ptr = spec + (window_offset + w) * 128 + start_k;
                    int k = start_k;
                    if (cb <= 4) {
                        while (k < end_k) {
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
                        }
                    } else {
                        while (k < end_k) {
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
        window_offset += win_group_len;
    }
    return FAAD_OK;
}

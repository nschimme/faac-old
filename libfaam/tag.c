/*
 * Stream-based Tagging utilities for libfaam (iTunes ilst metadata atom writer)
 */

#include "libfaam_internal.h"

#define ITUNES_DATA_BINARY 0
#define ITUNES_DATA_TEXT   1
#define ITUNES_DATA_UINT8  0x15
#define ITUNES_DATA_IMAGE_GENERIC 0x0d
#define ITUNES_DATA_IMAGE_JPEG    13
#define ITUNES_DATA_IMAGE_PNG     14

#define FAAM_TAG_MAX_CUSTOM 16

static inline void write_u32(uint8_t *b, uint32_t val) { write_u32_be(b, val); }
static inline void write_u16(uint8_t *b, uint16_t val) { write_u16_be(b, val); }

static uint32_t append_data_box(uint8_t *dst, const char *name, uint32_t type_code, const void *data, size_t len) {
    if (!name || !data || len == 0) return 0;
    uint32_t box_size = 8 + 16 + (uint32_t)len;

    write_u32(dst, box_size);
    memcpy(dst + 4, name, 4);

    write_u32(dst + 8, 16 + (uint32_t)len);
    memcpy(dst + 12, "data", 4);
    write_u32(dst + 16, type_code);
    write_u32(dst + 20, 0);

    memcpy(dst + 24, data, len);
    return box_size;
}

static uint32_t append_text(uint8_t *dst, const char *name, const char *val) {
    if (!val || !val[0]) return 0;
    return append_data_box(dst, name, ITUNES_DATA_TEXT, val, strlen(val));
}

static uint32_t append_u8(uint8_t *dst, const char *name, uint8_t val) {
    return append_data_box(dst, name, ITUNES_DATA_UINT8, &val, 1);
}

static uint32_t append_index(uint8_t *dst, const char *name, uint16_t num, uint16_t total) {
    uint8_t buf[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    write_u16(buf + 2, num);
    write_u16(buf + 4, total);
    return append_data_box(dst, name, ITUNES_DATA_BINARY, buf, sizeof(buf));
}

/* Freeform custom tag: "----" wrapping mean/name/data sub-atoms. Not a simple
 * data box (append_data_box), so it gets its own layout. */
static uint32_t append_ext(uint8_t *dst, const char *mean, const char *name, const char *val) {
    if (!mean || !mean[0] || !name || !name[0] || !val || !val[0]) return 0;

    size_t mean_len = strlen(mean), name_len = strlen(name), val_len = strlen(val);
    uint32_t mean_box_sz = 8 + 4 + (uint32_t)mean_len;
    uint32_t name_box_sz = 8 + 4 + (uint32_t)name_len;
    uint32_t data_box_sz = 8 + 8 + (uint32_t)val_len;
    uint32_t total = 8 + mean_box_sz + name_box_sz + data_box_sz;

    uint8_t *p = dst;
    write_u32(p, total); memcpy(p + 4, "----", 4); p += 8;

    write_u32(p, mean_box_sz); memcpy(p + 4, "mean", 4); write_u32(p + 8, 0);
    memcpy(p + 12, mean, mean_len); p += mean_box_sz;

    write_u32(p, name_box_sz); memcpy(p + 4, "name", 4); write_u32(p + 8, 0);
    memcpy(p + 12, name, name_len); p += name_box_sz;

    write_u32(p, data_box_sz); memcpy(p + 4, "data", 4);
    write_u32(p + 8, ITUNES_DATA_TEXT); write_u32(p + 12, 0);
    memcpy(p + 16, val, val_len);

    return total;
}

/* Same sniff mux.c's put_itunes_data_box uses for "covr" -- kept in sync by
 * hand since the two writers (streaming vs flat-buffer) don't share a byte
 * layer, but the format decision is the same magic-number check either way. */
static uint32_t cover_type_code(const uint8_t *data, uint32_t len) {
    if (len > 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return ITUNES_DATA_IMAGE_PNG;
    if (len > 2 && data[0] == 0xFF && data[1] == 0xD8) return ITUNES_DATA_IMAGE_JPEG;
    return ITUNES_DATA_IMAGE_GENERIC;
}

static uint32_t build_ilst_payload(const faam_metadata *meta, uint8_t *dst)
{
    uint32_t len = 8; /* ilst box header, patched below */

    len += append_text(dst + len, "\251nam", meta->title);
    len += append_text(dst + len, "sonm", meta->title_sort);
    len += append_text(dst + len, "\251ART", meta->artist);
    len += append_text(dst + len, "soar", meta->artist_sort);
    len += append_text(dst + len, "\251alb", meta->album);
    len += append_text(dst + len, "soal", meta->album_sort);
    len += append_text(dst + len, "aART", meta->album_artist);
    len += append_text(dst + len, "soaa", meta->album_artist_sort);
    len += append_text(dst + len, "\251wrt", meta->composer);
    len += append_text(dst + len, "soco", meta->composer_sort);
    len += append_text(dst + len, "\251day", meta->year);
    len += append_text(dst + len, "\251cmt", meta->comment);
    len += append_text(dst + len, "\251too", meta->encoder);

    /* genre_code wins over genre_str when both are set -- it's the iTunes
     * ID3v1-index binary atom ("gnre"), which players special-case; genre_str
     * (free-text "\xa9gen") is the fallback for genres outside that table. */
    if (meta->genre_code) {
        uint8_t g[2];
        write_u16(g, meta->genre_code);
        len += append_data_box(dst + len, "gnre", ITUNES_DATA_BINARY, g, sizeof(g));
    } else {
        len += append_text(dst + len, "\251gen", meta->genre_str);
    }

    if (meta->compilation) len += append_u8(dst + len, "cpil", 1);
    if (meta->track_num) len += append_index(dst + len, "trkn", meta->track_num, meta->track_total);
    if (meta->disc_num) len += append_index(dst + len, "disk", meta->disc_num, meta->disc_total);

    if (meta->cover_art && meta->cover_bytes > 4) {
        len += append_data_box(dst + len, "covr", cover_type_code(meta->cover_art, meta->cover_bytes),
                                meta->cover_art, meta->cover_bytes);
    }

    /* meta->language has no well-established single ilst-level atom -- MP4
     * language is normally per-track (mdhd), not a container-wide tag. Left
     * unserialized rather than guessing at a nonstandard atom layout. */

    uint32_t n_custom = meta->num_custom_tags > FAAM_TAG_MAX_CUSTOM ? FAAM_TAG_MAX_CUSTOM : meta->num_custom_tags;
    for (uint32_t i = 0; i < n_custom; i++) {
        len += append_ext(dst + len, "faac", meta->custom_tags[i].name, meta->custom_tags[i].value);
    }

    write_u32(dst, len);
    memcpy(dst + 4, "ilst", 4);
    return len;
}

/* Upper bound on build_ilst_payload()'s output: ilst header + every simple
 * field's box overhead/content + cover art + custom tags. Cover art and
 * custom tags dominate for any real-world use, so size around those. */
static uint32_t estimate_ilst_capacity(const faam_metadata *meta)
{
    uint32_t cap = 8 + 8192; /* ilst header + all fixed-size text/binary fields, generously */
    if (meta->cover_art) cap += meta->cover_bytes + 64;
    uint32_t n_custom = meta->num_custom_tags > FAAM_TAG_MAX_CUSTOM ? FAAM_TAG_MAX_CUSTOM : meta->num_custom_tags;
    cap += n_custom * (uint32_t)(sizeof(((faam_custom_tag *)0)->name) + sizeof(((faam_custom_tag *)0)->value) + 64);
    return cap;
}

faam_status faam_update_tags_stream(const faam_io *io, const faam_metadata *meta)
{
    if (!io || !meta) return FAAM_ERR_INVALID_ARG;
    if (!io->read || !io->write || !io->seek || !io->tell) return FAAM_ERR_INVALID_ARG;

    faam_atom_ref moov, mdat;
    uint64_t file_size = 0;
    faam_atom_scan_top(io, &moov, &mdat, &file_size);
    if (moov.size < 8) return FAAM_ERR_BAD_CONTAINER;

    faam_atom_ref udta = { 0, 0 }, meta_atom = { 0, 0 }, ilst = { 0, 0 };
    faam_atom_find_child(io, moov.offset + 8, moov.offset + moov.size, "udta", &udta);
    if (udta.size >= 8)
        faam_atom_find_child(io, udta.offset + 8, udta.offset + udta.size, "meta", &meta_atom);
    if (meta_atom.size >= 12)
        faam_atom_find_child(io, meta_atom.offset + 12, meta_atom.offset + meta_atom.size, "ilst", &ilst);

    uint32_t cap = estimate_ilst_capacity(meta);
    uint8_t *ilst_buf = (uint8_t *)AllocMemory(cap);
    if (!ilst_buf) return FAAM_ERR_INSUFFICIENT_MEM;
    uint32_t ilst_len = build_ilst_payload(meta, ilst_buf);

    uint64_t ancestors[3];
    int n_anc = 0;
    faam_status rc;

    if (ilst.size >= 8) {
        ancestors[n_anc++] = meta_atom.offset;
        ancestors[n_anc++] = udta.offset;
        ancestors[n_anc++] = moov.offset;
        rc = faam_atom_resize(io, ilst.offset, ilst.size, ilst_buf, ilst_len,
                               ancestors, n_anc, &moov, &mdat, file_size);
    } else if (meta_atom.size >= 12) {
        /* No existing ilst: insert it as a new child at the end of meta's
         * current content instead of at meta_offset+12, so an existing hdlr
         * (or anything else already inside meta) isn't clobbered. */
        ancestors[n_anc++] = meta_atom.offset;
        ancestors[n_anc++] = udta.offset;
        ancestors[n_anc++] = moov.offset;
        rc = faam_atom_resize(io, meta_atom.offset + meta_atom.size, 0, ilst_buf, ilst_len,
                               ancestors, n_anc, &moov, &mdat, file_size);
    } else if (udta.size >= 8) {
        uint32_t wrap_len = 12 + ilst_len;
        uint8_t *wrap = (uint8_t *)AllocMemory(wrap_len);
        if (!wrap) {
            FreeMemory(ilst_buf);
            return FAAM_ERR_INSUFFICIENT_MEM;
        }
        write_u32(wrap, wrap_len);
        memcpy(wrap + 4, "meta", 4);
        write_u32(wrap + 8, 0); /* version/flags */
        memcpy(wrap + 12, ilst_buf, ilst_len);

        ancestors[n_anc++] = udta.offset;
        ancestors[n_anc++] = moov.offset;
        rc = faam_atom_resize(io, udta.offset + udta.size, 0, wrap, wrap_len,
                               ancestors, n_anc, &moov, &mdat, file_size);
        FreeMemory(wrap);
    } else {
        /* No udta atom at all in this moov. faam's own muxer always emits
         * one; a third-party file without any udta/meta scaffolding isn't
         * handled here (would need to synthesize udta itself). */
        rc = FAAM_ERR_BAD_CONTAINER;
    }

    FreeMemory(ilst_buf);
    return rc;
}

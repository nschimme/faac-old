/*
 * Stream-based Chapter / Bookmark management for libfaam (QuickTime chpl atom)
 */

#include "libfaam_internal.h"

static inline void write_u32(uint8_t *b, uint32_t val) { write_u32_be(b, val); }

faam_status faam_update_chapters_stream(const faam_io *io, const faam_chapter *chapters, uint32_t count)
{
    if (!io || !chapters) return FAAM_ERR_INVALID_ARG;
    if (!io->read || !io->write || !io->seek || !io->tell) return FAAM_ERR_INVALID_ARG;

    faam_atom_ref moov, mdat;
    uint64_t file_size = 0;
    faam_atom_scan_top(io, &moov, &mdat, &file_size);
    if (moov.size < 8) return FAAM_ERR_BAD_CONTAINER;

    faam_atom_ref udta = { 0, 0 }, chpl = { 0, 0 };
    faam_atom_find_child(io, moov.offset + 8, moov.offset + moov.size, "udta", &udta);
    if (udta.size >= 8)
        faam_atom_find_child(io, udta.offset + 8, udta.offset + udta.size, "chpl", &chpl);

    uint32_t chpl_cap = 16 + count * 265;
    uint8_t *chpl_buf = (uint8_t *)AllocMemory(chpl_cap);
    if (!chpl_buf) return FAAM_ERR_INSUFFICIENT_MEM;

    uint32_t chpl_len = 16;
    write_u32(chpl_buf + 8, 0); /* ver/flags */
    write_u32(chpl_buf + 12, count);

    for (uint32_t c = 0; c < count; c++) {
        uint64_t start_time = chapters[c].start_ms * 10000ULL; /* 100ns units */
        write_u64_be(chpl_buf + chpl_len, start_time);
        chpl_len += 8;

        size_t tlen = strlen(chapters[c].title);
        if (tlen > 255) tlen = 255;
        chpl_buf[chpl_len++] = (uint8_t)tlen;
        memcpy(chpl_buf + chpl_len, chapters[c].title, tlen);
        chpl_len += (uint32_t)tlen;
    }

    write_u32(chpl_buf, chpl_len);
    memcpy(chpl_buf + 4, "chpl", 4);

    uint64_t ancestors[2];
    int n_anc = 0;
    faam_status rc;

    if (chpl.size >= 8) {
        ancestors[n_anc++] = udta.offset;
        ancestors[n_anc++] = moov.offset;
        rc = faam_atom_resize(io, chpl.offset, chpl.size, chpl_buf, chpl_len,
                               ancestors, n_anc, &moov, &mdat, file_size);
    } else if (udta.size >= 8) {
        ancestors[n_anc++] = udta.offset;
        ancestors[n_anc++] = moov.offset;
        rc = faam_atom_resize(io, udta.offset + udta.size, 0, chpl_buf, chpl_len,
                               ancestors, n_anc, &moov, &mdat, file_size);
    } else {
        /* No udta atom at all -- faam's own muxer always emits one. */
        rc = FAAM_ERR_BAD_CONTAINER;
    }

    FreeMemory(chpl_buf);
    return rc;
}

/*
 * Shared in-place MP4 atom resize logic for libfaam's stream-based tag/chapter
 * rewriters (tag.c, chapter.c).
 *
 * The tricky part isn't finding an atom -- it's growing one safely. Writing a
 * bigger payload at the same file offset silently overwrites whatever follows
 * it (more moov content, or mdat sample data). Fixing that requires shifting
 * every trailing byte forward, and, if moov precedes mdat in the file
 * (faststart layout), correcting every stco/co64 chunk offset since those are
 * absolute file positions that just moved.
 */

#include "libfaam_internal.h"

#define FAAM_ATOM_TAIL_CHUNK 65536

static bool read_atom_hdr(const faam_io *io, uint64_t offset, char type[4], uint64_t *size) {
    uint8_t hdr[8];
    if (!io->seek(io->user_data, offset)) return false;
    if (io->read(io->user_data, hdr, 8) != 8) return false;
    memcpy(type, hdr + 4, 4);
    uint32_t sz32 = read_u32_be(hdr);
    if (sz32 == 1) {
        uint8_t ext[8];
        if (io->read(io->user_data, ext, 8) != 8) return false;
        *size = read_u64_be(ext);
    } else {
        *size = sz32;
    }
    return true;
}

void faam_atom_scan_top(const faam_io *io, faam_atom_ref *moov, faam_atom_ref *mdat, uint64_t *file_size) {
    moov->offset = moov->size = 0;
    mdat->offset = mdat->size = 0;
    uint64_t pos = 0;
    for (;;) {
        char type[4];
        uint64_t size;
        if (!read_atom_hdr(io, pos, type, &size) || size < 8) break;
        if (memcmp(type, "moov", 4) == 0) { moov->offset = pos; moov->size = size; }
        else if (memcmp(type, "mdat", 4) == 0) { mdat->offset = pos; mdat->size = size; }
        pos += size;
    }
    *file_size = pos;
}

bool faam_atom_find_child(const faam_io *io, uint64_t range_start, uint64_t range_end, const char name[4], faam_atom_ref *out) {
    uint64_t pos = range_start;
    while (pos + 8 <= range_end) {
        char type[4];
        uint64_t size;
        if (!read_atom_hdr(io, pos, type, &size) || size < 8 || pos + size > range_end) break;
        if (memcmp(type, name, 4) == 0) {
            out->offset = pos;
            out->size = size;
            return true;
        }
        pos += size;
    }
    return false;
}

/* trak/mdia/minf/stbl are the only containers on the path down to stco/co64;
 * recursing into everything (e.g. stsd's opaque sample entries) risks
 * misparsing atom-shaped garbage in a leaf box's payload. */
static bool is_offset_table_container(const char type[4]) {
    static const char *containers[] = { "trak", "mdia", "minf", "stbl" };
    for (size_t i = 0; i < sizeof(containers) / sizeof(containers[0]); i++) {
        if (memcmp(type, containers[i], 4) == 0) return true;
    }
    return false;
}

static bool shift_chunk_offsets(const faam_io *io, uint64_t box_offset, uint32_t entry_size, int64_t delta) {
    uint8_t hdr[8];
    if (!io->seek(io->user_data, box_offset + 8) || io->read(io->user_data, hdr, 8) != 8) return false;
    uint32_t count = read_u32_be(hdr + 4);
    uint64_t entries_off = box_offset + 16;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t buf[8];
        uint64_t off = entries_off + (uint64_t)i * entry_size;
        if (!io->seek(io->user_data, off) || io->read(io->user_data, buf, entry_size) != (int32_t)entry_size) return false;
        uint64_t val = (entry_size == 8) ? read_u64_be(buf) : read_u32_be(buf);
        val = (uint64_t)((int64_t)val + delta);
        if (entry_size == 8) write_u64_be(buf, val); else write_u32_be(buf, (uint32_t)val);
        if (!io->seek(io->user_data, off) || io->write(io->user_data, buf, entry_size) != (int32_t)entry_size) return false;
    }
    return true;
}

static bool fixup_chunk_offsets(const faam_io *io, uint64_t start, uint64_t end, int64_t delta) {
    uint64_t pos = start;
    while (pos + 8 <= end) {
        char type[4];
        uint64_t size;
        if (!read_atom_hdr(io, pos, type, &size) || size < 8 || pos + size > end) break;

        if (memcmp(type, "stco", 4) == 0) {
            if (!shift_chunk_offsets(io, pos, 4, delta)) return false;
        } else if (memcmp(type, "co64", 4) == 0) {
            if (!shift_chunk_offsets(io, pos, 8, delta)) return false;
        } else if (is_offset_table_container(type)) {
            if (!fixup_chunk_offsets(io, pos + 8, pos + size, delta)) return false;
        }
        pos += size;
    }
    return true;
}

faam_status faam_atom_resize(const faam_io *io,
                              uint64_t atom_offset, uint64_t old_size,
                              const uint8_t *new_atom, uint32_t new_size,
                              const uint64_t *ancestor_offsets, int num_ancestors,
                              const faam_atom_ref *moov, const faam_atom_ref *mdat,
                              uint64_t file_size)
{
    if (new_size <= old_size) {
        uint64_t diff = old_size - new_size;
        if (!io->seek(io->user_data, atom_offset)) return FAAM_ERR_IO_WRITE;
        if (io->write(io->user_data, new_atom, new_size) != (int32_t)new_size) return FAAM_ERR_IO_WRITE;

        if (diff >= 8) {
            uint8_t free_box[8];
            write_u32_be(free_box, (uint32_t)diff);
            memcpy(free_box + 4, "free", 4);
            if (io->write(io->user_data, free_box, 8) != 8) return FAAM_ERR_IO_WRITE;
        } else if (diff > 0) {
            /* Too little slack for a free atom; fold it into this atom's own
             * declared size instead (parsers skip the leftover trailing bytes). */
            uint8_t szbuf[4];
            write_u32_be(szbuf, (uint32_t)old_size);
            if (!io->seek(io->user_data, atom_offset) || io->write(io->user_data, szbuf, 4) != 4) return FAAM_ERR_IO_WRITE;
        }
        return FAAM_OK;
    }

    uint64_t delta = new_size - old_size;

    /* mdat only moves if it starts at/after the atom being grown. When mdat
     * comes first (the common non-faststart layout this project's own muxer
     * produces), nothing in moov's sample tables is affected. */
    if (mdat && mdat->size > 0 && mdat->offset > atom_offset && moov && moov->size > 0) {
        if (!fixup_chunk_offsets(io, moov->offset + 8, moov->offset + moov->size, (int64_t)delta))
            return FAAM_ERR_IO_WRITE;
    }

    uint64_t tail_start = atom_offset + old_size;
    uint64_t tail_len = (file_size > tail_start) ? (file_size - tail_start) : 0;
    if (tail_len > 0) {
        uint8_t *buf = (uint8_t *)AllocMemory(FAAM_ATOM_TAIL_CHUNK);
        if (!buf) return FAAM_ERR_INSUFFICIENT_MEM;

        uint64_t remaining = tail_len;
        while (remaining > 0) {
            uint32_t chunk = (uint32_t)(remaining < FAAM_ATOM_TAIL_CHUNK ? remaining : FAAM_ATOM_TAIL_CHUNK);
            uint64_t src_off = tail_start + (remaining - chunk);
            uint64_t dst_off = src_off + delta;
            /* Copy from EOF backward so an overlapping dest range is always
             * written after its source has been read. */
            if (!io->seek(io->user_data, src_off) || io->read(io->user_data, buf, chunk) != (int32_t)chunk) {
                FreeMemory(buf);
                return FAAM_ERR_IO_READ;
            }
            if (!io->seek(io->user_data, dst_off) || io->write(io->user_data, buf, chunk) != (int32_t)chunk) {
                FreeMemory(buf);
                return FAAM_ERR_IO_WRITE;
            }
            remaining -= chunk;
        }
        FreeMemory(buf);
    }

    /* Safe now: the tail has already been relocated past atom_offset+new_size. */
    if (!io->seek(io->user_data, atom_offset) || io->write(io->user_data, new_atom, new_size) != (int32_t)new_size)
        return FAAM_ERR_IO_WRITE;

    for (int i = 0; i < num_ancestors; i++) {
        uint8_t hdr[4];
        if (!io->seek(io->user_data, ancestor_offsets[i]) || io->read(io->user_data, hdr, 4) != 4)
            return FAAM_ERR_IO_READ;
        write_u32_be(hdr, read_u32_be(hdr) + (uint32_t)delta);
        if (!io->seek(io->user_data, ancestor_offsets[i]) || io->write(io->user_data, hdr, 4) != 4)
            return FAAM_ERR_IO_WRITE;
    }

    return FAAM_OK;
}

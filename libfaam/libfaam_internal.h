/*
 * Internal header for libfaam
 */

#ifndef LIBFAAM_INTERNAL_H
#define LIBFAAM_INTERNAL_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "faam.h"

/* Memory management macros (overridable for embedded PSRAM / fast internal SRAM) */
#ifndef AllocMemory
#define AllocMemory(size) malloc(size)
#endif
#ifndef FreeMemory
#define FreeMemory(block) free(block)
#endif
#ifndef AllocMemoryFast
#define AllocMemoryFast(size) malloc(size)
#endif
#ifndef FreeMemoryFast
#define FreeMemoryFast(block) free(block)
#endif
#ifndef ReallocMemory
#define ReallocMemory(block, size) realloc(block, size)
#endif

#define FAAM_MAX_TRACKS 8

typedef struct {
    uint64_t offset;
    uint32_t size;
    uint32_t duration;
    bool is_keyframe;
} faam_sample;

typedef struct {
    uint32_t count;
    uint32_t delta;
} faam_stts_entry;

typedef struct {
    faam_track_info info;
    uint8_t codec_data[256];
    uint32_t codec_data_len;
    faam_sample *samples;
    uint32_t total_frames;
    uint32_t current_frame;
} faam_demuxer_track;

struct faam_demuxer {
    faam_io io;

    faam_gapless_info gapless;
    bool has_gapless;

    uint64_t elst_media_time;
    uint64_t elst_segment_duration;
    bool has_elst;

    faam_metadata metadata;
    uint8_t *cover_art_owned; /* heap copy backing metadata.cover_art; the ilst-source
                                * buffer it was parsed from is freed right after init() */
    faam_chapter chapters[64];
    uint32_t num_chapters;

    faam_demuxer_track tracks[FAAM_MAX_TRACKS];
    uint32_t num_tracks;
    uint32_t movie_timescale;

    uint64_t mdat_start_offset;
};

typedef struct {
    faam_track_config cfg;
    uint8_t codec_data[256];
    uint32_t codec_data_len;

    faam_sample *samples;
    uint32_t sample_count;
    uint32_t sample_capacity;

    faam_stts_entry *stts_entries;
    uint32_t stts_count;
    uint32_t stts_capacity;

    uint32_t *stss_entries; /* 1-based sample index of keyframes */
    uint32_t stss_count;
    uint32_t stss_capacity;

    uint32_t max_frame_size;
    uint32_t max_bitrate;
    uint32_t avg_bitrate;
    struct {
        uint32_t max;
        uint32_t avg;
        uint64_t size;
        uint64_t samples;
    } bitrate_window;
    uint32_t last_frame_samples;
} faam_muxer_track;

struct faam_muxer {
    faam_io io;

    faam_muxer_config cfg;
    faam_muxer_track tracks[FAAM_MAX_TRACKS];
    uint32_t num_tracks;

    uint64_t mdat_pos;
    uint64_t mdat_size;

    uint8_t *membuf;
    size_t mempos;
    size_t memcap;
    int mem_error;
};

/* Endian utilities */
static inline uint16_t read_u16_be(const uint8_t *b) {
    return (uint16_t)((b[0] << 8) | b[1]);
}

static inline uint32_t read_u32_be(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static inline uint64_t read_u64_be(const uint8_t *b) {
    return ((uint64_t)read_u32_be(b) << 32) | (uint64_t)read_u32_be(b + 4);
}

static inline void write_u16_be(uint8_t *b, uint16_t val) {
    b[0] = (uint8_t)(val >> 8);
    b[1] = (uint8_t)val;
}

static inline void write_u32_be(uint8_t *b, uint32_t val) {
    b[0] = (uint8_t)(val >> 24);
    b[1] = (uint8_t)(val >> 16);
    b[2] = (uint8_t)(val >> 8);
    b[3] = (uint8_t)val;
}

static inline void write_u64_be(uint8_t *b, uint64_t val) {
    write_u32_be(b, (uint32_t)(val >> 32));
    write_u32_be(b + 4, (uint32_t)val);
}

/* Shared atom-tree location/resize primitives (atom_patch.c), used by both
 * tag.c and chapter.c's in-place mp4 rewriters so the tail-shift / stco
 * correction logic that makes growing an atom safe exists in exactly one
 * place. */
typedef struct {
    uint64_t offset; /* absolute file offset of the atom's size field */
    uint64_t size;   /* atom's stated size (0 if not found) */
} faam_atom_ref;

/* Walks top-level boxes from file start via seek/read (no whole-file
 * buffering, no size cap) until a short read marks EOF. Fills moov and mdat
 * refs (size left 0 if absent) and *file_size with the resulting EOF offset. */
void faam_atom_scan_top(const faam_io *io, faam_atom_ref *moov, faam_atom_ref *mdat, uint64_t *file_size);

/* Finds the first direct child box named `name` within byte range
 * [range_start, range_end) -- pass range_start/range_end as the container's
 * *content* bounds (i.e. already past its own box header, and past the
 * extra 4-byte version/flags for full boxes like "meta"). */
bool faam_atom_find_child(const faam_io *io, uint64_t range_start, uint64_t range_end, const char name[4], faam_atom_ref *out);

/* Replaces the atom at [atom_offset, atom_offset+old_size) with new_atom[0..new_size)
 * (old_size == 0 means "insert a new child at atom_offset", used when the
 * target atom doesn't exist yet).
 *
 * If new_size <= old_size, patches in place and pads the leftover space with
 * a "free" atom (or, if the leftover is under 8 bytes, folds it into the
 * atom's own declared size).
 *
 * If new_size > old_size, this shifts every file byte from the old atom's
 * end through EOF forward by the size delta (chunked, back-to-front, so
 * source/destination ranges never unsafely overlap), bumps every ancestor's
 * 32-bit size field in `ancestor_offsets`, and -- only when `mdat` lies
 * *after* atom_offset (moov-before-mdat / faststart-style layout, where
 * growing something inside moov physically relocates mdat) -- walks moov's
 * trak/mdia/minf/stbl boxes and adds the delta to every stco/co64 chunk
 * offset, since those are absolute file offsets into mdat.
 */
faam_status faam_atom_resize(const faam_io *io,
                              uint64_t atom_offset, uint64_t old_size,
                              const uint8_t *new_atom, uint32_t new_size,
                              const uint64_t *ancestor_offsets, int num_ancestors,
                              const faam_atom_ref *moov, const faam_atom_ref *mdat,
                              uint64_t file_size);

#endif /* LIBFAAM_INTERNAL_H */

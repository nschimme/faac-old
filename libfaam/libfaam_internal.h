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

#endif /* LIBFAAM_INTERNAL_H */

/*
 * FAAM - FAAC/FAAD Media Manipulator API
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * Lightweight ISO BMFF (MP4/M4A/M4B/MP4V) container parser and builder.
 * Operates strictly over abstract faam_io stream callbacks (zero file path dependency).
 * Supports video (H.264/AVC, H.265/HEVC) and audio (AAC, PCM) tracks, metadata, and chapters.
 */

#ifndef FAAM_H
#define FAAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FAAM_VERSION_MAJOR 1
#define FAAM_VERSION_MINOR 0
#define FAAM_VERSION_PATCH 0

#ifndef FAAMAPI
# if defined(_WIN32)
#  define FAAMAPI __declspec(dllexport)
# elif defined(__GNUC__) && (__GNUC__ >= 4)
#  define FAAMAPI __attribute__((visibility("default")))
# else
#  define FAAMAPI
# endif
#endif

typedef struct faam_demuxer faam_demuxer;
typedef struct faam_muxer   faam_muxer;

typedef enum faam_status {
    FAAM_OK                   = 0,
    FAAM_ERR_INVALID_ARG      = -1,
    FAAM_ERR_BAD_CONTAINER    = -2, /* Invalid MP4 atom structure or missing track */
    FAAM_ERR_IO_READ          = -3, /* I/O read failure or unexpected EOF */
    FAAM_ERR_IO_WRITE         = -4, /* I/O write failure */
    FAAM_ERR_INSUFFICIENT_MEM = -5, /* Provided memory arena is too small */
    FAAM_ERR_NO_TRACK         = -6, /* No matching video/audio track found */
    FAAM_ERR_UNSUPPORTED      = -7, /* Feature not supported or disabled at compile time */
    FAAM_STATUS_MAX           = 0x7fffffff
} faam_status;

typedef enum faam_track_type {
    FAAM_TRACK_AUDIO = 1,
    FAAM_TRACK_VIDEO = 2
} faam_track_type;

typedef enum faam_codec_id {
    FAAM_CODEC_GENERIC = 0,
    FAAM_CODEC_AAC     = 1,
    FAAM_CODEC_H264    = 2,
    FAAM_CODEC_H265    = 3
} faam_codec_id;

/* Abstract Stream I/O for embedded platforms (SPIFFS, SDMMC, RAM, network) */
typedef struct faam_io {
    void    *user_data;
    int32_t (*read)(void *user_data, void *buf, uint32_t bytes_to_read);
    int32_t (*write)(void *user_data, const void *buf, uint32_t bytes_to_write);
    bool    (*seek)(void *user_data, uint64_t offset);
    uint64_t(*tell)(void *user_data);
} faam_io;

/* Gapless audio parameters (corresponds to iTunSMPB atom) */
typedef struct faam_gapless_info {
    uint32_t encoder_delay;    /* Leading priming samples to discard (usually 1024) */
    uint32_t end_padding;      /* Trailing zero-padding samples to discard */
    uint64_t total_samples;    /* Original unpadded PCM sample count */
} faam_gapless_info;

/* Chapter Metadata Entry */
typedef struct faam_chapter {
    uint64_t start_ms;         /* Chapter start time in milliseconds */
    uint64_t duration_ms;      /* Chapter duration in milliseconds */
    char     title[128];       /* Chapter title */
} faam_chapter;

/* Track configuration parameters for muxer initialization */
typedef struct faam_track_config {
    uint32_t        struct_size;
    faam_track_type track_type;       /* FAAM_TRACK_AUDIO or FAAM_TRACK_VIDEO */
    faam_codec_id   codec_id;         /* FAAM_CODEC_AAC, FAAM_CODEC_H264, FAAM_CODEC_H265, etc. */
    uint32_t        track_id;         /* 1-based track identifier (0 for auto-assign) */
    uint32_t        timescale;        /* Track timescale (e.g., sample rate for audio, 90000 for video) */
    uint16_t        width;            /* Video frame width in pixels (video tracks) */
    uint16_t        height;           /* Video frame height in pixels (video tracks) */
    uint32_t        sample_rate;      /* Audio sample rate in Hz (audio tracks) */
    uint32_t        channels;         /* Audio channel count (audio tracks) */
    uint32_t        bits_per_sample;  /* Sample bit depth (16, 24, 32 for audio) */
    const uint8_t  *codec_data;       /* Codec extradata (e.g. esds / avcC / hvcC payload) */
    uint32_t        codec_data_len;   /* Length of codec extradata in bytes */
} faam_track_config;

/* Information about a parsed track in a container */
typedef struct faam_track_info {
    uint32_t        track_id;
    faam_track_type track_type;
    faam_codec_id   codec_id;
    uint32_t        timescale;
    uint16_t        width;            /* Video width in pixels */
    uint16_t        height;           /* Video height in pixels */
    uint32_t        sample_rate;      /* Audio sample rate in Hz */
    uint32_t        channels;         /* Audio channels */
    uint32_t        total_frames;     /* Frame/sample count */
    uint64_t        total_duration;   /* Total duration in timescale units */
} faam_track_info;

/* Frame location metadata returned by streaming demuxer */
typedef struct faam_frame_loc {
    uint32_t track_id;         /* Track identifier for this frame */
    uint64_t file_offset;      /* Byte offset of payload frame in stream */
    uint32_t frame_bytes;      /* Length of the payload frame in bytes */
    uint32_t duration_ticks;   /* Frame duration in timescale ticks */
    bool     is_keyframe;      /* True if sync sample / keyframe / IDR frame */
} faam_frame_loc;

/* Custom key-value tag entry */
typedef struct faam_custom_tag {
    char name[64];
    char value[256];
} faam_custom_tag;

/* Metadata Tags Structure */
typedef struct faam_metadata {
    char title[256];
    char title_sort[256];
    char artist[256];
    char artist_sort[256];
    char album[256];
    char album_sort[256];
    char album_artist[256];
    char album_artist_sort[256];
    char composer[256];
    char composer_sort[256];
    char writer[256];
    char genre_str[128];
    char year[32];
    char comment[256];
    char encoder[128];
    char language[16];
    uint16_t genre_code;
    bool compilation;
    uint16_t track_num;
    uint16_t track_total;
    uint16_t disc_num;
    uint16_t disc_total;
    const uint8_t *cover_art;
    uint32_t cover_bytes;
    faam_custom_tag custom_tags[16];
    uint32_t num_custom_tags;
} faam_metadata;


/* --- Stream Demuxer API (MP4/M4A/MP4V -> Demuxer) --- */

FAAMAPI faam_status faam_demuxer_get_state_size(uint32_t *state_bytes);

FAAMAPI faam_status faam_demuxer_init(void *mem_buf, uint32_t mem_bytes,
                                      const faam_io *io,
                                      faam_demuxer **out_demuxer);

FAAMAPI void faam_demuxer_close(faam_demuxer *d);

FAAMAPI faam_status faam_demuxer_get_num_tracks(faam_demuxer *d, uint32_t *out_num_tracks);

FAAMAPI faam_status faam_demuxer_get_track_info(faam_demuxer *d, uint32_t track_index,
                                                faam_track_info *out_info);

FAAMAPI faam_status faam_demuxer_get_codec_data(faam_demuxer *d, uint32_t track_id,
                                                uint8_t *out_buf, uint32_t buf_cap,
                                                uint32_t *out_len);

FAAMAPI faam_status faam_demuxer_get_gapless(faam_demuxer *d, faam_gapless_info *out_gapless);

FAAMAPI faam_status faam_demuxer_get_metadata(faam_demuxer *d, faam_metadata *out_meta);

FAAMAPI faam_status faam_demuxer_get_chapters(faam_demuxer *d, faam_chapter *out_chapters,
                                               uint32_t cap, uint32_t *out_count);

FAAMAPI uint32_t faam_demuxer_get_total_frames(faam_demuxer *d, uint32_t track_id);

FAAMAPI faam_status faam_demuxer_next_frame_loc(faam_demuxer *d, faam_frame_loc *out_loc);

FAAMAPI faam_status faam_demuxer_read_frame(faam_demuxer *d,
                                            uint8_t *out_frame, uint32_t frame_cap,
                                            uint32_t *frame_bytes);

FAAMAPI faam_status faam_demuxer_seek_sample(faam_demuxer *d, uint32_t track_id, uint64_t sample_offset);


/* --- Stream Muxer API (Muxer -> MP4/M4A/M4B/MP4V) --- */

typedef struct faam_muxer_config {
    uint32_t            struct_size;
    uint32_t            creation_time;   /* MP4 creation time timestamp */
    bool                faststart;       /* True to reserve space and place moov atom at front */
    bool                is_m4b;          /* True to write M4B brand headers */
    faam_gapless_info   gapless;         /* Priming/padding metadata for iTunSMPB */
    faam_metadata       metadata;        /* Initial metadata tags */
    const faam_chapter *chapters;        /* Chapters list to inject */
    uint32_t            num_chapters;    /* Chapter count */
    faam_track_config   tracks[8];       /* Up to 8 tracks (video / audio) */
    uint32_t            num_tracks;      /* Number of configured tracks */
} faam_muxer_config;

FAAMAPI faam_status faam_muxer_config_init(faam_muxer_config *cfg, uint32_t caller_size);

FAAMAPI faam_status faam_muxer_config_add_track(faam_muxer_config *cfg,
                                                 const faam_track_config *track,
                                                 uint32_t *out_track_id);

FAAMAPI faam_status faam_muxer_get_state_size(const faam_muxer_config *cfg, uint32_t *state_bytes);

FAAMAPI faam_status faam_muxer_init(void *mem_buf, uint32_t mem_bytes,
                                    const faam_muxer_config *cfg,
                                    const faam_io *io,
                                    faam_muxer **out_muxer);

FAAMAPI faam_status faam_muxer_set_gapless(faam_muxer *m, const faam_gapless_info *gapless);
FAAMAPI faam_status faam_muxer_set_metadata(faam_muxer *m, const faam_metadata *meta);

FAAMAPI faam_status faam_muxer_write_frame(faam_muxer *m,
                                           uint32_t track_id,
                                           const uint8_t *frame_buf, uint32_t frame_bytes,
                                           uint32_t duration_ticks,
                                           bool is_keyframe);

FAAMAPI faam_status faam_muxer_finalize(faam_muxer *m);

FAAMAPI void faam_muxer_close(faam_muxer *m);

/* Querying Muxer Statistics */
typedef struct faam_muxer_info {
    uint32_t struct_size;
    uint32_t frame_count;
    uint64_t sample_count;
    uint32_t max_bitrate;
    uint32_t avg_bitrate;
    uint16_t max_frame_size;
} faam_muxer_info;

FAAMAPI faam_status faam_muxer_get_info(const faam_muxer *m, faam_muxer_info *out_info);

FAAMAPI const char *faam_strerror(faam_status status);

FAAMAPI faam_status faam_update_tags_stream(const faam_io *io, const faam_metadata *meta);
FAAMAPI faam_status faam_update_chapters_stream(const faam_io *io, const faam_chapter *chapters, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* FAAM_H */

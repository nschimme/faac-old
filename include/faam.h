/*
 * FAAM - FAAC/FAAD Media Manipulator API
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * Lightweight ISO BMFF (MP4/M4A/M4B) container parser and builder.
 * Operates strictly over abstract faam_io stream callbacks (zero file path dependency).
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
    FAAM_ERR_BAD_CONTAINER    = -2, /* Invalid MP4 atom structure or missing moov/esds */
    FAAM_ERR_IO_READ          = -3, /* I/O read failure or unexpected EOF */
    FAAM_ERR_IO_WRITE         = -4, /* I/O write failure */
    FAAM_ERR_INSUFFICIENT_MEM = -5, /* Provided memory arena is too small */
    FAAM_ERR_NO_AUDIO_TRACK   = -6, /* No AAC track found in container */
    FAAM_STATUS_MAX           = 0x7fffffff
} faam_status;

/* Abstract Stream I/O for embedded platforms (SPIFFS, SDMMC, RAM, network) */
typedef struct faam_io {
    void    *user_data;
    int32_t (*read)(void *user_data, void *buf, uint32_t bytes_to_read);
    int32_t (*write)(void *user_data, const void *buf, uint32_t bytes_to_write);
    bool    (*seek)(void *user_data, uint64_t offset);
    uint64_t(*tell)(void *user_data);
} faam_io;

/* Parsed AudioSpecificConfig (ASC) structure */
typedef struct faam_asc_info {
    uint8_t  object_type;      /* AAC-LC (2), HE-AAC v1 (5), HE-AAC v2 (29) */
    uint32_t sample_rate;      /* Native audio sample rate in Hz */
    uint8_t  channels;         /* Active channel count */
    bool     sbr_present;      /* True if SBR extension is signaled */
    bool     ps_present;       /* True if Parametric Stereo is signaled */
} faam_asc_info;

/* Gapless audio parameters (corresponds to iTunSMPB atom) */
typedef struct faam_gapless_info {
    uint32_t encoder_delay;    /* Leading priming samples to discard (usually 1024) */
    uint32_t end_padding;      /* Trailing zero-padding samples to discard */
    uint64_t total_samples;    /* Original unpadded PCM sample count */
} faam_gapless_info;

/* Frame location metadata returned by streaming demuxer */
typedef struct faam_frame_loc {
    uint64_t file_offset;      /* Byte offset of AAC payload frame in stream */
    uint32_t frame_bytes;      /* Length of the AAC payload frame in bytes */
    uint32_t duration_ticks;   /* Frame duration in timescale ticks */
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

/* Chapter Metadata Entry */
typedef struct faam_chapter {
    uint64_t start_ms;         /* Chapter start time in milliseconds */
    uint64_t duration_ms;      /* Chapter duration in milliseconds */
    char     title[128];       /* Chapter title */
} faam_chapter;


/* --- AudioSpecificConfig (ASC) Utilities --- */

FAAMAPI faam_status faam_asc_parse(const uint8_t *asc_buf, uint32_t asc_len,
                                   faam_asc_info *out_info);

FAAMAPI faam_status faam_asc_build(const faam_asc_info *info,
                                   uint8_t *out_asc, uint32_t asc_cap,
                                   uint32_t *out_len);


/* --- Stream Demuxer API (MP4/M4A -> FAAD Input) --- */

FAAMAPI faam_status faam_demuxer_get_state_size(uint32_t *state_bytes);

FAAMAPI faam_status faam_demuxer_init(void *mem_buf, uint32_t mem_bytes,
                                      const faam_io *io,
                                      faam_demuxer **out_demuxer);

FAAMAPI void faam_demuxer_close(faam_demuxer *d);

FAAMAPI faam_status faam_demuxer_get_asc(faam_demuxer *d,
                                         uint8_t *out_asc, uint32_t asc_cap,
                                         uint32_t *asc_len);

FAAMAPI faam_status faam_demuxer_get_gapless(faam_demuxer *d, faam_gapless_info *out_gapless);

FAAMAPI faam_status faam_demuxer_get_metadata(faam_demuxer *d, faam_metadata *out_meta);

FAAMAPI faam_status faam_demuxer_get_chapters(faam_demuxer *d, faam_chapter *out_chapters,
                                               uint32_t cap, uint32_t *out_count);

FAAMAPI uint32_t faam_demuxer_get_total_frames(faam_demuxer *d);

FAAMAPI faam_status faam_demuxer_next_frame_loc(faam_demuxer *d, faam_frame_loc *out_loc);

FAAMAPI faam_status faam_demuxer_read_frame(faam_demuxer *d,
                                            uint8_t *out_frame, uint32_t frame_cap,
                                            uint32_t *frame_bytes);

FAAMAPI faam_status faam_demuxer_seek_sample(faam_demuxer *d, uint64_t sample_offset);


/* --- Stream Muxer API (FAAC Output -> MP4/M4A/M4B) --- */

typedef struct faam_muxer_config {
    uint32_t                struct_size;
    uint32_t                timescale;       /* Audio timescale (typically matching sample rate) */
    uint32_t                channels;        /* Audio channel count */
    uint32_t                bits_per_sample; /* Sample bit depth (16, 24, 32) */
    bool                    constant_rate;   /* True if constant bitrate */
    uint32_t                creation_time;   /* MP4 creation time timestamp */
    const uint8_t          *asc_buf;         /* AudioSpecificConfig from FAAC */
    uint32_t                asc_len;
    faam_gapless_info       gapless;         /* Priming/padding metadata for iTunSMPB */
    bool                    is_m4b;          /* True to write M4B brand headers */
    bool                    faststart;       /* True to reserve space and place moov atom at front */
    faam_metadata           metadata;        /* Initial metadata tags */
    const faam_chapter     *chapters;        /* Chapters list to inject */
    uint32_t                num_chapters;    /* Chapter count */
} faam_muxer_config;

FAAMAPI faam_status faam_muxer_config_init(faam_muxer_config *cfg, uint32_t caller_size);

FAAMAPI faam_status faam_muxer_get_state_size(const faam_muxer_config *cfg, uint32_t *state_bytes);

FAAMAPI faam_status faam_muxer_init(void *mem_buf, uint32_t mem_bytes,
                                    const faam_muxer_config *cfg,
                                    const faam_io *io,
                                    faam_muxer **out_muxer);

FAAMAPI faam_status faam_muxer_set_gapless(faam_muxer *m, const faam_gapless_info *gapless);
FAAMAPI faam_status faam_muxer_set_metadata(faam_muxer *m, const faam_metadata *meta);

FAAMAPI faam_status faam_muxer_write_frame(faam_muxer *m,
                                           const uint8_t *frame_buf, uint32_t frame_bytes,
                                           uint32_t duration_ticks);

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

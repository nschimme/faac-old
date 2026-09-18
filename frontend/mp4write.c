/*
 * Wrapper delegating mp4write to libfaam muxer over abstract stream I/O
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#include "charset.h"
#else
#include <unistd.h>
#endif

#include "mp4write.h"

#ifdef HAVE_LIBFAAM
#include "faam.h"

static faam_muxer *g_muxer = NULL;
static void *g_muxer_mem = NULL;
static faam_muxer_config g_cfg;
static faam_track_config g_track_cfg;
static FILE *g_file = NULL;
static faam_io g_io;

static int32_t file_read_cb(void *user_data, void *buf, uint32_t bytes) {
    return (int32_t)fread(buf, 1, bytes, (FILE *)user_data);
}

static int32_t file_write_cb(void *user_data, const void *buf, uint32_t bytes) {
    return (int32_t)fwrite(buf, 1, bytes, (FILE *)user_data);
}

static bool file_seek_cb(void *user_data, uint64_t offset) {
    return fseek((FILE *)user_data, (long)offset, SEEK_SET) == 0;
}

static uint64_t file_tell_cb(void *user_data) {
    return (uint64_t)ftell((FILE *)user_data);
}
#endif

int mp4_open(const char *path, bool overwrite) {
#ifdef HAVE_LIBFAAM
#ifdef _WIN32
    if (!overwrite && win32_access_utf8(path, 0) == 0) return 1;
    g_file = win32_fopen_utf8(path, "wb");
#else
    if (!overwrite && access(path, 0) == 0) return 1;
    g_file = fopen(path, "wb");
#endif
    if (!g_file) return 1;

    g_io.user_data = g_file;
    g_io.read = file_read_cb;
    g_io.write = file_write_cb;
    g_io.seek = file_seek_cb;
    g_io.tell = file_tell_cb;

    faam_metadata meta_backup = g_cfg.metadata;
    const uint8_t *codec_data_backup = g_track_cfg.codec_data;
    uint32_t codec_len_backup = g_track_cfg.codec_data_len;

    faam_muxer_config_init(&g_cfg, sizeof(g_cfg));
    g_cfg.metadata = meta_backup;

    memset(&g_track_cfg, 0, sizeof(g_track_cfg));
    g_track_cfg.struct_size = sizeof(g_track_cfg);
    g_track_cfg.track_type = FAAM_TRACK_AUDIO;
    g_track_cfg.codec_id = FAAM_CODEC_AAC;
    g_track_cfg.timescale = 44100;
    g_track_cfg.codec_data = codec_data_backup;
    g_track_cfg.codec_data_len = codec_len_backup;
    return 0;
#else
    (void)path; (void)overwrite;
    return 1;
#endif
}

void mp4_set_creation_time(uint32_t t) {
#ifdef HAVE_LIBFAAM
    g_cfg.creation_time = t;
#else
    (void)t;
#endif
}

void mp4_set_format(uint32_t samplerate, uint32_t channels, uint32_t bits) {
#ifdef HAVE_LIBFAAM
    g_track_cfg.timescale = samplerate;
    g_track_cfg.sample_rate = samplerate;
    g_track_cfg.channels = channels;
    g_track_cfg.bits_per_sample = bits;
#else
    (void)samplerate; (void)channels; (void)bits;
#endif
}

void mp4_set_constant_rate(bool constant) {
#ifdef HAVE_LIBFAAM
    (void)constant;
#else
    (void)constant;
#endif
}

void mp4_set_decoder_config(const uint8_t *asc, unsigned long size) {
#ifdef HAVE_LIBFAAM
    g_track_cfg.codec_data = asc;
    g_track_cfg.codec_data_len = (uint32_t)size;
#else
    (void)asc; (void)size;
#endif
}

void mp4_set_encoder(const char *value) {
#ifdef HAVE_LIBFAAM
    if (value) strncpy(g_cfg.metadata.encoder, value, sizeof(g_cfg.metadata.encoder) - 1);
#else
    (void)value;
#endif
}

void mp4_set_tag(mp4_tag_id_t id, const char *value) {
#ifdef HAVE_LIBFAAM
    if (!value) return;
    switch (id) {
    case MP4TAG_ARTIST: strncpy(g_cfg.metadata.artist, value, sizeof(g_cfg.metadata.artist) - 1); break;
    case MP4TAG_ARTISTSORT: strncpy(g_cfg.metadata.artist_sort, value, sizeof(g_cfg.metadata.artist_sort) - 1); break;
    case MP4TAG_TITLE: strncpy(g_cfg.metadata.title, value, sizeof(g_cfg.metadata.title) - 1); break;
    case MP4TAG_ALBUM: strncpy(g_cfg.metadata.album, value, sizeof(g_cfg.metadata.album) - 1); break;
    case MP4TAG_ALBUMSORT: strncpy(g_cfg.metadata.album_sort, value, sizeof(g_cfg.metadata.album_sort) - 1); break;
    case MP4TAG_ALBUMARTIST: strncpy(g_cfg.metadata.album_artist, value, sizeof(g_cfg.metadata.album_artist) - 1); break;
    case MP4TAG_ALBUMARTISTSORT: strncpy(g_cfg.metadata.album_artist_sort, value, sizeof(g_cfg.metadata.album_artist_sort) - 1); break;
    case MP4TAG_COMPOSER: strncpy(g_cfg.metadata.composer, value, sizeof(g_cfg.metadata.composer) - 1); break;
    case MP4TAG_COMPOSERSORT: strncpy(g_cfg.metadata.composer_sort, value, sizeof(g_cfg.metadata.composer_sort) - 1); break;
    case MP4TAG_YEAR: strncpy(g_cfg.metadata.year, value, sizeof(g_cfg.metadata.year) - 1); break;
    case MP4TAG_COMMENT: strncpy(g_cfg.metadata.comment, value, sizeof(g_cfg.metadata.comment) - 1); break;
    default: break;
    }
#else
    (void)id; (void)value;
#endif
}

void mp4_set_genre(uint16_t genre) {
#ifdef HAVE_LIBFAAM
    g_cfg.metadata.genre_code = genre;
#else
    (void)genre;
#endif
}

void mp4_set_language(const char *lang) {
#ifdef HAVE_LIBFAAM
    if (lang) strncpy(g_cfg.metadata.language, lang, sizeof(g_cfg.metadata.language) - 1);
#else
    (void)lang;
#endif
}

void mp4_set_compilation(bool flag) {
#ifdef HAVE_LIBFAAM
    g_cfg.metadata.compilation = flag;
#else
    (void)flag;
#endif
}

void mp4_set_track(uint16_t num, uint16_t total) {
#ifdef HAVE_LIBFAAM
    g_cfg.metadata.track_num = num; g_cfg.metadata.track_total = total;
#else
    (void)num; (void)total;
#endif
}

void mp4_set_disc(uint16_t num, uint16_t total) {
#ifdef HAVE_LIBFAAM
    g_cfg.metadata.disc_num = num; g_cfg.metadata.disc_total = total;
#else
    (void)num; (void)total;
#endif
}

void mp4_set_cover(const uint8_t *data, uint32_t size) {
#ifdef HAVE_LIBFAAM
    g_cfg.metadata.cover_art = data; g_cfg.metadata.cover_bytes = size;
#else
    (void)data; (void)size;
#endif
}

void mp4_set_gapless(uint32_t priming, uint32_t padding, uint64_t original_samples) {
#ifdef HAVE_LIBFAAM
    g_cfg.gapless.encoder_delay = priming;
    g_cfg.gapless.end_padding = padding;
    g_cfg.gapless.total_samples = original_samples;
#else
    (void)priming; (void)padding; (void)original_samples;
#endif
}

int mp4_add_custom_tag(const char *name, const char *value) {
#ifdef HAVE_LIBFAAM
    if (!name || !value) return -1;
    uint32_t idx = g_cfg.metadata.num_custom_tags;
    if (idx < 16) {
        strncpy(g_cfg.metadata.custom_tags[idx].name, name, sizeof(g_cfg.metadata.custom_tags[idx].name) - 1);
        strncpy(g_cfg.metadata.custom_tags[idx].value, value, sizeof(g_cfg.metadata.custom_tags[idx].value) - 1);
        g_cfg.metadata.num_custom_tags++;
    }
    return 0;
#else
    (void)name; (void)value;
    return -1;
#endif
}

int mp4_write_frame(const uint8_t *data, uint32_t size, uint32_t samples) {
#ifdef HAVE_LIBFAAM
    if (!g_muxer) {
        faam_muxer_config_add_track(&g_cfg, &g_track_cfg, NULL);

        uint32_t state_size = 0;
        faam_muxer_get_state_size(&g_cfg, &state_size);
        g_muxer_mem = calloc(1, state_size);
        if (!g_muxer_mem) return -1;

        if (faam_muxer_init(g_muxer_mem, state_size, &g_cfg, &g_io, &g_muxer) != FAAM_OK) {
            free(g_muxer_mem);
            g_muxer_mem = NULL;
            return -1;
        }
    }
    return faam_muxer_write_frame(g_muxer, 1, data, size, samples, true) == FAAM_OK ? 0 : -1;
#else
    (void)data; (void)size; (void)samples;
    return -1;
#endif
}

int mp4_finish(void) {
#ifdef HAVE_LIBFAAM
    if (g_muxer) {
        faam_muxer_set_metadata(g_muxer, &g_cfg.metadata);
        faam_muxer_set_gapless(g_muxer, &g_cfg.gapless);
        faam_muxer_finalize(g_muxer);
        faam_muxer_close(g_muxer);
        g_muxer = NULL;
    }
    if (g_muxer_mem) {
        free(g_muxer_mem);
        g_muxer_mem = NULL;
    }
    if (g_file) {
        fclose(g_file);
        g_file = NULL;
    }
    return 0;
#else
    return 0;
#endif
}

int mp4_close(void) {
    return mp4_finish();
}

static inline void get_info_helper(faam_muxer_info *info) {
    memset(info, 0, sizeof(*info));
    info->struct_size = sizeof(*info);
#ifdef HAVE_LIBFAAM
    if (g_muxer) faam_muxer_get_info(g_muxer, info);
#endif
}

uint32_t mp4_frame_count(void) {
    faam_muxer_info info; get_info_helper(&info); return info.frame_count;
}

uint64_t mp4_sample_count(void) {
    faam_muxer_info info; get_info_helper(&info); return info.sample_count;
}

uint32_t mp4_max_bitrate(void) {
    faam_muxer_info info; get_info_helper(&info); return info.max_bitrate;
}

uint32_t mp4_avg_bitrate(void) {
    faam_muxer_info info; get_info_helper(&info); return info.avg_bitrate;
}

uint16_t mp4_max_frame_size(void) {
    faam_muxer_info info; get_info_helper(&info); return info.max_frame_size;
}

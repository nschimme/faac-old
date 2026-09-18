/*
 * Direct memory-stream wrapper for mp4_read_track_buf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef HAVE_LIBFAAM
#include "faam.h"
#endif

typedef struct {
    uint64_t offset;
    uint32_t size;
} MP4Sample;

typedef struct {
    uint8_t *asc_buf;
    uint32_t asc_len;
    uint32_t delay;
    uint32_t padding;
    MP4Sample *samples;
    uint32_t num_samples;
    char major_brand[16];
    char encoder_tag[64];
} MP4Track;

#ifdef HAVE_LIBFAAM
typedef struct {
    const uint8_t *buf;
    uint64_t size;
    uint64_t pos;
} memory_stream;

static int32_t mem_read_cb(void *user_data, void *dst, uint32_t bytes) {
    memory_stream *ms = (memory_stream *)user_data;
    if (ms->pos >= ms->size) return 0;
    uint32_t avail = (uint32_t)(ms->size - ms->pos);
    uint32_t copy_bytes = bytes < avail ? bytes : avail;
    memcpy(dst, ms->buf + ms->pos, copy_bytes);
    ms->pos += copy_bytes;
    return (int32_t)copy_bytes;
}

static bool mem_seek_cb(void *user_data, uint64_t offset) {
    memory_stream *ms = (memory_stream *)user_data;
    if (offset > ms->size) return false;
    ms->pos = offset;
    return true;
}

static uint64_t mem_tell_cb(void *user_data) {
    memory_stream *ms = (memory_stream *)user_data;
    return ms->pos;
}
#endif

bool mp4_read_track_buf(const uint8_t *buf, long file_size, MP4Track *track)
{
    memset(track, 0, sizeof(*track));
#ifdef HAVE_LIBFAAM
    if (!buf || file_size < 32) return false;

    memory_stream ms = { buf, (uint64_t)file_size, 0 };

    faam_io io;
    io.user_data = &ms;
    io.read = mem_read_cb;
    io.write = NULL;
    io.seek = mem_seek_cb;
    io.tell = mem_tell_cb;

    uint32_t demux_size = 0;
    faam_demuxer_get_state_size(&demux_size);
    void *mem = malloc(demux_size);

    faam_demuxer *d = NULL;
    if (faam_demuxer_init(mem, demux_size, &io, &d) != FAAM_OK) {
        free(mem);
        return false;
    }

    uint8_t asc[64];
    uint32_t asc_len = 0;
    faam_demuxer_get_codec_data(d, 1, asc, sizeof(asc), &asc_len);
    if (asc_len > 0) {
        track->asc_buf = (uint8_t *)malloc(asc_len);
        memcpy(track->asc_buf, asc, asc_len);
        track->asc_len = asc_len;
    }

    faam_gapless_info gapless;
    faam_demuxer_get_gapless(d, &gapless);
    track->delay = gapless.encoder_delay;
    track->padding = gapless.end_padding;

    /* Query total frames directly from demuxer */
    uint32_t total_frames = faam_demuxer_get_total_frames(d, 1);
    if (total_frames > 0) {
        track->samples = (MP4Sample *)calloc(total_frames, sizeof(MP4Sample));
        track->num_samples = total_frames;

        faam_frame_loc loc;
        for (uint32_t i = 0; i < total_frames; i++) {
            if (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) {
                track->samples[i].offset = loc.file_offset;
                track->samples[i].size = loc.frame_bytes;
                uint32_t bytes_read = 0;
                faam_demuxer_read_frame(d, NULL, 0, &bytes_read);
            }
        }
    }

    faam_demuxer_close(d);
    free(mem);
    return track->asc_buf != NULL && track->num_samples > 0;
#else
    (void)buf; (void)file_size;
    return false;
#endif
}

void mp4_free_track(MP4Track *track)
{
    if (track->asc_buf) free(track->asc_buf);
    if (track->samples) free(track->samples);
    memset(track, 0, sizeof(*track));
}

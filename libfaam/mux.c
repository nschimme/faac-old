/*
 * Full Thread-Safe Multi-Track ISO BMFF Muxer Engine for libfaam
 * Supports audio (AAC, PCM) and video (H.264/AVC, H.265/HEVC) tracks.
 */

#include <stdio.h>
#include "libfaam_internal.h"

#if defined(__has_builtin)
#if __has_builtin(__builtin_bswap32) && __has_builtin(__builtin_bswap16)
#define MP4_HAVE_BSWAP_BUILTINS 1
#endif
#elif defined(__GNUC__)
#define MP4_HAVE_BSWAP_BUILTINS 1
#endif

#if defined(MP4_HAVE_BSWAP_BUILTINS)
#define BSWAP32 __builtin_bswap32
#define BSWAP16 __builtin_bswap16
#elif defined(_MSC_VER)
#define BSWAP32 _byteswap_ulong
#define BSWAP16 _byteswap_ushort
#else
static inline uint32_t BSWAP32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000) | (x << 24);
}
static inline uint16_t BSWAP16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}
#endif

enum {
    MP4_EPOCH_OFFSET = 2082844800,
    MP4_FP1616_ONE = 0x00010000,
    MP4_FP0230_ONE = 0x40000000,
    MP4_FP0808_ONE = 0x0100,
    MP4_DESC_HDR = 5,
    ITUNES_DATA_BINARY = 0,
    ITUNES_DATA_TEXT   = 1,
    ITUNES_DATA_UINT8  = 0x15,
    ITUNES_DATA_IMAGE  = 0x0d,
    MP4_OBJECT_TYPE_AUDIO_ISO_14496_3 = 0x40,
    MP4_STREAM_TYPE_AUDIO             = 0x15,
    MP4_DECODER_BUFFER_BYTES_PER_CH   = 6144 / 8,
    MP4_URL_SELF_CONTAINED = 1,
    ISO639_UND_PACKED  = 0x55C4,
};

static inline bool grow_membuf(faam_muxer *m, size_t extra) {
    if (m->mempos + extra <= m->memcap) return true;
    size_t max_cap = ((size_t)1 << 30);
    size_t new_cap = m->memcap ? m->memcap * 2 : 1024;
    while (m->mempos + extra > new_cap && new_cap < max_cap) {
        if (new_cap > max_cap / 2) { new_cap = max_cap; break; }
        new_cap *= 2;
    }
    if (m->mempos + extra > new_cap) return false;
    void *tmp = ReallocMemory(m->membuf, new_cap);
    if (!tmp) {
        return false;
    }
    m->membuf = (uint8_t *)tmp;
    m->memcap = new_cap;
    return true;
}

static inline void mem_write(faam_muxer *m, const void *data, size_t size) {
    if (m->membuf) {
        if (!grow_membuf(m, size)) { m->mem_error = 1; return; }
        memcpy(m->membuf + m->mempos, data, size);
        m->mempos += size;
    } else if (m->io.write && !m->mem_error) {
        if (m->io.write(m->io.user_data, data, (uint32_t)size) != (int32_t)size) m->mem_error = 1;
    }
}

static inline void put_u32(faam_muxer *m, uint32_t val) {
    uint8_t buf[4];
    write_u32_be(buf, val);
    mem_write(m, buf, 4);
}

static inline void put_u16(faam_muxer *m, uint16_t val) {
    uint8_t buf[2];
    write_u16_be(buf, val);
    mem_write(m, buf, 2);
}

static inline void put_u64(faam_muxer *m, uint64_t val) {
    uint8_t buf[8];
    write_u64_be(buf, val);
    mem_write(m, buf, 8);
}

static inline void put_time(faam_muxer *m, uint64_t val, bool use64) {
    if (use64) put_u64(m, val); else put_u32(m, (uint32_t)val);
}

static inline void put_u8(faam_muxer *m, uint8_t val) { mem_write(m, &val, 1); }
static inline void put_data(faam_muxer *m, const void *data, size_t size) { mem_write(m, data, size); }

static inline long start_atom(faam_muxer *m, const char *name) {
    long pos = m->membuf ? (long)m->mempos : (long)m->io.tell(m->io.user_data);
    put_u32(m, 0);
    put_data(m, name, 4);
    return pos;
}

static inline void end_atom(faam_muxer *m, long pos) {
    if (m->membuf) {
        uint32_t size = (uint32_t)(m->mempos - pos);
#ifndef WORDS_BIGENDIAN
        size = BSWAP32(size);
#endif
        memcpy(m->membuf + pos, &size, 4);
    } else if (m->io.seek && m->io.write && m->io.tell) {
        uint64_t curr = m->io.tell(m->io.user_data);
        m->io.seek(m->io.user_data, (uint64_t)pos);
        put_u32(m, (uint32_t)(curr - pos));
        m->io.seek(m->io.user_data, curr);
    }
}

static void put_descriptor(faam_muxer *m, uint8_t tag, uint32_t size) {
    uint8_t buf[5];
    buf[0] = tag;
    buf[1] = ((size >> 21) & 0x7f) | 0x80;
    buf[2] = ((size >> 14) & 0x7f) | 0x80;
    buf[3] = ((size >> 7) & 0x7f) | 0x80;
    buf[4] = (size & 0x7f);
    mem_write(m, buf, 5);
}

static void put_itunes_data_box(faam_muxer *m, const char *name, uint32_t type_code, const void *data, size_t len) {
    if (!name || !data) return;
    long box      = start_atom(m, name);
    long data_box = start_atom(m, "data");
    put_u32(m, type_code);
    put_u32(m, 0);
    put_data(m, data, len);
    end_atom(m, data_box);
    end_atom(m, box);
}

static void put_tag(faam_muxer *m, const char *name, const char *data) {
    if (data && strlen(data) > 0)
        put_itunes_data_box(m, name, ITUNES_DATA_TEXT, data, strlen(data));
}

static void put_tag_u8(faam_muxer *m, const char *name, uint8_t val) {
    put_itunes_data_box(m, name, ITUNES_DATA_UINT8, &val, 1);
}

static void put_tag_genre(faam_muxer *m, uint16_t genre) {
#ifndef WORDS_BIGENDIAN
    uint16_t val = BSWAP16(genre);
#else
    uint16_t val = genre;
#endif
    put_itunes_data_box(m, "gnre", ITUNES_DATA_BINARY, &val, 2);
}

static void put_tag_index(faam_muxer *m, const char *name, uint16_t num, uint16_t total) {
    uint16_t buf[4] = {
        0,
#ifndef WORDS_BIGENDIAN
        BSWAP16(num),
        BSWAP16(total),
#else
        num,
        total,
#endif
        0
    };
    put_itunes_data_box(m, name, ITUNES_DATA_BINARY, buf, sizeof(buf));
}

static void put_tag_ext(faam_muxer *m, const char *mean, const char *name, const char *val) {
    if (!mean || !name || !val) return;
    long box      = start_atom(m, "----");
    long mean_box = start_atom(m, "mean");
    put_u32(m, 0);
    put_data(m, mean, strlen(mean));
    end_atom(m, mean_box);
    long name_box = start_atom(m, "name");
    put_u32(m, 0);
    put_data(m, name, strlen(name));
    end_atom(m, name_box);
    long data_box = start_atom(m, "data");
    put_u32(m, ITUNES_DATA_TEXT);
    put_u32(m, 0);
    put_data(m, val, strlen(val));
    end_atom(m, data_box);
    end_atom(m, box);
}

faam_status faam_muxer_config_init(faam_muxer_config *cfg, uint32_t caller_size)
{
    if (!cfg || caller_size < sizeof(faam_muxer_config)) return FAAM_ERR_INVALID_ARG;
    memset(cfg, 0, caller_size);
    cfg->struct_size = caller_size;
    cfg->gapless.encoder_delay = 1024;
    return FAAM_OK;
}

faam_status faam_muxer_config_add_track(faam_muxer_config *cfg, const faam_track_config *track, uint32_t *out_track_id)
{
    if (!cfg || !track) return FAAM_ERR_INVALID_ARG;
    if (cfg->num_tracks >= 8) return FAAM_ERR_INSUFFICIENT_MEM;

    uint32_t idx = cfg->num_tracks;
    cfg->tracks[idx] = *track;
    cfg->tracks[idx].track_id = track->track_id ? track->track_id : (idx + 1);
    cfg->num_tracks++;

    if (out_track_id) *out_track_id = cfg->tracks[idx].track_id;
    return FAAM_OK;
}

faam_status faam_muxer_get_state_size(const faam_muxer_config *cfg, uint32_t *state_bytes)
{
    (void)cfg;
    if (!state_bytes) return FAAM_ERR_INVALID_ARG;
    *state_bytes = sizeof(struct faam_muxer);
    return FAAM_OK;
}

faam_status faam_muxer_init(void *mem_buf, uint32_t mem_bytes, const faam_muxer_config *cfg, const faam_io *io, faam_muxer **out_muxer)
{
    if (!mem_buf || mem_bytes < sizeof(struct faam_muxer) || !cfg || !io || !out_muxer) {
        return FAAM_ERR_INVALID_ARG;
    }

    struct faam_muxer *m = (struct faam_muxer *)mem_buf;
    memset(m, 0, sizeof(*m));
    m->cfg = *cfg;
    m->io = *io;

    if (cfg->num_tracks == 0) {
        faam_track_config def_track;
        memset(&def_track, 0, sizeof(def_track));
        def_track.track_type = FAAM_TRACK_AUDIO;
        def_track.codec_id = FAAM_CODEC_AAC;
        def_track.timescale = 44100;
        def_track.sample_rate = 44100;
        def_track.channels = 2;
        def_track.bits_per_sample = 16;
        faam_muxer_config_add_track(&m->cfg, &def_track, NULL);
    }

    m->num_tracks = m->cfg.num_tracks;
    for (uint32_t t = 0; t < m->num_tracks; t++) {
        faam_muxer_track *tr = &m->tracks[t];
        tr->cfg = m->cfg.tracks[t];
        if (tr->cfg.codec_data && tr->cfg.codec_data_len > 0) {
            uint32_t len = tr->cfg.codec_data_len < sizeof(tr->codec_data) ? tr->cfg.codec_data_len : (uint32_t)sizeof(tr->codec_data);
            memcpy(tr->codec_data, tr->cfg.codec_data, len);
            tr->codec_data_len = len;
        }

        tr->sample_capacity = 1024;
        tr->samples = (faam_sample *)AllocMemory(tr->sample_capacity * sizeof(faam_sample));
        if (!tr->samples) {
            faam_muxer_close(m);
            return FAAM_ERR_INSUFFICIENT_MEM;
        }
        memset(tr->samples, 0, tr->sample_capacity * sizeof(faam_sample));

        tr->stts_capacity = 16;
        tr->stts_entries = (faam_stts_entry *)AllocMemoryFast(tr->stts_capacity * sizeof(faam_stts_entry));
        if (!tr->stts_entries) {
            faam_muxer_close(m);
            return FAAM_ERR_INSUFFICIENT_MEM;
        }
        memset(tr->stts_entries, 0, tr->stts_capacity * sizeof(faam_stts_entry));

        tr->stss_capacity = 16;
        tr->stss_entries = (uint32_t *)AllocMemoryFast(tr->stss_capacity * sizeof(uint32_t));
        if (!tr->stss_entries) {
            faam_muxer_close(m);
            return FAAM_ERR_INSUFFICIENT_MEM;
        }
        memset(tr->stss_entries, 0, tr->stss_capacity * sizeof(uint32_t));
    }

    uint8_t ftyp[36] = {
        0x00, 0x00, 0x00, 0x20, 'f', 't', 'y', 'p',
        'i', 's', 'o', 'm', 0x00, 0x00, 0x00, 0x00,
        'i', 's', 'o', 'm', 'i', 's', 'o', '2',
        0x00, 0x00, 0x00, 0x08, 'w', 'i', 'd', 'e',
        0x00, 0x00, 0x00, 0x00
    };
    if (m->cfg.is_m4b) {
        ftyp[8] = 'M'; ftyp[9] = '4'; ftyp[10] = 'B'; ftyp[11] = ' ';
    }
    if (m->io.write) m->io.write(m->io.user_data, ftyp, 32);

    uint8_t mdat_hdr[8] = { 0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't' };
    if (m->io.write) m->io.write(m->io.user_data, mdat_hdr, 8);
    m->mdat_pos = m->io.tell ? m->io.tell(m->io.user_data) : 40;

    *out_muxer = m;
    return FAAM_OK;
}

FAAMAPI faam_status faam_muxer_set_gapless(faam_muxer *m, const faam_gapless_info *gapless) {
    if (!m || !gapless) return FAAM_ERR_INVALID_ARG;
    m->cfg.gapless = *gapless;
    return FAAM_OK;
}

FAAMAPI faam_status faam_muxer_set_metadata(faam_muxer *m, const faam_metadata *meta) {
    if (!m || !meta) return FAAM_ERR_INVALID_ARG;
    m->cfg.metadata = *meta;
    return FAAM_OK;
}

faam_status faam_muxer_write_frame(faam_muxer *m, uint32_t track_id, const uint8_t *frame_buf, uint32_t frame_bytes, uint32_t duration_ticks, bool is_keyframe)
{
    if (!m || !frame_buf || frame_bytes == 0) return FAAM_ERR_INVALID_ARG;

    faam_muxer_track *tr = NULL;
    for (uint32_t t = 0; t < m->num_tracks; t++) {
        if (m->tracks[t].cfg.track_id == track_id) {
            tr = &m->tracks[t];
            break;
        }
    }
    if (!tr && m->num_tracks > 0) tr = &m->tracks[0];
    if (!tr) return FAAM_ERR_NO_TRACK;

    if (m->io.write) {
        if (m->io.write(m->io.user_data, frame_buf, frame_bytes) != (int32_t)frame_bytes) return FAAM_ERR_IO_WRITE;
    }

    m->mdat_size += frame_bytes;
    tr->bitrate_window.samples += duration_ticks;

    if (tr->sample_count >= tr->sample_capacity) {
        uint32_t new_cap = tr->sample_capacity * 2;
        faam_sample *tmp = (faam_sample *)ReallocMemory(tr->samples, new_cap * sizeof(faam_sample));
        if (!tmp) return FAAM_ERR_INSUFFICIENT_MEM;
        tr->samples = tmp;
        tr->sample_capacity = new_cap;
    }

    tr->samples[tr->sample_count].offset = m->mdat_pos + m->mdat_size - frame_bytes;
    tr->samples[tr->sample_count].size = frame_bytes;
    tr->samples[tr->sample_count].duration = duration_ticks;
    tr->samples[tr->sample_count].is_keyframe = is_keyframe;
    tr->sample_count++;

    if (is_keyframe && tr->cfg.track_type == FAAM_TRACK_VIDEO) {
        if (tr->stss_count >= tr->stss_capacity) {
            uint32_t new_cap = tr->stss_capacity * 2;
            uint32_t *tmp = (uint32_t *)ReallocMemory(tr->stss_entries, new_cap * sizeof(uint32_t));
            if (!tmp) return FAAM_ERR_INSUFFICIENT_MEM;
            tr->stss_entries = tmp;
            tr->stss_capacity = new_cap;
        }
        tr->stss_entries[tr->stss_count++] = tr->sample_count; /* 1-based index */
    }

    if (tr->max_frame_size < frame_bytes) tr->max_frame_size = frame_bytes;

    if (tr->stts_count > 0 && tr->stts_entries[tr->stts_count - 1].delta == duration_ticks) {
        tr->stts_entries[tr->stts_count - 1].count++;
    } else {
        if (tr->stts_count >= tr->stts_capacity) {
            uint32_t new_cap = tr->stts_capacity * 2;
            faam_stts_entry *tmp = (faam_stts_entry *)ReallocMemory(tr->stts_entries, new_cap * sizeof(faam_stts_entry));
            if (!tmp) return FAAM_ERR_INSUFFICIENT_MEM;
            tr->stts_entries = tmp;
            tr->stts_capacity = new_cap;
        }
        tr->stts_entries[tr->stts_count].count = 1;
        tr->stts_entries[tr->stts_count].delta = duration_ticks;
        tr->stts_count++;
    }

    return FAAM_OK;
}

faam_status faam_muxer_finalize(faam_muxer *m)
{
    if (!m) return FAAM_ERR_INVALID_ARG;
    m->mem_error = 0;

    if (m->io.seek && m->io.write) {
        uint64_t pos = m->io.tell ? m->io.tell(m->io.user_data) : 0;
        m->io.seek(m->io.user_data, m->mdat_pos - 8);
        uint32_t sz_be = BSWAP32((uint32_t)(m->mdat_size + 8));
        m->io.write(m->io.user_data, &sz_be, 4);
        m->io.seek(m->io.user_data, pos);
    }

    m->mempos = 0;
    m->memcap = 65536;
    for (uint32_t t = 0; t < m->num_tracks; t++) {
        m->memcap += (size_t)m->tracks[t].sample_count * 12;
    }
    m->membuf = (uint8_t *)AllocMemory(m->memcap);
    if (!m->membuf) return FAAM_ERR_INSUFFICIENT_MEM;

    uint32_t movie_timescale = 1000;
    uint64_t max_movie_dur = 0;

    long moov = start_atom(m, "moov");
    long mvhd = start_atom(m, "mvhd");
    uint32_t now = m->cfg.creation_time ? m->cfg.creation_time + MP4_EPOCH_OFFSET : 0;

    for (uint32_t t = 0; t < m->num_tracks; t++) {
        faam_muxer_track *tr = &m->tracks[t];
        uint32_t ts = tr->cfg.timescale ? tr->cfg.timescale : (tr->cfg.track_type == FAAM_TRACK_AUDIO ? 44100 : 90000);
        uint64_t dur_mv = (tr->bitrate_window.samples * (uint64_t)movie_timescale) / ts;
        if (dur_mv > max_movie_dur) max_movie_dur = dur_mv;
    }

    bool use64_time = (max_movie_dur > 0xFFFFFFFFULL);

    put_u32(m, use64_time ? (1U << 24) : 0);
    put_time(m, now, use64_time); put_time(m, now, use64_time);
    put_u32(m, movie_timescale); put_time(m, max_movie_dur, use64_time);
    put_u32(m, MP4_FP1616_ONE); put_u16(m, MP4_FP0808_ONE); put_u16(m, 0); put_u32(m, 0); put_u32(m, 0);
    put_u32(m, MP4_FP1616_ONE); put_u32(m, 0); put_u32(m, 0);
    put_u32(m, 0); put_u32(m, MP4_FP1616_ONE); put_u32(m, 0);
    put_u32(m, 0); put_u32(m, 0); put_u32(m, MP4_FP0230_ONE);
    put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u32(m, 0);
    put_u32(m, m->num_tracks + 1);
    end_atom(m, mvhd);

    for (uint32_t t = 0; t < m->num_tracks; t++) {
        faam_muxer_track *tr = &m->tracks[t];
        uint32_t ts = tr->cfg.timescale ? tr->cfg.timescale : (tr->cfg.track_type == FAAM_TRACK_AUDIO ? 44100 : 90000);
        uint64_t track_dur_mv = (tr->bitrate_window.samples * (uint64_t)movie_timescale) / ts;

        long trak = start_atom(m, "trak");
        long tkhd = start_atom(m, "tkhd");
        put_u32(m, (use64_time ? (1U << 24) : 0) | 1);
        put_time(m, now, use64_time); put_time(m, now, use64_time);
        put_u32(m, tr->cfg.track_id); put_u32(m, 0);
        put_time(m, track_dur_mv, use64_time);
        put_u32(m, 0); put_u32(m, 0);
        put_u16(m, 0); put_u16(m, 0); put_u16(m, tr->cfg.track_type == FAAM_TRACK_AUDIO ? MP4_FP0808_ONE : 0); put_u16(m, 0);
        put_u32(m, MP4_FP1616_ONE); put_u32(m, 0); put_u32(m, 0);
        put_u32(m, 0); put_u32(m, MP4_FP1616_ONE); put_u32(m, 0);
        put_u32(m, 0); put_u32(m, 0); put_u32(m, MP4_FP0230_ONE);
        put_u32(m, (uint32_t)tr->cfg.width << 16); put_u32(m, (uint32_t)tr->cfg.height << 16);
        end_atom(m, tkhd);

        if (tr->cfg.track_type == FAAM_TRACK_AUDIO && m->cfg.gapless.encoder_delay > 0) {
            long edts = start_atom(m, "edts");
            long elst = start_atom(m, "elst");
            put_u32(m, use64_time ? (1U << 24) : 0);
            put_u32(m, 1);
            put_time(m, m->cfg.gapless.total_samples ? m->cfg.gapless.total_samples : tr->bitrate_window.samples, use64_time);
            put_time(m, m->cfg.gapless.encoder_delay, use64_time);
            put_u16(m, 1); put_u16(m, 0);
            end_atom(m, elst);
            end_atom(m, edts);
        }

        long mdia = start_atom(m, "mdia");
        long mdhd = start_atom(m, "mdhd");
        put_u32(m, use64_time ? (1U << 24) : 0);
        put_time(m, now, use64_time); put_time(m, now, use64_time);
        put_u32(m, ts); put_time(m, tr->bitrate_window.samples, use64_time);
        put_u16(m, ISO639_UND_PACKED); put_u16(m, 0);
        end_atom(m, mdhd);

        long hdlr = start_atom(m, "hdlr");
        put_u32(m, 0); put_u32(m, 0);
        if (tr->cfg.track_type == FAAM_TRACK_AUDIO) put_data(m, "soun", 4);
        else put_data(m, "vide", 4);
        put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u8(m, 0);
        end_atom(m, hdlr);

        long minf = start_atom(m, "minf");
        if (tr->cfg.track_type == FAAM_TRACK_AUDIO) {
            long smhd = start_atom(m, "smhd");
            put_u32(m, 0); put_u16(m, 0); put_u16(m, 0);
            end_atom(m, smhd);
        } else {
            long vmhd = start_atom(m, "vmhd");
            put_u32(m, 1); put_u16(m, 0); put_u16(m, 0); put_u16(m, 0); put_u16(m, 0);
            end_atom(m, vmhd);
        }

        long dinf = start_atom(m, "dinf");
        long dref = start_atom(m, "dref");
        put_u32(m, 0); put_u32(m, 1);
        long url = start_atom(m, "url ");
        put_u32(m, MP4_URL_SELF_CONTAINED);
        end_atom(m, url);
        end_atom(m, dref);
        end_atom(m, dinf);

        long stbl = start_atom(m, "stbl");
        long stsd = start_atom(m, "stsd");
        put_u32(m, 0); put_u32(m, 1);

        if (tr->cfg.track_type == FAAM_TRACK_AUDIO) {
            long mp4a = start_atom(m, "mp4a");
            put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0);
            put_u16(m, 1); put_u32(m, 0); put_u32(m, 0);
            put_u16(m, (uint16_t)(tr->cfg.channels ? tr->cfg.channels : 2));
            put_u16(m, (uint16_t)(tr->cfg.bits_per_sample ? tr->cfg.bits_per_sample : 16));
            put_u16(m, 0); put_u16(m, 0);
            put_u16(m, (uint16_t)(ts > UINT16_MAX ? UINT16_MAX : ts));
            put_u16(m, 0);

            long esds = start_atom(m, "esds");
            put_u32(m, 0);
            put_descriptor(m, 3, 3 + MP4_DESC_HDR + 13 + MP4_DESC_HDR + tr->codec_data_len + MP4_DESC_HDR + 1);
            put_u16(m, 0); put_u8(m, 0);
            put_descriptor(m, 4, 13 + MP4_DESC_HDR + tr->codec_data_len);
            put_u8(m, MP4_OBJECT_TYPE_AUDIO_ISO_14496_3); put_u8(m, MP4_STREAM_TYPE_AUDIO);
            uint32_t bufferSizeDB = MP4_DECODER_BUFFER_BYTES_PER_CH * (tr->cfg.channels ? tr->cfg.channels : 2);
            put_u8(m, (uint8_t)(bufferSizeDB >> 16));
            put_u8(m, (uint8_t)(bufferSizeDB >> 8));
            put_u8(m, (uint8_t)(bufferSizeDB & 0xff));
            put_u32(m, 128000); put_u32(m, 128000);
            put_descriptor(m, 5, tr->codec_data_len);
            put_data(m, tr->codec_data, tr->codec_data_len);
            put_descriptor(m, 6, 1); put_u8(m, 2);
            end_atom(m, esds);
            end_atom(m, mp4a);
        } else {
            const char *v_tag = (tr->cfg.codec_id == FAAM_CODEC_H265) ? "hvc1" : "avc1";
            const char *cfg_tag = (tr->cfg.codec_id == FAAM_CODEC_H265) ? "hvcC" : "avcC";
            long v_box = start_atom(m, v_tag);
            put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0);
            put_u16(m, 1); put_u16(m, 0); put_u16(m, 0);
            put_u32(m, 0); put_u32(m, 0); put_u32(m, 0);
            put_u16(m, tr->cfg.width ? tr->cfg.width : 1920);
            put_u16(m, tr->cfg.height ? tr->cfg.height : 1080);
            put_u32(m, 0x00480000); put_u32(m, 0x00480000);
            put_u32(m, 0); put_u16(m, 1);
            put_u8(m, 0); put_data(m, "FAAM Video", 10);
            for (int k = 10; k < 31; k++) put_u8(m, 0);
            put_u16(m, 0x0018); put_u16(m, 0xFFFF);

            if (tr->codec_data_len > 0) {
                long c_box = start_atom(m, cfg_tag);
                put_data(m, tr->codec_data, tr->codec_data_len);
                end_atom(m, c_box);
            }
            end_atom(m, v_box);
        }
        end_atom(m, stsd);

        long stts = start_atom(m, "stts");
        put_u32(m, 0); put_u32(m, tr->stts_count);
        for (uint32_t i = 0; i < tr->stts_count; i++) {
            put_u32(m, tr->stts_entries[i].count);
            put_u32(m, tr->stts_entries[i].delta);
        }
        end_atom(m, stts);

        if (tr->stss_count > 0) {
            long stss = start_atom(m, "stss");
            put_u32(m, 0); put_u32(m, tr->stss_count);
            for (uint32_t i = 0; i < tr->stss_count; i++) {
                put_u32(m, tr->stss_entries[i]);
            }
            end_atom(m, stss);
        }

        long stsc = start_atom(m, "stsc");
        put_u32(m, 0); put_u32(m, 1); /* ver/flags (0), entry count (1) */
        put_u32(m, 1); put_u32(m, 1); put_u32(m, 1); /* first_chunk(1), samples_per_chunk(1), sample_description_index(1) */
        end_atom(m, stsc);

        long stsz = start_atom(m, "stsz");
        put_u32(m, 0); put_u32(m, 0); put_u32(m, tr->sample_count);
        for (uint32_t i = 0; i < tr->sample_count; i++) {
            put_u32(m, tr->samples[i].size);
        }
        end_atom(m, stsz);

        bool use64_stco = false;
        for (uint32_t i = 0; i < tr->sample_count; i++) {
            if (tr->samples[i].offset > 0xFFFFFFFFULL) {
                use64_stco = true;
                break;
            }
        }

        if (!use64_stco) {
            long stco = start_atom(m, "stco");
            put_u32(m, 0); put_u32(m, tr->sample_count);
            for (uint32_t i = 0; i < tr->sample_count; i++) {
                put_u32(m, (uint32_t)tr->samples[i].offset);
            }
            end_atom(m, stco);
        } else {
            long co64 = start_atom(m, "co64");
            put_u32(m, 0); put_u32(m, tr->sample_count);
            for (uint32_t i = 0; i < tr->sample_count; i++) {
                put_u64(m, tr->samples[i].offset);
            }
            end_atom(m, co64);
        }

        end_atom(m, stbl);
        end_atom(m, minf);
        end_atom(m, mdia);
        end_atom(m, trak);
    }

    long udta = start_atom(m, "udta");

    if (m->cfg.chapters && m->cfg.num_chapters > 0) {
        long chpl = start_atom(m, "chpl");
        put_u32(m, 0);
        put_u32(m, m->cfg.num_chapters);
        for (uint32_t c = 0; c < m->cfg.num_chapters; c++) {
            put_u64(m, m->cfg.chapters[c].start_ms * 10000ULL);
            size_t tlen = strlen(m->cfg.chapters[c].title);
            if (tlen > 255) tlen = 255;
            put_u8(m, (uint8_t)tlen);
            put_data(m, m->cfg.chapters[c].title, tlen);
        }
        end_atom(m, chpl);
    }

    long meta = start_atom(m, "meta");
    put_u32(m, 0);
    long hdlr2 = start_atom(m, "hdlr");
    put_u32(m, 0); put_u32(m, 0); put_data(m, "mdirappl", 8);
    put_u32(m, 0); put_u32(m, 0); put_u8(m, 0);
    end_atom(m, hdlr2);

    long ilst = start_atom(m, "ilst");
    put_tag(m, "\xa9" "too", m->cfg.metadata.encoder[0] ? m->cfg.metadata.encoder : "FAAM");
    if (m->cfg.metadata.title[0]) put_tag(m, "\xa9" "nam", m->cfg.metadata.title);
    if (m->cfg.metadata.artist[0]) put_tag(m, "\xa9" "ART", m->cfg.metadata.artist);
    if (m->cfg.metadata.album[0]) put_tag(m, "\xa9" "alb", m->cfg.metadata.album);
    if (m->cfg.metadata.album_artist[0]) put_tag(m, "aART", m->cfg.metadata.album_artist);
    if (m->cfg.metadata.composer[0]) put_tag(m, "\xa9" "wrt", m->cfg.metadata.composer);
    if (m->cfg.metadata.year[0]) put_tag(m, "\xa9" "day", m->cfg.metadata.year);
    if (m->cfg.metadata.comment[0]) put_tag(m, "\xa9" "cmt", m->cfg.metadata.comment);
    if (m->cfg.metadata.genre_code) put_tag_genre(m, m->cfg.metadata.genre_code);
    if (m->cfg.metadata.compilation) put_tag_u8(m, "cpil", 1);
    if (m->cfg.metadata.track_num) put_tag_index(m, "trkn", m->cfg.metadata.track_num, m->cfg.metadata.track_total);
    if (m->cfg.metadata.disc_num) put_tag_index(m, "disk", m->cfg.metadata.disc_num, m->cfg.metadata.disc_total);
    if (m->cfg.metadata.cover_art && m->cfg.metadata.cover_bytes > 4) {
        const uint8_t *art = m->cfg.metadata.cover_art;
        uint32_t type_code = ITUNES_DATA_IMAGE;
        if (art[0] == 0x89 && art[1] == 'P' && art[2] == 'N' && art[3] == 'G') {
            type_code = 14;
        } else if (art[0] == 0xFF && art[1] == 0xD8) {
            type_code = 13;
        }
        put_itunes_data_box(m, "covr", type_code, m->cfg.metadata.cover_art, m->cfg.metadata.cover_bytes);
    }

    if (m->cfg.gapless.encoder_delay > 0) {
        char smpb[128];
        snprintf(smpb, sizeof(smpb),
                 " 00000000 %08X %08X %08X%08X 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000",
                 m->cfg.gapless.encoder_delay,
                 m->cfg.gapless.end_padding,
                 (uint32_t)(m->cfg.gapless.total_samples >> 32),
                 (uint32_t)(m->cfg.gapless.total_samples & 0xFFFFFFFFULL));
        put_tag_ext(m, "com.apple.iTunes", "iTunSMPB", smpb);
    }

    for (uint32_t i = 0; i < m->cfg.metadata.num_custom_tags; i++) {
        put_tag_ext(m, "faac", m->cfg.metadata.custom_tags[i].name, m->cfg.metadata.custom_tags[i].value);
    }

    end_atom(m, ilst);
    end_atom(m, meta);
    end_atom(m, udta);
    end_atom(m, moov);

    if (m->io.write) {
        m->io.write(m->io.user_data, m->membuf, (uint32_t)m->mempos);
    }

    FreeMemory(m->membuf);
    m->membuf = NULL;

    return FAAM_OK;
}

void faam_muxer_close(faam_muxer *m)
{
    if (!m) return;
    for (uint32_t t = 0; t < m->num_tracks; t++) {
        if (m->tracks[t].samples) FreeMemory(m->tracks[t].samples);
        if (m->tracks[t].stts_entries) FreeMemoryFast(m->tracks[t].stts_entries);
        if (m->tracks[t].stss_entries) FreeMemoryFast(m->tracks[t].stss_entries);
    }
    if (m->membuf) FreeMemory(m->membuf);
}

faam_status faam_muxer_get_info(const faam_muxer *m, faam_muxer_info *out_info)
{
    if (!m || !out_info || out_info->struct_size < sizeof(faam_muxer_info)) {
        return FAAM_ERR_INVALID_ARG;
    }
    out_info->struct_size = sizeof(faam_muxer_info);
    out_info->frame_count = m->num_tracks > 0 ? m->tracks[0].sample_count : 0;
    out_info->sample_count = m->num_tracks > 0 ? m->tracks[0].bitrate_window.samples : 0;
    out_info->max_bitrate = m->num_tracks > 0 ? m->tracks[0].max_bitrate : 0;
    out_info->avg_bitrate = m->num_tracks > 0 ? m->tracks[0].avg_bitrate : 0;
    out_info->max_frame_size = m->num_tracks > 0 ? (uint16_t)m->tracks[0].max_frame_size : 0;
    return FAAM_OK;
}

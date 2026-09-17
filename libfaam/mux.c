/*
 * Full Thread-Safe ISO BMFF Muxer Engine for libfaam
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
    MP4_TRACK_ID      = 1,
    MP4_NEXT_TRACK_ID = 2,
    MP4_URL_SELF_CONTAINED = 1,
    MP4_IO_BUFSIZE = 65536,
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
    void *tmp = realloc(m->membuf, new_cap);
    if (!tmp) {
        free(m->membuf);
        m->membuf = NULL;
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
#ifndef WORDS_BIGENDIAN
    val = BSWAP32(val);
#endif
    mem_write(m, &val, 4);
}

static inline void put_u16(faam_muxer *m, uint16_t val) {
#ifndef WORDS_BIGENDIAN
    val = BSWAP16(val);
#endif
    mem_write(m, &val, 2);
}

static inline void put_u64(faam_muxer *m, uint64_t val) {
#ifndef WORDS_BIGENDIAN
#if defined(MP4_HAVE_BSWAP_BUILTINS)
    val = __builtin_bswap64(val);
#elif defined(_MSC_VER)
    val = _byteswap_uint64(val);
#else
    val = ((val >> 56) & 0x00000000000000FFULL) |
          ((val >> 40) & 0x000000000000FF00ULL) |
          ((val >> 24) & 0x0000000000FF0000ULL) |
          ((val >> 8)  & 0x00000000FF000000ULL) |
          ((val << 8)  & 0x000000FF00000000ULL) |
          ((val << 24) & 0x0000FF0000000000ULL) |
          ((val << 40) & 0x00FF000000000000ULL) |
          ((val << 56) & 0xFF00000000000000ULL);
#endif
#endif
    mem_write(m, &val, 8);
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
    cfg->timescale = 44100;
    cfg->gapless.encoder_delay = 1024;
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

    if (cfg->asc_buf && cfg->asc_len > 0) {
        m->asc_len = cfg->asc_len < sizeof(m->asc_buf) ? cfg->asc_len : sizeof(m->asc_buf);
        memcpy(m->asc_buf, cfg->asc_buf, m->asc_len);
    }

    m->sample_rate = cfg->timescale ? cfg->timescale : 44100;
    m->num_channels = cfg->channels ? cfg->channels : 2;
    m->bits_per_sample = cfg->bits_per_sample ? cfg->bits_per_sample : 16;

    m->sample_capacity = 1024;
    m->samples = (faam_sample *)calloc(m->sample_capacity, sizeof(faam_sample));
    if (!m->samples) return FAAM_ERR_INSUFFICIENT_MEM;

    m->stts_capacity = 16;
    m->stts_entries = (faam_stts_entry *)calloc(m->stts_capacity, sizeof(faam_stts_entry));
    if (!m->stts_entries) { free(m->samples); return FAAM_ERR_INSUFFICIENT_MEM; }

    uint8_t ftyp[36] = {
        0x00, 0x00, 0x00, 0x20, 'f', 't', 'y', 'p',
        'M', '4', 'A', ' ', 0x00, 0x00, 0x00, 0x00,
        'M', '4', 'A', ' ', 'i', 's', 'o', 'm',
        0x00, 0x00, 0x00, 0x08, 'w', 'i', 'd', 'e',
        0x00, 0x00, 0x00, 0x00
    };
    if (cfg->is_m4b) {
        ftyp[8] = 'M'; ftyp[9] = '4'; ftyp[10] = 'B'; ftyp[11] = ' ';
        ftyp[16] = 'M'; ftyp[17] = '4'; ftyp[18] = 'B'; ftyp[19] = ' ';
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


faam_status faam_muxer_write_frame(faam_muxer *m, const uint8_t *frame_buf, uint32_t frame_bytes, uint32_t duration_ticks)
{
    if (!m || !frame_buf || frame_bytes == 0) return FAAM_ERR_INVALID_ARG;

    if (m->io.write) {
        if (m->io.write(m->io.user_data, frame_buf, frame_bytes) != (int32_t)frame_bytes) return FAAM_ERR_IO_WRITE;
    }

    m->mdat_size += frame_bytes;
    m->sample_count += duration_ticks;

    if (m->last_frame_samples <= duration_ticks) {
        m->bitrate_window.size += frame_bytes;
        m->bitrate_window.samples += duration_ticks;
        if (m->bitrate_window.samples >= m->sample_rate) {
            uint32_t br = (uint32_t)((uint64_t)8 * m->bitrate_window.size * m->sample_rate / m->bitrate_window.samples);
            if (m->bitrate_window.max < br) m->bitrate_window.max = br;
            m->bitrate_window.size = 0;
            m->bitrate_window.samples = 0;
        }
        m->last_frame_samples = duration_ticks;
    }

    if (m->frame_count >= m->sample_capacity) {
        uint32_t new_cap = m->sample_capacity * 2;
        faam_sample *tmp = (faam_sample *)realloc(m->samples, new_cap * sizeof(faam_sample));
        if (!tmp) return FAAM_ERR_INSUFFICIENT_MEM;
        m->samples = tmp;
        m->sample_capacity = new_cap;
    }

    m->samples[m->frame_count].offset = m->mdat_pos + m->mdat_size - frame_bytes;
    m->samples[m->frame_count].size = frame_bytes;
    m->samples[m->frame_count].duration = duration_ticks;
    m->frame_count++;

    if (m->max_frame_size < frame_bytes) m->max_frame_size = (uint16_t)frame_bytes;

    if (m->stts_count > 0 && m->stts_entries[m->stts_count - 1].delta == duration_ticks) {
        m->stts_entries[m->stts_count - 1].count++;
    } else {
        if (m->stts_count >= m->stts_capacity) {
            uint32_t new_cap = m->stts_capacity * 2;
            faam_stts_entry *tmp = (faam_stts_entry *)realloc(m->stts_entries, new_cap * sizeof(faam_stts_entry));
            if (!tmp) return FAAM_ERR_INSUFFICIENT_MEM;
            m->stts_entries = tmp;
            m->stts_capacity = new_cap;
        }
        m->stts_entries[m->stts_count].count = 1;
        m->stts_entries[m->stts_count].delta = duration_ticks;
        m->stts_count++;
    }

    return FAAM_OK;
}

faam_status faam_muxer_finalize(faam_muxer *m)
{
    if (!m) return FAAM_ERR_INVALID_ARG;
    m->mem_error = 0;

    /* Write updated mdat atom size directly to the stream before allocating membuf for moov */
    if (m->io.seek && m->io.write) {
        uint64_t pos = m->io.tell ? m->io.tell(m->io.user_data) : 0;
        m->io.seek(m->io.user_data, m->mdat_pos - 8);
        uint32_t sz_be = BSWAP32((uint32_t)(m->mdat_size + 8));
        m->io.write(m->io.user_data, &sz_be, 4);
        m->io.seek(m->io.user_data, pos);
    }

    m->avg_bitrate = (uint32_t)((uint64_t)8 * m->mdat_size * m->sample_rate / (m->sample_count ? m->sample_count : 1));
    if (!m->bitrate_window.max || m->cfg.constant_rate) m->max_bitrate = m->avg_bitrate;
    else m->max_bitrate = m->bitrate_window.max;

    m->mempos = 0;
    m->memcap = 65536 + (size_t)m->frame_count * 4;
    m->membuf = (uint8_t *)malloc(m->memcap);
    if (!m->membuf) return FAAM_ERR_INSUFFICIENT_MEM;

    bool use64_time = (m->sample_count > 0xFFFFFFFFULL);

    long moov = start_atom(m, "moov");
    long mvhd = start_atom(m, "mvhd");
    uint32_t now = m->cfg.creation_time ? m->cfg.creation_time + MP4_EPOCH_OFFSET : 0;
    put_u32(m, use64_time ? (1U << 24) : 0);
    put_time(m, now, use64_time); put_time(m, now, use64_time);
    put_u32(m, m->sample_rate); put_time(m, m->sample_count, use64_time);
    put_u32(m, MP4_FP1616_ONE); put_u16(m, MP4_FP0808_ONE); put_u16(m, 0); put_u32(m, 0); put_u32(m, 0);
    put_u32(m, MP4_FP1616_ONE); put_u32(m, 0); put_u32(m, 0);
    put_u32(m, 0); put_u32(m, MP4_FP1616_ONE); put_u32(m, 0);
    put_u32(m, 0); put_u32(m, 0); put_u32(m, MP4_FP0230_ONE);
    put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u32(m, 0);
    put_u32(m, MP4_NEXT_TRACK_ID);
    end_atom(m, mvhd);

    long trak = start_atom(m, "trak");
    long tkhd = start_atom(m, "tkhd");
    put_u32(m, (use64_time ? (1U << 24) : 0) | 1);
    put_time(m, now, use64_time); put_time(m, now, use64_time);
    put_u32(m, MP4_TRACK_ID); put_u32(m, 0);
    put_time(m, m->sample_count, use64_time);
    put_u32(m, 0); put_u32(m, 0);
    put_u16(m, 0); put_u16(m, 0); put_u16(m, MP4_FP0808_ONE); put_u16(m, 0);
    put_u32(m, MP4_FP1616_ONE); put_u32(m, 0); put_u32(m, 0);
    put_u32(m, 0); put_u32(m, MP4_FP1616_ONE); put_u32(m, 0);
    put_u32(m, 0); put_u32(m, 0); put_u32(m, MP4_FP0230_ONE);
    put_u32(m, 0); put_u32(m, 0);
    end_atom(m, tkhd);

    if (m->cfg.gapless.encoder_delay > 0) {
        long edts = start_atom(m, "edts");
        long elst = start_atom(m, "elst");
        put_u32(m, use64_time ? (1U << 24) : 0);
        put_u32(m, 1);
        put_time(m, m->cfg.gapless.total_samples ? m->cfg.gapless.total_samples : m->sample_count, use64_time);
        put_time(m, m->cfg.gapless.encoder_delay, use64_time);
        put_u16(m, 1); put_u16(m, 0);
        end_atom(m, elst);
        end_atom(m, edts);
    }

    long mdia = start_atom(m, "mdia");
    long mdhd = start_atom(m, "mdhd");
    put_u32(m, use64_time ? (1U << 24) : 0);
    put_time(m, now, use64_time); put_time(m, now, use64_time);
    put_u32(m, m->sample_rate); put_time(m, m->sample_count, use64_time);
    put_u16(m, ISO639_UND_PACKED); put_u16(m, 0);
    end_atom(m, mdhd);

    long hdlr = start_atom(m, "hdlr");
    put_u32(m, 0); put_u32(m, 0); put_data(m, "soun", 4);
    put_u32(m, 0); put_u32(m, 0); put_u32(m, 0); put_u8(m, 0);
    end_atom(m, hdlr);

    long minf = start_atom(m, "minf");
    long smhd = start_atom(m, "smhd");
    put_u32(m, 0); put_u16(m, 0); put_u16(m, 0);
    end_atom(m, smhd);

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
    long mp4a = start_atom(m, "mp4a");
    put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0); put_u8(m, 0);
    put_u16(m, 1); put_u32(m, 0); put_u32(m, 0);
    put_u16(m, (uint16_t)m->num_channels); put_u16(m, (uint16_t)m->bits_per_sample);
    put_u16(m, 0); put_u16(m, 0);
    put_u16(m, (uint16_t)(m->sample_rate > UINT16_MAX ? UINT16_MAX : m->sample_rate));
    put_u16(m, 0);

    long esds = start_atom(m, "esds");
    put_u32(m, 0);
    put_descriptor(m, 3, 3 + MP4_DESC_HDR + 13 + MP4_DESC_HDR + m->asc_len + MP4_DESC_HDR + 1);
    put_u16(m, 0); put_u8(m, 0);
    put_descriptor(m, 4, 13 + MP4_DESC_HDR + m->asc_len);
    put_u8(m, MP4_OBJECT_TYPE_AUDIO_ISO_14496_3); put_u8(m, MP4_STREAM_TYPE_AUDIO);
    uint32_t bufferSizeDB = MP4_DECODER_BUFFER_BYTES_PER_CH * m->num_channels;
    put_u8(m, (uint8_t)(bufferSizeDB >> 16));
    put_u8(m, (uint8_t)(bufferSizeDB >> 8));
    put_u8(m, (uint8_t)(bufferSizeDB & 0xff));
    put_u32(m, m->max_bitrate); put_u32(m, m->avg_bitrate);
    put_descriptor(m, 5, m->asc_len);
    put_data(m, m->asc_buf, m->asc_len);
    put_descriptor(m, 6, 1); put_u8(m, 2);
    end_atom(m, esds);
    end_atom(m, mp4a);
    end_atom(m, stsd);

    long stts = start_atom(m, "stts");
    put_u32(m, 0); put_u32(m, m->stts_count);
    for (uint32_t i = 0; i < m->stts_count; i++) {
        put_u32(m, m->stts_entries[i].count);
        put_u32(m, m->stts_entries[i].delta);
    }
    end_atom(m, stts);

    long stsc = start_atom(m, "stsc");
    put_u32(m, 0); put_u32(m, 1); put_u32(m, 1);
    put_u32(m, m->frame_count); put_u32(m, 1);
    end_atom(m, stsc);

    long stsz = start_atom(m, "stsz");
    put_u32(m, 0); put_u32(m, 0); put_u32(m, m->frame_count);
    for (uint32_t i = 0; i < m->frame_count; i++) {
        put_u32(m, m->samples[i].size);
    }
    end_atom(m, stsz);

    if (m->mdat_pos + m->mdat_size <= 0xFFFFFFFFULL) {
        long stco = start_atom(m, "stco");
        put_u32(m, 0); put_u32(m, 1); put_u32(m, (uint32_t)m->mdat_pos);
        end_atom(m, stco);
    } else {
        long co64 = start_atom(m, "co64");
        put_u32(m, 0); put_u32(m, 1); put_u64(m, m->mdat_pos);
        end_atom(m, co64);
    }

    end_atom(m, stbl);
    end_atom(m, minf);
    end_atom(m, mdia);
    end_atom(m, trak);

    long udta = start_atom(m, "udta");
    long meta = start_atom(m, "meta");
    put_u32(m, 0);
    long hdlr2 = start_atom(m, "hdlr");
    put_u32(m, 0); put_u32(m, 0); put_data(m, "mdirappl", 8);
    put_u32(m, 0); put_u32(m, 0); put_u8(m, 0);
    end_atom(m, hdlr2);

    long ilst = start_atom(m, "ilst");
    put_tag(m, "\xa9" "too", m->cfg.metadata.encoder[0] ? m->cfg.metadata.encoder : "FAAC");
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
        uint32_t type_code = ITUNES_DATA_IMAGE; /* 0x0D = JPEG */
        if (art[0] == 0x89 && art[1] == 'P' && art[2] == 'N' && art[3] == 'G') {
            type_code = 14; /* 0x0E = PNG */
        } else if (art[0] == 0xFF && art[1] == 0xD8) {
            type_code = 13; /* 0x0D = JPEG */
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

    free(m->membuf);
    m->membuf = NULL;

    return FAAM_OK;
}

void faam_muxer_close(faam_muxer *m)
{
    if (!m) return;
    if (m->samples) free(m->samples);
    if (m->stts_entries) free(m->stts_entries);
    if (m->membuf) free(m->membuf);
}

faam_status faam_muxer_get_info(const faam_muxer *m, faam_muxer_info *out_info)
{
    if (!m || !out_info || out_info->struct_size < sizeof(faam_muxer_info)) {
        return FAAM_ERR_INVALID_ARG;
    }
    out_info->struct_size = sizeof(faam_muxer_info);
    out_info->frame_count = m->frame_count;
    out_info->sample_count = m->sample_count;
    out_info->max_bitrate = m->max_bitrate;
    out_info->avg_bitrate = m->avg_bitrate;
    out_info->max_frame_size = m->max_frame_size;
    return FAAM_OK;
}

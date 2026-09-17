/*
 * Stream-based Demuxer Engine for libfaam (Recursive ISO MP4/M4A/M4B Box Walker)
 */

#include "libfaam_internal.h"

typedef struct {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
    uint32_t sample_description_index;
} STSCEntry;

static uint32_t parse_ber_length(const uint8_t *buf, long *offset, long max_offset)
{
    uint32_t len = 0;
    int count = 0;
    while (*offset < max_offset && count < 4) {
        uint8_t b = buf[(*offset)++];
        len = (len << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
        count++;
    }
    return len;
}

static bool is_audio_trak(const uint8_t *buf, long offset, long end)
{
    long cur = offset;
    while (cur + 8 <= end) {
        uint64_t box_size = read_u32_be(buf + cur);
        char type[5] = {0};
        memcpy(type, buf + cur + 4, 4);

        long header_size = 8;
        if (box_size == 1 && cur + 16 <= end) {
            box_size = read_u64_be(buf + cur + 8);
            header_size = 16;
        } else if (box_size == 0) {
            box_size = end - cur;
        }

        if (box_size < (uint64_t)header_size || cur + (long)box_size > end) break;

        long payload_offset = cur + header_size;
        long payload_end = cur + (long)box_size;

        if (memcmp(type, "mdia", 4) == 0) {
            return is_audio_trak(buf, payload_offset, payload_end);
        } else if (memcmp(type, "hdlr", 4) == 0 && payload_offset + 12 <= payload_end) {
            if (memcmp(buf + payload_offset + 8, "soun", 4) == 0) {
                return true;
            }
            return false;
        }
        cur = payload_end;
    }
    return true;
}

static void parse_boxes_recursive(const uint8_t *buf, long offset, long end, struct faam_demuxer *d,
                                  uint32_t **stsz_table, uint32_t *num_stsz_samples, uint32_t *fixed_sample_size,
                                  STSCEntry **stsc_table, uint32_t *num_stsc_entries,
                                  uint64_t **stco_table, uint32_t *num_stco_chunks)
{
    long cur = offset;
    while (cur + 8 <= end) {
        uint64_t box_size = read_u32_be(buf + cur);
        char type[5] = {0};
        memcpy(type, buf + cur + 4, 4);

        long header_size = 8;
        if (box_size == 1 && cur + 16 <= end) {
            box_size = read_u64_be(buf + cur + 8);
            header_size = 16;
        } else if (box_size == 0) {
            box_size = end - cur;
        }

        if (box_size < (uint64_t)header_size || cur + (long)box_size > end) break;

        long payload_offset = cur + header_size;
        long payload_end = cur + (long)box_size;

        if (memcmp(type, "trak", 4) == 0) {
            if (!is_audio_trak(buf, payload_offset, payload_end)) {
                cur = payload_end;
                continue;
            }
        }

        if (memcmp(type, "moov", 4) == 0 || memcmp(type, "trak", 4) == 0 ||
            memcmp(type, "mdia", 4) == 0 || memcmp(type, "minf", 4) == 0 ||
            memcmp(type, "stbl", 4) == 0 || memcmp(type, "udta", 4) == 0 ||
            memcmp(type, "meta", 4) == 0 || memcmp(type, "ilst", 4) == 0 ||
            memcmp(type, "stsd", 4) == 0 || memcmp(type, "mp4a", 4) == 0) {
            long sub_offset = payload_offset;
            if (memcmp(type, "meta", 4) == 0) sub_offset += 4;
            if (memcmp(type, "stsd", 4) == 0) sub_offset += 8;
            if (memcmp(type, "mp4a", 4) == 0) sub_offset += 28;
            parse_boxes_recursive(buf, sub_offset, payload_end, d,
                                   stsz_table, num_stsz_samples, fixed_sample_size,
                                   stsc_table, num_stsc_entries,
                                   stco_table, num_stco_chunks);
        } else if (memcmp(type, "esds", 4) == 0) {
            long pos = payload_offset + 4;
            while (pos < payload_end - 2) {
                uint8_t tag = buf[pos++];
                uint32_t tag_len = parse_ber_length(buf, &pos, payload_end);
                if (tag == 0x03) pos += 3;
                else if (tag == 0x04) pos += 13;
                else if (tag == 0x05) {
                    if (tag_len > 0 && pos + tag_len <= payload_end) {
                        d->asc_len = tag_len < sizeof(d->asc_buf) ? tag_len : sizeof(d->asc_buf);
                        memcpy(d->asc_buf, buf + pos, d->asc_len);
                        faam_asc_parse(d->asc_buf, d->asc_len, &d->asc_info);
                    }
                    break;
                } else pos += tag_len;
            }
        } else if (memcmp(type, "iTun", 4) == 0 || memcmp(type, "SMPB", 4) == 0) {
            for (long j = payload_offset; j < payload_end - 32; j++) {
                if (memcmp(buf + j, " 00000000 ", 10) == 0) {
                    char str_buf[128] = {0};
                    long copy_len = payload_end - j;
                    if (copy_len > (long)(sizeof(str_buf) - 1)) copy_len = sizeof(str_buf) - 1;
                    memcpy(str_buf, buf + j, copy_len);
                    sscanf(str_buf, " %*x %x %x", &d->gapless.encoder_delay, &d->gapless.end_padding);
                    d->has_gapless = true;
                    break;
                }
            }
        } else if (memcmp(type, "stsz", 4) == 0 && payload_offset + 12 <= payload_end) {
            *fixed_sample_size = read_u32_be(buf + payload_offset + 4);
            uint32_t sample_count = read_u32_be(buf + payload_offset + 8);
            if (sample_count > 0 && sample_count < 1000000) {
                *num_stsz_samples = sample_count;
                if (*fixed_sample_size == 0) {
                    *stsz_table = (uint32_t *)calloc(sample_count, sizeof(uint32_t));
                    for (uint32_t s = 0; s < sample_count && (payload_offset + 12 + s * 4) <= payload_end - 4; s++) {
                        (*stsz_table)[s] = read_u32_be(buf + payload_offset + 12 + s * 4);
                    }
                }
            }
        } else if (memcmp(type, "stsc", 4) == 0 && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries < 100000) {
                *num_stsc_entries = entries;
                *stsc_table = (STSCEntry *)calloc(entries, sizeof(STSCEntry));
                for (uint32_t e = 0; e < entries && (payload_offset + 8 + e * 12) <= payload_end - 12; e++) {
                    (*stsc_table)[e].first_chunk = read_u32_be(buf + payload_offset + 8 + e * 12);
                    (*stsc_table)[e].samples_per_chunk = read_u32_be(buf + payload_offset + 8 + e * 12 + 4);
                    (*stsc_table)[e].sample_description_index = read_u32_be(buf + payload_offset + 8 + e * 12 + 8);
                }
            }
        } else if (memcmp(type, "stco", 4) == 0 && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_u32_be(buf + payload_offset + 4);
            if (chunks > 0 && chunks < 1000000) {
                *num_stco_chunks = chunks;
                *stco_table = (uint64_t *)calloc(chunks, sizeof(uint64_t));
                for (uint32_t c = 0; c < chunks && (payload_offset + 8 + c * 4) <= payload_end - 4; c++) {
                    (*stco_table)[c] = read_u32_be(buf + payload_offset + 8 + c * 4);
                }
            }
        } else if (memcmp(type, "co64", 4) == 0 && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_u32_be(buf + payload_offset + 4);
            if (chunks > 0 && chunks < 1000000) {
                *num_stco_chunks = chunks;
                *stco_table = (uint64_t *)calloc(chunks, sizeof(uint64_t));
                for (uint32_t c = 0; c < chunks && (payload_offset + 8 + c * 8) <= payload_end - 8; c++) {
                    (*stco_table)[c] = read_u64_be(buf + payload_offset + 8 + c * 8);
                }
            }
        }

        cur = payload_end;
    }
}

static faam_status faam_parse_stream(struct faam_demuxer *d, const uint8_t *buf, long file_size)
{
    uint32_t num_stsz_samples = 0;
    uint32_t *stsz_table = NULL;
    uint32_t fixed_sample_size = 0;

    STSCEntry *stsc_table = NULL;
    uint32_t num_stsc_entries = 0;

    uint64_t *stco_table = NULL;
    uint32_t num_stco_chunks = 0;

    parse_boxes_recursive(buf, 0, file_size, d,
                           &stsz_table, &num_stsz_samples, &fixed_sample_size,
                           &stsc_table, &num_stsc_entries,
                           &stco_table, &num_stco_chunks);

    if (num_stsz_samples > 0) {
        d->samples = (faam_sample *)calloc(num_stsz_samples, sizeof(faam_sample));
        d->total_frames = num_stsz_samples;

        if (stco_table && num_stco_chunks > 0 && stsc_table && num_stsc_entries > 0) {
            uint32_t sample_idx = 0;
            for (uint32_t chunk_idx = 0; chunk_idx < num_stco_chunks; chunk_idx++) {
                uint32_t chunk_num = chunk_idx + 1;
                uint64_t chunk_offset = stco_table[chunk_idx];

                uint32_t samples_in_chunk = stsc_table[0].samples_per_chunk;
                for (uint32_t e = 0; e < num_stsc_entries; e++) {
                    if (chunk_num >= stsc_table[e].first_chunk) {
                        samples_in_chunk = stsc_table[e].samples_per_chunk;
                    } else break;
                }

                uint32_t sample_offset_in_chunk = 0;
                for (uint32_t s = 0; s < samples_in_chunk && sample_idx < num_stsz_samples; s++) {
                    uint32_t size = (fixed_sample_size != 0) ? fixed_sample_size : (stsz_table ? stsz_table[sample_idx] : 0);
                    d->samples[sample_idx].offset = chunk_offset + sample_offset_in_chunk;
                    d->samples[sample_idx].size = size;
                    d->samples[sample_idx].duration = 1024;
                    sample_offset_in_chunk += size;
                    sample_idx++;
                }
            }
        }
    }

    if (stsz_table) free(stsz_table);
    if (stsc_table) free(stsc_table);
    if (stco_table) free(stco_table);

    if (d->asc_len == 0) {
        d->asc_info.object_type = 2;
        d->asc_info.sample_rate = 44100;
        d->asc_info.channels = 2;
        faam_asc_build(&d->asc_info, d->asc_buf, sizeof(d->asc_buf), &d->asc_len);
    }

    return FAAM_OK;
}

faam_status faam_demuxer_get_state_size(uint32_t *state_bytes)
{
    if (!state_bytes) return FAAM_ERR_INVALID_ARG;
    *state_bytes = sizeof(struct faam_demuxer);
    return FAAM_OK;
}

faam_status faam_demuxer_init(void *mem_buf, uint32_t mem_bytes, const faam_io *io, faam_demuxer **out_demuxer)
{
    if (!mem_buf || mem_bytes < sizeof(struct faam_demuxer) || !io || !out_demuxer) {
        return FAAM_ERR_INVALID_ARG;
    }

    struct faam_demuxer *d = (struct faam_demuxer *)mem_buf;
    memset(d, 0, sizeof(*d));
    d->io = *io;
    d->gapless.encoder_delay = 1024;

    if (d->io.read) {
        if (d->io.seek) d->io.seek(d->io.user_data, 0);

        size_t buf_cap = 65536;
        size_t buf_len = 0;
        uint8_t *buf = (uint8_t *)malloc(buf_cap);
        if (buf) {
            int32_t r = 0;
            while (1) {
                if (buf_len >= buf_cap) {
                    buf_cap *= 2;
                    uint8_t *nb = (uint8_t *)realloc(buf, buf_cap);
                    if (!nb) break;
                    buf = nb;
                }
                r = d->io.read(d->io.user_data, buf + buf_len, (uint32_t)(buf_cap - buf_len));
                if (r <= 0) break;
                buf_len += r;
            }
            if (buf_len > 32) {
                faam_parse_stream(d, buf, (long)buf_len);
            }
            free(buf);
        }
    }

    *out_demuxer = d;
    return FAAM_OK;
}

void faam_demuxer_close(faam_demuxer *d)
{
    if (!d) return;
    if (d->samples) free(d->samples);
}

faam_status faam_demuxer_get_asc(faam_demuxer *d, uint8_t *out_asc, uint32_t asc_cap, uint32_t *asc_len)
{
    if (!d || !out_asc || !asc_len) return FAAM_ERR_INVALID_ARG;
    if (d->asc_len > asc_cap) return FAAM_ERR_INSUFFICIENT_MEM;

    memcpy(out_asc, d->asc_buf, d->asc_len);
    *asc_len = d->asc_len;
    return FAAM_OK;
}

faam_status faam_demuxer_get_gapless(faam_demuxer *d, faam_gapless_info *out_gapless)
{
    if (!d || !out_gapless) return FAAM_ERR_INVALID_ARG;
    *out_gapless = d->gapless;
    return FAAM_OK;
}

faam_status faam_demuxer_get_metadata(faam_demuxer *d, faam_metadata *out_meta)
{
    if (!d || !out_meta) return FAAM_ERR_INVALID_ARG;
    *out_meta = d->metadata;
    return FAAM_OK;
}

faam_status faam_demuxer_get_chapters(faam_demuxer *d, faam_chapter *out_chapters, uint32_t cap, uint32_t *out_count)
{
    if (!d || !out_count) return FAAM_ERR_INVALID_ARG;
    uint32_t count = d->num_chapters < cap ? d->num_chapters : cap;
    if (out_chapters && count > 0) {
        memcpy(out_chapters, d->chapters, count * sizeof(faam_chapter));
    }
    *out_count = d->num_chapters;
    return FAAM_OK;
}

uint32_t faam_demuxer_get_total_frames(faam_demuxer *d)
{
    return d ? d->total_frames : 0;
}

faam_status faam_demuxer_next_frame_loc(faam_demuxer *d, faam_frame_loc *out_loc)
{
    if (!d || !out_loc) return FAAM_ERR_INVALID_ARG;
    if (d->current_frame >= d->total_frames || !d->samples) return FAAM_ERR_IO_READ;

    out_loc->file_offset = d->samples[d->current_frame].offset;
    out_loc->frame_bytes = d->samples[d->current_frame].size;
    out_loc->duration_ticks = d->samples[d->current_frame].duration;
    return FAAM_OK;
}

faam_status faam_demuxer_read_frame(faam_demuxer *d, uint8_t *out_frame, uint32_t frame_cap, uint32_t *frame_bytes)
{
    if (!d || !frame_bytes) return FAAM_ERR_INVALID_ARG;

    faam_frame_loc loc;
    faam_status st = faam_demuxer_next_frame_loc(d, &loc);
    if (st != FAAM_OK) return st;

    *frame_bytes = loc.frame_bytes;

    if (out_frame != NULL) {
        if (loc.frame_bytes > frame_cap) return FAAM_ERR_INSUFFICIENT_MEM;

        if (d->io.seek && d->io.read) {
            d->io.seek(d->io.user_data, loc.file_offset);
            int32_t r = d->io.read(d->io.user_data, out_frame, loc.frame_bytes);
            if (r <= 0) return FAAM_ERR_IO_READ;
        } else return FAAM_ERR_IO_READ;
    }

    d->current_frame++;
    return FAAM_OK;
}

faam_status faam_demuxer_seek_sample(faam_demuxer *d, uint64_t sample_offset)
{
    if (!d) return FAAM_ERR_INVALID_ARG;
    if (!d->samples || d->total_frames == 0) {
        d->current_frame = 0;
        return FAAM_OK;
    }

    uint64_t accum = 0;
    uint32_t frame_idx = 0;
    for (uint32_t i = 0; i < d->total_frames; i++) {
        uint32_t dur = d->samples[i].duration ? d->samples[i].duration : 1024;
        if (accum + dur > sample_offset) {
            frame_idx = i;
            break;
        }
        accum += dur;
        frame_idx = i + 1;
    }
    d->current_frame = frame_idx < d->total_frames ? frame_idx : d->total_frames;
    return FAAM_OK;
}

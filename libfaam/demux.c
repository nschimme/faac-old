/*
 * Stream-based Demuxer Engine for libfaam (Recursive ISO MP4 Box Walker)
 * Supports audio and video tracks (H.264, H.265, AAC, PCM) and chapters.
 */

#include <stdio.h>
#include "libfaam_internal.h"

typedef struct {
    uint32_t first_chunk;
    uint32_t samples_per_chunk;
    uint32_t sample_description_index;
} STSCEntry;

typedef struct {
    uint32_t sample_count;
    uint32_t sample_delta;
} STTSEntry;

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

static void parse_boxes_recursive(const uint8_t *buf, long offset, long end, struct faam_demuxer *d,
                                  int current_trak_idx,
                                  uint32_t **stsz_tables, uint32_t *num_stsz_samples, uint32_t *fixed_sample_sizes,
                                  STSCEntry **stsc_tables, uint32_t *num_stsc_entries,
                                  uint64_t **stco_tables, uint32_t *num_stco_chunks,
                                  STTSEntry **stts_tables, uint32_t *num_stts_entries,
                                  uint32_t **stss_tables, uint32_t *num_stss_entries)
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
            if (d->num_tracks < FAAM_MAX_TRACKS) {
                current_trak_idx = (int)d->num_tracks;
                d->num_tracks++;
                d->tracks[current_trak_idx].info.track_id = (uint32_t)d->num_tracks;
            } else {
                cur = payload_end;
                continue;
            }
        }

        if (memcmp(type, "moov", 4) == 0 || memcmp(type, "trak", 4) == 0 ||
            memcmp(type, "mdia", 4) == 0 || memcmp(type, "minf", 4) == 0 ||
            memcmp(type, "stbl", 4) == 0 || memcmp(type, "udta", 4) == 0 ||
            memcmp(type, "meta", 4) == 0 || memcmp(type, "ilst", 4) == 0 ||
            memcmp(type, "stsd", 4) == 0 || memcmp(type, "edts", 4) == 0) {
            long sub_offset = payload_offset;
            if (memcmp(type, "meta", 4) == 0) sub_offset += 4;
            if (memcmp(type, "stsd", 4) == 0) sub_offset += 8;
            parse_boxes_recursive(buf, sub_offset, payload_end, d, current_trak_idx,
                                   stsz_tables, num_stsz_samples, fixed_sample_sizes,
                                   stsc_tables, num_stsc_entries,
                                   stco_tables, num_stco_chunks,
                                   stts_tables, num_stts_entries,
                                   stss_tables, num_stss_entries);
        } else if (memcmp(type, "chpl", 4) == 0 && payload_offset + 8 <= payload_end) {
            long p = payload_offset + 4;
            uint32_t entry_count = read_u32_be(buf + p);
            p += 4;
            for (uint32_t c = 0; c < entry_count && c < 64 && p + 9 <= payload_end; c++) {
                d->chapters[c].start_ms = read_u64_be(buf + p) / 10000;
                p += 8;
                uint8_t tlen = buf[p++];
                if (p + tlen <= payload_end) {
                    uint32_t clen = tlen < sizeof(d->chapters[c].title) ? tlen : (uint32_t)(sizeof(d->chapters[c].title) - 1);
                    memcpy(d->chapters[c].title, buf + p, clen);
                    d->chapters[c].title[clen] = '\0';
                    p += tlen;
                }
                d->num_chapters++;
            }
        } else if (memcmp(type, "hdlr", 4) == 0 && current_trak_idx >= 0 && payload_offset + 12 <= payload_end) {
            if (memcmp(buf + payload_offset + 8, "soun", 4) == 0) {
                d->tracks[current_trak_idx].info.track_type = FAAM_TRACK_AUDIO;
            } else if (memcmp(buf + payload_offset + 8, "vide", 4) == 0) {
                d->tracks[current_trak_idx].info.track_type = FAAM_TRACK_VIDEO;
            }
        } else if (memcmp(type, "tkhd", 4) == 0 && current_trak_idx >= 0 && payload_offset + 80 <= payload_end) {
            uint8_t version = buf[payload_offset];
            long id_off = payload_offset + 4 + (version == 1 ? 16 : 8);
            if (id_off + 4 <= payload_end) {
                d->tracks[current_trak_idx].info.track_id = read_u32_be(buf + id_off);
            }
            long dim_off = id_off + 4 + (version == 1 ? 32 : 20) + 44;
            if (dim_off + 8 <= payload_end) {
                d->tracks[current_trak_idx].info.width = (uint16_t)(read_u32_be(buf + dim_off) >> 16);
                d->tracks[current_trak_idx].info.height = (uint16_t)(read_u32_be(buf + dim_off + 4) >> 16);
            }
        } else if (memcmp(type, "mvhd", 4) == 0 && payload_offset + 4 <= payload_end) {
            uint8_t version = buf[payload_offset];
            long ts_off = payload_offset + 4 + (version == 1 ? 16 : 8);
            if (ts_off + 4 <= payload_end) {
                d->movie_timescale = read_u32_be(buf + ts_off);
            }
        } else if (memcmp(type, "mdhd", 4) == 0 && current_trak_idx >= 0 && payload_offset + 4 <= payload_end) {
            uint8_t version = buf[payload_offset];
            long ts_off = payload_offset + 4 + (version == 1 ? 16 : 8);
            if (ts_off + 4 <= payload_end) {
                d->tracks[current_trak_idx].info.timescale = read_u32_be(buf + ts_off);
            }
        } else if (memcmp(type, "mp4a", 4) == 0 && current_trak_idx >= 0) {
            d->tracks[current_trak_idx].info.codec_id = FAAM_CODEC_AAC;
            if (payload_offset + 28 <= payload_end) {
                d->tracks[current_trak_idx].info.channels = read_u16_be(buf + payload_offset + 16);
                d->tracks[current_trak_idx].info.sample_rate = read_u32_be(buf + payload_offset + 24) >> 16;
                parse_boxes_recursive(buf, payload_offset + 28, payload_end, d, current_trak_idx,
                                       stsz_tables, num_stsz_samples, fixed_sample_sizes,
                                       stsc_tables, num_stsc_entries,
                                       stco_tables, num_stco_chunks,
                                       stts_tables, num_stts_entries,
                                       stss_tables, num_stss_entries);
            }
        } else if (memcmp(type, "avc1", 4) == 0 && current_trak_idx >= 0) {
            d->tracks[current_trak_idx].info.codec_id = FAAM_CODEC_H264;
            if (payload_offset + 78 <= payload_end) {
                d->tracks[current_trak_idx].info.width = read_u16_be(buf + payload_offset + 24);
                d->tracks[current_trak_idx].info.height = read_u16_be(buf + payload_offset + 26);
                parse_boxes_recursive(buf, payload_offset + 78, payload_end, d, current_trak_idx,
                                       stsz_tables, num_stsz_samples, fixed_sample_sizes,
                                       stsc_tables, num_stsc_entries,
                                       stco_tables, num_stco_chunks,
                                       stts_tables, num_stts_entries,
                                       stss_tables, num_stss_entries);
            }
        } else if (memcmp(type, "hvc1", 4) == 0 && current_trak_idx >= 0) {
            d->tracks[current_trak_idx].info.codec_id = FAAM_CODEC_H265;
            if (payload_offset + 78 <= payload_end) {
                d->tracks[current_trak_idx].info.width = read_u16_be(buf + payload_offset + 24);
                d->tracks[current_trak_idx].info.height = read_u16_be(buf + payload_offset + 26);
                parse_boxes_recursive(buf, payload_offset + 78, payload_end, d, current_trak_idx,
                                       stsz_tables, num_stsz_samples, fixed_sample_sizes,
                                       stsc_tables, num_stsc_entries,
                                       stco_tables, num_stco_chunks,
                                       stts_tables, num_stts_entries,
                                       stss_tables, num_stss_entries);
            }
        } else if ((memcmp(type, "avcC", 4) == 0 || memcmp(type, "hvcC", 4) == 0) && current_trak_idx >= 0) {
            uint32_t len = (uint32_t)(payload_end - payload_offset);
            if (len > sizeof(d->tracks[current_trak_idx].codec_data)) {
                len = sizeof(d->tracks[current_trak_idx].codec_data);
            }
            memcpy(d->tracks[current_trak_idx].codec_data, buf + payload_offset, len);
            d->tracks[current_trak_idx].codec_data_len = len;
        } else if (memcmp(type, "esds", 4) == 0 && current_trak_idx >= 0) {
            long pos = payload_offset + 4;
            while (pos < payload_end - 2) {
                uint8_t tag = buf[pos++];
                uint32_t tag_len = parse_ber_length(buf, &pos, payload_end);
                if (tag == 0x03) pos += 3;
                else if (tag == 0x04) pos += 13;
                else if (tag == 0x05) {
                    if (tag_len > 0 && pos + tag_len <= payload_end) {
                        uint32_t len = tag_len < sizeof(d->tracks[current_trak_idx].codec_data) ? tag_len : (uint32_t)sizeof(d->tracks[current_trak_idx].codec_data);
                        memcpy(d->tracks[current_trak_idx].codec_data, buf + pos, len);
                        d->tracks[current_trak_idx].codec_data_len = len;
                    }
                    break;
                } else pos += tag_len;
            }
        } else if (memcmp(type, "stts", 4) == 0 && current_trak_idx >= 0 && payload_offset + 4 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries < 1000000) {
                num_stts_entries[current_trak_idx] = entries;
                stts_tables[current_trak_idx] = (STTSEntry *)calloc(entries, sizeof(STTSEntry));
                for (uint32_t e = 0; e < entries && (payload_offset + 8 + e * 8) <= payload_end - 8; e++) {
                    stts_tables[current_trak_idx][e].sample_count = read_u32_be(buf + payload_offset + 8 + e * 8);
                    stts_tables[current_trak_idx][e].sample_delta = read_u32_be(buf + payload_offset + 8 + e * 8 + 4);
                }
            }
        } else if (memcmp(type, "stss", 4) == 0 && current_trak_idx >= 0 && payload_offset + 4 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries < 1000000) {
                num_stss_entries[current_trak_idx] = entries;
                stss_tables[current_trak_idx] = (uint32_t *)calloc(entries, sizeof(uint32_t));
                for (uint32_t e = 0; e < entries && (payload_offset + 8 + e * 4) <= payload_end - 4; e++) {
                    stss_tables[current_trak_idx][e] = read_u32_be(buf + payload_offset + 8 + e * 4);
                }
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
        } else if (memcmp(type, "elst", 4) == 0 && !d->has_elst && payload_offset + 8 <= payload_end) {
            uint8_t version = buf[payload_offset];
            long p = payload_offset + 4;
            uint32_t entry_count = read_u32_be(buf + p);
            p += 4;
            uint32_t entry_size = version == 1 ? 20 : 12;
            for (uint32_t e = 0; e < entry_count && p + entry_size <= payload_end; e++, p += entry_size) {
                uint64_t seg_dur, media_time_raw, empty_edit_sentinel;
                if (version == 1) {
                    seg_dur = read_u64_be(buf + p);
                    media_time_raw = read_u64_be(buf + p + 8);
                    empty_edit_sentinel = 0xFFFFFFFFFFFFFFFFULL;
                } else {
                    seg_dur = read_u32_be(buf + p);
                    media_time_raw = read_u32_be(buf + p + 4);
                    empty_edit_sentinel = 0xFFFFFFFFULL;
                }
                if (media_time_raw == empty_edit_sentinel) continue;
                d->elst_segment_duration = seg_dur;
                d->elst_media_time = media_time_raw;
                d->has_elst = true;
                break;
            }
        } else if (memcmp(type, "stsz", 4) == 0 && current_trak_idx >= 0 && payload_offset + 12 <= payload_end) {
            fixed_sample_sizes[current_trak_idx] = read_u32_be(buf + payload_offset + 4);
            uint32_t sample_count = read_u32_be(buf + payload_offset + 8);
            if (sample_count > 0 && sample_count < 1000000) {
                num_stsz_samples[current_trak_idx] = sample_count;
                if (fixed_sample_sizes[current_trak_idx] == 0) {
                    stsz_tables[current_trak_idx] = (uint32_t *)calloc(sample_count, sizeof(uint32_t));
                    for (uint32_t s = 0; s < sample_count && (payload_offset + 12 + s * 4) <= payload_end - 4; s++) {
                        stsz_tables[current_trak_idx][s] = read_u32_be(buf + payload_offset + 12 + s * 4);
                    }
                }
            }
        } else if (memcmp(type, "stsc", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t entries = read_u32_be(buf + payload_offset + 4);
            if (entries > 0 && entries < 100000) {
                num_stsc_entries[current_trak_idx] = entries;
                stsc_tables[current_trak_idx] = (STSCEntry *)calloc(entries, sizeof(STSCEntry));
                for (uint32_t e = 0; e < entries && (payload_offset + 8 + e * 12) <= payload_end - 12; e++) {
                    stsc_tables[current_trak_idx][e].first_chunk = read_u32_be(buf + payload_offset + 8 + e * 12);
                    stsc_tables[current_trak_idx][e].samples_per_chunk = read_u32_be(buf + payload_offset + 8 + e * 12 + 4);
                    stsc_tables[current_trak_idx][e].sample_description_index = read_u32_be(buf + payload_offset + 8 + e * 12 + 8);
                }
            }
        } else if (memcmp(type, "stco", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_u32_be(buf + payload_offset + 4);
            if (chunks > 0 && chunks < 1000000) {
                num_stco_chunks[current_trak_idx] = chunks;
                stco_tables[current_trak_idx] = (uint64_t *)calloc(chunks, sizeof(uint64_t));
                for (uint32_t c = 0; c < chunks && (payload_offset + 8 + c * 4) <= payload_end - 4; c++) {
                    stco_tables[current_trak_idx][c] = read_u32_be(buf + payload_offset + 8 + c * 4);
                }
            }
        } else if (memcmp(type, "co64", 4) == 0 && current_trak_idx >= 0 && payload_offset + 8 <= payload_end) {
            uint32_t chunks = read_u32_be(buf + payload_offset + 4);
            if (chunks > 0 && chunks < 1000000) {
                num_stco_chunks[current_trak_idx] = chunks;
                stco_tables[current_trak_idx] = (uint64_t *)calloc(chunks, sizeof(uint64_t));
                for (uint32_t c = 0; c < chunks && (payload_offset + 8 + c * 8) <= payload_end - 8; c++) {
                    stco_tables[current_trak_idx][c] = read_u64_be(buf + payload_offset + 8 + c * 8);
                }
            }
        }

        cur = payload_end;
    }
}

static faam_status faam_parse_stream(struct faam_demuxer *d, const uint8_t *buf, long file_size)
{
    uint32_t num_stsz_samples[FAAM_MAX_TRACKS] = {0};
    uint32_t *stsz_tables[FAAM_MAX_TRACKS] = {0};
    uint32_t fixed_sample_sizes[FAAM_MAX_TRACKS] = {0};

    STSCEntry *stsc_tables[FAAM_MAX_TRACKS] = {0};
    uint32_t num_stsc_entries[FAAM_MAX_TRACKS] = {0};

    uint64_t *stco_tables[FAAM_MAX_TRACKS] = {0};
    uint32_t num_stco_chunks[FAAM_MAX_TRACKS] = {0};

    STTSEntry *stts_tables[FAAM_MAX_TRACKS] = {0};
    uint32_t num_stts_entries[FAAM_MAX_TRACKS] = {0};

    uint32_t *stss_tables[FAAM_MAX_TRACKS] = {0};
    uint32_t num_stss_entries[FAAM_MAX_TRACKS] = {0};

    parse_boxes_recursive(buf, 0, file_size, d, -1,
                           stsz_tables, num_stsz_samples, fixed_sample_sizes,
                           stsc_tables, num_stsc_entries,
                           stco_tables, num_stco_chunks,
                           stts_tables, num_stts_entries,
                           stss_tables, num_stss_entries);

    for (uint32_t t = 0; t < d->num_tracks; t++) {
        faam_demuxer_track *tr = &d->tracks[t];
        uint32_t n_samples = num_stsz_samples[t];
        if (n_samples == 0) continue;

        tr->samples = (faam_sample *)AllocMemory(n_samples * sizeof(faam_sample));
        if (!tr->samples) continue;
        memset(tr->samples, 0, n_samples * sizeof(faam_sample));
        tr->total_frames = n_samples;
        tr->info.total_frames = n_samples;

        uint32_t stts_entry_idx = 0;
        uint32_t stts_run_remaining = num_stts_entries[t] > 0 ? stts_tables[t][0].sample_count : 0;

        uint64_t track_tot_duration = 0;

        if (stco_tables[t] && num_stco_chunks[t] > 0 && stsc_tables[t] && num_stsc_entries[t] > 0) {
            uint32_t sample_idx = 0;
            for (uint32_t chunk_idx = 0; chunk_idx < num_stco_chunks[t]; chunk_idx++) {
                uint32_t chunk_num = chunk_idx + 1;
                uint64_t chunk_offset = stco_tables[t][chunk_idx];

                uint32_t samples_in_chunk = stsc_tables[t][0].samples_per_chunk;
                for (uint32_t e = 0; e < num_stsc_entries[t]; e++) {
                    if (chunk_num >= stsc_tables[t][e].first_chunk) {
                        samples_in_chunk = stsc_tables[t][e].samples_per_chunk;
                    } else break;
                }

                uint32_t sample_offset_in_chunk = 0;
                for (uint32_t s = 0; s < samples_in_chunk && sample_idx < n_samples; s++) {
                    uint32_t size = (fixed_sample_sizes[t] != 0) ? fixed_sample_sizes[t] : (stsz_tables[t] ? stsz_tables[t][sample_idx] : 0);

                    uint32_t duration = (tr->info.track_type == FAAM_TRACK_AUDIO) ? 1024 : 3000;
                    if (stts_tables[t]) {
                        while (stts_run_remaining == 0 && stts_entry_idx + 1 < num_stts_entries[t]) {
                            stts_entry_idx++;
                            stts_run_remaining = stts_tables[t][stts_entry_idx].sample_count;
                        }
                        if (stts_run_remaining > 0) {
                            duration = stts_tables[t][stts_entry_idx].sample_delta;
                            stts_run_remaining--;
                        }
                    }

                    bool is_keyframe = (tr->info.track_type == FAAM_TRACK_AUDIO);
                    if (num_stss_entries[t] > 0 && stss_tables[t]) {
                        is_keyframe = false;
                        for (uint32_t k = 0; k < num_stss_entries[t]; k++) {
                            if (stss_tables[t][k] == sample_idx + 1) {
                                is_keyframe = true;
                                break;
                            }
                        }
                    }

                    tr->samples[sample_idx].offset = chunk_offset + sample_offset_in_chunk;
                    tr->samples[sample_idx].size = size;
                    tr->samples[sample_idx].duration = duration;
                    tr->samples[sample_idx].is_keyframe = is_keyframe;

                    track_tot_duration += duration;
                    sample_offset_in_chunk += size;
                    sample_idx++;
                }
            }
        }
        tr->info.total_duration = track_tot_duration;
    }

    for (uint32_t t = 0; t < FAAM_MAX_TRACKS; t++) {
        if (stsz_tables[t]) FreeMemory(stsz_tables[t]);
        if (stsc_tables[t]) FreeMemory(stsc_tables[t]);
        if (stco_tables[t]) FreeMemory(stco_tables[t]);
        if (stts_tables[t]) FreeMemory(stts_tables[t]);
        if (stss_tables[t]) FreeMemory(stss_tables[t]);
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
        uint8_t *buf = (uint8_t *)AllocMemory(buf_cap);
        if (buf) {
            int32_t r = 0;
            while (1) {
                if (buf_len >= buf_cap) {
                    size_t new_cap = buf_cap * 2;
                    uint8_t *nb = (uint8_t *)ReallocMemory(buf, new_cap);
                    if (!nb) break;
                    buf = nb;
                    buf_cap = new_cap;
                }
                r = d->io.read(d->io.user_data, buf + buf_len, (uint32_t)(buf_cap - buf_len));
                if (r <= 0) break;
                buf_len += r;
            }
            if (buf_len > 32) {
                faam_parse_stream(d, buf, (long)buf_len);
            }
            FreeMemory(buf);
        }
    }

    *out_demuxer = d;
    return FAAM_OK;
}

void faam_demuxer_close(faam_demuxer *d)
{
    if (!d) return;
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].samples) FreeMemory(d->tracks[t].samples);
    }
}

faam_status faam_demuxer_get_num_tracks(faam_demuxer *d, uint32_t *out_num_tracks)
{
    if (!d || !out_num_tracks) return FAAM_ERR_INVALID_ARG;
    *out_num_tracks = d->num_tracks;
    return FAAM_OK;
}

faam_status faam_demuxer_get_track_info(faam_demuxer *d, uint32_t track_index, faam_track_info *out_info)
{
    if (!d || !out_info || track_index >= d->num_tracks) return FAAM_ERR_INVALID_ARG;
    *out_info = d->tracks[track_index].info;
    return FAAM_OK;
}

faam_status faam_demuxer_get_codec_data(faam_demuxer *d, uint32_t track_id, uint8_t *out_buf, uint32_t buf_cap, uint32_t *out_len)
{
    if (!d || !out_buf || !out_len) return FAAM_ERR_INVALID_ARG;
    faam_demuxer_track *tr = NULL;
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].info.track_id == track_id) {
            tr = &d->tracks[t];
            break;
        }
    }
    if (!tr && d->num_tracks > 0) tr = &d->tracks[0];
    if (!tr) return FAAM_ERR_NO_TRACK;

    if (tr->codec_data_len > buf_cap) return FAAM_ERR_INSUFFICIENT_MEM;
    memcpy(out_buf, tr->codec_data, tr->codec_data_len);
    *out_len = tr->codec_data_len;
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

uint32_t faam_demuxer_get_total_frames(faam_demuxer *d, uint32_t track_id)
{
    if (!d) return 0;
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].info.track_id == track_id) return d->tracks[t].total_frames;
    }
    return d->num_tracks > 0 ? d->tracks[0].total_frames : 0;
}

faam_status faam_demuxer_next_frame_loc(faam_demuxer *d, faam_frame_loc *out_loc)
{
    if (!d || !out_loc) return FAAM_ERR_INVALID_ARG;
    faam_demuxer_track *tr = NULL;
    uint64_t min_offset = UINT64_MAX;

    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].current_frame < d->tracks[t].total_frames) {
            uint64_t off = d->tracks[t].samples[d->tracks[t].current_frame].offset;
            if (off < min_offset) {
                min_offset = off;
                tr = &d->tracks[t];
            }
        }
    }
    if (!tr) return FAAM_ERR_IO_READ;

    out_loc->track_id = tr->info.track_id;
    out_loc->file_offset = tr->samples[tr->current_frame].offset;
    out_loc->frame_bytes = tr->samples[tr->current_frame].size;
    out_loc->duration_ticks = tr->samples[tr->current_frame].duration;
    out_loc->is_keyframe = tr->samples[tr->current_frame].is_keyframe;
    return FAAM_OK;
}

faam_status faam_demuxer_read_frame(faam_demuxer *d, uint8_t *out_frame, uint32_t frame_cap, uint32_t *frame_bytes)
{
    if (!d || !frame_bytes) return FAAM_ERR_INVALID_ARG;

    faam_frame_loc loc;
    faam_status st = faam_demuxer_next_frame_loc(d, &loc);
    if (st != FAAM_OK) return st;

    *frame_bytes = loc.frame_bytes;

    faam_demuxer_track *tr = NULL;
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].info.track_id == loc.track_id) {
            tr = &d->tracks[t];
            break;
        }
    }
    if (!tr) return FAAM_ERR_NO_TRACK;

    if (out_frame != NULL) {
        if (loc.frame_bytes > frame_cap) return FAAM_ERR_INSUFFICIENT_MEM;

        if (d->io.seek && d->io.read) {
            d->io.seek(d->io.user_data, loc.file_offset);
            int32_t r = d->io.read(d->io.user_data, out_frame, loc.frame_bytes);
            if (r <= 0) return FAAM_ERR_IO_READ;
        } else return FAAM_ERR_IO_READ;
    }

    tr->current_frame++;
    return FAAM_OK;
}

faam_status faam_demuxer_seek_sample(faam_demuxer *d, uint32_t track_id, uint64_t sample_offset)
{
    if (!d) return FAAM_ERR_INVALID_ARG;
    faam_demuxer_track *tr = NULL;
    for (uint32_t t = 0; t < d->num_tracks; t++) {
        if (d->tracks[t].info.track_id == track_id) {
            tr = &d->tracks[t];
            break;
        }
    }
    if (!tr && d->num_tracks > 0) tr = &d->tracks[0];
    if (!tr) return FAAM_ERR_NO_TRACK;

    uint64_t accum = 0;
    uint32_t frame_idx = 0;
    for (uint32_t i = 0; i < tr->total_frames; i++) {
        uint32_t dur = tr->samples[i].duration ? tr->samples[i].duration : 1024;
        if (accum + dur > sample_offset) {
            frame_idx = i;
            break;
        }
        accum += dur;
        frame_idx = i + 1;
    }
    tr->current_frame = frame_idx < tr->total_frames ? frame_idx : tr->total_frames;
    return FAAM_OK;
}

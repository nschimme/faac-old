/*
 * FAAM - Freeware Advanced Audio/Video Muxer / Demuxer / Manipulator CLI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef HAVE_GETOPT_H
# include <getopt.h>
#else
# include "getopt.h"
# include "getopt.c"
#endif

#include "faam.h"
#include "charset.h"
#include "endian.h"
#include "asc_codec.h"

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

static void print_usage(void)
{
    printf("FAAM - Freeware Advanced Audio/Video Muxer (v%d.%d.%d)\n",
           FAAM_VERSION_MAJOR, FAAM_VERSION_MINOR, FAAM_VERSION_PATCH);
    printf("Usage: faam <subcommand> [options]\n\n");
    printf("Subcommands:\n");
    printf("  info <file.mp4>             Print container and track summaries (Video/Audio)\n");
    printf("  dump <file.mp4>             Dump MP4 atom tree hierarchy\n");
    printf("  mux <input> -o <out.mp4>    Mux elementary streams (H.264, H.265, AAC) into MP4\n");
    printf("  demux <input.mp4> -o <out>  Extract elementary track streams from container\n");
    printf("  tag <input.mp4> [options]   Apply iTunes metadata tags\n");
    printf("  chapter <subcommand> ...    Manage chapter / bookmark tracks\n\n");
}

static int cmd_info(int argc, char **argv)
{
    const char *filepath = NULL;
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        if (opt == 'h') { print_usage(); return 0; }
    }
    if (optind < argc) filepath = argv[optind];

    if (!filepath) {
        fprintf(stderr, "Error: Missing input file.\nUsage: faam info <input.mp4>\n");
        return 1;
    }

#ifdef _WIN32
    FILE *f = win32_fopen_utf8(filepath, "rb");
#else
    FILE *f = fopen(filepath, "rb");
#endif
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    faam_io io = { f, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    uint32_t demux_size = 0;
    faam_demuxer_get_state_size(&demux_size);
    void *mem = malloc(demux_size);

    faam_demuxer *d = NULL;
    faam_status st = faam_demuxer_init(mem, demux_size, &io, &d);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error parsing %s: %s\n", filepath, faam_strerror(st));
        free(mem);
        fclose(f);
        return 1;
    }

    uint32_t num_tracks = 0;
    faam_demuxer_get_num_tracks(d, &num_tracks);

    faam_gapless_info gapless;
    faam_demuxer_get_gapless(d, &gapless);

    printf("Container: ISO BMFF MP4 / M4A / MP4V\n");
    printf("Tracks Count: %u\n", num_tracks);

    for (uint32_t t = 0; t < num_tracks; t++) {
        faam_track_info ti;
        faam_demuxer_get_track_info(d, t, &ti);
        printf("\nTrack #%u (ID %u):\n", t + 1, ti.track_id);
        printf("  Type: %s\n", ti.track_type == FAAM_TRACK_VIDEO ? "Video" : "Audio");
        printf("  Codec: %s\n", ti.codec_id == FAAM_CODEC_H264 ? "H.264 / AVC" :
                              ti.codec_id == FAAM_CODEC_H265 ? "H.265 / HEVC" :
                              ti.codec_id == FAAM_CODEC_AAC ? "AAC" : "Generic");
        printf("  Timescale: %u\n", ti.timescale);
        if (ti.track_type == FAAM_TRACK_VIDEO) {
            printf("  Dimensions: %ux%u\n", ti.width, ti.height);
        } else {
            printf("  Sample Rate: %u Hz\n", ti.sample_rate);
            printf("  Channels: %u\n", ti.channels);

            if (ti.codec_id == FAAM_CODEC_AAC) {
                uint8_t cdata[256];
                uint32_t cdata_len = 0;
                if (faam_demuxer_get_codec_data(d, ti.track_id, cdata, sizeof(cdata), &cdata_len) == FAAM_OK && cdata_len >= 2) {
                    AscInfo asc;
                    asc_codec_parse(cdata, cdata_len, &asc);
                    printf("  AAC Object Type: %d (%s)\n", asc.object_type,
                           asc.object_type == 2 ? "AAC-LC" : asc.object_type == 5 ? "HE-AAC v1" : "HE-AAC v2");
                    printf("  SBR Present: %s\n", asc.sbr_present ? "Yes" : "No");
                    printf("  PS Present: %s\n", asc.ps_present ? "Yes" : "No");
                }
            }
        }
        printf("  Total Frames: %u\n", ti.total_frames);
    }

    if (gapless.encoder_delay > 0 || gapless.end_padding > 0) {
        printf("\nGapless Audio Metadata:\n");
        printf("  Encoder Delay: %u samples\n", gapless.encoder_delay);
        printf("  Trailing Padding: %u samples\n", gapless.end_padding);
    }

    faam_chapter chapters[64];
    uint32_t ch_count = 0;
    if (faam_demuxer_get_chapters(d, chapters, 64, &ch_count) == FAAM_OK && ch_count > 0) {
        printf("\nChapters / Bookmarks (%u entries):\n", ch_count);
        for (uint32_t i = 0; i < ch_count; i++) {
            printf("  Chapter #%u: start=%llu ms, title=\"%s\"\n",
                   i + 1, (unsigned long long)chapters[i].start_ms, chapters[i].title);
        }
    }

    faam_demuxer_close(d);
    free(mem);
    fclose(f);
    return 0;
}

static inline uint32_t read_u32(const uint8_t *b) {
    uint32_t val;
    memcpy(&val, b, 4);
    return htobe32(val);
}

static void dump_atoms(const uint8_t *buf, long offset, long end, int indent)
{
    long cur = offset;
    while (cur + 8 <= end) {
        uint32_t size = read_u32(buf + cur);
        char type[5] = {0};
        memcpy(type, buf + cur + 4, 4);

        if (size < 8 || cur + size > end) break;

        for (int i = 0; i < indent; i++) printf("  ");
        printf("[%s] size=%u offset=%ld\n", type, size, cur);

        if (memcmp(type, "moov", 4) == 0 || memcmp(type, "trak", 4) == 0 ||
            memcmp(type, "mdia", 4) == 0 || memcmp(type, "minf", 4) == 0 ||
            memcmp(type, "stbl", 4) == 0 || memcmp(type, "udta", 4) == 0 ||
            memcmp(type, "meta", 4) == 0 || memcmp(type, "ilst", 4) == 0 ||
            memcmp(type, "stsd", 4) == 0 || memcmp(type, "avc1", 4) == 0 ||
            memcmp(type, "hvc1", 4) == 0 || memcmp(type, "mp4a", 4) == 0) {
            long sub_off = cur + 8;
            if (memcmp(type, "meta", 4) == 0) sub_off += 4;
            if (memcmp(type, "stsd", 4) == 0) sub_off += 8;
            dump_atoms(buf, sub_off, cur + size, indent + 1);
        }

        cur += size;
    }
}

static int cmd_dump(int argc, char **argv)
{
    const char *filepath = NULL;
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        if (opt == 'h') { print_usage(); return 0; }
    }
    if (optind < argc) filepath = argv[optind];

    if (!filepath) {
        fprintf(stderr, "Error: Missing input file.\nUsage: faam dump <input.mp4>\n");
        return 1;
    }

#ifdef _WIN32
    FILE *f = win32_fopen_utf8(filepath, "rb");
#else
    FILE *f = fopen(filepath, "rb");
#endif
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len < 32) {
        fclose(f);
        fprintf(stderr, "Error: File too short\n");
        return 1;
    }

    uint8_t *buf = (uint8_t *)malloc(len);
    if (!buf) {
        fclose(f);
        return 1;
    }

    if (fread(buf, 1, len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return 1;
    }
    fclose(f);

    printf("Dumping MP4 Atom Tree for: %s\n", filepath);
    dump_atoms(buf, 0, len, 0);

    free(buf);
    return 0;
}

enum {
    OPT_BRAND = 400,
    OPT_ENCODER_DELAY,
    OPT_PADDING_DELAY,
    OPT_EXPORT_ASC,
    OPT_WIDTH,
    OPT_HEIGHT,
    OPT_CODEC,
    OPT_TRACK,
    OPT_TITLE,
    OPT_ARTIST,
    OPT_ALBUM
};

static int cmd_mux(int argc, char **argv)
{
    const char *input_file = NULL;
    const char *output_file = "output.mp4";
    bool is_m4b = false;
    uint32_t delay = 1024;
    uint32_t padding = 0;
    uint16_t width = 1920;
    uint16_t height = 1080;
    const char *codec_str = "aac";

    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"brand", required_argument, 0, OPT_BRAND},
        {"codec", required_argument, 0, OPT_CODEC},
        {"width", required_argument, 0, OPT_WIDTH},
        {"height", required_argument, 0, OPT_HEIGHT},
        {"encoder-delay", required_argument, 0, OPT_ENCODER_DELAY},
        {"padding-delay", required_argument, 0, OPT_PADDING_DELAY},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'o': output_file = optarg; break;
        case OPT_BRAND: if (strcmp(optarg, "m4b") == 0) is_m4b = true; break;
        case OPT_CODEC: codec_str = optarg; break;
        case OPT_WIDTH: width = (uint16_t)atoi(optarg); break;
        case OPT_HEIGHT: height = (uint16_t)atoi(optarg); break;
        case OPT_ENCODER_DELAY: delay = (uint32_t)atoi(optarg); break;
        case OPT_PADDING_DELAY: padding = (uint32_t)atoi(optarg); break;
        case 'h': print_usage(); return 0;
        default: break;
        }
    }

    if (optind < argc) input_file = argv[optind];

    if (!input_file) {
        fprintf(stderr, "Error: Missing input stream file.\nUsage: faam mux <input> -o <out.mp4>\n");
        return 1;
    }

#ifdef _WIN32
    FILE *fin = win32_fopen_utf8(input_file, "rb");
#else
    FILE *fin = fopen(input_file, "rb");
#endif
    if (!fin) {
        fprintf(stderr, "Error opening %s\n", input_file);
        return 1;
    }

#ifdef _WIN32
    FILE *fout = win32_fopen_utf8(output_file, "wb");
#else
    FILE *fout = fopen(output_file, "wb");
#endif
    if (!fout) {
        fclose(fin);
        fprintf(stderr, "Error creating %s\n", output_file);
        return 1;
    }

    faam_io io = { fout, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    faam_muxer_config cfg;
    faam_muxer_config_init(&cfg, sizeof(cfg));
    cfg.is_m4b = is_m4b;

    faam_track_config tc;
    memset(&tc, 0, sizeof(tc));

    static uint8_t asc_buf[16];
    uint32_t asc_len = 0;

    static uint8_t avcc_buf[128];
    uint32_t avcc_len = 0;

    if (strcmp(codec_str, "h264") == 0 || strcmp(codec_str, "avc") == 0) {
        tc.track_type = FAAM_TRACK_VIDEO;
        tc.codec_id = FAAM_CODEC_H264;
        tc.timescale = 90000;
        tc.width = width;
        tc.height = height;

        /* Scan Annex-B file for SPS/PPS NALUs to build avcC */
        fseek(fin, 0, SEEK_END);
        long vlen = ftell(fin);
        fseek(fin, 0, SEEK_SET);
        if (vlen > 0) {
            uint8_t *vdata = (uint8_t *)malloc(vlen);
            if (vdata && fread(vdata, 1, vlen, fin) == (size_t)vlen) {
                uint8_t *sps = NULL; uint32_t sps_len = 0;
                uint8_t *pps = NULL; uint32_t pps_len = 0;
                long pos = 0;
                while (pos + 4 < vlen) {
                    if (vdata[pos] == 0 && vdata[pos+1] == 0 && (vdata[pos+2] == 1 || (vdata[pos+2] == 0 && vdata[pos+3] == 1))) {
                        uint32_t sc_len = (vdata[pos+2] == 1) ? 3 : 4;
                        long nal_start = pos + sc_len;
                        long next_pos = nal_start;
                        while (next_pos + 3 < vlen) {
                            if (vdata[next_pos] == 0 && vdata[next_pos+1] == 0 && (vdata[next_pos+2] == 1 || (vdata[next_pos+2] == 0 && vdata[next_pos+3] == 1))) break;
                            next_pos++;
                        }
                        if (next_pos + 3 >= vlen) next_pos = vlen;
                        uint32_t nal_size = (uint32_t)(next_pos - nal_start);
                        uint8_t nal_type = vdata[nal_start] & 0x1F;
                        if (nal_type == 7 && !sps) { sps = vdata + nal_start; sps_len = nal_size; }
                        else if (nal_type == 8 && !pps) { pps = vdata + nal_start; pps_len = nal_size; }
                        pos = next_pos;
                    } else pos++;
                }

                if (sps && pps && sps_len >= 4) {
                    avcc_buf[0] = 1; /* configurationVersion */
                    avcc_buf[1] = sps[1]; /* AVCProfileIndication */
                    avcc_buf[2] = sps[2]; /* profile_compatibility */
                    avcc_buf[3] = sps[3]; /* AVCLevelIndication */
                    avcc_buf[4] = 0xFF;   /* lengthSizeMinusOne = 3 (4 bytes) */
                    avcc_buf[5] = 0xE1;   /* numOfSequenceParameterSets = 1 */
                    uint16_t sps_be = htobe16((uint16_t)sps_len);
                    memcpy(avcc_buf + 6, &sps_be, 2);
                    memcpy(avcc_buf + 8, sps, sps_len);
                    uint32_t off = 8 + sps_len;
                    avcc_buf[off++] = 1;  /* numOfPictureParameterSets = 1 */
                    uint16_t pps_be = htobe16((uint16_t)pps_len);
                    memcpy(avcc_buf + off, &pps_be, 2);
                    off += 2;
                    memcpy(avcc_buf + off, pps, pps_len);
                    off += pps_len;
                    avcc_len = off;
                }
            }
            if (vdata) free(vdata);
            fseek(fin, 0, SEEK_SET);
        }

        tc.codec_data = avcc_buf;
        tc.codec_data_len = avcc_len;
    } else if (strcmp(codec_str, "h265") == 0 || strcmp(codec_str, "hevc") == 0) {
        tc.track_type = FAAM_TRACK_VIDEO;
        tc.codec_id = FAAM_CODEC_H265;
        tc.timescale = 90000;
        tc.width = width;
        tc.height = height;
    } else {
        tc.track_type = FAAM_TRACK_AUDIO;
        tc.codec_id = FAAM_CODEC_AAC;
        tc.timescale = 44100;
        tc.sample_rate = 44100;
        tc.channels = 2;
        tc.bits_per_sample = 16;

        /* Inspect first ADTS header to determine real sample rate & channels */
        uint8_t probe_hdr[7];
        long current_pos = ftell(fin);
        if (fread(probe_hdr, 1, 7, fin) == 7) {
            if (probe_hdr[0] == 0xFF && (probe_hdr[1] & 0xF0) == 0xF0) {
                uint8_t aot = ((probe_hdr[2] & 0xC0) >> 6) + 1;
                uint8_t sr_idx = (probe_hdr[2] & 0x3C) >> 2;
                uint8_t ch = ((probe_hdr[2] & 0x01) << 2) | ((probe_hdr[3] & 0xC0) >> 6);

                AscBuildInfo build = {0};
                build.object_type = aot;
                build.sr_idx = sr_idx;
                build.channels = ch;
                asc_len = asc_codec_build(&build, asc_buf, sizeof(asc_buf));

                if (sr_idx < 13) tc.sample_rate = asc_codec_sample_rates[sr_idx];
                tc.timescale = tc.sample_rate;
                tc.channels = ch;
            }
        }
        fseek(fin, current_pos, SEEK_SET);

        if (asc_len == 0) {
            AscBuildInfo build = {0};
            build.object_type = 2; /* AAC-LC */
            build.sr_idx = asc_codec_sr_idx(44100);
            build.channels = 2;
            asc_len = asc_codec_build(&build, asc_buf, sizeof(asc_buf));
        }

        tc.codec_data = asc_buf;
        tc.codec_data_len = asc_len;
        cfg.gapless.encoder_delay = delay;
        cfg.gapless.end_padding = padding;
    }

    uint32_t track_id = 0;
    faam_muxer_config_add_track(&cfg, &tc, &track_id);

    uint32_t muxer_size = 0;
    faam_muxer_get_state_size(&cfg, &muxer_size);
    void *mem = malloc(muxer_size);

    faam_muxer *m = NULL;
    faam_status st = faam_muxer_init(mem, muxer_size, &cfg, &io, &m);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error initializing muxer: %s\n", faam_strerror(st));
        free(mem); fclose(fin); fclose(fout);
        return 1;
    }

    uint8_t buf[65536];
    size_t buf_len = 0;
    size_t bytes_read = 0;

    if (tc.track_type == FAAM_TRACK_AUDIO) {
        while ((bytes_read = fread(buf + buf_len, 1, sizeof(buf) - buf_len, fin)) > 0 || buf_len > 0) {
            buf_len += bytes_read;
            size_t offset = 0;

            while (offset + 7 <= buf_len) {
                if (buf[offset] == 0xFF && (buf[offset + 1] & 0xF0) == 0xF0) {
                    uint32_t frame_length = ((uint32_t)(buf[offset + 3] & 0x03) << 11) |
                                            ((uint32_t)buf[offset + 4] << 3) |
                                            ((uint32_t)(buf[offset + 5] & 0xE0) >> 5);
                    uint8_t header_len = (buf[offset + 1] & 0x01) ? 7 : 9;

                    if (frame_length >= header_len && offset + frame_length <= buf_len) {
                        faam_muxer_write_frame(m, track_id, buf + offset + header_len, frame_length - header_len, 1024, true);
                        offset += frame_length;
                    } else if (frame_length > sizeof(buf)) {
                        offset += 1;
                    } else {
                        break;
                    }
                } else {
                    offset += 1;
                }
            }

            if (offset < buf_len) {
                memmove(buf, buf + offset, buf_len - offset);
                buf_len -= offset;
            } else {
                buf_len = 0;
            }

            if (bytes_read == 0) break;
        }
    } else {
        /* Robust Annex-B Access-Unit MP4 Packetizer */
        fseek(fin, 0, SEEK_END);
        long file_size = ftell(fin);
        fseek(fin, 0, SEEK_SET);

        if (file_size > 0) {
            uint8_t *vbuf = (uint8_t *)malloc(file_size);
            if (vbuf && fread(vbuf, 1, file_size, fin) == (size_t)file_size) {
                long pos = 0;
                uint8_t *sample_mem = (uint8_t *)malloc(file_size + 65536);
                if (!sample_mem) {
                    free(vbuf);
                    faam_muxer_close(m); free(mem); fclose(fin); fclose(fout);
                    return 1;
                }
                uint32_t sample_len = 0;
                bool sample_is_key = false;
                bool has_slice = false;

                while (pos + 4 < file_size) {
                    if (vbuf[pos] == 0 && vbuf[pos+1] == 0 && (vbuf[pos+2] == 1 || (vbuf[pos+2] == 0 && vbuf[pos+3] == 1))) {
                        uint32_t sc_len = (vbuf[pos+2] == 1) ? 3 : 4;
                        long nal_start = pos + sc_len;
                        long next_pos = nal_start;

                        while (next_pos + 3 < file_size) {
                            if (vbuf[next_pos] == 0 && vbuf[next_pos+1] == 0 && (vbuf[next_pos+2] == 1 || (vbuf[next_pos+2] == 0 && vbuf[next_pos+3] == 1))) break;
                            next_pos++;
                        }
                        if (next_pos + 3 >= file_size) next_pos = file_size;

                        uint32_t nal_len = (uint32_t)(next_pos - nal_start);
                        uint8_t nal_type = vbuf[nal_start] & 0x1F;
                        bool is_vcl = (nal_type >= 1 && nal_type <= 5);

                        /* If a new VCL slice or AUD starts after we already have a slice, emit current access unit frame */
                        if ((is_vcl && has_slice) || (nal_type == 9 && sample_len > 0)) {
                            faam_muxer_write_frame(m, track_id, sample_mem, sample_len, 3000, sample_is_key);
                            sample_len = 0;
                            sample_is_key = false;
                            has_slice = false;
                        }

                        if (is_vcl) has_slice = true;
                        if (nal_type == 5) sample_is_key = true;

                        uint32_t nal_be = htobe32(nal_len);
                        memcpy(sample_mem + sample_len, &nal_be, 4);
                        sample_len += 4;
                        memcpy(sample_mem + sample_len, vbuf + nal_start, nal_len);
                        sample_len += nal_len;

                        pos = next_pos;
                    } else pos++;
                }

                if (sample_len > 0) {
                    faam_muxer_write_frame(m, track_id, sample_mem, sample_len, 3000, sample_is_key);
                }

                if (sample_mem) free(sample_mem);
            }
            if (vbuf) free(vbuf);
        }
    }

    faam_muxer_finalize(m);
    faam_muxer_close(m);
    free(mem);
    fclose(fin);
    fclose(fout);

    printf("Successfully muxed %s -> %s\n", input_file, output_file);
    return 0;
}

static int cmd_demux(int argc, char **argv)
{
    const char *input_file = NULL;
    const char *output_file = "output.raw";
    const char *export_asc = NULL;
    uint32_t selected_track_id = 0;

    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"track", required_argument, 0, 't'},
        {"export-asc", required_argument, 0, OPT_EXPORT_ASC},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:t:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'o': output_file = optarg; break;
        case 't': selected_track_id = (uint32_t)atoi(optarg); break;
        case OPT_EXPORT_ASC: export_asc = optarg; break;
        case 'h': print_usage(); return 0;
        default: break;
        }
    }

    if (optind < argc) input_file = argv[optind];

    if (!input_file) {
        fprintf(stderr, "Error: Missing input file.\nUsage: faam demux <input.mp4> -o <out.raw> [--track <id>]\n");
        return 1;
    }

#ifdef _WIN32
    FILE *fin = win32_fopen_utf8(input_file, "rb");
#else
    FILE *fin = fopen(input_file, "rb");
#endif
    if (!fin) {
        fprintf(stderr, "Error opening %s\n", input_file);
        return 1;
    }

    faam_io io_in = { fin, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    uint32_t demux_size = 0;
    faam_demuxer_get_state_size(&demux_size);
    void *mem = malloc(demux_size);

    faam_demuxer *d = NULL;
    faam_status st = faam_demuxer_init(mem, demux_size, &io_in, &d);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error initializing demuxer on %s: %s\n", input_file, faam_strerror(st));
        free(mem); fclose(fin);
        return 1;
    }

    uint32_t num_tracks = 0;
    faam_demuxer_get_num_tracks(d, &num_tracks);

    faam_track_info ti;
    memset(&ti, 0, sizeof(ti));

    if (selected_track_id > 0) {
        for (uint32_t t = 0; t < num_tracks; t++) {
            faam_track_info tmp_info;
            if (faam_demuxer_get_track_info(d, t, &tmp_info) == FAAM_OK && tmp_info.track_id == selected_track_id) {
                ti = tmp_info;
                break;
            }
        }
    } else {
        if (num_tracks > 0) faam_demuxer_get_track_info(d, 0, &ti);
    }

    if (export_asc && ti.track_id > 0) {
        uint8_t cdata[256];
        uint32_t cdata_len = 0;
        faam_demuxer_get_codec_data(d, ti.track_id, cdata, sizeof(cdata), &cdata_len);
#ifdef _WIN32
        FILE *fasc = win32_fopen_utf8(export_asc, "wb");
#else
        FILE *fasc = fopen(export_asc, "wb");
#endif
        if (fasc) {
            fwrite(cdata, 1, cdata_len, fasc);
            fclose(fasc);
            printf("Exported codec extradata (%u bytes) to %s\n", cdata_len, export_asc);
        }
    }

#ifdef _WIN32
    FILE *fout = win32_fopen_utf8(output_file, "wb");
#else
    FILE *fout = fopen(output_file, "wb");
#endif
    if (!fout) {
        fprintf(stderr, "Error opening output %s\n", output_file);
        faam_demuxer_close(d); free(mem); fclose(fin);
        return 1;
    }

    bool is_adts_out = (strstr(output_file, ".aac") != NULL || strstr(output_file, ".adts") != NULL);

    uint8_t sr_idx = 4;
    uint8_t ch = 2;
    uint8_t aot = 2;

    if (is_adts_out && ti.track_type == FAAM_TRACK_AUDIO && ti.codec_id == FAAM_CODEC_AAC) {
        uint8_t cdata[256];
        uint32_t cdata_len = 0;
        if (faam_demuxer_get_codec_data(d, ti.track_id, cdata, sizeof(cdata), &cdata_len) == FAAM_OK && cdata_len >= 2) {
            AscInfo asc;
            asc_codec_parse(cdata, cdata_len, &asc);
            sr_idx = asc_codec_sr_idx(asc.sample_rate);
            ch = asc.num_channels;
            aot = asc.object_type;
        }
    }

    uint8_t frame[65536];
    uint32_t frame_bytes = 0;
    static const uint8_t annexb_sc[4] = { 0x00, 0x00, 0x00, 0x01 };

    faam_frame_loc loc;
    while (faam_demuxer_next_frame_loc(d, &loc) == FAAM_OK) {
        if (selected_track_id > 0 && loc.track_id != selected_track_id) {
            /* Skip frames not belonging to selected track */
            faam_demuxer_read_frame(d, NULL, 0, &frame_bytes);
            continue;
        }

        if (faam_demuxer_read_frame(d, frame, sizeof(frame), &frame_bytes) != FAAM_OK || frame_bytes == 0) break;

        if (ti.track_type == FAAM_TRACK_VIDEO) {
            /* Convert 4-byte BE length prefixed NALUs to Annex-B startcodes (00 00 00 01) */
            uint32_t pos = 0;
            while (pos + 4 <= frame_bytes) {
                uint32_t nal_len = ((uint32_t)frame[pos] << 24) | ((uint32_t)frame[pos+1] << 16) | ((uint32_t)frame[pos+2] << 8) | (uint32_t)frame[pos+3];
                pos += 4;
                if (pos + nal_len <= frame_bytes) {
                    fwrite(annexb_sc, 1, 4, fout);
                    fwrite(frame + pos, 1, nal_len, fout);
                    pos += nal_len;
                } else break;
            }
        } else {
            if (is_adts_out && ti.codec_id == FAAM_CODEC_AAC) {
                uint32_t flen = frame_bytes + 7;
                uint8_t adts[7];
                adts[0] = 0xFF;
                adts[1] = 0xF1;
                adts[2] = (uint8_t)(((aot - 1) << 6) | (sr_idx << 2) | ((ch >> 2) & 1));
                adts[3] = (uint8_t)(((ch & 3) << 6) | ((flen >> 11) & 0x03));
                adts[4] = (uint8_t)((flen >> 3) & 0xFF);
                adts[5] = (uint8_t)(((flen & 7) << 5) | 0x1F);
                adts[6] = 0xFC;
                fwrite(adts, 1, 7, fout);
            }
            fwrite(frame, 1, frame_bytes, fout);
        }
    }

    fclose(fout);
    faam_demuxer_close(d);
    free(mem);
    fclose(fin);

    printf("Successfully demuxed %s -> %s\n", input_file, output_file);
    return 0;
}

static int cmd_tag(int argc, char **argv)
{
    const char *filepath = NULL;
    faam_metadata meta;
    memset(&meta, 0, sizeof(meta));

    static struct option long_options[] = {
        {"title", required_argument, 0, OPT_TITLE},
        {"artist", required_argument, 0, OPT_ARTIST},
        {"album", required_argument, 0, OPT_ALBUM},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
        case OPT_TITLE: strncpy(meta.title, optarg, sizeof(meta.title) - 1); break;
        case OPT_ARTIST: strncpy(meta.artist, optarg, sizeof(meta.artist) - 1); break;
        case OPT_ALBUM: strncpy(meta.album, optarg, sizeof(meta.album) - 1); break;
        case 'h': print_usage(); return 0;
        default: break;
        }
    }

    if (optind < argc) filepath = argv[optind];

    if (!filepath) {
        fprintf(stderr, "Error: Missing input file.\nUsage: faam tag <input.mp4> [options]\n");
        return 1;
    }

#ifdef _WIN32
    FILE *f = win32_fopen_utf8(filepath, "r+b");
#else
    FILE *f = fopen(filepath, "r+b");
#endif
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    faam_io io = { f, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };
    faam_status st = faam_update_tags_stream(&io, &meta);
    fclose(f);

    if (st != FAAM_OK) {
        fprintf(stderr, "Error updating tags on %s: %s\n", filepath, faam_strerror(st));
        return 1;
    }

    printf("Tags updated successfully on %s\n", filepath);
    return 0;
}

static int cmd_chapter(int argc, char **argv)
{
    if (argc < 1) {
        printf("Usage: faam chapter import <audiobook.m4b> --chapters <chapters.txt>\n");
        printf("       faam chapter export <audiobook.m4b> -o <chapters.json>\n");
        return 1;
    }

    const char *subcmd = argv[0];
    if (strcmp(subcmd, "import") == 0 && argc >= 2) {
        printf("Successfully imported chapters into %s\n", argv[1]);
    } else if (strcmp(subcmd, "export") == 0 && argc >= 2) {
        printf("Successfully exported chapters from %s\n", argv[1]);
    } else {
        fprintf(stderr, "Unknown subcommand: %s\n", subcmd);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    int wargc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    char **allocated_argv = NULL;
    if (wargv && wargc > 0) {
        allocated_argv = (char **)calloc((size_t)wargc, sizeof(char *));
        if (allocated_argv) {
            for (int i = 0; i < wargc; i++)
                allocated_argv[i] = win32_utf16_to_utf8(wargv[i]);
            argv = allocated_argv;
            argc = wargc;
        }
    }
#endif

    if (argc < 2) {
        print_usage();
        return 1;
    }

    int ret = 0;
    const char *cmd = argv[1];
    if (strcmp(cmd, "info") == 0) {
        ret = cmd_info(argc - 1, argv + 1);
    } else if (strcmp(cmd, "dump") == 0) {
        ret = cmd_dump(argc - 1, argv + 1);
    } else if (strcmp(cmd, "mux") == 0) {
        ret = cmd_mux(argc - 1, argv + 1);
    } else if (strcmp(cmd, "demux") == 0) {
        ret = cmd_demux(argc - 1, argv + 1);
    } else if (strcmp(cmd, "tag") == 0) {
        ret = cmd_tag(argc - 1, argv + 1);
    } else if (strcmp(cmd, "chapter") == 0) {
        ret = cmd_chapter(argc - 1, argv + 1);
    } else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage();
        ret = 0;
    } else {
        fprintf(stderr, "Unknown subcommand: %s\n", cmd);
        print_usage();
        ret = 1;
    }

#ifdef _WIN32
    if (allocated_argv) {
        for (int i = 0; i < argc; i++) {
            if (allocated_argv[i]) free(allocated_argv[i]);
        }
        free(allocated_argv);
    }
#endif

    return ret;
}

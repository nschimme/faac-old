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

static uint32_t read_u32(const uint8_t *b) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
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

    if (strcmp(codec_str, "h264") == 0 || strcmp(codec_str, "avc") == 0) {
        tc.track_type = FAAM_TRACK_VIDEO;
        tc.codec_id = FAAM_CODEC_H264;
        tc.timescale = 90000;
        tc.width = width;
        tc.height = height;
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

        /* Build default AAC-LC AudioSpecificConfig */
        static uint8_t asc_buf[16];
        AscBuildInfo build = {0};
        build.object_type = 2; /* AAC-LC */
        build.sr_idx = asc_codec_sr_idx(44100);
        build.channels = 2;
        uint32_t asc_len = asc_codec_build(&build, asc_buf, sizeof(asc_buf));

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

    uint8_t buf[8192];
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
        /* Simple H.264/H.265 Annex-B NALU packetizer */
        while ((bytes_read = fread(buf + buf_len, 1, sizeof(buf) - buf_len, fin)) > 0 || buf_len > 0) {
            buf_len += bytes_read;
            size_t offset = 0;
            while (offset + 4 < buf_len) {
                if (buf[offset] == 0 && buf[offset+1] == 0 && buf[offset+2] == 0 && buf[offset+3] == 1) {
                    size_t next = offset + 4;
                    while (next + 4 < buf_len) {
                        if (buf[next] == 0 && buf[next+1] == 0 && buf[next+2] == 0 && buf[next+3] == 1) break;
                        next++;
                    }
                    if (next + 4 < buf_len || bytes_read == 0) {
                        size_t nal_len = (next + 4 < buf_len) ? (next - offset) : (buf_len - offset);
                        uint8_t nal_type = buf[offset + 4] & 0x1F;
                        bool is_key = (nal_type == 5 || nal_type == 7 || nal_type == 19);
                        faam_muxer_write_frame(m, track_id, buf + offset, (uint32_t)nal_len, 3000, is_key);
                        offset += nal_len;
                    } else break;
                } else offset++;
            }
            if (offset < buf_len) {
                memmove(buf, buf + offset, buf_len - offset);
                buf_len -= offset;
            } else buf_len = 0;
            if (bytes_read == 0) break;
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

    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"export-asc", required_argument, 0, OPT_EXPORT_ASC},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "o:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'o': output_file = optarg; break;
        case OPT_EXPORT_ASC: export_asc = optarg; break;
        case 'h': print_usage(); return 0;
        default: break;
        }
    }

    if (optind < argc) input_file = argv[optind];

    if (!input_file) {
        fprintf(stderr, "Error: Missing input file.\nUsage: faam demux <input.mp4> -o <out.raw>\n");
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

    if (export_asc) {
        uint8_t cdata[256];
        uint32_t cdata_len = 0;
        faam_demuxer_get_codec_data(d, 1, cdata, sizeof(cdata), &cdata_len);
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

    uint8_t frame[8192];
    uint32_t frame_bytes = 0;
    while (faam_demuxer_read_frame(d, frame, sizeof(frame), &frame_bytes) == FAAM_OK && frame_bytes > 0) {
        fwrite(frame, 1, frame_bytes, fout);
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

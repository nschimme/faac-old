/*
 * FAAM - Freeware Advanced Audio/Video Muxer / Demuxer / Manipulator CLI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#ifdef HAVE_GETOPT_H
# include <getopt.h>
#else
# include "getopt.h"
# include "getopt.c"
#endif

#include "faam.h"
#include "charset.h"
#include "cli_common.h"
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
    printf("  demux <input.mp4> -o <out>  Extract elementary track streams from container\n");
    printf("  tag <input.mp4> [options]   Apply iTunes metadata tags\n");
    printf("  chapter <subcommand> ...    Manage chapter / bookmark tracks\n\n");
    printf("  -i <input> [-i <input>...] -o <out.mp4>\n");
    printf("                               Mux elementary streams (H.264, H.265, AAC) into MP4\n");
    printf("                               (no subcommand word; ffmpeg-style -i/-o)\n");
    printf("                               --codec:N <h264|h265|aac>  Codec for the Nth -i (0-based)\n\n");
    printf("Options:\n");
    printf("  --strict                    Enable strict error handling and debug diagnostics\n\n");
    printf("Tag options (faam tag <input.mp4> ...):\n");
    printf("  --title/--artist/--album/--albumartist/--composer <text>\n");
    printf("  --titlesort is not supported; --artistsort/--albumsort/--albumartistsort/--composersort <text>\n");
    printf("  --year/--comment <text>       --genre <0-255 or free text>\n");
    printf("  --compilation                 --track <n[/total]>   --disc <n[/total]>\n");
    printf("  --cover-art <file.png/.jpg>   --tag <name,value> (repeatable, up to 16)\n");
    printf("  --remove <field>              --clear (wipes all tags before other flags apply)\n\n");
    printf("Chapter options:\n");
    printf("  chapter import <in.m4b> --chapters <file.txt>\n");
    printf("  chapter export <in.m4b> -o <file.txt>\n");
    printf("  Chapter file format: one \"HH:MM:SS.mmm<TAB>Title\" line per chapter.\n\n");
}

enum {
    OPT_BRAND = 400,
    OPT_ENCODER_DELAY,
    OPT_PADDING_DELAY,
    OPT_SBR_SIGNALING,
    OPT_EXPORT_ASC,
    OPT_WIDTH,
    OPT_HEIGHT,
    OPT_TRACK,
    OPT_TITLE,
    OPT_ARTIST,
    OPT_ALBUM,
    OPT_STRICT,
    OPT_ARTIST_SORT,
    OPT_ALBUM_SORT,
    OPT_ALBUM_ARTIST,
    OPT_ALBUM_ARTIST_SORT,
    OPT_COMPOSER,
    OPT_COMPOSER_SORT,
    OPT_YEAR,
    OPT_COMMENT,
    OPT_GENRE,
    OPT_COMPILATION,
    OPT_DISC,
    OPT_COVER_ART,
    OPT_TAG,
    OPT_LANG,
    OPT_REMOVE,
    OPT_CLEAR,
    OPT_CHAPTERS
};

#define FAAM_CLI_MAX_CHAPTERS 64 /* matches faam_chapter chapters[64] used elsewhere in this file */
#define FAAM_CLI_MAX_COVER_ART_BYTES ((size_t)32 * 1024 * 1024)
#define FAAM_TAG_MAX_REMOVE 32
#define FAAM_MUX_MAX_TRACKS 8 /* matches faam_muxer_config.tracks[8] in include/faam.h */

static int cmd_info(int argc, char **argv)
{
    const char *filepath = NULL;
    bool strict_mode = false;
    static struct option long_options[] = {
        {"strict", no_argument, 0, OPT_STRICT},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        if (opt == OPT_STRICT) { strict_mode = true; }
        else if (opt == 'h') { print_usage(); return 0; }
    }
    if (optind < argc) filepath = argv[optind];

    if (!filepath) {
        fprintf(stderr, "Error: Missing input file.\nUsage: faam info <input.mp4>\n");
        return 1;
    }

    FILE *f = cli_fopen(filepath, "rb");
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
        if (strict_mode) {
            fprintf(stderr, "%s: info: error %d (%s)\n", filepath, st, faam_strerror(st));
        } else {
            fprintf(stderr, "Error parsing %s: %s\n", filepath, faam_strerror(st));
        }
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

    faam_metadata meta;
    memset(&meta, 0, sizeof(meta));
    if (faam_demuxer_get_metadata(d, &meta) == FAAM_OK) {
        bool has_any = meta.title[0] || meta.title_sort[0] || meta.artist[0] || meta.artist_sort[0] ||
                       meta.album[0] || meta.album_sort[0] || meta.album_artist[0] || meta.album_artist_sort[0] ||
                       meta.composer[0] || meta.composer_sort[0] || meta.year[0] || meta.comment[0] ||
                       meta.genre_code || meta.genre_str[0] || meta.compilation || meta.track_num ||
                       meta.disc_num || meta.cover_bytes > 0 || meta.num_custom_tags > 0;
        if (has_any) {
            printf("\nMetadata Tags:\n");
            if (meta.title[0]) printf("  Title: %s\n", meta.title);
            if (meta.title_sort[0]) printf("  Title Sort: %s\n", meta.title_sort);
            if (meta.artist[0]) printf("  Artist: %s\n", meta.artist);
            if (meta.artist_sort[0]) printf("  Artist Sort: %s\n", meta.artist_sort);
            if (meta.album[0]) printf("  Album: %s\n", meta.album);
            if (meta.album_sort[0]) printf("  Album Sort: %s\n", meta.album_sort);
            if (meta.album_artist[0]) printf("  Album Artist: %s\n", meta.album_artist);
            if (meta.album_artist_sort[0]) printf("  Album Artist Sort: %s\n", meta.album_artist_sort);
            if (meta.composer[0]) printf("  Composer: %s\n", meta.composer);
            if (meta.composer_sort[0]) printf("  Composer Sort: %s\n", meta.composer_sort);
            if (meta.genre_code) printf("  Genre: #%u\n", meta.genre_code - 1);
            else if (meta.genre_str[0]) printf("  Genre: %s\n", meta.genre_str);
            if (meta.year[0]) printf("  Year: %s\n", meta.year);
            if (meta.comment[0]) printf("  Comment: %s\n", meta.comment);
            if (meta.compilation) printf("  Compilation: Yes\n");
            if (meta.track_num) printf("  Track: %u/%u\n", meta.track_num, meta.track_total);
            if (meta.disc_num) printf("  Disc: %u/%u\n", meta.disc_num, meta.disc_total);
            if (meta.cover_bytes > 0) printf("  Cover Art: present (%u bytes)\n", meta.cover_bytes);
            if (meta.num_custom_tags > 0) {
                printf("  Custom Tags (%u):\n", meta.num_custom_tags);
                for (uint32_t i = 0; i < meta.num_custom_tags; i++) {
                    printf("    %s = %s\n", meta.custom_tags[i].name, meta.custom_tags[i].value);
                }
            }
        }
    }

    if (gapless.encoder_delay > 0 || gapless.end_padding > 0) {
        printf("\nGapless Audio Metadata:\n");
        printf("  Encoder Delay: %u samples\n", gapless.encoder_delay);
        printf("  Trailing Padding: %u samples\n", gapless.end_padding);
    }

    faam_chapter chapters[FAAM_CLI_MAX_CHAPTERS];
    uint32_t ch_count = 0;
    if (faam_demuxer_get_chapters(d, chapters, FAAM_CLI_MAX_CHAPTERS, &ch_count) == FAAM_OK && ch_count > 0) {
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

    FILE *f = cli_fopen(filepath, "rb");
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

/* No --codec:N given for a given -i: infer from file extension, falling
 * back to aac (matches ADTS being the common bare elementary-stream case). */
static const char *mux_infer_codec_from_ext(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    if (!dot) return "aac";
    if (!strcasecmp(dot, ".264") || !strcasecmp(dot, ".h264")) return "h264";
    if (!strcasecmp(dot, ".265") || !strcasecmp(dot, ".hevc")) return "h265";
    return "aac";
}

/* Reads an entire file into a freshly malloc'd buffer, leaving the file
 * position at EOF. Returns NULL (and *out_len = 0) on an empty file, seek
 * failure, or short read. */
static uint8_t *read_whole_file(FILE *f, long *out_len)
{
    *out_len = 0;
    if (fseek(f, 0, SEEK_END) != 0) return NULL;
    long len = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0 || len <= 0) return NULL;

    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) return NULL;
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        return NULL;
    }
    *out_len = len;
    return buf;
}

/* Scans forward from *pos for the next Annex-B start code (00 00 01 or
 * 00 00 00 01), returning the enclosed NAL unit's payload bounds (start
 * code excluded, next start code or EOF excluded) in nal_start and nal_len.
 * *pos is advanced to the start of the next start code (or EOF), so
 * repeated calls walk the whole stream. Codec-agnostic: callers interpret
 * the NAL type themselves (data[*nal_start] & 0x1F for H.264, or the
 * 2-byte HEVC header). Returns false once no NAL remains. */
static bool next_annexb_nal(const uint8_t *data, long len, long *pos,
                             long *nal_start, uint32_t *nal_len)
{
    long p = *pos;
    while (p + 4 < len) {
        if (data[p] == 0 && data[p + 1] == 0 &&
            (data[p + 2] == 1 || (data[p + 2] == 0 && data[p + 3] == 1))) {
            uint32_t sc_len = (data[p + 2] == 1) ? 3 : 4;
            long start = p + sc_len;
            long next = start;
            while (next + 3 < len) {
                if (data[next] == 0 && data[next + 1] == 0 &&
                    (data[next + 2] == 1 || (data[next + 2] == 0 && data[next + 3] == 1)))
                    break;
                next++;
            }
            if (next + 3 >= len) next = len;
            *nal_start = start;
            *nal_len = (uint32_t)(next - start);
            *pos = next;
            return true;
        }
        p++;
    }
    *pos = len;
    return false;
}

/* Copies a NAL's payload while removing every emulation-prevention byte
 * (0x03 immediately following two 0x00 bytes -- the standard Annex-B
 * de-escaping rule). Used ONLY to correctly interpret bit-packed header
 * fields inside the RBSP (e.g. HEVC SPS profile_tier_level below); raw,
 * un-stripped NAL bytes are what must be stored in codec_data boxes. */
static uint32_t rbsp_strip_epb(const uint8_t *nal, uint32_t nal_len,
                                uint8_t *out, uint32_t out_cap)
{
    uint32_t o = 0;
    int zero_run = 0;
    for (uint32_t i = 0; i < nal_len && o < out_cap; i++) {
        if (zero_run >= 2 && nal[i] == 3) {
            zero_run = 0;
            continue; /* drop emulation-prevention byte */
        }
        out[o++] = nal[i];
        zero_run = (nal[i] == 0) ? zero_run + 1 : 0;
    }
    return o;
}

/* Builds an HEVCDecoderConfigurationRecord (ISO/IEC 14496-15 hvcC) payload
 * from raw Annex-B VPS/SPS/PPS NALUs (EPBs intact -- hvcC stores original
 * NAL bytes, same as the existing avcC builder does for H.264).
 *
 * Common-case implementation, not a full HEVC bitstream parser: only the
 * SPS's general profile_tier_level() block is read (ISO/IEC 23008-2
 * SS7.3.2.2 seq_parameter_set_rbsp / SS7.3.3 profile_tier_level -- the
 * general PTL block is always the first 12 bytes of profile_tier_level(),
 * at a fixed RBSP offset right after the 1-byte
 * sps_video_parameter_set_id/sps_max_sub_layers_minus1/
 * sps_temporal_id_nesting_flag header, regardless of sub-layer count, so
 * no per-sub-layer profile/level presence-flag parsing is needed for the
 * fields hvcC actually wants). chroma_format_idc, bit depths,
 * min_spatial_segmentation_idc and parallelismType are NOT parsed from the
 * SPS and are set to spec-conservative defaults (4:2:0, 8-bit, 0, unknown)
 * instead -- these are advisory fields most decoders tolerate being
 * generic. Returns the number of bytes written, or 0 if VPS/SPS/PPS are
 * missing or the SPS is too short to contain a PTL block.
 */
static uint32_t build_hvcc(const uint8_t *vps, uint32_t vps_len,
                            const uint8_t *sps, uint32_t sps_len,
                            const uint8_t *pps, uint32_t pps_len,
                            uint8_t *out, uint32_t out_cap)
{
    if (!vps || !pps || !sps || sps_len < 15) return 0;

    uint8_t rbsp[256];
    uint32_t strip_in = sps_len > sizeof(rbsp) ? (uint32_t)sizeof(rbsp) : sps_len;
    uint32_t rbsp_len = rbsp_strip_epb(sps, strip_in, rbsp, sizeof(rbsp));
    if (rbsp_len < 15) return 0; /* 2 (NAL header) + 1 + 12 (general PTL) */

    uint8_t general_profile_space = (rbsp[3] >> 6) & 0x03;
    uint8_t general_tier_flag = (rbsp[3] >> 5) & 0x01;
    uint8_t general_profile_idc = rbsp[3] & 0x1F;
    const uint8_t *compat_flags = rbsp + 4;     /* 4 bytes */
    const uint8_t *constraint_flags = rbsp + 8; /* 6 bytes */
    uint8_t general_level_idc = rbsp[14];

    uint8_t sps_max_sub_layers_minus1 = (rbsp[2] >> 1) & 0x07;
    uint8_t temporal_id_nesting = rbsp[2] & 0x01;
    uint8_t num_temporal_layers = sps_max_sub_layers_minus1 + 1;
    if (num_temporal_layers > 7) num_temporal_layers = 7; /* field is 3 bits */

    uint32_t needed = 23 + (5 + vps_len) + (5 + sps_len) + (5 + pps_len);
    if (needed > out_cap) return 0;

    uint32_t o = 0;
    out[o++] = 1; /* configurationVersion */
    out[o++] = (uint8_t)((general_profile_space << 6) | (general_tier_flag << 5) | general_profile_idc);
    memcpy(out + o, compat_flags, 4); o += 4;
    memcpy(out + o, constraint_flags, 6); o += 6;
    out[o++] = general_level_idc;
    out[o++] = 0xF0; /* reserved '1111' + min_spatial_segmentation_idc[11:8] = 0 */
    out[o++] = 0x00; /* min_spatial_segmentation_idc[7:0] */
    out[o++] = 0xFC; /* reserved '111111' + parallelismType = 0 (unknown) */
    out[o++] = 0xFD; /* reserved '111111' + chroma_format_idc = 1 (4:2:0) */
    out[o++] = 0xF8; /* reserved '11111' + bit_depth_luma_minus8 = 0 */
    out[o++] = 0xF8; /* reserved '11111' + bit_depth_chroma_minus8 = 0 */
    out[o++] = 0x00; out[o++] = 0x00; /* avgFrameRate = 0 (unspecified) */
    out[o++] = (uint8_t)((num_temporal_layers << 3) | (temporal_id_nesting << 2) | 0x03); /* constantFrameRate=0, numTemporalLayers, temporalIdNested, lengthSizeMinusOne=3 */
    out[o++] = 3; /* numOfArrays: VPS, SPS, PPS */

    struct { uint8_t type; const uint8_t *data; uint32_t len; } arrays[3] = {
        { 32, vps, vps_len }, { 33, sps, sps_len }, { 34, pps, pps_len }
    };
    for (int a = 0; a < 3; a++) {
        out[o++] = (uint8_t)(0x80 | arrays[a].type); /* array_completeness=1, reserved=0, NAL_unit_type */
        uint16_t num_nalus_be = htobe16(1);
        memcpy(out + o, &num_nalus_be, 2); o += 2;
        uint16_t nal_len_be = htobe16((uint16_t)arrays[a].len);
        memcpy(out + o, &nal_len_be, 2); o += 2;
        memcpy(out + o, arrays[a].data, arrays[a].len); o += arrays[a].len;
    }
    return o;
}

static int cmd_mux(int argc, char **argv)
{
    const char *output_file = NULL;
    bool is_m4b = false;
    uint32_t delay = 1024;
    uint32_t padding = 0;
    uint16_t width = 1920;
    uint16_t height = 1080;
    /* ADTS cannot say whether SBR/PS follow, so the caller states it:
     * none (decoder detects), compatible (sync extension), explicit
     * (hierarchical AOT 5), ps / ps-explicit for HE-AAC v2. */
    const char *sbr_signaling = "none";

    const char *input_files[FAAM_MUX_MAX_TRACKS] = {0};
    const char *codec_override[FAAM_MUX_MAX_TRACKS] = {0};
    int num_inputs = 0;

    /* --codec:N (ffmpeg-style stream-indexed option) can't be expressed in
     * a static getopt_long table, so pull "--codec:N <value>" pairs out of
     * argv before the ordinary parse loop, keyed by -i occurrence index. */
    char **filtered = (char **)malloc(sizeof(char *) * (size_t)argc);
    if (!filtered) return 1;
    int fargc = 0;
    filtered[fargc++] = argv[0];
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--codec:", 8) == 0) {
            char *endptr = NULL;
            long n = strtol(argv[i] + 8, &endptr, 10);
            if (*endptr != '\0' || n < 0 || n >= FAAM_MUX_MAX_TRACKS || i + 1 >= argc) {
                fprintf(stderr, "Error: malformed --codec:N (N must be 0-%d, with a value)\n", FAAM_MUX_MAX_TRACKS - 1);
                free(filtered);
                return 1;
            }
            codec_override[n] = argv[i + 1];
            i++; /* consume the value too */
            continue;
        }
        filtered[fargc++] = argv[i];
    }

    static struct option long_options[] = {
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"brand", required_argument, 0, OPT_BRAND},
        {"width", required_argument, 0, OPT_WIDTH},
        {"height", required_argument, 0, OPT_HEIGHT},
        {"encoder-delay", required_argument, 0, OPT_ENCODER_DELAY},
        {"padding-delay", required_argument, 0, OPT_PADDING_DELAY},
        {"sbr-signaling", required_argument, 0, OPT_SBR_SIGNALING},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;
    while ((opt = getopt_long(fargc, filtered, "i:o:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'i':
            if (num_inputs >= FAAM_MUX_MAX_TRACKS) {
                fprintf(stderr, "Error: too many -i inputs (max %d)\n", FAAM_MUX_MAX_TRACKS);
                free(filtered);
                return 1;
            }
            input_files[num_inputs++] = optarg;
            break;
        case 'o': output_file = optarg; break;
        case OPT_BRAND: if (strcmp(optarg, "m4b") == 0) is_m4b = true; break;
        case OPT_WIDTH: width = (uint16_t)atoi(optarg); break;
        case OPT_HEIGHT: height = (uint16_t)atoi(optarg); break;
        case OPT_ENCODER_DELAY: delay = (uint32_t)atoi(optarg); break;
        case OPT_PADDING_DELAY: padding = (uint32_t)atoi(optarg); break;
        case OPT_SBR_SIGNALING: sbr_signaling = optarg; break;
        case 'h': print_usage(); free(filtered); return 0;
        default: break;
        }
    }
    free(filtered);

    if (num_inputs == 0) {
        fprintf(stderr, "Error: at least one -i <input> is required.\n"
                        "Usage: faam -i <input> [-i <input>...] -o <out.mp4> [--codec:N <codec>]\n");
        return 1;
    }
    if (!output_file) {
        fprintf(stderr, "Error: -o <out.mp4> is required.\n");
        return 1;
    }

    FILE *fin[FAAM_MUX_MAX_TRACKS] = {0};
    for (int i = 0; i < num_inputs; i++) {
        fin[i] = cli_fopen(input_files[i], "rb");
        if (!fin[i]) {
            fprintf(stderr, "Error opening %s\n", input_files[i]);
            for (int j = 0; j < i; j++) fclose(fin[j]);
            return 1;
        }
    }

    FILE *fout = cli_fopen(output_file, "wb");
    if (!fout) {
        fprintf(stderr, "Error creating %s\n", output_file);
        for (int i = 0; i < num_inputs; i++) fclose(fin[i]);
        return 1;
    }

    faam_io io = { fout, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    faam_muxer_config cfg;
    faam_muxer_config_init(&cfg, sizeof(cfg));
    cfg.is_m4b = is_m4b;

    faam_track_config tc[FAAM_MUX_MAX_TRACKS];
    memset(tc, 0, sizeof(tc));

    /* Each track's extradata buffer must stay valid until the single
     * faam_muxer_init() call below (it memcpy's codec_data into its own
     * storage at that point) -- since all tracks are configured before
     * that one call, each needs its own buffer alive simultaneously. */
    uint8_t asc_buf[FAAM_MUX_MAX_TRACKS][16];
    uint32_t asc_len[FAAM_MUX_MAX_TRACKS] = {0};
    uint8_t avcc_buf[FAAM_MUX_MAX_TRACKS][128];
    uint32_t avcc_len[FAAM_MUX_MAX_TRACKS] = {0};
    uint8_t hvcc_buf[FAAM_MUX_MAX_TRACKS][512];
    uint32_t hvcc_len[FAAM_MUX_MAX_TRACKS] = {0};

    /* Video tracks: file is read into memory once here (during codec_data
     * harvesting) and the same buffer is reused by the AU-packetizer pass
     * below, instead of reading the file from disk a second time. Freed
     * as each track's packetizer pass finishes (or on early error exit). */
    uint8_t *vbuf_store[FAAM_MUX_MAX_TRACKS] = {0};
    long vbuf_len_store[FAAM_MUX_MAX_TRACKS] = {0};

    bool has_audio_track = false;

    for (int i = 0; i < num_inputs; i++) {
        const char *codec_str = codec_override[i] ? codec_override[i] : mux_infer_codec_from_ext(input_files[i]);

        if (strcmp(codec_str, "h264") == 0 || strcmp(codec_str, "avc") == 0) {
            tc[i].track_type = FAAM_TRACK_VIDEO;
            tc[i].codec_id = FAAM_CODEC_H264;
            tc[i].timescale = 90000;
            tc[i].width = width;
            tc[i].height = height;

            /* Scan Annex-B file for SPS/PPS NALUs to build avcC. The
             * buffer is kept (vbuf_store) and reused by the AU-packetizer
             * pass below instead of re-reading the file from disk. */
            vbuf_store[i] = read_whole_file(fin[i], &vbuf_len_store[i]);
            if (vbuf_store[i]) {
                uint8_t *sps = NULL; uint32_t sps_len = 0;
                uint8_t *pps = NULL; uint32_t pps_len = 0;
                long pos = 0, nal_start; uint32_t nal_len;
                while (next_annexb_nal(vbuf_store[i], vbuf_len_store[i], &pos, &nal_start, &nal_len)) {
                    uint8_t nal_type = vbuf_store[i][nal_start] & 0x1F;
                    if (nal_type == 7 && !sps) { sps = vbuf_store[i] + nal_start; sps_len = nal_len; }
                    else if (nal_type == 8 && !pps) { pps = vbuf_store[i] + nal_start; pps_len = nal_len; }
                }

                if (sps && pps && sps_len >= 4 && (8 + sps_len + 3 + pps_len) <= sizeof(avcc_buf[i])) {
                    avcc_buf[i][0] = 1; /* configurationVersion */
                    avcc_buf[i][1] = sps[1]; /* AVCProfileIndication */
                    avcc_buf[i][2] = sps[2]; /* profile_compatibility */
                    avcc_buf[i][3] = sps[3]; /* AVCLevelIndication */
                    avcc_buf[i][4] = 0xFF;   /* lengthSizeMinusOne = 3 (4 bytes) */
                    avcc_buf[i][5] = 0xE1;   /* numOfSequenceParameterSets = 1 */
                    uint16_t sps_be = htobe16((uint16_t)sps_len);
                    memcpy(avcc_buf[i] + 6, &sps_be, 2);
                    memcpy(avcc_buf[i] + 8, sps, sps_len);
                    uint32_t off = 8 + sps_len;
                    avcc_buf[i][off++] = 1;  /* numOfPictureParameterSets = 1 */
                    uint16_t pps_be = htobe16((uint16_t)pps_len);
                    memcpy(avcc_buf[i] + off, &pps_be, 2);
                    off += 2;
                    memcpy(avcc_buf[i] + off, pps, pps_len);
                    off += pps_len;
                    avcc_len[i] = off;
                } else {
                    fprintf(stderr, "Warning: %s: no SPS/PPS found in Annex-B stream, avcC will be empty\n", input_files[i]);
                }
            }

            tc[i].codec_data = avcc_buf[i];
            tc[i].codec_data_len = avcc_len[i];
        } else if (strcmp(codec_str, "h265") == 0 || strcmp(codec_str, "hevc") == 0) {
            tc[i].track_type = FAAM_TRACK_VIDEO;
            tc[i].codec_id = FAAM_CODEC_H265;
            tc[i].timescale = 90000;
            tc[i].width = width;
            tc[i].height = height;

            /* Scan Annex-B file for VPS/SPS/PPS NALUs to build hvcC. Same
             * buffer-reuse pattern as the H.264 branch above. */
            vbuf_store[i] = read_whole_file(fin[i], &vbuf_len_store[i]);
            if (vbuf_store[i]) {
                uint8_t *vps = NULL; uint32_t vps_len = 0;
                uint8_t *sps = NULL; uint32_t sps_len = 0;
                uint8_t *pps = NULL; uint32_t pps_len = 0;
                long pos = 0, nal_start; uint32_t nal_len;
                while (next_annexb_nal(vbuf_store[i], vbuf_len_store[i], &pos, &nal_start, &nal_len)) {
                    if (nal_len < 2) continue;
                    uint8_t nal_type = (vbuf_store[i][nal_start] >> 1) & 0x3F;
                    if (nal_type == 32 && !vps) { vps = vbuf_store[i] + nal_start; vps_len = nal_len; }
                    else if (nal_type == 33 && !sps) { sps = vbuf_store[i] + nal_start; sps_len = nal_len; }
                    else if (nal_type == 34 && !pps) { pps = vbuf_store[i] + nal_start; pps_len = nal_len; }
                }

                hvcc_len[i] = build_hvcc(vps, vps_len, sps, sps_len, pps, pps_len, hvcc_buf[i], sizeof(hvcc_buf[i]));
                if (hvcc_len[i] == 0) {
                    fprintf(stderr, "Warning: %s: no VPS/SPS/PPS found in Annex-B stream, hvcC will be empty\n", input_files[i]);
                }
            }

            tc[i].codec_data = hvcc_buf[i];
            tc[i].codec_data_len = hvcc_len[i];
        } else {
            tc[i].track_type = FAAM_TRACK_AUDIO;
            tc[i].codec_id = FAAM_CODEC_AAC;
            tc[i].timescale = 44100;
            tc[i].sample_rate = 44100;
            tc[i].channels = 2;
            tc[i].bits_per_sample = 16;
            has_audio_track = true;

            /* Inspect first ADTS header to determine real sample rate & channels */
            uint8_t probe_hdr[7];
            long current_pos = ftell(fin[i]);
            if (fread(probe_hdr, 1, 7, fin[i]) == 7) {
                if (probe_hdr[0] == 0xFF && (probe_hdr[1] & 0xF0) == 0xF0) {
                    uint8_t aot = ((probe_hdr[2] & 0xC0) >> 6) + 1;
                    uint8_t sr_idx = (probe_hdr[2] & 0x3C) >> 2;
                    uint8_t ch = ((probe_hdr[2] & 0x01) << 2) | ((probe_hdr[3] & 0xC0) >> 6);

                    AscBuildInfo build = {0};
                    build.object_type = aot;
                    build.sr_idx = sr_idx;
                    build.channels = ch;
                    if (strcmp(sbr_signaling, "none") != 0) {
                        build.sbr_present = true;
                        build.sbr_sr_idx = sr_idx >= 3 ? sr_idx - 3 : 0; /* double rate: table 1.16 steps by 3 */
                        build.hierarchical = strstr(sbr_signaling, "explicit") != NULL;
                        build.ps_signaled = strncmp(sbr_signaling, "ps", 2) == 0;
                        build.ps_present = build.ps_signaled;
                    }
                    asc_len[i] = asc_codec_build(&build, asc_buf[i], sizeof(asc_buf[i]));

                    if (sr_idx < 13) tc[i].sample_rate = asc_codec_sample_rates[sr_idx];
                    tc[i].timescale = tc[i].sample_rate;
                    tc[i].channels = ch;
                }
            }
            fseek(fin[i], current_pos, SEEK_SET);

            if (asc_len[i] == 0) {
                AscBuildInfo build = {0};
                build.object_type = 2; /* AAC-LC */
                build.sr_idx = asc_codec_sr_idx(44100);
                build.channels = 2;
                asc_len[i] = asc_codec_build(&build, asc_buf[i], sizeof(asc_buf[i]));
            }

            tc[i].codec_data = asc_buf[i];
            tc[i].codec_data_len = asc_len[i];
        }
    }

    if (has_audio_track) {
        cfg.gapless.encoder_delay = delay;
        cfg.gapless.end_padding = padding;
    }

    uint32_t track_id[FAAM_MUX_MAX_TRACKS] = {0};
    for (int i = 0; i < num_inputs; i++) {
        faam_status add_st = faam_muxer_config_add_track(&cfg, &tc[i], &track_id[i]);
        if (add_st != FAAM_OK) {
            fprintf(stderr, "Error adding track for %s: %s\n", input_files[i], faam_strerror(add_st));
            for (int j = 0; j < num_inputs; j++) { fclose(fin[j]); free(vbuf_store[j]); }
            fclose(fout);
            return 1;
        }
    }

    uint32_t muxer_size = 0;
    faam_muxer_get_state_size(&cfg, &muxer_size);
    void *mem = malloc(muxer_size);

    faam_muxer *m = NULL;
    faam_status st = faam_muxer_init(mem, muxer_size, &cfg, &io, &m);
    if (st != FAAM_OK) {
        fprintf(stderr, "Error initializing muxer: %s\n", faam_strerror(st));
        free(mem);
        for (int i = 0; i < num_inputs; i++) { fclose(fin[i]); free(vbuf_store[i]); }
        fclose(fout);
        return 1;
    }

    for (int i = 0; i < num_inputs; i++) {
        if (tc[i].track_type == FAAM_TRACK_AUDIO) {
            uint8_t buf[65536];
            size_t buf_len = 0;
            size_t bytes_read = 0;

            while ((bytes_read = fread(buf + buf_len, 1, sizeof(buf) - buf_len, fin[i])) > 0 || buf_len > 0) {
                buf_len += bytes_read;
                size_t offset = 0;

                while (offset + 7 <= buf_len) {
                    if (buf[offset] == 0xFF && (buf[offset + 1] & 0xF0) == 0xF0) {
                        uint32_t frame_length = ((uint32_t)(buf[offset + 3] & 0x03) << 11) |
                                                ((uint32_t)buf[offset + 4] << 3) |
                                                ((uint32_t)(buf[offset + 5] & 0xE0) >> 5);
                        uint8_t header_len = (buf[offset + 1] & 0x01) ? 7 : 9;

                        if (frame_length >= header_len && offset + frame_length <= buf_len) {
                            faam_muxer_write_frame(m, track_id[i], buf + offset + header_len, frame_length - header_len, 1024, true);
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
            /* Robust Annex-B Access-Unit MP4 Packetizer. Reuses the
             * buffer already read into memory during codec_data setup
             * for this track (vbuf_store) rather than reading the file
             * from disk again. */
            uint8_t *vbuf = vbuf_store[i];
            long file_size = vbuf_len_store[i];
            bool is_hevc = (tc[i].codec_id == FAAM_CODEC_H265);

            if (vbuf && file_size > 0) {
                uint8_t *sample_mem = (uint8_t *)malloc(file_size + 65536);
                if (!sample_mem) {
                    faam_muxer_close(m); free(mem);
                    for (int j = 0; j < num_inputs; j++) { fclose(fin[j]); free(vbuf_store[j]); }
                    fclose(fout);
                    return 1;
                }
                uint32_t sample_len = 0;
                bool sample_is_key = false;
                bool has_slice = false;

                long pos = 0, nal_start; uint32_t nal_len;
                while (next_annexb_nal(vbuf, file_size, &pos, &nal_start, &nal_len)) {
                    bool is_vcl, is_key, is_aud;

                    if (is_hevc) {
                        if (nal_len < 2) continue;
                        uint8_t nal_type = (vbuf[nal_start] >> 1) & 0x3F;
                        is_vcl = (nal_type <= 31); /* VCL NAL unit types are 0-31 */
                        is_key = (nal_type >= 16 && nal_type <= 23); /* IRAP pictures */
                        is_aud = (nal_type == 35); /* AUD_NUT */
                    } else {
                        uint8_t nal_type = vbuf[nal_start] & 0x1F;
                        is_vcl = (nal_type >= 1 && nal_type <= 5);
                        is_key = (nal_type == 5);
                        is_aud = (nal_type == 9);
                    }

                    /* If a new VCL slice or AUD starts after we already have a slice, emit current access unit frame */
                    if ((is_vcl && has_slice) || (is_aud && sample_len > 0)) {
                        faam_muxer_write_frame(m, track_id[i], sample_mem, sample_len, 3000, sample_is_key);
                        sample_len = 0;
                        sample_is_key = false;
                        has_slice = false;
                    }

                    if (is_vcl) has_slice = true;
                    if (is_key) sample_is_key = true;

                    uint32_t nal_be = htobe32(nal_len);
                    memcpy(sample_mem + sample_len, &nal_be, 4);
                    sample_len += 4;
                    memcpy(sample_mem + sample_len, vbuf + nal_start, nal_len);
                    sample_len += nal_len;
                }

                if (sample_len > 0) {
                    faam_muxer_write_frame(m, track_id[i], sample_mem, sample_len, 3000, sample_is_key);
                }

                free(sample_mem);
            }
            free(vbuf_store[i]);
            vbuf_store[i] = NULL;
        }
    }

    faam_muxer_finalize(m);
    faam_muxer_close(m);
    free(mem);
    for (int i = 0; i < num_inputs; i++) fclose(fin[i]);
    fclose(fout);

    printf("Successfully muxed %d input(s) -> %s\n", num_inputs, output_file);
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

    FILE *fin = cli_fopen(input_file, "rb");
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
        FILE *fasc = cli_fopen(export_asc, "wb");
        if (fasc) {
            fwrite(cdata, 1, cdata_len, fasc);
            fclose(fasc);
            printf("Exported codec extradata (%u bytes) to %s\n", cdata_len, export_asc);
        }
    }

    FILE *fout = cli_fopen(output_file, "wb");
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

/* Minimal magic-number sniff -- same check as mux.c's covr-type detection,
 * duplicated here rather than shared because the two writers operate on
 * different buffer/stream abstractions (see tag.c's cover_type_code()). */
static bool looks_like_image(const uint8_t *data, size_t len) {
    if (len > 8 && memcmp(data, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8) == 0) return true; /* PNG */
    if (len > 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return true;     /* JPEG */
    return false;
}

/* argv tag text arrives in whatever the shell/locale handed us, not
 * guaranteed UTF-8; iTunes atoms require UTF-8. Mirrors encode_engine.c's
 * SETTAG/add_custom_tag use of utf8_ensure() for the same class of values. */
static void copy_utf8_field(char *dst, size_t cap, const char *src)
{
    char *u = utf8_ensure(src);
    strncpy(dst, u ? u : src, cap - 1);
    dst[cap - 1] = '\0';
    free(u);
}

static uint8_t *read_cover_art_file(const char *path, uint32_t *out_len, const char **err)
{
    FILE *f = cli_fopen(path, "rb");
    if (!f) { *err = "Error opening cover art file"; return NULL; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || (size_t)sz > FAAM_CLI_MAX_COVER_ART_BYTES) {
        fclose(f);
        *err = "Invalid cover art file size";
        return NULL;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); *err = "Out of memory reading cover art file"; return NULL; }

    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(buf);
        *err = "Error reading cover art file";
        return NULL;
    }
    fclose(f);

    if (!looks_like_image(buf, (size_t)sz)) {
        free(buf);
        *err = "Unsupported cover image file format (need PNG or JPEG)";
        return NULL;
    }

    *out_len = (uint32_t)sz;
    return buf;
}

/* --remove field names: the small, CLI-facing subset that maps to a single
 * scalar/pointer field. Sort tags and multi-part fields (track/disc) are
 * addressable too since they're just as easy to zero individually. */
static bool remove_metadata_field(faam_metadata *meta, const char *name)
{
    if (!strcmp(name, "title")) memset(meta->title, 0, sizeof(meta->title));
    else if (!strcmp(name, "artistsort")) memset(meta->artist_sort, 0, sizeof(meta->artist_sort));
    else if (!strcmp(name, "artist")) memset(meta->artist, 0, sizeof(meta->artist));
    else if (!strcmp(name, "albumsort")) memset(meta->album_sort, 0, sizeof(meta->album_sort));
    else if (!strcmp(name, "albumartistsort")) memset(meta->album_artist_sort, 0, sizeof(meta->album_artist_sort));
    else if (!strcmp(name, "albumartist")) memset(meta->album_artist, 0, sizeof(meta->album_artist));
    else if (!strcmp(name, "album")) memset(meta->album, 0, sizeof(meta->album));
    else if (!strcmp(name, "composersort")) memset(meta->composer_sort, 0, sizeof(meta->composer_sort));
    else if (!strcmp(name, "composer")) memset(meta->composer, 0, sizeof(meta->composer));
    else if (!strcmp(name, "year")) memset(meta->year, 0, sizeof(meta->year));
    else if (!strcmp(name, "comment")) memset(meta->comment, 0, sizeof(meta->comment));
    else if (!strcmp(name, "genre")) { memset(meta->genre_str, 0, sizeof(meta->genre_str)); meta->genre_code = 0; }
    else if (!strcmp(name, "compilation")) meta->compilation = false;
    else if (!strcmp(name, "track")) { meta->track_num = 0; meta->track_total = 0; }
    else if (!strcmp(name, "disc")) { meta->disc_num = 0; meta->disc_total = 0; }
    else if (!strcmp(name, "cover-art")) { meta->cover_art = NULL; meta->cover_bytes = 0; }
    else if (!strcmp(name, "custom")) meta->num_custom_tags = 0;
    else return false;
    return true;
}

static int cmd_tag(int argc, char **argv)
{
    const char *filepath = NULL;
    bool strict_mode = false;
    faam_metadata meta;
    memset(&meta, 0, sizeof(meta));

    bool want_clear = false;
    char remove_names[FAAM_TAG_MAX_REMOVE][32];
    int num_remove = 0;
    uint8_t *cover_buf = NULL;

    static struct option long_options[] = {
        {"title", required_argument, 0, OPT_TITLE},
        {"artist", required_argument, 0, OPT_ARTIST},
        {"artistsort", required_argument, 0, OPT_ARTIST_SORT},
        {"album", required_argument, 0, OPT_ALBUM},
        {"albumsort", required_argument, 0, OPT_ALBUM_SORT},
        {"albumartist", required_argument, 0, OPT_ALBUM_ARTIST},
        {"albumartistsort", required_argument, 0, OPT_ALBUM_ARTIST_SORT},
        {"composer", required_argument, 0, OPT_COMPOSER},
        {"composersort", required_argument, 0, OPT_COMPOSER_SORT},
        {"year", required_argument, 0, OPT_YEAR},
        {"comment", required_argument, 0, OPT_COMMENT},
        {"genre", required_argument, 0, OPT_GENRE},
        {"compilation", no_argument, 0, OPT_COMPILATION},
        {"track", required_argument, 0, OPT_TRACK},
        {"disc", required_argument, 0, OPT_DISC},
        {"cover-art", required_argument, 0, OPT_COVER_ART},
        {"tag", required_argument, 0, OPT_TAG},
        {"lang", required_argument, 0, OPT_LANG},
        {"language", required_argument, 0, OPT_LANG},
        {"remove", required_argument, 0, OPT_REMOVE},
        {"clear", no_argument, 0, OPT_CLEAR},
        {"strict", no_argument, 0, OPT_STRICT},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    /* Pass 1: only look for --clear/--remove, so their effect ("clear/strip
     * before applying anything else in this invocation") doesn't depend on
     * where they happen to fall in argv relative to the value-setting flags. */
    {
        int opt;
        int saved_opterr = opterr;
        opterr = 0;
        optind = 1;
        while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
            if (opt == OPT_CLEAR) {
                want_clear = true;
            } else if (opt == OPT_REMOVE) {
                if (num_remove < FAAM_TAG_MAX_REMOVE) {
                    strncpy(remove_names[num_remove], optarg, sizeof(remove_names[0]) - 1);
                    remove_names[num_remove][sizeof(remove_names[0]) - 1] = '\0';
                    num_remove++;
                }
            }
        }
        opterr = saved_opterr;
    }

    if (optind < argc) filepath = argv[optind];
    /* optind above reflects pass 1's scan; re-derive it after pass 2 below. */

    if (!filepath) {
        fprintf(stderr, "Error: Missing input file.\nUsage: faam tag <input.mp4> [options]\n");
        return 1;
    }

    FILE *f = cli_fopen(filepath, "r+b");
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    faam_io io = { f, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };

    if (!want_clear) {
        uint32_t demux_size = 0;
        faam_demuxer_get_state_size(&demux_size);
        void *dmem = malloc(demux_size);
        faam_demuxer *d = NULL;
        if (dmem && faam_demuxer_init(dmem, demux_size, &io, &d) == FAAM_OK) {
            faam_demuxer_get_metadata(d, &meta);
            faam_demuxer_close(d);
        }
        free(dmem);
    }

    for (int i = 0; i < num_remove; i++) {
        if (!remove_metadata_field(&meta, remove_names[i])) {
            fprintf(stderr, "Warning: unrecognized --remove field \"%s\", ignoring\n", remove_names[i]);
        }
    }

    /* Pass 2: apply every value-setting flag on top of the (possibly
     * existing, possibly cleared) base metadata. */
    const char *err_msg = NULL;
    int opt;
    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
        case OPT_TITLE: copy_utf8_field(meta.title, sizeof(meta.title), optarg); break;
        case OPT_ARTIST: copy_utf8_field(meta.artist, sizeof(meta.artist), optarg); break;
        case OPT_ARTIST_SORT: copy_utf8_field(meta.artist_sort, sizeof(meta.artist_sort), optarg); break;
        case OPT_ALBUM: copy_utf8_field(meta.album, sizeof(meta.album), optarg); break;
        case OPT_ALBUM_SORT: copy_utf8_field(meta.album_sort, sizeof(meta.album_sort), optarg); break;
        case OPT_ALBUM_ARTIST: copy_utf8_field(meta.album_artist, sizeof(meta.album_artist), optarg); break;
        case OPT_ALBUM_ARTIST_SORT: copy_utf8_field(meta.album_artist_sort, sizeof(meta.album_artist_sort), optarg); break;
        case OPT_COMPOSER: copy_utf8_field(meta.composer, sizeof(meta.composer), optarg); break;
        case OPT_COMPOSER_SORT: copy_utf8_field(meta.composer_sort, sizeof(meta.composer_sort), optarg); break;
        case OPT_YEAR: copy_utf8_field(meta.year, sizeof(meta.year), optarg); break;
        case OPT_COMMENT: copy_utf8_field(meta.comment, sizeof(meta.comment), optarg); break;
        case OPT_LANG: strncpy(meta.language, optarg, sizeof(meta.language) - 1); break;
        case OPT_COMPILATION: meta.compilation = true; break;
        case OPT_GENRE: {
            char *endptr = NULL;
            long g = strtol(optarg, &endptr, 10);
            if (endptr != optarg && *endptr == '\0') {
                if (g < 0 || g > 255) err_msg = "Genre number out of range (0-255)";
                else meta.genre_code = (uint16_t)(g + 1);
            } else {
                copy_utf8_field(meta.genre_str, sizeof(meta.genre_str), optarg);
            }
            break;
        }
        case OPT_TRACK:
            if (sscanf(optarg, "%hu/%hu", &meta.track_num, &meta.track_total) < 1)
                err_msg = "Wrong track number (expected N or N/total)";
            break;
        case OPT_DISC:
            if (sscanf(optarg, "%hu/%hu", &meta.disc_num, &meta.disc_total) < 1)
                err_msg = "Wrong disc number (expected N or N/total)";
            break;
        case OPT_COVER_ART: {
            const char *cerr = NULL;
            uint32_t clen = 0;
            free(cover_buf);
            cover_buf = read_cover_art_file(optarg, &clen, &cerr);
            if (!cover_buf) {
                err_msg = cerr;
            } else {
                meta.cover_art = cover_buf;
                meta.cover_bytes = clen;
            }
            break;
        }
        case OPT_TAG: {
            char *tagname = optarg;
            char *tagval = strchr(optarg, ',');
            if (!tagval) {
                err_msg = "Missing tag value (expected --tag name,value)";
            } else {
                *tagval++ = '\0';
                if (!*tagval) {
                    err_msg = "Tag value cannot be empty";
                } else if (meta.num_custom_tags >= 16) {
                    err_msg = "Too many custom tags (max 16)";
                } else {
                    /* Matches encode_engine.c's add_custom_tag(): only the
                     * value is UTF-8-ensured, the name is a fixed identifier. */
                    strncpy(meta.custom_tags[meta.num_custom_tags].name, tagname, sizeof(meta.custom_tags[0].name) - 1);
                    copy_utf8_field(meta.custom_tags[meta.num_custom_tags].value, sizeof(meta.custom_tags[0].value), tagval);
                    meta.num_custom_tags++;
                }
            }
            break;
        }
        case OPT_REMOVE: /* handled in pass 1 */
        case OPT_CLEAR:  /* handled in pass 1 */
        case OPT_STRICT: strict_mode = true; break;
        case 'h': print_usage(); free(cover_buf); fclose(f); return 0;
        default: break;
        }
        if (err_msg) break;
    }

    if (err_msg) {
        fprintf(stderr, "Error: %s\n", err_msg);
        free(cover_buf);
        fclose(f);
        return 1;
    }

    faam_status st = faam_update_tags_stream(&io, &meta);
    free(cover_buf);
    fclose(f);

    if (st != FAAM_OK) {
        if (strict_mode) {
            fprintf(stderr, "%s: tag: error %d (%s)\n", filepath, st, faam_strerror(st));
        } else {
            fprintf(stderr, "Error updating tags on %s: %s\n", filepath, faam_strerror(st));
        }
        return 1;
    }

    printf("Tags updated successfully on %s\n", filepath);
    return 0;
}

/* Chapter file format: one chapter per line, "HH:MM:SS.mmm<TAB>Title". No
 * duration field -- QuickTime's chpl atom (chapter.c) doesn't store one
 * either; a chapter's extent is implicitly "until the next chapter starts". */
static bool parse_chapter_line(const char *line, faam_chapter *out)
{
    unsigned hh, mm, ms;
    unsigned ss_i;
    int consumed = 0;
    if (sscanf(line, "%u:%u:%u.%u%n", &hh, &mm, &ss_i, &ms, &consumed) != 4) return false;
    if (mm > 59 || ss_i > 59) return false;

    const char *rest = line + consumed;
    if (*rest != '\t') return false;
    rest++;

    memset(out, 0, sizeof(*out));
    out->start_ms = ((uint64_t)hh * 3600 + (uint64_t)mm * 60 + ss_i) * 1000 + ms;

    size_t len = strlen(rest);
    while (len > 0 && (rest[len - 1] == '\n' || rest[len - 1] == '\r')) len--;
    if (len >= sizeof(out->title)) len = sizeof(out->title) - 1;
    memcpy(out->title, rest, len);
    out->title[len] = '\0';
    return true;
}

static int cmd_chapter_import(const char *filepath, const char *chapters_path)
{
    FILE *cf = cli_fopen(chapters_path, "r");
    if (!cf) {
        fprintf(stderr, "Error opening chapter file %s\n", chapters_path);
        return 1;
    }

    faam_chapter chapters[FAAM_CLI_MAX_CHAPTERS];
    uint32_t count = 0;
    char line[512];
    int lineno = 0;
    bool bad = false;

    while (fgets(line, sizeof(line), cf)) {
        lineno++;
        if (line[0] == '\0' || line[0] == '\n') continue;

        if (count >= FAAM_CLI_MAX_CHAPTERS) {
            fprintf(stderr, "%s:%d: too many chapters (max %d)\n", chapters_path, lineno, FAAM_CLI_MAX_CHAPTERS);
            bad = true;
            break;
        }
        if (!parse_chapter_line(line, &chapters[count])) {
            fprintf(stderr, "%s:%d: malformed chapter line (expected HH:MM:SS.mmm<TAB>Title)\n", chapters_path, lineno);
            bad = true;
            break;
        }
        count++;
    }
    fclose(cf);

    if (bad) return 1;
    if (count == 0) {
        fprintf(stderr, "Error: %s contains no chapters\n", chapters_path);
        return 1;
    }

    FILE *f = cli_fopen(filepath, "r+b");
    if (!f) {
        fprintf(stderr, "Error opening %s\n", filepath);
        return 1;
    }

    faam_io io = { f, file_read_cb, file_write_cb, file_seek_cb, file_tell_cb };
    faam_status st = faam_update_chapters_stream(&io, chapters, count);
    fclose(f);

    if (st != FAAM_OK) {
        fprintf(stderr, "Error updating chapters on %s: %s\n", filepath, faam_strerror(st));
        return 1;
    }

    printf("Successfully imported %u chapters into %s\n", count, filepath);
    return 0;
}

static int cmd_chapter_export(const char *filepath, const char *output_path)
{
    FILE *f = cli_fopen(filepath, "rb");
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

    faam_chapter chapters[FAAM_CLI_MAX_CHAPTERS];
    uint32_t count = 0;
    faam_demuxer_get_chapters(d, chapters, FAAM_CLI_MAX_CHAPTERS, &count);
    faam_demuxer_close(d);
    free(mem);
    fclose(f);

    FILE *of = cli_fopen(output_path, "w");
    if (!of) {
        fprintf(stderr, "Error creating %s\n", output_path);
        return 1;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint64_t ms = chapters[i].start_ms;
        unsigned hh = (unsigned)(ms / 3600000);
        unsigned mm = (unsigned)((ms / 60000) % 60);
        unsigned ss = (unsigned)((ms / 1000) % 60);
        unsigned mmm = (unsigned)(ms % 1000);
        fprintf(of, "%02u:%02u:%02u.%03u\t%s\n", hh, mm, ss, mmm, chapters[i].title);
    }
    fclose(of);

    printf("Successfully exported %u chapters from %s to %s\n", count, filepath, output_path);
    return 0;
}

static int cmd_chapter(int argc, char **argv)
{
    /* main() dispatches as cmd_chapter(argc-1, argv+1), so argv[0] here is
     * literally "chapter" (mirroring how cmd_info/cmd_tag/etc. get their own
     * name in argv[0]); the real subcommand is argv[1]. */
    if (argc < 2) {
        printf("Usage: faam chapter import <audiobook.m4b> --chapters <chapters.txt>\n");
        printf("       faam chapter export <audiobook.m4b> -o <chapters.txt>\n");
        return 1;
    }

    const char *subcmd = argv[1];
    int sub_argc = argc - 1;
    char **sub_argv = argv + 1; /* sub_argv[0] == subcmd, skipped by getopt like a program name */

    const char *filepath = NULL;
    const char *chapters_path = NULL;
    const char *output_path = NULL;

    static struct option long_options[] = {
        {"chapters", required_argument, 0, OPT_CHAPTERS},
        {"output", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;
    while ((opt = getopt_long(sub_argc, sub_argv, "o:h", long_options, NULL)) != -1) {
        switch (opt) {
        case OPT_CHAPTERS: chapters_path = optarg; break;
        case 'o': output_path = optarg; break;
        case 'h': print_usage(); return 0;
        default: break;
        }
    }
    if (optind < sub_argc) filepath = sub_argv[optind];

    if (!strcmp(subcmd, "import")) {
        if (!filepath || !chapters_path) {
            fprintf(stderr, "Usage: faam chapter import <audiobook.m4b> --chapters <chapters.txt>\n");
            return 1;
        }
        return cmd_chapter_import(filepath, chapters_path);
    } else if (!strcmp(subcmd, "export")) {
        if (!filepath || !output_path) {
            fprintf(stderr, "Usage: faam chapter export <audiobook.m4b> -o <chapters.txt>\n");
            return 1;
        }
        return cmd_chapter_export(filepath, output_path);
    }

    fprintf(stderr, "Unknown subcommand: %s\n", subcmd);
    return 1;
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
    } else if (strcmp(cmd, "demux") == 0) {
        ret = cmd_demux(argc - 1, argv + 1);
    } else if (strcmp(cmd, "tag") == 0) {
        ret = cmd_tag(argc - 1, argv + 1);
    } else if (strcmp(cmd, "chapter") == 0) {
        ret = cmd_chapter(argc - 1, argv + 1);
    } else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage();
        ret = 0;
    } else if (cmd[0] == '-') {
        /* Verb-less mux invocation (faam -i a -i b -o out.mp4): there is no
         * verb word occupying argv[1], so argv[1] is already the first real
         * flag. cmd_mux()/getopt_long expect argv[0] of their sub-array to
         * be a skippable "program name" slot (mirroring how "mux" filled
         * that role in the old faam mux <input> -o <out> form) -- passing
         * argv+1 directly here would make getopt_long silently skip and
         * drop this first flag. Splice in a placeholder argv[0] instead. */
        char **mux_argv = (char **)malloc(sizeof(char *) * (size_t)argc);
        if (!mux_argv) {
            fprintf(stderr, "Error: out of memory\n");
            ret = 1;
        } else {
            mux_argv[0] = "mux";
            for (int i = 1; i < argc; i++) mux_argv[i] = argv[i];
            ret = cmd_mux(argc, mux_argv);
            free(mux_argv);
        }
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

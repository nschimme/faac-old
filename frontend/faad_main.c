/*
 * FAAD CLI Executable - Modernized Unix Audio Decoder Utility
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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

#include "faad.h"
#include "charset.h"
#include "endian.h"

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

extern bool mp4_read_track_buf(const uint8_t *buf, long file_size, MP4Track *track);
extern void mp4_free_track(MP4Track *track);

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
    uint32_t fill;
} PCMFifo;

static void fifo_init(PCMFifo *f, uint32_t capacity)
{
    f->data = (uint8_t *)malloc(capacity > 0 ? capacity : 65536);
    f->size = capacity > 0 ? capacity : 65536;
    f->head = 0;
    f->tail = 0;
    f->fill = 0;
}

static void fifo_free(PCMFifo *f)
{
    if (f->data) free(f->data);
    f->data = NULL;
    f->size = 0;
    f->head = 0;
    f->tail = 0;
    f->fill = 0;
}

static void fifo_push(PCMFifo *f, const uint8_t *src, uint32_t len)
{
    if (len == 0) return;
    if (f->fill + len > f->size) {
        uint32_t new_size = f->size * 2;
        while (new_size < f->fill + len) new_size *= 2;
        uint8_t *new_data = (uint8_t *)malloc(new_size);
        if (f->fill > 0) {
            if (f->tail < f->head) {
                memcpy(new_data, f->data + f->tail, f->fill);
            } else {
                uint32_t first = f->size - f->tail;
                memcpy(new_data, f->data + f->tail, first);
                memcpy(new_data + first, f->data, f->head);
            }
        }
        free(f->data);
        f->data = new_data;
        f->size = new_size;
        f->tail = 0;
        f->head = f->fill;
    }
    uint32_t first = f->size - f->head;
    if (len <= first) {
        memcpy(f->data + f->head, src, len);
        f->head = (f->head + len) % f->size;
    } else {
        memcpy(f->data + f->head, src, first);
        memcpy(f->data, src + first, len - first);
        f->head = len - first;
    }
    f->fill += len;
}

static uint32_t fifo_pop(PCMFifo *f, uint8_t *dst, uint32_t len)
{
    if (len > f->fill) len = f->fill;
    if (len == 0) return 0;
    uint32_t first = f->size - f->tail;
    if (len <= first) {
        memcpy(dst, f->data + f->tail, len);
        f->tail = (f->tail + len) % f->size;
    } else {
        memcpy(dst, f->data + f->tail, first);
        memcpy(dst + first, f->data, len - first);
        f->tail = len - first;
    }
    f->fill -= len;
    return len;
}

static void fifo_truncate_tail(PCMFifo *f, uint32_t bytes_to_remove)
{
    if (bytes_to_remove >= f->fill) {
        f->head = 0;
        f->tail = 0;
        f->fill = 0;
        return;
    }
    if (f->head >= bytes_to_remove) {
        f->head -= bytes_to_remove;
    } else {
        f->head = f->size - (bytes_to_remove - f->head);
    }
    f->fill -= bytes_to_remove;
}

static void write_wav_header(FILE *f, uint32_t sample_rate, uint16_t num_channels, uint32_t total_pcm_bytes, uint16_t bits_per_sample, bool is_float)
{
    fseek(f, 0, SEEK_SET);
    uint32_t file_size = htole32(36 + total_pcm_bytes);
    uint16_t bytes_per_sample = bits_per_sample / 8;
    uint32_t byte_rate = htole32(sample_rate * num_channels * bytes_per_sample);
    uint16_t block_align = htole16(num_channels * bytes_per_sample);
    uint32_t sr_le = htole32(sample_rate);
    uint16_t ch_le = htole16(num_channels);
    uint16_t bps_le = htole16(bits_per_sample);

    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);

    uint32_t fmt_chunk_size = htole32(16);
    uint16_t audio_format = htole16(is_float ? 3 : 1); /* 1 = PCM, 3 = IEEE Float */
    fwrite(&fmt_chunk_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&ch_le, 2, 1, f);
    fwrite(&sr_le, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bps_le, 2, 1, f);

    uint32_t pcm_bytes_le = htole32(total_pcm_bytes);
    fwrite("data", 1, 4, f);
    fwrite(&pcm_bytes_le, 4, 1, f);
}

static void print_usage(const char *prog)
{
    faad_library_info info;
    info.struct_size = sizeof(info);
    if (faad_get_library_info(&info) != FAAD_OK) {
        info.version = "3.0.0";
    }

    printf("FAAD - Freeware Advanced Audio Decoder (v%s)\n", info.version);
    printf("Usage: %s [options] <infile.aac|infile.m4a>\n\n", prog);
    printf("I/O & Format Options:\n");
    printf("  -o, --output <file>    Set output filename (default: stdout if piped, or infile.wav)\n");
    printf("  -w, --stdout           Write output PCM to stdout\n");
    printf("  -f, --format <type>    Output container format: wav (default), raw\n");
    printf("  -b, --bits <depth>     Sample depth: 16 (default), 24, 32f (32-bit float)\n");
    printf("  -a, --adts <file>      Extract raw ADTS stream from MP4 without decoding\n\n");
    printf("Processing Options:\n");
    printf("  -d, --downmix [mode]   Downmix audio (mono/1 or stereo/2, default: mono)\n");
    printf("  -j, --jump <seconds>   Start decoding from specified timestamp\n");
    printf("      --no-gapless       Disable automatic gapless trim/padding handling\n\n");
    printf("Information & General:\n");
    printf("  -i, --info             Display bitstream & container metadata, then exit\n");
    printf("      --json             Output bitstream info in JSON format\n");
    printf("  -q, --quiet            Quiet mode (suppress decoding progress)\n");
    printf("      --strict           Strict mode (noisily error and report debug details on failure)\n");
    printf("  -h, --help             Display this help text\n");
}

enum {
    OPT_NO_GAPLESS = 300,
    OPT_JSON,
    OPT_STRICT
};

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

    const char *infile = NULL;
    const char *outfile = NULL;
    const char *adts_outfile = NULL;
    bool write_stdout = false;
    bool raw_format = false;
    uint32_t bit_depth = 16;
    bool is_float = false;
    bool downmix_stereo = false;
    bool gapless = true;
    bool info_only = false;
    bool json_info = false;
    bool quiet = false;
    bool strict_mode = false;
    double jump_seconds = 0.0;

    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"stdout", no_argument, 0, 'w'},
        {"format", required_argument, 0, 'f'},
        {"bits", required_argument, 0, 'b'},
        {"adts", required_argument, 0, 'a'},
        {"downmix", optional_argument, 0, 'd'},
        {"jump", required_argument, 0, 'j'},
        {"no-gapless", no_argument, 0, OPT_NO_GAPLESS},
        {"info", no_argument, 0, 'i'},
        {"json", no_argument, 0, OPT_JSON},
        {"quiet", no_argument, 0, 'q'},
        {"strict", no_argument, 0, OPT_STRICT},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "o:wf:b:a:d::j:iqh", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'o': outfile = optarg; break;
        case 'w': write_stdout = true; break;
        case 'f': if (strcmp(optarg, "raw") == 0) raw_format = true; break;
        case 'b':
            if (strcmp(optarg, "24") == 0) bit_depth = 24;
            else if (strcmp(optarg, "32f") == 0 || strcmp(optarg, "32") == 0) { bit_depth = 32; is_float = true; }
            else bit_depth = 16;
            break;
        case 'a': adts_outfile = optarg; break;
        case 'd': downmix_stereo = true; break;
        case 'j': jump_seconds = atof(optarg); break;
        case OPT_NO_GAPLESS: gapless = false; break;
        case 'i': info_only = true; break;
        case OPT_JSON: json_info = true; info_only = true; break;
        case 'q': quiet = true; break;
        case OPT_STRICT: strict_mode = true; break;
        case 'h': print_usage(argv[0]); return 0;
        default: break;
        }
    }

    if (optind < argc) {
        infile = argv[optind];
    }

    if (!infile) {
        print_usage(argv[0]);
        return 1;
    }

#ifdef _WIN32
    FILE *fin = win32_fopen_utf8(infile, "rb");
#else
    FILE *fin = fopen(infile, "rb");
#endif
    if (!fin) {
        fprintf(stderr, "Error opening input file %s\n", infile);
        return 1;
    }

    fseek(fin, 0, SEEK_END);
    long file_len = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    uint8_t *inbuf = (uint8_t *)malloc(file_len > 0 ? file_len : 1);
    if (!inbuf) {
        fclose(fin);
        return 1;
    }

    if (fread(inbuf, 1, file_len, fin) != (size_t)file_len) {
        fprintf(stderr, "Error reading input file\n");
        free(inbuf);
        fclose(fin);
        return 1;
    }
    fclose(fin);

    MP4Track track;
    memset(&track, 0, sizeof(track));
    bool is_mp4 = mp4_read_track_buf(inbuf, file_len, &track);

    /* Direct ADTS extraction from MP4 container without decoding */
    if (adts_outfile && is_mp4) {
#ifdef _WIN32
        FILE *fadts = win32_fopen_utf8(adts_outfile, "wb");
#else
        FILE *fadts = fopen(adts_outfile, "wb");
#endif
        if (!fadts) {
            fprintf(stderr, "Error opening ADTS output file %s\n", adts_outfile);
            free(inbuf);
            mp4_free_track(&track);
            return 1;
        }
        for (uint32_t s = 0; s < track.num_samples; s++) {
            uint64_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            if (offset > 0 && offset + size <= (uint64_t)file_len) {
                uint8_t adts_hdr[7] = { 0xFF, 0xF1, 0x50, 0x80, 0x00, 0x1F, 0xFC };
                uint32_t frame_len = size + 7;
                adts_hdr[3] = (uint8_t)(0x80 | ((frame_len >> 11) & 0x03));
                adts_hdr[4] = (uint8_t)((frame_len >> 3) & 0xFF);
                adts_hdr[5] = (uint8_t)(((frame_len & 0x07) << 5) | 0x1F);
                fwrite(adts_hdr, 1, 7, fadts);
                fwrite(inbuf + offset, 1, size, fadts);
            }
        }
        fclose(fadts);
        if (!quiet) printf("Extracted %u raw ADTS frames to %s\n", track.num_samples, adts_outfile);
        free(inbuf);
        mp4_free_track(&track);
        return 0;
    }

    faad_config cfg;
    faad_config_init(&cfg, sizeof(cfg));
    cfg.stream_format = is_mp4 ? FAAD_STREAM_RAW : FAAD_STREAM_ADTS;
    cfg.output_format = is_float ? FAAD_OUTPUT_FLOAT : FAAD_OUTPUT_16BIT;
    cfg.downmix_mode = downmix_stereo ? FAAD_DOWNMIX_MONO : FAAD_DOWNMIX_NONE;

    faad_decoder *dec = NULL;
    faad_status st = faad_decoder_create(&cfg, is_mp4 ? track.asc_buf : NULL, is_mp4 ? track.asc_len : 0, &dec);
    if (st != FAAD_OK) {
        fprintf(stderr, "Failed to open FAAD decoder: %s\n", faad_strerror(st));
        free(inbuf);
        if (is_mp4) mp4_free_track(&track);
        return 1;
    }

    FILE *fout = NULL;
    if (!info_only) {
        if (write_stdout) {
            fout = stdout;
            quiet = true;
        } else {
            if (!outfile) {
                char *out_path = (char *)malloc(strlen(infile) + 8);
                strcpy(out_path, infile);
                char *dot = strrchr(out_path, '.');
                if (dot) strcpy(dot, raw_format ? ".raw" : ".wav");
                else strcat(out_path, raw_format ? ".raw" : ".wav");
                outfile = out_path;
            }
#ifdef _WIN32
            fout = win32_fopen_utf8(outfile, "wb");
#else
            fout = fopen(outfile, "wb");
#endif
            if (!fout) {
                fprintf(stderr, "Error opening output file %s\n", outfile);
                faad_decoder_destroy(dec); dec = NULL;
                free(inbuf);
                if (is_mp4) mp4_free_track(&track);
                return 1;
            }
            if (!raw_format) {
                faad_stream_info sinfo;
                uint32_t init_sr = 44100;
                uint32_t init_ch = 2;
                if (faad_decoder_get_info(dec, &sinfo) == FAAD_OK) {
                    if (sinfo.sample_rate > 0) init_sr = sinfo.sample_rate;
                    if (sinfo.channels > 0) init_ch = sinfo.channels;
                }
                write_wav_header(fout, init_sr, (uint16_t)init_ch, 0, bit_depth, is_float);
            }
        }
    }

    uint8_t outbuf[65536];
    uint8_t pcm24_buf[98304];
    uint32_t total_pcm_bytes = 0;
    uint32_t sample_rate = 44100;
    uint32_t num_channels = 2;
    enum faad_object_type obj_type = FAAD_OBJ_LC;
    uint32_t frames_decoded = 0;

    uint32_t start_frame = 0;
    if (jump_seconds > 0.0) {
        faad_stream_info sinfo;
        uint32_t sr = 44100;
        uint32_t fl = 1024;
        if (faad_decoder_get_info(dec, &sinfo) == FAAD_OK) {
            if (sinfo.sample_rate > 0) sr = sinfo.sample_rate;
            if (sinfo.object_type == FAAD_OBJ_HE_AAC_V1 || sinfo.object_type == FAAD_OBJ_HE_AAC_V2) {
                fl = 2048;
            }
        }
        start_frame = (uint32_t)((jump_seconds * (double)sr) / (double)fl);
    }

    /* Gapless counts are in the track's timescale, the core rate; the decoder
     * may output at twice that (SBR), so they are scaled by the first frame. */
    uint32_t samples_to_skip = (is_mp4 && gapless) ? track.delay : 0;
    uint32_t padding_samples = (is_mp4 && gapless) ? track.padding : 0;
    bool gapless_scaled = false;
    PCMFifo fifo;
    fifo_init(&fifo, 262144);

    if (is_mp4) {
        for (uint32_t s = start_frame; s < track.num_samples; s++) {
            uint64_t offset = track.samples[s].offset;
            uint32_t size = track.samples[s].size;
            if (offset == 0 || offset + size > (uint64_t)file_len) continue;

            uint32_t bytes_consumed = 0;
            uint32_t bytes_written = 0;

            faad_frame_info finfo;
            st = faad_decode_frame(dec, inbuf + offset, size,
                                   &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written, &finfo);

            if (st != FAAD_OK) {
                if (strict_mode) {
                    fprintf(stderr, "[STRICT ERROR] Frame %u (sample %u, offset 0x%llx, size %u): Decode failed with status %d (%s)\n",
                            s, s, (unsigned long long)offset, size, st, faad_strerror(st));
                    faad_decoder_destroy(dec);
                    free(inbuf);
                    mp4_free_track(&track);
                    return 1;
                }
            } else if (bytes_written > 0) {
                /* Per-frame info, not the stream info: SBR may be signalled
                 * implicitly and only known once the payload is decoded. */
                sample_rate = finfo.sample_rate;
                num_channels = finfo.channels;
                obj_type = finfo.sbr_active ? FAAD_OBJ_HE_AAC_V1 : FAAD_OBJ_LC;

                uint32_t dec_bytes_per_sample = is_float ? 4 : 2;
                uint32_t dec_bytes_per_frame_sample = num_channels * dec_bytes_per_sample;
                uint32_t frame_samples = bytes_written / dec_bytes_per_frame_sample;
                if (!gapless_scaled) {
                    uint32_t factor = finfo.samples_per_ch / 1024;
                    if (factor > 1) {
                        samples_to_skip *= factor;
                        padding_samples *= factor;
                    }
                    gapless_scaled = true;
                }

                uint8_t *write_ptr = outbuf;
                uint32_t samples_to_write = frame_samples;

                if (samples_to_skip > 0) {
                    if (samples_to_skip >= samples_to_write) {
                        samples_to_skip -= samples_to_write;
                        samples_to_write = 0;
                    } else {
                        write_ptr += samples_to_skip * dec_bytes_per_frame_sample;
                        samples_to_write -= samples_to_skip;
                        samples_to_skip = 0;
                    }
                }

                if (fout && samples_to_write > 0) {
                    if (bit_depth == 24 && !is_float) {
                        /* Convert int16_t PCM from decoder to 24-bit PCM */
                        const int16_t *src_pcm = (const int16_t *)write_ptr;
                        uint32_t total_items = samples_to_write * num_channels;
                        for (uint32_t k = 0; k < total_items; k++) {
                            int32_t val24 = ((int32_t)src_pcm[k]) << 8;
                            pcm24_buf[k * 3 + 0] = (uint8_t)(val24 & 0xFF);
                            pcm24_buf[k * 3 + 1] = (uint8_t)((val24 >> 8) & 0xFF);
                            pcm24_buf[k * 3 + 2] = (uint8_t)((val24 >> 16) & 0xFF);
                        }
                        fifo_push(&fifo, pcm24_buf, total_items * 3);
                    } else {
                        fifo_push(&fifo, write_ptr, samples_to_write * dec_bytes_per_frame_sample);
                    }

                    uint32_t padding_bytes = padding_samples * num_channels * (bit_depth / 8);
                    if (fifo.fill > padding_bytes) {
                        uint32_t can_pop = fifo.fill - padding_bytes;
                        uint8_t pop_buf[4096];
                        while (can_pop > 0) {
                            uint32_t chunk = can_pop < sizeof(pop_buf) ? can_pop : sizeof(pop_buf);
                            uint32_t popped = fifo_pop(&fifo, pop_buf, chunk);
                            if (popped == 0) break;
                            fwrite(pop_buf, 1, popped, fout);
                            total_pcm_bytes += popped;
                            can_pop -= popped;
                        }
                    }
                }
                frames_decoded++;
            }
        }
    } else {
        uint32_t offset = 0;
        while (offset < (uint32_t)file_len) {
            uint32_t bytes_consumed = 0;
            uint32_t bytes_written = 0;

            faad_frame_info finfo;
            st = faad_decode_frame(dec, inbuf + offset, file_len - offset,
                                   &bytes_consumed, outbuf, sizeof(outbuf), &bytes_written, &finfo);

            if (st != FAAD_OK) {
                if (st == FAAD_ERR_NEED_MORE_DATA || bytes_consumed == 0) {
                    break;
                }
                if (strict_mode) {
                    fprintf(stderr, "[STRICT ERROR] Frame %u (stream offset 0x%x, remaining %ld): Decode failed with status %d (%s)\n",
                            frames_decoded, offset, file_len - offset, st, faad_strerror(st));
                    faad_decoder_destroy(dec);
                    free(inbuf);
                    return 1;
                }
                offset += bytes_consumed; /* resync distance on SYNC_LOST */
                continue;
            }

            sample_rate = finfo.sample_rate;
            num_channels = finfo.channels;
            obj_type = finfo.sbr_active ? FAAD_OBJ_HE_AAC_V1 : FAAD_OBJ_LC;

            if (fout && bytes_written > 0) {
                uint32_t dec_bytes_per_sample = is_float ? 4 : 2;
                uint32_t dec_bytes_per_frame_sample = num_channels * dec_bytes_per_sample;
                uint32_t frame_samples = bytes_written / dec_bytes_per_frame_sample;
                if (!gapless_scaled) {
                    uint32_t factor = finfo.samples_per_ch / 1024;
                    if (factor > 1) {
                        samples_to_skip *= factor;
                        padding_samples *= factor;
                    }
                    gapless_scaled = true;
                }

                if (bit_depth == 24 && !is_float) {
                    const int16_t *src_pcm = (const int16_t *)outbuf;
                    uint32_t total_items = frame_samples * num_channels;
                    for (uint32_t k = 0; k < total_items; k++) {
                        int32_t val24 = ((int32_t)src_pcm[k]) << 8;
                        pcm24_buf[k * 3 + 0] = (uint8_t)(val24 & 0xFF);
                        pcm24_buf[k * 3 + 1] = (uint8_t)((val24 >> 8) & 0xFF);
                        pcm24_buf[k * 3 + 2] = (uint8_t)((val24 >> 16) & 0xFF);
                    }
                    fifo_push(&fifo, pcm24_buf, total_items * 3);
                } else {
                    fifo_push(&fifo, outbuf, bytes_written);
                }

                uint8_t pop_buf[4096];
                while (fifo.fill > 0) {
                    uint32_t chunk = fifo.fill < sizeof(pop_buf) ? fifo.fill : sizeof(pop_buf);
                    uint32_t popped = fifo_pop(&fifo, pop_buf, chunk);
                    if (popped == 0) break;
                    fwrite(pop_buf, 1, popped, fout);
                    total_pcm_bytes += popped;
                }
            }

            frames_decoded++;
            offset += bytes_consumed;
        }
    }

    if (fout) {
        if (is_mp4 && gapless && padding_samples > 0) {
            uint32_t padding_bytes = padding_samples * num_channels * (bit_depth / 8);
            if (fifo.fill > padding_bytes) {
                fifo_truncate_tail(&fifo, padding_bytes);
            } else {
                fifo.fill = 0;
            }
        }
        uint8_t pop_buf[4096];
        while (fifo.fill > 0) {
            uint32_t chunk = fifo.fill < sizeof(pop_buf) ? fifo.fill : sizeof(pop_buf);
            uint32_t popped = fifo_pop(&fifo, pop_buf, chunk);
            if (popped == 0) break;
            fwrite(pop_buf, 1, popped, fout);
            total_pcm_bytes += popped;
        }
    }
    fifo_free(&fifo);

    double duration_sec = (double)(frames_decoded * (obj_type == FAAD_OBJ_HE_AAC_V1 ? 2048 : 1024)) / (sample_rate ? sample_rate : 44100);
    double avg_bitrate_kbps = (file_len * 8.0) / (duration_sec > 0 ? duration_sec * 1000.0 : 1.0);

    if (json_info) {
        printf("{\n");
        printf("  \"file\": \"%s\",\n", infile);
        printf("  \"container\": \"%s\",\n", is_mp4 ? "MP4 / M4A" : "ADTS Bitstream");
        printf("  \"major_brand\": \"%s\",\n", track.major_brand[0] ? track.major_brand : "M4A");
        printf("  \"duration_seconds\": %.2f,\n", duration_sec);
        printf("  \"audio\": {\n");
        printf("    \"profile\": \"%s\",\n", (obj_type == FAAD_OBJ_HE_AAC_V1) ? "HE-AAC v1 (AAC-LC + SBR)" : "AAC-LC");
        printf("    \"channels\": %u,\n", num_channels);
        printf("    \"sample_rate_hz\": %u,\n", sample_rate);
        printf("    \"bitrate_avg_kbps\": %.1f,\n", avg_bitrate_kbps);
        printf("    \"total_frames\": %u\n", frames_decoded);
        printf("  },\n");
        printf("  \"metadata\": {\n");
        printf("    \"encoder\": \"%s\"\n", track.encoder_tag[0] ? track.encoder_tag : "FAAC");
        printf("  }\n");
        printf("}\n");
    } else if (info_only) {
        printf("File:        %s\n", infile);
        printf("Container:   %s (Major Brand: %s)\n", is_mp4 ? "MP4 / M4A" : "ADTS Bitstream", track.major_brand[0] ? track.major_brand : "M4A");
        printf("Duration:    %02d:%02d:%02d.%02d (%.2f seconds)\n\n",
               (int)duration_sec / 3600, ((int)duration_sec % 3600) / 60, (int)duration_sec % 60, (int)(duration_sec * 100) % 100, duration_sec);
        printf("Audio Stream:\n");
        printf("  Profile:   %s\n", (obj_type == FAAD_OBJ_HE_AAC_V1) ? "HE-AAC v1 (AAC-LC + SBR)" : "AAC-LC");
        printf("  Channels:  %u (%s)\n", num_channels, (num_channels == 1) ? "Mono" : ((num_channels == 2) ? "Stereo" : "Multichannel"));
        printf("  Sample Rate: %.1f kHz\n", sample_rate / 1000.0f);
        printf("  Bitrate:   %.1f kbps (Avg)\n", avg_bitrate_kbps);
        printf("  Frames:    %u frames\n\n", frames_decoded);
        printf("Metadata (Tags):\n");
        printf("  Encoder:   %s\n", track.encoder_tag[0] ? track.encoder_tag : "FAAC");
    } else if (fout) {
        if (!raw_format && fout != stdout) {
            write_wav_header(fout, sample_rate, (uint16_t)num_channels, total_pcm_bytes, bit_depth, is_float);
            fclose(fout);
        }
        if (!quiet) {
            printf("Decoded %u frames (%u bytes, %d-bit %s) to %s\n",
                   frames_decoded, total_pcm_bytes, bit_depth, is_float ? "float" : "PCM", outfile ? outfile : "stdout");
        }
    }

    faad_decoder_destroy(dec); dec = NULL;
    free(inbuf);
    if (is_mp4) mp4_free_track(&track);

#ifdef _WIN32
    if (allocated_argv) {
        for (int i = 0; i < argc; i++) {
            if (allocated_argv[i]) free(allocated_argv[i]);
        }
        free(allocated_argv);
    }
#endif

    return 0;
}

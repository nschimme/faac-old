/*
 * FAAD3 - Ultra-Lightweight Advanced Audio Decoder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * Designed for bare-metal, RTOS, and zero-allocation environments.
 */

#ifndef FAAD_H
#define FAAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define FAAD_VERSION_MAJOR 3
#define FAAD_VERSION_MINOR 0
#define FAAD_VERSION_PATCH 0
#define FAAD_VERSION_HEX \
    ((FAAD_VERSION_MAJOR << 16) | (FAAD_VERSION_MINOR << 8) | FAAD_VERSION_PATCH)

#ifndef FAADAPI
# if defined(_WIN32)
#  define FAADAPI __declspec(dllexport)
# elif defined(__GNUC__) && (__GNUC__ >= 4)
#  define FAADAPI __attribute__((visibility("default")))
# else
#  define FAADAPI
# endif
#endif

/* Opaque decoder handle */
typedef struct faad_decoder faad_decoder;

typedef enum faad_status {
    FAAD_OK                    = 0,
    FAAD_ERR_INVALID_ARGUMENT  = -1,  /* NULL pointer or bad arguments */
    FAAD_ERR_UNSUPPORTED       = -2,  /* Unsupported profile or bitstream feature */
    FAAD_ERR_INSUFFICIENT_MEM  = -3,  /* Provided static memory block is too small */
    FAAD_ERR_OUTPUT_TOO_SMALL  = -4,  /* Provided PCM output buffer capacity too small */
    FAAD_ERR_NEED_MORE_DATA    = -5,  /* Input buffer doesn't contain a full frame */
    FAAD_ERR_DECODE_FAILED     = -6,  /* Bitstream corruption or DSP error */
    FAAD_ERR_SYNC_LOST         = -7,  /* Lost frame synchronization (e.g., bad ADTS syncword) */
    FAAD_STATUS_MAX            = 0x7fffffff
} faad_status;

/* Global library metadata */
typedef struct faad_library_info {
    uint32_t                struct_size;
    const char             *version;
    const char             *copyright;
    uint32_t                max_channels;
    bool                    sbr_supported;
    bool                    ps_supported;
} faad_library_info;

FAADAPI faad_status faad_get_library_info(faad_library_info *out);

enum faad_object_type {
    FAAD_OBJ_NULL      = 0,
    FAAD_OBJ_LC        = 2,           /* AAC-LC (Low Complexity) */
    FAAD_OBJ_HE_AAC_V1 = 5,           /* HE-AAC v1 (AAC-LC + SBR) */
    FAAD_OBJ_HE_AAC_V2 = 29,          /* HE-AAC v2 (AAC-LC + SBR + PS) */
    FAAD_OBJ_MAX       = 0x7fffffff
};

enum faad_stream_format {
    FAAD_STREAM_RAW  = 0,             /* Raw AAC Access Units (requires ASC out-of-band) */
    FAAD_STREAM_ADTS = 1,             /* Self-framing ADTS bitstream */
    FAAD_STREAM_MAX  = 0x7fffffff
};

enum faad_output_format {
    FAAD_OUTPUT_16BIT = 1,            /* Signed 16-bit PCM (int16_t) */
    FAAD_OUTPUT_FLOAT = 4,            /* 32-bit floating point PCM (float) */
    FAAD_OUTPUT_MAX   = 0x7fffffff
};

enum faad_downmix_mode {
    FAAD_DOWNMIX_NONE   = 0,          /* Preserve native channel layout */
    FAAD_DOWNMIX_STEREO = 1,          /* Downmix surround channels to 2-channel stereo */
    FAAD_DOWNMIX_MONO   = 2           /* Downmix during IMDCT to mono (Saves ~45% CPU/RAM) */
};

/* Decoder configuration provided at initialization */
typedef struct faad_config {
    uint32_t                struct_size;   /* Must be set via faad_config_init() */
    enum faad_stream_format stream_format; /* RAW or ADTS */
    enum faad_output_format output_format; /* 16-bit integer or 32-bit float */
    enum faad_downmix_mode  downmix_mode;  /* Channel downmixing strategy */
} faad_config;

/* Static stream information (derived from ASC or ADTS headers) */
typedef struct faad_stream_info {
    uint32_t                sample_rate;      /* Base sample rate in Hz */
    uint32_t                channels;         /* Base channel count */
    enum faad_object_type   object_type;      /* Detected object type */
    uint32_t                delay_samples;    /* Intrinsic decoder delay (priming samples) */
} faad_stream_info;

/* Dynamic frame metadata returned after every decoded packet */
typedef struct faad_frame_info {
    uint32_t                sample_rate;      /* Effective frame sample rate (reflects SBR upsampling) */
    uint32_t                samples_per_ch;   /* Decoded samples per channel (1024 or 2048) */
    uint8_t                 channels;         /* Active output channel count */
    bool                    sbr_active;       /* True if SBR extension was applied */
    bool                    ps_active;        /* True if Parametric Stereo was applied */
} faad_frame_info;


/* --- Configuration --- */

/* Initializes config structure with ABI-safe defaults. */
FAADAPI faad_status faad_config_init(faad_config *cfg, uint32_t caller_size);


/* --- Memory Management & Initialization --- */

/*
 * Queries the exact bytes of SRAM required to instantiate the decoder.
 * Embedded applications use this to allocate static .bss/.dram memory.
 */
FAADAPI faad_status faad_get_state_size(const faad_config *cfg, uint32_t *state_bytes_out);

/*
 * Initializes the decoder using a caller-provided memory block.
 * Guarantees zero internal heap allocations (no malloc/free).
 *
 * @param mem_buf    Pointer to static memory block.
 * @param mem_size   Size of mem_buf (must be >= size returned by faad_get_state_size).
 * @param asc_buf    (Optional) AudioSpecificConfig buffer for RAW streams.
 * @param asc_len    Length of asc_buf.
 */
FAADAPI faad_status faad_decoder_init(void *mem_buf, uint32_t mem_size,
                                      const faad_config *cfg,
                                      const uint8_t *asc_buf, uint32_t asc_len,
                                      faad_decoder **out_dec);

/* Convenience wrapper for desktop: Allocates memory internally via malloc(). */
FAADAPI faad_status faad_decoder_create(const faad_config *cfg,
                                        const uint8_t *asc_buf, uint32_t asc_len,
                                        faad_decoder **out_dec);

/* Destroys the decoder. Only calls free() if created via faad_decoder_create(). */
FAADAPI void faad_decoder_destroy(faad_decoder *dec);


/* --- Execution & Control --- */

/*
 * Extracts static stream metadata. Safe to call immediately after init
 * if ASC was provided, or after the first ADTS frame is parsed.
 */
FAADAPI faad_status faad_decoder_get_info(const faad_decoder *dec, faad_stream_info *out_info);

/*
 * Flushes internal IMDCT overlap and SBR delay-line history buffers.
 * MUST be called when seeking, or if network packet loss is detected (RTP drop).
 */
FAADAPI faad_status faad_decoder_flush(faad_decoder *dec);

/*
 * Primary DSP Engine Interface: Decodes exactly ONE AAC Access Unit (packet/frame).
 * Operates zero-copy on incoming network payloads.
 *
 * @param in_buf         Pointer to single ADTS frame or RAW Access Unit.
 * @param in_bytes       Size of input buffer.
 * @param bytes_consumed Returns exact number of bitstream bytes parsed.
 * @param out_pcm        Caller-allocated output buffer for PCM samples.
 * @param out_cap_bytes  Capacity of out_pcm in bytes.
 * @param bytes_written  Returns exact number of PCM bytes generated.
 * @param frame_info     Returns inline metadata for the rendered frame.
 */
FAADAPI faad_status faad_decode_frame(faad_decoder *dec,
                                      const uint8_t *in_buf, uint32_t in_bytes,
                                      uint32_t *bytes_consumed,
                                      void *out_pcm, uint32_t out_cap_bytes,
                                      uint32_t *bytes_written,
                                      faad_frame_info *frame_info);

/* Maps status codes to human-readable strings. */
FAADAPI const char *faad_strerror(faad_status status);

#ifdef __cplusplus
}
#endif

#endif /* FAAD_H */

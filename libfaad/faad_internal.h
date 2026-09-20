/*
 * FAAD Decoder Internal Header
 */

#ifndef FAAD_INTERNAL_H
#define FAAD_INTERNAL_H

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Memory management macros (overridable for embedded PSRAM / fast internal SRAM) */
#ifndef AllocMemory
#define AllocMemory(size) malloc(size)
#endif
#ifndef FreeMemory
#define FreeMemory(block) free(block)
#endif
#ifndef AllocMemoryFast
#define AllocMemoryFast(size) malloc(size)
#endif
#ifndef FreeMemoryFast
#define FreeMemoryFast(block) free(block)
#endif
#ifndef ReallocMemory
#define ReallocMemory(block, size) realloc(block, size)
#endif


#include "faad.h"
#include "huffdata.h"

/* Channel capacity: the build's -Dmax-channels (config.h) when present. Every
 * per-channel buffer, including the SBR and PS state, scales with it. */
#ifndef MAX_CHANNELS
#define MAX_CHANNELS 8
#endif
#if (MAX_CHANNELS < 2 || defined(FAAD_DISABLE_SBR)) && !defined(FAAD_DISABLE_PS)
#define FAAD_DISABLE_PS /* parametric stereo needs SBR's QMF domain and two output channels */
#endif
#define FRAME_LEN_LONG 1024
#define FRAME_LEN_SHORT 128
#define NUM_WINDOWS 8

/* ISO/IEC 14496-3 Table 1.16 sampling_frequency_index, shared by asc.c
 * (parsing) and sfb_tables.c (scale-factor-band table selection). */
extern const uint32_t faad_sample_rates[16];

/* Syntactic Element IDs per ISO 14496-3 */
#define ID_SCE 0x0
#define ID_CPE 0x1
#define ID_CCE 0x2
#define ID_LFE 0x3
#define ID_DSE 0x4
#define ID_PCE 0x5
#define ID_FIL 0x6
#define ID_END 0x7

/* Window Sequences */
#define ONLY_LONG_SEQUENCE 0
#define LONG_START_SEQUENCE 1
#define EIGHT_SHORT_SEQUENCE 2
#define LONG_STOP_SEQUENCE 3

/* Window Shapes */
#define SINE_WINDOW 0
#define KBD_WINDOW  1

/* SBR & PS Constants */
#define SBR_EXTENSION_DATA 13
#define SBR_EXTENSION_DATA_CRC 14
#define PS_EXTENSION_DATA 2

#define SBR_PS_BANDS 20
#define SBR_PS_IID_LEVELS 15
#define SBR_PS_ICC_LEVELS 8

typedef struct {
    const uint8_t *buffer;
    uint32_t len;        /* total size in bytes */
    uint32_t byte_pos;   /* current byte index */
    uint32_t bit_pos;    /* bit offset within current byte (0..7, MSB to LSB) */
} BitReader;

void bits_init(BitReader *bs, const uint8_t *buffer, uint32_t len);

uint32_t bits_get(BitReader *bs, uint32_t nbits);
uint32_t bits_show(BitReader *bs, uint32_t nbits);

static inline uint32_t bits_get_1(BitReader *bs)
{
    if (bs->byte_pos < bs->len) {
        uint32_t bit = (bs->buffer[bs->byte_pos] >> (7 - bs->bit_pos)) & 1U;
        uint32_t next_bit = bs->bit_pos + 1;
        bs->byte_pos += next_bit >> 3;
        bs->bit_pos = next_bit & 7;
        return bit;
    }
    return 0;
}

static inline uint32_t bits_get_fast(BitReader *bs, uint32_t nbits)
{
    if (nbits <= 24 && bs->byte_pos + 4 <= bs->len) {
        const uint8_t *ptr = bs->buffer + bs->byte_pos;
        uint32_t word = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) |
                        ((uint32_t)ptr[2] << 8)  | (uint32_t)ptr[3];
        uint32_t val = (word >> (32 - bs->bit_pos - nbits)) & ((1U << nbits) - 1U);
        uint32_t total_bits = bs->bit_pos + nbits;
        bs->byte_pos += total_bits >> 3;
        bs->bit_pos = total_bits & 7;
        return val;
    }
    return bits_get(bs, nbits);
}

static inline uint32_t bits_show_fast(BitReader *bs, uint32_t nbits)
{
    if (nbits <= 24 && bs->byte_pos + 4 <= bs->len) {
        const uint8_t *ptr = bs->buffer + bs->byte_pos;
        uint32_t word = ((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) |
                        ((uint32_t)ptr[2] << 8)  | (uint32_t)ptr[3];
        return (word >> (32 - bs->bit_pos - nbits)) & ((1U << nbits) - 1U);
    }
    return bits_show(bs, nbits);
}
void bits_skip(BitReader *bs, uint32_t nbits);
void bits_byte_align(BitReader *bs);
uint32_t bits_get_consumed(BitReader *bs);
void bits_slice_rtp_au(BitReader *sub_bs, const BitReader *parent_bs, uint32_t byte_offset, uint32_t au_len);

typedef struct {
    enum faad_object_type object_type;
    uint32_t sample_rate;
    uint32_t num_channels;
    bool is_sbr;
    bool is_ps;
    uint32_t sbr_sample_rate;
} AudioSpecificConfig;

faad_status asc_decode(BitReader *bs, AudioSpecificConfig *asc);
faad_status adts_decode_header(BitReader *bs, AudioSpecificConfig *asc, uint32_t *frame_length);

typedef struct {
    uint8_t window_sequence;
    uint8_t window_shape;
    uint8_t max_sfb;
    uint8_t num_window_groups;
    uint8_t window_group_length[NUM_WINDOWS];
    uint8_t num_windows;
    uint16_t sfb_offsets[68];
    uint8_t num_sfbs;
    uint8_t sect_cb[8][64];
    uint8_t sect_start[8][64];
    uint8_t sect_end[8][64];
    uint8_t num_sections[8];
    int8_t sample_rate_index; /* core rate index, for TNS_MAX_BANDS */
    int16_t sfb_cb[8][64];
    int16_t scalefactors[8][64];
    uint8_t global_gain;

    /* PNS */
    bool pns_used[8][64];

    /* Pulse data */
    bool pulse_data_present;

    /* TNS data */
    bool tns_data_present;
    uint8_t tns_n_filt[8];
    uint8_t tns_length[8][4];
    uint8_t tns_order[8][4];
    uint8_t tns_direction[8][4];
    uint8_t tns_coef_res[8];
    int8_t  tns_coef[8][4][32];

    /* Gain control */
    bool gain_control_present;
} ICSInfo;

typedef struct {
    bool common_window;
    uint8_t ms_mask_present; /* 0 none, 1 per band, 2 all bands */
    uint8_t ms_used[8][64];
    ICSInfo ics[2];
} CPEInfo;

#ifdef FAAD_STATS
/* Aggregate, opt-in decoder diagnostics -- mirrors libfaac's FAAC_STATS in
 * spirit (see libfaac/stats.h), but embedded per-decoder rather than a
 * process-wide global: test_faad.c decodes with multiple concurrent
 * faad_decoder instances, and a global would race and corrupt across them. */
typedef struct FaadDecStats {
    unsigned int totalFrames;
    unsigned int elementCounts[8]; /* indexed by syntax_id: SCE,CPE,CCE,LFE,DSE,PCE,FIL,END */
    unsigned int nonEndTermination;

    unsigned int lastChannels;
    bool haveLastChannels;
    unsigned int channelCountChanges;
    unsigned int minChannels, maxChannels;

    unsigned int icsCount;
    unsigned int tnsActiveFrames;
    unsigned int shortBlockIcsCount;
    unsigned int sbrActiveFrames;
    unsigned int sbrHeaderCount;
    unsigned int sbrEnvelopeSum;
    unsigned int psActiveFrames;
    unsigned int psIidBandsSum;
    unsigned int psIccBandsSum;

    unsigned int huffEscapeHits[13]; /* 1..11 spectral books, 12 = scalefactor book */
    unsigned int huffEscapeMisses;
    unsigned int escbookMagnitudeEscapes;

    unsigned int fillElementCount;
    unsigned int fillElementPadBitsSum;
    unsigned int fillElementMaxPad;

    unsigned int errorConcealmentFrames;
} FaadDecStats;
#endif

/* ---- Parametric stereo (ISO/IEC 14496-3 §8.6) ---- */
#define PS_MAX_ENV      5   /* four coded envelopes plus the implicit trailing one */
#define PS_NR_PAR       34
#define PS_NR_BANDS     91  /* hybrid sub-bands in the 34-parameter layout */
#define PS_QMF_SLOTS    32
#define PS_IN_SLOTS     38  /* QMF slots handed to the hybrid bank: 32 plus 6 of look-ahead */
#define PS_MAX_DELAY    14
#define PS_MAX_AP_DELAY 5

typedef struct {
    bool    start;          /* a header has been seen */
    bool    enable_iid, enable_icc, enable_ext, enable_ipdopd;
    bool    iid_quant;      /* fine (31-step) IID quantisation */
    uint8_t icc_mode;
    uint8_t nr_iid_par, nr_icc_par, nr_ipdopd_par;
    uint8_t frame_class, num_env, num_env_old;
    int8_t  border[PS_MAX_ENV + 1];
    int8_t  iid_par[PS_MAX_ENV][PS_NR_PAR];
    int8_t  icc_par[PS_MAX_ENV][PS_NR_PAR];
    int8_t  ipd_par[PS_MAX_ENV][PS_NR_PAR];
    int8_t  opd_par[PS_MAX_ENV][PS_NR_PAR];
    bool    is34, is34_old;

    float   in_buf[5][PS_IN_SLOTS + 6][2];                  /* hybrid analysis history */
    float   delay[PS_NR_BANDS][PS_QMF_SLOTS + PS_MAX_DELAY][2];
    float   ap_delay[50][3][PS_QMF_SLOTS + PS_MAX_AP_DELAY][2];
    float   peak_decay_nrg[PS_NR_PAR], power_smooth[PS_NR_PAR], peak_decay_diff_smooth[PS_NR_PAR];
    float   H[4][2][PS_MAX_ENV + 1][PS_NR_PAR];             /* mixing matrix per envelope border */
    int8_t  ipd_hist[17], opd_hist[17];                     /* two previous indices, packed */
} PSState;

/* ---- SBR (ISO/IEC 14496-3 §4.6.18) ---- */
#define SBR_SLOTS        32  /* QMF time slots per frame: numTimeSlots (16) * RATE (2) */
#define SBR_T_HFGEN      8   /* slots of the previous frame kept for the covariance and X_low */
#define SBR_T_HFADJ      2   /* offset of the envelope-adjusted region within the buffer */
#define SBR_BUF_SLOTS    (SBR_SLOTS + SBR_T_HFGEN)
#define SBR_MAX_BANDS    64
#define SBR_MAX_ENV      5
#define SBR_MAX_NQ       5
#define SBR_MAX_PATCHES  6
#define SBR_MAX_LIM      (SBR_MAX_BANDS + SBR_MAX_PATCHES + 2)

typedef struct {
    /* frame grid */
    uint8_t frame_class, L_E, L_Q, bs_pointer;
    int8_t  l_A;
    uint8_t t_E[SBR_MAX_ENV + 1], t_Q[3], freq_res[SBR_MAX_ENV];
    uint8_t df_env[SBR_MAX_ENV], df_noise[2];
    uint8_t invf_mode[SBR_MAX_NQ], invf_mode_prev[SBR_MAX_NQ];
    bool    add_harmonic_flag;
    uint8_t add_harmonic[SBR_MAX_BANDS];
    int16_t E[SBR_MAX_ENV][SBR_MAX_BANDS];
    int16_t Q[2][SBR_MAX_NQ];
    bool    amp_res; /* this frame's resolution (a single FIXFIX envelope forces 1.5 dB) */

    /* carried across frames */
    int16_t E_prev[SBR_MAX_BANDS];
    int16_t Q_prev[SBR_MAX_NQ];
    uint8_t freq_res_prev;
    float   bw_array[SBR_MAX_NQ];
    float   g_hist[4][SBR_MAX_BANDS];
    float   q_hist[4][SBR_MAX_BANDS];
    uint8_t s_index_prev[SBR_MAX_BANDS];
    int8_t  l_A_prev;
    uint8_t L_E_prev;
    uint8_t t_E_end_prev; /* RATE * t_E(L_E) of the previous frame */
    uint8_t kx_prev, M_prev;
    uint16_t index_noise;
    uint8_t  index_sine;
    bool    have_frame;   /* a payload has been decoded since the last reset */
    bool    primed;       /* smoothing history holds real gains */
    float   x_low_tail[32][SBR_T_HFGEN][2];
    float   y_tail[SBR_MAX_BANDS][SBR_T_HFGEN][2];
    float   qmf_x[320];  /* analysis delay line, newest sample first */
    float   qmf_v[1280]; /* synthesis delay line */
} SBRChannel;

/* Header and frequency tables, shared by the channels of one element and
 * stored at the element's first channel. */
typedef struct {
    bool header_present;
    bool coupling;
    uint8_t nch;
    bool amp_res;
    uint8_t start_freq, stop_freq, xover_band, freq_scale, alter_scale, noise_bands;
    uint8_t limiter_bands, limiter_gains, interpol_freq, smoothing_mode;
    uint8_t k0, k2, kx, M;
    uint8_t n_master, n_high, n_low, n_q, n_lim, num_patches;
    uint8_t f_master[SBR_MAX_BANDS + 1], f_high[SBR_MAX_BANDS + 1], f_low[SBR_MAX_BANDS + 1];
    uint8_t f_noise[SBR_MAX_NQ + 1], f_lim[SBR_MAX_LIM + 1];
    uint8_t patch_start[SBR_MAX_PATCHES], patch_num[SBR_MAX_PATCHES];
} SBRElement;

/* Per-frame working buffers, one channel at a time. */
typedef struct {
    float x_low[32][SBR_BUF_SLOTS][2];
    float x_high[SBR_MAX_BANDS][SBR_BUF_SLOTS][2];
    float y[SBR_MAX_BANDS][SBR_BUF_SLOTS][2];
    float x[PS_IN_SLOTS][64][2]; /* assembled output per slot (38 for the PS look-ahead) */
#ifndef FAAD_DISABLE_PS
    float ps_l[PS_NR_BANDS][PS_QMF_SLOTS][2];
    float ps_r[PS_NR_BANDS][PS_QMF_SLOTS][2];
    float ps_out[2][PS_QMF_SLOTS][64][2];
#endif
} SBRScratch;

struct faad_decoder {
    faad_config config;
    AudioSpecificConfig asc;
    bool asc_parsed;
    bool is_heap_allocated;

    uint32_t frame_samples; /* 1024 or 2048 */
    uint32_t num_channels;
    uint32_t sample_rate;      /* nominal (post-SBR) rate, for reporting */
    uint32_t core_sample_rate; /* the rate the AAC core codec itself (window/sfb layout) actually runs at -- Fs/2 of sample_rate when SBR is present */

    float spec[MAX_CHANNELS][FRAME_LEN_LONG];
    float overlap[MAX_CHANNELS][FRAME_LEN_LONG];
    uint8_t prev_window_shape[MAX_CHANNELS]; /* the left window half follows the previous block's shape */

#ifndef FAAD_DISABLE_SBR
    SBRChannel sbr[MAX_CHANNELS];
    SBRElement sbr_el[MAX_CHANNELS];
    SBRScratch sbr_scratch;
#endif
    bool sbr_present;

#ifndef FAAD_DISABLE_PS
    PSState ps;
#endif
    bool ps_present;

    uint32_t pns_seed;
    uint32_t consecutive_errors;
    float prev_spec[MAX_CHANNELS][FRAME_LEN_LONG];

    /* Frame decode scratch buffers moved from C call stack to reduce stack depth (<1 KB) */
    float pcm_float[MAX_CHANNELS * FRAME_LEN_LONG];
    float pcm_final[MAX_CHANNELS * 2048];

#ifdef FAAD_STATS
    FaadDecStats stats;
#endif
};

void setup_sfb_offsets(ICSInfo *ics, uint32_t sample_rate);
faad_status decode_scale_factor_data(BitReader *bs, ICSInfo *ics, uint32_t sample_rate
#ifdef FAAD_STATS
    , FaadDecStats *stats
#endif
);
faad_status decode_spectral_data(BitReader *bs, ICSInfo *ics, float *spec
#ifdef FAAD_STATS
    , FaadDecStats *stats
#endif
);
void dequantize_spectrum(ICSInfo *ics, float *spec);
void apply_pns(ICSInfo *ics, float *spec, uint32_t *pns_seed);
void apply_ms_stereo(CPEInfo *cpe, float *spec_l, float *spec_r);
void apply_is_stereo(CPEInfo *cpe, float *spec_l, float *spec_r);
void apply_freq_downmix_mono(float *spec_l, const float *spec_r);
void apply_tns(ICSInfo *ics, float *spec);
void imdct_and_window(struct faad_decoder *dec, uint32_t ch, ICSInfo *ics, float *spec, float *out_pcm);

faad_status decode_pce(BitReader *bs, struct faad_decoder *dec);
faad_status decode_cce(BitReader *bs, struct faad_decoder *dec);
faad_status decode_dse(BitReader *bs);
faad_status decode_ics(BitReader *bs, struct faad_decoder *dec, ICSInfo *ics, float *spec, bool common_window);
faad_status decode_cpe(BitReader *bs, struct faad_decoder *dec, CPEInfo *cpe, uint32_t ch);
faad_status decode_sce(BitReader *bs, struct faad_decoder *dec, ICSInfo *ics, uint32_t ch);

void faad_init_global_tables(void);
void sbr_init_tables(void);
faad_status sbr_decode_extension(struct faad_decoder *dec, BitReader *bs, uint32_t ch0, uint32_t syntax_id, bool crc);
void ps_read_data(struct faad_decoder *dec, BitReader *bs, uint32_t bits_left);
void ps_apply(struct faad_decoder *dec, float X[PS_IN_SLOTS][64][2], float L[PS_QMF_SLOTS][64][2], float R[PS_QMF_SLOTS][64][2], int top);
void init_ps_tables(void);
void sbr_apply(struct faad_decoder *dec, uint32_t num_ch, float *pcm_in, float *pcm_out);

#endif /* FAAD_INTERNAL_H */

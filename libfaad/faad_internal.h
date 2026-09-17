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

#include "faad.h"
#include "huffdata.h"

#define MAX_CHANNELS 8
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
    bool ms_mask_present;
    uint8_t ms_used[8][64];
    ICSInfo ics[2];
} CPEInfo;

typedef struct {
    bool header_present;
    bool enable_iid;
    bool enable_icc;
    int8_t iid_idx[SBR_PS_BANDS];
    int8_t icc_idx[SBR_PS_BANDS];
    float h11[SBR_PS_BANDS];
    float h22[SBR_PS_BANDS];
    float h12[SBR_PS_BANDS];
    float h21[SBR_PS_BANDS];
    float delay_r[3][64];
    float delay_i[3][64];
} PSState;

typedef struct {
    uint8_t bs_frame_class;
    uint8_t bs_num_env;
    uint8_t bs_freq_res[8];
    uint8_t bs_pointer;
    uint8_t t_E[9];
    uint8_t bs_num_noise;
    uint8_t bs_add_harmonic[64];
    int8_t  E_orig[8][64];
    int8_t  Q_orig[8][64];
    bool header_present;
    uint8_t bs_start_freq;
    uint8_t bs_stop_freq;
    uint8_t bs_xover_band;
    float qmf_delay[2][32][64];
    float qmf_ovl[640];
    float qmf_ana_ovl[320];
} SBRState;

struct faad_decoder {
    faad_config config;
    AudioSpecificConfig asc;
    bool asc_parsed;
    bool is_heap_allocated;

    uint32_t frame_samples; /* 1024 or 2048 */
    uint32_t num_channels;
    uint32_t sample_rate;

    float spec[MAX_CHANNELS][FRAME_LEN_LONG];
    float overlap[MAX_CHANNELS][FRAME_LEN_LONG];

    SBRState sbr[MAX_CHANNELS];
    bool sbr_present;

    PSState ps;
    bool ps_present;

    uint32_t pns_seed;
    uint32_t consecutive_errors;
    float prev_spec[MAX_CHANNELS][FRAME_LEN_LONG];
};

void setup_sfb_offsets(ICSInfo *ics, uint32_t sample_rate);
faad_status decode_scale_factor_data(BitReader *bs, ICSInfo *ics, uint32_t sample_rate);
faad_status decode_spectral_data(BitReader *bs, ICSInfo *ics, float *spec);
void dequantize_spectrum(ICSInfo *ics, float *spec);
void apply_pns(ICSInfo *ics, float *spec, uint32_t *pns_seed);
void apply_ms_stereo(CPEInfo *cpe, float *spec_l, float *spec_r);
void apply_is_stereo(CPEInfo *cpe, float *spec_l, float *spec_r);
void apply_freq_downmix_mono(float *spec_l, const float *spec_r);
void apply_tns(ICSInfo *ics, float *spec);
void imdct_and_window(struct faad_decoder *dec, uint32_t ch, ICSInfo *ics, float *spec, float *out_pcm);

faad_status decode_pce(BitReader *bs, struct faad_decoder *dec);
faad_status decode_dse(BitReader *bs);
faad_status decode_ics(BitReader *bs, struct faad_decoder *dec, ICSInfo *ics, float *spec, bool common_window);
faad_status decode_cpe(BitReader *bs, struct faad_decoder *dec, CPEInfo *cpe, uint32_t ch);
faad_status decode_sce(BitReader *bs, struct faad_decoder *dec, ICSInfo *ics, uint32_t ch);

void faad_init_global_tables(void);
void sbr_init_tables(void);
faad_status sbr_decode_extension(struct faad_decoder *dec, BitReader *bs, uint32_t ch, uint32_t syntax_id);
void sbr_apply(struct faad_decoder *dec, uint32_t num_ch, float *pcm_in, float *pcm_out);

#endif /* FAAD_INTERNAL_H */

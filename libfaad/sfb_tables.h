/*
 * Decoder-side scale-factor-band table glue.
 * The actual ISO/IEC 14496-3 SFB offset tables live in libfaab, shared with
 * the encoder's FFT/SBR-table/Huffman-codebook core.
 */

#ifndef FAAD_SFB_TABLES_H
#define FAAD_SFB_TABLES_H

#include <stdint.h>
#include "../libfaab/sfb_tables.h"

int get_sr_index(uint32_t sample_rate);

#endif /* FAAD_SFB_TABLES_H */

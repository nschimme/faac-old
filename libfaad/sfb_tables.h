/*
 * ISO/IEC 14496-3 Scale Factor Band (SFB) Tables for all AAC sample rates
 */

#ifndef SFB_TABLES_H
#define SFB_TABLES_H

#include <stdint.h>

extern const uint16_t * const sfb_offsets_1024[12];
extern const uint8_t num_sfbs_1024[12];

extern const uint16_t * const sfb_offsets_128[12];
extern const uint8_t num_sfbs_128[12];

int get_sr_index(uint32_t sample_rate);

#endif /* SFB_TABLES_H */

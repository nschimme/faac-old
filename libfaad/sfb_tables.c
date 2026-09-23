/*
 * Decoder-side scale-factor-band table glue.
 * The offset/width tables this used to define locally now live in libfaab
 * (see sfb_tables.h); this file only keeps the sample-rate-index lookup,
 * which needs faad_sample_rates and so stays decoder-specific.
 */

#include "faad_internal.h"
#include "sfb_tables.h"

int get_sr_index(uint32_t sample_rate)
{
    for (int i = 0; i < 12; i++) {
        if (faad_sample_rates[i] == sample_rate) {
            return i;
        }
    }
    return 4; /* Default 44.1 kHz */
}

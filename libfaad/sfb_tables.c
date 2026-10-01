/*
 * FAAD - Freeware Advanced Audio Decoder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

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

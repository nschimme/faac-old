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
 * The actual ISO/IEC 14496-3 SFB offset tables live in libfaab, shared with
 * the encoder's FFT/SBR-table/Huffman-codebook core.
 */

#ifndef FAAD_SFB_TABLES_H
#define FAAD_SFB_TABLES_H

#include <stdint.h>
#include "../libfaab/sfb_tables.h"

int get_sr_index(uint32_t sample_rate);

#endif /* FAAD_SFB_TABLES_H */

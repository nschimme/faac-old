/*
 * FAAM - Freeware Advanced Audio/Video Muxer
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
 * Utility functions and error strings for libfaam
 */

#include "libfaam_internal.h"

const char *faam_strerror(faam_status status)
{
    switch (status) {
    case FAAM_OK:
        return "Success";
    case FAAM_ERR_INVALID_ARG:
        return "Invalid argument or null pointer";
    case FAAM_ERR_BAD_CONTAINER:
        return "Invalid MP4 atom structure or corrupt container";
    case FAAM_ERR_IO_READ:
        return "I/O read error or premature EOF";
    case FAAM_ERR_IO_WRITE:
        return "I/O write error";
    case FAAM_ERR_INSUFFICIENT_MEM:
        return "Insufficient memory arena or allocation buffer";
    case FAAM_ERR_NO_TRACK:
        return "No matching video/audio track found in container";
    case FAAM_ERR_UNSUPPORTED:
        return "Feature or operation not supported or disabled";
    default:
        return "Unknown error code";
    }
}

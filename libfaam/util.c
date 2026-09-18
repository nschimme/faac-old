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

/*
 * Shared CLI helpers for the faac/faad/faam frontends.
 */

#include "cli_common.h"

FILE *cli_fopen(const char *path, const char *mode) {
#ifdef _WIN32
    return win32_fopen_utf8(path, mode);
#else
    return fopen(path, mode);
#endif
}

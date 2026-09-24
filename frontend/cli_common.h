/*
 * Shared CLI helpers for the faac/faad/faam frontends.
 */

#ifndef CLI_COMMON_H
#define CLI_COMMON_H

#ifdef _WIN32
#include "charset.h"
#else
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* fopen() a UTF-8 path: win32_fopen_utf8() on Windows, plain fopen()
   elsewhere. Same open-failure semantics as fopen() either way -- callers
   still check the returned NULL and report the error themselves. */
FILE *cli_fopen(const char *path, const char *mode);

#ifdef __cplusplus
}
#endif

#endif /* CLI_COMMON_H */

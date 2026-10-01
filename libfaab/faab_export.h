/*
 * FAAC - Freeware Advanced Audio Coder
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
 * FAAC - Freeware Advanced Audio Coder
 *
 * Export macro for libfaab's public API (FFT engine, SBR/Huffman/SFB
 * tables), mirroring FAACAPI/FAADAPI/FAAMAPI in include/faac.h, faad.h,
 * faam.h: hidden-by-default library, explicit default visibility on the
 * declared cross-library-boundary surface only.
 */

#ifndef FAAB_EXPORT_H
#define FAAB_EXPORT_H

#ifndef FAABAPI
# if defined(_WIN32)
   /* Linked statically on Windows (see meson.build): no DLL boundary. */
#  define FAABAPI
# elif defined(__GNUC__) && (__GNUC__ >= 4)
#  define FAABAPI __attribute__((visibility("default")))
# else
#  define FAABAPI
# endif
#endif

#endif /* FAAB_EXPORT_H */

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
#  define FAABAPI __declspec(dllexport)
# elif defined(__GNUC__) && (__GNUC__ >= 4)
#  define FAABAPI __attribute__((visibility("default")))
# else
#  define FAABAPI
# endif
#endif

#endif /* FAAB_EXPORT_H */

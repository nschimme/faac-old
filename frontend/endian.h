/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * Endianness and byte swapping utilities.
 */

#ifndef FAAC_ENDIAN_H
#define FAAC_ENDIAN_H

#include <stdint.h>
#include <stdbool.h>

#if defined(__has_builtin)
#if __has_builtin(__builtin_bswap16) && __has_builtin(__builtin_bswap32) && __has_builtin(__builtin_bswap64)
#define FAAC_HAVE_BSWAP_BUILTINS 1
#endif
#elif defined(__GNUC__)
#define FAAC_HAVE_BSWAP_BUILTINS 1
#endif

static inline uint16_t bswap16(uint16_t x) {
#if defined(FAAC_HAVE_BSWAP_BUILTINS)
    return __builtin_bswap16(x);
#elif defined(_MSC_VER)
    return _byteswap_ushort(x);
#else
    return (uint16_t)((x >> 8) | (x << 8));
#endif
}

static inline uint32_t bswap32(uint32_t x) {
#if defined(FAAC_HAVE_BSWAP_BUILTINS)
    return __builtin_bswap32(x);
#elif defined(_MSC_VER)
    return _byteswap_ulong(x);
#else
    return (x >> 24) | ((x >> 8) & 0xff00) | ((x << 8) & 0xff0000) | (x << 24);
#endif
}

static inline uint64_t bswap64(uint64_t x) {
#if defined(FAAC_HAVE_BSWAP_BUILTINS)
    return __builtin_bswap64(x);
#elif defined(_MSC_VER)
    return _byteswap_uint64(x);
#else
    return ((x >> 56) & 0x00000000000000FFULL) |
           ((x >> 40) & 0x000000000000FF00ULL) |
           ((x >> 24) & 0x0000000000FF0000ULL) |
           ((x >> 8)  & 0x00000000FF000000ULL) |
           ((x << 8)  & 0x000000FF00000000ULL) |
           ((x << 24) & 0x0000FF0000000000ULL) |
           ((x << 40) & 0x00FF000000000000ULL) |
           ((x << 56) & 0xFF00000000000000ULL);
#endif
}

static inline uint16_t htobe16(uint16_t x) {
#if WORDS_BIGENDIAN
    return x;
#else
    return bswap16(x);
#endif
}

static inline uint32_t htobe32(uint32_t x) {
#if WORDS_BIGENDIAN
    return x;
#else
    return bswap32(x);
#endif
}

static inline uint64_t htobe64(uint64_t x) {
#if WORDS_BIGENDIAN
    return x;
#else
    return bswap64(x);
#endif
}

static inline uint16_t htole16(uint16_t x) {
#if WORDS_BIGENDIAN
    return bswap16(x);
#else
    return x;
#endif
}

static inline uint32_t htole32(uint32_t x) {
#if WORDS_BIGENDIAN
    return bswap32(x);
#else
    return x;
#endif
}

static inline uint16_t le16toh(uint16_t x) {
#if WORDS_BIGENDIAN
    return bswap16(x);
#else
    return x;
#endif
}

static inline uint32_t le32toh(uint32_t x) {
#if WORDS_BIGENDIAN
    return bswap32(x);
#else
    return x;
#endif
}

static inline int16_t read_pcm16(const int16_t *p, bool bigendian) {
    uint16_t val;
    memcpy(&val, p, sizeof(val));
#if WORDS_BIGENDIAN
    if (!bigendian) val = bswap16(val);
#else
    if (bigendian) val = bswap16(val);
#endif
    return (int16_t)val;
}

static inline int32_t read_pcm24(const uint8_t *p, bool bigendian) {
    int32_t s;
    if (bigendian) {
        s = ((int32_t)p[0] << 16) | ((int32_t)p[1] << 8) | (int32_t)p[2];
    } else {
        s = (int32_t)p[0] | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);
    }
    if (s & 0x800000) s |= (int32_t)0xFF000000;
    return s;
}

static inline int32_t read_pcm32(const int32_t *p, bool bigendian) {
    uint32_t val;
    memcpy(&val, p, sizeof(val));
#if WORDS_BIGENDIAN
    if (!bigendian) val = bswap32(val);
#else
    if (bigendian) val = bswap32(val);
#endif
    return (int32_t)val;
}

#endif /* FAAC_ENDIAN_H */

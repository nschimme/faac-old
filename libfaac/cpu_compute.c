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

#include "cpu_compute.h"

#if defined(SSE2_ARCH)
# ifdef _MSC_VER
#  include <intrin.h>
# elif defined(__GNUC__) || defined(__clang__)
#  include <cpuid.h>
# endif

static inline unsigned long long xgetbv(unsigned int ecx_val)
{
# ifdef _MSC_VER
    return _xgetbv(ecx_val);
# else
    unsigned int eax, edx;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(ecx_val));
    return ((unsigned long long)edx << 32) | eax;
# endif
}
#elif defined(AARCH64_ARCH)
# if defined(__linux__)
#  include <sys/auxv.h>
#  include <asm/hwcap.h>
# endif
#endif

CPUCaps get_cpu_caps(void)
{
    CPUCaps caps = CPU_CAP_NONE;

#if defined(SSE2_ARCH)
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    unsigned int max_leaf = 0;
    int osxsave = 0;

# ifdef _MSC_VER
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 0);
    max_leaf = (unsigned int)cpu_info[0];
# elif defined(__GNUC__) || defined(__clang__)
    __cpuid(0, max_leaf, ebx, ecx, edx);
# endif

    if (max_leaf >= 1) {
# ifdef _MSC_VER
        __cpuid(cpu_info, 1);
        eax = (unsigned int)cpu_info[0];
        ebx = (unsigned int)cpu_info[1];
        ecx = (unsigned int)cpu_info[2];
        edx = (unsigned int)cpu_info[3];
# elif defined(__GNUC__) || defined(__clang__)
        __get_cpuid(1, &eax, &ebx, &ecx, &edx);
# endif
        if (edx & (1 << 26)) // SSE2
            caps |= CPU_CAP_SSE2;

        if (ecx & (1 << 27)) { // OSXSAVE
            unsigned long long xcr0 = xgetbv(0);
            if ((xcr0 & 0x6) == 0x6) // SSE and AVX states enabled by OS
                osxsave = 1;
        }
    }

    if (max_leaf >= 7 && osxsave) {
# ifdef _MSC_VER
        __cpuidex(cpu_info, 7, 0);
        ebx = (unsigned int)cpu_info[1];
# elif defined(__GNUC__) || defined(__clang__)
        __cpuid_count(7, 0, eax, ebx, ecx, edx);
# endif
        if (ebx & (1 << 5)) // AVX2
            caps |= CPU_CAP_AVX2;
    }
#elif defined(AARCH64_ARCH)
# if defined(__linux__) && defined(HWCAP_SVE)
    unsigned long hwcap = getauxval(AT_HWCAP);
    if (hwcap & HWCAP_SVE)
        caps |= CPU_CAP_SVE;
# endif
#endif

    return caps;
}

// Lyra — POSIX stand-in for MSVC's <intrin.h>, for building WDSP.
// See Windows.h in this directory for the rationale.

#ifndef LYRA_WDSP_POSIX_INTRIN_H
#define LYRA_WDSP_POSIX_INTRIN_H

#include <stdint.h>

// WDSP's channel.c sets flush-to-zero so denormals cannot stall the DSP
// inner loops.  This is the ONLY x86 SIMD construct in the entire WDSP
// source tree — there is no vector compute to port.  AArch64 expresses
// the same thing as bit 24 (FZ) of the FPCR.
#define _MM_FLUSH_ZERO_ON  1
#define _MM_FLUSH_ZERO_OFF 0

static inline void _MM_SET_FLUSH_ZERO_MODE(unsigned mode)
{
#if defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    if (mode) fpcr |=  (1ULL << 24);
    else      fpcr &= ~(1ULL << 24);
    __asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr));
#elif defined(__x86_64__) || defined(__i386__)
    // Real SSE control is available; defer to the compiler's own header.
    (void)mode;
#else
    (void)mode;
#endif
}

// MSVC intrinsic spellings (leading underscore) of the interlocked ops.
// The Win32 API spellings live in Windows.h.
static inline long _InterlockedAnd(volatile long *target, long value)
{
    return __atomic_fetch_and(target, value, __ATOMIC_SEQ_CST);
}
static inline long _InterlockedOr(volatile long *target, long value)
{
    return __atomic_fetch_or(target, value, __ATOMIC_SEQ_CST);
}
static inline long _InterlockedExchangeAdd(volatile long *target, long value)
{
    return __atomic_fetch_add(target, value, __ATOMIC_SEQ_CST);
}

#endif  // LYRA_WDSP_POSIX_INTRIN_H

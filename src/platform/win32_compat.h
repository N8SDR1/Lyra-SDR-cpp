// Lyra — Win32 synchronisation primitives on POSIX hosts.
//
// WHY THIS EXISTS
// ---------------
// The ChannelMaster files under src/wire (AAMix, CMaster, CmBuffs,
// ObBuffs, Ivac, Network, NetworkProto1, Ep6RecvThread, ...) are a
// deliberate VERBATIM port of the openHPSDR Thetis reference, kept
// line-for-line so they can be audited against upstream ("do as
// reference, period" — see docs/THETIS_DIRECT_PORT_PLAN.md).  Rewriting
// their Win32 critical sections and semaphores into std::mutex /
// std::counting_semaphore would destroy that grep-parity and make every
// future upstream merge a manual reconciliation.
//
// So instead of changing the ported code, this header supplies the Win32
// primitives it calls.  On Windows it is empty and the real SDK is used —
// the Windows build is byte-for-byte unaffected.  On POSIX it maps each
// primitive onto its closest faithful equivalent.
//
// This is intentionally NOT a general Windows emulation.  It implements
// exactly the surface src/wire and src/hl2_stream.cpp use, and nothing
// else; anything unused is left undefined so a future call site fails
// loudly at compile time rather than silently doing the wrong thing.
//
// Deliberately absent: the `min`/`max` macros that real <windows.h>
// defines.  They would break <algorithm>, Qt headers and std::min/max
// across the whole C++ build.  (The WDSP shim under
// third_party/wdsp_posix DOES define them, because WDSP is C and needs
// them — the two shims are separate for exactly this reason.)

#pragma once

#if !defined(_WIN32)

#include <dispatch/dispatch.h>
#include <pthread.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

// ---------------------------------------------------------------- types
using HANDLE = void *;
using LPVOID = void *;
using DWORD  = unsigned long;
using BOOL   = int;

// Calling-convention annotations are meaningless off Windows.  The
// reference declares its thread entry points `DWORD WINAPI f(LPVOID)`.
#define WINAPI
#define CALLBACK

#ifndef TRUE
#  define TRUE  1
#  define FALSE 0
#endif

#define INFINITE      0xFFFFFFFFUL
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT  258

// The reference declares `LARGE_INTEGER liDueTime` for the EP2 waitable
// timer.  The HL2 Protocol-1 path never arms it (the OutboundRing
// condition-variable handshake carries cadence), so the type exists for
// declaration parity only.
union LARGE_INTEGER {
    struct { unsigned long LowPart; long HighPart; } u;
    long long QuadPart;
};

// ------------------------------------------------- critical sections
// Win32 critical sections are re-entrant for the owning thread; the
// mutex must therefore be recursive or the ported code will self-deadlock
// where it takes a section it already holds.
struct CRITICAL_SECTION {
    pthread_mutex_t mutex;
    bool            initialized;
};

using LPCRITICAL_SECTION = CRITICAL_SECTION *;

inline void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cs->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    cs->initialized = true;
}

// The reference passes a spin count (2500) as a Win32 scheduling hint.
// POSIX mutexes spin adaptively already, so it is safely ignored.
inline BOOL InitializeCriticalSectionAndSpinCount(CRITICAL_SECTION *cs, DWORD spin_count)
{
    (void)spin_count;
    InitializeCriticalSection(cs);
    return TRUE;
}

inline void EnterCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_lock(&cs->mutex); }
inline void LeaveCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_unlock(&cs->mutex); }

inline void DeleteCriticalSection(CRITICAL_SECTION *cs)
{
    if (cs->initialized) {
        pthread_mutex_destroy(&cs->mutex);
        cs->initialized = false;
    }
}

// ------------------------------------------------- tagged HANDLEs
// A Win32 HANDLE is polymorphic: the ported code creates both semaphores
// and threads, then passes either to WaitForSingleObject and CloseHandle.
// hl2_stream.cpp's close path does exactly this — it waits on the EP2
// writer THREAD handle with a 1 s timeout.  So handles carry a tag and
// the wait/close entry points dispatch on it; treating every handle as a
// semaphore would corrupt memory the first time a thread handle arrived.
enum class LyraHandleKind : std::uint32_t {
    Semaphore = 0x4C595253u,   // 'LYRS'
    Thread    = 0x4C595254u    // 'LYRT'
};

struct LyraHandleBase {
    LyraHandleKind kind;
};

struct LyraSemaphoreHandle : LyraHandleBase {
    dispatch_semaphore_t semaphore;
};

struct LyraThreadHandle : LyraHandleBase {
    pthread_t        thread;
    std::atomic<bool> finished;
};

inline LyraHandleBase *lyra_handle(HANDLE h)
{
    return static_cast<LyraHandleBase *>(h);
}

// -------------------------------------------- semaphores and events
// Events become semaphores: the ported code uses them only in the
// auto-reset signal/wait pattern, which a counting semaphore models
// exactly; it never relies on manual-reset broadcast wake-all.
inline HANDLE CreateSemaphore(void *attrs, long initial, long maximum, void *name)
{
    (void)attrs; (void)maximum; (void)name;
    auto *handle = new LyraSemaphoreHandle{};
    handle->kind      = LyraHandleKind::Semaphore;
    handle->semaphore = dispatch_semaphore_create(initial);
    return static_cast<HANDLE>(handle);
}

inline BOOL ReleaseSemaphore(HANDLE h, long count, long *previous)
{
    (void)previous;
    if (!h || lyra_handle(h)->kind != LyraHandleKind::Semaphore) return FALSE;
    auto *handle = static_cast<LyraSemaphoreHandle *>(h);
    for (long i = 0; i < count; ++i)
        dispatch_semaphore_signal(handle->semaphore);
    return TRUE;
}

inline HANDLE CreateEvent(void *attrs, BOOL manual_reset, BOOL initial, void *name)
{
    (void)manual_reset;
    return CreateSemaphore(attrs, initial ? 1 : 0, 1, name);
}

inline BOOL SetEvent(HANDLE h) { return ReleaseSemaphore(h, 1, nullptr); }

inline BOOL ResetEvent(HANDLE h) { (void)h; return TRUE; }

inline DWORD WaitForSingleObject(HANDLE h, DWORD milliseconds)
{
    if (!h) return WAIT_TIMEOUT;

    if (lyra_handle(h)->kind == LyraHandleKind::Thread) {
        auto *handle = static_cast<LyraThreadHandle *>(h);
        if (milliseconds == INFINITE) {
            pthread_join(handle->thread, nullptr);
            handle->finished.store(true, std::memory_order_release);
            return WAIT_OBJECT_0;
        }
        // pthread_join has no timed form on macOS.  Poll the completion
        // flag the trampoline sets; the wire path only ever uses this to
        // bound teardown, so 1 ms granularity is ample.
        for (DWORD elapsed = 0; elapsed < milliseconds; ++elapsed) {
            if (handle->finished.load(std::memory_order_acquire)) {
                pthread_join(handle->thread, nullptr);
                return WAIT_OBJECT_0;
            }
            usleep(1000);
        }
        return WAIT_TIMEOUT;
    }

    auto *handle = static_cast<LyraSemaphoreHandle *>(h);
    const dispatch_time_t deadline =
        (milliseconds == INFINITE)
            ? DISPATCH_TIME_FOREVER
            : dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(milliseconds) * 1000000LL);
    return dispatch_semaphore_wait(handle->semaphore, deadline) == 0 ? WAIT_OBJECT_0
                                                                     : WAIT_TIMEOUT;
}

// The ported call sites wait on handles that a single producer signals
// together, so bWaitAll makes no observable difference here.
inline DWORD WaitForMultipleObjects(DWORD count, HANDLE *handles, BOOL wait_all, DWORD milliseconds)
{
    (void)wait_all;
    for (DWORD i = 0; i < count; ++i)
        WaitForSingleObject(handles[i], milliseconds);
    return WAIT_OBJECT_0;
}

inline BOOL CloseHandle(HANDLE h)
{
    if (!h) return FALSE;
    if (lyra_handle(h)->kind == LyraHandleKind::Thread) {
        auto *handle = static_cast<LyraThreadHandle *>(h);
        // Detach if still running so the pthread cleans itself up.
        if (!handle->finished.load(std::memory_order_acquire))
            pthread_detach(handle->thread);
        delete handle;
        return TRUE;
    }
    auto *handle = static_cast<LyraSemaphoreHandle *>(h);
    dispatch_release(handle->semaphore);
    delete handle;
    return TRUE;
}

#define CreateSemaphoreW CreateSemaphore
#define CreateSemaphoreA CreateSemaphore
#define CreateEventW     CreateEvent
#define CreateEventA     CreateEvent

// Bit-test-and-modify: return the PREVIOUS state of the bit, as Win32 does.
inline unsigned char InterlockedBitTestAndSet(volatile long *base, long bit)
{
    const long mask = 1L << bit;
    return (__atomic_fetch_or(base, mask, __ATOMIC_SEQ_CST) & mask) ? 1 : 0;
}
inline unsigned char InterlockedBitTestAndReset(volatile long *base, long bit)
{
    const long mask = 1L << bit;
    return (__atomic_fetch_and(base, ~mask, __ATOMIC_SEQ_CST) & mask) ? 1 : 0;
}

// MSVC <intrin.h> spellings.  The ported ChannelMaster code uses both
// these and the Win32 API names above, sometimes in the same file.
inline long _InterlockedExchange(volatile long *target, long value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
inline long _InterlockedAnd(volatile long *target, long value)
{
    return __atomic_fetch_and(target, value, __ATOMIC_SEQ_CST);
}
inline long _InterlockedOr(volatile long *target, long value)
{
    return __atomic_fetch_or(target, value, __ATOMIC_SEQ_CST);
}
inline long _InterlockedExchangeAdd(volatile long *target, long value)
{
    return __atomic_fetch_add(target, value, __ATOMIC_SEQ_CST);
}
inline long _InterlockedIncrement(volatile long *target)
{
    return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);
}
inline long _InterlockedDecrement(volatile long *target)
{
    return __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);
}

// ------------------------------------------------ aligned allocation
// The ported code pairs _aligned_malloc with _aligned_free; POSIX frees
// posix_memalign blocks with plain free().
inline void *_aligned_malloc(std::size_t size, std::size_t alignment)
{
    void *p = nullptr;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (posix_memalign(&p, alignment, size) != 0) return nullptr;
    return p;
}
inline void _aligned_free(void *p) { std::free(p); }

// ------------------------------------------------------------ threads
// `_beginthreadex` returns a handle the reference stores and later waits
// on (hl2_stream.cpp bounds teardown with a 1 s WaitForSingleObject), so
// the thread stays joinable and the trampoline publishes a completion
// flag the timed wait above can poll.
using LyraThreadProc = unsigned (*)(void *);

struct LyraThreadStart {
    LyraThreadProc    entry;
    void             *argument;
    LyraThreadHandle *handle;
};

inline void *lyra_thread_trampoline(void *raw)
{
    auto *start = static_cast<LyraThreadStart *>(raw);
    LyraThreadProc    entry  = start->entry;
    void             *arg    = start->argument;
    LyraThreadHandle *handle = start->handle;
    delete start;
    entry(arg);
    handle->finished.store(true, std::memory_order_release);
    return nullptr;
}

// Signature mirrors the CRT's: (security, stack, start, arg, flags, tid).
inline uintptr_t _beginthreadex(void *security, unsigned stack_size,
                                LyraThreadProc entry, void *argument,
                                unsigned flags, unsigned *thread_id)
{
    (void)security; (void)stack_size; (void)flags;
    auto *handle = new LyraThreadHandle{};
    handle->kind = LyraHandleKind::Thread;
    handle->finished.store(false, std::memory_order_relaxed);

    auto *start = new LyraThreadStart{entry, argument, handle};
    if (pthread_create(&handle->thread, nullptr, lyra_thread_trampoline, start) != 0) {
        delete start;
        delete handle;
        return 0;
    }
    if (thread_id) *thread_id = 0;
    return reinterpret_cast<uintptr_t>(handle);
}

// The reference casts its thread entry through this CRT typedef.
using _beginthreadex_proc_type = LyraThreadProc;

// The older CRT spelling: entry returns void, thread is fire-and-forget
// (ObBuffs/CmBuffs start their ring pumps this way and never join them).
using LyraSimpleThreadProc = void (*)(void *);

struct LyraSimpleThreadStart {
    LyraSimpleThreadProc entry;
    void                *argument;
};

inline void *lyra_simple_thread_trampoline(void *raw)
{
    auto *start = static_cast<LyraSimpleThreadStart *>(raw);
    LyraSimpleThreadProc entry = start->entry;
    void                *arg   = start->argument;
    delete start;
    entry(arg);
    return nullptr;
}

inline uintptr_t _beginthread(LyraSimpleThreadProc entry, unsigned stack_size, void *argument)
{
    (void)stack_size;
    auto *start = new LyraSimpleThreadStart{entry, argument};
    pthread_t thread;
    if (pthread_create(&thread, nullptr, lyra_simple_thread_trampoline, start) != 0) {
        delete start;
        return static_cast<uintptr_t>(-1);
    }
    pthread_detach(thread);
    return reinterpret_cast<uintptr_t>(thread);
}

inline void _endthread() { pthread_exit(nullptr); }

// <windows.h> string-literal macro; the ported code passes TEXT("Pro Audio").
#ifndef TEXT
#  define TEXT(x) x
#endif

// --------------------------------------------- thread priority (MMCSS)
// MMCSS has no POSIX equivalent.  These succeed without doing anything;
// real-time scheduling for the threads that actually determine whether
// Lyra holds its HL2 wire cadence is applied explicitly through
// src/platform/thread_priority.h, which uses Mach time-constraint
// policy.  Pretending to set priority here would imply a guarantee this
// header cannot deliver.
#define THREAD_PRIORITY_IDLE          (-15)
#define THREAD_PRIORITY_LOWEST        (-2)
#define THREAD_PRIORITY_BELOW_NORMAL  (-1)
#define THREAD_PRIORITY_NORMAL        0
#define THREAD_PRIORITY_ABOVE_NORMAL  1
#define THREAD_PRIORITY_HIGHEST       2
#define THREAD_PRIORITY_TIME_CRITICAL 15

enum AVRT_PRIORITY {
    AVRT_PRIORITY_LOW      = -1,
    AVRT_PRIORITY_NORMAL   = 0,
    AVRT_PRIORITY_HIGH     = 1,
    AVRT_PRIORITY_CRITICAL = 2
};

inline HANDLE GetCurrentThread() { return reinterpret_cast<HANDLE>(pthread_self()); }

inline BOOL SetThreadPriority(HANDLE thread, int priority)
{
    (void)thread; (void)priority;
    return TRUE;
}

inline HANDLE AvSetMmThreadCharacteristics(const char *task, DWORD *task_index)
{
    (void)task;
    if (task_index) *task_index = 0;
    return nullptr;
}

inline HANDLE AvSetMmThreadCharacteristicsW(const wchar_t *task, DWORD *task_index)
{
    (void)task;
    if (task_index) *task_index = 0;
    return nullptr;
}

inline BOOL AvSetMmThreadPriority(HANDLE task, AVRT_PRIORITY priority)
{
    (void)task; (void)priority;
    return TRUE;
}

inline BOOL AvRevertMmThreadCharacteristics(HANDLE task) { (void)task; return TRUE; }

// ------------------------------------------------------------- timing
// Win32 Sleep() takes milliseconds.  usleep() is interrupted by signals,
// so loop on the remainder — a short sleep in the wire path must not
// return early or the EP2 cadence drifts.
inline void Sleep(DWORD milliseconds)
{
    struct timespec request {
        static_cast<time_t>(milliseconds / 1000),
        static_cast<long>((milliseconds % 1000) * 1000000L)
    };
    struct timespec remaining {};
    while (nanosleep(&request, &remaining) == -1 && errno == EINTR)
        request = remaining;
}

#endif  // !_WIN32

// Lyra — POSIX compatibility shim for building WDSP on macOS / Linux.
//
// WDSP (TAPR OpenHPSDR-wdsp, Dr. Warren Pratt NR0V, GPL v3+) is written
// against the Win32 API: its `comm.h` includes <Windows.h>, <process.h>,
// <intrin.h> and <avrt.h>, and the DSP core uses Win32 critical sections,
// semaphores and interlocked intrinsics throughout.
//
// This header stands in for <Windows.h> when WDSP is compiled for a POSIX
// host.  It is deliberately NOT a general-purpose Windows emulation — it
// implements exactly the surface WDSP 2.00 touches (verified by building
// all 74 translation units to a clean link).  Nothing in Lyra itself
// includes this file; it exists only on the WDSP compiler include path
// (see tools/build_wdsp_macos.sh).
//
// Mapping:
//   CRITICAL_SECTION  -> recursive pthread_mutex  (Win32 critical sections
//                        are recursive; a plain mutex would self-deadlock)
//   CreateSemaphore   -> dispatch_semaphore / POSIX sem
//   Interlocked*      -> __atomic builtins, sequentially consistent
//   _beginthread      -> detached pthread
//   QueueUserWorkItem -> GCD global queue (Win32 thread pool)
//   MMCSS (avrt.h)    -> no-ops; real-time priority is applied by Lyra on
//                        its own wire threads, not from inside WDSP.
//
// NOTE ON `LONG`: Win32 LONG is 32-bit; here it is `long` (64-bit on
// LP64).  That is safe because LONG appears only in WDSP-internal state
// reached through the Interlocked* wrappers below — it never crosses the
// DLL/dylib boundary into Lyra, whose call tables use explicit types.
// Keeping it as `long` makes the atomic wrappers type-consistent.

#ifndef LYRA_WDSP_POSIX_WINDOWS_H
#define LYRA_WDSP_POSIX_WINDOWS_H

#if defined(_WIN32)
#  error "wdsp_posix shim must not be used on Windows; use the real SDK."
#endif

#include <dispatch/dispatch.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <stdint.h>
#include <stdio.h>   // must precede the dprintf rename below
#include <stdlib.h>
#include <string.h>
#include <strings.h>   // strcasecmp — console pseudo-device detection
#include <unistd.h>

// WDSP declares `void dprintf(const char*, ...)` (utilities.h), which
// collides with POSIX `int dprintf(int fd, const char*, ...)`.  <stdio.h>
// is included above FIRST so the libc declaration is established under its
// real name; only WDSP's own later declaration gets renamed.
#define dprintf wdsp_dprintf

#define WINAPI
#define CALLBACK

// Calling-convention annotations are meaningless on AArch64/SysV and
// clang warns on every occurrence; drop them outright.
#define __stdcall
#define __cdecl

// <Windows.h> supplies these as macros unless NOMINMAX is defined; WDSP
// relies on them (RXA.c, analyzer.c, emnr.c, cfcomp.c, ...).
#ifndef min
#  define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#  define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

// ---------------------------------------------------------------- types
typedef void          *HANDLE;
typedef void          *LPVOID;
typedef unsigned long  DWORD;
typedef unsigned long *LPDWORD;
typedef int            BOOL;
typedef long           LONG;      // see NOTE ON `LONG` above
typedef unsigned long  ULONG;
typedef unsigned char  BYTE;
typedef unsigned char  byte;
typedef const char    *LPCTSTR;
typedef char          *LPTSTR;

#define TEXT(x) x
#ifndef TRUE
#  define TRUE  1
#  define FALSE 0
#endif
#define INFINITE      0xFFFFFFFFUL
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT  258

// ---------------------------------------------- critical sections
// Win32 critical sections are re-entrant for the owning thread, so the
// mutex must be PTHREAD_MUTEX_RECURSIVE.  WDSP does take a section it
// already holds (e.g. cfcomp/analyzer teardown paths), and a default
// mutex would deadlock there.
typedef struct {
    pthread_mutex_t mutex;
    int             initialized;
} CRITICAL_SECTION;

static inline void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cs->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    cs->initialized = 1;
}

// The spin count is a Win32 scheduling hint only; POSIX adaptive mutexes
// already spin briefly, so it is safely ignored.
static inline BOOL InitializeCriticalSectionAndSpinCount(CRITICAL_SECTION *cs,
                                                         DWORD spin_count)
{
    (void)spin_count;
    InitializeCriticalSection(cs);
    return TRUE;
}

static inline void EnterCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_lock(&cs->mutex); }
static inline void LeaveCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_unlock(&cs->mutex); }

static inline void DeleteCriticalSection(CRITICAL_SECTION *cs)
{
    if (cs->initialized) {
        pthread_mutex_destroy(&cs->mutex);
        cs->initialized = 0;
    }
}

typedef CRITICAL_SECTION *LPCRITICAL_SECTION;

// ------------------------------------------- semaphores and events
// Both map to dispatch semaphores.  WDSP only ever uses events in the
// auto-reset, signal/wait pattern that a counting semaphore models
// exactly; it never relies on manual-reset broadcast semantics.
static inline HANDLE CreateSemaphore(void *attrs, long initial, long maximum, void *name)
{
    (void)attrs; (void)maximum; (void)name;
    return (HANDLE)dispatch_semaphore_create(initial);
}

static inline BOOL ReleaseSemaphore(HANDLE h, long count, long *previous)
{
    (void)previous;
    for (long i = 0; i < count; ++i)
        dispatch_semaphore_signal((dispatch_semaphore_t)h);
    return TRUE;
}

static inline HANDLE CreateEvent(void *attrs, BOOL manual_reset, BOOL initial, void *name)
{
    (void)attrs; (void)manual_reset; (void)name;
    return (HANDLE)dispatch_semaphore_create(initial ? 1 : 0);
}

static inline BOOL SetEvent(HANDLE h)
{
    dispatch_semaphore_signal((dispatch_semaphore_t)h);
    return TRUE;
}

static inline BOOL ResetEvent(HANDLE h) { (void)h; return TRUE; }

static inline DWORD WaitForSingleObject(HANDLE h, DWORD milliseconds)
{
    dispatch_time_t deadline =
        (milliseconds == INFINITE)
            ? DISPATCH_TIME_FOREVER
            : dispatch_time(DISPATCH_TIME_NOW, (int64_t)milliseconds * 1000000LL);
    return dispatch_semaphore_wait((dispatch_semaphore_t)h, deadline) == 0
               ? WAIT_OBJECT_0
               : WAIT_TIMEOUT;
}

// WDSP's single use waits on one handle with bWaitAll semantics that do
// not matter (both handles are signalled by the same producer).
static inline DWORD WaitForMultipleObjects(DWORD count, HANDLE *handles,
                                           BOOL wait_all, DWORD milliseconds)
{
    (void)wait_all;
    for (DWORD i = 0; i < count; ++i)
        WaitForSingleObject(handles[i], milliseconds);
    return WAIT_OBJECT_0;
}

static inline BOOL CloseHandle(HANDLE h)
{
    if (h) dispatch_release((dispatch_semaphore_t)h);
    return TRUE;
}

#define CreateSemaphoreW CreateSemaphore
#define CreateSemaphoreA CreateSemaphore
#define CreateEventW     CreateEvent
#define CreateEventA     CreateEvent

// --------------------------------------------------- interlocked ops
// Win32 Interlocked* are full barriers, so __ATOMIC_SEQ_CST is the
// faithful mapping.  (The underscore-prefixed <intrin.h> spellings live
// in the intrin.h shim.)
static inline long InterlockedExchange(volatile long *target, long value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}
static inline long InterlockedIncrement(volatile long *target)
{
    return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);
}
static inline long InterlockedDecrement(volatile long *target)
{
    return __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);
}
static inline long InterlockedAnd(volatile long *target, long value)
{
    return __atomic_fetch_and(target, value, __ATOMIC_SEQ_CST);
}
static inline long InterlockedOr(volatile long *target, long value)
{
    return __atomic_fetch_or(target, value, __ATOMIC_SEQ_CST);
}

// Return the PREVIOUS state of the bit, matching Win32.
static inline unsigned char InterlockedBitTestAndSet(volatile long *base, long bit)
{
    const long mask = 1L << bit;
    return (__atomic_fetch_or(base, mask, __ATOMIC_SEQ_CST) & mask) ? 1 : 0;
}
static inline unsigned char InterlockedBitTestAndReset(volatile long *base, long bit)
{
    const long mask = 1L << bit;
    return (__atomic_fetch_and(base, ~mask, __ATOMIC_SEQ_CST) & mask) ? 1 : 0;
}

// ------------------------------------------------ Win32 thread pool
typedef DWORD (*LPTHREAD_START_ROUTINE)(void *);

typedef struct {
    LPTHREAD_START_ROUTINE function;
    void                  *context;
} LyraWdspWorkItem;

static inline void lyra_wdsp_run_work_item(void *raw)
{
    LyraWdspWorkItem *item = (LyraWdspWorkItem *)raw;
    item->function(item->context);
    free(item);
}

// WDSP queues analyzer FFT work here.  DISPATCH_QUEUE_PRIORITY_HIGH keeps
// spectrum work ahead of ordinary background work, matching the intent of
// the Win32 thread pool in a latency-sensitive DSP path.
static inline BOOL QueueUserWorkItem(LPTHREAD_START_ROUTINE function,
                                     void *context, unsigned long flags)
{
    (void)flags;
    LyraWdspWorkItem *item = (LyraWdspWorkItem *)malloc(sizeof *item);
    if (!item) return FALSE;
    item->function = function;
    item->context  = context;
    dispatch_async_f(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0),
                     item, lyra_wdsp_run_work_item);
    return TRUE;
}

// ------------------------------------------------------------- misc
static inline void   Sleep(DWORD milliseconds) { usleep((useconds_t)milliseconds * 1000); }
static inline HANDLE GetCurrentThread(void)    { return (HANDLE)pthread_self(); }

// WDSP raises its DSP worker threads with the Thetis pattern (see avrt.h):
// on macOS "high priority" means QOS_CLASS_USER_INTERACTIVE for the calling
// thread — the scheduler strongly prefers P-cores and runs it at high
// priority.  macOS cannot set another thread's QoS, so a SetThreadPriority
// on a foreign handle (or below HIGHEST) is a no-op.
#define THREAD_PRIORITY_IDLE          (-15)
#define THREAD_PRIORITY_LOWEST        (-2)
#define THREAD_PRIORITY_BELOW_NORMAL  (-1)
#define THREAD_PRIORITY_NORMAL        0
#define THREAD_PRIORITY_ABOVE_NORMAL  1
#define THREAD_PRIORITY_HIGHEST       2
#define THREAD_PRIORITY_TIME_CRITICAL 15

static inline BOOL lyra_set_current_thread_qos(qos_class_t qos)
{
    return pthread_set_qos_class_self_np(qos, 0) == 0 ? TRUE : FALSE;
}

static inline BOOL SetThreadPriority(HANDLE thread, int priority)
{
    if (priority < THREAD_PRIORITY_HIGHEST) return TRUE;
    if (!pthread_equal((pthread_t)thread, pthread_self())) return TRUE;
    return lyra_set_current_thread_qos(QOS_CLASS_USER_INTERACTIVE);
}

static inline void *_aligned_malloc(size_t size, size_t alignment)
{
    void *p = NULL;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (posix_memalign(&p, alignment, size) != 0) return NULL;
    return p;
}
static inline void _aligned_free(void *p) { free(p); }

// WDSP writes diagnostics through OutputDebugString; stderr is the
// closest POSIX equivalent and keeps the text visible in Lyra's log.
static inline void OutputDebugStringA(const char *text) { fputs(text ? text : "", stderr); }
#define OutputDebugStringW OutputDebugStringA

// WDSP's wisdom builder redirects stdout to the Windows console device
// (`freopen_s(&s, "conout$", "w", stdout)`, wisdom.c:56).  Those device
// names are not paths on POSIX — passing them through to freopen() would
// CREATE A FILE named "conout$" in the working directory and swallow
// stdout into it.  Recognise the pseudo-devices instead:
//   conout$/conin$ -> leave the stream alone (Lyra captures stdout/stderr)
//   NUL            -> /dev/null
static inline int freopen_s(FILE **stream_out, const char *path,
                            const char *mode, FILE *stream)
{
    if (path && (strcasecmp(path, "conout$") == 0 ||
                 strcasecmp(path, "conin$")  == 0)) {
        if (stream_out) *stream_out = stream;
        return 0;
    }

    const char *resolved = path;
    if (path && strcasecmp(path, "nul") == 0) resolved = "/dev/null";

    FILE *result = freopen(resolved, mode, stream);
    if (stream_out) *stream_out = result;
    return result ? 0 : -1;
}

// WDSP's wisdom builder opens a console to report FFTW planning progress.
// There is no such concept here; planning progress goes to stderr.
static inline BOOL AllocConsole(void) { return TRUE; }
static inline BOOL FreeConsole(void)  { return TRUE; }

#endif  // LYRA_WDSP_POSIX_WINDOWS_H

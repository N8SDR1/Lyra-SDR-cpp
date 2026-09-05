// Lyra — POSIX stand-in for Windows' <avrt.h> (MMCSS), for building WDSP.
//
// MMCSS ("Multimedia Class Scheduler Service") has no POSIX equivalent.
// These are deliberate no-ops rather than an approximation: WDSP calls
// them on its internal worker threads, while the threads whose scheduling
// actually determines whether Lyra holds its HL2 wire cadence are Lyra's
// own EP2/EP6 threads.  Those get real Mach time-constraint scheduling in
// src/platform/thread_priority.h.  Faking priority here would suggest a
// guarantee this shim cannot make.

#ifndef LYRA_WDSP_POSIX_AVRT_H
#define LYRA_WDSP_POSIX_AVRT_H

typedef enum {
    AVRT_PRIORITY_LOW      = -1,
    AVRT_PRIORITY_NORMAL   = 0,
    AVRT_PRIORITY_HIGH     = 1,
    AVRT_PRIORITY_CRITICAL = 2
} AVRT_PRIORITY;

static inline HANDLE AvSetMmThreadCharacteristics(LPCTSTR task_name, DWORD *task_index)
{
    (void)task_name;
    if (task_index) *task_index = 0;
    return (HANDLE)0;
}

static inline BOOL AvSetMmThreadPriority(HANDLE task, AVRT_PRIORITY priority)
{
    (void)task; (void)priority;
    return TRUE;
}

static inline BOOL AvRevertMmThreadCharacteristics(HANDLE task)
{
    (void)task;
    return TRUE;
}

#endif  // LYRA_WDSP_POSIX_AVRT_H

// Lyra — POSIX stand-in for Windows' <avrt.h> (MMCSS), for building WDSP.
//
// MMCSS ("Multimedia Class Scheduler Service") has no macOS equivalent.
// WDSP's main.c raises each channel's DSP thread with
// AvSetMmThreadCharacteristics("Pro Audio") and reverts it on exit; both
// act on the CALLING thread, mapped onto QOS_CLASS_USER_INTERACTIVE (see
// SetThreadPriority in Windows.h).  QoS rather than Mach time-constraint
// policy: a DSP buffer can take milliseconds, which would overrun a
// time-constraint computation budget.
//
// The returned task handle is an opaque marker.  WDSP only hands it back
// to AvSetMmThreadPriority / AvRevertMmThreadCharacteristics.

#ifndef LYRA_WDSP_POSIX_AVRT_H
#define LYRA_WDSP_POSIX_AVRT_H

typedef enum {
    AVRT_PRIORITY_LOW      = -1,
    AVRT_PRIORITY_NORMAL   = 0,
    AVRT_PRIORITY_HIGH     = 1,
    AVRT_PRIORITY_CRITICAL = 2
} AVRT_PRIORITY;

static char lyra_mmcss_task_marker;

static inline HANDLE AvSetMmThreadCharacteristics(LPCTSTR task_name, DWORD *task_index)
{
    (void)task_name;
    if (task_index) *task_index = 0;
    if (!lyra_set_current_thread_qos(QOS_CLASS_USER_INTERACTIVE))
        return (HANDLE)0;
    return (HANDLE)&lyra_mmcss_task_marker;
}

static inline BOOL AvSetMmThreadPriority(HANDLE task, AVRT_PRIORITY priority)
{
    (void)task; (void)priority;
    return TRUE;
}

static inline BOOL AvRevertMmThreadCharacteristics(HANDLE task)
{
    if (!task) return TRUE;
    return lyra_set_current_thread_qos(QOS_CLASS_DEFAULT);
}

#endif  // LYRA_WDSP_POSIX_AVRT_H

// Lyra — POSIX stand-in for MSVC's <process.h>, for building WDSP.
// See Windows.h in this directory for the rationale.

#ifndef LYRA_WDSP_POSIX_PROCESS_H
#define LYRA_WDSP_POSIX_PROCESS_H

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

typedef void (*LyraWdspThreadProc)(void *);

typedef struct {
    LyraWdspThreadProc entry;
    void              *argument;
} LyraWdspThreadStart;

static inline void *lyra_wdsp_thread_trampoline(void *raw)
{
    LyraWdspThreadStart *start = (LyraWdspThreadStart *)raw;
    LyraWdspThreadProc   entry = start->entry;
    void                *arg   = start->argument;
    free(start);
    entry(arg);
    return NULL;
}

// WDSP starts its DSP worker threads with _beginthread and never joins
// them — they run until the channel is torn down.  Detached pthreads have
// exactly those semantics and need no handle bookkeeping.
static inline uintptr_t _beginthread(LyraWdspThreadProc entry,
                                     unsigned stack_size, void *argument)
{
    (void)stack_size;   // default 512 KB stack is ample for WDSP workers
    LyraWdspThreadStart *start = (LyraWdspThreadStart *)malloc(sizeof *start);
    if (!start) return (uintptr_t)-1;
    start->entry    = entry;
    start->argument = argument;

    pthread_t thread;
    if (pthread_create(&thread, NULL, lyra_wdsp_thread_trampoline, start) != 0) {
        free(start);
        return (uintptr_t)-1;
    }
    pthread_detach(thread);
    return (uintptr_t)thread;
}

static inline void _endthread(void) { pthread_exit(NULL); }

#endif  // LYRA_WDSP_POSIX_PROCESS_H

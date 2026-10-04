/* Offline test: WDSP's flush_iobuffs() pattern — close a semaphore whose
 * count has dropped below its initial value, then recreate it — must not
 * crash.  libdispatch traps (SIGTRAP) when a dispatch semaphore is disposed
 * below its creation value; Windows does not.  Covers the WDSP POSIX shim
 * (third_party/wdsp_posix/include).  A crash here IS the failure.
 *   build: cmake --build build --target test_wdsp_posix_semaphore
 *   run:   build/test_wdsp_posix_semaphore
 */
#include <Windows.h>

#include <stdio.h>

static int g_failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-60s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (!ok) ++g_failures;
}

int main(void)
{
    printf("test_wdsp_posix_semaphore\n");

    /* wdsp 2.00 iobuffs.c: Sem_OutReady = CreateSemaphore(0, n, 1000, 0) */
    const long n = 4;
    HANDLE sem = CreateSemaphore(0, n, 1000, 0);
    int got = 0;
    for (int i = 0; i < n + 1; ++i)
        if (WaitForSingleObject(sem, 0) == WAIT_OBJECT_0) ++got;
    check("CreateSemaphore(initial=4) yields exactly 4 waits", got == n);

    /* iobuffs.c:458-459 — close below the initial count, then recreate */
    CloseHandle(sem);
    check("CloseHandle below the initial count does not crash", 1);
    sem = CreateSemaphore(0, n, 1000, 0);
    check("recreated semaphore is immediately waitable",
          WaitForSingleObject(sem, 0) == WAIT_OBJECT_0);
    CloseHandle(sem);

    HANDLE ev = CreateEvent(0, FALSE, TRUE, 0);
    check("CreateEvent(initial=TRUE) is signalled",
          WaitForSingleObject(ev, 0) == WAIT_OBJECT_0);
    CloseHandle(ev);
    check("CloseHandle on a consumed initially-set event does not crash", 1);

    printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASS",
           g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

/* Offline test: the MMCSS stand-ins in WDSP's POSIX shim
 * (third_party/wdsp_posix/include) raise WDSP's calling thread to
 * QOS_CLASS_USER_INTERACTIVE and revert it — the exact sequence WDSP's
 * main.c runs on each channel's DSP thread.
 *   build: cmake --build build --target test_wdsp_posix_qos
 *   run:   build/test_wdsp_posix_qos
 */
#include <Windows.h>
#include <avrt.h>

#include <pthread/qos.h>
#include <stdio.h>

static int g_failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-60s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

static void *wdspMainPattern(void *arg)
{
    (void)arg;
    /* wdsp 2.00 main.c:30-34 */
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
    if (hTask != 0) AvSetMmThreadPriority(hTask, 2);
    else SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    check("AvSetMmThreadCharacteristics returns a task handle", hTask != 0);
    check("  ... and raises the thread to USER_INTERACTIVE",
          qos_class_self() == QOS_CLASS_USER_INTERACTIVE);
    /* wdsp 2.00 main.c:60 */
    if (hTask != 0) AvRevertMmThreadCharacteristics(hTask);
    check("AvRevertMmThreadCharacteristics drops back to DEFAULT",
          qos_class_self() == QOS_CLASS_DEFAULT);
    return NULL;
}

static void *setPriorityPattern(void *arg)
{
    (void)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    check("SetThreadPriority(self, HIGHEST) raises to USER_INTERACTIVE",
          qos_class_self() == QOS_CLASS_USER_INTERACTIVE);
    return NULL;
}

static void runOnThread(void *(*fn)(void *))
{
    pthread_t t;
    pthread_create(&t, NULL, fn, NULL);
    pthread_join(t, NULL);
}

int main(void)
{
    printf("test_wdsp_posix_qos\n");
    runOnThread(wdspMainPattern);
    runOnThread(setPriorityPattern);
    printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASS",
           g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

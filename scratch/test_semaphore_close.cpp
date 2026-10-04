// Offline test: closing a Win32-shim semaphore whose count has dropped below
// its initial value must not crash.  libdispatch traps (SIGTRAP) when a
// dispatch semaphore is disposed below its creation value; Windows does not.
// Covers src/platform/win32_compat.h (Lyra's ported ChannelMaster code).
// A crash here IS the failure — the process dies before printing ALL PASS.
//   build: cmake --build build --target test_semaphore_close
//   run:   build/test_semaphore_close
#include "platform/win32_compat.h"

#include <cstdio>

static int g_failures = 0;

static void check(const char *name, bool ok)
{
    std::printf("  %-60s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

int main()
{
    std::printf("test_semaphore_close\n");

    // Initial count honoured: three immediate waits succeed, the fourth
    // times out.
    HANDLE sem = CreateSemaphore(nullptr, 3, 1000, nullptr);
    int got = 0;
    for (int i = 0; i < 4; ++i)
        if (WaitForSingleObject(sem, 0) == WAIT_OBJECT_0) ++got;
    check("CreateSemaphore(initial=3) yields exactly 3 waits", got == 3);
    // Count is now 0, below the initial 3 — Windows closes this fine.
    CloseHandle(sem);
    check("CloseHandle below the initial count does not crash", true);

    HANDLE ev = CreateEvent(nullptr, FALSE, TRUE, nullptr);
    check("CreateEvent(initial=TRUE) is signalled",
          WaitForSingleObject(ev, 0) == WAIT_OBJECT_0);
    CloseHandle(ev);
    check("CloseHandle on a consumed initially-set event does not crash", true);

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASS",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

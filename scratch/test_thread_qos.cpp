// Offline test: the macOS MMCSS stand-ins in src/platform/win32_compat.h
// raise the calling thread to QOS_CLASS_USER_INTERACTIVE and revert it.
//   build: cmake --build build --target test_thread_qos
//   run:   build/test_thread_qos
#include "platform/win32_compat.h"

#include <pthread/qos.h>
#include <cstdio>
#include <thread>

static int g_failures = 0;

static void check(const char *name, bool ok)
{
    std::printf("  %-60s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

static qos_class_t currentQos()
{
    return qos_class_self();
}

int main()
{
    std::printf("test_thread_qos\n");

    // The Thetis pattern: AvSetMmThreadCharacteristics, then the
    // AvSetMmThreadPriority / SetThreadPriority fallback, then revert.
    std::thread([] {
        DWORD taskIndex = 0;
        HANDLE task = AvSetMmThreadCharacteristics("Pro Audio", &taskIndex);
        check("AvSetMmThreadCharacteristics returns a task handle",
              task != nullptr);
        check("  ... and raises the thread to USER_INTERACTIVE",
              currentQos() == QOS_CLASS_USER_INTERACTIVE);
        check("AvSetMmThreadPriority succeeds on that handle",
              AvSetMmThreadPriority(task, AVRT_PRIORITY_CRITICAL) == TRUE);
        check("AvRevertMmThreadCharacteristics succeeds",
              AvRevertMmThreadCharacteristics(task) == TRUE);
        check("  ... and drops the thread back to DEFAULT",
              currentQos() == QOS_CLASS_DEFAULT);
    }).join();

    std::thread([] {
        DWORD taskIndex = 0;
        HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        check("AvSetMmThreadCharacteristicsW raises to USER_INTERACTIVE",
              task != nullptr && currentQos() == QOS_CLASS_USER_INTERACTIVE);
    }).join();

    std::thread([] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        check("SetThreadPriority(self, HIGHEST) raises to USER_INTERACTIVE",
              currentQos() == QOS_CLASS_USER_INTERACTIVE);
    }).join();

    std::thread([] {
        const qos_class_t before = currentQos();
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        check("SetThreadPriority(self, NORMAL) leaves QoS alone",
              currentQos() == before);
    }).join();

    // macOS cannot change another thread's QoS; the shim must not touch
    // the CALLING thread when handed some other thread's handle.
    std::thread([] {
        std::thread other([] {});
        const HANDLE otherHandle =
            reinterpret_cast<HANDLE>(other.native_handle());
        const qos_class_t before = currentQos();
        SetThreadPriority(otherHandle, THREAD_PRIORITY_HIGHEST);
        check("SetThreadPriority(other thread) leaves the caller alone",
              currentQos() == before);
        other.join();
    }).join();

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASS",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

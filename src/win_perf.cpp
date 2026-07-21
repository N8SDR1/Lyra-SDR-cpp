// Lyra — Windows performance controls implementation.  See win_perf.h.

#include "win_perf.h"

#include <QDebug>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>   // ShellExecuteExW ("runas")
#endif

namespace lyra::perf {

#ifdef _WIN32

namespace {

constexpr wchar_t kThrottleSubkey[] =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
constexpr wchar_t kThrottleValue[]  = L"NetworkThrottlingIndex";

constexpr DWORD kThrottleDisabled = 0xFFFFFFFF;   // no throttle (SDR/gaming fix)
constexpr DWORD kThrottleDefault  = 0x0000000A;   // Windows client default (10)

} // namespace

void applyProcessPriority(int level) {
    DWORD cls = NORMAL_PRIORITY_CLASS;
    switch (static_cast<ProcessPriority>(level)) {
    case ProcessPriority::High:        cls = HIGH_PRIORITY_CLASS;         break;
    case ProcessPriority::AboveNormal: cls = ABOVE_NORMAL_PRIORITY_CLASS; break;
    case ProcessPriority::Normal:
    default:                           cls = NORMAL_PRIORITY_CLASS;       break;
    }
    if (!SetPriorityClass(GetCurrentProcess(), cls))
        qWarning() << "win_perf: SetPriorityClass failed, err" << GetLastError();
}

bool networkThrottleDisabled() {
    DWORD data = 0;
    DWORD size = sizeof(data);
    // HKLM read is allowed for a non-elevated process.
    const LSTATUS rc = RegGetValueW(HKEY_LOCAL_MACHINE, kThrottleSubkey,
                                    kThrottleValue, RRF_RT_REG_DWORD, nullptr,
                                    &data, &size);
    return rc == ERROR_SUCCESS && data == kThrottleDisabled;
}

bool setNetworkThrottleDisabled(bool disable) {
    // HKLM is machine-wide → needs elevation.  Rather than require Lyra to
    // run elevated, launch a one-shot elevated reg.exe (single UAC prompt).
    // The SystemProfile subkey always exists on Windows, so `reg add` just
    // overwrites the value.
    const DWORD value = disable ? kThrottleDisabled : kThrottleDefault;
    wchar_t params[512];
    // reg add "HKLM\...\SystemProfile" /v NetworkThrottlingIndex /t REG_DWORD /d 0x... /f
    swprintf(params, 512,
             L"add \"HKLM\\%ls\" /v %ls /t REG_DWORD /d %lu /f",
             kThrottleSubkey, kThrottleValue, static_cast<unsigned long>(value));

    SHELLEXECUTEINFOW sei{};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
    sei.lpVerb       = L"runas";     // triggers the UAC elevation prompt
    sei.lpFile       = L"reg.exe";
    sei.lpParameters = params;
    sei.nShow        = SW_HIDE;

    if (!ShellExecuteExW(&sei) || sei.hProcess == nullptr) {
        // User declined UAC, or the launch failed.
        qWarning() << "win_perf: elevated reg.exe launch failed / declined, err"
                   << GetLastError();
        return false;
    }
    WaitForSingleObject(sei.hProcess, 10000);   // reg.exe is near-instant
    DWORD exitCode = 1;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    CloseHandle(sei.hProcess);
    if (exitCode != 0) {
        qWarning() << "win_perf: reg.exe exited" << exitCode;
        return false;
    }
    return true;
}

#else  // !_WIN32 — inert stubs (project is Windows-only; keep callers building)

void applyProcessPriority(int) {}
bool networkThrottleDisabled() { return false; }
bool setNetworkThrottleDisabled(bool) { return false; }

#endif

} // namespace lyra::perf

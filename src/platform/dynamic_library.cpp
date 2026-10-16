// Lyra — cross-platform dynamic library loading.  See dynamic_library.h.

#include "platform/dynamic_library.h"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#include <QDir>
#include <QFileInfo>

#include <atomic>

namespace lyra::platform {

namespace {
// Set once by WdspNative::load(), read by resolve_wdsp_calls().  Atomic
// because the WDSP wisdom build runs on a worker thread and the call
// table can be resolved from a different thread than the loader.
std::atomic<ModuleHandle> g_wdspModule{nullptr};
}  // namespace

void setWdspModule(ModuleHandle module)
{
    g_wdspModule.store(module, std::memory_order_release);
}

ModuleHandle wdspModule()
{
    return g_wdspModule.load(std::memory_order_acquire);
}

#if defined(_WIN32)

namespace {

// Format a Win32 error code into a readable message.
QString formatWindowsError(DWORD code)
{
    wchar_t *buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);

    QString description;
    if (length && buffer) {
        description = QString::fromWCharArray(buffer, length).trimmed();
        ::LocalFree(buffer);
    }
    return description.isEmpty()
               ? QStringLiteral("Win32 error %1").arg(code)
               : QStringLiteral("Win32 error %1: %2").arg(code).arg(description);
}

}  // namespace

ModuleHandle loadLibrary(const QString &absolutePath, QString *error)
{
    const QString nativePath = QDir::toNativeSeparators(absolutePath);

    // LOAD_WITH_ALTERED_SEARCH_PATH makes the loader resolve the
    // library's own dependencies (FFTW, rnnoise, specbleach) from the
    // directory the library itself lives in.
    HMODULE module = ::LoadLibraryExW(
        reinterpret_cast<const wchar_t *>(nativePath.utf16()), nullptr,
        LOAD_WITH_ALTERED_SEARCH_PATH);

    if (!module) {
        if (error) *error = formatWindowsError(::GetLastError());
        return nullptr;
    }
    return static_cast<ModuleHandle>(module);
}

void *resolveSymbol(ModuleHandle module, const char *symbolName)
{
    if (!module || !symbolName) return nullptr;
    return reinterpret_cast<void *>(
        ::GetProcAddress(static_cast<HMODULE>(module), symbolName));
}

void unloadLibrary(ModuleHandle module)
{
    if (module) ::FreeLibrary(static_cast<HMODULE>(module));
}

QString wdspLibraryFileName() { return QStringLiteral("wdsp.dll"); }

#else  // POSIX

ModuleHandle loadLibrary(const QString &absolutePath, QString *error)
{
    // RTLD_NOW so every undefined symbol surfaces here, at a point where
    // we can report it, rather than as a crash on first DSP call.
    // RTLD_LOCAL keeps WDSP's symbols out of the global namespace.
    ::dlerror();  // clear any stale error
    void *module = ::dlopen(absolutePath.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
    if (!module) {
        const char *reason = ::dlerror();
        if (error) {
            *error = reason ? QString::fromUtf8(reason)
                            : QStringLiteral("dlopen failed for %1").arg(absolutePath);
        }
        return nullptr;
    }
    return static_cast<ModuleHandle>(module);
}

void *resolveSymbol(ModuleHandle module, const char *symbolName)
{
    if (!module || !symbolName) return nullptr;
    ::dlerror();
    return ::dlsym(module, symbolName);
}

void unloadLibrary(ModuleHandle module)
{
    if (module) ::dlclose(module);
}

QString wdspLibraryFileName()
{
#  if defined(__APPLE__)
    return QStringLiteral("libwdsp.dylib");
#  else
    return QStringLiteral("libwdsp.so");
#  endif
}

#endif

}  // namespace lyra::platform

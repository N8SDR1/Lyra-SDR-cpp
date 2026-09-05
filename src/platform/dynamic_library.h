// Lyra — cross-platform dynamic library loading.
//
// Lyra loads the WDSP DSP engine at runtime rather than linking it, so
// the engine can be replaced without rebuilding the app.  Two call sites
// need it: WdspNative::load() (src/wdsp_native.cpp) and
// resolve_wdsp_calls() (src/wire/wdspcalls.cpp).
//
// Windows    -> LoadLibraryExW / GetProcAddress / FreeLibrary
// macOS/Linux-> dlopen / dlsym / dlclose
//
// The platform difference that matters beyond the call names is how a
// library finds its own dependencies.  On Windows the loader is told to
// search the DLL's directory (LOAD_WITH_ALTERED_SEARCH_PATH).  On macOS
// libwdsp.dylib is built with `-install_name @rpath/libwdsp.dylib` and
// resolves FFTW through absolute Homebrew paths, so loading by absolute
// path is sufficient.

#pragma once

#include <QString>

namespace lyra::platform {

// Opaque module handle.  Null means "not loaded".
using ModuleHandle = void *;

// Load a shared library by absolute path.  Returns nullptr on failure and
// fills `error` with a human-readable reason.
ModuleHandle loadLibrary(const QString &absolutePath, QString *error);

// Resolve an exported symbol.  Returns nullptr if absent — callers decide
// whether that is fatal (wdsp_native.cpp) or merely logged (wdspcalls.cpp).
void *resolveSymbol(ModuleHandle module, const char *symbolName);

// Unload.  Safe to call with nullptr.
void unloadLibrary(ModuleHandle module);

// Platform-correct filename for the WDSP engine, so callers do not
// hardcode an extension: "wdsp.dll" on Windows, "libwdsp.dylib" on macOS,
// "libwdsp.so" on Linux.
QString wdspLibraryFileName();

// ---- the process-wide WDSP engine handle -------------------------------
//
// Two independent call sites resolve symbols out of the SAME loaded
// engine: WdspNative (src/wdsp_native.cpp), which owns its lifetime, and
// resolve_wdsp_calls() (src/wire/wdspcalls.cpp), which fills the ported
// ChannelMaster call table.
//
// On Windows the second site used GetModuleHandleW(L"wdsp.dll") to find
// the already-loaded DLL by name.  There is no portable equivalent —
// dlopen-by-name would search the loader paths and could map a DIFFERENT
// copy.  So the owner registers its handle here once, and the other site
// borrows it.  Borrowers must NOT unload it.
void         setWdspModule(ModuleHandle module);
ModuleHandle wdspModule();

}  // namespace lyra::platform

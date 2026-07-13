// Lyra — WDSP DLL loader implementation (Step 3a).  See wdsp_native.h
// for the locked architecture + scope.

#include "wdsp_native.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <thread>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QProcess>
#include <QProgressBar>
#include <QRadialGradient>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace lyra::dsp {

namespace {

// Format a Windows error code into a human-readable message via
// FormatMessageW — mirrors the helper in hl2_stream.cpp.
QString winError(DWORD code) {
    wchar_t *buf = nullptr;
    const DWORD len = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM     |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    QString descr;
    if (len && buf) {
        descr = QString::fromWCharArray(buf, len).trimmed();
        ::LocalFree(buf);
    }
    return descr.isEmpty()
        ? QStringLiteral("Win32 error %1").arg(code)
        : QStringLiteral("Win32 error %1: %2").arg(code).arg(descr);
}

// Branded art for the one-time-setup splash: the real Lyra logo (the
// Orpheus + golden lyre + HL2 icon) centred, with a handful of stars
// twinkling in the margins around it.  A `twinkle` value (0..1), driven
// a few times by a timer in the splash, pulses the star glow so it
// "flashes a couple times" then settles.  Plain QWidget (no Q_OBJECT /
// no moc): paintEvent is a virtual override, animation driven via
// setTwinkle().
class LyraSplashArt : public QWidget {
public:
    explicit LyraSplashArt(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        logo_ = QPixmap(QStringLiteral(
            ":/qt/qml/Lyra/src/assets/logo/lyra-icon-256.png"));
    }
    void setTwinkle(qreal t) { twinkle_ = t; update(); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const int w = width(), h = height();

        // Real logo, square, centred and scaled to the panel height.
        const int ls = h;
        const QRect logoRect((w - ls) / 2, 0, ls, ls);
        if (!logo_.isNull())
            p.drawPixmap(logoRect, logo_);

        // Twinkling stars scattered in the margins beside the logo.
        const qreal tw = twinkle_;
        static const QPointF frac[] = {
            {0.10, 0.24}, {0.05, 0.58}, {0.15, 0.82}, {0.21, 0.40},
            {0.90, 0.22}, {0.95, 0.56}, {0.85, 0.80}, {0.79, 0.38},
        };
        for (const auto &f : frac) {
            const QPointF c(f.x() * w, f.y() * h);
            const qreal r  = 1.6;
            const qreal gr = r * (2.0 + 2.5 * tw);
            QRadialGradient g(c, gr);
            QColor glow(200, 225, 255);
            glow.setAlphaF(0.22 + 0.45 * tw);
            g.setColorAt(0.0, glow);
            g.setColorAt(1.0, QColor(200, 225, 255, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(c, gr, gr);
            p.setBrush(QColor(226, 236, 255));
            p.drawEllipse(c, r, r);
        }
    }

private:
    QPixmap logo_;
    qreal   twinkle_ = 0.0;
};

} // namespace

WdspNative::WdspNative(QObject *parent) : QObject(parent) {}

WdspNative::~WdspNative() {
    unload();
}

bool WdspNative::load() {
    if (handle_ != nullptr) {
        return true;  // already loaded — idempotent
    }

    // Resolve `<exe-dir>/_native/`.  applicationDirPath() returns
    // the canonical native path on Windows so this is safe with
    // spaces / non-ASCII operator usernames.
    const QString exeDir   = QCoreApplication::applicationDirPath();
    const QString nativeDir = QDir::cleanPath(exeDir +
                              QStringLiteral("/_native"));
    const QString dllPath   = QDir::cleanPath(nativeDir +
                              QStringLiteral("/wdsp.dll"));

    if (!QFileInfo::exists(dllPath)) {
        loadError_ = QStringLiteral("wdsp.dll not found at %1")
                     .arg(dllPath);
        emitLog(QStringLiteral("[wdsp] LOAD FAILED: %1")
                .arg(loadError_));
        emit loadedChanged();
        return false;
    }

    // Add _native/ to the dynamic-link search path so wdsp.dll's
    // dependent DLLs (libfftw3-3.dll etc.) resolve from the same
    // directory.  The Python tree uses `os.add_dll_directory`;
    // C++ equivalent is `AddDllDirectory` (Win10+/Win8+) — it
    // APPENDS to the search path rather than replacing it (vs the
    // older `SetDllDirectory` which clobbers + has subtle side
    // effects).  Need LOAD_LIBRARY_SEARCH_USER_DIRS in the
    // LoadLibraryExW call for AddDllDirectory's entries to take
    // effect.  Operator is on Windows 11 so this path is supported.
    const std::wstring nativeDirW = nativeDir.toStdWString();
    DLL_DIRECTORY_COOKIE cookie =
        ::AddDllDirectory(nativeDirW.c_str());
    if (!cookie) {
        // Non-fatal — LoadLibraryExW may still find it via the
        // default search.  Log the issue and continue.
        const DWORD err = ::GetLastError();
        emit logLine(QStringLiteral(
            "[wdsp] AddDllDirectory failed (continuing): %1")
            .arg(winError(err)));
    }

    const std::wstring dllPathW = dllPath.toStdWString();
    HMODULE h = ::LoadLibraryExW(
        dllPathW.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
        LOAD_LIBRARY_SEARCH_USER_DIRS    |
        LOAD_LIBRARY_SEARCH_APPLICATION_DIR);

    if (cookie) {
        // We don't need the cookie to persist past LoadLibraryExW
        // — wdsp.dll's deps are resolved at this point.  Remove
        // to keep the search-path list clean.
        ::RemoveDllDirectory(cookie);
    }

    if (!h) {
        const DWORD err = ::GetLastError();
        loadError_ = winError(err);
        emitLog(QStringLiteral(
            "[wdsp] LOAD FAILED: LoadLibraryExW(%1): %2")
            .arg(dllPath, loadError_));
        emit loadedChanged();
        return false;
    }

    handle_     = static_cast<void*>(h);
    loadedFrom_ = dllPath;
    loadError_.clear();
    emitLog(QStringLiteral("[wdsp] LOADED: %1").arg(dllPath));

    // Step 3b: resolve the minimum WDSP entry points via
    // GetProcAddress.  On any miss we unload + return false so the
    // operator sees an explicit symbol-resolution failure rather
    // than a deferred crash at first use in Step 3c.
    if (!resolveSymbols()) {
        ::FreeLibrary(h);
        handle_ = nullptr;
        loadedFrom_.clear();
        emit loadedChanged();
        return false;
    }

    emit loadedChanged();
    return true;
}

void WdspNative::emitLog(const QString &line) {
    // Mirror every log line to both the QML log panel (via the
    // logLine signal) AND the host's stdout via qInfo() -- the
    // operator runs lyra.exe from a console specifically to
    // capture diagnostics, so a one-stop place to read every
    // status line is operator-friendly.  Cheap (one printf-like
    // call); no production concern.
    qInfo("%s", qPrintable(line));
    emit logLine(line);
}

void WdspNative::logWisdom(const QString &line) {
    // qWarning (NOT qInfo) so the in-app LogBuffer keeps the line even
    // when the operator has NOT enabled verbose logging -- a field
    // report of "rebuilds every launch" needs these with zero setup.
    qWarning("%s", qPrintable(line));
    emit logLine(line);

    // Also mirror to a dedicated wisdom.log right next to the cache dir,
    // so the one file a user sends is self-contained.  Appended across
    // launches (each line timestamped) so the every-launch pattern is
    // visible in one file.  Best-effort: never throws, never blocks the
    // caller on failure.  Capped so it can't grow unbounded.
    const QString dir  = wisdomDir();
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("wisdom.log"));
    QFile f(path);
    const QIODevice::OpenMode mode =
        (QFileInfo(path).size() > 256 * 1024)
            ? (QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)
            : (QIODevice::WriteOnly | QIODevice::Append   | QIODevice::Text);
    if (f.open(mode)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString(Qt::ISODate)
           << QStringLiteral("  ") << line << '\n';
    }
}

bool WdspNative::resolveSymbols() {
    HMODULE mod = static_cast<HMODULE>(handle_);
    if (!mod) return false;

    QStringList missing;
    int found = 0;
    int total = 0;

    // Wrapper resolves one symbol into the strongly-typed function
    // pointer, increments the running tallies, and records the
    // name on miss.  reinterpret_cast through FARPROC is the
    // standard Win32 idiom (the MS docs explicitly bless this for
    // GetProcAddress assignment to function-pointer types).
    auto resolve = [&](auto &fnPtr, const char *name) {
        ++total;
        using FnT = std::remove_reference_t<decltype(fnPtr)>;
        FARPROC p = ::GetProcAddress(mod, name);
        if (p) {
            fnPtr = reinterpret_cast<FnT>(p);
            ++found;
        } else {
            fnPtr = nullptr;
            missing << QString::fromLatin1(name);
        }
    };

    resolve(api_.OpenChannel,         "OpenChannel");
    resolve(api_.CloseChannel,        "CloseChannel");
    resolve(api_.SetChannelState,     "SetChannelState");
    resolve(api_.fexchange0,          "fexchange0");
    resolve(api_.SetRXAMode,          "SetRXAMode");
    resolve(api_.RXASetPassband,      "RXASetPassband");
    resolve(api_.SetRXAAGCMode,       "SetRXAAGCMode");
    resolve(api_.SetRXAPanelBinaural, "SetRXAPanelBinaural");
    resolve(api_.SetRXAShiftFreq,     "SetRXAShiftFreq");   // #174 CTUNE
    resolve(api_.SetRXAShiftRun,      "SetRXAShiftRun");    // #174 CTUNE
    resolve(api_.RXANBPSetShiftFrequency, "RXANBPSetShiftFrequency");  // #174 CTUNE notch track
    resolve(api_.RXASetMP,            "RXASetMP");           // #159 RX filter type
    resolve(api_.TXASetMP,            "TXASetMP");           // #159 TX filter type
    resolve(api_.WDSPwisdom,          "WDSPwisdom");
    // wisdom_get_status is OPTIONAL -- only used to show live "Planning FFT
    // size N" progress on the first-run splash.  Soft-resolve it directly so
    // a WDSP build that lacks it (older/other) still loads normally; it is
    // NOT counted against the required-symbol tally.
    if (FARPROC pWs = ::GetProcAddress(mod, "wisdom_get_status"))
        api_.wisdom_get_status =
            reinterpret_cast<fn_wisdom_get_status_t>(pWs);
    // WDSP impulse cache exports -- OPTIONAL (a wdsp.dll predating the
    // impulse cache still loads + runs; the feature just stays off).
    // Soft-resolve like wisdom_get_status, NOT the hard resolve() (which
    // counts toward the required-symbol tally).
    if (FARPROC p = ::GetProcAddress(mod, "init_impulse_cache"))
        api_.init_impulse_cache =
            reinterpret_cast<fn_init_impulse_cache_t>(p);
    if (FARPROC p = ::GetProcAddress(mod, "read_impulse_cache"))
        api_.read_impulse_cache =
            reinterpret_cast<fn_read_impulse_cache_t>(p);
    if (FARPROC p = ::GetProcAddress(mod, "save_impulse_cache"))
        api_.save_impulse_cache =
            reinterpret_cast<fn_save_impulse_cache_t>(p);
    if (FARPROC p = ::GetProcAddress(mod, "use_impulse_cache"))
        api_.use_impulse_cache =
            reinterpret_cast<fn_use_impulse_cache_t>(p);
    if (FARPROC p = ::GetProcAddress(mod, "destroy_impulse_cache"))
        api_.destroy_impulse_cache =
            reinterpret_cast<fn_destroy_impulse_cache_t>(p);
    resolve(api_.SetRXAAGCThresh,     "SetRXAAGCThresh");
    resolve(api_.SetRXAAGCSlope,      "SetRXAAGCSlope");
    resolve(api_.SetRXAPanelGain1,    "SetRXAPanelGain1");
    resolve(api_.GetRXAMeter,         "GetRXAMeter");
    resolve(api_.SetRXAEMNRRun,        "SetRXAEMNRRun");
    resolve(api_.SetRXAEMNRgainMethod, "SetRXAEMNRgainMethod");
    resolve(api_.SetRXAEMNRnpeMethod,  "SetRXAEMNRnpeMethod");
    resolve(api_.SetRXAEMNRaeRun,      "SetRXAEMNRaeRun");
    resolve(api_.SetRXAEMNRPosition,   "SetRXAEMNRPosition");
    resolve(api_.SetRXAEMNRpost2Run,   "SetRXAEMNRpost2Run");
    resolve(api_.SetRXAAGCDecay,         "SetRXAAGCDecay");
    resolve(api_.SetRXAAGCHang,          "SetRXAAGCHang");
    resolve(api_.SetRXAAGCHangThreshold, "SetRXAAGCHangThreshold");
    resolve(api_.SetRXAAGCFixed,         "SetRXAAGCFixed");
    resolve(api_.SetRXAANFRun,         "SetRXAANFRun");
    resolve(api_.SetRXAANFVals,        "SetRXAANFVals");
    resolve(api_.SetRXAANRRun,         "SetRXAANRRun");
    resolve(api_.SetRXAANRVals,        "SetRXAANRVals");
    resolve(api_.RXANBPAddNotch,       "RXANBPAddNotch");
    resolve(api_.RXANBPEditNotch,      "RXANBPEditNotch");
    resolve(api_.RXANBPDeleteNotch,    "RXANBPDeleteNotch");
    resolve(api_.RXANBPGetNumNotches,  "RXANBPGetNumNotches");
    resolve(api_.RXANBPSetNotchesRun,  "RXANBPSetNotchesRun");
    resolve(api_.SetRXASSQLRun,        "SetRXASSQLRun");
    resolve(api_.SetRXASSQLThreshold,  "SetRXASSQLThreshold");
    resolve(api_.SetRXASSQLTauMute,    "SetRXASSQLTauMute");
    resolve(api_.SetRXASSQLTauUnMute,  "SetRXASSQLTauUnMute");
    resolve(api_.SetRXAFMSQRun,        "SetRXAFMSQRun");
    resolve(api_.SetRXAFMSQThreshold,  "SetRXAFMSQThreshold");
    resolve(api_.SetRXAAMSQRun,        "SetRXAAMSQRun");
    resolve(api_.SetRXAAMSQThreshold,  "SetRXAAMSQThreshold");
    resolve(api_.SetRXAAMSQMaxTail,    "SetRXAAMSQMaxTail");
    resolve(api_.create_nobEXT,        "create_nobEXT");
    resolve(api_.destroy_nobEXT,       "destroy_nobEXT");
    resolve(api_.xnobEXT,              "xnobEXT");
    resolve(api_.SetEXTNOBRun,         "SetEXTNOBRun");
    resolve(api_.SetEXTNOBThreshold,   "SetEXTNOBThreshold");
    resolve(api_.SetRXABiQuadRun,        "SetRXABiQuadRun");
    resolve(api_.SetRXABiQuadFreq,       "SetRXABiQuadFreq");
    resolve(api_.SetRXABiQuadBandwidth,  "SetRXABiQuadBandwidth");
    resolve(api_.SetRXABiQuadGain,       "SetRXABiQuadGain");
    resolve(api_.XCreateAnalyzer,        "XCreateAnalyzer");
    resolve(api_.DestroyAnalyzer,        "DestroyAnalyzer");
    resolve(api_.SetAnalyzer,            "SetAnalyzer");
    resolve(api_.Spectrum0,              "Spectrum0");
    resolve(api_.GetPixels,              "GetPixels");
    resolve(api_.SetDisplayDetectorMode, "SetDisplayDetectorMode");
    resolve(api_.SetDisplayAverageMode,  "SetDisplayAverageMode");
    resolve(api_.SetDisplayNumAverage,   "SetDisplayNumAverage");
    resolve(api_.SetDisplayAvBackmult,   "SetDisplayAvBackmult");
    // TX-1 (design v2 §5.2): WDSP TXA surface.  See typedef block
    // header for the deliberately-omitted symbols (SetTXABandpassRun
    // §15.23 trap, SetTXAPanelSelect, SetTXAALCThresh-doesn't-exist).
    resolve(api_.SetTXAMode,           "SetTXAMode");
    resolve(api_.SetTXABandpassFreqs,  "SetTXABandpassFreqs");
    resolve(api_.SetTXAPHROTRun,       "SetTXAPHROTRun");
    resolve(api_.SetTXAPHROTCorner,    "SetTXAPHROTCorner");
    resolve(api_.SetTXAPHROTNstages,   "SetTXAPHROTNstages");
    resolve(api_.SetTXAALCAttack,      "SetTXAALCAttack");
    resolve(api_.SetTXAALCDecay,       "SetTXAALCDecay");
    resolve(api_.SetTXAALCHang,        "SetTXAALCHang");
    resolve(api_.SetTXAALCMaxGain,     "SetTXAALCMaxGain");
    resolve(api_.SetTXAALCSt,          "SetTXAALCSt");
    resolve(api_.SetTXALevelerAttack,  "SetTXALevelerAttack");
    resolve(api_.SetTXALevelerDecay,   "SetTXALevelerDecay");
    resolve(api_.SetTXALevelerHang,    "SetTXALevelerHang");
    resolve(api_.SetTXALevelerTop,     "SetTXALevelerTop");
    resolve(api_.SetTXALevelerSt,      "SetTXALevelerSt");
    resolve(api_.SetTXAPanelGain1,     "SetTXAPanelGain1");
    resolve(api_.GetTXAMeter,          "GetTXAMeter");
    resolve(api_.TXAGetaSipF1,         "TXAGetaSipF1");   // reserved
    resolve(api_.TXASetSipMode,        "TXASetSipMode");  // Task #44 Phase 2 (reference mechanism)
    resolve(api_.TXASetSipDisplay,     "TXASetSipDisplay");
    resolve(api_.SetDisplaySampleRate, "SetDisplaySampleRate");  // Task #44 Phase 2 (rate setter)
    resolve(api_.create_resampleFV,    "create_resampleFV");
    resolve(api_.xresampleFV,          "xresampleFV");
    resolve(api_.destroy_resampleFV,   "destroy_resampleFV");
    // Stage B (Thetis ChannelMaster port — AAMix) — complex-double
    // resampler.  Missing-symbol failure here is the Stage B.0 gate
    // signal: aamix per-input resampling cannot ship if the bundled
    // wdsp.dll does not export these.  (Source-verified
    // __declspec(dllexport) at wdsp/resample.h:60-70, so this should
    // resolve clean — but the linker is the bench, not the source.)
    resolve(api_.create_resample,      "create_resample");
    resolve(api_.destroy_resample,     "destroy_resample");
    resolve(api_.flush_resample,       "flush_resample");
    resolve(api_.xresample,            "xresample");

    if (!missing.isEmpty()) {
        loadError_ = QStringLiteral(
            "symbols resolved %1/%2 -- MISSING: %3")
            .arg(found).arg(total).arg(missing.join(QStringLiteral(", ")));
        emitLog(QStringLiteral("[wdsp] %1").arg(loadError_));
        return false;
    }

    emitLog(QStringLiteral("[wdsp] symbols: %1/%2 resolved")
            .arg(found).arg(total));
    return true;
}

void WdspNative::unload() {
    if (handle_ == nullptr) return;

    // WDSP impulse-cache save + teardown (reference DestroyDSP parity,
    // radio.cs 164-180) -- runs while the DLL is still loaded.  unload()
    // fires from ~WdspNative during QApplication destruction, i.e. AFTER
    // app.exec() returns and every aboutToQuit channel-close handler has
    // run, so no channel is computing filters concurrently.  save_ persists
    // the impulses computed this session so the next launch skips the
    // recompute; destroy_ frees the DLL-global cache + its critical
    // section before FreeLibrary.  Guarded by impulseCacheInited_ so a
    // never-inited / exports-absent load is a clean no-op.
    if (impulseCacheInited_ && api_.save_impulse_cache) {
        const QString file = QDir::cleanPath(
            wisdomDir() + QStringLiteral("/impulse_cache.dat"));
        const QByteArray fileBytes =
            QDir::toNativeSeparators(file).toLocal8Bit();
        const int rc = api_.save_impulse_cache(fileBytes.constData());
        logWisdom(QStringLiteral(
            "[wdsp] impulse-cache: save_impulse_cache(%1) rc=%2 "
            "(rc=0 saved)").arg(file).arg(rc));
    }
    if (impulseCacheInited_ && api_.destroy_impulse_cache)
        api_.destroy_impulse_cache();
    impulseCacheInited_ = false;

    ::FreeLibrary(static_cast<HMODULE>(handle_));
    handle_ = nullptr;
    loadedFrom_.clear();
    loadError_.clear();
    emit loadedChanged();
}

// ---------------------------------------------------------------
// Step 3c-i: FFTW WISDOM plumbing.
// ---------------------------------------------------------------

QString WdspNative::wisdomDir() {
    // Lyra-C++-PRIVATE directory.  Qt's GenericDataLocation
    // resolves to %APPDATA%\ on Windows; we then carve out our
    // own "N8SDR/Lyra-cpp/fftw/" subdir so we share with NEITHER
    // Python Lyra (which uses .../N8SDR/Lyra/fftw/) NOR any other
    // HPSDR app.  Isolation-by-directory per CLAUDE.md §15.26
    // (the wisdom-file format isn't versioned; different WDSP
    // builds can produce mutually-incompatible cached plans, so
    // cross-app sharing is a hidden footgun we explicitly avoid).
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(base +
        QStringLiteral("/N8SDR/Lyra-cpp/fftw"));
}

namespace {

// Hard-coded filename per WDSP source (wisdom.c) — same name
// every HPSDR app produces; isolation is purely by directory.
constexpr const char *kWisdomFilename = "wdspWisdom00";

bool wisdomFileExists(const QString &dir) {
    return QFileInfo::exists(QDir(dir).filePath(
        QString::fromLatin1(kWisdomFilename)));
}

}  // namespace

QString WdspNative::wisdomFilePath() {
    return QDir(wisdomDir()).filePath(QString::fromLatin1(kWisdomFilename));
}

bool WdspNative::wisdomExists() {
    return wisdomFileExists(wisdomDir());
}

bool WdspNative::deleteWisdom() {
    const QString path = wisdomFilePath();
    if (!QFileInfo::exists(path))
        return true;                 // already gone — next launch rebuilds
    return QFile::remove(path);
}

int WdspNative::runWisdomCall(const QString &callDir,
                              bool showModalImmediately) {
    // Call api_.WDSPwisdom(<callDir>/) on a BACKGROUND thread while the
    // GUI stays responsive.  WDSPwisdom imports an existing cache in
    // <100 ms (returns 0), or -- when the file is missing or REJECTED by
    // FFTW -- runs a multi-minute FFTW_PATIENT search + writes the file
    // (returns 1).  On that rebuild branch WDSP internally does
    // AllocConsole() + freopen(CONOUT$) + FreeConsole(); on our windowed
    // (WIN32_EXECUTABLE) build that would pop a visible console window
    // and leave CRT stdout dangling.  We tame it: pre-own a HIDDEN
    // console so WDSP's AllocConsole no-ops and its output goes nowhere
    // visible, then restore stdout/stderr afterward.  This is the
    // Thetis in-process model (radio.cs:135 calls WDSPwisdom directly);
    // building on a worker + a modal-if-slow keeps the UI responsive,
    // which the reference does not, and -- crucially -- there is NO
    // self-spawned child (the old `lyra.exe --build-wisdom` pattern trips
    // antivirus SONAR: an unsigned exe launching a copy of itself).
    //
    // Returns the WDSPwisdom return code (0=import, 1=rebuilt).
    QString dirArg = callDir;
    if (!dirArg.endsWith(QLatin1Char('/')) &&
        !dirArg.endsWith(QLatin1Char('\\'))) {
        dirArg += QLatin1Char('/');
    }
    const QByteArray dirBytes =
        QDir::toNativeSeparators(dirArg).toLocal8Bit();

    std::atomic<int>  rc{0};
    std::atomic<bool> done{false};
    std::thread worker([this, dirBytes, &rc, &done]() {
        // Tame WDSP's console ONLY if we don't already own one (a dev
        // console build must keep its own console).  Import-only calls
        // never touch the console, so this whole block is a no-op on the
        // fast path -- it bites only when WDSP actually rebuilds.
        const bool weOwnConsole = (::GetConsoleWindow() == nullptr);
        if (weOwnConsole && ::AllocConsole()) {
            if (HWND cw = ::GetConsoleWindow())
                ::ShowWindow(cw, SW_HIDE);
        }
        rc.store(api_.WDSPwisdom(
            const_cast<char *>(dirBytes.constData())));
        if (weOwnConsole) {
            // WDSP already called FreeConsole on its rebuild branch;
            // make sure nothing lingers and restore stdout/stderr (WDSP's
            // freopen(CONOUT$) left them on a now-dead console handle).
            // Lyra logs via Qt, not CRT stdout, so this is defensive but
            // keeps the CRT sane for anything that might touch it.
            if (::GetConsoleWindow()) ::FreeConsole();
            FILE *f = nullptr;
            ::freopen_s(&f, "NUL", "w", stdout);
            ::freopen_s(&f, "NUL", "w", stderr);
        }
        done.store(true);
    });

    // Main thread: pump events; show a modal notice ONLY if the call
    // runs longer than the threshold (a rebuild), never for a fast
    // import.  No hard timeout -- an in-process worker can't be safely
    // killed, and a genuine FFTW hang would wedge either way; the UI
    // stays responsive so the operator can always quit.
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (done.load()) loop.quit();
    });
    poll.start();

    QDialog *dlg = nullptr;
    auto showDialog = [&]() {
        if (done.load() || dlg) return;
        if (!qobject_cast<QApplication *>(QCoreApplication::instance()))
            return;

        // Community / help links only -- deliberately NO donate prompt on
        // a first-run setup screen (don't nag before the user has even
        // seen Lyra).  GitHub matches the About dialog.
        const QString kDiscordUrl =
            QStringLiteral("https://discord.gg/BwjsQvjcSc");
        const QString kGithubUrl  =
            QStringLiteral("https://github.com/N8SDR1/Lyra-SDR-cpp");

        dlg = new QDialog(nullptr, Qt::Dialog | Qt::CustomizeWindowHint |
                                       Qt::WindowTitleHint);
        dlg->setWindowTitle(
            QCoreApplication::translate("wdsp", "Lyra — one-time setup"));
        dlg->setModal(true);
        dlg->setMinimumSize(640, 420);

        auto *v = new QVBoxLayout(dlg);
        v->setContentsMargins(30, 18, 30, 20);
        v->setSpacing(12);

        // Branded art: the real Lyra logo with stars twinkling around it,
        // flashed a few times by the twinkle timer below.
        auto *art = new LyraSplashArt(dlg);
        art->setFixedHeight(168);
        v->addWidget(art);

        // Rotating 'while you wait' billboard.  Fixed min height so the
        // dialog doesn't jump as cards change.
        auto *card = new QLabel(dlg);
        card->setWordWrap(true);
        card->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        card->setMinimumHeight(96);
        card->setStyleSheet(QStringLiteral("font-size:14px;"));
        v->addWidget(card, 1);

        // Blue indeterminate 'scrolling' bar.
        auto *bar = new QProgressBar(dlg);
        bar->setRange(0, 0);
        bar->setTextVisible(false);
        bar->setFixedHeight(8);
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar{border:none;border-radius:4px;"
            "background:rgba(128,128,128,60);}"
            "QProgressBar::chunk{border-radius:4px;background:#2f81f7;}"));
        v->addWidget(bar);

        // Live "Planning … FFT size N" progress, read straight from WDSP's
        // wisdom_get_status() -- the SAME string Thetis prints to its
        // console.  Shows real progress under the rotating tips so the
        // operator sees the build genuinely advancing (reassuring on slow
        // hardware where it can run many minutes).  Optional: only wired if
        // the DLL exported the symbol (soft-resolved in resolveSymbols()).
        auto *prog = new QLabel(dlg);
        prog->setAlignment(Qt::AlignHCenter);
        prog->setStyleSheet(QStringLiteral(
            "font-family:Consolas,'Cascadia Mono',monospace;"
            "font-size:11px; color:#7fd7ff;"));
        v->addWidget(prog);
        if (api_.wisdom_get_status) {
            auto *progTimer = new QTimer(dlg);
            progTimer->setInterval(200);
            QObject::connect(progTimer, &QTimer::timeout, prog,
                             [this, prog]() {
                if (const char *s = api_.wisdom_get_status()) {
                    const QString t = QString::fromLatin1(s).simplified();
                    if (!t.isEmpty()) prog->setText(t);
                }
            });
            progTimer->start();
        }

        auto *status = new QLabel(QCoreApplication::translate("wdsp",
            "<b>Optimizing FFT plans — one-time setup.</b>  Lyra is "
            "<b>not ready yet</b>; it opens automatically when this "
            "finishes."), dlg);
        status->setWordWrap(true);
        status->setAlignment(Qt::AlignHCenter);
        status->setStyleSheet(QStringLiteral("font-size:11px;"));
        v->addWidget(status);

        // The important 'do not close' line -- bigger, and its colour
        // pulses red -> purple -> blue -> white to draw the eye.
        auto *important = new QLabel(QCoreApplication::translate("wdsp",
            "⚠  IMPORTANT — please do not close anything."), dlg);
        important->setAlignment(Qt::AlignHCenter);
        important->setStyleSheet(QStringLiteral(
            "font-size:15px; font-weight:800;"));
        v->addWidget(important);
        auto *pulse = new QTimer(dlg);
        pulse->setInterval(50);
        QObject::connect(pulse, &QTimer::timeout, important,
                         [important, phase = 0.0]() mutable {
            static const QColor stops[] = {
                QColor(0xFF, 0x3B, 0x30),   // red
                QColor(0xB2, 0x4B, 0xF3),   // purple
                QColor(0x2F, 0x81, 0xF7),   // blue
                QColor(0xFF, 0xFF, 0xFF),   // white
            };
            phase += 0.035;
            if (phase >= 4.0) phase -= 4.0;
            const int seg   = int(phase) % 4;
            const qreal f   = phase - int(phase);
            const QColor &a = stops[seg];
            const QColor &b = stops[(seg + 1) % 4];
            const int r  = int(a.red()   + (b.red()   - a.red())   * f);
            const int gg = int(a.green() + (b.green() - a.green()) * f);
            const int bl = int(a.blue()  + (b.blue()  - a.blue())  * f);
            important->setStyleSheet(QStringLiteral(
                "font-size:15px; font-weight:800; color:rgb(%1,%2,%3);")
                .arg(r).arg(gg).arg(bl));
        });
        pulse->start();

        // Persistent help links (always clickable, unlike the cycling
        // cards).  This one-time splash is seen ONLY on first launch -- exactly
        // the new-operator audience -- so lead with a "New to Lyra?" link to
        // the Getting-Started guide.  It opens the bundled PDF if present
        // (offline, version-matched); otherwise it falls back to the copy on
        // GitHub.  Discord/GitHub open in the browser.
        const QString kQuickStart = QStringLiteral("lyra:quickstart");
        const QString kQuickStartWeb = QStringLiteral(
            "https://github.com/N8SDR1/Lyra-SDR-cpp/blob/main/docs/help/"
            "GETTING_STARTED.md");
        QStringList links;
        links << QStringLiteral("<a href='%1'><b>New to Lyra?</b></a>")
                     .arg(kQuickStart);
        links << QStringLiteral("<a href='%1'>Discord</a>").arg(kDiscordUrl);
        links << QStringLiteral("<a href='%1'>GitHub</a>").arg(kGithubUrl);
        auto *help = new QLabel(
            QCoreApplication::translate("wdsp", "Help &amp; more:  ")
                + links.join(QStringLiteral("  &nbsp;·&nbsp;  ")), dlg);
        help->setAlignment(Qt::AlignHCenter);
        help->setTextInteractionFlags(Qt::TextBrowserInteraction);
        help->setOpenExternalLinks(false);   // handled below so we can route
                                              // the quickstart to a local PDF
        help->setStyleSheet(QStringLiteral("font-size:11px;"));
        QObject::connect(help, &QLabel::linkActivated, help,
                         [kQuickStart, kQuickStartWeb](const QString &href) {
            if (href == kQuickStart) {
                const QString pdf =
                    QDir(QCoreApplication::applicationDirPath())
                        .filePath(QStringLiteral("docs/Lyra-Getting-Started.pdf"));
                if (QFileInfo::exists(pdf)) {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(pdf));
                    return;
                }
                QDesktopServices::openUrl(QUrl(kQuickStartWeb));
                return;
            }
            QDesktopServices::openUrl(QUrl(href));
        });
        v->addWidget(help);

        // Billboard content cycled by the timer below.
        static const QStringList cards = {
            QCoreApplication::translate("wdsp",
                "<b>Welcome to Lyra</b><br>A modern, Vulkan-accelerated "
                "SDR transceiver for the Hermes&nbsp;Lite&nbsp;2 / 2+."),
            QCoreApplication::translate("wdsp",
                "<b>Tip · Tuning</b><br>Click the panadapter to jump there, "
                "drag the passband edges to set your filter, and "
                "right-click to drop a notch."),
            QCoreApplication::translate("wdsp",
                "<b>Did you know?</b><br>Lyra decodes CW on-screen — open "
                "the <i>CW&nbsp;Dec</i> panel and it prints what it hears."),
            QCoreApplication::translate("wdsp",
                "<b>Tip · CW</b><br>Build click-to-send CW macros in the CW "
                "console — {CALL}, {RST} and {NAME} fill themselves in."),
            QCoreApplication::translate("wdsp",
                "<b>Did you know?</b><br>You can save and share whole "
                "screen layouts: Settings → Backup&nbsp;&amp;&nbsp;Restore "
                "→ Share&nbsp;a&nbsp;layout."),
            QCoreApplication::translate("wdsp",
                "<b>Tip · DX spots</b><br>Turn on Spots to see cluster / RBN "
                "spots right on the panadapter — click one to tune it."),
            QCoreApplication::translate("wdsp",
                "<b>Did you know?</b><br>Lyra has a full TX rack — "
                "parametric EQ, a multiband combinator, tube-plate reverb "
                "and a voice keyer."),
            QCoreApplication::translate("wdsp",
                "<b>Tip · Profiles</b><br>Save your whole TX chain as a "
                "profile and switch it in one click — it can even launch "
                "your logger with it."),
            QCoreApplication::translate("wdsp",
                "<b>Did you know?</b><br>With CTUN you drag the tuning "
                "marker onto a signal while the radio's centre stays put."),
            QCoreApplication::translate("wdsp",
                "<b>Tip · Noise</b><br>Stack NR, the noise blanker, ANF and "
                "the auto-notch to dig weak signals out of the noise."),
            QCoreApplication::translate("wdsp",
                "<b>Did you know?</b><br>The Tuner panel remembers your "
                "manual ATU settings per band and per antenna."),
            QCoreApplication::translate("wdsp",
                "<b>Did you know?</b><br>Route audio to WSJT-X / FLDigi with "
                "the built-in VAC, or drive Lyra over TCI from your logger."),
            QCoreApplication::translate("wdsp",
                "<b>Tip · Calibrate</b><br>Zero-beat your radio against WWV "
                "with the built-in frequency-calibration tool."),
            QCoreApplication::translate("wdsp",
                "<b>Did you know?</b><br>Capture a few seconds of your band "
                "noise, then subtract it — Lyra's captured-noise profile "
                "pulls signals out of a noisy floor."),
            QCoreApplication::translate("wdsp",
                "<b>Tip · Logging</b><br>Pair Lyra with SDRLogger+ over the "
                "Combo link — spots land on your panadapter and a CW macro "
                "can log the QSO for you."),
            QCoreApplication::translate("wdsp",
                "<b>Almost ready…</b><br>This one-time step tunes the DSP "
                "math for <i>your</i> CPU, so every launch after this is "
                "quick."),
        };
        card->setText(cards.at(0));
        auto *cycle = new QTimer(dlg);
        cycle->setInterval(6500);
        QObject::connect(cycle, &QTimer::timeout, card,
                         [card, i = 0]() mutable {
                             i = (i + 1) % int(cards.size());
                             card->setText(cards.at(i));
                         });
        cycle->start();

        // Flash the constellation / harp a few times, then settle to a
        // gentle steady glow for the rest of the wait.
        auto *twk = new QTimer(dlg);
        twk->setInterval(33);   // ~30 fps
        QObject::connect(twk, &QTimer::timeout, art,
                         [art, twk, t = 0.0, dir = 1.0, flashes = 0]() mutable {
                             t += dir * 0.06;
                             if (t >= 1.0) { t = 1.0; dir = -1.0; }
                             else if (t <= 0.0) {
                                 t = 0.0; dir = 1.0;
                                 if (++flashes >= 3) {
                                     art->setTwinkle(0.4);   // steady rest glow
                                     twk->stop();
                                     return;
                                 }
                             }
                             art->setTwinkle(t);
                         });
        twk->start();

        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    };

    QTimer modalTimer;
    modalTimer.setSingleShot(true);
    QObject::connect(&modalTimer, &QTimer::timeout, &loop,
                     [&]() { showDialog(); });
    if (showModalImmediately) {
        // Known build: the caller has deferred the main window, so show
        // the notice at once -- there is no ready-looking Lyra to sit
        // behind, and no gap where the screen is blank.
        showDialog();
    } else {
        modalTimer.start(500);   // import: notice only if it unexpectedly rebuilds
    }

    loop.exec();             // responsive; quits when the worker finishes

    if (dlg) { dlg->close(); dlg->deleteLater(); }
    worker.join();
    return rc.load();
}

bool WdspNative::ensureWisdom() {
    if (!isLoaded()) {
        logWisdom(QStringLiteral(
            "[wdsp] wisdom: cannot ensure — DLL not loaded"));
        return false;
    }
    if (!api_.WDSPwisdom) {
        logWisdom(QStringLiteral(
            "[wdsp] wisdom: cannot ensure — WDSPwisdom symbol "
            "not resolved"));
        return false;
    }

    const QString dir = wisdomDir();
    QDir().mkpath(dir);

    // ---- Fast path: cache exists -> import in place. ----
    // Runs through runWisdomCall (worker thread + console taming) so
    // that even if the cached file is REJECTED by FFTW and WDSP silently
    // re-plans, it neither pops a console window nor freezes the GUI
    // thread; the 'please wait' notice appears only if that rebuild
    // actually happens (a clean import is sub-100 ms).
    if (wisdomFileExists(dir)) {
        logWisdom(QStringLiteral(
            "[wdsp] wisdom: loading cached plans from %1").arg(dir));
        QElapsedTimer t; t.start();
        const int rc = runWisdomCall(dir);
        const bool rebuilt = (rc == 1);
        if (rc == 1) {
            // WDSPwisdom returns 1 ONLY when it rebuilt the plans --
            // i.e. the cached file was present but REJECTED by FFTW
            // (stale after a wdsp.dll/FFTW bump, wrong-CPU, or corrupt)
            // and silently re-planned IN-PROCESS.  Should be rare (the
            // atomic publish means a truncated file can't linger); log it
            // so a "rebuilds every launch" report on an existing file is
            // unambiguous rather than looking like a normal load.  WDSP
            // re-exports the file itself on this branch, so it self-heals.
            logWisdom(QStringLiteral(
                "[wdsp] wisdom: cached file at %1 was REJECTED by FFTW "
                "and re-planned in-process in %2 ms -- the cache is "
                "stale/incompatible").arg(dir).arg(t.elapsed()));
        } else {
            logWisdom(QStringLiteral(
                "[wdsp] wisdom: loaded cached plans in %1 ms (rc=%2)")
                .arg(t.elapsed()).arg(rc));
        }
        initImpulseCache(dir, rebuilt);
        return true;
    }

    // ---- Slow path: no cache -> build IN PLACE, exactly like Thetis. ----
    // Thetis (radio.cs CreateDSP) calls WDSPwisdom(finalDir) directly --
    // WDSP writes wdspWisdom00 straight into the cache folder, no temp
    // dir, no rename.  We mirror that: build directly into `dir`.  An
    // earlier version built into a QTemporaryDir + MoveFileExW'd it into
    // place for "atomicity", but that extra file op is (a) something
    // Thetis does NOT do and (b) a second write/rename on %LOCALAPPDATA%
    // that a folder-shield / antivirus can block AFTER the plan succeeds
    // -- leaving the final wdspWisdom00 absent so every launch rebuilds.
    // Thetis proves the atomicity is unnecessary: if a build is
    // interrupted, the partial wdspWisdom00 is simply re-rejected by FFTW
    // on the next launch and rebuilt -- the SAME self-correcting path as a
    // missing file.  So: write in place, one operation, like Thetis.
    logWisdom(QStringLiteral(
        "[wdsp] wisdom: building IN PLACE (one-time, may take several "
        "minutes; a 'please wait' notice shows and Lyra stays "
        "responsive) -- writing directly to %1, Thetis-style").arg(dir));

    QElapsedTimer t; t.start();
    // Build the plans IN-PROCESS on a worker thread (no self-spawned
    // child -- that pattern trips antivirus SONAR: an unsigned exe
    // launching a copy of itself).  WDSPwisdom writes wdspWisdom00 into
    // `dir` AND leaves the freshly-planned wisdom live in FFTW's
    // process-global state, so once this returns the running process is
    // ready -- NO separate re-import needed.  runWisdomCall keeps the GUI
    // responsive + tames WDSP's AllocConsole.  Show the modal immediately:
    // main.cpp has deferred the main window for this build, so there is
    // nothing to sit behind and no blank gap.
    const int rc = runWisdomCall(dir, /*showModalImmediately=*/true);

    // Verify the file landed (Thetis just trusts the write; we log so a
    // "rebuilds every launch" report has an unambiguous cause).  A missing
    // / empty file here = the write itself was blocked (folder shield /
    // permissions) -- the ONE thing an antivirus can still do, but now
    // there is no separate rename step to also block.
    const QString finalFile = wisdomFilePath();
    const QFileInfo finalInfo(finalFile);
    if (!finalInfo.exists() || finalInfo.size() == 0) {
        logWisdom(QStringLiteral(
            "[wdsp] wisdom: BUILD FAILED — no usable wisdom file at %1 "
            "after WDSPwisdom (rc=%2) -- the write was blocked "
            "(antivirus folder shield / permissions?)")
            .arg(finalFile).arg(rc));
        return false;
    }
    logWisdom(QStringLiteral(
        "[wdsp] wisdom: built in %1 s (rc=%2), %3 bytes written to %4")
        .arg(t.elapsed() / 1000).arg(rc)
        .arg(finalInfo.size()).arg(finalFile));
    // Wisdom was just built from scratch -> any pre-existing
    // impulse_cache.dat is stale (hashed against the old plan world);
    // treat as rebuilt so it is deleted + the read skipped.
    initImpulseCache(dir, /*wisdomRebuilt=*/true);
    return true;
}

// ---------------------------------------------------------------
// WDSP impulse cache init (reference radio.cs CreateDSP 143-158).
// ---------------------------------------------------------------
// Runs inside ensureWisdom() -- AFTER the WDSPwisdom call, BEFORE the
// first OpenChannel (main.cpp: ensureWisdom() precedes openRx1() +
// create_xmtr).  This is the exact ordering the reference uses:
// WDSPwisdom -> [delete stale cache if rebuilt] -> init_impulse_cache
// -> [read cache unless rebuilt] -> create channels.
void WdspNative::initImpulseCache(const QString &dir, bool wisdomRebuilt) {
    if (!api_.init_impulse_cache) {
        logWisdom(QStringLiteral(
            "[wdsp] impulse-cache: exports absent in this wdsp.dll -- "
            "skipping (filters recompute per launch; harmless)"));
        return;
    }

    const QString file =
        QDir::cleanPath(dir + QStringLiteral("/impulse_cache.dat"));
    const QByteArray fileBytes =
        QDir::toNativeSeparators(file).toLocal8Bit();

    // If the FFT wisdom was (re)built this launch, any existing cache is
    // stale -- the cached impulses were hashed against the old plan
    // world.  Delete it and skip the read (reference: radio.cs 137-146).
    if (wisdomRebuilt) {
        if (QFile::exists(file) && QFile::remove(file)) {
            logWisdom(QStringLiteral(
                "[wdsp] impulse-cache: wisdom rebuilt -> deleted stale %1")
                .arg(file));
        }
    }

    // Default-on, no operator toggle for now (use = 1).  A future
    // Settings switch would call api_.use_impulse_cache(0/1) live.
    api_.init_impulse_cache(1);
    impulseCacheInited_ = true;

    if (!wisdomRebuilt && api_.read_impulse_cache) {
        const int rc = api_.read_impulse_cache(fileBytes.constData());
        logWisdom(QStringLiteral(
            "[wdsp] impulse-cache: init(1) + read(%1) rc=%2 "
            "(0=loaded; -1=absent/incompatible -> filters recompute + "
            "repopulate, saved at exit)").arg(file).arg(rc));
    } else {
        logWisdom(QStringLiteral(
            "[wdsp] impulse-cache: init(1); read skipped "
            "(wisdom rebuilt -> starting fresh, saved at exit)"));
    }
}

} // namespace lyra::dsp

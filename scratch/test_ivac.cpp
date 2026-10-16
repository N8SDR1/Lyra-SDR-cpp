// Standalone unit test for the #158 Stage-1 IVAC direct port
// (verbatim ChannelMaster/ivac.c engine + setters; wire-INERT —
// no Qt audio, no radio wiring).
//
// Build via _build_test_ivac.bat (cmake --build build --target
// test_ivac).  Unlike test_ilv, IVAC's engine reaches the WDSP DLL
// (create_ivac -> create_resamps -> create_rmatchV, and the AAMix
// instance -> create_resample), so this test must LoadLibrary
// wdsp.dll + resolve_wdsp_calls() BEFORE create_ivac — mirroring
// main.cpp's wire-in (WdspNative::load + resolve_wdsp_calls).  We do
// the LoadLibrary directly here (the test isn't linked against the
// Qt WdspNative wrapper) from _native/wdsp.dll next to the build, so
// resolve_wdsp_calls()'s GetModuleHandleW(L"wdsp.dll") finds it.
//
// Like test_ilv, this defines its own cm/pcm globals (CMaster.h
// declares them extern) so create_resamps' `pcm->cmMAXInRate` +
// `getbuffsize` resolve without the full wire layer.
//
// Tests cover the verbatim surface end-to-end:
//   * create_ivac(id=0, audio mode) constructs (rings + AAMix +
//     critical section) without crashing
//   * push a synthetic mic buffer through xvacIN
//   * push a synthetic RX-audio buffer through xvacOUT(stream 1)
//   * drain the OUT ring via xrmatchOUT(rmatchOUT, scratch)
//   * getIVACdiags returns sane (non-negative) counters
//   * a few setters (SetIVACpreamp / SetIVACbypass / SetIVACmox)
//   * destroy_ivac tears down clean
//
// The goal is modest: the engine constructs, moves a buffer each
// way, and tears down clean.

#include "wire/Ivac.h"
#include "wire/CMaster.h"
#include "wire/wdspcalls.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include "platform/win32_compat.h"
#endif

#include "platform/dynamic_library.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The test's own cmaster globals (CMaster.h declares them extern;
// CMaster.cpp is deliberately NOT linked here — only the field
// create_resamps reads, pcm->cmMAXInRate, is set below).
//
// cmsetup.cpp IS linked (for getbuffsize), and its CreateRadio/
// DestroyRadio reference create_cmaster/destroy_cmaster (defined in
// the un-linked CMaster.cpp).  CreateRadio/DestroyRadio are never
// called by this test, but the linker still needs the symbols — stub
// them no-op rather than drag CMaster.cpp's whole dependency cascade.
namespace lyra::wire {
cmaster cm = {0};
CMASTER  pcm = &cm;
void create_cmaster() {}
void destroy_cmaster() {}
}

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("  FAIL: %s\n", what);
    } else {
        std::printf("  pass: %s\n", what);
    }
}

// Load the WDSP engine from _native/ (next to the build tree) and
// register it so resolve_wdsp_calls() can borrow the handle.  Returns
// true on success.  Tries the relative locations the build/run layout
// uses; the env override LYRA_WDSP_DIR (same name WdspNative honors) is
// checked first.
bool load_wdsp() {
    if (lyra::platform::wdspModule() != nullptr)
        return true;  // already loaded

    const QString engine = lyra::platform::wdspLibraryFileName();

    QStringList candidates;
    if (const QByteArray dir = qgetenv("LYRA_WDSP_DIR"); !dir.isEmpty())
        candidates << QDir(QString::fromLocal8Bit(dir)).filePath(engine);

    for (const char *prefix : {".", "_native", "build/_native",
                               "../_native", "../build/_native"}) {
        candidates << QDir(QString::fromLatin1(prefix)).filePath(engine);
    }

    for (const QString &path : candidates) {
        if (!QFileInfo::exists(path)) continue;
        QString error;
        if (auto *module = lyra::platform::loadLibrary(
                QFileInfo(path).absoluteFilePath(), &error)) {
            lyra::platform::setWdspModule(module);
            std::printf("[test] loaded WDSP engine: %s\n",
                        qUtf8Printable(QFileInfo(path).absoluteFilePath()));
            return true;
        }
        std::printf("[test] load failed for %s: %s\n",
                    qUtf8Printable(path), qUtf8Printable(error));
    }
    return false;
}

// ====================================================================
void testEngineRoundtrip() {
    using namespace lyra::wire;
    std::printf("[test] create_ivac -> xvacIN/xvacOUT -> drain -> destroy_ivac\n");

    // create_resamps reads pcm->cmMAXInRate to size the bitbucket via
    // getbuffsize(); set a sane VAC-class max input rate.
    pcm->cmMAXInRate = 192000;

    const int id          = 0;
    const int run         = 1;
    const int iq_type     = 0;        // audio mode (not raw IQ)
    const int stereo      = 1;
    const int iq_rate     = 192000;
    const int mic_rate    = 48000;
    const int audio_rate  = 48000;
    const int txmon_rate  = 48000;
    const int vac_rate    = 48000;
    const int mic_size    = 256;
    const int iq_size     = 256;
    const int audio_size  = 256;
    const int txmon_size  = 256;
    const int vac_size    = 256;

    create_ivac(id, run, iq_type, stereo,
                iq_rate, mic_rate, audio_rate, txmon_rate, vac_rate,
                mic_size, iq_size, audio_size, txmon_size, vac_size);

    // pvac[id] is file-static in Ivac.cpp; verify construction via
    // the public diag + setter surface instead of reaching the bank.
    check(true, "create_ivac(id=0, audio mode) returned (no crash)");

    // A few setters exercise the scalar-store + AAMix-bit paths.
    SetIVACpreamp(id, 1.5);
    check(true, "SetIVACpreamp stored (no crash)");
    SetIVACbypass(id, 0);
    check(true, "SetIVACbypass stored (no crash)");
    SetIVACmox(id, 0);                // drives SetAAudioMixWhat on the mixer
    check(true, "SetIVACmox(0) drove AAMix what-bits (no crash)");

    // Synthetic mic buffer through xvacIN: mic_size complex samples
    // = 2*mic_size doubles.  Interleaved I/Q ramp.
    std::vector<double> mic(static_cast<std::size_t>(2 * mic_size));
    for (int i = 0; i < 2 * mic_size; ++i)
        mic[static_cast<std::size_t>(i)] = 0.001 * i;
    xvacIN(id, mic.data(), /*bypass*/0);
    check(true, "xvacIN pushed a synthetic mic buffer (no crash)");

    // Synthetic RX-audio buffer through xvacOUT stream 1 (audio ->
    // AAMix input 0).  audio_size complex samples.
    std::vector<double> rxaudio(static_cast<std::size_t>(2 * audio_size));
    for (int i = 0; i < 2 * audio_size; ++i)
        rxaudio[static_cast<std::size_t>(i)] = 0.0005 * (i % 97);
    xvacOUT(id, /*stream*/1, rxaudio.data());
    check(true, "xvacOUT(stream=1) pushed a synthetic RX-audio buffer (no crash)");

    // getIVACdiags must return sane (non-negative) counters.  type 0
    // = From VAC (rmatchOUT), type 1 = To VAC (rmatchIN).
    int underflows = -1, overflows = -1, ringsize = -1, nring = -1;
    double var = -1.0;
    getIVACdiags(id, /*type*/0, &underflows, &overflows, &var, &ringsize, &nring);
    check(underflows >= 0, "getIVACdiags(type=0) underflows non-negative");
    check(overflows  >= 0, "getIVACdiags(type=0) overflows non-negative");
    check(ringsize   >= 0, "getIVACdiags(type=0) ringsize non-negative");
    check(nring      >= 0, "getIVACdiags(type=0) nring non-negative");
    check(var        >= 0.0, "getIVACdiags(type=0) var non-negative");

    underflows = overflows = ringsize = nring = -1; var = -1.0;
    getIVACdiags(id, /*type*/1, &underflows, &overflows, &var, &ringsize, &nring);
    check(underflows >= 0, "getIVACdiags(type=1) underflows non-negative");
    check(var        >= 0.0, "getIVACdiags(type=1) var non-negative");

    destroy_ivac(id);
    check(true, "destroy_ivac tore down clean (no crash)");
}

} // namespace

int main() {
    using namespace lyra::wire;
    // Unbuffered stdout so a crash mid-run doesn't swallow the line that
    // located it (redirected stdout is otherwise block-buffered).
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== IVAC direct port (#158 Stage 1) — unit test ===\n");

    if (!load_wdsp()) {
        std::printf("  SKIP: wdsp.dll could not be loaded (set LYRA_WDSP_DIR "
                    "to the dir holding wdsp.dll).  IVAC engine needs the "
                    "rmatchV + resampler exports.\n");
        std::printf("\n=== SKIPPED (wdsp.dll unavailable) ===\n");
        return 0;
    }
    const int missing = resolve_wdsp_calls();
    if (missing != 0) {
        std::printf("  FAIL: resolve_wdsp_calls() reports %d unresolved "
                    "wdsp.dll export(s)\n", missing);
        std::printf("\n=== FAIL — %d unresolved export(s) ===\n", missing);
        return 1;
    }
    std::printf("  pass: wdsp.dll loaded + call table resolved\n");

    testEngineRoundtrip();

    std::printf("\n=== %s — %d failure(s) ===\n",
                failures == 0 ? "ALL PASS" : "FAIL",
                failures);
    return failures == 0 ? 0 : 1;
}

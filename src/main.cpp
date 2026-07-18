// Lyra — Hermes Lite 2 / 2+ SDR transceiver (C++23 / Qt 6 rebuild).
//
// Step 1 entry point.  Opens a Qt Quick window backed by RHI
// (Vulkan/D3D12 on Windows; Metal on macOS; OpenGL fallback) and
// kicks off a C++ UDP discovery sweep on a dedicated worker
// QThread.  Discovery results stream into the window as they
// arrive.  No Python, no GIL, no in-process bottleneck on the
// wire path.

#include "hl2_discovery.h"
#include "hl2_stream.h"
#include "panadapter.h"
#include "waterfall.h"
#include "freqdisplay.h"
#include "wdsp_engine.h"
#include "mic_source.h"
#include "mainwindow.h"
#include "single_instance.h"   // one-instance guard (TCI-port / double-radio collision)
// TX-rip Phase 1 (Q2): tci_mic_source.h / tx_dsp_worker.h removed —
// the TX DSP subsystem is being rebuilt from empty files per
// docs/TX_ARCHITECTURAL_MAPPING.md §10.3.  TX wiring below collapses
// to a stub until the new components land.
#include "wdsp_native.h"
#include "wire/ILV.h"       // P0.b direct port — reference ilv.h verbatim (pcm->xmtr[].pilv)
#include "wire/CMaster.h"   // create_cmaster() / destroy_cmaster() / pcm
#include "tci/TciTxBridge.h"  // TCI TX-audio re-home bridge (sink + InboundTCITxAudio cb)
#include "eqmodel.h"          // #50 native TX parametric EQ — txProcessCb wire hook
#include "speechmodel.h"      // #88 native TX speech rack — txProcessCb wire hook
#include "combinatormodel.h"  // #51 native TX combinator — txProcessCb wire hook
#include "platemodel.h"       // #52 native TX plate reverb — txProcessCb wire hook
#include "WaterfallIdController.h"  // #175 waterfall-ID arm/cadence orchestrator
#include "wire/cmsetup.h"   // P0.d direct port — reference cmsetup.h verbatim
#include "wire/wdspcalls.h" // P0.a — WDSP call-table resolver (the one approved linkage seam)

// P0.d — consumer-side declarations for the PORT-exported
// ChannelMaster functions main.cpp calls.  The reference exports
// these via dllexport with NO header declaration (the C# console
// declares them consumer-side via DllImport); Lyra consumers
// re-declare them the same way (scratch/test_ilv.cpp precedent for
// the ILV PORT setters).  Definitions: wire/cmsetup.cpp
// (SetRadioStructure / set_cmdefault_rates, cmsetup.c:29-86) and
// wire/CMaster.cpp (create_xmtr / destroy_xmtr, cmaster.c:112-271).
namespace lyra::wire {
void SetRadioStructure (int cmSTREAM, int cmRCVR, int cmXMTR,
	int cmSubRCVR, int cmNspc, int* cmSPC, int* cmMAXInbound,
	int cmMAXInRate, int cmMAXAudioRate, int cmMAXTxOutRate);
void set_cmdefault_rates (int* xcm_inrates, int aud_outrate,
	int* rcvr_ch_outrates, int* xmtr_ch_outrates);
void create_xmtr();
void destroy_xmtr();
void create_rnet();   // P3 — once-per-process, AFTER create_xmtr (see QTimer block)
int  ivacInitPortAudio();    // #158 DL-1 — PortAudio process init (wire/Ivac.cpp)
void ivacTerminatePortAudio();// #158 DL-1 — PortAudio process shutdown (wire/Ivac.cpp)
}  // namespace lyra::wire

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#ifdef _WIN32
// WIN32_LEAN_AND_MEAN prevents windows.h pulling in winsock1, which
// would clash with winsock2.h (transitively included via the existing
// networking headers later in this file).  Need just TerminateProcess
// + GetCurrentProcess for the Task #40 zombie-shutdown watchdog.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {
// Task #40 — TX-triggered zombie shutdown watchdog.  Operator-flagged
// 2026-05-31: closing Lyra after using TX leaves the process running in
// the background (clean exit on RX-only sessions).  Until the root
// cause is identified + fixed properly, this watchdog stops the
// bleeding: a detached thread armed at aboutToQuit polls a
// "shutdown_complete" flag, and if main() hasn't returned within 10 s
// of aboutToQuit firing it calls TerminateProcess (Win32) / std::_Exit
// (fallback).  Aggressive — bypasses static destructors, leaks file
// handles + sockets back to the OS — but the OS reclaims those at
// process exit anyway, and the operator-visible symptom (process keeps
// running) is the bug we're solving.  The companion diagnostic
// [shutdown] qWarning lines flushed before this fires tell us in the
// next bench WHICH teardown step took >10 s, so the next commit can
// fix the actual wedge.  Once root cause is fixed, this watchdog stays
// as belt-and-suspenders against future regressions.
std::atomic<bool> g_shutdown_complete{false};
}  // namespace

#include <QCoreApplication>
#include <QGuiApplication>
#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QSettings>
#include <QString>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>
#include <QSurfaceFormat>

#include "mainwindow.h"
#include "wxservice.h"
#include "prefs.h"
#include "profile/ProfileManager.h"   // Stage-0 TX/RX profiles
#include "profile/ProfileBindings.h"
#include "profile/ProfileStore.h"
#include "profile/CompanionLauncher.h"   // startup auto-launch (Settings → Hardware)
#include <QSettings>
#include "logbuffer.h"
#include "theme.h"
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QtQml>

#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

int main(int argc, char *argv[])
{
    // NOTE: the FFTW wisdom build used to run here as a self-spawned
    // `lyra.exe --build-wisdom <dir>` child (to isolate WDSPwisdom's
    // AllocConsole from the parent's stdout).  That was removed: an
    // unsigned exe launching a copy of itself trips antivirus SONAR
    // heuristics, which killed the build child on some machines so the
    // wisdom file never persisted and Lyra re-optimized on every launch.
    // The build now runs IN-PROCESS on a worker thread with the console
    // tamed -- see WdspNative::runWisdomCall (the Thetis model).

#ifdef _WIN32
    // Explicit WSAStartup before any native socket use (HL2Stream
    // uses raw WinSock2 — see src/hl2_stream.cpp).  Qt would do it
    // lazily for QUdpSocket, but we don't want to depend on Qt's
    // init ordering for our wire path.  WSAStartup is refcounted —
    // safe to call alongside Qt's.
    WSADATA wsa;
    ::WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    // Single-instance guard — FIRST, before the graphics-safe-mode sentinel
    // below (so a bounced second launch never leaves ui/gfxStartupPending
    // set and false-triggers safe mode on the next real launch) and before
    // any radio / audio / port is touched.  A second copy would collide on
    // the HL2 socket, the audio device, and the TCI listen port (40001) the
    // first already owns — the operator-visible "port in use".  Instead the
    // second launch RAISES the running window and exits.  `siServerName` is
    // used further down (after the window exists) to start the raise channel.
    //
    // instanceId is "default" today; the planned two-radio / two-config
    // feature will pass a distinct id per config so each radio gets exactly
    // one instance.  `--new-instance` bypasses the guard (power-user hatch).
    QString siServerName;
    if (!lyra::ui::acquireSingleInstance(argc, argv,
                                         QStringLiteral("default"),
                                         &siServerName))
        return 0;   // another Lyra is already running — it has been raised

    // Qt RHI backend selection.  Lyra targets Vulkan as the
    // primary graphics path per FEATURES.md §0 (cross-vendor /
    // cross-platform).  setGraphicsApi()
    // MUST be called BEFORE QGuiApplication construction.  If
    // Vulkan is unavailable at runtime (no driver loader / no
    // SDK / unsupported GPU) Qt RHI transparently falls back to
    // D3D12 → OpenGL — no operator-visible breakage.
    //
    // Setting QSG_INFO=1 makes Qt log the chosen backend on
    // startup so the operator can VERIFY which RHI is live
    // (look for "Using QRhi with backend Vulkan" in stdout).
    // qputenv is portable; std::setenv would be POSIX-only.
    qputenv("QSG_INFO", "1");
    // Org/app identity set statically (before any QSettings read) so the
    // graphics-backend pref below resolves to the right HKCU location.
    // QStandardPaths leaf is "Lyra-cpp" — no dir sharing with Python Lyra
    // ("Lyra") per CLAUDE.md §15.26.
    QCoreApplication::setOrganizationName(QStringLiteral("N8SDR"));
    QCoreApplication::setApplicationName(QStringLiteral("Lyra-cpp"));
    QCoreApplication::setOrganizationDomain(
        QStringLiteral("github.com/N8SDR1/Lyra-SDR"));
    // Capture qDebug/qWarning/etc into the in-app Log viewer + a rolling
    // app-data log file.  Release builds are GUI-subsystem (no console),
    // so this is the operator's window into diagnostics.  Install early —
    // after the app-data path resolves (org/app name set above), before
    // any service object starts logging.
    lyra::ui::LogBuffer::instance().install();

    // Factory-reset sentinel.  The Settings "Reset to factory defaults"
    // button can't safely wipe QSettings from inside the running app — the
    // live UI + docks would just re-persist their state on shutdown.  So it
    // arms this key and asks for a restart; here, on the NEXT launch (before
    // ANY pref / layout / graphics value is read), we clear the whole scope
    // so everything comes up at code defaults.  Same deferred-to-next-launch
    // idiom as the graphics safe-mode sentinel below and the wisdom rebuild.
    // Snapshot files (%APPDATA%\…\snapshots) and the FFT wisdom cache are on
    // disk, not in QSettings, so they deliberately survive — the snapshots
    // are the safety net if the reset was a mistake.
    {
        QSettings s;
        if (s.value(QStringLiteral("app/factoryResetPending"), false).toBool()) {
            s.clear();               // also removes the sentinel
            s.sync();
            qInfo("[factory-reset] cleared all settings to defaults");
        }
    }

    // Safe-boot hatch: `--safe` (or LYRA_SAFE=1 in the environment) forces the
    // software renderer and skips PC audio-device enumeration + auto-connect —
    // a guaranteed way in when a wedged GPU driver or a faulting virtual audio
    // device (VoiceMeeter / VB-Cable) is killing startup before the window ever
    // appears, and a quick way to localise which one.  --safe is normalised to
    // the env var so the WdspEngine ctor (which owns the audio enumeration) and
    // the auto-connect gate both see it without threading a flag through.
    bool safeBoot = qEnvironmentVariableIsSet("LYRA_SAFE");
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--safe")) {
            safeBoot = true;
            qputenv("LYRA_SAFE", "1");
        }
    }
    if (safeBoot)
        qWarning("[safe] SAFE BOOT active — software graphics, no audio device "
                 "enumeration, no auto-connect");

    // Operator-selectable RHI backend (Settings → Visuals → Graphics
    // backend; restart-to-apply since the API is fixed at startup).
    // "auto" leaves Qt RHI to pick per platform; any explicit value still
    // falls back transparently if that backend is unavailable at runtime.
    // Set below if this launch entered graphics "safe mode" after detecting
    // that the previous startup crashed — used to show a one-time notice once
    // the window is up.  gfxSafeBackend names the backend safe mode forced.
    bool    gfxCrashRecovered = false;
    QString gfxSafeBackend;
    {
        using RI = QSGRendererInterface;
        QSettings s;
        // Resolution order: LYRA_GRAPHICS env var (no-UI escape hatch for
        // testers) -> persisted Settings -> "auto" default.  "auto" leaves
        // the API unpinned so Qt RHI picks the most compatible backend per
        // machine (Direct3D 11 on Windows) and honours QSG_RHI_BACKEND.
        // Vulkan stays fully selectable in Settings -> Visuals.
        //
        // Crash-safe fallback ("safe mode after crash"): a "startup pending"
        // sentinel is set here and cleared ~2 s after the window is up (past
        // the scene-graph build where a bad GPU driver crashes, ~0.8 s in on
        // field reports — AMD D3D11 + many QQuickWidget swapchains).  If a
        // fresh launch finds the sentinel STILL set, the previous launch died
        // during UI build -> step down to a safer backend (OpenGL, then the
        // software rasterizer) so a tester never has to touch the registry or
        // an env var.  Sticky (persisted) until the operator picks a backend
        // in Settings -> Visuals, which clears it.  An explicit LYRA_GRAPHICS
        // override is always honoured and never overridden (debug hatch).
        const bool crashed =
            s.value(QStringLiteral("ui/gfxStartupPending"), false).toBool();
        const bool envForced =
            !qEnvironmentVariable("LYRA_GRAPHICS").trimmed().isEmpty();
        int safeDepth = s.value(QStringLiteral("ui/gfxSafeDepth"), 0).toInt();
        if (crashed && !envForced) {
            safeDepth = std::min(safeDepth + 1, 2);   // 1 = OpenGL, 2 = Software
            s.setValue(QStringLiteral("ui/gfxSafeDepth"), safeDepth);
            s.setValue(QStringLiteral("ui/gfxSafeMode"), true);
            gfxCrashRecovered = true;
        }
        s.setValue(QStringLiteral("ui/gfxStartupPending"), true);
        s.sync();                          // flush to registry before we risk a crash

        QString be = qEnvironmentVariable("LYRA_GRAPHICS").trimmed().toLower();
        if (be.isEmpty())
            be = s.value(QStringLiteral("ui/graphicsBackend"),
                         QStringLiteral("auto")).toString().toLower();
        // Safe mode overrides the auto/saved choice (never an env override).
        if (!envForced && s.value(QStringLiteral("ui/gfxSafeMode"), false).toBool()) {
            be = (safeDepth >= 2) ? QStringLiteral("software")
                                  : QStringLiteral("opengl");
            gfxSafeBackend = be;
        }
        if (safeBoot) be = QStringLiteral("software");   // safe-boot override
        if      (be == "vulkan")   QQuickWindow::setGraphicsApi(RI::Vulkan);
        else if (be == "opengl")   QQuickWindow::setGraphicsApi(RI::OpenGL);
        else if (be == "d3d11")    QQuickWindow::setGraphicsApi(RI::Direct3D11);
        else if (be == "d3d12")    QQuickWindow::setGraphicsApi(RI::Direct3D12);
        else if (be == "software")
            QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        // "auto" (default) -> leave unpinned; Qt RHI auto-selects.
        if (gfxCrashRecovered)
            qWarning("[gfx] previous startup did not complete — graphics "
                     "safe mode (depth %d -> %s)", safeDepth, qPrintable(be));
    }

    // Multisample anti-aliasing.  Without this the RHI swapchain runs
    // at 1 sample/pixel — every scene-graph edge (the spectrum trace,
    // passband box, markers) renders as hard stair-stepped pixels (the
    // "etch-a-sketch" look).  4x MSAA lets the GPU smooth every polygon
    // edge essentially for free on a modern card.  This is the single
    // biggest perceived-quality win and applies to ALL geometry, not
    // just the panadapter.  Must be set on the default surface format
    // BEFORE QGuiApplication so the QML window's swapchain picks it up.
    {
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        fmt.setSamples(4);
        QSurfaceFormat::setDefaultFormat(fmt);
    }

    QApplication app(argc, argv);

    // NOTE: do NOT force the "Basic" Qt Quick Controls style here.  It
    // silences the "does not support customization" warning flood, but the
    // panels are laid out against the native-Windows style's control metrics
    // (slider handle size, spin-box dimensions) — Basic's larger defaults
    // grow the sliders / spin boxes and clip panels.  The warnings are benign
    // log noise; killing them properly means pinning every control's metrics
    // in QML (a dedicated theming pass), not a one-line style switch.

    // Window / taskbar icon (the embedded .exe icon comes from src/lyra.rc;
    // this sets the running window + taskbar icon from the bundled PNG).
    app.setWindowIcon(QIcon(QStringLiteral(
        ":/qt/qml/Lyra/src/assets/logo/lyra-icon-256.png")));
    // Apply old-Lyra's dark cool-CRT theme to the whole QtWidgets shell
    // (menu bar, dock title bars, Settings dialog, tabs, controls).
    app.setStyleSheet(lyra::ui::lyraStyleSheet());
    // Hyperlink colours: Qt's default Link role is a dark blue that is
    // unreadable on Lyra's near-black surfaces.  Set the cyan accent
    // app-wide so EVERY rich-text widget (the Help guide today, anything
    // with links later) renders readable links on the dark background.
    {
        QPalette pal = app.palette();
        pal.setColor(QPalette::Link, QColor(0x5e, 0xc8, 0xff));
        pal.setColor(QPalette::LinkVisited, QColor(0x8f, 0xd6, 0xff));
        app.setPalette(pal);
    }
    // (Org / app / domain identity already set statically above, before
    // the graphics-backend QSettings read.)

    // Discovery lives on the Qt main thread.  This is FINE: QUdpSocket
    // is fully async / event-loop-driven on Qt (no blocking recvfrom,
    // no GIL, no Python).  The "wire path on its OWN OS thread"
    // requirement applies to the real-time EP2 writer (380 Hz
    // cadence) and the RX loop (~5000 datagrams/s) — those land in
    // dedicated threads in later commits.  Discovery is one-shot
    // bursty UDP — it does not need its own thread, and putting it
    // on one breaks QML's main-thread Connections requirement
    // (QQmlEngine refuses cross-thread connect from QML).
    auto *discovery = new lyra::ipc::HL2Discovery(&app);

    // P0.d — radio-structure configuration, the reference's locked
    // pre-create contract: "set radio structure, call this first"
    // (SetRadioStructure, cmsetup.c:29) then "set default sample
    // rates, call this before 'create'" (set_cmdefault_rates,
    // cmsetup.c:58).  The reference's C# console drives these via
    // DllImport before CreateRadio(); Lyra's operating-point VALUES
    // are configuration (not ported code):
    //
    //   cmSTREAM   = 2  — stream 0 = RX1 (stream exists per the
    //                     reference layout; its pump idles because
    //                     Lyra's RX dispatch is router->WdspEngine,
    //                     the operator-approved hybrid — no Inbound()
    //                     producer feeds it), stream 1 = TX.
    //   cmRCVR     = 1, cmXMTR = 1, cmSubRCVR = 1, cmNspc = 0.
    //                     Derived ids then match the live channel
    //                     layout exactly: chid(0,0)=0 (WdspEngine RX1
    //                     channel), chid(1,0)=1 (TX channel),
    //                     inid(1,0)=1, txid(1)=0.
    //   cmMAXInbound  = 256/stream (max samples per Inbound() call —
    //                     comfortably above the EP6 mic batch).
    //   cmMAXInRate   = 384000 (HL2 max IQ rate).
    //   cmMAXAudioRate / cmMAXTxOutRate = 48000 (AK4951 codec rate /
    //                     fixed HL2 TX out rate, CLAUDE.md §3.5).
    //   xcm_inrates   = 48000 per stream (TX mic-in rate; stream 0
    //                     unused by the pump), audio out 48000,
    //                     rcvr/xmtr ch_outrates 48000 -> every
    //                     derived size = getbuffsize(48000) = 64.
    {
        int cmSPC[cmMAXspc]   = {0, 0};
        int cmMAXInbound[2]   = {256, 256};
        lyra::wire::SetRadioStructure(2, 1, 1, 1, 0, cmSPC,
                                      cmMAXInbound, 384000, 48000, 48000);
        int xcm_inrates[2]    = {48000, 48000};
        int rcvr_outrates[1]  = {48000};
        int xmtr_outrates[1]  = {48000};
        lyra::wire::set_cmdefault_rates(xcm_inrates, 48000,
                                        rcvr_outrates, xmtr_outrates);
    }

    // Reference cmaster.c:273-320 — `create_cmaster()` is the central
    // process-wide CMaster lifecycle init (P0.d verbatim port; every
    // deferred subsystem is enumerated inline in src/wire/CMaster.cpp
    // with the reference text carried in place).  The router create at
    // cmaster.c:316 is inside this call; the per-stream CMB rings +
    // pump threads + in[] buffers + update[] critical sections are
    // created here per the reference per-stream loop.  create_xmtr()
    // is the one DEFERRED-CALLSITE accommodation — it runs in the
    // QTimer block below AFTER wdsp->load() + resolve_wdsp_calls()
    // (its OpenChannel/XCreateAnalyzer calls need the resolved seam).
    //
    // Must be called BEFORE HL2Stream construction (which wires the
    // EP6 thread to router_instance(0) on open) AND BEFORE the
    // `register_sink(...)` call below.  Process-lifetime; the matching
    // `destroy_cmaster()` runs in the aboutToQuit handler-4 AFTER all
    // consumers (the HL2Stream itself) have been torn down — matches
    // the reference's destroy_cmaster reverse-order discipline.
    lyra::wire::create_cmaster();

    // TCI TX-audio re-home: register the TciTxBridge as the
    // InboundTCITxAudio source so the verbatim cm_main TX pump can pull
    // inbound TCI client audio (CMaster.cpp xcmaster case 1, gated on
    // use_tci_audio).  Process-lifetime; safe immediately after
    // create_cmaster (pcm now exists).  The use_tci_audio gate itself is
    // wired to the operator mic-source selector below (after prefs).
    lyra::wire::SendpInboundTCITxAudio(&lyra::tci::TciTxBridge::inboundCb);

    // #50 native parametric-EQ rack stage: register EqModel's static bridge
    // as the pre-fexchange0 mic-EQ processor.  No-op until the EqModel is
    // constructed (MainWindow) — its ctor publishes the live engine, its
    // dtor clears it; the cb null-checks, so registration order is safe.
    lyra::wire::SendpTxEqProcessor(&lyra::ui::EqModel::txProcessCb);

    // #88 native speech rack (Auto-AGC + De-esser): register before the EQ
    // in the mic chain.  Same no-op-until-constructed safety as the EQ hook.
    lyra::wire::SendpTxSpeechProcessor(&lyra::ui::SpeechModel::txProcessCb);

    // #51 native 5-band combinator: register AFTER the EQ in the mic chain
    // (Speech -> EQ -> Combinator).  Same no-op-until-constructed safety, and
    // the model defaults the stage OFF so it's inert until the operator
    // enables it.
    lyra::wire::SendpTxCombinatorProcessor(&lyra::ui::CombinatorModel::txProcessCb);

    // #52 native Plate reverb: register AFTER the combinator in the mic chain
    // (Speech -> EQ -> Combinator -> Plate).  Same no-op-until-constructed
    // safety; the model defaults the stage OFF so it's inert until enabled.
    lyra::wire::SendpTxPlateProcessor(&lyra::ui::PlateModel::txProcessCb);

    // Step 2a: the stream object opens the EP6 RX path to a
    // selected radio on its OWN dedicated OS thread (std::jthread
    // inside HL2Stream — see hl2_stream.h for the locked
    // architecture rationale).  Exposed to QML as `Stream`.
    auto *stream = new lyra::ipc::HL2Stream(&app);

    // Step 3a: bundle the WDSP DSP engine.  Created here so the
    // QML context-property is available when Main.qml loads, but
    // load() itself is DEFERRED until AFTER engine.loadFromModule
    // — otherwise the logLine signal we emit on success/failure
    // arrives before the QML Connections are wired and the
    // operator never sees the result.  Step 3a's scope is
    // load-or-fail only — no symbols resolved yet (Step 3b), no
    // DSP yet (Step 3c+).
    auto *wdsp = new lyra::dsp::WdspNative(&app);

    // Step 3c-ii: the WDSP RX channel engine.  Owns the lifecycle of
    // a single WDSP receiver channel (OpenChannel + config + start /
    // stop + close).  Created here so the QML context-property exists
    // when Main.qml loads; openRx1() is DEFERRED until after the DLL
    // loads + wisdom is ensured (below) so the channel-open is fast.
    auto *wdspEngine = new lyra::dsp::WdspEngine(wdsp, &app);

    // Stage 2b2-fix-v3 — Step 3d: route DDC0 baseband IQ from the
    // EP6 thread into the DSP engine.  Registers directly on
    // router_instance(0) port=0/call_idx=0/ctrl_word=0 — the
    // reference's `LoadRouterAll(prouter[0], …)` posture
    // (router.c) flattened to the single-sink case Lyra needs.
    // Signature is the reference's (n_samples, iq_pairs) order;
    // we adapt to WdspEngine::feedIq(iq, n) inside the lambda.
    // Routes BEFORE HL2Stream::open() spawns the EP6 thread, so
    // the very first datagram dispatched lands in the sink.  No
    // HL2Stream shim involved — the previous `setIqSink(…)`
    // member was a Lyra-native arg-reorder wrapper; retiring it
    // matches the reference's "register at the router directly"
    // shape (Rule 26 cleanup).
    lyra::wire::register_sink(
        lyra::wire::router_instance(0),
        /*port=*/0, /*call_idx=*/0, /*ctrl_word=*/0,
        [wdspEngine](int n, const double* iq) {
            wdspEngine->feedIq(iq, n);
        });

    // Step 5: RX audio → HL2 onboard-codec (AK4951) jack is handled
    // inside WdspEngine now — dispatchAudioFrame → OutBound(0) → the
    // verbatim EP2 writer (sendProtocol1Samples).  The old
    // setHl2AudioSink/pushAudio EP2-ring path retired in §7.

    // TX-1 components 3 + 4c — Path A construction ordering
    // (matches the C reference's create_cmaster + create_xmtr
    // posture: the TXA channel is only OpenChannel'd AFTER the
    // WDSP DLL has been loaded).  The pointers are declared
    // here at outer scope (nullptr-initialised) so the
    // aboutToQuit teardown lambda — connected NOW, in
    // connection order BEFORE the stream->close() connect
    // below at line ~250 — captures them BY REFERENCE and
    // sees whichever value is live at app exit.  Actual
    // construction is deferred to the QTimer::singleShot block
    // further down (the same block that gates wdsp->ensureWisdom
    // + wdspEngine->openRx1 on wdsp->load() succeeding).  If
    // wdsp->load() fails or the user closes before the timer
    // fires, the pointers stay nullptr and `delete nullptr` is
    // a well-defined no-op — RX path is unaffected either way.
    //
    // Teardown order (aboutToQuit fires in connection order):
    //   1. stream->registerTxIqSource({}) +
    //      stream->registerTxControl({}) -> clears BOTH callback
    //      registrations FIRST.  Both callbacks capture txWorker
    //      by value (pointer); deleting txWorker before clearing
    //      them would leave dangling pointers that the EP2 writer
    //      thread (source) and the FSM (control: start/stop/inject)
    //      could call concurrently.  Both registrations take a
    //      brief mutex; after clearing, EP2 writer falls through
    //      to zero-fill on next datagram and FSM keydown/keyup
    //      hooks no-op cleanly (app is shutting down anyway).
    //   2. txWorker delete -> clears its mic consumer, joins
    //      worker thread, closes TX WDSP channel.
    //   3. stream->close() -> joins the rx-loop thread + ep6Thread.
    //   4. micSource delete -> clears ep6Thread's mic sink AFTER
    //      ep6Thread has been joined (Stage 2b2c: the wire-side
    //      sink contract is F1 set-once-before-start, so the
    //      sink-clear at dtor must happen with ep6Thread NOT
    //      running; the captured `this` is also safe — no reader
    //      can be inside the lambda once close() returns).
    lyra::dsp::Hl2Ep6MicSource *micSource = nullptr;
    // P0.d (2026-06-12) — create_xmtr()/destroy_xmtr() gate flag at
    // outer scope for the aboutToQuit handler-1.5.  The reference
    // calls create_xmtr() inside create_cmaster (cmaster.c:288);
    // Lyra-cpp's DEFERRED-CALLSITE accommodation (documented in
    // wire/CMaster.cpp's preamble) invokes it from the QTimer block
    // below AFTER wdsp->load() + resolve_wdsp_calls() succeed — the
    // verbatim body's OpenChannel/XCreateAnalyzer calls resolve
    // through the wire/wdspcalls.h seam and need the loaded DLL.
    // The flag gates the matching destroy_xmtr() (handler-1.5) so a
    // failed/never-run create never tears down through unresolved
    // seam pointers.
    //
    // The former `TxChannel` RAII carve-out is GONE: P0.d's verbatim
    // create_xmtr opens the WDSP TXA channel itself (cmaster.c:
    // 177-190 — OpenChannel(chid(inid(1,0),0)=1, 64, 4096, 48000,
    // 96000, 48000, type=1, state=0, ...)) and allocates the
    // xmtr[i].out[0..2] buffers (cmaster.c:126-127).  Reference
    // posture preserved: at create-time the TX channel is OPEN but
    // NOT STARTED (state=0; the keydown FSM keys it later via
    // SetChannelState through the seam).
    //
    // No local ILV pointer either (P0.b): the ILV lives at
    // pcm->xmtr[0].pilv, created by create_xmtr (cmaster.c:226-232)
    // and torn down by destroy_xmtr (cmaster.c:258).  SendpOutboundTx
    // (P3) wires the raw Outbound fn ptr via SetILVOutputPointer(0,
    // ...) AFTER create_xmtr, per the reference's registration
    // ordering.
    bool xmtrCreated = false;

    // Task #40 — TX-triggered zombie shutdown watchdog.  REGISTERED
    // FIRST so it fires before any teardown handler and the
    // 10 s watchdog timer starts at the very beginning of teardown.
    // Also drops a "[shutdown] === aboutToQuit FIRED ===" marker into
    // lyra-log.txt so the next bench can confirm aboutToQuit even
    // ran (a separate-class bug would be aboutToQuit never firing —
    // e.g. window-close not triggering app quit).
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        qWarning("[shutdown] === aboutToQuit FIRED — watchdog armed (10 s) ===");
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (g_shutdown_complete.load(std::memory_order_acquire)) return;
            qWarning("[shutdown] === WATCHDOG: teardown >10 s — TerminateProcess ===");
#ifdef _WIN32
            ::TerminateProcess(::GetCurrentProcess(), 1);
#else
            std::_Exit(1);
#endif
        }).detach();
    });

    // Note: `win` is declared further down — capture by reference so
    // the lambda sees the populated pointer at fire time (aboutToQuit
    // runs during app.exec(), well before main() returns).  Same
    // pattern as &txWorker/&micSource above.
    lyra::ui::MainWindow *winRef = nullptr;
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     [stream]() {
        // TX-rip Phase 1 (Q2): teardown collapses to the RX-only
        // surface (HL2Stream TX callbacks unregister + Hl2Ep6MicSource
        // delete).  TxDspWorker / TciMicSource teardown returns with
        // the rebuild per docs/TX_ARCHITECTURAL_MAPPING.md §10.3.
        //
        // Stage 2b2c: `delete micSource` moved out of this handler
        // and into a dedicated handler between stream->close()
        // (handler-2) and destroy_router (handler-4) — see below.
        // The reference-faithful ep6Thread().set_mic_sink({}) at
        // ~Hl2Ep6MicSource requires ep6Thread NOT running, so the
        // dtor must run AFTER stream->close() has joined it.
        qWarning("[shutdown] handler-1 ENTRY");
        if (stream) {
            qWarning("[shutdown] handler-1 step a: registerTxIqSource({}) - start");
            stream->registerTxIqSource({});
            qWarning("[shutdown] handler-1 step a: done");
            qWarning("[shutdown] handler-1 step b: registerTxControl({}) - start");
            stream->registerTxControl({});
            qWarning("[shutdown] handler-1 step b: done");
        }
        qWarning("[shutdown] handler-1 EXIT");
    });

    // P0.d (2026-06-12) — handler-1.5: destroy_xmtr().
    //
    // Reference cmaster.c:255-271 `destroy_xmtr` — destroys the ILV
    // (destroy_ilv), the TX analyzer (DestroyAnalyzer), closes the
    // WDSP TXA channel (CloseChannel at cmaster.c:265, NO preceding
    // SetChannelState dmode=1 per the reference design; the blocking-
    // flush stop belongs at keyup not at destroy-time) and frees the
    // xmtr[i].out[0..2] buffers — the verbatim P0.d port in
    // wire/CMaster.cpp does all four (the dexp/eer/txgain/sidetone
    // lines stay DEFERRED with their subsystems).
    //
    // Lyra ordering: between handler-1 (clears stream->registerTx{
    // IqSource,Control}({}) -- no more callbacks fire after this point)
    // and handler-2 (stream->close -- joins EP6 thread).  This window
    // is when the xmtr is safe to tear down: no producer can fire
    // SendpOutboundTx (handler-1 cleared), no consumer can pull
    // TxIqSource (handler-1 cleared).  Running BEFORE handler-4's
    // destroy_cmaster() preserves the reference's relative teardown
    // order (destroy_xmtr before the per-stream cmbuffs teardown,
    // cmaster.c:322-337) — see the DEFERRED-CALLSITE note in
    // wire/CMaster.cpp.
    //
    // Gated on xmtrCreated: if wdsp->load() failed or the operator
    // closed before the QTimer fired, create_xmtr() never ran and
    // the verbatim teardown (no null guards, reference posture) must
    // not run against unresolved seam pointers / a never-opened
    // channel.
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     [&xmtrCreated, wdspEngine]() {
        qWarning("[shutdown] handler-1.5 ENTRY (destroy_xmtr)");
        if (xmtrCreated) {
            wdspEngine->setTxaChannelOpen(false);   // #159 — txa[1] gone
            lyra::wire::destroy_xmtr();
            xmtrCreated = false;
        }
        qWarning("[shutdown] handler-1.5 EXIT");
    });

    // Task #26 — auto-mute RX1 audio while the wire MOX bit is live so
    // the operator doesn't self-deafen off their own TX coupling.
    // moxActiveChanged fires on the TR-settled edges only (post-mox_delay
    // + rf_delay on keydown; post-ptt_out_delay on keyup), so the mute
    // tracks the actual on-air window, not the operator's click.  The
    // WdspEngine side honours an operator-toggleable autoMuteOnTx
    // setting (default ON; persisted), so this signal can be ignored
    // by the gain calc when ESSB self-monitoring or similar is wanted.
    // AutoConnection: signal emitted from the Qt main thread (via the
    // FSM QTimers in HL2Stream), slot also lives on the main thread.
    QObject::connect(stream, &lyra::ipc::HL2Stream::moxActiveChanged,
                     wdspEngine, &lyra::dsp::WdspEngine::setTxMuted);

    // #158 DL-4 — mute RX out of the VAC mixer during TX (reference
    // SetIVACmox what-flag gating; RX→VAC silent on the air, the no-feedback
    // behavior).  Same TR-settled MOX edge as setTxMuted above; main-thread,
    // no-op when VAC1 isn't live.
    QObject::connect(stream, &lyra::ipc::HL2Stream::moxActiveChanged,
                     wdspEngine, &lyra::dsp::WdspEngine::setVacMox);

    // Task #44 Phase 2 — analyzer source swap on the MOX edge.
    // Same moxActiveChanged signal as setTxMuted (above); connection
    // ORDER matters: setTxMuted MUST run before setTxOwnsAnalyzer on
    // the keydown edge so audio mutes BEFORE the analyzer flips
    // sources (operator hears silence, not a brief blip).  Qt
    // guarantees connection-order DirectConnection slot dispatch
    // for same-thread emitter+receiver — leave THIS connect AFTER
    // the setTxMuted connect above.  Reordering them is a §15.26
    // PART B integrity hazard (amendment A.7 in the reconciled
    // doc).  If a future maintainer needs to reorder for any
    // reason, route both through a single combined slot instead.
    QObject::connect(stream, &lyra::ipc::HL2Stream::moxActiveChanged,
                     wdspEngine, &lyra::dsp::WdspEngine::setTxOwnsAnalyzer);
    // #105 — the analyzer source swap stays on wire MOX ONLY, NOT the CW
    // keyed state.  Gateware QSK CW generates the carrier from the cwx bits
    // and NEVER feeds the WDSP TX analyzer, so swapping to the TX-owned
    // analyzer during CW shows an EMPTY panadapter/waterfall.  Leaving the
    // panadapter RX-owned during CW keeps the actual keyed carrier visible
    // (the RX path receives it) — what the operator wants to see on the air.
    // (Red-on-air + the meter still follow CW via txDisplayActive / the
    // meter's keyed OR; only the spectrum SOURCE stays RX.)

    // P4.b TUN display-honesty — feed the panadapter the TX-analyzer
    // NCO−dial offset so its TX-state crop renders the TUN carrier at its
    // true RF (the dial, on the SSB marker) instead of NCO-relative.  The
    // TX analyzer's DC sits at the TX NCO (dial ∓ cw_pitch during TUN)
    // while the panadapter axis is centred on the RX DDS (= dial); this
    // closes that gap.  0 in RX and voice TX.  Same-thread AutoConnection.
    QObject::connect(stream, &lyra::ipc::HL2Stream::txAnalyzerOffsetChanged,
                     wdspEngine, &lyra::dsp::WdspEngine::setTxAnalyzerOffsetHz);

    // #174 CTUNE — when CTUNE locks the DDC centre, the freq path emits the
    // RX demod shift (dial − centre); drive the WDSP RXA receiver oscillator
    // with it.  0 when CTUNE is off (non-CTUNE tuning path unaffected).
    QObject::connect(stream, &lyra::ipc::HL2Stream::rxShiftHzChanged,
                     wdspEngine, &lyra::dsp::WdspEngine::setRxShiftHz);

    // #105 CW-2 — one CW pitch.  WdspEngine::cwPitchHz (the RX pitch / marker,
    // edited from the Tuning panel + the CW tab) is the single source; feed it
    // to the stream so the keyed CW carrier offset lands on the marker and the
    // HW sidetone runs at the CW pitch.  Seeded once, then tracks live.
    QObject::connect(wdspEngine, &lyra::dsp::WdspEngine::cwPitchChanged,
                     stream, [stream, wdspEngine] {
                         stream->setCwPitchHz(wdspEngine->cwPitchHz());
                     });
    stream->setCwPitchHz(wdspEngine->cwPitchHz());   // seed from persisted RX pitch

    // #91 VOX anti-VOX — feed the stream's VOX gate the live "what the
    // operator hears" RX-audio RMS from the engine.  Pulled on the Qt
    // main thread inside HL2Stream::onVoxPoll (50 ms), so it never runs
    // after the stream closes → dangle-proof (wdspEngine is an app child,
    // outlives the stream's poll timer).  VOX itself is default-OFF.
    stream->setVoxRxRmsProvider(
        [wdspEngine]() { return wdspEngine->voxRxAudioRmsLin(); });

    // Stop the RX worker on quit BEFORE the QObject children are
    // destroyed.  The IQ sink (above) calls into wdspEngine from the
    // worker thread; aboutToQuit fires while every object is still
    // alive, so closing the stream here joins the worker and
    // guarantees no feedIq() runs into a half-torn-down engine /
    // freed audio ring (see WdspEngine::stopAudio).
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     stream, [stream]() {
        qWarning("[shutdown] handler-2 ENTRY (stream->close)");
        if (stream) stream->close();
        qWarning("[shutdown] handler-2 EXIT");
    });

    // Stage 2b2c — Hl2Ep6MicSource teardown.  Runs AFTER handler-2
    // (stream->close has joined ep6Thread), so the dtor's
    // `ep6Thread().set_mic_sink({})` lands under the F1 set-once-
    // before-start invariant (ep6Thread is NOT running here), and
    // the lambda's captured `this` is safe (no reader thread can
    // be inside it).  Qt connects in registration order and fires
    // aboutToQuit handlers in that same order.
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     [&micSource]() {
        qWarning("[shutdown] handler-3 ENTRY (delete micSource - ~Hl2Ep6MicSource runs now)");
        delete micSource; micSource = nullptr;
        qWarning("[shutdown] handler-3 EXIT");
    });

    // Reference cmaster.c:322-337 — `destroy_cmaster()` is the central
    // process-wide CMaster lifecycle teardown.  Reverse-order match to
    // create_cmaster (which destroy_router(0,0) at line 326 is part of).
    // Phase B 2026-06-09 replaced the bare `lyra::wire::destroy_router(0,0)`
    // that previously lived here as a stand-in.  MUST run AFTER handler-2
    // above (stream->close → joins the EP6 thread → guarantees no
    // in-flight xrouter() dispatch through &router_instance(0) at the
    // moment we free the Router slot).  Qt connects in registration
    // order and fires aboutToQuit handlers in that same order, so this
    // lambda runs after handler-2 and the Stage-2b2c micSource-delete
    // handler-3.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        qWarning("[shutdown] handler-4 ENTRY (destroy_cmaster)");
        lyra::wire::destroy_cmaster();
        qWarning("[shutdown] handler-4 EXIT");
    });

    // #158 DL-1 — handler-PA: Pa_Terminate.  Connected LAST (Qt fires
    // aboutToQuit in connection order), so PortAudio is torn down after
    // every radio/audio handler above.  Idempotent no-op if Pa_Initialize
    // never ran (wdsp.dll absent → the create_rnet block never executed).
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        qWarning("[shutdown] handler-PA ENTRY (Pa_Terminate)");
        lyra::wire::ivacTerminatePortAudio();
        qWarning("[shutdown] handler-PA EXIT");
    });

    // Step 5: register the panadapter widget as a QML type under its
    // own URI (keeps it separate from the qt_add_qml_module "Lyra"
    // module's auto-registration).  Main.qml: `import LyraUI`.
    qmlRegisterType<lyra::ui::Panadapter>("LyraUI", 1, 0, "Panadapter");
    qmlRegisterType<lyra::ui::Waterfall>("LyraUI", 1, 0, "Waterfall");
    qmlRegisterType<lyra::ui::FreqDisplay>("LyraUI", 1, 0, "FreqDisplay");

    // The app shell is a QtWidgets QMainWindow (dock framework +
    // menu bar + toolbar + layout persistence).  It embeds the Quick
    // scene-graph UI via QQuickWidget and exposes the four service
    // objects to that QML as context properties (Discovery / Stream /
    // Wdsp / WdspEngine).  All native C++ — no Python, no GIL.
    auto *prefs = new lyra::ui::Prefs(&app);
    // Live-apply the verbose-logging toggle (Settings → Hardware) to the
    // log buffer; the initial value was read from QSettings on install().
    lyra::ui::LogBuffer::instance().setVerbose(prefs->debugLogging());
    QObject::connect(prefs, &lyra::ui::Prefs::debugLoggingChanged, prefs,
                     [prefs]() {
                         lyra::ui::LogBuffer::instance().setVerbose(
                             prefs->debugLogging());
                     });
    // Task #44 Phase 1 — wire MOX edges into Prefs so dbMin/dbMax
    // swap to the TX persistence pair on the air, and back to the RX
    // pair on un-key.  QML's `dbMin: Prefs.dbMin` binding picks up
    // the swap via the dbMin/MaxChanged signals Prefs emits in
    // setMoxActive — no panadapter wiring needed.
    QObject::connect(stream, &lyra::ipc::HL2Stream::moxActiveChanged,
                     prefs,  &lyra::ui::Prefs::setMoxActive);
    // #105 — dB-range swap stays on wire MOX ONLY (NOT CW), for the same
    // reason as the analyzer swap above: during CW the panadapter stays
    // RX-owned showing the received keyed carrier, so it must keep the RX
    // dB range to render that signal correctly.  Red-on-air + meter still
    // follow CW; only the spectrum source + scale stay RX.
    prefs->setMoxActive(stream->moxActive());   // seed initial state

    // TCI TX-audio source gate (re-home): when the operator selects "tci"
    // as the mic source, set use_tci_audio so the verbatim xcmaster TX
    // pump pulls from the TciTxBridge instead of the AK4951 EP6 mic;
    // "mic1" (or anything else) → hardware mic.  Single transmitter → txid 0.
    // Drop any stale TCI backlog when switching away from TCI.  Seed now +
    // track operator changes.
    {
        // TX mic-source gate: exactly one of {codec mic, TCI, VAC1} drives TX.
        //   "tci"   → use_tci_audio (TciTxBridge)
        //   "micpc" → use_vac_audio (#158 Stage 4: VAC-in / PC mic / digital)
        //   else    → HL2 codec EP6 mic (both overrides off)
        // SetTX{TCI,Vac}Audio are interlocked; the VAC inbound cb is
        // null-guarded so selecting VAC with VAC1 disabled is safe (falls
        // back to the codec mic).
        auto applyTxAudioSource = [prefs]() {
            const QString src = prefs->micSource();
            const bool tci = (src == QStringLiteral("tci"));
            const bool vac = (src == QStringLiteral("micpc"));
            lyra::wire::SetTXTCIAudio(0, tci ? 1 : 0);
            lyra::wire::SetTXVacAudio(0, vac ? 1 : 0);
            if (!tci) lyra::tci::TciTxBridge::instance().clear();
        };
        QObject::connect(prefs, &lyra::ui::Prefs::micSourceChanged,
                         prefs, applyTxAudioSource);
        applyTxAudioSource();   // seed initial state
    }

    // ---- TX/RX Profiles (Stage 0b — live wiring) ----------------------
    // Named profile bundles recalled as a unit (Thetis TX-Profile idiom,
    // Lyra-native; docs/architecture/PROFILE_MODEL_STAGE0_DESIGN.md).
    // ProfileManager is decoupled from prefs/stream/wdspEngine via the
    // capture/apply ProfileBindings built here (the only place all three
    // are in scope).  Stage-0 = the EXISTING fields; VAC source/route
    // land with the IVAC port (#158).  PA-enable + PTT-input stay GLOBAL.
    // The Settings→Profiles tab (#49) + front ProfilePanel dock (#55)
    // wire to `profiles` in Stage 0c; dirty-refresh signal fan-out lands
    // there too (no consumer of modifiedChanged exists until the dock).
    auto *profileSettings = new QSettings(&app);  // app org/app name (set at startup)
    lyra::profile::ProfileBindings pb;
    pb.isTxActive = [stream]() { return stream->moxActive(); };
    pb.capture = [prefs, stream, wdspEngine]() {
        lyra::profile::Profile p;
        // NOTE: mode is deliberately NOT captured — a profile is a pure
        // signal chain; recall never changes the operating mode (§A,
        // Thetis-faithful).  Per-family auto-recall keys on the mode but
        // applies only the chain.
        p.rxBandwidth     = prefs->rxBandwidth();
        p.txBandwidth     = prefs->txBandwidth();
        p.bwLocked        = prefs->bwLocked();
        p.filterLow       = prefs->filterLow();
        p.micSource       = prefs->micSource();
        p.micGainDb       = stream->micGainDb();
        p.micBoost        = stream->micBoost();
        p.tuneDriveMode     = prefs->tuneDriveMode();
        p.fixedTuneDrivePct = prefs->fixedTuneDrivePct();
        // tuneDrivePct / txDriveLevel intentionally NOT captured — they are
        // per-band (BandMemory), so capturing them made a band change dirty
        // the profile (operator report 2026-06-16; see Profile.h).
        p.tciRxGainDb     = prefs->tciRxGainDb();
        p.tciTxGainDb     = prefs->tciTxGainDb();
        p.vac1Enabled     = wdspEngine->vac1Enabled();
        p.vac1AutoDigital = wdspEngine->vac1AutoDigital();
        p.vac1RxGainDb    = wdspEngine->vac1RxGainDb();
        p.vac1TxGainDb    = wdspEngine->vac1TxGainDb();
        p.vac1LatencyMs   = wdspEngine->vac1LatencyMs();   // v5 #158
        p.vac1VacSize     = wdspEngine->vac1VacSize();      // v5 #158
        p.agcMode         = wdspEngine->agcMode();
        p.autoMuteOnTx    = wdspEngine->autoMuteOnTx();
        // #160: ALC ceiling + Leveler trio — operator runs leveler ON for
        // SSB / OFF for digital, so it must ride in the profile.
        p.alcMaxGainLinear     = stream->alcMaxGainLinear();
        p.levelerOn            = stream->levelerOn();
        p.levelerMaxGainLinear = stream->levelerMaxGainLinear();
        p.levelerDecayMs       = stream->levelerDecayMs();
        // v4 (#109/#107/#93): TX modulation knobs — part of "how this setup
        // sounds", so they ride per-profile (PHROT for SSB/ESSB, FM deviation
        // + CTCSS for repeater vs simplex, AM carrier per AM profile).
        p.phrotEnabled    = stream->phrotEnabled();
        p.fmDeviationHz   = stream->fmDeviationHz();
        p.ctcssEnabled    = stream->ctcssEnabled();
        p.ctcssToneHz     = stream->ctcssToneHz();
        p.amCarrierPct    = stream->amCarrierPct();
        p.txTimeoutSec    = stream->txTimeoutSec();
        p.txTimeoutBypass = stream->txTimeoutBypass();
        // Native TX DSP rack (#49 v3) — capture each stage's full state via
        // its live model (a MainWindow member; reached statically since the
        // models aren't in this lambda's scope).  instance() is null until
        // MainWindow builds them, so an early capture just omits the blobs.
        if (auto *m = lyra::ui::EqModel::instance())         p.eq         = m->saveState();
        if (auto *m = lyra::ui::SpeechModel::instance())     p.speech     = m->saveState();
        if (auto *m = lyra::ui::CombinatorModel::instance()) p.combinator = m->saveState();
        if (auto *m = lyra::ui::PlateModel::instance())      p.plate      = m->saveState();
        return p;
    };
    pb.apply = [prefs, stream, wdspEngine](const lyra::profile::Profile &p) {
        // Apply order: BW/lock/filterLow -> source -> gains/dsp.  Mode is
        // intentionally NOT applied (profiles never change the operating
        // mode — §A).  (load() guards this whole pass on !moxActive — §15.25.)
        prefs->setRxBandwidth(p.rxBandwidth);
        prefs->setTxBandwidth(p.txBandwidth);
        prefs->setBwLocked(p.bwLocked);
        prefs->setFilterLow(p.filterLow);
        // VAC (#158) — apply VAC config BEFORE micSource so the VAC1 engine
        // is live (callback registered) when setMicSource arms use_vac_audio;
        // else a micpc profile would arm the source against a null inbound cb
        // (silent TX) until the next rebuild.  Gains/autoDigital first, then
        // enable (final rebuild sees everything), then the source.  Devices
        // stay global (Settings → Audio) — not profile fields.
        wdspEngine->setVac1RxGainDb(p.vac1RxGainDb);
        wdspEngine->setVac1TxGainDb(p.vac1TxGainDb);
        // Latency posture (v5 #158) BEFORE enable: while VAC is off these just
        // store the values, so the single rebuild that setVac1Enabled fires
        // picks up the profile's ring depth + buffer size in one shot (if VAC
        // is already live and only these changed, the setters rebuild here).
        wdspEngine->setVac1LatencyMs(p.vac1LatencyMs);
        wdspEngine->setVac1VacSize(p.vac1VacSize);
        wdspEngine->setVac1AutoDigital(p.vac1AutoDigital);
        wdspEngine->setVac1Enabled(p.vac1Enabled);
        prefs->setMicSource(p.micSource);   // fires the applyTciTxSource gate above
        stream->setMicGainDb(p.micGainDb);
        stream->setMicBoost(p.micBoost);
        prefs->setTuneDriveMode(p.tuneDriveMode);
        prefs->setFixedTuneDrivePct(p.fixedTuneDrivePct);
        // tuneDrivePct / txDriveLevel NOT applied — BandMemory owns per-band
        // TX power + tune drive; a profile load leaves them untouched.
        prefs->setTciRxGainDb(p.tciRxGainDb);
        prefs->setTciTxGainDb(p.tciTxGainDb);
        wdspEngine->setAgcMode(p.agcMode);
        wdspEngine->setAutoMuteOnTx(p.autoMuteOnTx);
        // #160: ALC ceiling + Leveler trio (these forward to the live TXA
        // chain via the txControl_ seam; safe to push any time).
        stream->setAlcMaxGainLinear(p.alcMaxGainLinear);
        stream->setLevelerOn(p.levelerOn);
        stream->setLevelerMaxGainLinear(p.levelerMaxGainLinear);
        stream->setLevelerDecayMs(p.levelerDecayMs);
        // v4 (#109/#107/#93): TX modulation knobs (forward through the
        // txControl_ seam; CTCSS run stays FM-gated via applyCtcssRun, PHROT
        // stays digital-gated via applyPhrotRun — safe to push any time).
        stream->setPhrotEnabled(p.phrotEnabled);
        stream->setFmDeviationHz(p.fmDeviationHz);
        stream->setCtcssEnabled(p.ctcssEnabled);
        stream->setCtcssToneHz(p.ctcssToneHz);
        stream->setAmCarrierPct(p.amCarrierPct);
        stream->setTxTimeoutSec(p.txTimeoutSec);
        stream->setTxTimeoutBypass(p.txTimeoutBypass);
        // Native TX DSP rack (#49 v3) — apply each stage to its live model.
        // Tolerant: an empty blob (pre-rack profile) leaves the stage at its
        // current state.  Runs after the wire chain so it never interleaves
        // with the BW/source switches above.
        if (auto *m = lyra::ui::EqModel::instance())         m->loadState(p.eq);
        if (auto *m = lyra::ui::SpeechModel::instance())     m->loadState(p.speech);
        if (auto *m = lyra::ui::CombinatorModel::instance()) m->loadState(p.combinator);
        if (auto *m = lyra::ui::PlateModel::instance())      m->loadState(p.plate);
    };
    auto *profiles = new lyra::profile::ProfileManager(
        std::move(pb), lyra::profile::ProfileStore(profileSettings), &app);
    // Per-mode auto-recall: switching to a mode bound to a profile recalls
    // it (DIGU/DIGL -> TCI-source profile), mid-TX-guarded inside load().
    QObject::connect(prefs, &lyra::ui::Prefs::modeChanged, profiles,
                     [prefs, profiles]() { profiles->onModeChanged(prefs->mode()); });

    // Dirty-refresh fan-out (Stage 0c): every live-state source that
    // capture() reads re-runs ProfileManager::refreshModified() so the
    // "● modified" indicator (Settings → Profiles + the front dock)
    // stays live.  refreshModified() early-returns while load() is
    // applying, so the apply-time setter storm never churns the flag.
    // Qt drops the trailing signal args for the arg-less slot.
    // modeChanged is NOT here — mode isn't a profile field, so changing
    // mode must not mark the active profile "modified" (it stays wired
    // to onModeChanged below for the per-family auto-recall).
    for (auto sig : {&lyra::ui::Prefs::rxBandwidthChanged,
                     &lyra::ui::Prefs::txBandwidthChanged,
                     &lyra::ui::Prefs::bwLockedChanged,
                     &lyra::ui::Prefs::filterLowChanged,
                     &lyra::ui::Prefs::micSourceChanged,
                     &lyra::ui::Prefs::tuneDriveModeChanged,
                     &lyra::ui::Prefs::fixedTuneDrivePctChanged,
                     &lyra::ui::Prefs::tciRxGainDbChanged,
                     &lyra::ui::Prefs::tciTxGainDbChanged})
        QObject::connect(prefs, sig, profiles,
                         &lyra::profile::ProfileManager::refreshModified);
    QObject::connect(stream, &lyra::ipc::HL2Stream::micGainDbChanged, profiles,
                     &lyra::profile::ProfileManager::refreshModified);
    QObject::connect(stream, &lyra::ipc::HL2Stream::micBoostChanged, profiles,
                     &lyra::profile::ProfileManager::refreshModified);
    // txDriveLevelChanged intentionally NOT wired — TX drive is per-band
    // (BandMemory), not a profile field, so it must not affect "modified".
    QObject::connect(stream, &lyra::ipc::HL2Stream::txTimeoutSecChanged, profiles,
                     &lyra::profile::ProfileManager::refreshModified);
    QObject::connect(stream, &lyra::ipc::HL2Stream::txTimeoutBypassChanged, profiles,
                     &lyra::profile::ProfileManager::refreshModified);
    QObject::connect(wdspEngine, &lyra::dsp::WdspEngine::agcModeChanged, profiles,
                     &lyra::profile::ProfileManager::refreshModified);
    QObject::connect(wdspEngine, &lyra::dsp::WdspEngine::autoMuteOnTxChanged, profiles,
                     &lyra::profile::ProfileManager::refreshModified);

    // applyDefaultAtStartup() is deferred to AFTER MainWindow is built (see
    // below) — the #49 rack models (EqModel/.../PlateModel) are MainWindow
    // members, so the default profile's rack blobs only have somewhere to
    // land once the window exists.  The non-rack fields apply identically
    // either way (it behaves like loading the default profile at launch).

    // Task #74 / #77 / #78 / #95 — TUN drive-mode orchestrator.  On a
    // TUN-arm edge, if the active tuneDriveMode wants a drive other than
    // the operator's live slider (TuneDriveTune → tuneDrivePct;
    // TuneDriveFixed → fixedTuneDrivePct), stash the current wire drive
    // and push that % (0..100 → 0..255 wire DAC); on TUN-off restore the
    // stashed value.  In TuneDriveSlider mode this is a no-op — TUN keys
    // at the operator's TX Drive % (byte-identical to pre-#74).
    //
    // #77 fix: use std::optional<int> on the heap (shared_ptr-wrapped
    // for lifetime) so we track whether a real TUN-arm edge has
    // stashed a value in this session.  HL2Stream emits a defensive
    // `tuneEnabledChanged(false)` at every stream START and STOP
    // (hl2_stream.cpp, the auto-clear at lines ~586/685) — without
    // the optional guard those spurious off-edges would call
    // `setTxDriveLevel(*savedDrive)` with the default-constructed
    // 0, blanking the operator's TX Drive on every stream restart.
    // Only an actual arm→release pair restores; spurious offs no-op.
    //
    // #78 / #95 fix: connections on Prefs::tuneDrivePctChanged and
    // ::fixedTuneDrivePctChanged route live control movement to the
    // stream during an active tune (when stream->tuneEnabled() and the
    // active mode wants a non-slider drive).  Critically do NOT touch
    // `savedDrive` — the stashed pre-tune value must stay frozen so the
    // tune-release restore is correct.
    //
    // Lambdas run on the emitter thread (DirectConnection by default;
    // HL2Stream + prefs both live on the main thread) so a TUN arm's
    // drive push completes BEFORE the QML's subsequent
    // Stream.requestMox(true) — first TX frame post-MOX-rise
    // carries the tune-drive level.
    {
        auto savedDrive = std::make_shared<std::optional<int>>();
        // The tune-drive % the active mode wants, or -1 = "no swap"
        // (TuneDriveSlider — TUN keys at the operator's live TX Drive).
        auto tunePctFor = [prefs]() -> int {
            switch (prefs->tuneDriveMode()) {
                case lyra::ui::Prefs::TuneDriveTune:
                    return prefs->tuneDrivePct();
                case lyra::ui::Prefs::TuneDriveFixed:
                    return prefs->fixedTuneDrivePct();
                default:
                    return -1;   // TuneDriveSlider
            }
        };
        QObject::connect(stream, &lyra::ipc::HL2Stream::tuneEnabledChanged,
                         prefs, [stream, tunePctFor, savedDrive](bool on) {
            if (on) {
                const int pct = tunePctFor();
                if (pct < 0) {
                    // Slider mode — never stash; clear any orphan stash
                    // so a later mode-change + arm cycle starts clean.
                    savedDrive->reset();
                    return;
                }
                // Real TUN-arm with a non-slider mode: stash + push.
                // Re-arm without a release between (defensive) — keep
                // the earliest pre-tune value, not a tune-drive value.
                if (!savedDrive->has_value()) {
                    *savedDrive = stream->txDriveLevel();
                }
                stream->setTxDriveLevel(std::clamp(int(std::lround(
                    pct * 255.0 / 100.0)), 0, 255));
            } else {
                // TUN-release.  Restore whenever a real arm stashed a
                // value — independent of the current mode, so switching
                // mode mid-tune still restores correctly.  (#77: the
                // spurious off-edges at stream start/stop carry no stash
                // and must no-op.)
                if (savedDrive->has_value()) {
                    stream->setTxDriveLevel(savedDrive->value());
                    savedDrive->reset();
                }
            }
        });

        // #78 / #95 — live-follow the active tune control while TUN is
        // armed: drag the TxPanel TUN-drive slider (TuneDriveTune) or
        // edit the fixed-drive value (TuneDriveFixed) and the wire DAC
        // follows immediately.  No-op when TUN is not armed or in slider
        // mode.  Never mutates savedDrive — restore-on-release must use
        // the pre-tune value, not the last live-tuned value.
        auto liveFollow = [stream, tunePctFor]() {
            if (!stream->tuneEnabled()) return;
            const int pct = tunePctFor();
            if (pct < 0) return;
            stream->setTxDriveLevel(std::clamp(int(std::lround(
                pct * 255.0 / 100.0)), 0, 255));
        };
        QObject::connect(prefs, &lyra::ui::Prefs::tuneDrivePctChanged,
                         prefs, liveFollow);
        QObject::connect(prefs, &lyra::ui::Prefs::fixedTuneDrivePctChanged,
                         prefs, liveFollow);
    }
    // Weather-alert service — polls the operator's enabled sources and
    // feeds the header badges + toasts.  Reads its location from Prefs,
    // so a station-location change re-arms it.
    auto *wx = new lyra::wx::WxService(prefs, &app);
    QObject::connect(prefs, &lyra::ui::Prefs::locationChanged,
                     wx, &lyra::wx::WxService::reloadConfig);
    qInfo("[startup] services ready — building main window");
    auto *win = new lyra::ui::MainWindow(discovery, stream, wdsp,
                                         wdspEngine, prefs, wx, profiles);
    winRef = win;   // populate the aboutToQuit teardown handler's reference
    qInfo("[startup] main window constructed");
    // Single-instance raise channel: a later launch of the same instanceId
    // pings this server (see acquireSingleInstance) instead of starting a
    // second radio; bring the existing window to the front.
    lyra::ui::startSingleInstanceServer(siServerName, win, [win]() {
        win->setWindowState((win->windowState() & ~Qt::WindowMinimized)
                            | Qt::WindowActive);
        win->show();
        win->raise();
        win->activateWindow();
    });
    // Defer showing the main window when a one-time FFTW-wisdom BUILD is
    // pending (the build runs in-process below, behind a modal 'please
    // wait').  We must NOT present a fully-rendered, ready-looking Lyra
    // behind that modal: a user treats it as usable, starts operating or
    // force-quits, and aborts the build -- which then re-optimizes on the
    // next launch, the exact loop this change fixes.  When wisdom already
    // exists (the normal case) show now; otherwise the singleShot below
    // reveals the window once the build finishes.
    const bool wisdomReady = lyra::dsp::WdspNative::wisdomExists();
    if (wisdomReady)
        win->show();

    // Crash-safe graphics fallback (see the RHI block near the top of main):
    // we survived UI construction — clear the "startup pending" sentinel a
    // couple of seconds in (after any deferred startup completes).  A crash
    // during UI build never reaches the event loop, so the sentinel stays set
    // and the NEXT launch steps down to a safer backend.  Also cleared on a
    // clean quit; a hard crash reaches neither, which is the whole point.
    QTimer::singleShot(2000, win, [safeBoot]() {
        QSettings s;
        s.setValue(QStringLiteral("ui/gfxStartupPending"), false);
        // A successful --safe boot also clears the graphics crash-ladder, so the
        // operator's NEXT normal launch retries their real backend from scratch
        // instead of staying pinned to the software rasterizer.
        if (safeBoot) {
            s.setValue(QStringLiteral("ui/gfxSafeMode"), false);
            s.setValue(QStringLiteral("ui/gfxSafeDepth"), 0);
        }
        s.sync();
    });
    QObject::connect(&app, &QApplication::aboutToQuit, []() {
        QSettings().setValue(QStringLiteral("ui/gfxStartupPending"), false);
    });
    // One-time notice if this launch recovered from a startup crash (deferred
    // so the main window paints first, then the dialog pops over it).
    if (gfxCrashRecovered) {
        QTimer::singleShot(500, win, [win, gfxSafeBackend]() {
            const QString name = (gfxSafeBackend == QStringLiteral("software"))
                ? QObject::tr("Software (no GPU)") : QObject::tr("OpenGL");
            QMessageBox box(win);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle(QObject::tr("Graphics safe mode"));
            box.setText(QObject::tr(
                "Lyra didn't start cleanly last time, so graphics were "
                "switched to <b>%1</b> to get you running.\n\n"
                "You can change this any time in "
                "Settings → Visuals → Graphics backend.").arg(name));
            auto *keep = box.addButton(QObject::tr("Keep safe mode"),
                                       QMessageBox::AcceptRole);
            auto *retry = box.addButton(QObject::tr("Use my setting next launch"),
                                        QMessageBox::RejectRole);
            box.setDefaultButton(keep);
            box.exec();
            if (box.clickedButton() == retry) {
                QSettings s;
                s.remove(QStringLiteral("ui/gfxSafeMode"));
                s.remove(QStringLiteral("ui/gfxSafeDepth"));
            }
        });
    }

    // #49 v3 — the TX DSP rack models now exist (MainWindow members).
    // (1) Flag the active profile ● modified when any rack control changes,
    //     so dirty-tracking covers the rack (operator-confirmed).
    // (2) Apply the default profile NOW (deferred from before MainWindow) so
    //     its rack blobs reach the live models at startup.
    if (auto *m = lyra::ui::EqModel::instance()) {
        QObject::connect(m, &lyra::ui::EqModel::bandsChanged, profiles,
                         &lyra::profile::ProfileManager::refreshModified);
        QObject::connect(m, &lyra::ui::EqModel::bypassChanged, profiles,
                         &lyra::profile::ProfileManager::refreshModified);
        QObject::connect(m, &lyra::ui::EqModel::makeupDbChanged, profiles,
                         &lyra::profile::ProfileManager::refreshModified);
    }
    if (auto *m = lyra::ui::SpeechModel::instance())
        QObject::connect(m, &lyra::ui::SpeechModel::changed, profiles,
                         &lyra::profile::ProfileManager::refreshModified);
    if (auto *m = lyra::ui::CombinatorModel::instance()) {
        QObject::connect(m, &lyra::ui::CombinatorModel::paramsChanged, profiles,
                         &lyra::profile::ProfileManager::refreshModified);
        QObject::connect(m, &lyra::ui::CombinatorModel::bypassChanged, profiles,
                         &lyra::profile::ProfileManager::refreshModified);
    }
    if (auto *m = lyra::ui::PlateModel::instance()) {
        QObject::connect(m, &lyra::ui::PlateModel::paramsChanged, profiles,
                         &lyra::profile::ProfileManager::refreshModified);
        QObject::connect(m, &lyra::ui::PlateModel::bypassChanged, profiles,
                         &lyra::profile::ProfileManager::refreshModified);
    }
    profiles->applyDefaultAtStartup();   // no-op until a default profile exists

    // Defer the WDSP load / wisdom / channel-open to the FIRST
    // event-loop iteration via a zero-delay single-shot.  These steps
    // emit status via logLine signals; if we run them here (before
    // app.exec()) the QML scene isn't live yet, so the lines reach the
    // console via qInfo() but NOT the in-UI Log panel (operator-
    // observed — only [disc]/[strm], which fire after exec, showed).
    // Posting to the event loop means every [wdsp] line lands in the
    // Log panel exactly like [disc]/[strm].
    QTimer::singleShot(0, &app, [wdsp, wdspEngine, stream, prefs, win,
                                  wisdomReady, &micSource, &xmtrCreated]() {
        if (wdsp->load()) {
            // P0.a (2026-06-09) — resolve the ChannelMaster-port WDSP
            // call table from the now-loaded wdsp.dll.  MUST run after
            // load() and BEFORE any ported ChannelMaster code calls
            // OpenChannel/fexchange0/xresample/pscc/etc through the
            // table.  Includes the full PureSignal entry-point family
            // (operator-committed feature; all exports verified present
            // in the bundled DLL via dumpbin 2026-06-09).
            {
                const int missing = lyra::wire::resolve_wdsp_calls();
                if (missing == 0) {
                    qInfo("[wdspcalls] ChannelMaster + PureSignal call "
                          "table fully resolved from wdsp.dll");
                } else {
                    qWarning("[wdspcalls] %d UNRESOLVED entry points — "
                             "ported ChannelMaster paths touching them "
                             "would crash; see preceding log lines",
                             missing);
                }
            }

            // Step 3c-i: ensure FFTW wisdom is loaded BEFORE the first
            // OpenChannel anywhere.  Without it, WDSP's PATIENT
            // planning runs in-process on first channel-open and
            // freezes the UI for several minutes.  ensureWisdom()
            // either imports a cached file (fast) or spawns a
            // subprocess to build one (slow but the work is in the
            // child process, so the parent doesn't run FFTW_PATIENT
            // in-process).
            wdsp->ensureWisdom();

            // The one-time wisdom build (if any) is done -- reveal the
            // main window we deferred so the user never saw a ready-
            // looking Lyra behind the 'please wait' modal.  No-op when
            // wisdom already existed (window shown at construction).
            if (!wisdomReady) {
                win->show();
                win->raise();
                win->activateWindow();
            }

            // Step 3c-ii: with the DLL loaded + wisdom in place, open
            // RX1 as a live WDSP channel.  Channel-lifecycle proof
            // only — no IQ flows through it yet (Step 3d wires the RX
            // worker -> fexchange0).  The engine's destructor closes
            // the channel at app exit.
            wdspEngine->openRx1();

            // P0.d (2026-06-12) — create_xmtr(), the verbatim
            // reference body (cmaster.c:112-253).
            //
            // Reference: create_cmaster() calls create_xmtr() inline
            // (cmaster.c:288, AFTER create_rcvr).  Lyra's DEFERRED-
            // CALLSITE accommodation (see wire/CMaster.cpp preamble)
            // invokes it HERE — after resolve_wdsp_calls() so the
            // verbatim body's OpenChannel / XCreateAnalyzer calls
            // resolve through the wire/wdspcalls.h seam, and after
            // wdspEngine->openRx1() (= the reference's create_rcvr
            // equivalent in the approved hybrid) so RX init precedes
            // TX init exactly as the reference orders it.
            //
            // The verbatim body per xmtr: malloc0's xmtr[i].out[0..2]
            // (cmaster.c:126-127), OpenChannel(chid(inid(1,0),0)=1,
            // 64, 4096, 48000, 96000, 48000, type=1, state=0,
            // 0.000, 0.010, 0.000, 0.010, block=1) (cmaster.c:
            // 177-190 — state=0 == OPEN-but-NOT-STARTED, the
            // reference's create-time posture; keydown keys it
            // later), XCreateAnalyzer(inid(1,0)=1, ...) (cmaster.c:
            // 192-198 — TX analyzer disp 1; WdspEngine's panadapter
            // analyzer is disp 0, no collision), create_ilv(0, 1,
            // ch_outsize=64, 2, 3, pcm->OutboundTx) (cmaster.c:
            // 226-232).  The dexp/anti-vox/txgain/eer/sidetone lines
            // stay DEFERRED with their subsystems (reference text
            // carried in place in wire/CMaster.cpp).
            //
            // pcm->OutboundTx is still null here — P3 wires
            // SendpOutboundTx -> SetILVOutputPointer AFTER this call,
            // per the reference's netInterface registration ordering
            // (the verbatim setters carry NO null guards).  Until P3
            // + an Inbound() producer land, the TX stream stays
            // wire-quiescent: nothing releases stream 1's
            // Sem_BuffReady, so xcmaster(1) never fires.
            lyra::wire::create_xmtr();
            xmtrCreated = true;
            // #159 — txa[1] now exists; let the DSP engine apply the
            // TX filter type (TXASetMP would AV before this point).
            wdspEngine->setTxaChannelOpen(true);
            qInfo("[tx] P0.d direct port: create_xmtr() — WDSP TXA "
                  "channel chid(1,0)=1 opened (64, 4096, 48000, "
                  "96000, 48000, type=1, state=0), TX analyzer "
                  "disp 1 created, xmtr[0].out[0..2] allocated, "
                  "pcm->xmtr[0].pilv=%p (run=0 bypass, obid=1, "
                  "insize=64, ninputs=2, what=3)",
                  static_cast<void*>(lyra::wire::pcm->xmtr[0].pilv));

            // P3 (2026-06-12) — create_rnet() ONCE per process, HERE,
            // matching the reference's init order (create_cmaster ->
            // create_xmtr -> create_rnet -> StartAudio).  The ordering
            // is LOAD-BEARING: create_rnet's tail registration
            // SendpOutboundTx(OutBound) derefs pcm->xmtr[0].pilv with
            // NO null guard (the verbatim SetILVOutputPointer), so it
            // must follow create_xmtr above.  Previously called from
            // HL2Stream::open() on EVERY stop/start — re-allocating
            // prn each cycle (leak + wire-state reset).  open() now
            // asserts the prn-non-null contract (netInterface.c:40)
            // instead; if wdsp.dll failed to load this block never
            // runs and open() refuses with a clear qCritical (no
            // WDSP = no radio, the reference posture).
            lyra::wire::create_rnet();
            qInfo("[wire] P3: create_rnet() — prn allocated, "
                  "SendpOutboundTx(OutBound) registered "
                  "(pcm->OutboundTx -> xmtr[0].pilv)");

            // #158 DL-1 — PortAudio process init, once per process,
            // adjacent to create_rnet (the reference brings PortAudio up
            // in netInterface alongside the radio).  The VAC device layer
            // (StartAudioIVAC) is not wired to wdsp_engine until DL-2;
            // this only spins up PortAudio so DL-2 has a live PA context.
            // The matching Pa_Terminate is the aboutToQuit handler-PA
            // connected in main scope (after handler-4 / destroy_cmaster).
            {
                int paErr = lyra::wire::ivacInitPortAudio();
                if (paErr == 0)
                    qInfo("[wire] DL-1: PortAudio initialized — VAC host "
                          "audio context ready (not yet wired to wdsp_engine)");
                else
                    qWarning("[wire] DL-1: Pa_Initialize failed (err=%d) — "
                             "VAC host audio unavailable", paErr);
            }

            // TX-1 Path A: construct micSource NOW that the WDSP DLL
            // is loaded.  Mic-source construction order RELATIVE to
            // create_xmtr: micSource AFTER the xmtr because the
            // eventual Stage 7.5 mic-consumer-lambda (which calls
            // lyra::wire::Inbound(1, n, mic_iq) -> CMB ring ->
            // cm_main pump -> xcmaster(1) -> fexchange0 + xilv via
            // pcm->xmtr[0]) requires the TX channel + ILV to be live
            // when the first Inbound fires.
            //
            // Pointers are captured by reference from main()'s
            // outer scope so the aboutToQuit lambda (connected
            // earlier) sees them populated at teardown.
            micSource = new lyra::dsp::Hl2Ep6MicSource(*stream);

            // P4.b §4 — FSM re-home: re-wire the TxControl surface to
            // the WIRE-LIVE WDSP TXA channel chid(1,0).  The TX-rip
            // Phase 1 removal left txControl_ UNREGISTERED, so
            // create_xmtr opened the TXA channel but the FSM never armed
            // it (SetChannelState) and never pushed its mode / ALC /
            // bandpass — it sat at create-time defaults, incl. the ALC
            // max-gain trap that pins the TXA output chain to silence.
            // Result: keyed TX (voice AND tune) emitted nothing on the
            // wire (operator bench 2026-06-13).  The old provider drove
            // a now-deleted TxDspWorker/TxChannel (origin/main:src/
            // main.cpp:673); re-pointed here to the wdspcalls seam on
            // chid(1,0), the wire-live owner of the TXA channel.  This
            // is the Lyra-native FSM control seam; the SetChannelState /
            // SetTXA* calls + values are reference-faithful (the
            // reference's MOX handler calls them directly — console.cs:
            // 30345 SetChannelState(id(1,0),1,0) keydown / :30357
            // (id(1,0),0,1) keyup; SetTXA* at channel setup).
            {
                const int txch = lyra::wire::chid(1, 0);   // stream 1, subrx 0 = TXA channel (=1)

                // §15.23-class TX sideband fix (2026-06-13, operator
                // 9100 bench: USB transmitted LSB).  WDSP derives the SSB
                // sideband PURELY from the SIGN of the bandpass edges:
                // TXASetupBPFilters (TXA.c:840) does
                // CalcBandpassFilter(bp0, f_low, f_high) using f_low/f_high
                // AS-IS for every SSB mode — SetTXAMode does NOT re-sign.
                // So a mode change MUST re-push the sign-coded bandpass.
                // Faithful restore of the old TxChannel::pushBandpassLocked
                // (origin/main tx_channel.cpp:82-114): sign per the SSB
                // convention (USB = +low/+high positive baseband; LSB =
                // -high/-low negative baseband — the TX mirror of RX
                // §14.2), SetTXABandpassFreqs FIRST then SetTXAMode.  My
                // earlier wire-seam port wired setMode=SetTXAMode-only +
                // setBandpass=raw, dropping both — that was the bug.
                // Shared state so setMode + setBandpass re-push coherently.
                struct TxFilter { int mode = 1; double low = 200.0; double high = 3100.0; };
                auto txf = std::make_shared<TxFilter>();
                // FM band-limiting (two distinct stages — do not conflate):
                //
                //  1) bp0 (this primary bandpass) runs on the AUDIO, ahead of
                //     the FM modulator.  For clean NBFM it must brick-wall the
                //     audio to the comms voice band (~300-3000 Hz) so nothing
                //     above the high-cut ever reaches the modulator — that is
                //     what prevents over-deviation / splatter at the source.
                //     So bp0's edges are the AUDIO band (±high-cut), NOT a
                //     function of deviation, and NOT the operator's SSB/ESSB TX
                //     width (up to 10 kHz, which would splatter on FM).
                //  2) The modulator's OWN internal output bandpass clamps the
                //     occupied RF channel to ±(deviation + high-cut) — Carson's
                //     rule.  That is maintained by the WDSP FM stage itself
                //     (tracks deviation); we pin its high-cut explicitly via
                //     SetTXAFMAFFreqs below so it isn't left at a create default.
                //
                // (Earlier this branch set bp0 to ±(deviation+high), i.e. ~8 kHz
                //  of audio, and relied on the modulator's RF bandpass to trim
                //  the over-deviated result after the fact — dirtier than simply
                //  band-limiting the audio first.)
                constexpr double kFmAfLowcutHz  = 300.0;
                constexpr double kFmAfHighcutHz = 3000.0;
                auto pushTxFilter = [txch, txf, stream]() {
                    // Per-mode sign-coded bandpass (TX mirror of RX §14.2):
                    //   USB-side (USB/CWU/DIGU)         → +low..+high   (positive baseband)
                    //   LSB-side (LSB/CWL/DIGL)         → -high..-low   (negative baseband)
                    //   double-sideband (DSB/FM/AM/SAM) → -high/2..+high/2 (symmetric)
                    // The symmetric edges are what make AM/DSB/FM occupy BOTH
                    // sidebands — the SSB-only sign-coding was why keying AM put a
                    // one-sided USB signal on the air.  txf->high carries the set
                    // TX BW (Prefs.txBandwidth, e.g. 6000 for "6k").  For SSB that
                    // value IS the one-sided edge (low..high ≈ the BW wide).  For a
                    // double-sideband mode the occupied width is the FULL ±edge span,
                    // so the symmetric edge must be high/2 to make the occupied BW
                    // equal the set TX BW — mirroring the RX convention exactly
                    // (WdspEngine::computePassband: half = bw/2, ±half).  Using ±high
                    // doubled the occupied bandwidth (operator bench: AM at 6k filled
                    // ±6k = 12k instead of ±3k = 6k).
                    double lo, hi;
                    switch (txf->mode) {
                        case 0: case 3: case 9:               // LSB / CWL / DIGL
                            lo = -txf->high; hi = -txf->low;  break;
                        case 5: {                             // FM
                            // Brick-wall the AUDIO to the comms voice band
                            // before the modulator: symmetric ±high-cut (a real
                            // ~3 kHz low-pass on the real-valued mic audio).
                            // The occupied-RF (Carson) clamp is the modulator's
                            // own internal bandpass, pinned via SetTXAFMAFFreqs
                            // below — it is NOT bp0's job.
                            lo = -kFmAfHighcutHz;    hi =  kFmAfHighcutHz;    break;
                        }
                        case 2: case 6: case 10: {            // DSB / AM / SAM
                            const double half = txf->high / 2.0;
                            lo = -half;      hi =  half;      break;
                        }
                        default:                              // USB / CWU / DIGU
                            lo =  txf->low;  hi =  txf->high; break;
                    }
                    lyra::wire::SetTXABandpassFreqs(txch, lo, hi);  // sign-coded edges FIRST
                    lyra::wire::SetTXAMode(txch, txf->mode);        // then mode (re-runs TXASetupBPFilters)
                    if (txf->mode == 5 && lyra::wire::SetTXAFMAFFreqs)
                        // Pin the FM modulator's audio high-cut so its internal
                        // occupied-RF bandpass is ±(deviation + 3000) from a
                        // known edge, not a create-time default.  Deviation
                        // changes re-track it (HL2Stream::setFmDeviation drives
                        // SetTXAFMDeviation + re-runs this).
                        lyra::wire::SetTXAFMAFFreqs(txch, kFmAfLowcutHz, kFmAfHighcutHz);
                    if (txf->mode == 5 && lyra::wire::SetTXAFMEmphPosition)
                        // Pre-emphasis: FM force-runs the emphasis block, so
                        // the chain POSITION is the on/off.  1 = the native
                        // 6 dB/oct (300–3000 Hz) comms curve runs (Comm);
                        // off-position 2 = both call sites pass through =
                        // a true bypass (Off — flat for digital/data).
                        lyra::wire::SetTXAFMEmphPosition(
                            txch, stream->fmEmphasisMode() == 1 ? 1 : 2);
                    // #107 — CTCSS run/freq is now operator-driven + FM-gated:
                    // HL2Stream::setTxMode → applyCtcssRun forwards the effective
                    // run (ctcssEnabled && FM) on every mode edge, so basic FM is
                    // silent unless the operator enables the sub-tone.  (Was a
                    // hardcoded SetTXACTCSSRun(txch,0) force here.)
                };

                stream->registerTxControl(lyra::ipc::HL2Stream::TxControl{
                    .start = [txch]() {
                        lyra::wire::SetChannelState(txch, 1, 0);   // arm — non-blocking cos² up-ramp
                    },
                    .stop = [txch]() {
                        lyra::wire::SetChannelState(txch, 0, 1);   // stop — BLOCKING down-ramp flush
                    },
                    .setInjectTxIq = [](bool) {
                        // Wire-live: the cm pump feeds OutBound(1) every
                        // cycle and the EP2 writer's `!XmitBit ⇒
                        // memset(outIQbufp)` gate decides zero-vs-pass —
                        // there is no separate inject branch (that was the
                        // retired legacy EP2 packer).  Channel arm/stop +
                        // XmitBit do the gating; this is now vestigial,
                        // but the FSM still calls it so keep it a no-op.
                    },
                    .setMode = [txf, pushTxFilter](int wdspMode) {
                        // §15.23 fix: re-push the sign-coded bandpass for the
                        // new mode, not just SetTXAMode (which alone can't flip
                        // the sideband — see pushTxFilter).  Full WDSP TXA mode
                        // passthrough (was clamped to LSB/USB) so AM/DSB/FM/SAM
                        // modulate double-sideband.
                        txf->mode = wdspMode;
                        pushTxFilter();
                    },
                    .setMicGainDb = [txch](double db) {
                        lyra::wire::SetTXAPanelGain1(txch, std::pow(10.0, db / 20.0));  // dB → linear
                    },
                    .setAlcMaxGainLinear = [txch](double lin) {
                        lyra::wire::SetTXAALCMaxGain(txch, lin);    // un-trap the ALC ceiling
                    },
                    .setAlcDecayMs = [txch](int ms) {
                        lyra::wire::SetTXAALCDecay(txch, ms);
                    },
                    .setLevelerOn = [txch](bool on, double top) {
                        lyra::wire::SetTXALevelerSt(txch, on ? 1 : 0);
                        lyra::wire::SetTXALevelerTop(txch, top);
                    },
                    .setLevelerDecayMs = [txch](int ms) {
                        lyra::wire::SetTXALevelerDecay(txch, ms);
                    },
                    .setAmCarrierLevel = [txch](double c) {   // #93/#106 AM/SAM carrier (0..1)
                        if (lyra::wire::SetTXAAMCarrierLevel)
                            lyra::wire::SetTXAAMCarrierLevel(txch, c);
                    },
                    .setBandpass = [txf, pushTxFilter](double lo, double hi) {
                        // Operator passes POSITIVE magnitudes; pushTxFilter
                        // signs them per the current mode (§15.23).
                        txf->low  = lo;
                        txf->high = hi;
                        pushTxFilter();
                    },
                    .setTune = [txch, txf](bool on) {
                        // P4.b TUN — WDSP TXA output-side tone generator
                        // (postgen/gen1), the Thetis TUN mechanism
                        // (console.cs:30787-30801): single tone (mode 0)
                        // at ±cw_pitch, mag MAX_TONE_MAG.  Sign per mode
                        // (USB +cw_pitch, LSB -cw_pitch).  This pairs with
                        // the ∓cw_pitch TX-DDS offset (HL2Stream::
                        // txDdsHzForTune) — they CANCEL so the on-air
                        // carrier is zero-beat at the dial (a co-channel
                        // SSB listener hears nothing).  MUST use the same
                        // kTuneCwPitchHz as the DDS offset.  The MOX
                        // keydown (TUN button) arms the TXA channel that
                        // processes this tone.
                        if (on) {
                            constexpr double kMaxToneMag = 0.99999;   // Thetis MAX_TONE_MAG
                            const double pitch = static_cast<double>(
                                lyra::ipc::HL2Stream::kTuneCwPitchHz);
                            const double freq = (txf->mode == 1) ? pitch : -pitch;  // USB +, LSB −
                            lyra::wire::SetTXAPostGenMode(txch, 0);            // 0 = single tone
                            lyra::wire::SetTXAPostGenToneFreq(txch, freq);
                            lyra::wire::SetTXAPostGenToneMag(txch, kMaxToneMag);
                            lyra::wire::SetTXAPostGenRun(txch, 1);
                            qInfo("[tx] TUN postgen: run=1 mode=0(single) freq=%.0f mag=%.5f "
                                  "(txf->mode=%d USB=1; pairs with the TX-DDS offset → dial)",
                                  freq, kMaxToneMag, txf->mode);
                        } else {
                            lyra::wire::SetTXAPostGenRun(txch, 0);
                            qInfo("[tx] TUN postgen: run=0 (stopped)");
                        }
                    },
                    .setPhrotRun = [txch](bool on) {   // #109 phase rotator
                        if (lyra::wire::SetTXAPHROTRun)
                            lyra::wire::SetTXAPHROTRun(txch, on ? 1 : 0);
                    },
                    .setFmEmphasis = [txch](int mode) {   // FM pre-emphasis: 0=Off, 1=Comm
                        if (lyra::wire::SetTXAFMEmphPosition)
                            // 1 = run the native 6 dB/oct comms curve; 2 = bypass
                            lyra::wire::SetTXAFMEmphPosition(txch, mode == 1 ? 1 : 2);
                    },
                    .setFmDeviation = [txch, pushTxFilter](double hz) {   // #107 FM deviation
                        if (lyra::wire::SetTXAFMDeviation)
                            lyra::wire::SetTXAFMDeviation(txch, hz);
                        // Re-clamp the FM output bandpass to the new Carson
                        // width.  No-op for non-FM modes (pushTxFilter reads
                        // the live mode and only mode 5 uses the deviation).
                        pushTxFilter();
                    },
                    .setCtcssFreq = [txch](double hz) {     // #107 CTCSS tone
                        if (lyra::wire::SetTXACTCSSFreq)
                            lyra::wire::SetTXACTCSSFreq(txch, hz);
                    },
                    .setCtcssRun = [txch](bool on) {        // #107 CTCSS run (FM-gated)
                        if (lyra::wire::SetTXACTCSSRun)
                            lyra::wire::SetTXACTCSSRun(txch, on ? 1 : 0);
                    },
                });
                // registerTxControl() pushes the persisted mic gain / ALC
                // max-gain / ALC decay / leveler ONCE here, so the freshly
                // armed channel is off the create-time ALC trap.
                qInfo("[tx] P4.b §4: TxControl re-homed to WDSP TXA "
                      "chid(1,0)=%d (arm/stop + mode/mic/ALC/leveler/"
                      "bandpass via the wdspcalls seam)", txch);

                // USB-stuck-LSB fix (origin/main TX-1 8a-tx-mode): TX
                // tracks the operator's RX sideband.  Push the current
                // mode now (channel sits at create-time 0=LSB otherwise)
                // + on every RX-mode change.
                auto wdspTxModeFor = [](const QString &uiMode) -> int {
                    const QString m = uiMode.toUpper();
                    // WDSP TXA modulation mode (coincides with the RX modeToWdsp
                    // enum for these).  AM/DSB/FM now pass through (were all
                    // clamped to USB) so WDSP's TXA modulates them natively as
                    // symmetric double-sideband instead of a one-sided USB signal.
                    // SAM is an RX-only demod — WDSP TXA has no sync-AM modulator,
                    // so keying in SAM transmits AM (Thetis parity).
                    if (m == QStringLiteral("LSB"))  return 0;
                    if (m == QStringLiteral("USB"))  return 1;
                    if (m == QStringLiteral("DSB"))  return 2;
                    if (m == QStringLiteral("CWL"))  return 3;
                    if (m == QStringLiteral("CWU"))  return 4;
                    if (m == QStringLiteral("FM"))   return 5;
                    if (m == QStringLiteral("AM"))   return 6;
                    if (m == QStringLiteral("SAM"))  return 6;   // TX SAM -> AM
                    if (m == QStringLiteral("DIGU")) return 7;
                    if (m == QStringLiteral("DIGL")) return 9;
                    return 1;       // USB default
                };
                // #50 native-rack digital gate: bypass the WHOLE mic DSP rack
                // (EQ + future Speech/Combinator/Plate) in the digital data
                // modes so FT8/JS8/etc. are never voice-shaped.  Gated by MODE,
                // not source — a VAC-as-mic voice op in USB still gets the EQ.
                auto txModeIsDigital = [](const QString &uiMode) -> bool {
                    const QString m = uiMode.toUpper();
                    return m == QStringLiteral("DIGU") || m == QStringLiteral("DIGL");
                };
                // Modes that auto-bypass the whole native mic rack: the digital
                // data modes (above) PLUS FM — a multiband compressor / plate
                // reverb feeding the FM modulator's 6 dB/oct pre-emphasis
                // over-drives into "mush", so FM voice runs the clean chain
                // (the brick-wall AF LPF + selectable pre-emphasis own the FM
                // audio shaping).  Forced, like the digital gate.
                auto txModeBypassesRack = [txModeIsDigital](const QString &uiMode) -> bool {
                    return txModeIsDigital(uiMode)
                        || uiMode.toUpper() == QStringLiteral("FM");
                };
                QObject::connect(wdspEngine,
                                 &lyra::dsp::WdspEngine::modeChanged,
                                 stream, [stream, wdspEngine, wdspTxModeFor, txModeBypassesRack]() {
                    const QString m = wdspEngine->mode();
                    stream->setTxMode(wdspTxModeFor(m));
                    lyra::wire::SetTxRackBypass(txModeBypassesRack(m) ? 1 : 0);
                });
                {
                    const QString m0 = wdspEngine->mode();
                    stream->setTxMode(wdspTxModeFor(m0));
                    lyra::wire::SetTxRackBypass(txModeBypassesRack(m0) ? 1 : 0);
                }

                // #175 — waterfall-ID arm/cadence orchestrator.  Owns the
                // chip-armed "send once + every N min" behaviour: per burst it
                // flattens TX (TCI source + whole-rack bypass + DIGU/DIGL +
                // leveler-off) and restores the operator's TX state on the
                // un-key edge.  All transient/non-persisted (no Prefs/QSettings
                // touched) so a crash mid-burst can't strand source/mode.
                // Parented to the stream (session lifetime).
                new WaterfallIdController(prefs, stream, wdspEngine, stream);

                // #174 CTUNE Stage 2 — feed the C++ edge model its display
                // context: the live display span (zoom) + the signed RX filter
                // passband edges (mode-dependent).  pushEffectiveRxFreq uses
                // these for Thetis's smooth-scroll / bCanFitInView / re-center.
                {
                    auto pushCtuneSpan = [stream, wdspEngine]() {
                        stream->setCtuneDisplaySpanHz(
                            static_cast<double>(wdspEngine->spanHz()));
                    };
                    QObject::connect(wdspEngine, &lyra::dsp::WdspEngine::spanChanged,
                                     stream, pushCtuneSpan);
                    pushCtuneSpan();

                    auto pushCtuneFilters = [stream, prefs, wdspEngine]() {
                        const int fl = prefs->filterLow();    // low-cut magnitude (Hz)
                        const int bw = prefs->rxBandwidth();  // high-cut magnitude (Hz)
                        const QString m = wdspEngine->mode().toUpper();
                        int lo, hi;
                        if (m == QStringLiteral("LSB") || m == QStringLiteral("DIGL")
                            || m == QStringLiteral("CWL")) {
                            lo = -bw; hi = -fl;               // lower sideband
                        } else if (m == QStringLiteral("AM") || m == QStringLiteral("SAM")
                            || m == QStringLiteral("DSB") || m == QStringLiteral("FM")) {
                            lo = -bw; hi = bw;                // symmetric (double-sideband)
                        } else {
                            lo = fl; hi = bw;                 // USB / DIGU / CWU upper
                        }
                        stream->setCtuneFilterEdges(lo, hi);
                    };
                    QObject::connect(prefs, &lyra::ui::Prefs::filterLowChanged,
                                     stream, pushCtuneFilters);
                    QObject::connect(prefs, &lyra::ui::Prefs::rxBandwidthChanged,
                                     stream, pushCtuneFilters);
                    QObject::connect(wdspEngine, &lyra::dsp::WdspEngine::modeChanged,
                                     stream, pushCtuneFilters);
                    pushCtuneFilters();
                }

                // TX-1 component 8c + Task #53 — operator TX bandpass.
                // The TX BW combo (high edge = Prefs.txBandwidth) + the
                // shared low edge (Prefs.filterLow) drive the WDSP TXA
                // bandpass via setTxBandpass -> TxControl.setBandpass.
                // Without this the TX filter is stuck at the channel's
                // create-time default (~200..3100 Hz ≈ "3K") no matter
                // what the operator picks — wire BOTH edges live AND push
                // once now (AFTER the setTxMode push above, so the
                // operator's high edge wins over the 3100 default the
                // mode push re-signed).
                auto pushTxBandpass = [stream, prefs]() {
                    stream->setTxBandpass(prefs->filterLow(),
                                          prefs->txBandwidth());
                };
                QObject::connect(prefs, &lyra::ui::Prefs::txBandwidthChanged,
                                 stream, pushTxBandpass);
                QObject::connect(prefs, &lyra::ui::Prefs::filterLowChanged,
                                 stream, pushTxBandpass);
                pushTxBandpass();
            }

            // Task #53 — shared filterLow also drives the RX bandpass.
            // WdspEngine.setFilterLowHz triggers an internal
            // recomputePassband + applyModeFilter (live, no channel
            // restart).  Initial push: pull the persisted Prefs value
            // ONCE so the freshly-opened RX channel uses it instead of
            // WdspEngine::filterLow_'s create-time 100 Hz default.
            QObject::connect(prefs, &lyra::ui::Prefs::filterLowChanged,
                             wdspEngine, [wdspEngine, prefs]() {
                wdspEngine->setFilterLowHz(prefs->filterLow());
            });
            wdspEngine->setFilterLowHz(prefs->filterLow());

            // Task #36 — Hardware PTT input forwarder.  Mirror the
            // operator-facing Prefs.hwPttEnabled into HL2Stream's
            // gating atomic so the EP6 RX worker sees the live
            // intent without any stream restart.  Initial push:
            // honour the persisted opt-in (default false per §10
            // Q#1 phantom-TX safety).
            QObject::connect(prefs, &lyra::ui::Prefs::hwPttEnabledChanged,
                             stream, [stream, prefs]() {
                stream->setHwPttEnabled(prefs->hwPttEnabled());
            });
            stream->setHwPttEnabled(prefs->hwPttEnabled());

            // ── Session restore: NR-C + CTUN (tester request 2026-07-03) ──
            // Both RX-only, so — unlike WF-ID, which we force OFF each launch
            // for TX safety — there's no reason not to remember them.
            //  • Auto-load the operator's chosen noise profile (Settings →
            //    Noise) so NR-C comes up ready instead of an empty field.
            //    loadNoiseProfile self-guards the sample rate: a stale-rate
            //    profile loads nothing and NR-C stays off (recapture hint).
            //  • Re-engage NR-C only if it was on last time AND the profile
            //    actually loaded.  Re-engage CTUN if it was on — the stream
            //    restored rx1FreqHz in its ctor, so it locks at the real dial.
            {
                QSettings s;
                const QString autoProf =
                    s.value(QStringLiteral("dsp/noiseAutoLoadProfile")).toString();
                if (!autoProf.isEmpty()
                    && wdspEngine->noiseProfiles().contains(autoProf)
                    && wdspEngine->loadNoiseProfile(autoProf)
                    && s.value(QStringLiteral("dsp/noiseApplyEnabled"),
                               false).toBool()) {
                    wdspEngine->setNoiseApply(true);
                }
                if (s.value(QStringLiteral("ui/ctunEnabled"), false).toBool())
                    stream->setCtuneEnabled(true);
            }
        }

        // Radio memory: auto-connect to the last radio so the operator
        // doesn't have to Discover every launch.  Independent of the
        // WDSP load above — the RX/wire path works regardless.  If the
        // radio is off/unreachable the UI just shows "RX stalled".
        const QString lastIp =
            QSettings().value(QStringLiteral("radio/lastIp")).toString();
        // Auto-start-on-launch opt-out (Settings → Hardware).  Default ON
        // (historical behaviour).  When the operator unticks it Lyra loads
        // but waits for an explicit Start instead of opening the radio.
        if (!lastIp.isEmpty() && prefs->autoStartOnLaunch()
            && !qEnvironmentVariableIsSet("LYRA_SAFE")) {
            // Resilient connect: probe the remembered IP and open it only
            // if the radio answers; otherwise scan and self-heal to its
            // real address.  Prevents a blind open of a stale saved IP
            // (e.g. the radio's DHCP lease changed) leaving the window
            // stuck "Connecting…" to a dead host on launch.
            win->beginConnect(lastIp);
        }

        // Safety net: if the window was deferred for a wisdom build but
        // the DLL failed to load (so ensureWisdom never ran and never
        // revealed it), reveal it here so the app is still visible/usable.
        if (!wisdomReady && !win->isVisible()) {
            win->show();
            win->raise();
            win->activateWindow();
        }
    });

    // --- Startup auto-launch (Settings → Hardware → Startup) ---
    // Fire the operator's enabled companion apps a few seconds after the UI
    // is up, so Lyra's CAT/TCI servers are listening first.  Fire-and-forget
    // (closing Lyra never kills them).  Staggered so several apps don't fight
    // for CPU/audio at once.  Runs regardless of which show()-path ran above.
    QTimer::singleShot(2500, &app, [&app]() {
        QSettings s;
        struct Slot { const char *en; const char *path; const char *args; };
        const Slot appSlots[] = {
            {"autostart/sdrlogger/enabled", "autostart/sdrlogger/path", nullptr},
            {"autostart/app1/enabled", "autostart/app1/path", nullptr},
            {"autostart/app2/enabled", "autostart/app2/path", nullptr},
        };
        int n = 0;
        for (const auto &sl : appSlots) {
            if (!s.value(sl.en, false).toBool()) continue;
            const QString path = s.value(sl.path).toString().trimmed();
            if (path.isEmpty()) continue;
            const QString args =
                sl.args ? s.value(sl.args).toString() : QString();
            QTimer::singleShot(n++ * 1500, &app, [path, args]() {
                lyra::profile::CompanionLauncher::launchDetached(path, args);
            });
        }
    });

    const int rc = app.exec();

    // Task #40 — signal the watchdog that teardown completed cleanly
    // BEFORE WSACleanup or any further work that could itself hang.
    // The watchdog thread is detached + sleeping; if shutdown_complete
    // is true when it wakes, it skips TerminateProcess and the OS
    // reclaims it normally on process exit.
    qWarning("[shutdown] app.exec() returned cleanly — clearing watchdog");
    g_shutdown_complete.store(true, std::memory_order_release);

#ifdef _WIN32
    qWarning("[shutdown] WSACleanup - start");
    ::WSACleanup();
    qWarning("[shutdown] WSACleanup - done");
#endif
    qWarning("[shutdown] main() returning rc=%d", rc);
    return rc;
}

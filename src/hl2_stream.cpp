// Lyra — HPSDR Protocol 1 stream implementation (EP6 receive + EP2
// keepalive).  See hl2_stream.h for the locked architecture +
// protocol reference.

#include "hl2_stream.h"

#include "bands.h"
// Step 14 Stage 1 — wire-layer includes for the inert wire-up of
// prn singleton + metis_wire_bind + outbound_init in open().  See
// docs/architecture/STEP14_PLAN.md.  These three calls become live
// callers of the new wire layer for the first time; nothing reads
// the bound state yet (rxWorkerLoop / txWorkerLoop are still the
// live RX/TX path until later stages).
#include "tx/CwKeyer.h"         // #105 CW-3a — host CW keyer (CWX) element pump
#include "dsp/WaterfallId.h"    // #175 — TX waterfall callsign-ID raster generator
#include "tci/TciTxBridge.h"    // #175 bench — synthetic TX-audio injection seam
#include "wire/RadioNet.h"      // RadioNet, prn, UpdateRadioProtocolSampleSize, SampleRateIn2Bits
#include "wire/MetisFrame.h"    // metis_wire_bind, metis_socket_fd
#include "wire/ObBuffs.h"       // destroy_obbuffs (P3 — close() ring teardown)
#include "wire/FrameComposer.h" // §5 control-plane mapping: set_rx_freq / set_tx_freq / set_rx_step_attn_db (P4.b RX side)
#include "wire/TxGain.h"        // SetTXFixedGain/Run — TX power model Stage 2 fine drive
#include "bands.h"              // amateurBands / bandIndexForFreq — TX power model Stage 3

// WinSock2 MUST be included before windows.h to avoid winsock 1.x
// being pulled in via windows.h transitively.  NOMINMAX keeps the
// windows.h macros from clobbering std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>    // GetUdpStatisticsEx (Task #48 diagnostic)
#include <process.h>     // _beginthreadex — EP2 writer thread (P4.b; reference StartAudioNative netInterface.c:66)

#include <QByteArray>
#include <QDebug>           // qInfo / qCritical for safety-event mirror
#include <QMetaObject>
#include <QSettings>
#include <Qt>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace lyra::ipc {

namespace {

// #107 — standard CTCSS sub-audible tone table (EIA/TIA-603, 49 tones,
// 67.0..254.1 Hz).  snapCtcssTone() maps an arbitrary operator value to
// the nearest standard tone so the UI combo + persisted value stay valid.
constexpr double kCtcssTones[] = {
     67.0,  69.3,  71.9,  74.4,  77.0,  79.7,  82.5,  85.4,  88.5,  91.5,
     94.8,  97.4, 100.0, 103.5, 107.2, 110.9, 114.8, 118.8, 123.0, 127.3,
    131.8, 136.5, 141.3, 146.2, 151.4, 156.7, 159.8, 162.2, 165.5, 167.9,
    171.3, 173.8, 177.3, 179.9, 183.5, 186.2, 189.9, 192.8, 196.6, 199.5,
    203.5, 206.5, 210.7, 218.1, 225.7, 229.1, 233.6, 241.8, 250.3, 254.1};

double snapCtcssTone(double hz) {
    double best = kCtcssTones[0];
    double bestErr = 1e9;
    for (double t : kCtcssTones) {
        const double e = (t > hz) ? (t - hz) : (hz - t);
        if (e < bestErr) { bestErr = e; best = t; }
    }
    return best;
}

// 64-byte HPSDR P1 host→radio control packet.  start=true sends
// 0xEFFE 0x04 0x01 (start IQ); start=false sends 0xEFFE 0x04 0x00
// (stop everything).
QByteArray buildControlPacket(bool start) {
    QByteArray pkt(64, char{0});
    pkt[0] = static_cast<char>(0xEF);
    pkt[1] = static_cast<char>(0xFE);
    pkt[2] = static_cast<char>(0x04);
    pkt[3] = static_cast<char>(start ? 0x01 : 0x00);
    return pkt;
}

// 1032-byte EP2 host→radio keepalive datagram.  Frame-0 C&C carries
// Stringify a Windows error code (FormatMessage) for log lines.
QString winsockError(int code) {
    wchar_t *buf = nullptr;
    const DWORD len = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM     |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    QString descr;
    if (len && buf) {
        descr = QString::fromWCharArray(buf, len).trimmed();
        ::LocalFree(buf);
    }
    return descr.isEmpty()
        ? QStringLiteral("WSA=%1").arg(code)
        : QStringLiteral("WSA=%1: %2").arg(code).arg(descr);
}

// Task #48 — snapshot the Windows system-wide UDPv4 RX counters
// via GetUdpStatisticsEx.  Returns true on success and fills the
// three outputs; returns false on any failure (caller writes
// sentinel -1).  Read-only, ~microsecond cost — safe to call from
// start/close.  Counters are SYSTEM-WIDE (Windows doesn't expose
// per-socket UDP drop counts), so other UDP apps on the box (DNS
// lookups, mDNS, browser, etc.) move them too — interpret deltas
// IN CONTEXT, alongside Lyra's per-session totalDg_/seqErrors_.
//
// Field mapping (per Microsoft docs + Windows TCP/IP-stack
// behaviour):
//   dwInDatagrams — total UDP datagrams delivered by the IP layer
//                   to the kernel UDP code (system-wide).
//   dwNoPorts     — datagrams arriving for an unlistened port.
//                   For a healthy Lyra session this should be ~0
//                   for HL2 traffic; a spike could mean HL2 sent
//                   after we closed the socket (timing diagnostic
//                   for clean shutdown).
//   dwInErrors    — datagrams that arrived BUT the kernel could
//                   not deliver to any socket for reasons OTHER
//                   than no-listener.  On Windows this INCLUDES
//                   socket-receive-buffer-full discards.  THIS is
//                   the counter that should move if our 4 MiB
//                   SO_RCVBUF is overflowing because Lyra's RX
//                   thread stalled.
//
// Interpreting the delta at close():
//   * Δ dwInErrors  > 0  AND  seqErrors > 0
//     → drops in the kernel UDP layer.  Lyra RX thread stalled,
//       socket buffer filled, kernel dropped packets.  Look at
//       Lyra-side: thread starvation, scheduler stall, paint
//       storm, etc.
//   * Δ dwInErrors == 0  AND  seqErrors > 0
//     → kernel saw NO problem; drops happened UPSTREAM of the
//       kernel.  Look at: NIC driver receive ring (Device
//       Manager → NIC → Properties → Advanced → "Receive
//       Buffers" → bump to max; "Interrupt Moderation" → off),
//       NIC hardware, switch in the path, EMI on the cable.
//   * Δ dwInDatagrams ≈ Lyra's totalDg_  (within a few % from
//     unrelated apps' traffic): sanity check that the system
//     stats are coherent with our session.
bool snapshotUdpStatsV4(qint64 *inDatagrams,
                        qint64 *inNoPorts,
                        qint64 *inErrors) {
    MIB_UDPSTATS stats{};
    const DWORD rc = ::GetUdpStatisticsEx(&stats, AF_INET);
    if (rc != NO_ERROR) {
        return false;
    }
    if (inDatagrams) *inDatagrams = static_cast<qint64>(stats.dwInDatagrams);
    if (inNoPorts)   *inNoPorts   = static_cast<qint64>(stats.dwNoPorts);
    if (inErrors)    *inErrors    = static_cast<qint64>(stats.dwInErrors);
    return true;
}

// Open a native UDP socket bound to ANY:ephemeral.  Sets a 4 MiB
// receive buffer (5 MB/sec EP6 wire rate × ~0.8 sec headroom for
// brief GUI / scheduler stalls — much larger than the kernel
// default 64 KiB which can drop packets under contention).
SocketHandle openNativeUdpSocket(QString *errOut) {
    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        if (errOut) *errOut = QStringLiteral("socket: %1")
                              .arg(winsockError(::WSAGetLastError()));
        return kInvalidSocket;
    }
    int rcvbuf = 4 * 1024 * 1024;
    ::setsockopt(s, SOL_SOCKET, SO_RCVBUF,
                 reinterpret_cast<const char*>(&rcvbuf),
                 sizeof(rcvbuf));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    if (::bind(s, reinterpret_cast<sockaddr*>(&local),
               sizeof(local)) != 0) {
        if (errOut) *errOut = QStringLiteral("bind: %1")
                              .arg(winsockError(::WSAGetLastError()));
        ::closesocket(s);
        return kInvalidSocket;
    }
    return static_cast<SocketHandle>(s);
}

quint16 localPortOf(SocketHandle sh) {
    sockaddr_in local{};
    int len = sizeof(local);
    if (::getsockname(static_cast<SOCKET>(sh),
                      reinterpret_cast<sockaddr*>(&local), &len) == 0) {
        return ntohs(local.sin_port);
    }
    return 0;
}

} // namespace

HL2Stream::HL2Stream(QObject *parent) : QObject(parent) {
    statsTimer_.setInterval(kStatPeriodMs);
    connect(&statsTimer_, &QTimer::timeout,
            this, &HL2Stream::onStatsTick);
    autoLnaTimer_.setInterval(kAutoLnaPeriodMs);
    connect(&autoLnaTimer_, &QTimer::timeout,
            this, &HL2Stream::onAutoLnaTick);
    // Stage 2b2-fix-v2 — HW-PTT-in poll timer (Qt main thread).
    // 50 ms cadence (20 Hz) — fast enough that foot-switch
    // press-to-key feels instantaneous, slow enough that the
    // per-tick `prn->ptt_in` read + edge-detect costs are noise.
    // Reads the level the EP6 thread writes unconditionally at
    // every status decode (`Ep6RecvThread.cpp:866`), mirroring
    // the reference's "wire writes prn->ptt_in; consumer FSM
    // polls and edge-detects on its own clock" posture.
    hwPttTimer_.setInterval(50);
    connect(&hwPttTimer_, &QTimer::timeout,
            this, &HL2Stream::onHwPttPoll);
    // #91 — VOX polls on the SAME 50 ms tick as HW-PTT (its own slot,
    // not chained inside onHwPttPoll, so onHwPttPoll's early-returns
    // can't skip it).  Timer start/stop lifecycle is shared (open()/
    // close()).  The detector is seeded via applyVoxParams() AFTER the
    // QSettings VOX loads further down (so persisted params take effect).
    connect(&hwPttTimer_, &QTimer::timeout,
            this, &HL2Stream::onVoxPoll);
    // Radio memory: restore the last tuned RX1 frequency.  QSettings
    // resolves to %APPDATA%\N8SDR\Lyra-cpp\ (org/app set in main()
    // before this object is constructed).
    const quint32 persistedRxHz =
        QSettings().value(QStringLiteral("rx/freqHz"), 7074000u).toUInt();
    rx1FreqHz_.store(persistedRxHz, std::memory_order_relaxed);
    // Frequency calibration — restore the persisted ppm trim and seed the
    // wire-side factor BEFORE the first freq push (open) so the very first
    // tune already lands corrected.  Default 1.0 = exact no-op.
    {
        const double f =
            QSettings().value(QStringLiteral("cal/freqCorrection"), 1.0).toDouble();
        freqCorrection_.store(f, std::memory_order_relaxed);
        lyra::wire::set_freq_correction(f);
    }
    // TX-0c-tune — simplex TX follows RX1 from launch (not just on
    // operator dial moves).  Without this, the persistence restore
    // bypasses setRx1FreqHz() so txFreqHz_ sits at its 7,074,000
    // default until the operator nudges the dial — and TUN lands the
    // carrier "several kHz below" the marker (operator-caught, 2026-05-28).
    // Single source of truth: every other write to rx1FreqHz_ goes
    // through setRx1FreqHz(), which mirrors there too.
    txFreqHz_.store(persistedRxHz, std::memory_order_relaxed);
    // SPLIT — restore VFO B + split-on state.  If split was left ON,
    // txFreqHz_ must come up pointing at VFO B (not the rx1 seed above),
    // so PS feedback + the TX NCO are correct from the first send.
    splitEnabled_.store(
        QSettings().value(QStringLiteral("tx/splitEnabled"), false).toBool(),
        std::memory_order_relaxed);
    vfoBHz_.store(
        QSettings().value(QStringLiteral("tx/vfoBHz"), persistedRxHz).toUInt(),
        std::memory_order_relaxed);
    if (splitEnabled_.load(std::memory_order_relaxed))
        txFreqHz_.store(vfoBHz_.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
    // RIT/XIT offsets (persisted; default disabled / 0).  Restored before
    // the first send so a persisted offset is applied from come-up.
    ritEnabled_.store(
        QSettings().value(QStringLiteral("rx/ritEnabled"), false).toBool(),
        std::memory_order_relaxed);
    ritOffsetHz_.store(std::clamp(
        QSettings().value(QStringLiteral("rx/ritOffsetHz"), 0).toInt(), -9999, 9999),
        std::memory_order_relaxed);
    xitEnabled_.store(
        QSettings().value(QStringLiteral("tx/xitEnabled"), false).toBool(),
        std::memory_order_relaxed);
    xitOffsetHz_.store(std::clamp(
        QSettings().value(QStringLiteral("tx/xitOffsetHz"), 0).toInt(), -9999, 9999),
        std::memory_order_relaxed);
    // External filter board: restore enable state + seed the OC pattern
    // for the restored band (so the board is correct from the first send).
    filterBoardEnabled_ =
        QSettings().value(QStringLiteral("hw/filterBoard"), false).toBool();
    // #199 Stage 2 — seed the editable OC table with the N2ADR preset (so an
    // enabled board reproduces today's per-band pattern byte-for-byte) + set
    // the family + sync the master gate to the restored enable state, THEN
    // seed the OC pattern for the restored band.  (Stage 3 replaces the
    // preset seed with the persisted oc/* table.)
    oc_.setFamily(lyra::oc::Family::HermesLite);
    seedOcN2adr();
    loadOcSettings();   // override the N2ADR seed if the operator edited the table
    oc_.setEnabled(filterBoardEnabled_);
    updateOcPattern();
    // RX1 LNA gain (AD9866 PGA) — restore the operator's last setting.
    lnaGainDb_.store(
        std::clamp(QSettings().value(QStringLiteral("rx/lnaGainDb"), 31).toInt(),
                   kLnaMinDb, kLnaMaxDb),
        std::memory_order_relaxed);
    // Auto-LNA: restore enable + undo + hold time.  Fresh-install
    // defaults match the operator's working station export
    // (auto-step-att=True, auto-att-undo-RX1=True,
    // hold-RX1=4): Auto ON / undo ON / 4 s hold.
    autoLnaEnabled_ = QSettings().value(QStringLiteral("rx/autoLna"), true).toBool();
    autoLnaUndo_    = QSettings().value(QStringLiteral("rx/autoLnaUndo"), true).toBool();
    autoLnaHoldSec_ = std::clamp(
        QSettings().value(QStringLiteral("rx/autoLnaHoldSec"), 4).toInt(), 1, 60);

    // TX-0c-pa-debug — host-side safety timeout state.  Persisted so an
    // operator-set 5-min timeout survives restarts.  Bypass starts false
    // every launch unless explicitly persisted as true (no surprise
    // "safety is off" on a fresh launch).
    txTimeoutSec_ = std::clamp(
        QSettings().value(QStringLiteral("tx/timeoutSeconds"),
                          kTxTimeoutDefaultSec).toInt(),
        kTxTimeoutMinSec, kTxTimeoutMaxSec);
    txTimeoutBypass_ = QSettings().value(
        QStringLiteral("tx/timeoutBypass"), false).toBool();
    // #91 — VOX state.  All persisted; VOX itself is default-OFF (no
    // surprise auto-keying on a fresh launch).  Params clamped to the
    // same ranges the setters enforce.  applyVoxParams() (called from
    // the ctor after the timer wiring) seeds the detector from these.
    voxEnabled_       = QSettings().value(
        QStringLiteral("tx/voxEnabled"), false).toBool();
    voxThresholdDbfs_ = std::clamp(QSettings().value(
        QStringLiteral("tx/voxThresholdDbfs"), -35.0).toDouble(), -80.0, 0.0);
    voxAntiVoxDbfs_   = std::clamp(QSettings().value(
        QStringLiteral("tx/voxAntiVoxDbfs"), -45.0).toDouble(), -90.0, 0.0);
    voxOpenMs_        = std::clamp(QSettings().value(
        QStringLiteral("tx/voxOpenMs"), 10).toInt(), 0, 500);
    voxHangMs_        = std::clamp(QSettings().value(
        QStringLiteral("tx/voxHangMs"), 300).toInt(), 0, 3000);
    voxAntiVoxOn_     = QSettings().value(
        QStringLiteral("tx/voxAntiVoxOn"), true).toBool();
    applyVoxParams();   // seed vox_ from the persisted params
    // #169 — SWR-protection state.  Operator surface persisted; the
    // advanced false-trigger-guard knobs (blank/dwell/floors) are
    // QSettings-only so the bench can tune them without a rebuild.
    swrProtectEnabled_ = QSettings().value(
        QStringLiteral("tx/swrProtectEnabled"),
        kDefaultSwrProtectEnabled).toBool();
    swrProtectLimit_ = std::clamp(
        QSettings().value(QStringLiteral("tx/swrProtectLimit"),
                          kSwrProtectDefaultLimit).toDouble(),
        kSwrProtectMinLimit, kSwrProtectMaxLimit);
    swrProtectDuringTune_ = QSettings().value(
        QStringLiteral("tx/swrProtectDuringTune"),
        kDefaultSwrProtectDuringTune).toBool();
    swrBlankMs_   = std::max(0, QSettings().value(
        QStringLiteral("tx/swrBlankMs"), kSwrBlankMsDefault).toInt());
    swrDwellMs_   = std::max(0, QSettings().value(
        QStringLiteral("tx/swrDwellMs"), kSwrDwellMsDefault).toInt());
    swrFwdFloorW_ = std::max(0.0, QSettings().value(
        QStringLiteral("tx/swrFwdFloorW"), kSwrFwdFloorWDefault).toDouble());
    swrRevFloorW_ = std::max(0.0, QSettings().value(
        QStringLiteral("tx/swrRevFloorW"), kSwrRevFloorWDefault).toDouble());
    swrProtectAction_ = std::clamp(QSettings().value(
        QStringLiteral("tx/swrProtectAction"), 0).toInt(), 0, 1);
    foldMinDrivePct_ = std::clamp(QSettings().value(
        QStringLiteral("tx/foldMinDrivePct"), kFoldMinDrivePctDefault).toInt(),
        1, 90);
    // #170a — Max TX drive cap (loaded before the drive-level store below
    // so the startup drive is itself clamped to the ceiling).
    // #170 (Max TX drive %) RETIRED — the Stage 3b watts Max cap (per-band,
    // predictive + reactive) supersedes the coarse drive-% ceiling.  Pin it
    // open (100 = no %-clamp) and drop any stored value so an operator's old
    // low setting can't strand the drive now the control is gone.
    maxDrivePct_ = 100;
    QSettings().remove(QStringLiteral("tx/maxDrivePct"));
    // PA enable is a PERSISTENT OPERATOR PREFERENCE (operator decision
    // 2026-05-29).  Restored across Lyra launches AND across stream
    // Stop/Start cycles within a session — what the operator last
    // explicitly chose on the Settings checkbox is what comes up.  The
    // cb58bcb come-up-not-keyed safety still applies to MOX, tune,
    // requestedMox_, and FSM state (those address a real come-up-
    // keyed bug); PA bias alone — without MOX — emits no carrier, so
    // a relaunch with PA preserved is safe (the operator must still
    // take a MOX action to put RF on the air, and the safety timer +
    // gateware watchdog cover the held-MOX and crashed-mid-key cases
    // that the original PA defensive clear was over-conservatively
    // also guarding against).
    paOn_.store(
        QSettings().value(QStringLiteral("tx/paEnabled"), false).toBool(),
        std::memory_order_relaxed);
    // Task #39 — HL2 +20 dB hardware Mic Boost (codec analog PGA).
    // Persisted across launches; NOT defensively cleared on
    // open/close (it's voice-chain calibration, not a safety gate).
    micBoost_.store(
        QSettings().value(QStringLiteral("tx/micBoost"), false).toBool(),
        std::memory_order_relaxed);
    // HL2 "Band Volts" output (MI0BOT / Ramdor gateware): persisted
    // across launches, seeded into the C0=0x00 frame's dither bit now so
    // the fan-pin band voltage is live from the first frame if enabled.
    bandVolts_.store(
        QSettings().value(QStringLiteral("hw/bandVolts"), false).toBool(),
        std::memory_order_relaxed);
    if (lyra::wire::prn != nullptr)
        lyra::wire::set_band_volts_output(
            bandVolts_.load(std::memory_order_relaxed));
    // TX-0c-pa-drive — operator-tunable drive DAC level (raw 0..255,
    // UI exposes 0..100 %).  Persisted across launches; NOT cleared on
    // stream open/close — the "volume" knob carries the operator's
    // intentional set point.  PA enable is the separate safety gate
    // (defensively cleared on every open/close); drive at any value
    // without PA + MOX produces zero RF, so persisting it is safe.
    // Default 0 on first-ever launch (operator must explicitly raise it
    // to emit a carrier).
    txDriveLevel_.store(
        std::min(maxDriveRaw(),
                 std::clamp(QSettings().value(QStringLiteral("tx/driveLevel"),
                                              0).toInt(), 0, 255)),
        std::memory_order_relaxed);
    // TX power model Stage 3 — load the per-band PA Gain table (Thetis
    // "PA Gain By Band").  Default 100 = neutral on every band; the
    // operator measures + nudges each band on the PA Gain Settings tab.
    {
        QSettings s;
        const auto &bands = lyra::amateurBands();
        for (int i = 0; i < kNumPaGainBands; ++i) {
            const QString band = i < int(bands.size())
                ? QString::fromUtf8(bands[i].name) : QString::number(i);
            paGainByBand_[i].store(
                s.value(QStringLiteral("pa_gain/%1/gain").arg(band),
                        kPaGainDefault).toDouble(),
                std::memory_order_relaxed);
            // Stage 3b — per-band measured full output (W); 0 = not measured.
            fullOutputWByBand_[i].store(
                s.value(QStringLiteral("pa_gain/%1/fullW").arg(band),
                        0.0).toDouble(),
                std::memory_order_relaxed);
            // Stage B — per-band TUN-learned cap drive ceiling (-1 = unset)
            // + the cap it was learned for (-1 = none).
            capCeilRaw_[i].store(
                s.value(QStringLiteral("pa_gain/%1/capCeilRaw").arg(band),
                        -1).toInt(),
                std::memory_order_relaxed);
            capCeilCapW_[i].store(
                s.value(QStringLiteral("pa_gain/%1/capCeilCapW").arg(band),
                        -1.0).toDouble(),
                std::memory_order_relaxed);
            // Stage B — "settled under the cap" latch.  The HL2 drive DAC is
            // coarse (16 steps); a cap can fall BETWEEN two hardware steps, so
            // the servo parks on the step just UNDER the cap and stops hunting.
            // Persisted so a converged band never blips over the cap again.
            capServoSettled_[i].store(
                s.value(QStringLiteral("pa_gain/%1/capSettled").arg(band),
                        false).toBool(),
                std::memory_order_relaxed);
            // Per-band PWR-meter trim (index-keyed; 1.0 = raw formula).
            pwrTrimByBand_[i].store(
                std::clamp(s.value(QStringLiteral("meter/pwrTrim/%1").arg(i),
                                   1.0).toDouble(), 0.1, 10.0),
                std::memory_order_relaxed);
            // Per-band raw-watts capture latch — session-only, start empty
            // (0 = no sample this session; populated live while transmitting).
            capturedRawW_[i].store(0.0, std::memory_order_relaxed);
            capturedDrive_[i].store(0, std::memory_order_relaxed);
        }
        // Stage 3b — the single watts Max cap (0 = off).
        maxOutputW_.store(
            s.value(QStringLiteral("tx/maxOutputW"), 0.0).toDouble(),
            std::memory_order_relaxed);
        // Cap arm gate (2026-07-03) — persisted; only meaningful with a value.
        // MIGRATION (safety): a cap set by a pre-arm-gate version has no
        // "tx/capArmed" key but WAS limiting the amp — treat it as armed so
        // upgrading never silently drops amp protection (the dangerous
        // direction = suddenly full power).  A cap value set AFTER the update
        // starts un-armed (the operator arms it after calibrating Full Output).
        {
            const double cap = maxOutputW_.load(std::memory_order_relaxed);
            const bool hasKey = s.contains(QStringLiteral("tx/capArmed"));
            const bool armed = hasKey
                ? (s.value(QStringLiteral("tx/capArmed")).toBool() && cap > 0.0)
                : (cap > 0.0);                       // migrate existing cap → armed
            capArmed_.store(armed, std::memory_order_relaxed);
            if (!hasKey && cap > 0.0)
                s.setValue(QStringLiteral("tx/capArmed"), true);
        }
    }
    // TX-1 component 5a — load operator-tuned TR-sequencing + fade
    // durations from QSettings (tx/trSeq/<key>).  Defaults match the
    // operator's working-station HL2+ DB export (15/50/13/5 ms) which
    // is hot-switch-safe for typical external SS HF linears; fade
    // defaults (50 ms in / 13 ms out) align with rfDelayMs_ and
    // spaceMoxDelayMs_ respectively.  Clamped to [kMinFsmDelayMs,
    // kMaxFsmDelayMs] on load to defend against stale / corrupt
    // QSettings (e.g. an operator hand-edited the file).
    moxDelayMs_ = std::clamp(
        QSettings().value(QStringLiteral("tx/trSeq/moxDelayMs"),
                          kDefaultMoxDelayMs).toInt(),
        kMinFsmDelayMs, kMaxFsmDelayMs);
    rfDelayMs_ = std::clamp(
        QSettings().value(QStringLiteral("tx/trSeq/rfDelayMs"),
                          kDefaultRfDelayMs).toInt(),
        kMinFsmDelayMs, kMaxFsmDelayMs);
    spaceMoxDelayMs_ = std::clamp(
        QSettings().value(QStringLiteral("tx/trSeq/spaceMoxDelayMs"),
                          kDefaultSpaceMoxDelayMs).toInt(),
        kMinFsmDelayMs, kMaxFsmDelayMs);
    pttOutDelayMs_ = std::clamp(
        QSettings().value(QStringLiteral("tx/trSeq/pttOutDelayMs"),
                          kDefaultPttOutDelayMs).toInt(),
        kMinFsmDelayMs, kMaxFsmDelayMs);
    txStopDelayMs_ = std::clamp(
        QSettings().value(QStringLiteral("tx/trSeq/txStopDelayMs"),
                          kDefaultTxStopDelayMs).toInt(),
        kMinFsmDelayMs, kMaxFsmDelayMs);
    // §3.9-5 revert: MoxEdgeFade deleted (operator-rejected 2026-06-06).
    // Reference does not envelope-shape SSB TX I/Q at all — host-side
    // cos² fade was a Lyra invention.  Hot-switch protection for an
    // external linear is now rfDelayMs_ ALONE (matches reference).
    // TX-1 component 8a — operator-tunable WDSP TXA gain stages.
    // micGainDb defaults to 0 dB = WDSP unity (no change vs the lyra-
    // cpp ship-no-setters posture at TxChannel::open()).
    //
    // alcMaxGainLinear defaults to 3.0 LINEAR (= 3.0× amplitude =
    // +9.54 dB amplification headroom), matching the verified
    // reference's Setup-load spinner default EXACTLY — integer
    // spinner 0..120 incr 1 default 3, passed straight to
    // SetTXAALCMaxGain as LINEAR (no dB conversion).  WDSP create-
    // time is 1.0 LINEAR (= 0 dB) which pins the entire TXA output
    // chain at a 0 dB ALC ceiling regardless of mic level — the
    // load-bearing trap that this default lifts.  Earlier Lyra
    // shipped this property as dB and called `dbToLin(3.0)=1.413`
    // — capping the ceiling at 47% of the reference's value and
    // producing a 5-6 dB power deficit on continuous mic-input
    // tones (task #79 root cause, fixed in §15.27 2026-06-03).
    //
    // The old QSettings key `tx/alcMaxGainDb` (dB semantics) is
    // intentionally abandoned on upgrade — operator silently
    // inherits the new reference-faithful default on first launch.
    //
    // Push-to-channel happens in registerTxControl() once the
    // TxControl callbacks are wired (the channel is open by then;
    // the operator's persisted/default values land on the channel
    // before the first keydown).  Clamped on load to defend against
    // stale / corrupt QSettings (hand-edited file).
    micGainDb_ = std::clamp(
        QSettings().value(QStringLiteral("tx/micGainDb"),
                          kDefaultMicGainDb).toDouble(),
        kMinMicGainDb, kMaxMicGainDb);
    alcMaxGainLinear_ = std::clamp(
        QSettings().value(QStringLiteral("tx/alcMaxGainLinear"),
                          kDefaultAlcMaxGainLinear).toDouble(),
        kMinAlcMaxGainLinear, kMaxAlcMaxGainLinear);
    // §15.27 Commit B — ALC decay (operator-tunable wcpagc release tau).
    alcDecayMs_ = std::clamp(
        QSettings().value(QStringLiteral("tx/alcDecayMs"),
                          kDefaultAlcDecayMs).toInt(),
        kMinAlcDecayMs, kMaxAlcDecayMs);
    // §15.27 Commit B — Leveler trio.  Default OFF (operator preference);
    // when enabled, defaults match the verified reference UI exactly
    // (15.0 LINEAR / 100 ms).  All three keys clamped on load.
    levelerOn_ = QSettings().value(QStringLiteral("tx/levelerOn"),
                                   kDefaultLevelerOn).toBool();
    // #109 — PHROT enable (default ON = WDSP/reference posture).
    phrotEnabled_ = QSettings().value(QStringLiteral("tx/phrotEnabled"),
                                      kDefaultPhrotEnabled).toBool();
    // #107 — FM operator knobs (deviation / CTCSS enable + tone).
    fmDeviationHz_ = std::clamp(
        QSettings().value(QStringLiteral("tx/fmDeviationHz"),
                          kDefaultFmDeviationHz).toDouble(), 1000.0, 6000.0);
    ctcssEnabled_  = QSettings().value(QStringLiteral("tx/ctcssEnabled"),
                                       false).toBool();
    ctcssToneHz_   = snapCtcssTone(
        QSettings().value(QStringLiteral("tx/ctcssToneHz"),
                          kDefaultCtcssToneHz).toDouble());
    fmEmphasisMode_ = std::clamp(
        QSettings().value(QStringLiteral("tx/fmEmphasisMode"), 1).toInt(), 0, 1);
    levelerMaxGainLinear_ = std::clamp(
        QSettings().value(QStringLiteral("tx/levelerMaxGainLinear"),
                          kDefaultLevelerMaxGainLinear).toDouble(),
        kMinLevelerMaxGainLinear, kMaxLevelerMaxGainLinear);
    levelerDecayMs_ = std::clamp(
        QSettings().value(QStringLiteral("tx/levelerDecayMs"),
                          kDefaultLevelerDecayMs).toInt(),
        kMinLevelerDecayMs, kMaxLevelerDecayMs);
    // §15.31 — ATT-on-TX operator surface.  Default ENABLED / 31 dB
    // (the verified reference's HL2 working posture); clamped on load.
    attOnTxEnabled_ = QSettings().value(QStringLiteral("tx/attOnTxEnabled"),
                                        kDefaultAttOnTxEnabled).toBool();
    // #94 External TX Inhibit — persisted, fail-safe (stays locked across
    // restarts until the operator clears it).
    txInhibit_ = QSettings().value(QStringLiteral("tx/inhibit"), false).toBool();
    attOnTxDb_ = std::clamp(
        QSettings().value(QStringLiteral("tx/attOnTxDb"), kAttOnTxDb).toInt(),
        0, 31);
    // #105 CW-1a — CW keyer config (cw_tx_design.md §7 defaults; clamped
    // on load against hand-edited QSettings).  prn doesn't exist yet at
    // ctor time — applyCwConfigToPrn() at open() seeds prn->cw from these.
    cwKeyerSpeedWpm_ = std::clamp(
        QSettings().value(QStringLiteral("tx/cw/keyerSpeedWpm"), 25).toInt(), 1, 60);
    cwKeyerWeight_ = std::clamp(
        QSettings().value(QStringLiteral("tx/cw/keyerWeight"), 50).toInt(), 33, 66);
    cwIambic_ = QSettings().value(QStringLiteral("tx/cw/iambic"), true).toBool();
    cwModeB_  = QSettings().value(QStringLiteral("tx/cw/modeB"), false).toBool();
    cwRevPaddle_ = QSettings().value(QStringLiteral("tx/cw/revPaddle"), false).toBool();
    cwStrictSpacing_ = QSettings().value(QStringLiteral("tx/cw/strictSpacing"), false).toBool();
    cwBreakInMode_ = std::clamp(
        QSettings().value(QStringLiteral("tx/cw/breakInMode"), 0).toInt(), 0, 2);  // default QSK
    cwHangDelayMs_ = std::clamp(
        QSettings().value(QStringLiteral("tx/cw/hangDelayMs"), 300).toInt(), 0, 1000);
    cwSidetoneOn_ = QSettings().value(QStringLiteral("tx/cw/sidetoneOn"), true).toBool();
    cwSidetoneLevel_ = std::clamp(
        QSettings().value(QStringLiteral("tx/cw/sidetoneLevel"), 64).toInt(), 0, 127);
    // #93/#106 — AM/SAM carrier level (% of 0..1 c_level; 50 % = WDSP default).
    amCarrierPct_ = std::clamp(
        QSettings().value(QStringLiteral("tx/amCarrierPct"),
                          kDefaultAmCarrierPct).toDouble(),
        0.0, 100.0);
    txSafetyTimer_ = new QTimer(this);
    txSafetyTimer_->setSingleShot(true);
    connect(txSafetyTimer_, &QTimer::timeout,
            this, &HL2Stream::onTxSafetyTimeout);
    // Self-wire keydown/keyup edges to arm/cancel the safety timer.
    // Same-thread queued not needed — moxActiveChanged fires from this
    // QObject's thread (the FSM steps run via QTimer::singleShot here).
    connect(this, &HL2Stream::moxActiveChanged, this, [this](bool on) {
        if (on)  armTxSafetyTimer();
        else     cancelTxSafetyTimer();
    });
    // TX-0c-tune — auto-disarm the tune-tone on every wire-MOX-off edge.
    // Whatever caused the keyup (operator click, safety timer, FSM
    // re-key collapse), the next time MOX rises the operator must
    // explicitly re-click TUN to emit a carrier.  Prevents a stray
    // MOX-only click from putting unintended RF on the antenna.
    connect(this, &HL2Stream::moxActiveChanged, this, [this](bool on) {
        if (!on && tuneEnabled_.load(std::memory_order_relaxed)) {
            setTuneEnabled(false);
        }
    });
    // #169 — SWR-protection evaluator: a 50 ms repeating tick armed on
    // key-down, cancelled on key-up, beside the one-shot TX-safety timer
    // above.  Same QObject affinity, so requestMox(false) on a trip funnels
    // through the standard keyup chain with no cross-thread marshalling.
    swrEvalTimer_ = new QTimer(this);
    swrEvalTimer_->setSingleShot(false);
    connect(swrEvalTimer_, &QTimer::timeout,
            this, &HL2Stream::evalSwrProtect);
    connect(this, &HL2Stream::moxActiveChanged, this, [this](bool on) {
        if (on)  armSwrProtect();
        else     disarmSwrProtect();
    });
    // #105 — keep the UI display-state (txDisplayActive_) in sync with both
    // the wire MOX bit and the CW keyed state.  Registered here (before the
    // main.cpp display-consumer connects) so the cached value is current
    // when those slots read txDisplayActive().
    connect(this, &HL2Stream::moxActiveChanged,
            this, &HL2Stream::updateTxDisplayActive);
    connect(this, &HL2Stream::cwKeyingActiveChanged,
            this, &HL2Stream::updateTxDisplayActive);
}

void HL2Stream::setLnaGainDb(int db)
{
    db = std::clamp(db, kLnaMinDb, kLnaMaxDb);
    if (db == lnaGainDb_.load(std::memory_order_relaxed)) {
        return;
    }
    lnaGainDb_.store(db, std::memory_order_relaxed);
    // §5 control-plane mapping (RX side): case 11 (!XmitBit) reads
    // prn->adc[0].rx_step_attn for the RX LNA gain.  +12 = the HPSDR P1
    // 0x14 bias, byte-identical to the retired composeCC RX branch
    // (0x40 | ((lnaGainDb_+12) & 0x3F)).  `db` is already clamped above.
    if (lyra::wire::prn != nullptr) {
        lyra::wire::set_rx_step_attn_db(db + 12, 0);
    }
    QSettings().setValue(QStringLiteral("rx/lnaGainDb"), db);
    emit lnaGainChanged();
    emit lnaSetByOperator(db);   // manual change → BandMemory saves per-band
    emit logLine(QStringLiteral("[hl2] LNA gain %1 dB").arg(db));
}

void HL2Stream::applyLnaGainNoPersist(int db)
{
    db = std::clamp(db, kLnaMinDb, kLnaMaxDb);
    if (db == lnaGainDb_.load(std::memory_order_relaxed)) {
        return;
    }
    lnaGainDb_.store(db, std::memory_order_relaxed);
    // §5 control-plane mapping (RX side) — auto-LNA path; same home as
    // setLnaGainDb (case 11 !XmitBit, +12 HPSDR P1 bias).  `db` clamped above.
    if (lyra::wire::prn != nullptr) {
        lyra::wire::set_rx_step_attn_db(db + 12, 0);
    }
    emit lnaGainChanged();   // slider / dB readout / S-meter comp all track
}

void HL2Stream::setAutoLna(bool on)
{
    if (on == autoLnaEnabled_) {
        return;
    }
    autoLnaEnabled_ = on;
    QSettings().setValue(QStringLiteral("rx/autoLna"), on);
    if (on) {
        overloadLevel_ = 0;
        recovering_ = false;
        holdClock_.restart();
        emit logLine(QStringLiteral("[hl2] Auto-LNA on"));
    } else {
        // Auto roamed the gain freely — hand the LNA back to the
        // operator's stored manual set point.
        const int manual = std::clamp(
            QSettings().value(QStringLiteral("rx/lnaGainDb"), 31).toInt(),
            kLnaMinDb, kLnaMaxDb);
        applyLnaGainNoPersist(manual);
        emit logLine(QStringLiteral("[hl2] Auto-LNA off, restored %1 dB").arg(manual));
    }
    emit autoLnaChanged();
}

void HL2Stream::setAutoLnaUndo(bool on)
{
    if (on == autoLnaUndo_) {
        return;
    }
    autoLnaUndo_ = on;
    QSettings().setValue(QStringLiteral("rx/autoLnaUndo"), on);
    emit autoLnaChanged();
}

void HL2Stream::setAutoLnaHoldSec(int sec)
{
    sec = std::clamp(sec, 1, 60);
    if (sec == autoLnaHoldSec_) {
        return;
    }
    autoLnaHoldSec_ = sec;
    QSettings().setValue(QStringLiteral("rx/autoLnaHoldSec"), sec);
    emit autoLnaChanged();
}

// 400 ms overload monitor + Auto-LNA loop (main thread).  Always runs
// while the stream is up so the overload lamp tracks regardless of
// Auto; gain is only touched when Auto is enabled.  Gain-sense mirror
// of the reference HERMESLITE auto-attenuator: confirm sustained
// overload (>3 cycles), back off 3 dB; when clear, creep +1 dB per
// hold interval up to the +48 ceiling (ride the overload edge).
void HL2Stream::onAutoLnaTick()
{
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    // Sample overload INSTANTANEOUSLY at poll time (most-recent
    // address-0 frame), matching the reference HL2 poll — a window-OR
    // over-triggered on a strong front end and pinned gain to the floor.
    // Stage 2b2: read adc_overload direct from telemetry struct
    // (Ep6RecvThread writes ControlBytesIn[1]&0x01 into adc[0]
    // per HL2 single-frame assignment semantics, networkproto1.c:502).
    const bool ov = (lyra::wire::prn != nullptr
                  && lyra::wire::prn->adc[0].adc_overload != 0);
    overloadLevel_ = ov ? std::min(overloadLevel_ + 1, 5)
                        : std::max(overloadLevel_ - 1, 0);
    // Lamp + back-off both key on SUSTAINED overload (level > 3 ≈ 1.6 s
    // of genuine clipping), matching the reference "red after 3 cycles".
    const bool sustained = ov && overloadLevel_ > 3;
    if (sustained != adcOverload_) {
        adcOverload_ = sustained;
        emit adcOverloadChanged();
    }
    if (!autoLnaEnabled_) {
        return;
    }

    const int g = lnaGainDb_.load(std::memory_order_relaxed);
    if (sustained) {
        // Sustained overload — back off 3 dB to protect the ADC.  Reset
        // recovery so the next pull-up waits the full hold time.
        const int ng = std::max(g - 3, kLnaMinDb);
        if (ng != g) {
            applyLnaGainNoPersist(ng);
            emit logLine(QStringLiteral("[hl2] Auto-LNA back-off → %1 dB").arg(ng));
        }
        recovering_ = false;
        holdClock_.restart();
    } else if (ov) {
        // Transient overload (not yet sustained) — hold, don't recover.
        recovering_ = false;
    } else if (autoLnaUndo_) {
        // Band clear.  The FIRST pull-up waits the operator's full hold
        // time (so a brief lull doesn't yank gain back up); subsequent
        // steps creep at the brisker recover cadence so we climb back
        // quickly.  Pull-downs above are unaffected by this.
        const qint64 need = recovering_
            ? static_cast<qint64>(kAutoLnaRecoverMs)
            : static_cast<qint64>(autoLnaHoldSec_) * 1000;
        if (holdClock_.elapsed() >= need) {
            if (g < kLnaMaxDb) {
                applyLnaGainNoPersist(g + 1);
            }
            recovering_ = true;
            holdClock_.restart();
        }
    }
}

// ── Safety-event log mirror ─────────────────────────────────────────
// emit logLine() (UI log dock) AND qInfo() / qCritical() (stderr +
// any installed file handler).  Used at the TX-safety surface so an
// operator-visible "what happened, when?" record survives a crash or
// a session where the in-app log dock was never opened.  The "[hl2]"
// tag is kept distinct from the existing "[hl2]" prefix on LNA logs
// (those stay logLine-only — not TX-safety).
void HL2Stream::safetyLog(const QString& msg) {
    emit logLine(msg);
    qInfo().noquote() << "[hl2-safety]" << msg;
}

void HL2Stream::fatalLog(const QString& msg) {
    emit logLine(msg);
    qCritical().noquote() << "[hl2-fatal]" << msg;
}

HL2Stream::~HL2Stream() {
    close();
}

void HL2Stream::open(const QString &ip) {
    if (running_.load(std::memory_order_acquire)) {
        emit logLine(QStringLiteral("open: stream already running, ignored"));
        return;
    }
    // Stage 2b2: rxWorker_ retired; Ep6RecvThread is the live EP6
    // recv path and is stopped via ep6Thread_.stop() in close().
    // (§7) txWorker_ retired with the wire-LIVE switchover — the EP2
    // writer is now the verbatim sendProtocol1Samples thread
    // (prn->hWriteThreadMain), torn down in close().
    if (socket_ != kInvalidSocket) {
        ::closesocket(static_cast<SOCKET>(socket_));
        socket_ = kInvalidSocket;
    }

    QString err;
    socket_ = openNativeUdpSocket(&err);
    if (socket_ == kInvalidSocket) {
        emit logLine(QStringLiteral("open: %1").arg(err));
        return;
    }
    const quint16 lport = localPortOf(socket_);

    targetIp_ = ip;
    // Stage 2b2: totalDg_/seqErrors_/framingErrors_/windowDg_ retired
    // from HL2Stream — Ep6RecvThread owns them as TU-scope atomics and
    // re-inits to 0 at run_loop thread entry on every start.

    // Task #48 — baseline the system-wide UDPv4 counters so the
    // close() log line can subtract and print the per-session
    // delta.  Snapshot is read-only + microsecond-cost; never
    // fails on any sane Windows install but we keep the -1
    // sentinel for paranoia / future cross-platform builds.
    {
        qint64 rx = -1, np = -1, er = -1;
        if (snapshotUdpStatsV4(&rx, &np, &er)) {
            udpStartInDatagrams_.store(rx);
            udpStartInNoPorts_.store(np);
            udpStartInErrors_.store(er);
        } else {
            udpStartInDatagrams_.store(-1);
            udpStartInNoPorts_.store(-1);
            udpStartInErrors_.store(-1);
        }
    }
    txTotalDg_.store(0);  txWindowDg_.store(0);
    txSendErrors_.store(0);
    // Stage 2b2: txSeq_ retired (metis_write_frame() owns the wire
    // sequence counter via the TU-scope MetisOutBoundSeqNum, shared
    // with the priming path for PureSignal-correct posture).
    // rx1DbFs_ retired (RX1 dBFS instrument removed per strict-
    // reference rule; WDSP audioDbFs is the reference-faithful path).
    dgPerSec_   = 0.0;
    txDgPerSec_ = 0.0;
    // cb58bcb-style come-up-not-keyed safety: every stream open starts
    // with MOX = off, FSM idle, tune disarmed.  Fixes the original
    // lock-MOX → Stop → Start → come-up-keyed bug.  PA enable is NO
    // LONGER force-cleared here (operator decision 2026-05-29): it is
    // a persistent operator preference, restored from QSettings in the
    // ctor.  PA bias alone without MOX produces no carrier — a fresh
    // open with paOn_ preserved emits the gateware bias bits but no
    // RF until the operator takes a deliberate MOX action.  Safety
    // timer + gateware watchdog cover the held-MOX and crashed-mid-
    // key cases that the original PA defensive clear was over-
    // conservatively also guarding against.
    mox_.store(false, std::memory_order_release);
    // TX-0c-tune — fresh stream always starts disarmed (tune is per-
    // session, not persisted).  DC-injection carrier means no NCO
    // state to reset beyond this flag.
    tuneEnabled_.store(false, std::memory_order_relaxed);
    requestedMox_  = false;
    fsmRunning_    = false;
    if (moxActive_) { moxActive_ = false; emit moxActiveChanged(false); }
    if (txSafetyTimer_ && txSafetyTimer_->isActive()) txSafetyTimer_->stop();
    emit tuneEnabledChanged(false);// TX panel UI follows
    // Mirror the persisted PA-enable into the UI on every open, so the
    // Settings checkbox accurately reflects the live atomic state even
    // after a previous close() that pre-2026-05-29 used to clobber it.
    // (Idempotent: setChecked checks differ-from-current before firing.)
    emit paEnabledChanged(paOn_.load(std::memory_order_relaxed));
    running_.store(true, std::memory_order_release);
    emit runningChanged();
    emit statsChanged();
    emit logLine(QStringLiteral(
        "opening EP6 stream to %1:%2 (local port %3) ...")
        .arg(ip).arg(kRadioPort).arg(lport));
    // Record the cb58bcb come-up-not-keyed defensive clears (above) so
    // a captured stderr log reconstructs "session N came up RX, MOX +
    // tune cleared, PA at <persisted-state>" without needing the in-app
    // log dock.  Per the 2026-05-29 posture relax, PA enable is a
    // persistent preference — the log records what it came up as so an
    // operator reading the captured log can tell whether the PA bias
    // would engage on the first MOX keydown.
    safetyLog(QStringLiteral(
        "come-up-not-keyed: MOX + tune force-cleared on stream open; "
        "PA enable preserved at %1")
        .arg(paOn_.load(std::memory_order_relaxed)
             ? QStringLiteral("ON")
             : QStringLiteral("off")));

    statsClock_.start();   // baseline for actual-elapsed dg/s
    // Auto-LNA / overload monitor: fresh accumulator + hold clock.
    overloadLevel_ = 0;
    recovering_ = false;
    // Stage 2b2: adcOverloadNow_ retired (Auto-LNA reads
    // prn->adc[0].adc_overload direct in onAutoLnaTick).
    holdClock_.start();
    // Stage 2b2-fix: statsTimer_/autoLnaTimer_ .start() MOVED to
    // the very END of open() (after create_rnet + ep6Thread_.start),
    // so the timer ticks can never fire before `prn` is allocated
    // and the EP6 producer is alive.  Matches reference's
    // "allocate state first, THEN start consumers" pattern at
    // StartAudioNative (netInterface.c:32-94).  The previous order
    // had a defensive nullptr guard in onAutoLnaTick that papered
    // over the race; this restructure removes the race instead.

    // Radio memory: remember this radio so the next launch can
    // auto-connect without a Discover (read in main()).
    QSettings().setValue(QStringLiteral("radio/lastIp"), ip);

    // Step 14 Stage 1 — wire-layer init.  Reference-faithful per
    // the 2026-06-06 operator audit that caught the prior
    // `radio_net()` Meyers-singleton patch + the buffer-init split
    // across three call sites.  Now: ALL allocation lives in
    // `create_rnet()` per the reference's single allocator at
    // `netInterface.c:1590-1763`; per-session work here is just
    // wire-bind + sync-flag reset.
    //
    // Reference provenance:
    //   - `create_rnet()` allocator: netInterface.c:1590-1763.
    //     P3 (2026-06-12): the call MOVED to the main.cpp QTimer
    //     block AFTER create_xmtr() — the reference's once-per-
    //     process C#-init ordering.  The per-open call that lived
    //     here re-allocated prn on every stop/start (leak +
    //     wire-state reset, contradicting the close() "prn stays
    //     alive for re-open" contract), and its new tail
    //     registration SendpOutboundTx(OutBound) derefs
    //     pcm->xmtr[0].pilv with NO null guard — it must follow
    //     create_xmtr.
    //   - `prn` non-null contract before session-open body proceeds:
    //     netInterface.c:40 (`if (... || prn == NULL) return 3;`).
    //   - `UpdateRadioProtocolSampleSize()` at session-open:
    //     netInterface.c:45 — per-protocol sample sizes + the
    //     obbuffs ring pair (ring 0 rx audio / ring 1 tx I/Q; each
    //     create starts its ob_main pump thread, blocked on its
    //     Sem_BuffReady until a producer delivers — ring 0 has no
    //     producer until P4's RX switchover; ring 1's producer is
    //     xilv in the stream-1 cm pump, quiescent until P4 feeds
    //     Inbound(1,...)).  destroy_obbuffs(0/1) at close() per
    //     StopAudio netInterface.c:112-113.
    //   - `prn->hsendEventHandles[0,1]` + `prn->hobbuffsRun[0,1]`
    //     per-session semaphore allocation (= Lyra-native
    //     bool-flag reset in `outbound_init`):
    //     netInterface.c:68-71.  The VERBATIM HANDLE quartet
    //     creation lands at P4 with the sendProtocol1Samples
    //     thread start it pairs with.
    //   - file-scope `listenSock` global bound at session-open
    //     (= TU-scope socket fd set by `metis_wire_bind`):
    //     implicit at every `sendPacket(listenSock, ...)` call site
    //     (e.g. networkproto1.c:55, 89, 234).
    {
        // Reference per-session contract (netInterface.c:40).
        if (lyra::wire::prn == nullptr) {
            qCritical("[hl2] open(): wire layer not initialized — "
                      "create_rnet() has not run (main.cpp QTimer "
                      "init incomplete?)");
            return;
        }
        lyra::wire::UpdateRadioProtocolSampleSize();  // :45 — sizes + obbuffs ring pair
        // Bind the wire socket + extract radio IPv4 (network byte
        // order) for the per-call sockaddr_in construct in
        // `send_packet` — matches reference's `MetisAddr` file-
        // scope global init at the StartMetis discovery path.
        in_addr ipv4{};
        ::inet_pton(AF_INET, ip.toLatin1().constData(), &ipv4);
        // Mirror reference's `prn->base_outbound_port` write at
        // `netInterface.c:1598` so `send_packet` reads the right
        // port (1024 default; HL2 P1 EP2 main path).
        lyra::wire::prn->base_outbound_port = kRadioPort;
        lyra::wire::metis_wire_bind(static_cast<int>(socket_),
                                    ipv4.s_addr);
        // (§7) The OutboundRing cv-translation + its per-session
        // outbound_init() reset retired with the wire-LIVE switchover:
        // the verbatim chain pairs/refills via prn->hsendLRSem /
        // hsendIQSem / hobbuffsRun[] (created in the open() quartet),
        // not the cv flags.
    }

    // ============== Stage 2b2 — Wire-LIVE migration ==============
    //
    // Reference session-open sequence (StartAudioNative,
    // netInterface.c:32-94):
    //   1. SendStartToMetis()                            netInterface.c:50
    //   2. CreateSemaphore + _beginthreadex(MetisReadThreadMain)
    //                                                    netInterface.c:60-61
    //   3. WaitForSingleObject(hReadThreadInitSem,INF)   netInterface.c:63
    //   4. CreateSemaphore + _beginthreadex(sendProtocol1Samples)
    //                                                    netInterface.c:65-66
    //   5. hsendEventHandles + hobbuffsRun semaphore set netInterface.c:68-71
    //
    // Lyra mirrors verbatim below.  The OLD rxWorker_ jthread +
    // open-time START send inside txWorkerLoop are retired.

    // (1) — hoist START from OLD txWorkerLoop:2511-2526 to caller-
    // thread context here.  Reference: SendStartToMetis at
    // netInterface.c:50, before any thread spawn.
    {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port   = htons(kRadioPort);
        ::inet_pton(AF_INET, ip.toLatin1().constData(), &dest.sin_addr);
        const QByteArray startPkt = buildControlPacket(true);
        const int sent = ::sendto(static_cast<SOCKET>(socket_),
                                  startPkt.constData(), startPkt.size(), 0,
                                  reinterpret_cast<sockaddr*>(&dest),
                                  sizeof(dest));
        if (sent != startPkt.size()) {
            const int wsaErr = ::WSAGetLastError();
            onFatalError(QStringLiteral("START: %1").arg(winsockError(wsaErr)));
            return;
        }
        emit logLine(QStringLiteral(
            "  START sent (0xEFFE 0x04 0x01 + 60 zeros), "
            "Ep6RecvThread engaging"));
    }

    // (2-3) — Wire Ep6RecvThread sinks BEFORE start().  Sink-
    // registration contract: registered ONCE, never reassigned
    // at runtime (assert(!running_) per Stage 2b2a).  Mirrors
    // reference's LoadRouterAll-once-at-session-open pattern
    // (router.c:111-143 called before _beginthreadex by C# console).
    //
    // Router sink is registered in main.cpp at WdspEngine setup
    // (so the IQ feed lambda has access to wdspEngine).  Here we
    // just point Ep6RecvThread at the router instance for dispatch.
    // Stage 2b2-fix-v2: router0_ HL2Stream member retired; the
    // Router is now process-singleton, created BEFORE HL2Stream
    // construction by main.cpp's `lyra::wire::create_router(0)`.
    // router_instance(0) returns the live Router; the lookup +
    // registry-lock cost is one-time per open() so it's a no-op
    // for steady-state RX/TX cadence.  Matches reference's
    // `prouter[0]` direct lookup at every xrouter call site.
    ep6Thread_.set_router(lyra::wire::router_instance(0), /*router_id=*/0);

    // Stage 2b2-fix-v2: HW-PTT-in is now a Qt-main-thread POLL of
    // `lyra::wire::prn->ptt_in` (the reference posture: wire side
    // writes the level unconditionally at `networkproto1.c:496`,
    // the FSM consumer reads it on its own clock and does its own
    // edge detect).  The previous wire-thread `set_hw_ptt_sink`
    // push-callback with `thread_local` edge-detect was a Lyra-
    // native idiom translation that the operator reference-verify
    // pass flagged as a deviation; replaced with `hwPttTimer_`
    // (started below at the end of open() alongside autoLnaTimer_)
    // which fires `onHwPttPoll()` at ~50 ms cadence on the Qt
    // main thread, mirroring the reference's
    // "consumer-polls-prn->ptt_in" shape.

    // (3) — Ep6RecvThread spawn + init-sem handshake.  start()
    // blocks until run_loop releases hReadThreadInitSem (per
    // Stage 2a: AFTER MMCSS classification, BEFORE priming +
    // WSAEventSelect — matches reference MetisReadThreadMain
    // release point at networkproto1.c:249).  Priming pass
    // (ForceCandCFrame(3) + 6 EP2 datagrams) runs async on the
    // EP6 thread after start() returns — same parallel-spawn
    // posture as reference (sendProtocol1Samples at :66 spawns
    // before the EP6 thread's priming completes).

    // §5 control-plane mapping (RX side) — at-open seed.  The verbatim
    // C&C composer (FrameComposer::write_main_loop_hl2) reads `prn` /
    // `SampleRateIn2Bits`, NOT the hl2_stream legacy atomics that the
    // retired composeCC read.  Seed those homes from the current
    // persisted operator state HERE — before ep6Thread_.start() kicks
    // off the priming pass (ForceCandCFrame reads prn->rx[0].frequency)
    // and before the writer's first compose — so RX tunes correctly
    // from the first frame instead of DDC0 sitting at 0 Hz until the
    // operator first touches the dial (dead-RX, operator-confirmed
    // 2026-06-13).  prn is guaranteed non-null here (the open() guard at
    // the top returns if create_rnet hasn't run).  Each operator setter
    // also writes its home on change (setRx1FreqHz / setLnaGainDb /
    // setSampleRate / updateOcPattern); this is the open-time snapshot.
    // Direct prn writes via the FrameComposer setters match the
    // reference (Console writes prn->rx[] from the control thread) + the
    // shipped AttOnTxPolicy precedent — no lock, no command queue.
    {
        const int rxHz =
            static_cast<int>(rx1FreqHz_.load(std::memory_order_relaxed));
        lyra::wire::set_rx_freq(0, rxHz);   // DDC0 RX1 (case 2/8/9)
        lyra::wire::set_rx_freq(1, rxHz);   // DDC1 (case 3 — RX1-mirror until RX2)
        lyra::wire::set_tx_freq(            // TX NCO + DDC2/3 mirror (case 1/5/6)
            txDdsHzForTune(txFreqHz_.load(std::memory_order_relaxed)));  // #105 CW carrier offset (carrier, not DDS)
        lyra::wire::set_rx_step_attn_db(    // LNA gain (case 11 !XmitBit)
            std::clamp(lnaGainDb_.load(std::memory_order_relaxed),
                       kLnaMinDb, kLnaMaxDb) + 12, 0);   // HPSDR P1 +12 bias
        lyra::wire::SampleRateIn2Bits =     // sample-rate code (case 0 C1)
            sampleRateBits_.load(std::memory_order_relaxed);
        lyra::wire::prn->oc_output = ocPattern_;  // OC pins (case 0 C2)

        // §5 control-plane mapping (TX side) — seed the TX homes too so
        // the first composed frame carries the operator's persisted TX
        // state (PA defaults OFF; drive default low; XmitBit 0 at open =
        // RX).  TX-inert at RX (gateware acts on these only during a
        // keyed MOX), so this does not change RX behaviour — it just
        // means the first keydown finds correct drive/PA/att already on
        // the wire rather than create-time defaults.
        {
            const int seedDrive = std::min(maxDriveRaw(),  // #170a drive cap
                static_cast<int>(txDriveLevel_.load(std::memory_order_relaxed)));
            applyTxPower_(seedDrive);   // Stage 3 — seed byte (case 10 C1) + txgain at TX-up
        }
        lyra::wire::set_pa_on(                   // PA enable (case 10 C2/C3)
            paOn_.load(std::memory_order_relaxed));
        lyra::wire::set_tx_step_attn_db(         // TX step-att (case 4/11)
            txStepAttnDb_.load(std::memory_order_relaxed));
        lyra::wire::XmitBit =                    // wire MOX gate (0 = RX at open)
            mox_.load(std::memory_order_relaxed) ? 1 : 0;
        // #105 CW-1a — seed prn->cw from the autoloaded operator config
        // (cases 12/13/14 read it).
        applyCwConfigToPrn();
        // #105 CW-2 — arm the firmware CW keyer if the radio came up in CW
        // (Thetis EnableCWKeyer): cw_enable follows txMode_.  Non-CW at open
        // → stays 0 (TX-inert).  The keyer config above is dormant until set.
        applyCwKeyerEnable();
    }

    ep6Thread_.start(static_cast<int>(socket_));

    // (4) — EP2 writer thread (sendProtocol1Samples).  THE wire path
    // now (P4.b switchover) — replaces the OLD txWorker_ jthread
    // (txWorkerLoop, retired below).  VERBATIM reference order,
    // StartAudioNative netInterface.c:65-71:
    //   prn->hWriteThreadInitSem = CreateSemaphore(..);     // (see note)
    //   prn->hWriteThreadMain    = _beginthreadex(.., sendProtocol1Samples, ..);
    //   prn->hsendEventHandles[0] = (prn->hsendLRSem = CreateSemaphore(..));
    //   prn->hsendEventHandles[1] = (prn->hsendIQSem = CreateSemaphore(..));
    //   prn->hobbuffsRun[0] = CreateSemaphore(..);
    //   prn->hobbuffsRun[1] = CreateSemaphore(..);
    //
    // Thread-FIRST then the four sems — exactly as the reference.  (An
    // earlier pass reordered sems-before-thread "to remove a startup
    // race"; reverted — that is the "I have a reason to differ" trap
    // the DESIGN PRINCIPLE forbids.  The reference's order is safe in
    // practice: the new thread's AvSetMmThreadCharacteristics setup
    // gives this creating thread a large head start to finish the four
    // CreateSemaphore calls before the writer first reaches
    // WaitForMultipleObjects.  Decade-proven in Thetis; faithful port
    // protects PureSignal correctness, which depends on byte-faithful
    // wire behaviour.)
    //
    // hWriteThreadInitSem (reference netInterface.c:65): a
    // create-but-never-waited-on init sem.  In Lyra the thread-init-sem
    // MECHANISM is the Lyra-native std::counting_semaphore member
    // (RadioNet.h:605) — the same sanctioned surrounding-architecture
    // translation already used for hReadThreadInitSem; it is
    // default-constructed with prn and (matching the reference)
    // nothing waits on it, so there is no Win32 creation line to port.
    //
    // Packaging accommodation (NOT behavioral): the reference passes
    // sendProtocol1Samples directly to _beginthreadex (C); C++ requires
    // the _beginthreadex_proc_type cast — same documented class as the
    // (AVRT_PRIORITY)2 cast in NetworkProto1.cpp.
    //
    // io_keep_running == 1 is already guaranteed here: ep6Thread_.start()
    // above blocked on the read thread's init-sem, which Ep6RecvThread
    // releases only AFTER setting io_keep_running = 1 (mirrors
    // MetisReadThreadMain networkproto1.c:246).  The writer's
    // `while (io_keep_running != 0)` loop then blocks on BOTH hsend
    // sems until the RX OutBound(0) chain (hsendLRSem) AND the TX
    // OutBound(1) chain (hsendIQSem) each deliver a buffer (the §1
    // pacing model — no fresh data ⇒ writer blocks ⇒ silent wire, the
    // reference stuck-carrier safety).
    lyra::wire::prn->hWriteThreadMain = reinterpret_cast<HANDLE>(_beginthreadex(
        nullptr, 0,
        reinterpret_cast<_beginthreadex_proc_type>(&lyra::wire::sendProtocol1Samples),
        nullptr, 0, nullptr));
    lyra::wire::prn->hsendEventHandles[0] =
        (lyra::wire::prn->hsendLRSem = ::CreateSemaphore(nullptr, 0, 1, nullptr));
    lyra::wire::prn->hsendEventHandles[1] =
        (lyra::wire::prn->hsendIQSem = ::CreateSemaphore(nullptr, 0, 1, nullptr));
    lyra::wire::prn->hobbuffsRun[0] = ::CreateSemaphore(nullptr, 0, 1, nullptr);
    lyra::wire::prn->hobbuffsRun[1] = ::CreateSemaphore(nullptr, 0, 1, nullptr);

    // Stage 2b2-fix: timer consumers START LAST — only after
    // create_rnet() above has allocated `prn` and the EP6/TX
    // workers are alive.  Reference posture: state-allocated +
    // producers-running BEFORE any state-reading consumer ticks.
    // Removes the prior race window where autoLnaTimer fired
    // before `prn` existed (the defensive nullptr guard in
    // onAutoLnaTick is now structurally unreachable, not just
    // unobservable).
    statsTimer_.start();
    autoLnaTimer_.start();
    // Stage 2b2-fix-v2: HW-PTT poll timer starts last too — same
    // "wait until prn is allocated and EP6 producer is up" rule.
    // Reset lastHwPttLevel_ to the current wire state so the first
    // tick after open() doesn't fire a spurious edge on whatever
    // level the radio happens to be sending at session start.
    lastHwPttLevel_ = (lyra::wire::prn != nullptr
                    && lyra::wire::prn->ptt_in != 0);
    hwPttTimer_.start();
}

void HL2Stream::close() {
    // Task #40 — TX-triggered zombie shutdown investigation.  qWarning
    // brackets so the next bench shows in lyra-log.txt which step (if
    // any) wedged: rxWorker_.join, txWorker_.join, sendto STOP, or the
    // closesocket itself.
    qWarning("[shutdown] HL2Stream::close ENTRY (running=%d ep6Alive=%d sockOpen=%d)",
             running_.load() ? 1 : 0,
             ep6Thread_.running() ? 1 : 0,
             socket_ != kInvalidSocket ? 1 : 0);
    if (!running_.load(std::memory_order_acquire) &&
        !ep6Thread_.running() &&
        socket_ == kInvalidSocket) {
        qWarning("[shutdown] HL2Stream::close NO-OP (nothing alive)");
        return;
    }
    emit logLine(QStringLiteral("closing EP6 stream ..."));
    // #105 CW-3a — quiesce the CW keyer BEFORE prn teardown so its
    // element-pump thread stops touching tx[0].cwx/cwx_ptt (the setters
    // guard prn-null, but abort() ends any in-flight message first).
    if (cwKeyer_) cwKeyer_->abort();
    // #105 — clear the UI CW keyed state directly (the keyer's onState
    // marshals via a queued invoke that may not run during teardown), so
    // the meter never sticks in TX after a Stop mid-CW-message.
    setCwKeyingActive(false);
    // Record the force-release-all (below) so a captured stderr log
    // shows "session N ended RX, MOX/tune dropped before the workers
    // were joined."  PA enable is preserved (operator preference) and
    // the gateware watchdog drops PA bias ~13 s after the EP2 stream
    // stops, so this is not a stuck-on-bias concern.
    safetyLog(QStringLiteral(
        "force-release-all: MOX + tune cleared before workers stop "
        "(PA preserved; gateware watchdog drops bias on EP2 timeout)"));

    // Force MOX off + tune off BEFORE the workers die so the final
    // EP2 frames carry MOX=0 + step-att restored + TX I/Q silent — no
    // stuck carrier even if the operator was mid-key when Stop was
    // clicked.  PA enable is intentionally NOT touched here (2026-05-
    // 29 posture relax — it's a persistent operator preference; bias
    // alone without MOX produces no carrier, and the gateware watchdog
    // drops bias on EP2 timeout if the operator wants the radio fully
    // safed they uncheck PA in Settings).  mox_ atomic is set directly
    // here (bypassing the FSM's TR-delay chain) because the workers
    // are about to die — there's no time for a graceful keyup.
    mox_.store(false, std::memory_order_release);
    // TX-0c-tune — force-disarm in lockstep with the MOX clear so the
    // final EP2 frames carry silent TX I/Q.
    tuneEnabled_.store(false, std::memory_order_relaxed);
    requestedMox_  = false;
    fsmRunning_    = false;
    if (moxActive_) {
        moxActive_ = false;
        emit moxActiveChanged(false);
        // OC pattern back to RX: forced close mid-TX would otherwise
        // leave the filter board in TX configuration after stop.  The
        // final EP2 frame carries the RX pattern alongside MOX=0 + PA
        // disarmed — symmetric with the FSM keyup restore in
        // fsmKeyupSettled.
        updateOcPattern(/*transmitting=*/false);
    }
    if (txSafetyTimer_ && txSafetyTimer_->isActive()) txSafetyTimer_->stop();
    emit tuneEnabledChanged(false);// TX panel UI follows
    // paEnabledChanged is intentionally NOT emitted here — paOn_ is
    // not touched in close() (per the 2026-05-29 persistence posture),
    // so the Settings checkbox stays at the operator's last explicit
    // setting across a Stop → next Start cycle.

    // ============== P4.b — EP2 writer teardown ==============
    //
    // Reference IOThreadStop (network.c:1434-1469) + StopAudio
    // (netInterface.c:96-114): io_keep_running=0 → wait the READ thread
    // → CloseHandle(hWriteThreadMain) + the 4 P1 sems → [StopAudio]
    // destroy_obbuffs(0/1).
    //
    // ⚠ DOCUMENTED LYRA DEVIATION (the ONLY P4.b deviation — operator-
    // approved 2026-06-13; design §6; rule #2 surrounding-architecture):
    // the reference does NOT wake or join sendProtocol1Samples — it
    // CloseHandle's the writer while it is still parked in
    // WaitForMultipleObjects(hsendEventHandles, INFINITE) and relies on
    // process-exit to reap it (a CloseHandle-while-waiting = documented
    // Win32 UB; the MW0LGE :1456 comment shows they fought crashes in
    // this very block).  VERIFIED 2026-06-13: the only
    // ReleaseSemaphore(hsend*) sites in the reference are inside
    // sendOutbound (network.c:1311-1336, the runtime producer path) —
    // there is NO shutdown-path wake anywhere in ChannelMaster, so the
    // writer is deterministically orphaned at stop.  Lyra must stop/
    // start cleanly (cb58bcb class; bench gate), so we WAKE the writer
    // (release both hsend sems once) → it observes io_keep_running=0 →
    // exits → then JOIN + CloseHandle.
    //
    // PURESIGNAL-SAFE: this runs ONLY at close(); it changes no WDSP
    // call, no wire byte, no buffer (outLRbufp/outIQbufp/OutBufp), no
    // MetisOutBoundSeqNum, and does NOT touch sendOutbound's runtime
    // sem-release path (the verbatim P2.c port).  It only governs WHEN
    // the writer THREAD stops — Lyra-native thread/process model.
    // create_xmtr's PS surface (out[3]/peer/…) is NOT torn down here
    // (that's app-quit destroy_xmtr), so it survives stop/start intact.
    lyra::wire::io_keep_running = 0;                  // ref network.c:1438
    qWarning("[shutdown] HL2Stream::close ep6Thread_.stop() - start");
    ep6Thread_.stop();   // ref: wait the read thread (producers quiesce; ob_main parks on Sem_BuffReady)
    qWarning("[shutdown] HL2Stream::close ep6Thread_.stop() - done");
    if (lyra::wire::prn != nullptr) {
        // Wake the parked writer (the accommodation the reference omits)
        // so it exits rather than leaking on a closed handle.  One final
        // EP2 frame may be emitted before it re-tests the flag — benign:
        // MOX was force-cleared above ⇒ !XmitBit ⇒ zeroed TX I/Q.
        if (lyra::wire::prn->hsendLRSem)
            ::ReleaseSemaphore(lyra::wire::prn->hsendLRSem, 1, nullptr);
        if (lyra::wire::prn->hsendIQSem)
            ::ReleaseSemaphore(lyra::wire::prn->hsendIQSem, 1, nullptr);
        qWarning("[shutdown] HL2Stream::close EP2 writer join - start");
        if (lyra::wire::prn->hWriteThreadMain) {
            ::WaitForSingleObject(lyra::wire::prn->hWriteThreadMain, 1000);
            ::CloseHandle(lyra::wire::prn->hWriteThreadMain);   // ref network.c:1462
            lyra::wire::prn->hWriteThreadMain = nullptr;
        }
        qWarning("[shutdown] HL2Stream::close EP2 writer join - done");
        // Close the 4 P1 sems (ref network.c:1464-1467).  Safe here —
        // BEFORE destroy_obbuffs as in the reference — because
        // ep6Thread_.stop() + the writer join above quiesced every
        // producer, so ob_main is parked on Sem_BuffReady and will not
        // ReleaseSemaphore a closed handle.  Null-cleared for a clean
        // re-open (open() recreates them).
        if (lyra::wire::prn->hsendIQSem)     { ::CloseHandle(lyra::wire::prn->hsendIQSem);     lyra::wire::prn->hsendIQSem = nullptr; }
        if (lyra::wire::prn->hsendLRSem)     { ::CloseHandle(lyra::wire::prn->hsendLRSem);     lyra::wire::prn->hsendLRSem = nullptr; }
        if (lyra::wire::prn->hobbuffsRun[0]) { ::CloseHandle(lyra::wire::prn->hobbuffsRun[0]); lyra::wire::prn->hobbuffsRun[0] = nullptr; }
        if (lyra::wire::prn->hobbuffsRun[1]) { ::CloseHandle(lyra::wire::prn->hobbuffsRun[1]); lyra::wire::prn->hobbuffsRun[1] = nullptr; }
        lyra::wire::prn->hsendEventHandles[0] = nullptr;
        lyra::wire::prn->hsendEventHandles[1] = nullptr;
    }

    // Both workers stopped — main thread sends STOP, then closes
    // the socket.  Best-effort; if STOP doesn't reach the gateware
    // its own watchdog will idle the stream within ~13 sec anyway.
    if (socket_ != kInvalidSocket) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port   = htons(kRadioPort);
        ::inet_pton(AF_INET,
                    targetIp_.toLatin1().constData(),
                    &dest.sin_addr);
        const QByteArray stopPkt = buildControlPacket(false);
        ::sendto(static_cast<SOCKET>(socket_),
                 stopPkt.constData(), stopPkt.size(), 0,
                 reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        ::closesocket(static_cast<SOCKET>(socket_));
        socket_ = kInvalidSocket;
    }

    // Step 14 Stage 1 — wire-layer bind teardown.  Clear the TU-scope
    // socket fd + dest pointer so any post-close call into
    // metis_write_frame() fails fast (sendto on -1) instead of writing
    // to a stale closed handle.  prn singleton stays alive — the
    // reference's prn is also non-null between session-close and the
    // next session-open (it points at a static _radionet struct), so
    // operator can re-open cleanly without re-allocating.
    lyra::wire::metis_wire_bind(-1, 0);

    // P3 (2026-06-12) — reference StopAudio (netInterface.c:112-113):
    // tear down the per-session obbuffs ring pair created by
    // UpdateRadioProtocolSampleSize at open().  destroy_obbuffs'
    // verbatim `obp.pcbuff[0] == NULL` guard covers close-before-
    // first-open; close()'s own NO-OP early-return above prevents a
    // double-close double-free (the reference does not clear the
    // obp aliases after free — P1 verbatim).
    lyra::wire::destroy_obbuffs(0);
    lyra::wire::destroy_obbuffs(1);

    statsTimer_.stop();
    autoLnaTimer_.stop();
    hwPttTimer_.stop();   // Stage 2b2-fix-v2 — HW-PTT poll
    if (adcOverload_) { adcOverload_ = false; emit adcOverloadChanged(); }
    onStatsTick();  // flush the final window so the UI shows true totals
    running_.store(false, std::memory_order_release);
    emit runningChanged();
    emit logLine(QStringLiteral(
        "stream closed: RX %1 dg (%2 seq errs, %3 framing), "
        "TX %4 keepalives (%5 send errors)")
        .arg(lyra::wire::ep6_total_datagrams())
        .arg(lyra::wire::ep6_seq_errors())
        .arg(lyra::wire::ep6_framing_errors())
        .arg(txTotalDg_.load())
        .arg(txSendErrors_.load()));

    // Task #48 — Windows system-wide UDPv4 delta for this session.
    // Lets the operator (and us) tell whether seq-error drops are
    // happening in the kernel UDP layer (Δ udpInErrors > 0 → Lyra
    // RX thread stalled, rcvbuf overflowed) or upstream of the
    // kernel (Δ udpInErrors == 0 while seqErrors > 0 → drops are
    // in the NIC ring / driver / hardware — operator-side fix via
    // Device Manager → NIC → Advanced → Receive Buffers ↑ +
    // Interrupt Moderation off).  Counters are SYSTEM-WIDE; other
    // UDP apps (DNS, mDNS, browser) inflate the totals — interpret
    // alongside Lyra-side numbers, not in isolation.
    {
        const qint64 startRx = udpStartInDatagrams_.load();
        const qint64 startNp = udpStartInNoPorts_.load();
        const qint64 startEr = udpStartInErrors_.load();
        qint64 endRx = -1, endNp = -1, endEr = -1;
        const bool ok = snapshotUdpStatsV4(&endRx, &endNp, &endEr);
        if (ok && startRx >= 0 && startNp >= 0 && startEr >= 0) {
            const qint64 dRx = endRx - startRx;
            const qint64 dNp = endNp - startNp;
            const qint64 dEr = endEr - startEr;
            const qint64 lyraSeq = lyra::wire::ep6_seq_errors();
            emit logLine(QStringLiteral(
                "udp4 stats Δ (system-wide): inDatagrams=%1  "
                "noPorts=%2  inErrors=%3   (vs Lyra seqErrors=%4)")
                .arg(dRx).arg(dNp).arg(dEr)
                .arg(lyraSeq));
            // Mirror to qInfo so it lands in the captured stderr
            // log too (the operator's lyra-log.txt path).
            qInfo("[udp4 stats] Δ inDatagrams=%lld noPorts=%lld "
                  "inErrors=%lld  (vs Lyra seqErrors=%lld)",
                  static_cast<long long>(dRx),
                  static_cast<long long>(dNp),
                  static_cast<long long>(dEr),
                  static_cast<long long>(lyraSeq));
        } else {
            emit logLine(QStringLiteral(
                "udp4 stats Δ: snapshot unavailable "
                "(start=%1/%2/%3 end ok=%4)")
                .arg(startRx).arg(startNp).arg(startEr)
                .arg(ok ? 1 : 0));
        }
    }
    qWarning("[shutdown] HL2Stream::close EXIT");
}

void HL2Stream::onStatsTick() {
    const qint64 rxWin = lyra::wire::ep6_drain_window_datagrams();
    const qint64 txWin = txWindowDg_.exchange(0, std::memory_order_acq_rel);
    // Divide by the ACTUAL elapsed interval (not the assumed period),
    // so timer jitter from main-thread load doesn't inflate the rate.
    qint64 elapsedMs = statsClock_.restart();
    if (elapsedMs <= 0) {
        elapsedMs = kStatPeriodMs;
    }
    const double scale = 1000.0 / static_cast<double>(elapsedMs);
    dgPerSec_   = static_cast<double>(rxWin) * scale;
    txDgPerSec_ = static_cast<double>(txWin) * scale;
    emit statsChanged();

    // TX-0a raw-telemetry probe — env-gated, 5 Hz, main thread.  Logs
    // the decoded raw ADC slots + converted values so the operator can
    // confirm/correct the gateware-rev-specific AINx→slot map against a
    // known reference on the bench (verify-don't-guess).
    static const bool kTelemDebug =
        qEnvironmentVariableIsSet("LYRA_TELEM_DEBUG") &&
        qgetenv("LYRA_TELEM_DEBUG") != "0";
    if (kTelemDebug && running_.load(std::memory_order_relaxed)) {
        // qWarning (not the logLine signal) so it lands in LogBuffer →
        // View → Log + the log file regardless of verbose, exactly like
        // the [wx-diag] diagnostics.  (A windowed build has no console
        // stdout, so a terminal will never show it.)
        // Raw-rotation dump so the ak4951v4 slot map can be read off the
        // operator's hardware.  a0/a8/a10 show both raw 16-bit BE pairs
        // Diagnostic dump of the reference EP6 slot rotation
        // (networkproto1.c:498-525).  Slot 0x18 carries no useful
        // data per gateware (`_hl2src/hl2_rtl_control.v:475`).  HL2
        // reinterpretation per `console.cs:24937-24941`: a8 C1:C2 is
        // temperature, a10 C3:C4 is PA current.
        // Stage 2b2: raw-int log reads from prn->... direct (the
        // single telemetry state of record after the Ep6RecvThread
        // migration; matches reference posture of int=0-until-first-
        // telemetry-frame).
        const int rxc = (lyra::wire::prn != nullptr)
            ? lyra::wire::prn->tx[0].exciter_power : 0;
        const int rfp = (lyra::wire::prn != nullptr)
            ? lyra::wire::prn->tx[0].fwd_power : 0;
        const int rrp = (lyra::wire::prn != nullptr)
            ? lyra::wire::prn->tx[0].rev_power : 0;
        const int rua = (lyra::wire::prn != nullptr)
            ? lyra::wire::prn->user_adc0 : 0;
        const QString s = QStringLiteral(
            "[telem] a8=(%1,%2) a10=(%3,%4) | T=%5C PA=%6A")
            .arg(rxc).arg(rfp).arg(rrp).arg(rua)
            .arg(hl2TempC(), 0, 'f', 1)
            .arg(paCurrentA(), 0, 'f', 2);
        qWarning("%s", qUtf8Printable(s));
    }
}

void HL2Stream::onFatalError(QString reason) {
    fatalLog(QStringLiteral("FATAL: %1").arg(reason));
    close();
}

// ---- TX-0a: HL2 telemetry getters (main thread) ------------------
// Convert the raw 12-bit EP6 ADC slots written by the RX worker.
// Formulas per the documented HL2 status rotation; the AINx→slot map
// and these scale constants are gateware-rev-specific on HL2+ and MUST
// be bench-verified (LYRA_TELEM_DEBUG=1 logs the raw rotation).  A
// known-good HL2+ reads ≈12-13 V supply/VDD, board-ambient temp, and
// (during full tune into a dummy) ≈1.8 A PA current.
namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
}
double HL2Stream::hl2TempC() const {
    // HL2 board temperature.  Reference HL2 path (`console.cs:
    // 24937-24941`) reinterprets the slot-0x08 C1:C2 byte (which the
    // reference C atomic calls `tx[0].exciter_power`) as raw temp on
    // HL2.  Formula per `console.cs:25079`:
    //   T_C = (3.26 * raw/4096 - 0.5) / 0.01
    // Gateware confirms (Y:/Claude local/_hl2src/hl2_rtl_control.v:
    // 473): `2'b01: iresp <= {... 4'h0, temperature, 4'h0, fwd_pwr}`
    // — slot 0x08 C1:C2 carries the on-board temp sensor (MCP9700,
    // 10 mV/°C, 0.5 V @ 0°C).
    //
    // Stage 2b2: read from `prn->tx[0].exciter_power` direct, mirroring
    // reference's read-_radionet-field pattern (matches reference's
    // `prn->tx[0].exciter_power = ...` write at networkproto1.c:506).
    // Wire-INERT pre-Stage-2b2; LIVE now that Ep6RecvThread::start runs.
    // Lyra-native -1 sentinel removed — reference reads int=0 at startup
    // until first telemetry frame populates the field; HL2 banner shows
    // briefly cold (~-50°C) for ~26ms post-open, matches reference UX.
    if (lyra::wire::prn == nullptr) return kNaN;
    const int raw = lyra::wire::prn->tx[0].exciter_power;
    return (3.26 * (raw / 4096.0) - 0.5) / 0.01;
}
double HL2Stream::hl2SupplyV() const {
    // Reference does NOT display supply voltage on HL2 per
    // `console.cs:26758-26761`: the status-bar "Volts" label slot is
    // reused to show temperature with a "C" suffix.  The C# accessor
    // `_MKIIPAVolts` is fed from `_voltsQueue` which is NEVER enqueued
    // on HL2 (`console.cs:24937-24941` HL2 branch only enqueues amps +
    // temp, not volts).  The HL2 gateware does not place supply in
    // the iresp 4-slot rotation either (`_hl2src/hl2_rtl_control.v:
    // 471-476` — slot 0x00 carries dsiq_status+VERSION_MAJOR; slot
    // 0x18 is "Unused in HL").
    //
    // Lyra mirrors this: V returns NaN on HL2; UI shows "n/a".  If
    // supply telemetry is wanted later, it has to come from a separate
    // I2C readback transaction (separate work item, not §3.9 / not
    // EP6).
    return kNaN;
}
double HL2Stream::paCurrentA() const {
    // HL2 PA bias current.  Reference HL2 path (`console.cs:24937-
    // 24941`) reinterprets the slot-0x10 C3:C4 byte (which the
    // reference C atomic calls `user_adc0`) as raw PA current on HL2.
    // Formula per `console.cs:25121-25131`:
    //   amps = ((3.26 * raw/4096) / 50) / 0.04 / (1000/1270)
    // Gateware confirms (`_hl2src/hl2_rtl_control.v:474`): `2'b10:
    // iresp <= {... 4'h0, rev_pwr, 4'h0, bias_current}` — slot 0x10
    // C3:C4 carries `bias_current` from the slow_adc ain2 channel.
    //
    // Stage 2b2: read `prn->user_adc0` direct (Ep6RecvThread writes
    // this from slot 0x10 C3:C4 per networkproto1.c:513).
    if (lyra::wire::prn == nullptr) return kNaN;
    const int raw = lyra::wire::prn->user_adc0;
    return ((3.26 * (raw / 4096.0)) / 50.0) / 0.04 / (1000.0 / 1270.0);
}
double HL2Stream::fwdPowerW() const {
    // Stage 2b2: read `prn->tx[0].fwd_power` direct (Ep6RecvThread
    // writes from slot 0x08 C3:C4 per networkproto1.c:507).
    if (lyra::wire::prn == nullptr) return kNaN;
    const int raw = lyra::wire::prn->tx[0].fwd_power;
    // Provisional + UNCALIBRATED — real watts need the per-band 3-point
    // forward-power cal (a later TX-3 step).
    const double v = (raw - 6.0) / 4095.0 * 3.3;
    return (v > 0.0) ? (v * v) / 1.5 : 0.0;
}
double HL2Stream::revPowerW() const {
    // Stage 2b2: read `prn->tx[0].rev_power` direct (Ep6RecvThread
    // writes from slot 0x10 C1:C2 per networkproto1.c:511).
    if (lyra::wire::prn == nullptr) return kNaN;
    const int raw = lyra::wire::prn->tx[0].rev_power;
    const double v = (raw - 6.0) / 4095.0 * 3.3;
    return (v > 0.0) ? (v * v) / 1.5 : 0.0;
}
double HL2Stream::fwdPowerCalW() const {
    // Raw formula watts × the current TX band's PWR-meter trim.  This is the
    // ONE calibrated watts value shared by the meter display and the watts
    // cap, so a "5 W" cap lands where the meter reads 5 W.  Band from the TX
    // freq (== RX freq in simplex).  No band / no trim -> raw.
    const double raw = fwdPowerW();
    if (std::isnan(raw)) return raw;
    const int band = lyra::bandIndexForFreq(
        static_cast<int>(txFreqHz_.load(std::memory_order_relaxed)));
    const double t = (band >= 0 && band < kNumPaGainBands)
                         ? pwrTrimByBand_[band].load(std::memory_order_relaxed)
                         : 1.0;
    return raw * t;
}
double HL2Stream::pwrTrimForBand(int idx) const {
    return (idx >= 0 && idx < kNumPaGainBands)
               ? pwrTrimByBand_[idx].load(std::memory_order_relaxed) : 1.0;
}
void HL2Stream::setPwrTrimForBand(int idx, double scale) {
    if (idx < 0 || idx >= kNumPaGainBands) return;
    scale = std::clamp(scale, 0.1, 10.0);
    pwrTrimByBand_[idx].store(scale, std::memory_order_relaxed);
    QSettings().setValue(QStringLiteral("meter/pwrTrim/%1").arg(idx), scale);
}

// ----------------------------------------------------------------
// Stage 2b2 (operator-locked 2026-06-07, "do as the reference does
// — period"): HL2Stream::rxWorkerLoop body retired.  The EP6 recv
// path is now `wire::Ep6RecvThread` (started from HL2Stream::open
// — see ep6Thread_.start() call site).  Per-DDC IQ dispatch goes
// through `wire::Router::xrouter` to whichever consumer main.cpp
// registered (WDSP feed via lyra::wire::register_sink at app boot).
// HW PTT-in level forwards via the `set_hw_ptt_sink` callback wired
// in HL2Stream::open (edge-detect lambda-local; reference posture =
// no wire-side edge state, `prn->ptt_in` is a single-bit level).
// Telemetry, mic decimation, ADC overload, seq/total/framing/window
// counters all live in `wire::Ep6RecvThread` + `wire/RadioNet` +
// TU-scope statics — single source of truth per reference's
// _radionet + file-scope-global pattern (network.h / networkproto1.c).
// ----------------------------------------------------------------

void HL2Stream::setRx1FreqHz(quint32 hz) {
    const quint32 prev = rx1FreqHz_.exchange(hz, std::memory_order_relaxed);
    if (prev != hz) {
        QSettings().setValue(QStringLiteral("rx/freqHz"), hz);
        emit rx1FreqChanged();
        emit logLine(QStringLiteral("RX1 -> %1 Hz (%2 MHz)")
                     .arg(hz).arg(hz / 1.0e6, 0, 'f', 6));
        // Band may have changed -> re-apply the filter-board OC pattern.
        updateOcPattern();
        // §5 control-plane mapping (RX side): the RX DDCs ALWAYS follow
        // VFO A — RX stays on VFO A even in SPLIT.  pushEffectiveRxFreq is
        // the single RX-NCO writer (RIT-adjusted): it tunes the DDCs from
        // rx1FreqHz_ + the RIT offset.  It guards the pre-open window
        // internally (a tune gesture before the wire layer is up is
        // captured by the at-open seed in open()).
        pushEffectiveRxFreq();
        // TX-0c-tune — simplex: TX follows VFO A.  In SPLIT, VFO B owns the
        // TX freq, so an RX1 dial gesture must NOT move TX — gate the
        // mirror on !split.  pushEffectiveTxFreq() is the SINGLE TX-freq
        // writer (PS-safe): it stores txFreqHz_ + pushes set_tx_freq
        // (TX NCO + the DDC2/3 PS-feedback regs).  setRx1FreqHz is the only
        // operator-facing tuner, so this captures every dial gesture, band
        // button, memory recall, TCI spot click, etc.
        if (!splitEnabled_.load(std::memory_order_relaxed))
            pushEffectiveTxFreq();
        else
            // SPLIT: VFO A (the panadapter centre) moved but the TX freq
            // (VFO B) didn't — the TX-analyzer crop offset still shifted, so
            // refresh it (pushEffectiveTxFreq isn't called on this path).
            emit txAnalyzerOffsetChanged(txAnalyzerOffsetHz());
    }
}

double HL2Stream::freqCorrection() const {
    return freqCorrection_.load(std::memory_order_relaxed);
}

void HL2Stream::setFreqCorrection(double factor) {
    // Clamp to ±1000 ppm.  A real HL2 TCXO is well under ±2 ppm; this only
    // bounds fat-fingered manual entry (and NaN) without blocking any
    // legitimate value.  (Reset uses exactly 1.0 → the byte-exact no-op.)
    if (!(factor >= 0.999 && factor <= 1.001)) {
        if (factor < 0.999)      factor = 0.999;
        else if (factor > 1.001) factor = 1.001;
        else                     factor = 1.0;   // NaN → neutral
    }
    const double old = freqCorrection_.exchange(factor, std::memory_order_relaxed);
    if (old == factor)
        return;
    lyra::wire::set_freq_correction(factor);
    QSettings s;
    // Snapshot the prior value for one-level "Restore previous" undo.
    s.setValue(QStringLiteral("cal/freqCorrectionPrev"), old);
    s.setValue(QStringLiteral("cal/freqCorrection"), factor);
    // Re-tune so the change takes effect immediately — mirror of the
    // reference's FreqCorrectionChanged re-applying to every VFO.
    pushEffectiveRxFreq();
    pushEffectiveTxFreq();
    emit freqCorrectionChanged(factor);
}

void HL2Stream::pushEffectiveTxFreq() {
    // The ONE TX-freq writer for the split-aware paths.  Effective TX freq
    // = VFO B when split is on, else VFO A (rx1).  Stores txFreqHz_ and
    // pushes set_tx_freq, which writes 0x02 (TX NCO) + 0x08/0x0a (DDC2/3 =
    // the PureSignal feedback DDCs) all to this one freq — so PS samples
    // the split VFO with no extra work (reference: tx[0].frequency drives
    // both the NCO and the PS-feedback DDCs, networkproto1.c cases 1/5/6).
    // XIT folds in here: TX = (split ? VFO B : VFO A) + xit offset.  Because
    // this is the ONE writer of the TX NCO + the DDC2/3 PS-feedback regs,
    // PureSignal samples the XIT-shifted TX automatically (no extra work).
    const qint64 base = splitEnabled_.load(std::memory_order_relaxed)
                            ? vfoBHz_.load(std::memory_order_relaxed)
                            : rx1FreqHz_.load(std::memory_order_relaxed);
    const quint32 eff = static_cast<quint32>(base
        + (xitEnabled_.load(std::memory_order_relaxed)
               ? xitOffsetHz_.load(std::memory_order_relaxed) : 0));
    txFreqHz_.store(eff, std::memory_order_relaxed);
    if (lyra::wire::prn != nullptr)
        // txDdsHzForTune applies the CW ∓pitch carrier offset (zero-beat).
        lyra::wire::set_tx_freq(txDdsHzForTune(eff));
    // TX power model Stage 3 — the per-band PA Gain (gbb) changes when the
    // TX band changes, so re-apply the power with the new band's gbb.  Only
    // on an actual band change, so a freq dial tick within a band is free.
    const int newBand = lyra::bandIndexForFreq(static_cast<int>(eff));
    if (newBand != lastTxBand_.exchange(newBand, std::memory_order_relaxed))
        applyTxPower_(txDriveLevel_.load(std::memory_order_relaxed));
    // The TX-analyzer crop offset (NCO − RX centre) changed — refresh the
    // panadapter so the TX signal paints at the new VFO-B position.
    emit txAnalyzerOffsetChanged(txAnalyzerOffsetHz());
}

// #174 CTUNE — sign of the WDSP RX demod shift.  CONFIRMED +1 correct by the
// 4-agent Thetis audit (2026-06-22): end-to-end Lyra delivers
// SetRXAShiftFreq(+(VFO−Centre)), byte-identical to Thetis, which computes
// rx1_osc=−(VFO−Centre) then SetRXAShiftFreq(−rx1_osc) (console.cs:32135 +
// radio.cs:1417).  Do NOT flip to −1.
static constexpr int kCtuneShiftSign = +1;

void HL2Stream::pushEffectiveRxFreq() {
    // The ONE RX-NCO writer (mirror of pushEffectiveTxFreq).  RX DDCs =
    // rx1FreqHz_ + RIT offset.  RIT moves only the receiver — TX is on the
    // separate pushEffectiveTxFreq path — matching the reference (RIT →
    // RXOsc only).  `if (prn)` guards the pre-open window.
    if (lyra::wire::prn == nullptr)
        return;
    const qint64 eff =
        static_cast<qint64>(rx1FreqHz_.load(std::memory_order_relaxed))
        + (ritEnabled_.load(std::memory_order_relaxed)
               ? ritOffsetHz_.load(std::memory_order_relaxed) : 0);
    quint32 center = ctuneCenterHz_.load(std::memory_order_relaxed);
    if (center != 0) {
        // #174 CTUNE: the DDC stays LOCKED at the centre; the WDSP RXA receiver
        // oscillator shifts the demod to the dial WITHIN the captured IQ span.
        // The whole center/shift/edge decision is computed HERE — the single
        // C++ RX-NCO writer EVERY tune source funnels through (panadapter, TCI
        // spot, keypad, memory, band) — so it holds even with the panadapter
        // dock closed.  Faithful port of Thetis's VFOA-pipeline CTUN block
        // (console.cs:32122-32248): smooth-scroll the centre at the display
        // margin, re-center on a far jump, revert to tracking when the passband
        // can't fit the zoomed view, and clamp to the usable IQ.  The panadapter
        // is a PURE READER of ctuneCenterHz — we emit ctuneChanged on a move.
        const double srHz = 48000.0 * static_cast<double>(
            1u << sampleRateBits_.load(std::memory_order_relaxed));
        double dispSpan = ctuneDispSpanHz_.load(std::memory_order_relaxed);
        if (dispSpan <= 0.0) dispSpan = srHz;          // pre-first-span fallback
        constexpr double kDispMargin = 0.05;           // Thetis dispMargin
        const int    filtLo = ctuneFiltLoHz_.load(std::memory_order_relaxed);
        const int    filtHi = ctuneFiltHiHz_.load(std::memory_order_relaxed);
        const double Lm = std::max(0.0, -static_cast<double>(filtLo));  // Thetis Lmargin
        const double Hm = std::max(0.0,  static_cast<double>(filtHi));  // Thetis Hmargin
        const double passbandW = static_cast<double>(filtHi - filtLo);
        const bool   canFit = passbandW < dispSpan * (1.0 - 2.0 * kDispMargin);
        const double half  = dispSpan / 2.0;
        const double Ldisp = -half + dispSpan * kDispMargin;
        const double Hdisp =  half - dispSpan * kDispMargin;
        constexpr double kJump = 500000.0;             // Thetis freqJumpThresh
        const double usable = srHz * 0.92 / 2.0;       // captured-IQ usable half

        qint64 cen = static_cast<qint64>(center);
        if (!canFit) {
            cen = eff;                                 // zoom too deep to fit passband → track (Thetis 32233)
        } else {
            const double p = static_cast<double>(eff - cen);   // demod display position (Thetis -rx1_osc)
            if ((p - Lm) < (Ldisp - kJump) || (p + Hm) > (Hdisp + kJump)) {
                cen = eff;                             // far jump (click/spot/memory) → re-center
            } else if ((p - Lm) < Ldisp) {
                cen -= static_cast<qint64>(Ldisp - (p - Lm));   // low margin → smooth-scroll centre
            } else if ((p + Hm) > Hdisp) {
                cen += static_cast<qint64>((p + Hm) - Hdisp);   // high margin → smooth-scroll centre
            }
            const double p2 = static_cast<double>(eff - cen);
            if (p2 < -usable + Lm || p2 > usable - Hm)
                cen = eff;                             // IQ clamp backstop → re-center
        }
        if (cen < 0) cen = 0;   // never wrap the quint32 store (defensive — only
                                // reachable at sub-MHz dial + pathological span)
        if (static_cast<quint32>(cen) != center) {
            center = static_cast<quint32>(cen);
            ctuneCenterHz_.store(center, std::memory_order_relaxed);
            emit ctuneChanged();                       // panadapter re-reads the centre
        }
        const int ci = static_cast<int>(center);
        lyra::wire::set_rx_freq(0, ci);  // DDC0 locked
        lyra::wire::set_rx_freq(1, ci);  // DDC1 locked (until RX2)
        const double shift = kCtuneShiftSign
            * static_cast<double>(eff - static_cast<qint64>(center));
        emit rxShiftHzChanged(shift);
    } else {
        const int hzi = static_cast<int>(eff);
        lyra::wire::set_rx_freq(0, hzi);  // DDC0 (case 2/8/9)
        lyra::wire::set_rx_freq(1, hzi);  // DDC1 (case 3 — until RX2)
        // CTUNE off: non-CTUNE path stays byte-identical — no shift emit
        // here.  The one disengage shift-off is emitted from setCtuneCenterHz.
    }
}

void HL2Stream::setCtuneEnabled(bool on) {
    const bool cur = ctuneCenterHz_.load(std::memory_order_relaxed) != 0;
    if (on == cur)
        return;   // already in the requested state
    // Engage snaps the locked centre to the current dial; disengage clears it.
    setCtuneCenterHz(on ? rx1FreqHz_.load(std::memory_order_relaxed) : 0u);
}

void HL2Stream::setCtuneCenterHz(quint32 hz) {
    if (ctuneCenterHz_.exchange(hz, std::memory_order_relaxed) == hz)
        return;
    emit ctuneChanged();
    emit logLine(hz ? QStringLiteral("CTUNE on — DDC locked @ %1 MHz")
                          .arg(hz / 1.0e6, 0, 'f', 6)
                    : QStringLiteral("CTUNE off"));
    pushEffectiveRxFreq();          // re-tune the DDC (+ shift emit when on)
    if (hz == 0)
        emit rxShiftHzChanged(0.0); // disengage: turn the WDSP RX shift off
}

void HL2Stream::setCtuneDisplaySpanHz(double hz) {
    if (hz < 0.0) hz = 0.0;
    ctuneDispSpanHz_.store(hz, std::memory_order_relaxed);
    // A zoom change moves the display span → re-evaluate the CTUNE edge model
    // (a zoom-in that no longer fits the passband must revert to tracking;
    // Thetis force-centers on zoom-in).  No-op when CTUNE is off.
    if (ctuneCenterHz_.load(std::memory_order_relaxed) != 0)
        pushEffectiveRxFreq();
}

void HL2Stream::setCtuneFilterEdges(int lowHz, int highHz) {
    ctuneFiltLoHz_.store(lowHz,  std::memory_order_relaxed);
    ctuneFiltHiHz_.store(highHz, std::memory_order_relaxed);
    // A mode / bandwidth change alters the passband margins + canFit → re-eval
    // the CTUNE edge model now (mirror setCtuneDisplaySpanHz) so a fit/no-fit
    // transition doesn't lag until the next tune.  No-op when CTUNE is off.
    if (ctuneCenterHz_.load(std::memory_order_relaxed) != 0)
        pushEffectiveRxFreq();
}

void HL2Stream::setRitEnabled(bool on) {
    if (ritEnabled_.exchange(on, std::memory_order_relaxed) == on)
        return;
    QSettings().setValue(QStringLiteral("rx/ritEnabled"), on);
    emit ritChanged();
    pushEffectiveRxFreq();   // re-tune RX DDCs with/without the offset
}

void HL2Stream::setRitOffsetHz(int hz) {
    hz = std::clamp(hz, -9999, 9999);
    if (ritOffsetHz_.exchange(hz, std::memory_order_relaxed) == hz)
        return;
    QSettings().setValue(QStringLiteral("rx/ritOffsetHz"), hz);
    emit ritChanged();
    if (ritEnabled_.load(std::memory_order_relaxed))
        pushEffectiveRxFreq();
}

void HL2Stream::setXitEnabled(bool on) {
    if (xitEnabled_.exchange(on, std::memory_order_relaxed) == on)
        return;
    QSettings().setValue(QStringLiteral("tx/xitEnabled"), on);
    emit xitChanged();
    pushEffectiveTxFreq();   // re-point the TX NCO (+ PS DDCs)
}

void HL2Stream::setXitOffsetHz(int hz) {
    hz = std::clamp(hz, -9999, 9999);
    if (xitOffsetHz_.exchange(hz, std::memory_order_relaxed) == hz)
        return;
    QSettings().setValue(QStringLiteral("tx/xitOffsetHz"), hz);
    emit xitChanged();
    if (xitEnabled_.load(std::memory_order_relaxed))
        pushEffectiveTxFreq();
}

void HL2Stream::setSplitEnabled(bool on) {
    const bool prev = splitEnabled_.exchange(on, std::memory_order_relaxed);
    if (prev == on)
        return;
    QSettings().setValue(QStringLiteral("tx/splitEnabled"), on);
    emit splitEnabledChanged();
    safetyLog(QStringLiteral("TX: SPLIT -> %1 (TX freq source = %2)")
                  .arg(on ? QStringLiteral("ON") : QStringLiteral("off"))
                  .arg(on ? QStringLiteral("VFO B") : QStringLiteral("VFO A")));
    // Re-point the TX NCO (+ PS-feedback DDCs) at the new source.
    pushEffectiveTxFreq();
}

void HL2Stream::setVfoBHz(quint32 hz) {
    const quint32 prev = vfoBHz_.exchange(hz, std::memory_order_relaxed);
    if (prev == hz)
        return;
    QSettings().setValue(QStringLiteral("tx/vfoBHz"), hz);
    emit vfoBHzChanged();
    emit logLine(QStringLiteral("VFO B -> %1 Hz (%2 MHz)")
                     .arg(hz).arg(hz / 1.0e6, 0, 'f', 6));
    // VFO B only affects the wire while split is on (it IS the TX freq then).
    if (splitEnabled_.load(std::memory_order_relaxed))
        pushEffectiveTxFreq();
}

// ---------------------------------------------------------------
// TX-0b: RF-transmit state setters (foundation; WIRE-INERT).
// Each clamps + stores the HL2+ byte encoding for its TX C&C frame
// (per the HL2 wire-protocol C reference + ak4951v4 gateware decode;
// see hl2_stream.h header doc for the per-register map + file:line
// cites).  NONE are read by the EP2 emission loop
// yet, so at MOX=0 / PA-off the datagram stays byte-identical to RX.
// A later TX phase wires these into the C&C round-robin under MOX gating.

void HL2Stream::setMox(bool on) {
    // Low-level: forces the wire MOX atomic directly, bypassing the
    // TR-sequenced FSM.  Prefer requestMox() for operator/CAT paths.
    // This stays in place for the FSM's own internal use + future
    // dev/diag tools.
    const bool prev = mox_.exchange(on, std::memory_order_relaxed);
    if (prev != on) {
        // §5 (TX): keep the wire gate (global XmitBit) coherent with the
        // raw MOX bit so diag/test tools that bypass the FSM still gate
        // the writer correctly.
        lyra::wire::XmitBit = on ? 1 : 0;
        // Raw setter bypasses the FSM's TR-delay chain — if anything
        // ever calls this in production, the operator needs to know.
        // Diag/test tools use it deliberately; nothing else should.
        safetyLog(QStringLiteral("TX: MOX bit (raw setter) -> %1")
                  .arg(on ? QStringLiteral("ON") : QStringLiteral("off")));
    }
}

void HL2Stream::setTxFreqHz(quint32 hz) {
    const quint32 prev = txFreqHz_.exchange(hz, std::memory_order_relaxed);
    if (prev != hz) {
        // §5 (TX): prn->tx[0].frequency (case 1/5/6).  In simplex,
        // setRx1FreqHz mirrors this; the standalone setter covers
        // split-mode / direct TX-freq paths.
        if (lyra::wire::prn != nullptr)
            // #105 CW carrier offset — transmit at the carrier (= DDS +
            // markerOffset in CW), not the bare DDS, so the keyed carrier
            // lands on the marker like every other TX NCO push.
            lyra::wire::set_tx_freq(txDdsHzForTune(hz));
        emit logLine(QStringLiteral("TX freq -> %1 Hz (%2 MHz)")
                     .arg(hz).arg(hz / 1.0e6, 0, 'f', 6));
    }
}

double HL2Stream::radioVolumeFor_(int requestedRaw) const {
    // TX power model Stage 3 — the Thetis HL2 RadioVolume (console.cs:47775):
    //   RadioVolume = min(driveSlider · gbb[band]/100 / 93.75, 1.0)
    // The requested raw is the operator drive % mapped 0..255, so the
    // slider value is raw·100/255.  gbb is the active TX band's PA Gain
    // (default 100 = neutral).  93.75 is the HL2 16-step DAC correction
    // (Thetis comment: "jump in steps of 16 but getting 6").
    const double pct = requestedRaw * 100.0 / 255.0;
    const int band = lyra::bandIndexForFreq(
        static_cast<int>(txFreqHz_.load(std::memory_order_relaxed)));
    const double gbb = (band >= 0 && band < kNumPaGainBands)
                           ? paGainByBand_[band].load(std::memory_order_relaxed)
                           : kPaGainDefault;
    const double rv = pct * (gbb / 100.0) / 93.75;
    return rv < 0.0 ? 0.0 : (rv > 1.0 ? 1.0 : rv);
}

int HL2Stream::wattsFallbackCeilingRaw_(int band, double capW) const {
    // Stage B — the CONSERVATIVE drive ceiling for a band that hasn't been
    // TUN-auto-learned yet (or whose cap changed).  A static model can't
    // predict each band's curve (the HL2 runs ~drive^2.6 low / ~drive^2.1
    // high + a 16-step DAC), so we deliberately UNDER-shoot: exponent 2.0 is
    // shallower than the shallowest measured band, guaranteeing output stays
    // under the cap on every band.  The TUN servo then walks this up to the
    // exact cap.  No Full Output measured → a safe-low fixed 30 % drive
    // (the cap can't be honoured without a reference, so stay quiet).
    if (band < 0 || band >= kNumPaGainBands) return 255;
    const double fullW = fullOutputWByBand_[band].load(std::memory_order_relaxed);
    if (fullW <= 0.0) return (255 * 30) / 100;       // no reference → safe-low
    if (capW >= fullW) return 255;                   // cap above full → no clamp
    const double ceilPct = 100.0 * std::pow(capW / fullW, 1.0 / kWattsFallbackExp);
    const int raw = static_cast<int>(std::lround(ceilPct * 255.0 / 100.0));
    return raw < 1 ? 1 : (raw > 255 ? 255 : raw);
}

bool HL2Stream::capTunedFor_(int band, double capW) const {
    if (band < 0 || band >= kNumPaGainBands) return false;
    if (capCeilRaw_[band].load(std::memory_order_relaxed) < 0) return false;
    const double learnedCap = capCeilCapW_[band].load(std::memory_order_relaxed);
    return std::abs(learnedCap - capW) < 0.01;
}

bool HL2Stream::capTunedForBand(int idx) const {
    return capTunedFor_(idx, maxOutputW_.load(std::memory_order_relaxed));
}

int HL2Stream::wattsDriveCeilingRaw_() const {
    // Stage B — the highest requested raw allowed on the active TX band so
    // output stays at/under the watts Max cap.  If the band has been
    // TUN-auto-learned FOR THIS CAP, use that locked ceiling (exact, landed
    // by the live meter).  Otherwise the conservative fallback (safe-under)
    // until TUN learns it.  255 (no clamp) when the cap is off.
    // Arm gate (2026-07-03): a cap value alone no longer limits — the operator
    // must ARM it (after calibrating Full Output).  Un-armed → no clamp, so an
    // uncalibrated cap can never silently hold power at the ~30 % fallback.
    if (!capActive_()) return 255;
    const double capW = maxOutputW_.load(std::memory_order_relaxed);
    const int band = lyra::bandIndexForFreq(
        static_cast<int>(txFreqHz_.load(std::memory_order_relaxed)));
    if (band < 0 || band >= kNumPaGainBands) return 255;
    if (capTunedFor_(band, capW))
        return capCeilRaw_[band].load(std::memory_order_relaxed);
    return wattsFallbackCeilingRaw_(band, capW);
}

void HL2Stream::tickCapServo_(double fwdW) {
    // Stage B — in TUN, AUTO-LEARN this band's drive ceiling for the cap by
    // walking it UP from the conservative fallback until the PWR meter
    // reaches the cap, then locking it.  Approach-from-below = the output
    // only ever rises TOWARD the cap, never overshoots → SS-amp-safe.  The
    // locked per-band ceiling is then used statically (incl. SSB), so it
    // caps voice peaks without chasing them.  Requires a Full Output
    // reference (so the start + the runaway bound are sane).
    if (!capActive_()) return;     // arm gate (2026-07-03)
    const double capW = maxOutputW_.load(std::memory_order_relaxed);
    if (capW <= 0.0) return;
    const int band = lyra::bandIndexForFreq(
        static_cast<int>(txFreqHz_.load(std::memory_order_relaxed)));
    if (band < 0 || band >= kNumPaGainBands) return;
    const double fullW = fullOutputWByBand_[band].load(std::memory_order_relaxed);
    if (fullW <= 0.0 || capW >= fullW) return;       // need a reference; cap≥full = no cap

    const auto persist = [this, band]() {
        const auto &bands = lyra::amateurBands();
        const QString b = band < int(bands.size())
            ? QString::fromUtf8(bands[band].name) : QString::number(band);
        QSettings s;
        s.setValue(QStringLiteral("pa_gain/%1/capCeilRaw").arg(b),
                   capCeilRaw_[band].load(std::memory_order_relaxed));
        s.setValue(QStringLiteral("pa_gain/%1/capCeilCapW").arg(b),
                   capCeilCapW_[band].load(std::memory_order_relaxed));
        s.setValue(QStringLiteral("pa_gain/%1/capSettled").arg(b),
                   capServoSettled_[band].load(std::memory_order_relaxed));
    };

    // Fresh (never learned, or the cap changed): seed from the conservative
    // fallback and begin the walk.  capCeilRaw_ drives the ceiling that
    // applyTxPower_ clamps to, so raising it raises the output.
    if (!capTunedFor_(band, capW)) {
        capCeilRaw_[band].store(wattsFallbackCeilingRaw_(band, capW),
                                std::memory_order_relaxed);
        capCeilCapW_[band].store(capW, std::memory_order_relaxed);
        capServoSettled_[band].store(false, std::memory_order_relaxed);
        capServoTicks_ = 0;
        persist();
        applyTxPower_(txDriveLevel_.load(std::memory_order_relaxed));
        return;
    }
    // Throttle so the PWR meter settles between steps.
    if (++capServoTicks_ < kCapServoStepTicks) return;
    capServoTicks_ = 0;
    if (!(fwdW >= swrFwdFloorW_)) return;            // NaN / below floor → wait
    // Runaway bound: never walk above the LEAST-conservative sane estimate
    // (a drive^3 ceiling) even if the meter under-reads — caps the over-drive
    // to a sensible value (the real curve is always shallower than cube, so
    // this never blocks reaching the true cap).
    const int hardMax = static_cast<int>(std::lround(
        255.0 * std::cbrt(capW / fullW)));
    int ceil = capCeilRaw_[band].load(std::memory_order_relaxed);
    const bool settled = capServoSettled_[band].load(std::memory_order_relaxed);
    // Coarse-DAC straddle: the HL2 drive DAC has ~16 steps, so one servo step
    // can swing the output ~0.5 W.  A cap can fall BETWEEN two hardware steps
    // (e.g. 3.35 W under / 3.93 W over a 3.5 W cap) — chasing the exact cap
    // then just hunts across the gap and the meter shows the over-cap peaks.
    // Rule: OVER the cap → step down AND latch "settled" so we PARK on the
    // step just under the cap and never climb back over it.  Only climb while
    // genuinely below the cap and NOT yet settled.  Re-arm the climb if the
    // reading falls well under the cap (a real change — band/thermal/drive),
    // so a legitimately under-driven band can still reach the cap.
    if (fwdW > capW && ceil > 1) {
        capCeilRaw_[band].store(std::max(1, ceil - kCapServoStepRaw),
                                std::memory_order_relaxed);
        capServoSettled_[band].store(true, std::memory_order_relaxed);
        persist();
        applyTxPower_(txDriveLevel_.load(std::memory_order_relaxed));
    } else if (fwdW < capW * 0.80 && settled) {
        // Fell well under the cap after settling → conditions changed; unlatch
        // so the climb below can re-converge to the new under-cap step.
        capServoSettled_[band].store(false, std::memory_order_relaxed);
        persist();
    } else if (fwdW < capW * 0.97 && ceil < hardMax && !settled) {
        capCeilRaw_[band].store(std::min(hardMax, ceil + kCapServoStepRaw),
                                std::memory_order_relaxed);
        persist();
        applyTxPower_(txDriveLevel_.load(std::memory_order_relaxed));
    }
    // else: parked on the highest step at/under the cap (settled), or already
    // in [cap·0.97, cap], or pinned at hardMax — locked.
}

void HL2Stream::applyTxPower_(int requestedRaw) {
    // The single TX power chokepoint.  RadioVolume drives BOTH the coarse
    // AD9866 drive_level byte (round(255·RV)) AND the ChannelMaster txgain
    // digital fixed gain (SetTXFixedGain(RV)) — exactly as Thetis drives
    // both from one RadioVolume.  The byte gives the 16 coarse steps; the
    // fixed gain fills continuously between them + below the PA floor
    // (sub-watt) and is PureSignal-safe (it's the reference's own TX gain
    // stage, where PS taps post-gain).  The per-band PA Gain shapes RV so
    // a band's number can trim or boost.  Both wire calls are guarded
    // (no-op before create_xmtr / when prn is null).
    // Stage 3b — apply the per-band watts ceiling here (NOT at the stored
    // setpoint), so the operator's drive is preserved + moving to a
    // less-restrictive band restores power.
    const int    ceiling = wattsDriveCeilingRaw_();
    const int    capped = std::min(requestedRaw, ceiling);
    const double rv  = radioVolumeFor_(capped);
    // #170c — CW watts-cap fold.  On CW key-down the gateware SUBSTITUTES a
    // full-scale carrier for the WDSP TX-I/Q path (hl2_rtl_radio.v:1145,
    // `tx_i = tx_cw_key ? {1'b0, tx_cwlevel[18:4]} : y2_i`), which BYPASSES
    // the ChannelMaster fixed digital gain set just below — so in CW the
    // coarse AD9866 drive byte is the ONLY host power lever.  For a low cap
    // most of the watts-cap attenuation lives in that (bypassed) fixed gain,
    // so an SSB/TUN-calibrated ceiling under-protects CW (operator bench:
    // 3 W cap -> ~5.5 W CW).  Fix: when the cap is armed AND we're in CW,
    // FOLD the fixed-gain factor into the coarse byte (byte carries rv*rv =
    // the same total digital scaling SSB gets across the byte + fine stages),
    // so the coarse byte alone honours the cap.  Coarse-DAC quantisation errs
    // the folded byte LOW, so CW stays at/under the cap.  Uncapped CW is
    // untouched (no surprise power change when the cap is off).  NOTE: TUN
    // uses the postgen tone through y2_i (fixed gain ACTIVE, not the keyed
    // carrier), so capped TUN in CW mode sits a touch under the cap — safe;
    // refine with a CW-keyed servo learn (option B) if the operator wants it.
    const bool cwFold = capActive_()
        && (txMode_.load(std::memory_order_relaxed) == 3      // WDSP CWL
         || txMode_.load(std::memory_order_relaxed) == 4);    // WDSP CWU
    // rv*rv folds the fine gain into the coarse byte; kCwCapHeadroom is a
    // conservative amplitude margin covering the coarse-DAC quantisation +
    // PA-curve residual so the folded byte lands UNDER the cap on any band.
    // Bench (N8SDR HL2+, 20 m, 3 W cap): bare rv*rv left CW at ~3.7 W (0.7 W
    // over); an ~0.85 amplitude margin (power ~0.85^[2..3]) clears it with a
    // safety cushion.  Protective cap -> deliberately errs low.  Reclaim the
    // residual exactly with a CW-keyed servo learn (option B) if wanted.
    constexpr double kCwCapHeadroom = 0.85;
    const double byteRv = cwFold ? rv * rv * kCwCapHeadroom : rv;
    const int    byte = static_cast<int>(std::lround(255.0 * byteRv));
    if (lyra::wire::prn != nullptr) lyra::wire::set_drive_level(byte);
    lyra::wire::SetTXFixedGainRun(0, 1);
    lyra::wire::SetTXFixedGain(0, rv, rv);

    // Amp-cap live indicator for the TX-panel CAP chip.  Recomputed here
    // because this is the one chokepoint every drive / PA-gain / band / cap
    // change flows through.  Only "actively limiting" (ceiling below the
    // requested drive) shows a chip: 2 = cap ON but this band is NOT
    // TUN-calibrated → clamped to the conservative ~30 % fallback (LOW power,
    // the trap); 1 = cap ON and holding a calibrated band at the set watts.
    int newCapStatus = 0;
    const double capW = maxOutputW_.load(std::memory_order_relaxed);
    if (capW > 0.0 && ceiling < requestedRaw) {
        const int cband = lyra::bandIndexForFreq(
            static_cast<int>(txFreqHz_.load(std::memory_order_relaxed)));
        newCapStatus = capTunedFor_(cband, capW) ? 1 : 2;
    }
    if (capStatus_.exchange(newCapStatus, std::memory_order_relaxed) != newCapStatus)
        emit capStatusChanged();
}

double HL2Stream::paGainForBand(int idx) const {
    if (idx < 0 || idx >= kNumPaGainBands) return kPaGainDefault;
    return paGainByBand_[idx].load(std::memory_order_relaxed);
}

void HL2Stream::setPaGainForBand(int idx, double gain) {
    if (idx < 0 || idx >= kNumPaGainBands) return;
    paGainByBand_[idx].store(gain, std::memory_order_relaxed);
    const auto &bands = lyra::amateurBands();
    QSettings().setValue(
        QStringLiteral("pa_gain/%1/gain")
            .arg(idx < int(bands.size()) ? QString::fromUtf8(bands[idx].name)
                                         : QString::number(idx)),
        gain);
    // Re-apply live if this is the band we're transmitting on, so the
    // operator sees the dummy-load power move as they nudge the number.
    const int cur = lyra::bandIndexForFreq(
        static_cast<int>(txFreqHz_.load(std::memory_order_relaxed)));
    if (cur == idx)
        applyTxPower_(txDriveLevel_.load(std::memory_order_relaxed));
}

double HL2Stream::fullOutputForBand(int idx) const {
    if (idx < 0 || idx >= kNumPaGainBands) return 0.0;
    return fullOutputWByBand_[idx].load(std::memory_order_relaxed);
}

void HL2Stream::setFullOutputForBand(int idx, double watts) {
    if (idx < 0 || idx >= kNumPaGainBands) return;
    fullOutputWByBand_[idx].store(watts, std::memory_order_relaxed);
    const auto &bands = lyra::amateurBands();
    QSettings().setValue(
        QStringLiteral("pa_gain/%1/fullW")
            .arg(idx < int(bands.size()) ? QString::fromUtf8(bands[idx].name)
                                         : QString::number(idx)),
        watts);
    // Re-apply if this band is live — the watts ceiling just changed.
    const int cur = lyra::bandIndexForFreq(
        static_cast<int>(txFreqHz_.load(std::memory_order_relaxed)));
    if (cur == idx)
        applyTxPower_(txDriveLevel_.load(std::memory_order_relaxed));
}

void HL2Stream::setMaxOutputW(double watts) {
    if (watts < 0.0) watts = 0.0;
    if (watts == maxOutputW_.load(std::memory_order_relaxed)) return;
    maxOutputW_.store(watts, std::memory_order_relaxed);
    QSettings().setValue(QStringLiteral("tx/maxOutputW"), watts);
    emit maxOutputWChanged(watts);
    // Clearing the cap value disarms it — an "armed but no value" state is
    // meaningless, and re-arming after re-setting a value is one explicit
    // click (2026-07-03).
    if (watts <= 0.0 && capArmed_.load(std::memory_order_relaxed))
        setCapArmed(false);
    // The ceiling changed for the live band — re-apply now.
    applyTxPower_(txDriveLevel_.load(std::memory_order_relaxed));
}

void HL2Stream::setCapArmed(bool on) {
    if (on == capArmed_.load(std::memory_order_relaxed)) return;
    // Never arm without a cap value to enforce (defensive — the UI already
    // gates the arm control, but keep the invariant here too).
    if (on && maxOutputW_.load(std::memory_order_relaxed) <= 0.0) return;
    capArmed_.store(on, std::memory_order_relaxed);
    QSettings().setValue(QStringLiteral("tx/capArmed"), on);
    emit capArmedChanged(on);
    // The effective ceiling just changed — re-apply on the live band.
    applyTxPower_(txDriveLevel_.load(std::memory_order_relaxed));
}

double HL2Stream::capturedRawWForBand(int idx) const {
    if (idx < 0 || idx >= kNumPaGainBands) return 0.0;
    return capturedRawW_[idx].load(std::memory_order_relaxed);
}

int HL2Stream::capturedDriveForBand(int idx) const {
    if (idx < 0 || idx >= kNumPaGainBands) return 0;
    return capturedDrive_[idx].load(std::memory_order_relaxed);
}

void HL2Stream::clearCapLearnForBand(int idx) {
    if (idx < 0 || idx >= kNumPaGainBands) return;
    capCeilRaw_[idx].store(-1, std::memory_order_relaxed);
    capCeilCapW_[idx].store(-1.0, std::memory_order_relaxed);
    capServoSettled_[idx].store(false, std::memory_order_relaxed);
    const auto &bands = lyra::amateurBands();
    const QString b = idx < int(bands.size())
        ? QString::fromUtf8(bands[idx].name) : QString::number(idx);
    QSettings s;
    s.setValue(QStringLiteral("pa_gain/%1/capCeilRaw").arg(b), -1);
    s.setValue(QStringLiteral("pa_gain/%1/capCeilCapW").arg(b), -1.0);
    s.setValue(QStringLiteral("pa_gain/%1/capSettled").arg(b), false);
    // Re-apply if this band is live so a stale locked ceiling stops biting now.
    const int cur = lyra::bandIndexForFreq(
        static_cast<int>(txFreqHz_.load(std::memory_order_relaxed)));
    if (cur == idx)
        applyTxPower_(txDriveLevel_.load(std::memory_order_relaxed));
}

void HL2Stream::setTxDriveLevel(int level) {
    // Lands C1 of slot 10 (frame 0x12) — the drive DAC level.  Gateware
    // uses the top 4 bits → 16 coarse steps; wire 0..255 maps the
    // operator's 0..100 % linearly via the UI before reaching here.
    //
    // At drive=0 the wire is byte-identical to TX-0c-pa-debug B-pa
    // regardless of PA enable / MOX state: zero amplitude on the DAC
    // means no carrier even when the PA is biased.  Above 0, with PA
    // enabled and MOX keyed, the operator gets actual RF — this is the
    // slice that takes the first-RF bench from bias-only to real
    // emissions.  Use a dummy load + watt-meter for the first session.
    // #170a drive cap — the single chokepoint: every drive write
    // (operator slider, TUN drive, BandMemory restore, TCI DRIVE) funnels
    // here, so clamping to the Max-TX-drive ceiling here covers all of
    // them.  Pure preventive clamp — it never trips, it just won't exceed
    // the ceiling.
    const int clamped = std::min(maxDriveRaw(), std::clamp(level, 0, 255));
    const int prev    = txDriveLevel_.exchange(clamped, std::memory_order_relaxed);
    if (prev == clamped) return;
    // §5 (TX): prn->tx[0].drive_level (compose_case_10 C1).
    applyTxPower_(clamped);   // Stage 3 — RadioVolume drives the byte + fixed gain
    QSettings().setValue(QStringLiteral("tx/driveLevel"), clamped);
    emit txDriveLevelChanged(clamped);
    // Operator-facing percent for the log line (gateware actually
    // resolves to 16 coarse steps, but the percent is the operator's
    // mental model — quoting raw 0..255 in the log would be noise).
    const int pct = static_cast<int>(std::lround(clamped * 100.0 / 255.0));
    emit logLine(QStringLiteral("TX: drive -> %1 %  (raw %2/255)")
                 .arg(pct).arg(clamped));
}

void HL2Stream::applyDriveLevelNoPersist(int level) {
    // #169 Phase 1b — same wire path as setTxDriveLevel (prn drive DAC),
    // but NO QSettings write + NO log spam: SWR Fold uses this to step the
    // applied drive down transiently so the operator's stored set point
    // survives, then restores it on the next key-down.  Emits
    // txDriveLevelChanged so the TxPanel drive readout tracks the fold
    // live (operator sees the power backing off).  Also respects the
    // #170a drive cap (defensive — fold only ever steps down).
    const int clamped = std::min(maxDriveRaw(), std::clamp(level, 0, 255));
    const int prev    = txDriveLevel_.exchange(clamped, std::memory_order_relaxed);
    if (prev == clamped) return;
    applyTxPower_(clamped);   // Stage 3 — SWR fold backs off byte + digital level
    emit txDriveLevelChanged(clamped);
}

void HL2Stream::setTxStepAttnDb(int db) {
    const int clamped = std::clamp(db, 0, 31);
    txStepAttnDb_.store(clamped, std::memory_order_relaxed);
    // §5 (TX): compose_case_4 (C3, 5-bit) + compose_case_11 (C4, 6-bit,
    // XmitBit-gated) read prn->adc[0].tx_step_attn.  set_tx_step_attn_db
    // stores the HL2 (31 - db) wire encoding — byte-identical to the
    // retired composeCC's (31 - txStepAttnDb_).  The FSM ATT-on-TX raise
    // (fsmAdvance → setTxStepAttnDb(kAttOnTxDb)) reaches the wire here.
    if (lyra::wire::prn != nullptr) lyra::wire::set_tx_step_attn_db(clamped);
}

void HL2Stream::setPaEnabled(bool on) {
    // Lands C2 bit 3 (0x08) of slot 10 (frame 0x12) — gateware
    // PA-enable, active-high.  Operator-gated via the Settings checkbox
    // (default OFF on every fresh stream open / close cycle for safety,
    // but persisted across clean Lyra exit-and-relaunch).
    //
    // When this is TRUE *and* MOX is on the wire *and* the gateware
    // sees the round-robin C2-bit-3 update, the PA bias engages — the
    // operator should see PA-current rise on the banner.  TX I/Q stays
    // zero until a future TX DSP commit, so PA bias rises but no
    // modulated carrier reaches the antenna (~0 W into dummy load).
    const bool prev = paOn_.exchange(on, std::memory_order_relaxed);
    if (prev == on) return;
    // §5 (TX): ApolloTuner/ApolloFilt globals + prn->tx[0].pa
    // (compose_case_10 C2 bit3 active-high PA-enable + C3 legacy bit).
    if (lyra::wire::prn != nullptr) lyra::wire::set_pa_on(on);
    QSettings().setValue(QStringLiteral("tx/paEnabled"), on);
    emit paEnabledChanged(on);
    safetyLog(QStringLiteral("TX: PA enable -> %1")
              .arg(on ? QStringLiteral("ON  (RF possible on next key)")
                      : QStringLiteral("off (PA bias disarmed)")));
}

void HL2Stream::setMicBoost(bool on) {
    // Task #39 — HL2 +20 dB hardware Mic Boost via the codec PGA.
    // Per the reference's SetMicBoost (netInterface.c:536-544 +
    // network.h:220-221 mic_boost : 1) the wire is a single bit
    // at C0 0x12 C2 bit 0: hardware is 2-state Off / +20 dB only.
    // Intermediate boost levels come from the operator Mic Gain
    // slider (Task #32) applied AFTER this hardware boost stage
    // — the two compose: HW +20 dB boost feeds the WDSP TXA
    // mic-input panel where the operator's Mic Gain slider
    // applies digital trim.
    //
    // No safety implication (purely audio-level), no MOX gating
    // (the bit can flip on RX without affecting any wire RF).
    // Persisted to QSettings; recalled on stream open.
    const bool prev = micBoost_.exchange(on, std::memory_order_relaxed);
    if (prev == on) return;
    QSettings().setValue(QStringLiteral("tx/micBoost"), on);
    emit micBoostChanged(on);
    safetyLog(QStringLiteral("TX: Mic Boost -> %1")
              .arg(on ? QStringLiteral("ON  (+20 dB HW)")
                      : QStringLiteral("off (0 dB HW)")));
}

void HL2Stream::setBandVoltsOutput(bool on) {
    // HL2 "Band Volts" gateware feature (MI0BOT / Ramdor Thetis builds):
    // sets the C0=0x00 frame C3 bit 3 (the ADC "dither" bit), which the
    // gateware decodes as `band_volts_enabled` (control.v:582-584) and then
    // emits a per-band analog voltage on the fan-PWM pin — used by amps,
    // tuners, and antenna switches that band-follow off a band voltage.
    //
    // Thetis parity: `chkHL2BandVolts` -> NetworkIO.SetADCDither ->
    // prn->adc[0].dither -> WriteMainLoop_HL2 case 0 C3 bit 3.
    //
    // TRADE-OFF: while on, the fan-PWM pin outputs band voltage instead of
    // fan control (control.v:604-714) — operator opt-in, default OFF.  No
    // MOX gating (band data should be current in RX too).  Persisted.
    const bool prev = bandVolts_.exchange(on, std::memory_order_relaxed);
    if (prev == on) return;
    if (lyra::wire::prn != nullptr) lyra::wire::set_band_volts_output(on);
    QSettings().setValue(QStringLiteral("hw/bandVolts"), on);
    emit bandVoltsOutputChanged(on);
    safetyLog(QStringLiteral("HW: Band-Volts output -> %1")
              .arg(on ? QStringLiteral("ON  (fan pin = band voltage)")
                      : QStringLiteral("off (fan pin = fan control)")));
}

int HL2Stream::cwTxCarrierOffsetHz() const {
    // #105 CW-2 — VFO − DDS for the current TX mode, == WdspEngine::
    // cwMarkerOffsetForMode so the keyed CW carrier paints on the marker.
    // CWU(4) carrier sits +pitch above the DDC, CWL(3) −pitch below; every
    // other mode 0.  Uses the live shared CW pitch (cwPitchHz_, fed from the
    // RX pitch) — the same value the marker uses — NOT the tune constant.
    const int tm = txMode_.load(std::memory_order_relaxed);
    const int p  = cwPitchHz_.load(std::memory_order_relaxed);
    return (tm == 4) ? +p : (tm == 3) ? -p : 0;   // CWU / CWL / other
}

void HL2Stream::setCwPitchHz(int hz) {
    // #105 CW-2 — the single CW pitch (shared with the RX pitch / marker,
    // WdspEngine::cwPitchHz; wired in main.cpp).  Drives the keyed CW carrier
    // offset (cwTxCarrierOffsetHz) so the carrier tracks the marker, and the
    // gateware HW sidetone freq (= CW pitch).  Clamp matches WdspEngine.
    const int c = std::clamp(hz, 200, 1500);
    if (c == cwPitchHz_.load(std::memory_order_relaxed)) return;
    cwPitchHz_.store(c, std::memory_order_relaxed);
    if (auto* p = lyra::wire::prn) {
        // One pitch: the gateware HW sidetone runs at the CW pitch (no
        // separate sidetone-freq control).
        p->cw.sidetone_freq = c;
        // Re-push the TX NCO so the keyed CW carrier offset follows the new
        // pitch (the marker moved to the new pitch too — keep the carrier on it).
        lyra::wire::set_tx_freq(
            txDdsHzForTune(txFreqHz_.load(std::memory_order_relaxed)));
    }
    // The panadapter TX-analyzer crop (NCO − DDS) changed with the pitch.
    emit txAnalyzerOffsetChanged(txAnalyzerOffsetHz());
}

int HL2Stream::txDdsHzForTune(quint32 dialHz) const {
    // P4.b TUN zero-beat — Thetis tx_freq computation gated on chkTUN
    // (console.cs:32574-32587): while tuning, offset the TX NCO by
    // ∓cw_pitch (USB −, LSB +) so the ±cw_pitch postgen tone (main.cpp
    // setTune) cancels to a carrier at the dial.  Not tuning → dial
    // unchanged.  Same kTuneCwPitchHz both sides → they cancel.
    if (!tuneEnabled_.load(std::memory_order_relaxed))
        // #105 CW-2 fix — the keyed CW carrier must land on the marker (the
        // displayed VFO = DDS + markerOffset), NOT at the DDC centre.  Add the
        // CW carrier offset (CWU +pitch / CWL −pitch / other 0), using the
        // live shared CW pitch.  Non-CW → 0 so SSB/AM/FM/DSB are unchanged.
        return static_cast<int>(dialHz) + cwTxCarrierOffsetHz();
    // ∓cw_pitch by sideband so the ±cw_pitch postgen tone cancels to a
    // carrier at the dial.  Double-sideband modes (DSB/FM/AM/SAM) are
    // centered → no offset; LSB-side {LSB,CWL,DIGL} +pitch; USB-side −pitch.
    const int tm = txMode_.load(std::memory_order_relaxed);
    const int off = (tm == 2 || tm == 5 || tm == 6 || tm == 10) ? 0
                  : (tm == 0 || tm == 3 || tm == 9) ? +kTuneCwPitchHz
                  :                                   -kTuneCwPitchHz;
    return static_cast<int>(dialHz) + off;
}

int HL2Stream::txAnalyzerOffsetHz() const {
    // The panadapter TX-analyzer crop = TX NCO − DDS, so the analyzer carrier
    // (which sits at the NCO) paints on the marker (= DDS + markerOffset).
    // #105 fix: the CW keyed carrier offset moved the NCO to the carrier for
    // ALL CW TX (not just TUN), so the crop must follow it for keyed CW too —
    // otherwise the analyzer paints the carrier at panadapter centre, off the
    // marker (CWL above / CWU below — the operator's bench report).  Computing
    // it as NCO − DDS covers SSB (0), keyed CW, and TUN uniformly, and tracks
    // the live CW pitch (txDdsHzForTune uses cwPitchHz_).
    // The TX-analyzer carrier sits at the TX NCO; the panadapter is centred
    // on the RX DDC (rx1FreqHz_ = VFO A).  So the paint offset = NCO − RX
    // centre.  In SIMPLEX txFreqHz_ == rx1, so this reduces to the CW/TUN
    // pitch offset (unchanged behaviour).  In SPLIT txFreqHz_ = VFO B, so the
    // TX carrier paints at (VFO B − VFO A) from centre — on the red VFO-B
    // marker — instead of stuck at centre (the operator-caught split bug).
    const int nco = txDdsHzForTune(txFreqHz_.load(std::memory_order_relaxed));
    return nco - static_cast<int>(rx1FreqHz_.load(std::memory_order_relaxed));
}

void HL2Stream::setTuneEnabled(bool on) {
    // Arms / disarms the tune carrier.  P4.b re-home: the carrier now
    // comes from the WDSP TXA output-side tone generator (postgen /
    // gen1) via txControl_.setTune — the Thetis TUN mechanism
    // (console.cs:30787-30801: SetTXAPostGenMode(0) single tone +
    // ToneFreq ±pitch + ToneMag MAX_TONE_MAG + Run 1).  The legacy
    // host-streamed DC injection (EP2 packer ~0.95-fs into TX-I) died
    // with the wire-live switchover, so it's replaced here.  The TXA
    // channel armed by the MOX keydown (requestMox, fired alongside this
    // by the TUN button) processes the postgen tone → carrier → wire.
    //
    // Drive % scales the on-air power.  NOT persisted (TUN is an explicit
    // gesture) and auto-cleared on every wire-MOX-off edge by the ctor's
    // self-wired safety.
    const bool prev = tuneEnabled_.exchange(on, std::memory_order_relaxed);
    if (prev == on) return;
    // Forward to the registered postgen callback (no-op if TX control
    // not yet registered — same pattern as the other setters).
    {
        std::function<void(bool)> fwd;
        {
            std::lock_guard<std::mutex> lk(txControlMtx_);
            fwd = txControl_.setTune;
        }
        if (fwd) fwd(on);
    }
    // P4.b TUN zero-beat: re-push the TX NCO so the ∓cw_pitch DDS offset
    // is applied (on) / removed (off) — txDdsHzForTune reads the
    // tuneEnabled_ just set above.  Pairs with the ±cw_pitch postgen tone
    // (the setTune fwd) → net carrier at the dial (Thetis chkTUN order).
    if (lyra::wire::prn != nullptr) {
        const quint32 dial = txFreqHz_.load(std::memory_order_relaxed);
        const int dds = txDdsHzForTune(dial);
        lyra::wire::set_tx_freq(dds);
        // TUN-zero-beat diagnostic: shows the dial vs the offset TX NCO so
        // the bench log pins where the carrier should land vs where it does.
        qInfo("[tx] TUN %s: dial=%u txDds(NCO)=%d off=%d mode=%d(USB=1) cw_pitch=%d"
              "  (gen1 tone = %s%d; net carrier should = dial)",
              on ? "ON" : "off",
              dial, dds, dds - static_cast<int>(dial),
              txMode_.load(std::memory_order_relaxed), kTuneCwPitchHz,
              (txMode_.load(std::memory_order_relaxed) == 1) ? "+" : "-",
              kTuneCwPitchHz);
    }
    emit tuneEnabledChanged(on);
    // P4.b TUN display-honesty: tell the panadapter the NCO−dial offset so
    // its TX-state crop renders the carrier at the dial (on the marker).
    emit txAnalyzerOffsetChanged(txAnalyzerOffsetHz());
    safetyLog(QStringLiteral("TX: tune-carrier -> %1")
              .arg(on ? QStringLiteral("ARMED  (WDSP postgen single tone, ±1 kHz per sideband — emits while MOX active)")
                      : QStringLiteral("disarmed (postgen stopped)")));
}

// ---------------------------------------------------------------
// TX-0c-fsm: MOX/PTT sequencer — single funnel `requestMox(bool)`,
// internal TR-sequenced state machine.  All steps run on this
// QObject's thread via QTimer::singleShot (no sleeps, no blocking).
// Wire workers see the resulting mox_ / txStepAttnDb_ atomic flips
// the same way they did before.
//
// State is captured by three plain bool/int fields owned by this
// thread (requestedMox_ / fsmRunning_ / savedTxStepAttn_), plus
// moxActive_ for the UI-visible "on the air" truth.  Re-entrancy
// is handled by re-reading requestedMox_ at every timer boundary —
// an operator cancel mid-keydown unwinds cleanly, a re-key during
// the keyup space window collapses to stay TX (standard HF-rig
// PTT-FSM pattern).

void HL2Stream::requestMox(bool on) {
    requestMox(on, PttSource::Manual);
}

void HL2Stream::requestMoxFromHwPtt(bool on) {
    requestMox(on, PttSource::HwPtt);
}

// Stage 2b2-fix-v2 — HW-PTT-in poll slot, Qt main thread.
// Mirrors the reference's "FSM consumer polls prn->ptt_in and
// edge-detects on its own clock" posture (no wire-thread push
// callback, no QueuedConnection slot hop per datagram).  Reads
// the level the EP6 thread wrote at the most-recent status
// decode (`Ep6RecvThread.cpp:866` unconditional write per the
// reference `networkproto1.c:496` semantic).  Edge state lives
// in `lastHwPttLevel_` (plain bool, Qt-main-thread sole writer
// + reader — no synchronisation needed).  Opt-in gated by
// `hwPttEnabled_` per §10 Q#1 (N8SDR HL2+/AK4951 non-zero
// ptt_in at RX rest; default-OFF safety until operator opts
// in after a per-unit bench check).
void HL2Stream::onHwPttPoll() {
    if (lyra::wire::prn == nullptr) return;          // stream torn down

    // #105 follow-up — paddle / straight-key / external-keyer CW meter flip.
    // Console CW (CWX) drives cwKeyingActive precisely via the keyer's onState
    // hook.  A hardware key into the radio jack has NO host-side signal, and
    // on this HL2+ the EP6 ptt_in line reads non-zero at RX rest (§10 Q#1), so
    // it can't be trusted for keyed detection.  Forward power IS clean (~0 at
    // rest, spikes on every keyed element) and covers paddle / straight-key /
    // external keyers uniformly.  In CW mode, when a CWX message isn't already
    // driving the state, latch cwKeyingActive on forward power above a small
    // floor and hold it for a hang so the meter doesn't chatter between
    // elements.  Display-only — never asserts the wire MOX bit (QSK keying
    // unchanged).  fwd>=floor is false for NaN, so a missing telemetry frame
    // reads as "not keyed" (no spurious flip).
    {
        constexpr double kCwKeyDetectW   = 0.5;  // W — clearly keyed RF
        constexpr int    kCwKeyHangTicks = 10;   // × ~50 ms ≈ 500 ms hold
        const int  tmCw    = txMode_.load(std::memory_order_relaxed);
        const bool cwMode  = (tmCw == 3 || tmCw == 4);
        const bool cwxBusy = cwKeyer_ && cwKeyer_->busy();
        if (cwMode && !cwxBusy) {
            const bool rf = (fwdPowerW() >= kCwKeyDetectW);
            if (rf) {
                cwKeyHangTicks_ = kCwKeyHangTicks;          // (re)arm the hang
                if (!cwKeyingActive_) setCwKeyingActive(true);
            } else if (cwKeyingActive_ && cwKeyHangTicks_ > 0
                       && --cwKeyHangTicks_ == 0) {
                setCwKeyingActive(false);                   // hang expired → RX
            }
        } else if (!cwMode && cwKeyingActive_ && !cwxBusy) {
            cwKeyHangTicks_ = 0;        // left CW mode mid-hold → drop now
            setCwKeyingActive(false);
        }
    }
    // #105 CW (Thetis QSK / firmware keyer) — the HL2's internal CW keyer
    // raises the SAME EP6 PTT/key line on every keyed element
    // (`ptt_resp = cw_on | ext_ptt`, hl2_rtl_radio.v:456).  In CW with the
    // firmware keyer the gateware keys the carrier AUTONOMOUSLY and the host
    // must stay in RECEIVE — the reference gates its whole PollPTT host-MOX
    // block on `!QSKEnabled` (console.cs:26010) so a paddle never flips the
    // host to TX.  Lyra previously forwarded this line to host MOX, which
    // switched the panadapter to the TX analyzer on every dit and painted the
    // keyed carrier off the marker (operator bench).  Match the reference:
    // do NOT forward host MOX from this line in firmware-keyer CW; the keyed
    // carrier is shown correctly on the marker by the (unchanged) RX path.
    // Keep lastHwPttLevel_ tracking the wire so a return to SSB doesn't fire a
    // stale edge.  (Manual/foot-switch TX in CW is a future break-in-mode
    // option, matching the reference's Semi/Manual paths.)
    const int tm = txMode_.load(std::memory_order_relaxed);
    // Suppress host MOX off the key line WHENEVER the firmware keyer is armed
    // in CW — in EVERY break-in mode, not just QSK.  #186 (operator BY-2 in the
    // HL2+ KEY jack, break-in = Semi): the HL2 gateware keyer keys the carrier
    // AUTONOMOUSLY and REQUIRES the wire MOX bit = 0 to do so (Protocol.md: "The
    // MOX bit must be sent as zero when sending CW").  On the HL2, QSK / Semi /
    // Manual differ ONLY in the gateware's `cw_hang_time` (the CWHANG state) —
    // NONE of them want the host to assert MOX.  The old QSK-only gate (copied
    // from the reference's `!QSKEnabled` PollPTT gate) let Semi/Manual forward
    // the paddle's key line to host MOX -> full SSB MOX_TX -> the gateware's
    // iambic keyer was bypassed and each lever just gated the carrier ("acts
    // like 2 keys", operator + bench cwwire log: MOX=1 while CWMode=IAMBIC-A).
    // The reference's per-mode host-MOX split is a reference-architecture quirk,
    // not right for the HL2 firmware keyer.  Break-in behaviour is preserved:
    // the operator's Semi 295 ms hang is delivered by the gateware, host stays
    // RX.  (A non-firmware-keyer path — cwFwKeyer_ off, e.g. external straight
    // key / foot-switch PTT — still host-MOXes off this line, unchanged.)
    const bool cwFwKeyerNoHostMox =
        cwFwKeyer_ && (tm == 3 || tm == 4);   // firmware keyer armed in CW
    if (!hwPttEnabled_.load(std::memory_order_acquire) || cwFwKeyerNoHostMox) {
        // Gate closed (opt-in off) OR QSK CW — keep lastHwPttLevel_ tracking
        // the wire so a re-enable / mode change doesn't fire a spurious edge.
        lastHwPttLevel_ = (lyra::wire::prn->ptt_in != 0);
        return;
    }
    const bool now = (lyra::wire::prn->ptt_in != 0);
    if (now != lastHwPttLevel_) {
        lastHwPttLevel_ = now;
        requestMoxFromHwPtt(now);
    }
}

// #91 — VOX gate tick.  Runs on the Qt main thread (50 ms cadence) as
// the SOLE owner of vox_ / voxOwnsKey_ / voxKeying_.  Reads the EP6
// mic RMS (wire thread → atomic) + the anti-VOX RX-audio RMS (audio
// thread → atomic) and drives the funnel via requestMoxFromVox with
// the multi-source safety Thetis's PollPTT enforces: VOX never opens
// while another source keys, and releases ONLY a key it raised itself.
void HL2Stream::onVoxPoll() {
    auto setKeying = [this](bool v) {
        if (voxKeying_ != v) { voxKeying_ = v; emit voxKeyingChanged(v); }
    };

    // Publish the live mic level (dBFS) ALWAYS — before any gating — so
    // the Settings VOX meter reads out even with VOX disabled / in CW,
    // giving the operator a reference to dial the threshold against.
    const double micRms = lyra::wire::ep6_mic_rms_lin();
    const double micDb  = micRms > 1e-9 ? 20.0 * std::log10(micRms) : -200.0;
    if (std::abs(micDb - voxMicDbfs_) > 0.1) {   // ~0.1 dB deadband, no spam
        voxMicDbfs_ = micDb;
        emit voxMicDbfsChanged(micDb);
    }

    // Publish the live RX-audio level (dBFS) too — the anti-VOX reference
    // (the "what the operator hears" RMS the gate compares against).  Same
    // always-before-gating treatment as the mic level so the Settings
    // anti-VOX meter stays live for dialing even with VOX off / in CW.
    const double rxRms = voxRxRmsProvider_ ? voxRxRmsProvider_()
                       : voxRxRmsLin_.load(std::memory_order_relaxed);
    const double rxDb  = rxRms > 1e-9 ? 20.0 * std::log10(rxRms) : -200.0;
    if (std::abs(rxDb - voxRxDbfs_) > 0.1) {     // ~0.1 dB deadband, no spam
        voxRxDbfs_ = rxDb;
        emit voxRxDbfsChanged(rxDb);
    }

    // Reconcile: our key vanished from under us (manual/foot-switch
    // release, TX-timeout, SWR cut, mode flip) — drop ownership + reset
    // so the gate restarts clean on the next mic crossing.
    if (voxOwnsKey_ &&
        (!mox_.load(std::memory_order_relaxed) || pttSource() != PttSource::Vox)) {
        voxOwnsKey_ = false;
        vox_.reset();
        setKeying(false);
    }

    // Gating (mirrors console.cs PollPTT): VOX active only when enabled,
    // in a voice mode (NOT CW: txMode_ 3=CWL / 4=CWU), and not inhibited.
    // Any gate off → make safe: release our own key, reset, idle.
    const int  tm     = txMode_.load(std::memory_order_relaxed);
    const bool cwMode = (tm == 3 || tm == 4);
    if (!voxEnabled_ || cwMode || txInhibit_) {
        if (voxOwnsKey_) {
            requestMoxFromVox(false);
            voxOwnsKey_ = false;
            setKeying(false);
        }
        vox_.reset();
        return;
    }

    // Anti-VOX RX level (rxRms computed at top with the readout).
    const bool   want    = vox_.tick(micRms, rxRms,   // micRms/rxRms at top
                                     static_cast<double>(hwPttTimer_.interval()));

    if (want && !voxOwnsKey_) {
        // Open edge — take the key ONLY if nobody else holds it (wire OR
        // intent).  If another source is keying, stay out of the way; the
        // reconcile block re-acquires cleanly once they release.
        if (!mox_.load(std::memory_order_relaxed) && !requestedMox_) {
            requestMoxFromVox(true);
            voxOwnsKey_ = true;
            setKeying(true);
        }
    } else if (!want && voxOwnsKey_) {
        // Close edge — release ONLY a key this gate raised.
        if (mox_.load(std::memory_order_relaxed) && pttSource() == PttSource::Vox) {
            requestMoxFromVox(false);
        }
        voxOwnsKey_ = false;
        setKeying(false);
    }
}

void HL2Stream::applyVoxParams() {
    lyra::tx::VoxDetector::Params p;
    p.thresholdDbfs = voxThresholdDbfs_;
    p.antiVoxDbfs   = voxAntiVoxDbfs_;
    p.openMs        = voxOpenMs_;
    p.hangMs        = voxHangMs_;
    p.antiVoxOn     = voxAntiVoxOn_;
    vox_.setParams(p);
}

void HL2Stream::setVoxEnabled(bool on) {
    if (voxEnabled_ == on) return;
    voxEnabled_ = on;
    QSettings().setValue(QStringLiteral("tx/voxEnabled"), on);
    if (!on) {
        // Turning VOX off must drop a VOX-held key immediately (don't
        // wait for the next poll) so the operator can't be left keyed.
        if (voxOwnsKey_ && mox_.load(std::memory_order_relaxed)
            && pttSource() == PttSource::Vox) {
            requestMoxFromVox(false);
        }
        voxOwnsKey_ = false;
        vox_.reset();
        if (voxKeying_) { voxKeying_ = false; emit voxKeyingChanged(false); }
    }
    emit voxEnabledChanged(on);
}

void HL2Stream::setVoxThresholdDbfs(double dbfs) {
    dbfs = std::clamp(dbfs, -80.0, 0.0);
    if (voxThresholdDbfs_ == dbfs) return;
    voxThresholdDbfs_ = dbfs;
    QSettings().setValue(QStringLiteral("tx/voxThresholdDbfs"), dbfs);
    applyVoxParams();
}

void HL2Stream::setVoxAntiVoxDbfs(double dbfs) {
    dbfs = std::clamp(dbfs, -90.0, 0.0);
    if (voxAntiVoxDbfs_ == dbfs) return;
    voxAntiVoxDbfs_ = dbfs;
    QSettings().setValue(QStringLiteral("tx/voxAntiVoxDbfs"), dbfs);
    applyVoxParams();
}

void HL2Stream::setVoxOpenMs(int ms) {
    ms = std::clamp(ms, 0, 500);
    if (voxOpenMs_ == ms) return;
    voxOpenMs_ = ms;
    QSettings().setValue(QStringLiteral("tx/voxOpenMs"), ms);
    applyVoxParams();
}

void HL2Stream::setVoxHangMs(int ms) {
    ms = std::clamp(ms, 0, 3000);
    if (voxHangMs_ == ms) return;
    voxHangMs_ = ms;
    QSettings().setValue(QStringLiteral("tx/voxHangMs"), ms);
    applyVoxParams();
}

void HL2Stream::setVoxAntiVoxOn(bool on) {
    if (voxAntiVoxOn_ == on) return;
    voxAntiVoxOn_ = on;
    QSettings().setValue(QStringLiteral("tx/voxAntiVoxOn"), on);
    applyVoxParams();
}

void HL2Stream::requestMoxFromTci(bool on) {
    requestMox(on, PttSource::Tci);
}

void HL2Stream::requestMoxFromSerialPtt(bool on) {
    requestMox(on, PttSource::Serial);
}

// #91 — VOX gate edge.  Same funnel as the other auto-PTT sources
// (wire behaviour identical to requestMox(bool, PttSource::Vox)); the
// onVoxPoll safety layer guarantees this only fires (on=true) when no
// higher-priority source holds the key, and (on=false) only for a key
// this gate itself raised — so VOX never stomps a foot-switch / manual
// MOX.  Adds NOTHING to the TX DSP chain (PureSignal-safe): VOX only
// decides WHEN to key.
void HL2Stream::requestMoxFromVox(bool on) {
    requestMox(on, PttSource::Vox);
}

// #171 — serial straight-key / external-keyer edge.  Drives the #105 CWX
// wire path (tx[0].cwx + cwx_ptt) directly off the incoming key state — no
// host iambic, the operator/keyer owns the timing.  cwx_ptt is held across
// the CW break-in hang on key-up so quick inter-element gaps don't drop TX.
void HL2Stream::requestCwKeyFromSerial(bool down) {
    const int tm = txMode_.load(std::memory_order_relaxed);
    if (!(tm == 3 || tm == 4)) {            // CW mode only (cw_enable gate)
        if (!down) {                        // mode left CW mid-key → make safe
            if (serialCwHangTimer_) serialCwHangTimer_->stop();
            setCwxKey(false);
            setCwxPtt(false);
            setCwKeyingActive(false);
        }
        return;
    }
    if (down) {
        if (serialCwHangTimer_) serialCwHangTimer_->stop();   // cancel pending drop
        setCwxPtt(true);          // hold TX (message level)
        setCwxKey(true);          // key the element
        setCwKeyingActive(true);  // UI display flip (meter RX↔TX)
    } else {
        setCwxKey(false);         // unkey the element
        if (!serialCwHangTimer_) {
            serialCwHangTimer_ = new QTimer(this);
            serialCwHangTimer_->setSingleShot(true);
            connect(serialCwHangTimer_, &QTimer::timeout, this, [this] {
                setCwxPtt(false);          // hang expired → drop TX to RX
                setCwKeyingActive(false);
            });
        }
        serialCwHangTimer_->start(std::max(0, cwHangDelayMs_));
    }
}

int HL2Stream::pushWaterfallIdAudio(const QString &callsign, double level,
                                    bool lsb) {
    // #175 bench (increment 2a).  Render the call → a 48 kHz mono raster
    // (matches the TXA in-rate, kTxInRate=48000) and inject it as the sole
    // TX source via the TCI TX-audio bridge — the proven digital-audio seam
    // (the WDSP cm_main pump drains it I=Q=mono when the source is TCI).
    // Returns the burst length in ms so the QML trigger can schedule the
    // keyup; 0 = blank call / nothing rendered → caller does NOT key.
    lyra::dsp::WaterfallIdParams p;   // 48 kHz; high-res transposed raster
    p.level = std::clamp(level, 0.0, 0.065);   // operator WF-ID Level (Prefs)
    p.reverseFreq = lsb;              // LSB pre-mirror (DIGL flips audio↔RF)
    const std::vector<float> buf = lyra::dsp::WaterfallId::render(callsign, p);
    if (buf.empty()) {
        safetyLog(QStringLiteral("#175 WF-ID: blank/empty render — not keying "
                                 "(set callsign in Settings -> Hardware)"));
        return 0;
    }
    auto &bridge = lyra::tci::TciTxBridge::instance();
    bridge.clear();
    bridge.pushMono(buf);
    const int ms = static_cast<int>(buf.size() * 1000 /
                                    static_cast<std::size_t>(p.sampleRate));
    safetyLog(QStringLiteral("#175 WF-ID bench: queued %1 samples (%2 ms) for '%3' "
                             "— ensure mic source = TCI + DIGU (flat) for the burst")
                  .arg(buf.size()).arg(ms).arg(callsign));
    return ms;
}

void HL2Stream::requestMox(bool on, PttSource source) {
    // #94 External TX Inhibit — hard lockout at the single keying funnel.
    // Block EVERY keydown intent (covers Manual / HW-PTT / TCI / CW / TUN /
    // Auto-ID — they all route through here).  Keyup (on=false) ALWAYS passes
    // so an in-flight TX can always fall back to RX.
    if (on && txInhibit_) {
        safetyLog(QStringLiteral("TX: keying BLOCKED — External TX Inhibit is on"));
        return;
    }
    // #201 — no TX while the offline MP4 converter is running (TX ⇄ convert
    // mutual lockout).  Transient; keyup always passes.
    if (on && convertLockout_) {
        safetyLog(QStringLiteral("TX: keying BLOCKED — a recording is converting to MP4"));
        return;
    }
    requestedMox_ = on;
    // Task #33 — Thetis-faithful PTT-source tracking (working
    // reference at TCIServer.cs:3537 routes TCI MOX through a
    // separate TCIPTT property; Lyra records the source as metadata
    // on the SAME FSM funnel, wire-identical, so subscribers like
    // TciServer can branch on it in their moxActiveChanged handler
    // — e.g. clearQueuedTxAudio only when PttSource::Tci releases).
    //
    // Record the source on the rising edge AND on a re-key (source
    // change mid-TX = operator handed off, e.g. operator MOX during
    // a TCI session); cleared back to Manual when the FSM fully
    // settles to RX (fsmKeyupSettled → resetPttSource()).  This
    // matches the working reference's TCIPTT lifecycle.
    if (on) {
        pttSource_.store(source, std::memory_order_release);
    }
    // Fire the press-intent pulse on EVERY keydown intent — even when
    // the FSM is already running (re-key during ptt_out_delay), and
    // even when the press is shorter than the TR delay (FSM cancels
    // mid-mox_delay).  The UI uses this to show the operator that
    // their press was acknowledged, separate from whether the wire
    // MOX bit ever settles.  Keyup releases (on=false) do NOT pulse —
    // the operator already saw the red light if they were keyed.
    if (on) {
        emit moxIntentPulse();
    }
    if (!fsmRunning_) {
        fsmAdvance();
    }
    // If fsmRunning_ is true the chain in flight will see the new
    // intent at its next timer boundary — no race with the wire
    // worker because all FSM bookkeeping is single-thread.
}

void HL2Stream::fsmAdvance() {
    // Decide what (if anything) to do based on intent vs wire state.
    const bool wireOn = mox_.load(std::memory_order_relaxed);
    if (requestedMox_ && !wireOn) {
        // Keydown: raise ATT-on-TX, then schedule MOX bit set after
        // mox_delay (gives the RX path time to attenuate before the
        // gateware T/R relay engages).
        fsmRunning_ = true;
        savedTxStepAttn_ = txStepAttnDb_.load(std::memory_order_relaxed);
        // §15.31: operator-gated ATT-on-TX.  Enabled → force the
        // configured protective value (default 31 dB → wire min LNA
        // gain); disabled → axis 0 (= the reference's SetTxAttenData(0)
        // "no ATT on TX" posture, RX-ADC unprotected during TX).
        setTxStepAttnDb(attOnTxEnabled_ ? attOnTxDb_ : 0);
        // Switch the OC pattern to the TX-side per-band bits FIRST so
        // the external filter board's relays have the mox_delay window
        // (~15 ms) to settle into TX configuration before the MOX bit
        // hits the wire and any RF starts.  On N2ADR this is a no-op at
        // the LPF level (RX/TX use the same per-band LPF) but it does
        // drop the RX-only 3 MHz HPF; on other filter boards where TX
        // LPF and RX BPF are on different relays, this gives them time
        // to settle before RF appears.  Hot-switch safety.
        updateOcPattern(/*transmitting=*/true);
        emit logLine(QStringLiteral(
            "TX: keydown — ATT-on-TX %1, mox_delay %2 ms")
            .arg(attOnTxEnabled_ ? QStringLiteral("%1 dB").arg(attOnTxDb_)
                                 : QStringLiteral("off"))
            .arg(moxDelayMs_));
        QTimer::singleShot(moxDelayMs_, this,
                           [this]() { fsmKeydownPostMox(); });
    } else if (!requestedMox_ && wireOn) {
        // Keyup: schedule the space_mox re-key window.  Don't touch
        // mox_ yet — spaceMoxDelayMs_ gives the operator a chance to
        // re-key (CW dot tail, mic re-key) before the wire flip.
        fsmRunning_ = true;
        emit logLine(QStringLiteral(
            "TX: keyup — space_mox_delay %1 ms (re-key window)")
            .arg(spaceMoxDelayMs_));
        QTimer::singleShot(spaceMoxDelayMs_, this,
                           [this]() { fsmKeyupPostSpace(); });
    } else {
        // Nothing to do: intent already matches wire state.
        fsmRunning_ = false;
    }
}

void HL2Stream::fsmKeydownPostMox() {
    if (!requestedMox_) {
        // Operator cancelled mid-keydown (before the wire MOX bit
        // even went on) — restore the saved step-att + OC RX pattern
        // and exit clean.  Symmetric with the fsmAdvance keydown raise.
        setTxStepAttnDb(savedTxStepAttn_);
        updateOcPattern(/*transmitting=*/false);
        safetyLog(QStringLiteral(
            "TX: keydown cancelled mid-mox_delay; ATT-on-TX restored"));
        fsmRunning_ = false;
        return;
    }
    // Flip the wire MOX bit — visible on the next EP2 datagram
    // (~2.6 ms worst case at 380 Hz cadence).  C0 bit 0 on both USB
    // frames of the next datagram (composeCC snapshots mox_ once
    // per send) — coherent.  Reference parity per console.cs:30332
    // (HdwMOXChanged → SetTRXrelay(1) + SetPttOut(1)) + console.cs:
    // 30335 (cmaster.Mox = tx — loads XmitBit the EP2 packer reads).
    // Lyra collapses HdwMOXChanged + cmaster.Mox into ONE atomic
    // since the EP2 packer's C0-bit-0 emission IS XmitBit.
    mox_.store(true, std::memory_order_release);
    // §5 control-plane mapping (TX): the verbatim write_main_loop_hl2
    // (compose_case_0 C0 + the writer's `!XmitBit ⇒ memset(outIQbufp)`
    // gate) reads the GLOBAL XmitBit — NOT mox_, the FSM atomic the
    // retired composeCC snapshotted.  Bridge it here, at the same site
    // the reference loads it (cmaster.Mox = tx, console.cs:30335), so
    // the wire MOX bit sets AND the writer stops zeroing — letting the
    // mic→TXA→OutBound(1) modulator I/Q reach the wire.  Set alongside
    // mox_ so the §15.25 keydown ordering already in place governs it.
    lyra::wire::XmitBit = 1;

    // §15.25 keydown ordering CORRECTED 2026-06-09 per Thetis
    // console.cs:30342-30345 read 3× verified:
    //   line 30342-30343:  if (rf_delay > 0) Thread.Sleep(rf_delay);
    //   line 30344:        AudioMOXChanged(tx);
    //   line 30345:        WDSP.SetChannelState(WDSP.id(1, 0), 1, 0);
    //
    // i.e. WDSP TXA channel start happens AFTER rf_delay, NOT BEFORE.
    // The previous Lyra ordering (startFn + setInjectTxIq before the
    // QTimer) began the WDSP TXA cos² up-ramp while the gateware T/R
    // relay was still settling for the full rfDelayMs_ window — an
    // external linear with hot-switch protection saw TX I/Q on the
    // wire during its own settle period.  Fixed by moving startFn()
    // and setInjectTxIq(true) into fsmKeydownSettled (after the rf_delay
    // QTimer fires), which is exactly Thetis's order.
    QTimer::singleShot(rfDelayMs_, this,
                       [this]() { fsmKeydownSettled(); });
}

void HL2Stream::fsmKeydownSettled() {
    if (!requestedMox_) {
        // Cancelled mid-rf_delay — wire MOX already went on (set in
        // fsmKeydownPostMox), but WDSP TXA has NOT started yet (the
        // §15.25 keydown-ordering CORRECTION moved that to here).
        // Unwind through the real keyup chain: stopFn() is a no-op
        // when the channel was never started (TxChannel.stop guards
        // on running_), so the unwind is safe.
        safetyLog(QStringLiteral(
            "TX: keydown cancelled mid-rf_delay; initiating keyup"));
        QTimer::singleShot(spaceMoxDelayMs_, this,
                           [this]() { fsmKeyupPostSpace(); });
        return;
    }
    // §15.25 keydown ordering CORRECTED 2026-06-09 per Thetis
    // console.cs:30344-30345 (read 3× verified):
    //   line 30344:  AudioMOXChanged(tx);     // audio routing FIRST
    //   line 30345:  WDSP.SetChannelState(WDSP.id(1, 0), 1, 0);
    //                                          // TX TXA start LAST
    //
    // setInjectTxIq(true) is the Lyra equivalent of AudioMOXChanged
    // (arms the EP2 packer's SSB-IQ branch); startFn() is the Lyra
    // equivalent of SetChannelState(id(1,0),1,0) (starts the WDSP
    // TXA cos² up-ramp).  This runs AFTER the rfDelayMs_ QTimer
    // fired — so the gateware T/R relay has settled and the linear's
    // hot-switch protection window has elapsed before any TX I/Q
    // hits the wire.  Reference parity restored.
    setInjectTxIq(true);
    {
        std::function<void()> startFn;
        {
            std::lock_guard<std::mutex> lk(txControlMtx_);
            startFn = txControl_.start;
        }
        if (startFn) startFn();
    }
    moxActive_ = true;
    emit moxActiveChanged(true);
    safetyLog(QStringLiteral("TX: MOX_TX (wire MOX + TXA settled)"));
    fsmRunning_ = false;
    // If the operator already requested off again during rf_delay
    // (rare), pick it up now.
    if (!requestedMox_) {
        fsmAdvance();
    }
}

void HL2Stream::fsmKeyupPostSpace() {
    if (requestedMox_) {
        // Re-key during the space window — collapse: stay TX, do not
        // flip the wire bit.  mox_ is still true on the wire,
        // moxActive_ remains true, no events emitted.
        emit logLine(QStringLiteral(
            "TX: re-keyed during space_mox window — staying TX"));
        fsmRunning_ = false;
        return;
    }

    // §15.25 keyup ordering CORRECTED 2026-06-09 — read Thetis
    // console.cs:30350-30383 THREE TIMES verified.  The previous
    // codification of this invariant said "(1) Clear inject_tx_iq
    // first" which was REFERENCE-DIVERGENT — Thetis actually does:
    //   line 30352-30353:  if (space_mox_delay > 0)
    //                          Thread.Sleep(space_mox_delay);
    //   line 30355:        _mox = tx;  // C# in-memory only
    //   line 30357:        WDSP.SetChannelState(WDSP.id(1,0), 0, 1);
    //                                            // TXA stop BLOCKING (FIRST)
    //   line 30367-30368:  if (mox_delay > 0)
    //                          Thread.Sleep(mox_delay);
    //   line 30373:        AudioMOXChanged(tx);  // EP2 branch zero
    //   line 30374:        HdwMOXChanged(tx, freq);  // SetPttOut(0)
    //   line 30376:        cmaster.Mox = tx;     // XmitBit = 0
    //   line 30377-30378:  if (ptt_out_delay > 0)
    //                          Thread.Sleep(ptt_out_delay);
    //   line 30379:        WDSP.SetChannelState(WDSP.id(0,0), 1, 0);
    //                                            // RX restart
    //
    // The KEY: WDSP TXA stop BLOCKING is FIRST (so the cos² downslew
    // tail flows out fexchange0 → EP2 packer → wire WHILE XmitBit is
    // still 1).  AudioMOXChanged (= Lyra's setInjectTxIq(false)) only
    // fires AFTER mox_delay.  HdwMOXChanged + cmaster.Mox (= Lyra's
    // mox_.store(false)) fire right after — they're the actual wire
    // MOX-bit drop.  Then ptt_out_delay → RX restart.
    //
    // Lyra ordering (post-correction):
    //   fsmKeyupPostSpace:  stopFn() BLOCKING, then QTimer(txStopDelayMs_)
    //                       → fsmKeyupTxOff
    //   fsmKeyupTxOff:      setInjectTxIq(false), mox_.store(false),
    //                       then QTimer(pttOutDelayMs_) → fsmKeyupSettled
    //   fsmKeyupSettled:    (RX restart N/A in Lyra-cpp v0.2.0 SSB —
    //                       RX-DSP stays running through TX with audio
    //                       muted via _tx_rx_muted gate; re-evaluate at
    //                       v0.2.2 CW work).  Cleanup + emit.
    //
    // The fsmKeyupInFlight stage in the previous design folded into
    // fsmKeyupTxOff post-correction (its only job — clear wire MOX bit
    // after txStopDelayMs_ — now happens in fsmKeyupTxOff alongside
    // setInjectTxIq).
    {
        std::function<void()> stopFn;
        {
            std::lock_guard<std::mutex> lk(txControlMtx_);
            stopFn = txControl_.stop;
        }
        if (stopFn) stopFn();
    }
    // Schedule the in-flight clear wait.  Thetis line 30367-30368
    // mox_delay (default 10 ms) lets WDSP-internal cos² downslew
    // samples emitted post-stop reach the HL2 + be packed at the
    // wire WHILE XmitBit is still 1, so the operator's keyed-tail
    // SSB envelope arrives on-air cleanly rather than being chopped
    // by a premature AudioMOXChanged.
    QTimer::singleShot(txStopDelayMs_, this,
                       [this]() { fsmKeyupTxOff(); });
}

void HL2Stream::fsmKeyupTxOff() {
    // §15.25 keyup ordering CORRECTED 2026-06-09 — this step now
    // implements Thetis console.cs:30373-30376 (the two-step wire
    // teardown: AudioMOXChanged → HdwMOXChanged + cmaster.Mox).
    //
    // (1) setInjectTxIq(false) is Lyra's AudioMOXChanged equivalent
    //     — arms the EP2 packer's else-branch (zero TX I/Q) regardless
    //     of XmitBit.  Until this fires, the EP2 packer continues to
    //     emit whatever the (stopped) WDSP TXA chain has left in
    //     out[0] — which after stopFn() BLOCKING + txStopDelayMs_ is
    //     a fully-faded silent buffer.
    //
    // (2) mox_.store(false) is Lyra's HdwMOXChanged + cmaster.Mox
    //     equivalent — flips C0 bit 0 on the next EP2 datagram, which
    //     the HL2 gateware sees as PTT-out drop + T/R relay release.
    setInjectTxIq(false);
    mox_.store(false, std::memory_order_release);
    // §5 control-plane mapping (TX): drop the wire MOX gate alongside
    // mox_ — the writer resumes `!XmitBit ⇒ memset(outIQbufp)` so TX I/Q
    // goes silent.  Fires AFTER the keyup TXA-stop blocking flush +
    // txStopDelayMs_ (§15.25), so the faded cos² tail already reached
    // the wire while XmitBit was still 1.
    lyra::wire::XmitBit = 0;
    // ptt_out_delay (Thetis line 30377-30378) gives the hardware T/R
    // relay time to physically switch back to RX before any RX-side
    // restoration logic runs.  Then fsmKeyupSettled does cleanup.
    QTimer::singleShot(pttOutDelayMs_, this,
                       [this]() { fsmKeyupSettled(); });
}

void HL2Stream::fsmKeyupSettled() {
    if (requestedMox_) {
        // Operator re-keyed in the ~5 ms ptt_out window — the wire
        // bit cleared briefly, now needs to go back on.  Unwind to
        // the keydown path through fsmAdvance.
        emit logLine(QStringLiteral(
            "TX: re-keyed during ptt_out_delay — re-keying"));
        fsmRunning_ = false;
        fsmAdvance();
        return;
    }
    // Restore the operator's pre-keydown step-att (today always 0;
    // forward-compat for a future operator-tunable RX step-att).
    setTxStepAttnDb(savedTxStepAttn_);
    // Switch the OC pattern back to the RX-side per-band bits.  This
    // happens AFTER the wire MOX bit has cleared (in fsmKeyupTxOff per
    // the §15.25 corrected order) AND ptt_out_delay has elapsed — so
    // the filter board only returns to RX configuration when no RF is
    // being emitted.  Mirror of the fsmAdvance keydown raise; symmetric
    // hot-switch safety.
    updateOcPattern(/*transmitting=*/false);
    moxActive_ = false;
    emit moxActiveChanged(false);
    safetyLog(QStringLiteral(
        "TX: RX (wire MOX cleared, ATT-on-TX restored to %1 dB)")
        .arg(savedTxStepAttn_));
    // Task #33 — release PTT-source tracking back to Manual now that
    // the FSM has fully settled to RX.  Order matters: AFTER emitting
    // moxActiveChanged(false) so subscribers reading pttSource() in
    // their slot see the source that just released, not the default
    // post-release state.  Equivalent to the working reference's
    // m_tciPttActive clearing in SyncTciPttToMox / TRX:0,false.
    pttSource_.store(PttSource::Manual, std::memory_order_release);
    fsmRunning_ = false;
}

// ---------------------------------------------------------------
// TX-0c-pa-debug: host-side TX safety timeout.
//
// QTimer-based auto-MOX-off so a stuck transmitter (held key, asleep
// at the desk, runaway PTT) doesn't hold the air for hours.  Arms on
// the wire MOX-on edge (moxActiveChanged(true)), cancels on the
// MOX-off edge.  On fire we route through requestMox(false) — same
// path as a normal key-up, so the FSM runs the keyup TR-delay chain
// cleanly + ATT-on-TX is restored.  Bypass switches the safety off
// entirely (long-form AM ragchews, CW beacons, bench debugging).

void HL2Stream::setTxTimeoutSec(int sec) {
    sec = std::clamp(sec, kTxTimeoutMinSec, kTxTimeoutMaxSec);
    if (sec == txTimeoutSec_) return;
    txTimeoutSec_ = sec;
    QSettings().setValue(QStringLiteral("tx/timeoutSeconds"), sec);
    emit txTimeoutSecChanged(sec);
    safetyLog(QStringLiteral("TX safety timeout = %1 s (%2 min)")
              .arg(sec).arg(sec / 60.0, 0, 'f', 1));
    // If currently keyed, re-arm with the new full duration.  Operator
    // intent ("I want N more seconds from now") — symmetric with the
    // bypass-toggle behaviour below.
    if (moxActive_ && !txTimeoutBypass_) {
        armTxSafetyTimer();
    }
}

void HL2Stream::setTxTimeoutBypass(bool on) {
    if (on == txTimeoutBypass_) return;
    txTimeoutBypass_ = on;
    QSettings().setValue(QStringLiteral("tx/timeoutBypass"), on);
    emit txTimeoutBypassChanged(on);
    safetyLog(QStringLiteral("TX safety timeout bypass -> %1")
              .arg(on ? QStringLiteral("ON (no auto-release)")
                      : QStringLiteral("off (auto-release armed)")));
    if (moxActive_) {
        if (on)  cancelTxSafetyTimer();    // safety just turned OFF
        else     armTxSafetyTimer();       // safety just turned ON;
                                           // fresh full duration
    }
}

// ---- Task #36: Hardware PTT input forwarder opt-in ----------------
//
// Per project-memory §10 Q#1 + §15.25 RESOLVED-CORRECTION, the EP6
// C0 bit 0 (`ptt_in`) is NOT guaranteed to be a clean 0 at RX rest
// across HL2 gateware revs — N8SDR's HL2+/AK4951 unit empirically
// shows a non-zero level, which means an always-on forwarder would
// mis-read it as a foot-switch press → spurious requestMox(true) →
// phantom-TX surge.  The forwarder in rxWorkerLoop is therefore
// HARD-GATED on this atomic; default OFF; operator opts in via
// Settings → TX → Advanced AFTER bench-verifying ptt_in on their
// own unit (or after wiring a real foot-switch / mic-button).
//
// Persistence lives in Prefs (tx/hw_ptt_enabled); main.cpp mirrors
// Prefs.hwPttEnabled into this setter so the in-flight wire path
// always sees the operator's current intent without a stream
// restart.
//
// Stage 2b2: the edge-detect previous level now lives in a
// thread_local inside the Ep6RecvThread hw_ptt_sink lambda
// (registered at open()), so setHwPttEnabled() no longer needs
// to reach into the wire thread's state — toggling the gate
// from the main thread is sufficient because the lambda's
// thread_local prev is reset implicitly across stop()/start()
// cycles (the Ep6RecvThread re-enters its run_loop with
// fresh-thread storage on every start).  During an in-session
// toggle, a stale prev=true on a re-enable is harmless: the
// new-level read settles to the live wire value within a few
// EP6 frames and the next genuine edge fires correctly.
void HL2Stream::setHwPttEnabled(bool on) {
    const bool prev = hwPttEnabled_.exchange(on, std::memory_order_release);
    if (prev == on) return;
    safetyLog(QStringLiteral("HW PTT input forwarder -> %1")
              .arg(on ? QStringLiteral("ENABLED (foot-switch will key)")
                      : QStringLiteral("disabled (default-safe)")));
    emit hwPttEnabledChanged(on);
}

// ---- TX-1 component 5a: TR-sequencing + cos² envelope setters ----
//
// Each setter clamps, persists to QSettings, and emits the matching
// change signal so Settings UI bindings refresh.  All run on the Qt
// main thread (Settings UI / FSM thread — same QObject).  The FSM's
// next QTimer::singleShot scheduling reads the live member, so any
// change made while the radio is keyed takes effect on the NEXT MOX
// edge (no mid-transition perturbation).
//
// ⚠ HOT-SWITCH SAFETY: rfDelayMs_ in particular is load-bearing
// protection for external solid-state HF linears.  The setter
// honours the operator's value (clamped to [kMinFsmDelayMs,
// kMaxFsmDelayMs]) — no internal floor beyond that — because the
// operator's specific amp's switching spec is the authority, not a
// generic safe default.  Settings UI tooltips carry the hot-switch
// warning so a low value is an informed operator choice.

void HL2Stream::setMoxDelayMs(int ms) {
    const int clamped = std::clamp(ms, kMinFsmDelayMs, kMaxFsmDelayMs);
    if (clamped == moxDelayMs_) return;
    moxDelayMs_ = clamped;
    QSettings().setValue(QStringLiteral("tx/trSeq/moxDelayMs"), clamped);
    emit moxDelayMsChanged(clamped);
}

void HL2Stream::setRfDelayMs(int ms) {
    const int clamped = std::clamp(ms, kMinFsmDelayMs, kMaxFsmDelayMs);
    if (clamped == rfDelayMs_) return;
    rfDelayMs_ = clamped;
    QSettings().setValue(QStringLiteral("tx/trSeq/rfDelayMs"), clamped);
    emit rfDelayMsChanged(clamped);
}

void HL2Stream::setSpaceMoxDelayMs(int ms) {
    const int clamped = std::clamp(ms, kMinFsmDelayMs, kMaxFsmDelayMs);
    if (clamped == spaceMoxDelayMs_) return;
    spaceMoxDelayMs_ = clamped;
    QSettings().setValue(QStringLiteral("tx/trSeq/spaceMoxDelayMs"), clamped);
    emit spaceMoxDelayMsChanged(clamped);
}

void HL2Stream::setPttOutDelayMs(int ms) {
    const int clamped = std::clamp(ms, kMinFsmDelayMs, kMaxFsmDelayMs);
    if (clamped == pttOutDelayMs_) return;
    pttOutDelayMs_ = clamped;
    QSettings().setValue(QStringLiteral("tx/trSeq/pttOutDelayMs"), clamped);
    emit pttOutDelayMsChanged(clamped);
}

void HL2Stream::setTxStopDelayMs(int ms) {
    const int clamped = std::clamp(ms, kMinFsmDelayMs, kMaxFsmDelayMs);
    if (clamped == txStopDelayMs_) return;
    txStopDelayMs_ = clamped;
    QSettings().setValue(QStringLiteral("tx/trSeq/txStopDelayMs"), clamped);
    emit txStopDelayMsChanged(clamped);
}

// TX-1 component 8a — operator-tunable WDSP TXA gain stages.  Each
// setter: (1) clamps to its declared range, (2) compares vs the
// current value (no-op on no-change to avoid signal storms when QML
// bindings re-fire on a Restore-Defaults click), (3) persists to
// QSettings under tx/<key>, (4) emits the changed signal for UI
// readback, (5) forwards to the registered TxControl callback OUTSIDE
// the registration lock so TxChannel's channelMtx_ does the WDSP
// setter serialisation (matches the registerTxControl push pattern).
//
// Live-apply contract: when the operator drags the slider, the WDSP
// setter call lands on the next ~2.6 ms fexchange0 block (TxChannel's
// channelMtx_ guards the setter ↔ process() race; the operator-setter
// call simply waits at most one block period if process() is
// in-flight, which is fine for an infrequent operator click rate).
void HL2Stream::setMicGainDb(double db) {
    const double clamped = std::clamp(db, kMinMicGainDb, kMaxMicGainDb);
    if (clamped == micGainDb_) return;
    micGainDb_ = clamped;
    QSettings().setValue(QStringLiteral("tx/micGainDb"), clamped);
    emit micGainDbChanged(clamped);
    std::function<void(double)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setMicGainDb;
    }
    if (fwd) fwd(clamped);
}

void HL2Stream::setAlcMaxGainLinear(double linear) {
    // §15.27 — pass-through LINEAR factor (matches the verified
    // reference's Setup spinner-to-SetTXAALCMaxGain plumbing
    // exactly: no unit conversion at any layer between operator
    // value and WDSP API).  Clamped to [0, 120] LINEAR to match
    // the reference's spinner range.
    const double clamped = std::clamp(linear,
                                      kMinAlcMaxGainLinear,
                                      kMaxAlcMaxGainLinear);
    if (clamped == alcMaxGainLinear_) return;
    alcMaxGainLinear_ = clamped;
    QSettings().setValue(QStringLiteral("tx/alcMaxGainLinear"), clamped);
    emit alcMaxGainLinearChanged(clamped);
    std::function<void(double)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setAlcMaxGainLinear;
    }
    if (fwd) fwd(clamped);
}

// §15.27 Commit B — ALC decay (operator-tunable wcpagc release tau).
// Same persistence + forward pattern as setAlcMaxGainLinear.
void HL2Stream::setAlcDecayMs(int decay_ms) {
    const int clamped = std::clamp(decay_ms,
                                   kMinAlcDecayMs,
                                   kMaxAlcDecayMs);
    if (clamped == alcDecayMs_) return;
    alcDecayMs_ = clamped;
    QSettings().setValue(QStringLiteral("tx/alcDecayMs"), clamped);
    emit alcDecayMsChanged(clamped);
    std::function<void(int)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setAlcDecayMs;
    }
    if (fwd) fwd(clamped);
}

// §15.27 Commit B — Leveler enable.  Forwards the CURRENT max-gain
// value alongside the enable state because the WDSP run-flag setter
// pairs with the ceiling (we always want them coherent on every
// state change).  Same persistence + forward pattern as above.
void HL2Stream::setLevelerOn(bool on) {
    if (on == levelerOn_) return;
    levelerOn_ = on;
    QSettings().setValue(QStringLiteral("tx/levelerOn"), on);
    emit levelerOnChanged(on);
    std::function<void(bool,double)> fwd;
    double topLinear;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setLevelerOn;
        topLinear = levelerMaxGainLinear_;
    }
    if (fwd) fwd(on, topLinear);
}

// #109 — compute the EFFECTIVE PHROT run state and push it to the WDSP
// TX channel.  Phase rotation is a voice-only trick — it distorts digital
// waveforms (FT8/FT4/RTTY/etc.) — so the WDSP run state is the operator's
// intent AND-gated with "not a digital mode": auto-OFF in DIGU/DIGL even
// when the operator toggle is on.  Mirrors the native-rack digital bypass
// (SetTxRackBypass) and the RX EQ #59 mode gate.  WDSP TXA mode 7 = DIGU,
// 9 = DIGL (see wdspTxModeFor in main.cpp).  Called from the operator
// toggle, every TX-mode change, and channel open (registerTxControl).
void HL2Stream::applyPhrotRun() {
    const int  m       = txMode_.load(std::memory_order_relaxed);
    const bool digital = (m == 7 || m == 9);
    const bool run     = phrotEnabled_ && !digital;
    std::function<void(bool)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setPhrotRun;
    }
    if (fwd) fwd(run);
}

// #109 — PHROT (phase rotator) enable.  Persists + emits + re-derives the
// effective WDSP run state (operator intent gated by the current mode).
// Mirrors setLevelerOn.  Operator on/off, default ON; auto-OFF in digital.
void HL2Stream::setPhrotEnabled(bool on) {
    if (on == phrotEnabled_) return;
    phrotEnabled_ = on;
    QSettings().setValue(QStringLiteral("tx/phrotEnabled"), on);
    emit phrotEnabledChanged(on);
    applyPhrotRun();
}

// #107 — push the EFFECTIVE CTCSS run (operator enable AND FM mode) to the
// WDSP TX channel.  CTCSS is an FM-only modulator stage (WDSP mode 5); any
// other mode forces run=0.  Called from the operator toggle, every
// setTxMode edge, and channel open (registerTxControl).
void HL2Stream::applyCtcssRun() {
    const bool fm  = (txMode_.load(std::memory_order_relaxed) == 5);
    const bool run = ctcssEnabled_ && fm;
    std::function<void(bool)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setCtcssRun;
    }
    if (fwd) fwd(run);
}

// #107 — FM peak deviation (Hz).  Persists + emits + forwards to the WDSP
// TXA fmmod stage (mode-independent to set; only bites in FM).
void HL2Stream::setFmDeviationHz(double hz) {
    const double v = std::clamp(hz, 1000.0, 6000.0);
    if (v == fmDeviationHz_) return;
    fmDeviationHz_ = v;
    QSettings().setValue(QStringLiteral("tx/fmDeviationHz"), v);
    emit fmDeviationHzChanged(v);
    std::function<void(double)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setFmDeviation;
    }
    if (fwd) fwd(v);
}

// FM pre-emphasis mode (0=Off, 1=Comm).  Persists + emits + forwards to the
// WDSP emphasis-position setter via TxControl (no-op out of FM until the FM
// branch re-applies on entry).
void HL2Stream::setFmEmphasisMode(int mode) {
    const int v = std::clamp(mode, 0, 1);
    if (v == fmEmphasisMode_) return;
    fmEmphasisMode_ = v;
    QSettings().setValue(QStringLiteral("tx/fmEmphasisMode"), v);
    emit fmEmphasisModeChanged(v);
    std::function<void(int)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setFmEmphasis;
    }
    if (fwd) fwd(v);
}

// #107 — CTCSS sub-tone enable.  Persists + emits + re-derives the effective
// run (FM-only) via applyCtcssRun().
void HL2Stream::setCtcssEnabled(bool on) {
    if (on == ctcssEnabled_) return;
    ctcssEnabled_ = on;
    QSettings().setValue(QStringLiteral("tx/ctcssEnabled"), on);
    emit ctcssEnabledChanged(on);
    applyCtcssRun();
}

// #107 — CTCSS sub-tone frequency (snapped to the standard table).  Persists
// + emits + forwards the tone to the WDSP TXA fmmod stage.
void HL2Stream::setCtcssToneHz(double hz) {
    const double v = snapCtcssTone(hz);
    if (v == ctcssToneHz_) return;
    ctcssToneHz_ = v;
    QSettings().setValue(QStringLiteral("tx/ctcssToneHz"), v);
    emit ctcssToneHzChanged(v);
    std::function<void(double)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setCtcssFreq;
    }
    if (fwd) fwd(v);
}

// §15.31 — ATT-on-TX enable.  Persists + emits; if currently keyed,
// re-applies to the wire live so the operator sees the front-end
// attenuation engage/disengage mid-TX (the FSM otherwise only sets it
// on the keydown/keyup edges).  Disabling removes RX-ADC protection on
// TX — operator opt-out; default ON.  Runs on the Qt main thread, same
// as the FSM steps (no lock needed — the AttOnTxPolicy-precedent
// single-int prn write, mirroring setLnaGainDb).
void HL2Stream::setAttOnTxEnabled(bool on) {
    if (on == attOnTxEnabled_) return;
    attOnTxEnabled_ = on;
    QSettings().setValue(QStringLiteral("tx/attOnTxEnabled"), on);
    emit attOnTxEnabledChanged(on);
    if (mox_.load(std::memory_order_relaxed))
        setTxStepAttnDb(on ? attOnTxDb_ : 0);
    safetyLog(QStringLiteral("TX: ATT-on-TX -> %1")
              .arg(on ? QStringLiteral("ON (%1 dB)").arg(attOnTxDb_)
                      : QStringLiteral("off (RX-ADC unprotected on TX)")));
}

// #94 External TX Inhibit — hard operator lockout of all keying.  Gated at
// the requestMox funnel (see above).  Engaging while keyed forces an
// immediate unkey so the radio drops to RX the instant the operator locks
// out (e.g. just connected a scope / 2nd SDR).  Persisted (fail-safe).
void HL2Stream::setTxInhibit(bool on) {
    if (on == txInhibit_) return;
    txInhibit_ = on;
    QSettings().setValue(QStringLiteral("tx/inhibit"), on);
    emit txInhibitChanged(on);
    safetyLog(on ? QStringLiteral("TX: External TX Inhibit ENGAGED — keying locked out")
                 : QStringLiteral("TX: External TX Inhibit cleared"));
    // If we just locked out while keyed, drop to RX now (keyup always passes
    // the gate above).
    if (on && mox_.load(std::memory_order_relaxed))
        requestMox(false, PttSource::Manual);
}

// §15.31 — ATT-on-TX dB value (0..31; default 31).  Persists + emits;
// re-applies live while keyed + enabled.
void HL2Stream::setAttOnTxDb(int db) {
    const int clamped = std::clamp(db, 0, 31);
    if (clamped == attOnTxDb_) return;
    attOnTxDb_ = clamped;
    QSettings().setValue(QStringLiteral("tx/attOnTxDb"), clamped);
    emit attOnTxDbChanged(clamped);
    if (attOnTxEnabled_ && mox_.load(std::memory_order_relaxed))
        setTxStepAttnDb(clamped);
}

// #169 — SWR-protection operator surface.  Persist + emit; the enable
// setter also arms / stands down the live evaluator if keyed right now.
void HL2Stream::setSwrProtectEnabled(bool on) {
    if (on == swrProtectEnabled_) return;
    swrProtectEnabled_ = on;
    QSettings().setValue(QStringLiteral("tx/swrProtectEnabled"), on);
    emit swrProtectEnabledChanged(on);
    if (mox_.load(std::memory_order_relaxed)) {
        if (on) {                       // armed mid-TX: start a fresh window
            swrTicks_ = 0;
            swrOverTicks_ = 0;
            if (swrEvalTimer_) swrEvalTimer_->start(kSwrEvalIntervalMs);
        } else if (swrEvalTimer_) {     // disabled mid-TX: stand down
            swrEvalTimer_->stop();
        }
    }
    safetyLog(QStringLiteral("TX SWR protect -> %1")
              .arg(on ? QStringLiteral("ON (%1:1)")
                            .arg(swrProtectLimit_, 0, 'f', 1)
                      : QStringLiteral("off (no auto-cut on reflected power)")));
}

void HL2Stream::setSwrProtectLimit(double ratio) {
    const double clamped =
        std::clamp(ratio, kSwrProtectMinLimit, kSwrProtectMaxLimit);
    if (clamped == swrProtectLimit_) return;
    swrProtectLimit_ = clamped;
    QSettings().setValue(QStringLiteral("tx/swrProtectLimit"), clamped);
    emit swrProtectLimitChanged(clamped);
}

void HL2Stream::setSwrProtectDuringTune(bool on) {
    if (on == swrProtectDuringTune_) return;
    swrProtectDuringTune_ = on;
    QSettings().setValue(QStringLiteral("tx/swrProtectDuringTune"), on);
    emit swrProtectDuringTuneChanged(on);
}

void HL2Stream::setSwrProtectAction(int action) {
    const int clamped = std::clamp(action, 0, 1);   // 0 = Cut, 1 = Fold
    if (clamped == swrProtectAction_) return;
    swrProtectAction_ = clamped;
    QSettings().setValue(QStringLiteral("tx/swrProtectAction"), clamped);
    emit swrProtectActionChanged(clamped);
    safetyLog(QStringLiteral("TX SWR protect action -> %1")
              .arg(clamped == 1 ? QStringLiteral("Fold") : QStringLiteral("Cut")));
}

void HL2Stream::setFoldMinDrivePct(int pct) {
    const int clamped = std::clamp(pct, 1, 90);
    if (clamped == foldMinDrivePct_) return;
    foldMinDrivePct_ = clamped;
    QSettings().setValue(QStringLiteral("tx/foldMinDrivePct"), clamped);
    emit foldMinDrivePctChanged(clamped);
}

void HL2Stream::setMaxDrivePct(int pct) {
    const int clamped = std::clamp(pct, 1, 100);   // 100 = no cap
    if (clamped == maxDrivePct_) return;
    maxDrivePct_ = clamped;
    QSettings().setValue(QStringLiteral("tx/maxDrivePct"), clamped);
    emit maxDrivePctChanged(clamped);
    // Re-clamp the live drive DOWN to the new ceiling if it now exceeds
    // it (never raises a drive the operator didn't ask for).  Routes
    // through setTxDriveLevel so the wire + readout + persistence all
    // track.  Raising the cap leaves the current drive where it is — the
    // operator re-drives up to the new headroom themselves.
    const int cap = maxDriveRaw();
    if (txDriveLevel_.load(std::memory_order_relaxed) > cap) setTxDriveLevel(cap);
    safetyLog(QStringLiteral("TX max drive cap -> %1 %%").arg(clamped));
}

// =========================================================================
// #105 CW-1a — CW keyer config setters + the prn->cw push.
//
// Each setter clamps, stores the member, persists (tx/cw/*), emits, and
// pushes the whole CW block to prn->cw.  applyCwConfigToPrn() does NOT
// touch prn->cw.cw_enable — that bit is owned by the keying FSM commit
// (CW-2/CW-3); until it is set the gateware ignores all of this, so these
// setters are wire-INERT (composer cases 12/13/14 carry safe config but
// the FPGA only acts on it in CW).
// =========================================================================
void HL2Stream::applyCwConfigToPrn() {
    auto* p = lyra::wire::prn;
    if (!p) return;                         // radio not created yet
    p->cw.keyer_speed    = cwKeyerSpeedWpm_;
    p->cw.keyer_weight   = cwKeyerWeight_;
    p->cw.iambic         = cwIambic_        ? 1 : 0;
    p->cw.mode_b         = cwModeB_         ? 1 : 0;
    p->cw.rev_paddle     = cwRevPaddle_     ? 1 : 0;
    p->cw.strict_spacing = cwStrictSpacing_ ? 1 : 0;
    p->cw.break_in       = (cwBreakInMode_ != 2) ? 1 : 0;  // QSK+Semi on; Manual off
    p->cw.hang_delay     = cwHangDelayMs_;
    p->cw.sidetone       = cwSidetoneOn_    ? 1 : 0;
    p->cw.sidetone_level = cwSidetoneLevel_;
    p->cw.sidetone_freq  = cwPitchHz_.load(std::memory_order_relaxed);  // one pitch (== RX/CW pitch)
    // cw_enable intentionally untouched here — owned by applyCwKeyerEnable()
    // (the Thetis EnableCWKeyer analog), driven by CW-mode entry/exit.
}

// #105 CW-2 — Thetis EnableCWKeyer analog.
//
// Reference: console.cs CWFWKeyer (default true) -> netInterface.c:1044
// EnableCWKeyer(enable) { prn->cw.cw_enable = enable; SetSidetoneRun(...); }.
// cw_enable is the FIRMWARE-keyer + sidetone master: with it set, the HL2's
// internal iambic keyer (hl2_rtl_radio.v TX state machine) reads the physical
// paddle/key on ext_keydown, shapes + keys the carrier, runs the HW sidetone,
// and handles semi break-in (cw_hang_time / CWHANG state) — all autonomously,
// with NO host MOX.  The host's only job for paddle CW is to arm this bit.
//
// Lyra ties the arm to CW mode (CWL=3 / CWU=4) rather than a persistent Setup
// checkbox: outside CW the bit is cleared so a stray paddle press can't key a
// CW carrier mid-SSB.  Same wire effect as Thetis's CWFWKeyer; a CWFWKeyer
// Settings toggle + the software-keyer path land together in CW-3.
//
// Wire: composer case 13 (addr 0x0f) C1 carries prn->cw.cw_enable on the next
// C&C round-robin cycle (Thetis pushes immediately via CmdTx(); Lyra's
// round-robin re-emits every full cycle, ~ms — fine for a mode-change arm,
// which is not keydown-timing-critical).
void HL2Stream::applyCwKeyerEnable() {
    auto* p = lyra::wire::prn;
    if (!p) return;                         // radio not created yet
    const int tm = txMode_.load(std::memory_order_relaxed);
    const bool cw = (tm == 3 || tm == 4);   // WDSP CWL / CWU
    p->cw.cw_enable = (cwFwKeyer_ && cw) ? 1 : 0;
}

// =========================================================================
// #105 CW-3a — host software keyer (CWX): wire-bit setters + send/abort.
//
// The keyer thread (CwKeyer) drives tx[0].cwx (per Morse element) and
// tx[0].cwx_ptt (held for the message) via the injected callbacks below.
// `cw_enable` (armed in CW mode by applyCwKeyerEnable) gates BOTH the host
// EP2 overlay (NetworkProto1.cpp packs these bits) AND the gateware CWX
// decode (cmd_data[24] = the 0x0f C1 bit = cw_enable, hl2_rtl_dsopenhpsdr1
// .v:394) — so in CW mode the bits flow and the gateware keys the carrier
// (cwx -> cwx_keydown, CWTX) + holds TX between elements via cwx_ptt + its
// 500-unit spacing hang, and makes the HW sidetone.  Host carrier/WDSP NOT
// involved.  QSK (default break-in): host stays RX, like the paddle.
// =========================================================================
void HL2Stream::ensureCwKeyer() {
    if (cwKeyer_) return;
    cwKeyer_ = std::make_unique<lyra::tx::CwKeyer>(
        [this](bool down) { setCwxKey(down); },
        [this](bool on)   { setCwxPtt(on);   },
        [this](bool tx) {
            // Break-in policy hook (message-level: true before the first
            // element, false after the cwx_ptt drop).  QSK (default): the
            // gateware keys the carrier autonomously from cwx/cwx_ptt while
            // the host stays RX on the wire — no host MOX, exactly like the
            // paddle (CW-2).  We deliberately do NOT assert wire MOX here.
            // What we DO is reflect the keyed state to the UI via
            // cwKeyingActive so the meter flips to TX and shows forward
            // power (operator bench: the meter stayed on the RX S-meter
            // during CW while the external watt-meter showed output).  The
            // keyer calls us from its own pump thread; marshal onto this
            // QObject's thread so the state change + signal match the
            // moxActive_ threading contract.  Semi / Manual host-MOX
            // integration (actual wire keying) is still a CW-3 follow-on.
            QMetaObject::invokeMethod(this, [this, tx]{ setCwKeyingActive(tx); },
                                      Qt::QueuedConnection);
        });
}

void HL2Stream::setCwxKey(bool down) {
    auto* p = lyra::wire::prn;
    if (!p) return;                         // radio gone / not created
    p->tx[0].cwx = down ? 1 : 0;
}

void HL2Stream::setCwxPtt(bool on) {
    auto* p = lyra::wire::prn;
    if (!p) return;
    p->tx[0].cwx_ptt = on ? 1 : 0;
}

// #105 — UI-facing CW keyed state (drives the meter RX↔TX flip).  Dedups
// and emits on this QObject's thread; never touches the wire MOX bit (QSK
// keeps the host RX on the wire — the gateware keys the PA itself).
void HL2Stream::setCwKeyingActive(bool on) {
    if (cwKeyingActive_ == on) return;
    cwKeyingActive_ = on;
    emit cwKeyingActiveChanged(on);
}

// #105 — recompute the UI display TX-state = wire MOX OR CW keyed.  Drives
// the red-on-air QML binding (Stream.txDisplayActive) and the CW panadapter
// rescale/analyzer swap wired in main.cpp.  Never touches the wire MOX bit.
void HL2Stream::updateTxDisplayActive() {
    const bool v = moxActive_ || cwKeyingActive_;
    if (v == txDisplayActive_) return;
    txDisplayActive_ = v;
    emit txDisplayActiveChanged(v);
}

void HL2Stream::sendCw(const QString& text) {
    const int tm = txMode_.load(std::memory_order_relaxed);
    if (!(tm == 3 || tm == 4)) return;      // CW mode only (cw_enable gate)
    if (text.isEmpty()) return;
    ensureCwKeyer();
    cwKeyer_->send(text.toStdString(), cwKeyerSpeedWpm_, cwKeyerWeight_);
}

void HL2Stream::abortCw() {
    if (cwKeyer_) cwKeyer_->abort();
}

void HL2Stream::setCwKeyerSpeedWpm(int wpm) {
    const int c = std::clamp(wpm, 1, 60);
    if (c == cwKeyerSpeedWpm_) return;
    cwKeyerSpeedWpm_ = c;
    QSettings().setValue(QStringLiteral("tx/cw/keyerSpeedWpm"), c);
    emit cwKeyerSpeedWpmChanged(c);
    applyCwConfigToPrn();
}

void HL2Stream::setCwKeyerWeight(int weight) {
    const int c = std::clamp(weight, 33, 66);
    if (c == cwKeyerWeight_) return;
    cwKeyerWeight_ = c;
    QSettings().setValue(QStringLiteral("tx/cw/keyerWeight"), c);
    emit cwKeyerWeightChanged(c);
    applyCwConfigToPrn();
}

void HL2Stream::setCwIambic(bool on) {
    if (on == cwIambic_) return;
    cwIambic_ = on;
    QSettings().setValue(QStringLiteral("tx/cw/iambic"), on);
    emit cwIambicChanged(on);
    applyCwConfigToPrn();
}

void HL2Stream::setCwModeB(bool on) {
    if (on == cwModeB_) return;
    cwModeB_ = on;
    QSettings().setValue(QStringLiteral("tx/cw/modeB"), on);
    emit cwModeBChanged(on);
    applyCwConfigToPrn();
}

void HL2Stream::setCwRevPaddle(bool on) {
    if (on == cwRevPaddle_) return;
    cwRevPaddle_ = on;
    QSettings().setValue(QStringLiteral("tx/cw/revPaddle"), on);
    emit cwRevPaddleChanged(on);
    applyCwConfigToPrn();
}

void HL2Stream::setCwStrictSpacing(bool on) {
    if (on == cwStrictSpacing_) return;
    cwStrictSpacing_ = on;
    QSettings().setValue(QStringLiteral("tx/cw/strictSpacing"), on);
    emit cwStrictSpacingChanged(on);
    applyCwConfigToPrn();
}

void HL2Stream::setCwBreakInMode(int mode) {
    const int m = std::clamp(mode, 0, 2);   // 0=QSK 1=Semi 2=Manual
    if (m == cwBreakInMode_) return;
    cwBreakInMode_ = m;
    QSettings().setValue(QStringLiteral("tx/cw/breakInMode"), m);
    emit cwBreakInModeChanged(m);
    applyCwConfigToPrn();   // re-derives prn->cw.break_in; the host-MOX gate
                            // (onHwPttPoll) reads cwBreakInMode_ live.
}

void HL2Stream::setCwHangDelayMs(int ms) {
    const int c = std::clamp(ms, 0, 1000);
    if (c == cwHangDelayMs_) return;
    cwHangDelayMs_ = c;
    QSettings().setValue(QStringLiteral("tx/cw/hangDelayMs"), c);
    emit cwHangDelayMsChanged(c);
    applyCwConfigToPrn();
}

void HL2Stream::setCwSidetoneOn(bool on) {
    if (on == cwSidetoneOn_) return;
    cwSidetoneOn_ = on;
    QSettings().setValue(QStringLiteral("tx/cw/sidetoneOn"), on);
    emit cwSidetoneOnChanged(on);
    applyCwConfigToPrn();
}

void HL2Stream::setCwSidetoneLevel(int level) {
    const int c = std::clamp(level, 0, 127);
    if (c == cwSidetoneLevel_) return;
    cwSidetoneLevel_ = c;
    QSettings().setValue(QStringLiteral("tx/cw/sidetoneLevel"), c);
    emit cwSidetoneLevelChanged(c);
    applyCwConfigToPrn();
}

// §15.27 Commit B — Leveler max-gain ceiling (LINEAR factor,
// matches the verified reference's Setup spinner-to-SetTXALevelerTop
// plumbing exactly).  Clamped to [0, 20] LINEAR to match the
// reference's spinner range (narrower than ALC's 0..120).  Forwards
// the CURRENT enable state alongside the new ceiling so the WDSP
// pair stays coherent.
void HL2Stream::setLevelerMaxGainLinear(double linear) {
    const double clamped = std::clamp(linear,
                                      kMinLevelerMaxGainLinear,
                                      kMaxLevelerMaxGainLinear);
    if (clamped == levelerMaxGainLinear_) return;
    levelerMaxGainLinear_ = clamped;
    QSettings().setValue(QStringLiteral("tx/levelerMaxGainLinear"), clamped);
    emit levelerMaxGainLinearChanged(clamped);
    std::function<void(bool,double)> fwd;
    bool on;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setLevelerOn;
        on  = levelerOn_;
    }
    if (fwd) fwd(on, clamped);
}

// §15.27 Commit B — Leveler decay (exponential-curve tau in ms).
void HL2Stream::setLevelerDecayMs(int decay_ms) {
    const int clamped = std::clamp(decay_ms,
                                   kMinLevelerDecayMs,
                                   kMaxLevelerDecayMs);
    if (clamped == levelerDecayMs_) return;
    levelerDecayMs_ = clamped;
    QSettings().setValue(QStringLiteral("tx/levelerDecayMs"), clamped);
    emit levelerDecayMsChanged(clamped);
    std::function<void(int)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setLevelerDecayMs;
    }
    if (fwd) fwd(clamped);
}

// #93/#106 — AM/SAM carrier level, operator-facing as a percent of the
// STANDARD AM carrier (0..100 %, default 100 % = standard full-carrier AM).
// The operator percent is a POWER ratio: it maps to the WDSP carrier
// coefficient (SetTXAAMCarrierLevel, 0..1) as  c = sqrt(pct/100) * 0.5  so
// the displayed number tracks carrier power linearly and "100 %" is the
// textbook AM carrier (coefficient 0.5 = carrier at half the peak envelope
// amplitude = 25 % of PEP).  Higher = more carrier power (less relative
// sideband); 0 → suppressed-carrier (DSB-like).  This mirrors the reference
// (Setup → AM carrier %) exactly so operator-stored values transfer 1:1.
// WDSP ignores the coefficient outside AM/SAM, so it can be pushed any time.
static inline double amPctToCarrierLevel(double pct) {
    return std::sqrt(std::clamp(pct, 0.0, 100.0) * 0.01) * 0.5;
}

void HL2Stream::setAmCarrierPct(double pct) {
    const double clamped = std::clamp(pct, 0.0, 100.0);
    if (clamped == amCarrierPct_) return;
    amCarrierPct_ = clamped;
    QSettings().setValue(QStringLiteral("tx/amCarrierPct"), clamped);
    emit amCarrierPctChanged(clamped);
    std::function<void(double)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setAmCarrierLevel;
    }
    if (fwd) fwd(amPctToCarrierLevel(clamped));   // % power → WDSP 0..1 c_level
}

// TX-1 component 8a-tx-mode — relay the operator's WDSP TXA mode to the
// TX channel via the registered TxControl.setMode callback.  No
// persistence here (RX mode in WdspEngine is the operator-driven source
// of truth + already QSettings-persisted; TX mode is the slave that
// tracks it).  No no-op guard on (mode == cached) — the TxChannel side
// (post-fix to tx_channel.cpp::setMode) deliberately propagates every
// call to WDSP, so this layer doesn't need to second-guess either.  The
// wdspMode argument is the raw WDSP TXA mode integer (0=LSB, 1=USB);
// clamped to [0, 1] here as a defence against translator bugs.
void HL2Stream::setTxMode(int wdspMode) {
    // Full WDSP TXA modulation-mode range (0=LSB..10=SAM, up to 13).  Was
    // clamped to [0,1] (SSB-only) — widened so AM(6)/DSB(2)/FM(5)/SAM(10)
    // reach SetTXAMode and modulate natively.  Mirror it so txDdsHzForTune
    // signs the TUN DDS offset by sideband (USB −cw_pitch / LSB +cw_pitch /
    // double-sideband centered).
    const int clamped = std::clamp(wdspMode, 0, 13);
    txMode_.store(clamped, std::memory_order_relaxed);
    // #105 CW-2 — arm/disarm the firmware CW keyer on the mode edge (Thetis
    // EnableCWKeyer): cw_enable on in CWL/CWU, off otherwise.  Must follow
    // the txMode_ store above (applyCwKeyerEnable reads it).
    applyCwKeyerEnable();
    // #109 — re-derive the PHROT run state on the mode edge: phase rotation
    // auto-disables in DIGU/DIGL (digital), re-enables in voice modes if the
    // operator toggle is on.  Must follow the txMode_ store above.
    applyPhrotRun();
    // #107 — re-derive the CTCSS run state on the mode edge: the FM sub-tone
    // runs only in FM (WDSP mode 5) and only when the operator enabled it.
    applyCtcssRun();
    // #170c — re-derive the TX drive byte on the mode edge: the CW watts-cap
    // fold in applyTxPower_ folds the (gateware-bypassed) fixed gain into the
    // coarse byte only in CW, so entering/leaving CW must re-push the byte.
    // Wire-guarded + TX-inert at RX (gateware acts on drive only while keyed).
    applyTxPower_(txDriveLevel_.load(std::memory_order_relaxed));
    std::function<void(int)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setMode;
    }
    // TX-1 component 8a-tx-mode — diagnostic log so we can verify the
    // mode-push chain is firing end-to-end on bench.  Operator's
    // 2026-05-31 follow-up bench showed "no change" after the initial
    // 8a-tx-mode commit; one possibility is the path was never reached
    // (stale build, callback unwired).  This log answers "did
    // setTxMode get called and was the callback registered" in the
    // bench [tx] log lines so we don't have to guess next time.
    qInfo("[tx] setTxMode(wdsp=%d) -> %s",
          clamped,
          fwd ? "forwarded to TxControl.setMode"
              : "NO-OP (TxControl.setMode not registered)");
    if (fwd) fwd(clamped);
    // Re-push the TX NCO on every mode change: the CW carrier offset
    // (cwTxCarrierOffsetHz) flips sign between CWU/CWL, and the TUN DDS offset
    // (when tuning) flips too — keep the keyed/tune carrier on the marker.
    // Benign during RX (TX NCO is consumed only while transmitting).
    if (lyra::wire::prn != nullptr) {
        const int dds = static_cast<int>(txFreqHz_.load(std::memory_order_relaxed));
        const int nco = txDdsHzForTune(dds);
        lyra::wire::set_tx_freq(nco);
        // Display-honesty: re-tell the panadapter so the TX crop stays on
        // the marker when the offset sign flips with the sideband.
        emit txAnalyzerOffsetChanged(txAnalyzerOffsetHz());
        // #105 CW carrier diagnostic — ground truth for the carrier-on-marker
        // bench.  In CW the TX NCO should equal the carrier = DDS + offset
        // (== the marker, which WdspEngine draws at DDS + cwMarkerOffset).
        qInfo("[tx] CW-carrier: mode=%d(CWL=3/CWU=4) dds=%d cwPitch=%d "
              "offset=%+d -> TX_NCO=%d (should == marker = dds+offset)",
              clamped, dds, cwPitchHz_.load(std::memory_order_relaxed),
              cwTxCarrierOffsetHz(), nco);
    }
}

// TX-1 component 8c + Task #53 — operator TX bandpass.  Forwards
// (low, high) Hz to the registered TxControl.setBandpass callback.
// TxChannel internally sign-codes per the current WDSP mode (USB
// pass-through, LSB negate-and-swap), so we always pass positive
// edges.  Both edges are pulled from Prefs at the call site:
//   low  = Prefs.filterLow   (shared with the RX bandpass; Task #53)
//   high = Prefs.txBandwidth (per-mode TX BW combo; component 8c)
// No-op if high<=0 or low<0 or low>=high (defence — Prefs clamps
// both sides into sane ranges but the safety check is cheap).
void HL2Stream::setTxBandpass(int lowHz, int highHz) {
    if (highHz <= 0 || lowHz < 0 || lowHz >= highHz) return;
    const double low  = static_cast<double>(lowHz);
    const double high = static_cast<double>(highHz);
    std::function<void(double, double)> fwd;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        fwd = txControl_.setBandpass;
    }
    qInfo("[tx] setTxBandpass(low=%d, high=%d) -> %s",
          lowHz, highHz,
          fwd ? "forwarded to TxControl.setBandpass"
              : "NO-OP (TxControl.setBandpass not registered)");
    if (fwd) fwd(low, high);
}

// ─────────────────────────────────────────────────────────────
// TX-1 component 6: SSB I/Q injection gate + source registration
// ─────────────────────────────────────────────────────────────
//
// setInjectTxIq is the FSM hand-off point.  Wire-inert default
// (false) means the EP2 packer's SSB branch is never taken, so
// no SSB I/Q can land on the wire even with MOX keyed.  The FSM
// (follow-up commit) flips this TRUE at keydown AFTER the WDSP
// TXA channel is started + rf_delay has elapsed, and FALSE at
// keyup BEFORE the wire MOX bit clears (per §5.7 keyup ordering
// invariant).
//
// NOT persisted to QSettings — operator intent is captured in
// the FSM gestures (PTT, MOX, TUN buttons), not in a "default
// SSB armed" preference.  Stream stop / restart always comes
// up with injectTxIq_=false (matches the cb58bcb-class
// come-up-not-keyed safety posture for MOX + tune).

void HL2Stream::setInjectTxIq(bool on) {
    const bool prev = injectTxIq_.exchange(on, std::memory_order_acq_rel);
    if (prev == on) return;
    safetyLog(QStringLiteral(
        "TX: inject_tx_iq -> %1 (EP2 packer %2 SSB samples on MOX)")
        .arg(on ? QStringLiteral("ARMED") : QStringLiteral("disarmed"))
        .arg(on ? QStringLiteral("pulling") : QStringLiteral("ignoring")));
    // TX-1 component 7 — forward to the registered TxControl callback
    // so the TxDspWorker producer-side flag flips in lockstep with the
    // EP2-packer consumer-side flag.  Single point of control from
    // the FSM.  Brief lock; uncontested (registration at app boot only).
    {
        std::function<void(bool)> fwd;
        {
            std::lock_guard<std::mutex> lk(txControlMtx_);
            fwd = txControl_.setInjectTxIq;
        }
        if (fwd) fwd(on);
    }
    emit injectTxIqChanged(on);
}

// TX-1 component 7 — register the TX channel lifecycle callbacks.
// Empty TxControl = clear all five (start/stop/setInjectTxIq +
// component-8a setMicGainDb/setAlcMaxGainLinear).  See TxControl
// struct doc in the header for the contract.  Same mutex pattern as
// registerTxIqSource.
//
// TX-1 component 8a — registration ALSO pushes the autoloaded
// micGainDb_ + alcMaxGainLinear_ to the channel ONCE so the freshly-
// opened WDSP TXA channel doesn't sit at WDSP create-time defaults
// (especially the load-bearing ALC max-gain = 1.0 LINEAR trap that
// pins the TXA output chain at a 0 dB ALC ceiling).  This is the
// architectural equivalent of the verified reference's Setup first-
// load pushing the operator's persisted profile values onto the
// channel.  Capture the callbacks INSIDE the lock, release the
// lock, then call OUTSIDE the lock — TxChannel's own channelMtx_
// will serialise the WDSP setter call against any in-flight
// process() on the worker thread.
void HL2Stream::registerTxControl(TxControl ctl) {
    std::function<void(double)>       pushMic;
    std::function<void(double)>       pushAlc;
    std::function<void(int)>          pushAlcDecay;
    std::function<void(bool,double)>  pushLeveler;
    std::function<void(int)>          pushLevelerDecay;
    std::function<void(double)>       pushAmCarrier;
    std::function<void(bool)>         pushPhrot;
    std::function<void(double)>       pushFmDev;
    std::function<void(double)>       pushCtcssFreq;
    std::function<void(bool)>         pushCtcssRun;
    bool   levOn;
    double levTop;
    double amCarLin;
    bool   phrotOn;
    double fmDev;
    double ctcssFreq;
    bool   ctcssOn;
    {
        std::lock_guard<std::mutex> lk(txControlMtx_);
        txControl_ = std::move(ctl);
        // Only push if the callback landed — a clear ({}) call
        // gives us null function objects, in which case we just
        // dropped the registration and there's nothing to push.
        pushMic          = txControl_.setMicGainDb;
        pushAlc          = txControl_.setAlcMaxGainLinear;
        pushAlcDecay     = txControl_.setAlcDecayMs;
        pushLeveler      = txControl_.setLevelerOn;
        pushLevelerDecay = txControl_.setLevelerDecayMs;
        pushAmCarrier    = txControl_.setAmCarrierLevel;
        pushPhrot        = txControl_.setPhrotRun;
        pushFmDev        = txControl_.setFmDeviation;
        pushCtcssFreq    = txControl_.setCtcssFreq;
        pushCtcssRun     = txControl_.setCtcssRun;
        levOn            = levelerOn_;
        levTop           = levelerMaxGainLinear_;
        amCarLin         = amPctToCarrierLevel(amCarrierPct_);   // % power → c_level
        // #109 — push the EFFECTIVE run (operator intent gated by mode):
        // auto-OFF in DIGU/DIGL even when the toggle is on.
        const int m      = txMode_.load(std::memory_order_relaxed);
        phrotOn          = phrotEnabled_ && !(m == 7 || m == 9);
        // #107 — FM deviation/tone push once; CTCSS run is FM-gated (mode 5).
        fmDev            = fmDeviationHz_;
        ctcssFreq        = ctcssToneHz_;
        ctcssOn          = ctcssEnabled_ && (m == 5);
    }
    if (pushMic)          pushMic(micGainDb_);
    if (pushAlc)          pushAlc(alcMaxGainLinear_);
    if (pushAlcDecay)     pushAlcDecay(alcDecayMs_);
    if (pushLeveler)      pushLeveler(levOn, levTop);
    if (pushLevelerDecay) pushLevelerDecay(levelerDecayMs_);
    if (pushAmCarrier)    pushAmCarrier(amCarLin);
    if (pushPhrot)        pushPhrot(phrotOn);
    if (pushFmDev)        pushFmDev(fmDev);
    if (pushCtcssFreq)    pushCtcssFreq(ctcssFreq);
    if (pushCtcssRun)     pushCtcssRun(ctcssOn);
}

// Source registration — caller-owned lifetime.  The wire writer
// thread calls the registered std::function once per datagram when
// (moxBit && injectTxIq_); the callback fills 126 complex<float>
// samples and returns true, or returns false if no data was ready.
//
// Passing an empty std::function ({}) clears the source.  Caller
// MUST clear before the underlying object goes away.  The mutex
// here is for the register/clear race only — the hot-path read
// inside the EP2 packer takes the same mutex briefly per datagram,
// which is fine at ~380 Hz (uncontested unless mid-registration).
void HL2Stream::registerTxIqSource(TxIqSource src) {
    std::lock_guard<std::mutex> lk(txIqSourceMtx_);
    txIqSource_ = std::move(src);
}

void HL2Stream::armTxSafetyTimer() {
    if (txTimeoutBypass_ || !txSafetyTimer_) return;
    // QTimer::start(int) with a fresh duration replaces any pending
    // expiry — operator intent on bypass-off-then-on or duration change
    // is "give me a fresh full window from this moment."
    txSafetyTimer_->start(txTimeoutSec_ * 1000);
    safetyLog(QStringLiteral("TX safety: armed for %1 s").arg(txTimeoutSec_));
}

void HL2Stream::cancelTxSafetyTimer() {
    if (!txSafetyTimer_) return;
    if (txSafetyTimer_->isActive()) {
        txSafetyTimer_->stop();
        safetyLog(QStringLiteral("TX safety: cancelled (keyup or bypass)"));
    }
}

void HL2Stream::onTxSafetyTimeout() {
    // Drive the auto-release through the normal funnel so the TR-delay
    // keyup chain runs cleanly + ATT-on-TX restores + UI red-on-air
    // clears via the standard moxActiveChanged(false) edge.
    safetyLog(QStringLiteral(
        "TX safety: timeout reached after %1 s — auto-releasing")
        .arg(txTimeoutSec_));
    emit txTimeoutFired();
    requestMox(false);
}

// #169 — SWR-protection evaluator.  arm/disarm bracket the keyed window;
// eval runs every kSwrEvalIntervalMs while keyed and implements the four
// false-trigger guards before cutting.
void HL2Stream::armSwrProtect() {
    // Manual re-arm: every fresh key-down clears the prior trip latch so
    // the operator re-keying is the explicit "I've fixed the antenna,
    // try again" gesture (no auto-recovery while still keyed).
    if (swrProtectTripped_) {
        swrProtectTripped_ = false;
        emit swrProtectTrippedChanged(false);
    }
    if (!swrProtectReason_.isEmpty()) {
        swrProtectReason_.clear();
        emit swrProtectReasonChanged(swrProtectReason_);
    }
    // Fold restore (manual re-arm / never auto-recover): if a prior
    // transmission folded the drive down, hand the operator's stored
    // drive set point back on this fresh key-down.
    if (swrFolded_) {
        swrFolded_ = false;
        applyDriveLevelNoPersist(swrFoldPreDrive_);
    }
    swrTicks_ = 0;
    swrOverTicks_ = 0;
    wattsOverTicks_ = 0;
    // Always run the evaluator while transmitting: it drives the SWR check
    // (gated on swrProtectEnabled_), the watts cap fold + servo (gated on
    // capActive_ — value set AND armed, 2026-07-03), AND the per-band
    // raw-watts capture latch that "Calibrate this band" reads (needed on
    // every TX, cap on or off).  The tick is cheap and every action inside is
    // individually gated, so running it unconditionally is safe.
    if (!swrEvalTimer_) return;
    swrEvalTimer_->start(kSwrEvalIntervalMs);
}

void HL2Stream::disarmSwrProtect() {
    if (swrEvalTimer_) swrEvalTimer_->stop();
    // NOTE: the trip latch (swrProtectTripped_/reason) is deliberately
    // NOT cleared here — it must survive the keyup so the operator sees
    // WHY TX cut, until the next key-down re-arms (armSwrProtect()).
}

void HL2Stream::evalSwrProtect() {
    // Guard A — key-down blanking: skip the T/R-settle + ALC ramp window
    // where fwd/rev are still slewing and the reading is meaningless.
    // Applies to both the watts cap and SWR checks.
    ++swrTicks_;
    if (swrTicks_ * kSwrEvalIntervalMs < swrBlankMs_) return;
    const double fwd    = fwdPowerW();      // RAW — SWR ratio (below) + guards
    const double fwdCal = fwdPowerCalW();   // CALIBRATED — the watts cap

    // Per-band raw-watts capture latch (2026-07-03) — "capture while you
    // tune".  While a steady carrier is up, EWMA the RAW (un-trimmed) formula
    // watts for the live TX band + remember the drive level it was taken at,
    // so "Calibrate this band" can compute the trim (and, at full drive, set
    // Full Output) off a held sample without a second key-down.
    if (fwd >= swrFwdFloorW_) {
        const int cb = lyra::bandIndexForFreq(
            static_cast<int>(txFreqHz_.load(std::memory_order_relaxed)));
        if (cb >= 0 && cb < kNumPaGainBands) {
            const double prev = capturedRawW_[cb].load(std::memory_order_relaxed);
            capturedRawW_[cb].store(prev > 0.0 ? prev * 0.7 + fwd * 0.3 : fwd,
                                    std::memory_order_relaxed);
            capturedDrive_[cb].store(txDriveLevel_.load(std::memory_order_relaxed),
                                     std::memory_order_relaxed);
        }
    }

    // Stage 3b-2 — REACTIVE watts cap (GROSS backstop to the predictive
    // cap).  Runs on EVERY band, even during a deliberate tune (TUN is the
    // over-drive case), independent of the SWR-protect enable: fold the
    // drive when measured forward power sits well over the cap for the
    // dwell.  The PREDICTIVE cap already lands a MEASURED band right at the
    // cap, so the threshold must clear that landing (+ PWR-meter ballistic/
    // noise) or it false-trips on a normal at-cap carrier.  30 % margin: a
    // measured band at the cap never trips; an UNMEASURED band running full
    // (~2× the cap or more) still folds.  This is a coarse net for an
    // unmeasured band / a wildly-wrong Full Output, NOT a fine limiter.
    // Gated on the arm (2026-07-03): the reactive fold only runs when the cap
    // is actually engaged, so an un-armed cap value never folds the drive.
    constexpr double kWattsCapMargin = 1.30;
    const double capW = maxOutputW_.load(std::memory_order_relaxed);
    if (capActive_() && fwdCal >= swrFwdFloorW_ && fwdCal > capW * kWattsCapMargin) {
        if (++wattsOverTicks_ * kSwrEvalIntervalMs >= swrDwellMs_) {
            foldWattsProtect(fwdCal, capW);
            return;
        }
    } else {
        wattsOverTicks_ = 0;
    }

    // Stage B — in TUN with the cap on, AUTO-LEARN this band's drive ceiling
    // (walk it up to the cap from below + lock).  Runs only in tune (SSB uses
    // the locked ceiling statically); the reactive fold above stays dormant
    // because the servo keeps fwd at/under the cap, well below cap·1.30.
    if (capActive_() && tuneEnabled_.load(std::memory_order_relaxed))
        tickCapServo_(fwdCal);

    // #169 SWR protect — operator opt-out for a deliberate ATU tune carrier
    // (default protects during tune), and gated on the SWR-protect enable
    // (the evaluator may be running only for the watts cap above).
    if (!swrProtectEnabled_) return;
    if (tuneEnabled_.load(std::memory_order_relaxed) && !swrProtectDuringTune_)
        return;
    // Guard B — power floors: below them we're not transmitting enough
    // for the reflected-power ratio to be anything but noise.  NaN (no
    // telemetry yet) fails the >= comparisons → treated as below-floor.
    const double rev = revPowerW();
    if (!(fwd >= swrFwdFloorW_) || !(rev >= swrRevFloorW_)) {
        swrOverTicks_ = 0;            // reset the dwell on any quiet tick
        return;
    }
    // Calibration-free SWR from the reflection coefficient: rho =
    // sqrt(Prev/Pfwd), SWR = (1+rho)/(1-rho).  rev>=fwd (impossible
    // physically, but the uncalibrated formula could glitch) → clamp
    // rho just under 1 so SWR reads very high and trips.
    double rho = std::sqrt(rev / fwd);
    if (rho > 0.999) rho = 0.999;
    const double swr = (1.0 + rho) / (1.0 - rho);
    if (swr < swrProtectLimit_) {
        swrOverTicks_ = 0;
        return;
    }
    // Guard C — dwell: require the over-limit condition to persist so a
    // single noisy sample can't trip (the reference's consecutive-poll
    // debounce, expressed in ms).
    ++swrOverTicks_;
    if (swrOverTicks_ * kSwrEvalIntervalMs < swrDwellMs_) return;
    // Sustained over the limit — apply the operator-chosen action.
    if (swrProtectAction_ == 1) foldSwrProtect(swr);   // Fold
    else                        tripSwrProtect(swr);   // Cut
}

void HL2Stream::foldSwrProtect(double swr) {
    // Fold action — monotone x0.5 drive step-down toward the floor; stay
    // keyed and keep evaluating, giving each reduced level a fresh dwell
    // window.  If we're already at the floor and STILL over limit, escalate
    // to a hard Cut (a fold that can't bring SWR down isn't protecting).
    // No auto-recovery: the drive stays reduced until the next key-down
    // restores it (armSwrProtect()).
    swrOverTicks_ = 0;                       // fresh dwell before next step
    if (!swrFolded_) {                       // capture operator's set point once
        swrFolded_ = true;
        swrFoldPreDrive_ = txDriveLevel_.load(std::memory_order_relaxed);
    }
    const int floorLevel =
        std::clamp((255 * foldMinDrivePct_) / 100, 1, 255);
    const int cur = txDriveLevel_.load(std::memory_order_relaxed);
    if (cur <= floorLevel) {
        // At the fold floor and still over → escalate to Cut.
        safetyLog(QStringLiteral(
            "TX SWR protect: fold floor (%1 %) reached, still SWR %2:1 — "
            "escalating to cut")
            .arg(foldMinDrivePct_).arg(swr, 0, 'f', 1));
        tripSwrProtect(swr);                 // stops timer, cuts, sets reason
        return;
    }
    const int next = std::max(floorLevel, cur / 2);
    applyDriveLevelNoPersist(next);
    const int pct = static_cast<int>(std::lround(next * 100.0 / 255.0));
    // Compact latched reason (the triggering ratio) — kept short so the
    // TxPanel PROT lamp can't overflow its neighbour; the fold level is
    // already shown live on the TX Drive % readout, and the full detail
    // (ratio + limit + fold target) lives in the safetyLog line below.
    swrProtectReason_ = QStringLiteral("SWR %1:1").arg(swr, 0, 'f', 1);
    swrProtectTripped_ = true;               // lamp red = protection acted
    emit swrProtectReasonChanged(swrProtectReason_);
    emit swrProtectTrippedChanged(true);
    emit swrProtectCut(swrProtectReason_);
    safetyLog(QStringLiteral(
        "TX SWR protect: SWR %1:1 >= %2:1 — fold drive -> %3 %% (raw %4/255)")
        .arg(swr, 0, 'f', 1).arg(swrProtectLimit_, 0, 'f', 1)
        .arg(pct).arg(next));
}

void HL2Stream::foldWattsProtect(double fwdW, double capW) {
    // Stage 3b-2 — REACTIVE watts cap fold.  Same monotone ×0.5 step-down
    // as the SWR fold, sharing swrFolded_/swrFoldPreDrive_ so the operator's
    // set point is captured once and restored on the next key-down.  At the
    // fold floor and STILL over the cap → escalate to a hard Cut (a fold
    // that can't get under the cap isn't protecting the amp).  Reuses the
    // same PROT lamp; the reason text ("PWR x W") says it was the watts cap.
    wattsOverTicks_ = 0;
    if (!swrFolded_) {
        swrFolded_ = true;
        swrFoldPreDrive_ = txDriveLevel_.load(std::memory_order_relaxed);
    }
    const int floorLevel =
        std::clamp((255 * foldMinDrivePct_) / 100, 1, 255);
    const int cur = txDriveLevel_.load(std::memory_order_relaxed);
    if (cur <= floorLevel) {
        safetyLog(QStringLiteral(
            "TX watts cap: fold floor (%1 %) reached, still %2 W > %3 W — "
            "escalating to cut")
            .arg(foldMinDrivePct_).arg(fwdW, 0, 'f', 1).arg(capW, 0, 'f', 1));
        if (swrEvalTimer_) swrEvalTimer_->stop();
        swrProtectReason_ = QStringLiteral("PWR %1 W").arg(fwdW, 0, 'f', 1);
        swrProtectTripped_ = true;
        emit swrProtectReasonChanged(swrProtectReason_);
        emit swrProtectTrippedChanged(true);
        emit swrProtectCut(swrProtectReason_);
        requestMox(false);
        return;
    }
    const int next = std::max(floorLevel, cur / 2);
    applyDriveLevelNoPersist(next);
    const int pct = static_cast<int>(std::lround(next * 100.0 / 255.0));
    swrProtectReason_ = QStringLiteral("PWR %1 W").arg(fwdW, 0, 'f', 1);
    swrProtectTripped_ = true;
    emit swrProtectReasonChanged(swrProtectReason_);
    emit swrProtectTrippedChanged(true);
    emit swrProtectCut(swrProtectReason_);
    safetyLog(QStringLiteral(
        "TX watts cap: %1 W > %2 W — fold drive -> %3 %% (raw %4/255)")
        .arg(fwdW, 0, 'f', 1).arg(capW, 0, 'f', 1).arg(pct).arg(next));
}

void HL2Stream::tripSwrProtect(double swr) {
    // Guard D — latch + manual re-arm: stop the evaluator, latch the
    // lamp state, and CUT through the single unkey funnel so the standard
    // keyup TR-delay chain runs (ATT-on-TX restores, UI red-on-air clears
    // via moxActiveChanged(false)).  The latch clears only on the next
    // key-down (armSwrProtect()).
    swrOverTicks_ = 0;
    if (swrEvalTimer_) swrEvalTimer_->stop();
    swrProtectReason_ = QStringLiteral("SWR %1:1").arg(swr, 0, 'f', 1);
    swrProtectTripped_ = true;
    emit swrProtectReasonChanged(swrProtectReason_);
    emit swrProtectTrippedChanged(true);
    emit swrProtectCut(swrProtectReason_);
    safetyLog(QStringLiteral(
        "TX SWR protect: %1 >= %2:1 — auto-cut")
        .arg(swrProtectReason_)
        .arg(swrProtectLimit_, 0, 'f', 1));
    requestMox(false);
}

void HL2Stream::setSampleRate(int hz) {
    std::uint8_t bits;
    int micDec;
    switch (hz) {
    case 96000:  bits = 0x01; micDec = 2; break;
    case 192000: bits = 0x02; micDec = 4; break;
    case 384000: bits = 0x03; micDec = 8; break;
    default:     return;   // 48k excluded (EP2 cadence)
    }
    sampleRateBits_.store(bits, std::memory_order_relaxed);
    // §5 control-plane mapping (RX side): compose_case_0 C1 reads the
    // `SampleRateIn2Bits` global (NOT sampleRateBits_, the retired
    // composeCC source).  Plain global — no prn guard needed; valid at
    // all times.  The at-open seed covers the initial rate.
    lyra::wire::SampleRateIn2Bits = bits;
    // TX Component 3 — keep the mic decimation factor coherent with
    // the new wire rate so the 48 kHz mic output stays at 48 kHz across
    // rate switches (matches the C reference's `SetDDCRate`
    // factor table at netInterface.c:1328-1354).  The
    // `mic_decimation_count_` does NOT need to reset across normal rate
    // switches — same-direction phase walk-in stabilises within one
    // datagram; the RX-worker-thread-only counter is safe to leave alone.
    // Stage 2b2: route the mic decimation factor to the TU-scope
    // free variable that Ep6RecvThread's mic-tap reads (replaces the
    // retired HL2Stream::micDecimationFactor_ atomic).  Plain int per
    // reference; single-writer (setSampleRate on Qt main) vs single-
    // reader (Ep6RecvThread tap) is benign on the wire-rate codepath.
    lyra::wire::mic_decimation_factor = micDec;
    emit logLine(QStringLiteral("IQ sample rate -> %1 kHz (wire C1), "
                                "mic dec /%2")
                 .arg(hz / 1000).arg(micDec));
}

void HL2Stream::setFilterBoardEnabled(bool on) {
    if (on == filterBoardEnabled_) {
        return;
    }
    filterBoardEnabled_ = on;
    // #199 — keep OcControl's master gate in sync (this also re-arms its −1
    // change gate on the enable edge, so the next emit — including a
    // legitimate "all pins off" = 0 on disable — is never swallowed).
    oc_.setEnabled(on);
    QSettings().setValue(QStringLiteral("hw/filterBoard"), on);
    updateOcPattern();
    emit filterBoardChanged(on);
    emit logLine(QStringLiteral("Filter board %1")
                 .arg(on ? QStringLiteral("ENABLED") : QStringLiteral("off")));
}


void HL2Stream::updateOcPattern(bool transmitting) {
    // OC pattern for the current band when the board is enabled, else 0.
    // The FSM passes transmitting=true on keydown (in fsmAdvance, after
    // raising ATT-on-TX) so the TX-side OC bits hit the wire BEFORE the
    // MOX bit + rf_delay window.  This gives the filter board's relays
    // time to settle into TX configuration (per-band LPF in place) before
    // any RF energy starts being emitted — operator's hot-switch safety.
    // On keyup the FSM passes transmitting=false in fsmKeyupSettled AFTER
    // the wire MOX bit has cleared and ptt_out_delay elapsed, so the
    // board switches back to RX configuration only after RF is gone.
    // #199 Stage 2 — the pattern now comes from the editable OcControl table
    // (seeded with the N2ADR preset), NOT the hard-coded n2adrOcPattern.  The
    // enable gate + the call-site timing are unchanged, so a disabled board
    // still emits 0 and an N2ADR-seeded enabled board is byte-identical.
    quint8 bits = 0;
    if (filterBoardEnabled_) {
        const int bi = lyra::bandIndexForFreq(
            static_cast<int>(rx1FreqHz_.load(std::memory_order_relaxed)));
        // Non-split (RX2 not built): bandA == bandB == the RX1 band, and the
        // TX OC band == the RX1 band today.  When RX2/split lands the TX path
        // passes the TX-freq-derived index as bandA (§7.3); compute() already
        // takes bandB.  pa=false: no external-PA-override state is wired yet
        // and every xPA gate defaults off, so it is ignored (Stage 4 wires it).
        bits = oc_.compute(bi, bi, transmitting, /*tune*/ false,
                           /*twoTone*/ false, /*pa*/ false);
    }
    // Change gate (Thetis m_nOldBits, −1-seeded so the first emit after an
    // enable/open is never swallowed — §7.5.1).  Push only on a real change.
    if (!oc_.takeIfChanged(bits)) {
        return;
    }
    setOcOutput(bits);
}

void HL2Stream::setOcOutput(quint8 bits) {
    // §7.5.5 — OC pins occupy C2[7:1]; the frame-0 composer does the
    // `((oc_output << 1) & 0xFE) | cw.eer`.  Write the raw 7-bit pattern
    // home (compose_case_0 reads prn->oc_output); mirror into ocPattern_ for
    // the Q_PROPERTY ocBits readout + the retired ocC2_ atomic (left until
    // the §7 composeCC retirement cleanup).  `if (prn)` guards the pre-open
    // window (covered by the at-open seed).  Qt-main/control thread only
    // (§7.5.2); the EP2 writer thread reads the single settled byte.
    ocPattern_ = static_cast<int>(bits & 0x7F);
    if (lyra::wire::prn != nullptr) {
        lyra::wire::prn->oc_output = ocPattern_;
    }
    ocC2_.store(static_cast<std::uint8_t>((ocPattern_ << 1) & 0xFE),
                std::memory_order_relaxed);
    emit ocBitsChanged(ocPattern_);
}

void HL2Stream::seedOcN2adr() {
    // Seed OcControl's per-band RX/TX masks from the N2ADR filter-board
    // preset (bands.cpp n2adrOcPattern) so an enabled board reproduces the
    // shipped behaviour exactly.  The Stage-4 "N2ADR Filter" preset button
    // re-seeds through this same helper.
    const int n = static_cast<int>(lyra::amateurBands().size());
    for (int bi = 0; bi < n; ++bi) {
        oc_.setRxMaskA(bi,
            static_cast<quint8>(lyra::n2adrOcPattern(bi, /*tx*/ false)));
        oc_.setTxMaskA(bi,
            static_cast<quint8>(lyra::n2adrOcPattern(bi, /*tx*/ true)));
    }
}

void HL2Stream::applyOcEdit() {
    // #199 Stage 4 — the Settings "Filters / BCD" tab mutates oc_ via its
    // setters, then calls this to (a) persist the table to oc/* and (b)
    // re-emit the current-band pattern so an edit to the ACTIVE band takes
    // effect on the wire immediately (the change gate suppresses a no-op).
    saveOcSettings();
    updateOcPattern();
}

void HL2Stream::saveOcSettings() const {
    QSettings s;
    s.beginGroup(QStringLiteral("oc"));
    s.setValue(QStringLiteral("present"), true);
    const int n = oc_.nBands();
    for (int b = 0; b < n; ++b) {
        s.setValue(QStringLiteral("rxA/%1").arg(b), int(oc_.rxMaskA(b)));
        s.setValue(QStringLiteral("txA/%1").arg(b), int(oc_.txMaskA(b)));
        s.setValue(QStringLiteral("rxB/%1").arg(b), int(oc_.rxMaskB(b)));
        s.setValue(QStringLiteral("txB/%1").arg(b), int(oc_.txMaskB(b)));
    }
    for (int g = 0; g < lyra::oc::OcControl::kGroups; ++g) {
        for (int p = 0; p < lyra::oc::OcControl::kPins; ++p) {
            s.setValue(QStringLiteral("txPinAction/%1/%2").arg(g).arg(p),
                       int(oc_.txPinAction(g, p)));
            s.setValue(QStringLiteral("txPinPa/%1/%2").arg(g).arg(p),
                       oc_.txPinPa(g, p));
            s.setValue(QStringLiteral("rxPinPa/%1/%2").arg(g).arg(p),
                       oc_.rxPinPa(g, p));
        }
    }
    s.setValue(QStringLiteral("splitPins"), oc_.splitPins());
    s.setValue(QStringLiteral("rxABitMask"), oc_.rxABitMask());
    s.setValue(QStringLiteral("allowHotSwitch"), oc_.allowHotSwitch());
    s.endGroup();
}

void HL2Stream::loadOcSettings() {
    QSettings s;
    s.beginGroup(QStringLiteral("oc"));
    if (!s.value(QStringLiteral("present"), false).toBool()) {
        s.endGroup();
        return;   // never edited -> keep the N2ADR seed (byte-identical)
    }
    const int n = oc_.nBands();
    for (int b = 0; b < n; ++b) {
        oc_.setRxMaskA(b, quint8(s.value(QStringLiteral("rxA/%1").arg(b),
                                         int(oc_.rxMaskA(b))).toInt()));
        oc_.setTxMaskA(b, quint8(s.value(QStringLiteral("txA/%1").arg(b),
                                         int(oc_.txMaskA(b))).toInt()));
        oc_.setRxMaskB(b, quint8(s.value(QStringLiteral("rxB/%1").arg(b), 0).toInt()));
        oc_.setTxMaskB(b, quint8(s.value(QStringLiteral("txB/%1").arg(b), 0).toInt()));
    }
    for (int g = 0; g < lyra::oc::OcControl::kGroups; ++g) {
        for (int p = 0; p < lyra::oc::OcControl::kPins; ++p) {
            oc_.setTxPinAction(g, p + 1,
                static_cast<lyra::oc::TxPinAction>(
                    s.value(QStringLiteral("txPinAction/%1/%2").arg(g).arg(p),
                            int(lyra::oc::TxPinAction::MoxTuneTwoTone)).toInt()));
            oc_.setTxPinPa(g, p + 1,
                s.value(QStringLiteral("txPinPa/%1/%2").arg(g).arg(p), false).toBool());
            oc_.setRxPinPa(g, p + 1,
                s.value(QStringLiteral("rxPinPa/%1/%2").arg(g).arg(p), false).toBool());
        }
    }
    oc_.setSplitPins(s.value(QStringLiteral("splitPins"), false).toBool());
    oc_.setRxABitMask(s.value(QStringLiteral("rxABitMask"), 0x0f).toInt());
    oc_.setAllowHotSwitch(s.value(QStringLiteral("allowHotSwitch"), false).toBool());
    s.endGroup();
}


} // namespace lyra::ipc

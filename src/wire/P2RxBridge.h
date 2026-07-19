// Lyra — Protocol 2 RX bridge (Saturn / ANAN G2, Phase D).
//
// App-side owner of a P2Session: runs it on its OWN QThread and feeds
// the radio's DDC0 IQ frames into router 0 — the SAME sink the P1 EP6
// path dispatches to (main.cpp registers router_instance(0) port 0 →
// WdspEngine::feedIq at boot).  Everything downstream of that seam —
// WDSP filters / AGC / NR / demod, the panadapter analyzer, S-meter,
// and the audio output — runs UNCHANGED: this bridge is the P2
// equivalent of the EP6 receive thread, nothing more.
//
// This mirrors Thetis's own architecture: its P2 read loop hands DDC
// IQ to the identical xrouter(0, 0, …) call (network.c:608) that
// Lyra's router port is a verbatim port of.
//
// Division of labour with HL2Stream:
//   * HL2Stream REMAINS the tuning/state authority — every VFO
//     gesture, band button, memory recall and CAT command lands in
//     HL2Stream::setRx1FreqHz whether or not the P1 wire is up.  The
//     bridge subscribes to rx1FreqChanged and mirrors the dial onto
//     DDC0, so the entire tuning UI drives the Saturn for free.
//   * The two wire paths are mutually exclusive: open() refuses while
//     the P1 stream runs, and the Settings Open path closes this
//     bridge before opening an HL2.  That guarantee is what makes the
//     single-feeder-thread contract of WdspEngine::feedIq hold.
//
// Sample rate: the DDC follows the engine's IQ rate when it is a
// legal P2 rate (48/96/192/384 kHz — the engine's own choices all
// are); the panadapter span therefore tracks exactly as it does on
// the HL2.  A rate change while running re-sends the DDC config live.
//
// RX-only: P2Session never sets the transmit bit, sends drive 0 and
// PA-disabled (see P2Session.h bench-safety note) — this bridge
// cannot key the Saturn.  TX is later work.
//
// Threading: P2Session (QUdpSocket + timers) lives on thread_; every
// session mutation is marshalled onto that thread via
// QMetaObject::invokeMethod functors.  The IQ decode + xrouter
// dispatch run ON the session thread (direct connection), making it
// the single DSP feeder thread while a P2 radio is open.

#pragma once

#include <QObject>
#include <QString>
#include <QThread>
#include <limits>

namespace lyra::ipc { class HL2Stream; }
namespace lyra::dsp { class WdspEngine; }

namespace lyra::wire {

class P2Session;

class P2RxBridge : public QObject {
    Q_OBJECT

public:
    explicit P2RxBridge(lyra::ipc::HL2Stream *stream,
                        lyra::dsp::WdspEngine *engine,
                        QObject *parent = nullptr);
    ~P2RxBridge() override;

    bool    isRunning() const { return running_; }
    QString targetIp()  const { return ip_; }

    // Live telemetry in real units, converted from the radio's HP
    // status stream with the ACTIVE hardware profile's constants
    // (Thetis convertToVolts/convertToAmps parity).  NaN when the
    // session is down or the model has no volts/amps telemetry —
    // same contract as HL2Stream's getters, so the meter can prefer
    // whichever wire path is live.
    double supplyVolts() const { return supplyV_; }
    double paCurrentA() const { return paA_; }

public slots:
    // Open a P2 session to <ip>: seed DDC0 from the current VFO,
    // enable it at the engine's IQ rate, start the run-bit handshake.
    // Refuses (with a log line) while the P1 stream is running.
    // <mac>, when known (discovery rows carry it), selects the
    // radio's Layer-2 profile — its model + antenna override the
    // global defaults.
    void open(const QString &ip, const QString &mac = QString());
    // Tear down (HP run=0) and release the radio's controller lease.
    void close();

signals:
    void runningChanged();
    void logLine(QString line);

private:
    quint16 chooseRateKhz();   // engine rate → legal P2 rate (may set engine rate)
    // Mirror the tuning state onto the session: DDC0 = dial + RIT
    // (matching the HL2 path's pushEffectiveRxFreq semantics), DUC =
    // dial.  Called on every rx1FreqChanged AND ritChanged.
    void pushDialToSession();

    lyra::ipc::HL2Stream  *stream_  = nullptr;
    lyra::dsp::WdspEngine *engine_  = nullptr;
    QThread    thread_;
    P2Session *session_ = nullptr;   // lives on thread_
    bool       running_ = false;
    QString    ip_;
    quint16    rateKhz_ = 192;
    // Telemetry conversion state (main thread only — the status
    // signal is queued onto the bridge's thread).
    bool       hasVoltsAmps_ = false;
    float      ampVoff_  = 360.f;    // profile GetDefaultVoltCalibration
    float      ampSens_  = 120.f;
    double     supplyV_  = std::numeric_limits<double>::quiet_NaN();
    double     paA_      = std::numeric_limits<double>::quiet_NaN();
};

} // namespace lyra::wire

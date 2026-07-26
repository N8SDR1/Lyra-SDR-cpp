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
#include <QVariant>
#include <limits>

namespace lyra::ipc { class HL2Stream; }
namespace lyra::dsp { class WdspEngine; }

namespace lyra::wire {

class P2Session;

class P2RxBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(int rxAttenuationDb READ rxAttenuationDb WRITE setRxAttenuationDb
               NOTIFY frontEndChanged)
    Q_PROPERTY(int ddcAdc READ ddcAdc WRITE setDdcAdc NOTIFY frontEndChanged)
    Q_PROPERTY(int rxInput READ rxInput WRITE setRxInput NOTIFY frontEndChanged)
    Q_PROPERTY(bool hpfBypass READ hpfBypass WRITE setHpfBypass
               NOTIFY frontEndChanged)
    Q_PROPERTY(int trxAntenna READ trxAntenna WRITE setTrxAntenna
               NOTIFY frontEndChanged)
    Q_PROPERTY(int adcOverloadTier READ adcOverloadTier NOTIFY telemetryChanged)
    Q_PROPERTY(int adcOverloadMask READ adcOverloadMask NOTIFY telemetryChanged)
    Q_PROPERTY(int adc1Peak READ adc1Peak NOTIFY telemetryChanged)
    Q_PROPERTY(int adc2Peak READ adc2Peak NOTIFY telemetryChanged)

public:
    explicit P2RxBridge(lyra::ipc::HL2Stream *stream,
                        lyra::dsp::WdspEngine *engine,
                        QObject *parent = nullptr);
    ~P2RxBridge() override;

    bool    isRunning() const { return running_; }
    // True from open() until close() completes — a superset of
    // isRunning() that also covers a pending (unconfirmed) attempt.
    // UI/mutual-exclusion/shutdown code that needs "is this bridge
    // doing anything with the radio right now" should use this, not
    // isRunning() — isRunning() alone lets an offline radio's
    // unconfirmed open_ sit invisible: Close stays disabled, a
    // concurrent P1 open isn't blocked, and the shutdown handler skips
    // its early close (bench finding 2026-07-20).
    bool    isOpen()    const { return open_; }
    QString targetIp()  const { return ip_; }

    // Live telemetry in real units, converted from the radio's HP
    // status stream with the ACTIVE hardware profile's constants
    // (Thetis convertToVolts/convertToAmps parity).  NaN when the
    // session is down or the model has no volts/amps telemetry —
    // same contract as HL2Stream's getters, so the meter can prefer
    // whichever wire path is live.
    double supplyVolts() const { return supplyV_; }
    double paCurrentA() const { return paA_; }
    int rxAttenuationDb() const { return rxAttenuationDb_; }
    int ddcAdc() const { return ddcAdc_; }
    int rxInput() const { return rxInput_; }
    bool hpfBypass() const { return hpfBypass_; }
    int trxAntenna() const { return trxAntenna_; }
    int adcOverloadTier() const { return adcOverloadTier_; }
    int adcOverloadMask() const { return adcOverloadMask_; }
    int adc1Peak() const { return adc1Peak_; }
    int adc2Peak() const { return adc2Peak_; }
    double meterCalibrationOffset() const { return meterCalOffset_; }
    double displayCalibrationOffset() const { return displayCalOffset_; }

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
    void setRxAttenuationDb(int db);
    void setDdcAdc(int adc);
    void setRxInput(int input);
    void setHpfBypass(bool on);
    void setTrxAntenna(int ant);

signals:
    void runningChanged();
    // Fires whenever isOpen() changes — on both open() and close(),
    // independent of whether the radio ever confirmed.
    void openChanged();
    void frontEndChanged();
    void telemetryChanged();
    void logLine(QString line);

private:
    quint16 chooseRateKhz();   // engine rate → legal P2 rate (may set engine rate)
    // Mirror the tuning state onto the session: DDC0 = dial + RIT
    // (matching the HL2 path's pushEffectiveRxFreq semantics), DUC =
    // dial.  Called on every rx1FreqChanged AND ritChanged.
    void pushDialToSession();
    void restoreFrontEndForBand(const QString &band);
    void persistFrontEndValue(const QString &name, const QVariant &value);
    void pushFrontEndToSession();
    void activateTxProducerSeam();
    void deactivateTxProducerSeam();

    lyra::ipc::HL2Stream  *stream_  = nullptr;
    lyra::dsp::WdspEngine *engine_  = nullptr;
    QThread    thread_;
    P2Session *session_ = nullptr;   // lives on thread_
    // open_  = an open() attempt is in flight or a session is live —
    //          set synchronously in open(), cleared in close().  Used
    //          for the open()/close() reentry guard and to gate
    //          control-plane pushes (pushDialToSession) that are safe
    //          to send whether or not the radio has answered yet.
    // running_ = the radio has actually CONFIRMED the session (the
    //          first HP status packet arrived — P2Session::started).
    //          isRunning() reports this, not open_, so a stale DHCP
    //          address or offline radio doesn't show "connected"
    //          forever (bench finding 2026-07-20).
    bool       open_    = false;
    bool       running_ = false;
    bool       txProducerSeamActive_ = false;
    QString    ip_;
    quint16    rateKhz_ = 192;
    // Telemetry conversion state (main thread only — the status
    // signal is queued onto the bridge's thread).
    bool       hasVoltsAmps_ = false;
    float      ampVoff_  = 360.f;    // profile GetDefaultVoltCalibration
    float      ampSens_  = 120.f;
    double     supplyV_  = std::numeric_limits<double>::quiet_NaN();
    double     paA_      = std::numeric_limits<double>::quiet_NaN();
    QString    currentBand_;
    int        defaultTrxAntenna_ = 1;
    int        rxAttenuationDb_ = 0;
    int        ddcAdc_ = 0;
    int        rxInput_ = 0;
    bool       hpfBypass_ = false;
    int        trxAntenna_ = 1;
    int        adcOverloadTier_ = 0;
    int        adcOverloadMask_ = 0;
    int        adc1Peak_ = 0;
    int        adc2Peak_ = 0;
    int        overloadDecayPackets_ = 0;
    double     meterCalOffset_ = 0.0;
    double     displayCalOffset_ = 0.0;
};

} // namespace lyra::wire

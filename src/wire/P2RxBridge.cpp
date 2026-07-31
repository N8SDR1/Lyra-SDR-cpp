// Lyra — Protocol 2 RX bridge implementation.  See P2RxBridge.h.

#include "P2RxBridge.h"

#include "FrameComposer.h"
#include "P2Session.h"
#include "P2TxCmaster.h"
#include "Router.h"
#include "bandmemory.h"
#include "hardware/HardwareCatalog.h"
#include "hl2_stream.h"
#include "rig/RigRegistry.h"
#include "rig/RigScope.h"
#include "wdsp_engine.h"

#include <QMetaObject>
#include <QSettings>
#include <QTimer>
#include <QtDebug>
#include <algorithm>
#include <cmath>
#include <vector>

namespace lyra::wire {

namespace {
// Legal Protocol 2 DDC rates (kHz) — p2app rejects the whole DDC
// config packet on any other value (protocol2_command.c).
bool p2RateLegal(int khz) {
    return khz == 48 || khz == 96 || khz == 192 ||
           khz == 384 || khz == 768 || khz == 1536;
}

QString rigScopedKey(const QString &rigId, const QString &logicalKey) {
    if (!rigId.isEmpty())
        return QStringLiteral("rig/%1/%2").arg(rigId, logicalKey);
    return lyra::rig::scope::rigKey(logicalKey);
}

QString frontEndBandPrefix(const QString &rigId, const QString &band) {
    if (band.isEmpty()) return QString();
    return rigScopedKey(rigId, QStringLiteral("band_mem/")) +
           band + QStringLiteral("/p2/");
}
} // namespace

P2RxBridge::P2RxBridge(lyra::ipc::HL2Stream *stream,
                       lyra::dsp::WdspEngine *engine, QObject *parent)
    : QObject(parent), stream_(stream), engine_(engine) {
    session_ = new P2Session();          // parentless — moves to thread_
    session_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, session_, &QObject::deleteLater);
    thread_.setObjectName(QStringLiteral("lyra-p2-session"));
    thread_.start();

    // IQ frames → interleaved normalized doubles → router 0.  The
    // context object is session_, so this runs ON the session thread
    // (direct call at emit time) — the single feeder thread.  Decode
    // recipe = Thetis network.c:591-601: each 6-byte sample is I then
    // Q, big-endian signed 24-bit placed in the top 3 bytes of an
    // int32, scaled by 1/2^31.
    connect(session_, &P2Session::iqFrameReceived, session_,
            [](int ddc, quint32 /*seq*/, const QByteArray &iq) {
                if (ddc != 0) return;
                static thread_local std::vector<double> buf;
                const int n = iq.size() / 6;
                buf.resize(static_cast<std::size_t>(n) * 2);
                const auto *u =
                    reinterpret_cast<const unsigned char *>(iq.constData());
                for (int i = 0, k = 0; i < n; ++i, k += 6) {
                    const auto iRaw = static_cast<qint32>(
                        (u[k]     << 24) | (u[k + 1] << 16) | (u[k + 2] << 8));
                    const auto qRaw = static_cast<qint32>(
                        (u[k + 3] << 24) | (u[k + 4] << 16) | (u[k + 5] << 8));
                    buf[2 * static_cast<std::size_t>(i)]     = iRaw / 2147483648.0;
                    buf[2 * static_cast<std::size_t>(i) + 1] = qRaw / 2147483648.0;
                }
                xrouter(router_instance(0), 0, /*source=*/0, n, buf.data());
            });

    // Session diagnostics → re-emit as our own logLine.  Does NOT
    // call qInfo() itself — MainWindow connects P2RxBridge::logLine
    // to qInfo() once, covering both these relayed session lines and
    // the bridge's own (hardware profile, audio route, ...); doing it
    // here too double-printed every session-originated line once that
    // connection existed (bench-caught 2026-07-20).
    connect(session_, &P2Session::logLine, this,
            [this](const QString &l) { emit logLine(l); });

    // Telemetry: HP status → real units via the active profile's
    // constants (Thetis convertToVolts/convertToAmps, console.cs:
    // volts = raw/4095·5·(23/1.1) from AIN3; amps = max(0,
    // (raw·5000/4095 − voff)/sens) from AIN4).  Light smoothing —
    // status arrives ~5/s in RX; the meter model smooths further.
    connect(session_, &P2Session::statusReceived, this,
            [this](quint32, quint8 pttBits, quint8 overflows,
                   quint16, quint16 fwdRaw, quint16 revRaw, quint16,
                   quint16 adc1Peak, quint16 adc2Peak,
                   quint16 ain3Raw, quint16 ain4Raw, quint16,
                   quint8, quint16 ducFifo) {
                if (!running_) return;
                adcOverloadMask_ = overflows;
                adc1Peak_ = adc1Peak;
                adc2Peak_ = adc2Peak;
                ducFifoSamples_ = ducFifo;
                if (overflows != 0) {
                    adcOverloadTier_ = 2;
                    overloadDecayPackets_ = 5;
                } else if (adcOverloadTier_ == 2) {
                    adcOverloadTier_ = 1;
                } else if (adcOverloadTier_ == 1 &&
                           --overloadDecayPackets_ <= 0) {
                    adcOverloadTier_ = 0;
                }
                if (hasVoltsAmps_) {
                    const double volts =
                        (ain3Raw / 4095.0) * 5.0 * ((22.0 + 1.0) / 1.1);
                    const double mv = ain4Raw * 5000.0 / 4095.0;
                    const double amps =
                        std::max(0.0, (mv - ampVoff_) / ampSens_);
                    supplyV_ = std::isnan(supplyV_)
                                   ? volts : supplyV_ + 0.5 * (volts - supplyV_);
                    paA_ = std::isnan(paA_)
                               ? amps : paA_ + 0.5 * (amps - paA_);
                }
                if (g2PowerTelemetry_) {
                    // Thetis computeAlexFwd/RevPower ANAN-G2 constants:
                    // V=(ADC-offset)/4095*5, W=V^2/bridge. REV uses a
                    // different 6 m coupler constant.
                    const auto watts = [](quint16 raw, int offset,
                                          double bridge) {
                        const double v = std::max(
                            0.0, (static_cast<double>(raw) - offset) /
                                     4095.0 * 5.0);
                        return v * v / bridge;
                    };
                    const bool sixMetres =
                        stream_ && stream_->txFreqHz() >= 50'000'000u;
                    const double fwd = watts(fwdRaw, 32, 0.12);
                    const double rev =
                        watts(revRaw, 28, sixMetres ? 0.7 : 0.15);
                    fwdPowerW_ = std::isnan(fwdPowerW_)
                        ? fwd : fwdPowerW_ + 0.35 * (fwd - fwdPowerW_);
                    revPowerW_ = std::isnan(revPowerW_)
                        ? rev : revPowerW_ + 0.35 * (rev - revPowerW_);
                }

                // Saturn status byte 4 bit 0 is physical PTT OR keyer
                // activity (GetP2PTTKeyInputs). Feed edges into Lyra's
                // existing source-aware MOX FSM, behind its opt-in.
                const bool hwPtt = (pttBits & 0x01u) != 0;
                if (hwPtt != lastHardwarePtt_) {
                    lastHardwarePtt_ = hwPtt;
                    if (stream_ && stream_->hwPttEnabled())
                        stream_->requestMoxFromHwPtt(hwPtt);
                }
                emit telemetryChanged();
            });

    connect(session_, &P2Session::txStateChanged, this,
            [this](bool ready, bool armed, bool transmitting,
                   bool paEnabled, int drive, bool fault,
                   const QString &detail) {
                txTransportReady_ = ready;
                txBenchArmed_ = armed;
                txTransmitting_ = transmitting;
                txPaEnabled_ = paEnabled;
                txEffectiveDrive_ = drive;
                txFaultLatched_ = fault;
                txStateDetail_ = detail;
                if (fault && stream_ && stream_->moxActive())
                    stream_->requestMox(false);
                emit txStateChanged();
            });

    // Connection confirmation: isRunning() must not go true until the
    // radio has actually answered.  open() only BINDS the socket and
    // fires off the general/HP packets — a stale DHCP address or an
    // offline radio would otherwise report "connected" forever (the
    // socket bind succeeds regardless of whether anything is
    // listening at the far end).  P2Session::started() fires from
    // parseStatus() the first time a real HP status packet comes
    // back, which is the earliest honest "the radio is there" signal
    // — mirrors how the P1 path only shows Connected after HL2Stream
    // itself confirms traffic, not merely after calling open().
    connect(session_, &P2Session::started, this, [this](const QString &ip) {
        if (!open_) return;   // a stale signal from a session we already left
        running_ = true;
        ip_      = ip;
        emit runningChanged();
    });
    connect(session_, &P2Session::stopped, this, [this]() {
        if (!running_) return;
        running_ = false;
        emit runningChanged();
    });

    // VFO + RIT follow: HL2Stream stays the tuning authority (see
    // header); the DDC mirrors the RIT-adjusted effective RX
    // frequency exactly as the P1 path's pushEffectiveRxFreq does.
    if (stream_) {
        connect(stream_, &lyra::ipc::HL2Stream::rx1FreqChanged, this,
                [this]() { pushDialToSession(); });
        connect(stream_, &lyra::ipc::HL2Stream::ritChanged, this,
                [this]() { pushDialToSession(); });
        connect(stream_, &lyra::ipc::HL2Stream::splitEnabledChanged, this,
                [this]() { pushDialToSession(); });
        connect(stream_, &lyra::ipc::HL2Stream::vfoBHzChanged, this,
                [this]() { pushDialToSession(); });
        connect(stream_, &lyra::ipc::HL2Stream::xitChanged, this,
                [this]() { pushDialToSession(); });
        connect(stream_, &lyra::ipc::HL2Stream::freqCorrectionChanged,
                this, [this](double) { pushDialToSession(); });
        // This fires synchronously inside requestMox(true), before the
        // FSM advances. Cancelling here prevents even a transient TXA
        // start when the P2 connection has not been deliberately armed.
        connect(stream_, &lyra::ipc::HL2Stream::moxIntentPulse, this,
                [this]() {
                    if (!open_)
                        return;
                    if (txBenchArmed_ && txTransportReady_ &&
                        !txFaultLatched_)
                        return;
                    emit logLine(QStringLiteral(
                        "P2 TX: key request rejected - arm the P2 dummy-load "
                        "interlock and verify TX transport Ready"));
                    stream_->requestMox(false);
                });
        connect(stream_, &lyra::ipc::HL2Stream::moxActiveChanged, this,
                [this](bool on) {
                    if (open_)
                        syncTxIntentToSession(on);
                });
        connect(stream_, &lyra::ipc::HL2Stream::paEnabledChanged, this,
                [this](bool) {
                    if (open_ && stream_ && stream_->moxActive())
                        syncTxIntentToSession(true);
                });
        connect(stream_, &lyra::ipc::HL2Stream::txDriveLevelChanged, this,
                [this](int) {
                    if (open_ && stream_ && stream_->moxActive())
                        syncTxIntentToSession(true);
                });
    }

    // IQ-rate follow: the Display panel's rate switch reopens the WDSP
    // channel at the new rate (spanChanged fires); mirror it onto the
    // DDC so pitch/span stay true.  spanChanged also fires on zoom —
    // the rate comparison filters those out.
    if (engine_) {
        connect(engine_, &lyra::dsp::WdspEngine::spanChanged, this,
                [this]() {
                    if (!running_ || !engine_) return;
                    const int khz = engine_->sampleRate() / 1000;
                    if (!p2RateLegal(khz) ||
                        khz == static_cast<int>(rateKhz_)) return;
                    rateKhz_ = static_cast<quint16>(khz);
                    auto *s = session_;
                    QMetaObject::invokeMethod(s, [s, khz]() {
                        s->enableDdc(0, static_cast<quint16>(khz));
                        // Let timer events delayed by the RX rebuild drain,
                        // then re-prime the still-RX TX transport.
                        QTimer::singleShot(
                            100, s,
                            [s]() { s->restartTxTransportRxState(); });
                    });
                    emit logLine(QStringLiteral(
                        "P2: DDC0 rate → %1 kHz (following engine)").arg(khz));
                });
    }
}

P2RxBridge::~P2RxBridge() {
    // Deterministic teardown: run=0 must reach the radio before the
    // socket thread dies, so the close is a BLOCKING queued call.
    if (thread_.isRunning()) {
        auto *s = session_;
        QMetaObject::invokeMethod(s, &P2Session::close,
                                  Qt::BlockingQueuedConnection);
    }
    deactivateTxProducerSeam();
    thread_.quit();
    thread_.wait(2000);
}

QString P2RxBridge::txStatus() const {
    if (!open_) return QStringLiteral("Disconnected");
    if (txFaultLatched_)
        return QStringLiteral("FAULT: %1").arg(txStateDetail_);
    if (!running_) return QStringLiteral("Waiting for radio");
    if (!txHardwareSupported_)
        return QStringLiteral("RX only - TX hardware profile unverified");
    if (!txTransportReady_) return QStringLiteral("Priming TX transport");
    if (txTransmitting_)
        return QStringLiteral("TX %1%2")
            .arg((txEffectiveDrive_ * 100 + 127) / 255)
            .arg(txPaEnabled_ ? QStringLiteral("% PA") : QStringLiteral("% PA off"));
    if (!txBenchArmed_) return QStringLiteral("Ready - RF disarmed");
    return QStringLiteral("Armed - dummy load only");
}

void P2RxBridge::syncTxIntentToSession(bool on) {
    if (!session_)
        return;
    if (on && !txHardwareSupported_)
        on = false;
    const int capRaw =
        (std::clamp(txDriveLimitPercent_, 0, 25) * 255 + 50) / 100;
    const int drive = stream_
        ? std::min(stream_->txDriveLevel(), capRaw) : 0;
    const bool pa = stream_ && stream_->paEnabled();
    auto *s = session_;
    QMetaObject::invokeMethod(s, [s, on, pa, drive]() {
        s->setTransmitIntent(on, pa, drive);
    });
}

void P2RxBridge::setTxBenchArmed(bool on) {
    if (on && (!txHardwareSupported_ || !running_ ||
               !txTransportReady_ || txFaultLatched_)) {
        emit logLine(QStringLiteral(
            "P2 TX: cannot arm - G2 hardware profile and healthy "
            "radio/transport are required"));
        emit txStateChanged();
        return;
    }
    if (on == txBenchArmed_)
        return;
    txBenchArmed_ = on;
    auto *s = session_;
    QMetaObject::invokeMethod(s, [s, on]() {
        s->setTxOperatorArmed(on);
    });
    if (!on && stream_ && stream_->moxActive())
        stream_->requestMox(false);
    emit txStateChanged();
}

void P2RxBridge::setTxDriveLimitPercent(int percent) {
    const int limited = std::clamp(percent, 1, 25);
    if (limited == txDriveLimitPercent_)
        return;
    txDriveLimitPercent_ = limited;
    QSettings().setValue(
        rigScopedKey(rigId_, QStringLiteral("p2/txDriveLimitPct")), limited);
    if (open_ && stream_ && stream_->moxActive())
        syncTxIntentToSession(true);
    emit txStateChanged();
}

void P2RxBridge::resetTxUiState() {
    txBenchArmed_ = false;
    txHardwareSupported_ = false;
    txTransportReady_ = false;
    txTransmitting_ = false;
    txPaEnabled_ = false;
    txFaultLatched_ = false;
    txEffectiveDrive_ = 0;
    txStateDetail_ = QStringLiteral("Disconnected");
    ducFifoSamples_ = 0;
    lastHardwarePtt_ = false;
}

void P2RxBridge::activateTxProducerSeam() {
    if (txProducerSeamActive_)
        return;

    txProducerSeamActive_ = activateP2TxCmasterProducer();
    if (txProducerSeamActive_) {
        emit logLine(QStringLiteral(
            "P2 TX: WDSP producer seam ready (48 -> 192 kHz resampler "
            "-> bounded FIFO; port 1029 starts after radio status; "
            "transmit/drive/PA remain disarmed)"));
    } else {
        emit logLine(QStringLiteral(
            "P2 TX: CMaster producer seam unavailable; TX remains disabled"));
    }
}

void P2RxBridge::deactivateTxProducerSeam() {
    if (!txProducerSeamActive_)
        return;

    // Disable acceptance before restoring the P1 callback so the dedicated
    // P2 producer cannot race P1 teardown/startup.
    deactivateP2TxCmasterProducer();
    txProducerSeamActive_ = false;
}

void P2RxBridge::pushDialToSession() {
    // open_ (not running_): safe/meaningful to push a frequency update
    // to the session as soon as it's opening, whether or not the
    // radio has confirmed the session yet (P2Session applies it on
    // its next HP tick regardless).
    if (!open_ || !stream_) return;
    const QString band =
        lyra::ui::BandMemory::bandNameFor(static_cast<int>(stream_->rx1FreqHz()));
    if (band != currentBand_) {
        currentBand_ = band;
        restoreFrontEndForBand(band);
    }
    quint32 rx = stream_->rx1FreqHz();
    if (stream_->ritEnabled())
        rx = static_cast<quint32>(
            std::max<qint64>(0, qint64(rx) + stream_->ritOffsetHz()));
    const quint32 tx = stream_->txFreqHz();
    // Match P1's final wire-frequency choke point: calibration applies
    // after RIT for RX and to the effective TX carrier (VFO B/XIT when
    // enabled) for the DUC.
    rx = static_cast<quint32>(corrected_freq(static_cast<int>(rx)));
    const quint32 correctedTx =
        static_cast<quint32>(corrected_freq(static_cast<int>(tx)));
    auto *s = session_;
    QMetaObject::invokeMethod(s, [s, rx, correctedTx]() {
        s->setDdcFrequencyHz(0, rx);
        s->setDucFrequencyHz(correctedTx);
    });
}

void P2RxBridge::restoreFrontEndForBand(const QString &band) {
    const QString p = frontEndBandPrefix(rigId_, band);
    QSettings s;
    rxAttenuationDb_ = p.isEmpty() ? 0
        : std::clamp(s.value(p + QStringLiteral("attenuationDb"), 0).toInt(),
                     0, 31);
    ddcAdc_ = p.isEmpty() ? 0
        : std::clamp(s.value(p + QStringLiteral("ddc0Adc"), 0).toInt(), 0, 1);
    rxInput_ = p.isEmpty() ? 0
        : std::clamp(s.value(p + QStringLiteral("rxInput"), 0).toInt(), 0, 3);
    hpfBypass_ = !p.isEmpty() &&
        s.value(p + QStringLiteral("hpfBypass"), false).toBool();
    trxAntenna_ = p.isEmpty() ? defaultTrxAntenna_
        : std::clamp(s.value(p + QStringLiteral("trxAntenna"),
                             defaultTrxAntenna_).toInt(), 1, 3);
    pushFrontEndToSession();
    emit frontEndChanged();
}

void P2RxBridge::persistFrontEndValue(const QString &name,
                                      const QVariant &value) {
    const QString p = frontEndBandPrefix(rigId_, currentBand_);
    if (!p.isEmpty()) QSettings().setValue(p + name, value);
}

void P2RxBridge::pushFrontEndToSession() {
    if (!open_ || !session_) return;
    auto *s = session_;
    const int att = rxAttenuationDb_;
    const int adc = ddcAdc_;
    const int input = rxInput_;
    const bool bypass = hpfBypass_;
    const int ant = trxAntenna_;
    QMetaObject::invokeMethod(s, [s, att, adc, input, bypass, ant]() {
        s->setAdcAttenuation(adc, att);
        s->setDdcAdc(0, adc);
        s->setRxInput(static_cast<P2RxInput>(input));
        s->setHpfBypass(bypass);
        s->setTrxAntenna(ant);
    });
}

void P2RxBridge::setRxAttenuationDb(int db) {
    db = std::clamp(db, 0, 31);
    if (db == rxAttenuationDb_) return;
    rxAttenuationDb_ = db;
    persistFrontEndValue(QStringLiteral("attenuationDb"), db);
    pushFrontEndToSession();
    emit frontEndChanged();
}

void P2RxBridge::setDdcAdc(int adc) {
    adc = std::clamp(adc, 0, 1);
    if (adc == ddcAdc_) return;
    ddcAdc_ = adc;
    persistFrontEndValue(QStringLiteral("ddc0Adc"), adc);
    pushFrontEndToSession();
    emit frontEndChanged();
}

void P2RxBridge::setRxInput(int input) {
    input = std::clamp(input, 0, 3);
    if (input == rxInput_) return;
    rxInput_ = input;
    persistFrontEndValue(QStringLiteral("rxInput"), input);
    pushFrontEndToSession();
    emit frontEndChanged();
}

void P2RxBridge::setHpfBypass(bool on) {
    if (on == hpfBypass_) return;
    hpfBypass_ = on;
    persistFrontEndValue(QStringLiteral("hpfBypass"), on);
    pushFrontEndToSession();
    emit frontEndChanged();
}

void P2RxBridge::setTrxAntenna(int ant) {
    ant = std::clamp(ant, 1, 3);
    if (ant == trxAntenna_) return;
    trxAntenna_ = ant;
    persistFrontEndValue(QStringLiteral("trxAntenna"), ant);
    pushFrontEndToSession();
    emit frontEndChanged();
}

quint16 P2RxBridge::chooseRateKhz() {
    // Follow the engine's configured IQ rate when legal for P2 (all of
    // the engine's own rate choices are).  Anything else → normalize
    // both sides to the 192 kHz default.
    int khz = engine_ ? engine_->sampleRate() / 1000 : 192;
    if (!p2RateLegal(khz)) {
        khz = 192;
        if (engine_) engine_->setSampleRate(192000);
    }
    return static_cast<quint16>(khz);
}

void P2RxBridge::open(const QString &ip, const QString &mac) {
    if (open_) {
        if (ip_ == ip) return;
        close();
    }
    if (stream_ && stream_->isRunning()) {
        emit logLine(QStringLiteral(
            "P2: an HL2 connection is active — Close it before opening "
            "a Protocol 2 radio"));
        return;
    }

    activateTxProducerSeam();
    resetTxUiState();
    rateKhz_ = chooseRateKhz();
    // Seed DDC0 from the dial (rx/freqHz persists across launches; 0
    // only on a truly fresh install → park on 20 m).
    quint32 hz = stream_ ? stream_->rx1FreqHz() : 0;
    if (hz == 0) hz = 14'100'000u;
    const quint32 correctedHz =
        static_cast<quint32>(corrected_freq(static_cast<int>(hz)));
    quint32 txHz = stream_ ? stream_->txFreqHz() : hz;
    if (txHz == 0) txHz = hz;
    const quint32 correctedTx =
        static_cast<quint32>(corrected_freq(static_cast<int>(txHz)));

    // Rig identity (folded into RigRegistry — was a parallel
    // RadioProfileStore, docs/architecture/p2_identity_reconciliation.md
    // Part 1): its model + antenna win over the global Settings
    // defaults when the MAC is known.  ensureRig() is idempotent — the
    // caller (Settings openItem) has usually already registered this
    // rig, but the "Open at startup" path calls straight in here, so
    // this stays self-sufficient.  RadioFamily::Unknown preserves
    // whatever family is already on record rather than overwriting it
    // with a guess (this call site has no board name to derive one).
    lyra::rig::RigProfile prof;
    if (!mac.isEmpty()) {
        const QString rigId = lyra::rig::registry::ensureRig(
            mac, lyra::rig::RadioFamily::Unknown, QString(), ip);
        prof = lyra::rig::registry::rig(rigId);
        // Seed sane P2 defaults the first time this rig is opened —
        // mirrors the old RadioProfileStore::touch() seeding (a P2
        // radio can't use the HL2-jack path, and Saturn is the only
        // bench-verified P2 model today).
        bool seeded = false;
        if (prof.hardwareModelKey.isEmpty()) {
            // Only seed Saturn/ANAN-G2 when this rig is actually known
            // to be that family (or not yet classified).  HardwareCatalog
            // has no Brick row — no bench-measured PA/telemetry constants
            // for it exists yet — so writing "ANAN-G2" into a rig we
            // already know is a Brick would silently persist Saturn's
            // telemetry/audio-amp capability and front-end word table as
            // if they were confirmed facts about different hardware
            // (bench finding 2026-07-20).  Leave it unset for a Brick;
            // the resolve-below falls back transiently instead.
            if (prof.family == lyra::rig::RadioFamily::AnanP2 ||
                prof.family == lyra::rig::RadioFamily::Unknown) {
                const auto *dm = lyra::hardware::defaultModelForBoard(10, true);
                if (dm) { prof.hardwareModelKey = QLatin1String(dm->key); seeded = true; }
            }
        }
        if (prof.audioRoute.isEmpty()) {
            prof.audioRoute = QStringLiteral("pc");
            seeded = true;
        }
        if (seeded) lyra::rig::registry::upsertRig(prof);
    }
    rigId_ = prof.isValid() ? prof.rigId : QString();
    // Read the limit only after resolving this radio's identity. P2 rigs
    // are not the active rig, so RigScope alone would load another radio's
    // TX ceiling.
    txDriveLimitPercent_ = std::clamp(
        QSettings().value(
            rigScopedKey(rigId_, QStringLiteral("p2/txDriveLimitPct")),
            5).toInt(), 1, 25);

    // Per-radio audio routing — applied TRANSIENTLY (globals stay as
    // the HL2 configured them; restored at close()).  P2 profiles are
    // seeded "pc" so a Saturn just plays out the PC without touching
    // the HL2-jack setting.  route == "global" (the operator's explicit
    // "Follow global" choice) or empty (not yet seeded) both fall
    // through to the final branch below unchanged — no profile +
    // globals on the HL2 jack → warn (that path rides the P1 EP2
    // writer, which isn't running).
    if (engine_) {
        engine_->setRadioAudioSink({});   // clear any stale sink
        const QString route = prof.isValid() ? prof.audioRoute : QString();
        if (route == QLatin1String("radio")) {
            // RX audio out the RADIO's speaker (G2 on-board amp): the
            // engine's post-DSP int16 tee feeds the session's speaker
            // stream (fires on the session thread — see P2Session
            // sendSpeakerAudio).  The PC path is silenced via the
            // transient hl2 route so audio isn't doubled.  (That route
            // also applies the AK4951 jack attenuation to the gain —
            // ride the Volume slider; a dedicated gain trim can come
            // with the profile UI.)
            engine_->applyAudioRouteTransient(true, QString());
            auto *sess = session_;
            engine_->setRadioAudioSink([sess](const qint16 *lr, int n) {
                sess->sendSpeakerAudio(lr, n);
            });
        } else if (route == QLatin1String("pc"))
            engine_->applyAudioRouteTransient(false, QString());
        else if (route == QLatin1String("hl2"))
            engine_->applyAudioRouteTransient(true, QString());
        else if (engine_->audioDeviceIndex() == 0)
            emit logLine(QStringLiteral(
                "P2: RX audio output is set to the HL2 jack — pick a PC "
                "output device in Settings → Audio to hear this radio"));
    }

    // Hardware model (Layer-1 catalog; Thetis comboRadioModel
    // equivalent).  Precedence: radio profile → global setting →
    // Saturn-board default (the only bench-verified P2 family).
    QString modelKey = prof.isValid() ? prof.hardwareModelKey : QString();
    if (modelKey.isEmpty() && !prof.isValid())
        modelKey = QSettings().value(QStringLiteral("radio/hardwareModel"),
                                     QStringLiteral("ANAN-G2")).toString();
    const auto *hw = lyra::hardware::modelByKey(modelKey);
    if (!hw)
        emit logLine(QStringLiteral(
            "P2: no marketed hardware model selected for this radio; "
            "model-specific telemetry and front-end control stay disabled"));
    // Saturn is the ONLY bench-verified P2 model — if the rig's own
    // recorded family says otherwise (a Brick, most likely) but we're
    // still about to apply Saturn's constants, say so instead of
    // silently treating a guess as fact (bench finding 2026-07-20).
    // Telemetry conversion constants for this session's model.
    hasVoltsAmps_ = hw && hw->hasVolts && hw->hasAmps;
    g2PowerTelemetry_ = hw &&
        (modelKey.compare(QStringLiteral("ANAN-G2"), Qt::CaseInsensitive) == 0 ||
         modelKey.compare(QStringLiteral("ANAN-G2-1K"), Qt::CaseInsensitive) == 0);
    ampVoff_ = hw ? hw->voltOff  : 360.f;
    ampSens_ = hw ? hw->voltSens : 120.f;
    meterCalOffset_ = hw ? hw->rxMeterOffset : 0.0;
    displayCalOffset_ = hw ? hw->rxDisplayOffset : 0.0;
    if (engine_) engine_->setRxDisplayCalibrationDb(displayCalOffset_);
    supplyV_ = std::numeric_limits<double>::quiet_NaN();
    paA_     = std::numeric_limits<double>::quiet_NaN();
    fwdPowerW_ = std::numeric_limits<double>::quiet_NaN();
    revPowerW_ = std::numeric_limits<double>::quiet_NaN();
    if (hw)
        emit logLine(QStringLiteral(
            "P2: hardware profile %1 — %2 ADC%3, PS peak %4%5")
            .arg(QLatin1String(hw->displayName)).arg(hw->adcCount)
            .arg(hw->mkiiBpf ? QStringLiteral(", MkII BPF") : QString())
            .arg(hw->psDefaultPeakP2)
            .arg(prof.isValid()
                     ? QStringLiteral("  [radio profile %1]").arg(prof.mac)
                     : QString()));

    // TRX antenna port (ANT1/2/3): radio profile → global setting.
    const int ant = prof.isValid()
        ? prof.trxAntenna
        : std::clamp(QSettings()
                         .value(QStringLiteral("p2/trxAntenna"), 1)
                         .toInt(), 1, 3);

    // Runtime front-end policy follows the saved MARKETED model.  Board
    // id alone is insufficient (e.g. a Brick and a genuine Hermes-class
    // ANAN can share discovery identity but not front-end hardware).
    const auto *p2hw = lyra::wire::p2ProfileForModel(modelKey);
    // Only model-selected, bench-verified G2 profiles may expose the
    // transient TX arm. A generic/unverified P2 radio remains RX-only even
    // though the inert port-1029 transport can run safely in the background.
    txHardwareSupported_ = p2hw != nullptr;
    if (!p2hw)
        emit logLine(QStringLiteral(
            "P2: no verified antenna/filter profile for %1 — front end "
            "may not be configured correctly; verify manually")
            .arg(hw ? QLatin1String(hw->displayName) : modelKey));

    auto *s = session_;
    const quint16 rate = rateKhz_;
    defaultTrxAntenna_ = ant;
    currentBand_ =
        lyra::ui::BandMemory::bandNameFor(static_cast<int>(hz));
    restoreFrontEndForBand(currentBand_);
    const int att = rxAttenuationDb_;
    const int adc = ddcAdc_;
    const int input = rxInput_;
    const bool bypass = hpfBypass_;
    const int bandAnt = trxAntenna_;
    QMetaObject::invokeMethod(s, [s, ip, correctedHz, correctedTx, rate,
                                  bandAnt, p2hw,
                                  att, adc, input, bypass]() {
        s->setTxProducerSink([](const double *iq, int samples) {
            return feedP2TxCmasterInput(iq, samples);
        });
        s->setProfile(p2hw);
        s->setTrxAntenna(bandAnt);
        s->setAdcAttenuation(adc, att);
        s->setDdcAdc(0, adc);
        s->setRxInput(static_cast<P2RxInput>(input));
        s->setHpfBypass(bypass);
        s->setDdcFrequencyHz(0, correctedHz);
        // Start the DUC on the effective TX carrier, not the RX dial.
        // This matters immediately for split/VFO-B profiles; the normal
        // dial-update path continues to track later changes.
        s->setDucFrequencyHz(correctedTx);
        s->enableDdc(0, rate);
        s->open(ip);
    });

    ip_   = ip;
    open_ = true;
    emit openChanged();
    emit txStateChanged();
    emit logLine(QStringLiteral(
        "P2: opening %1 (DDC0 @ %2 kHz, %3 Hz)")
            .arg(ip).arg(rateKhz_).arg(correctedHz));
    // isRunning() stays false — and runningChanged() does NOT fire —
    // until P2Session::started() confirms the radio actually answered
    // (connected in the constructor).  See the open_/running_ split
    // in the header.
    pushDialToSession();   // refresh with RIT applied
}

void P2RxBridge::close() {
    if (!open_) return;
    auto *s = session_;
    if (stream_ && stream_->moxActive())
        stream_->requestMox(false);
    // BLOCKING: callers use close() to switch wire paths (Settings
    // Close, Start-button switch to an HL2) — the IQ feed must have
    // fully stopped before the P1 EP6 thread can start feeding the
    // same router sink.  The session thread is a pure event loop, so
    // this returns in microseconds.  Main-thread-only caller (would
    // deadlock if ever called on the session thread itself).
    QMetaObject::invokeMethod(s, &P2Session::close,
                              Qt::BlockingQueuedConnection);
    deactivateTxProducerSeam();
    open_    = false;
    running_ = false;
    ip_.clear();
    rigId_.clear();
    supplyV_ = std::numeric_limits<double>::quiet_NaN();
    paA_     = std::numeric_limits<double>::quiet_NaN();
    fwdPowerW_ = std::numeric_limits<double>::quiet_NaN();
    revPowerW_ = std::numeric_limits<double>::quiet_NaN();
    g2PowerTelemetry_ = false;
    adcOverloadTier_ = 0;
    adcOverloadMask_ = 0;
    adc1Peak_ = 0;
    adc2Peak_ = 0;
    overloadDecayPackets_ = 0;
    currentBand_.clear();
    meterCalOffset_ = 0.0;
    displayCalOffset_ = 0.0;
    resetTxUiState();
    if (engine_) engine_->setRxDisplayCalibrationDb(0.0);
    if (engine_) engine_->setRadioAudioSink({});   // stop the speaker tee
    // Return audio to the persisted global route (a per-radio "pc"
    // session route may have been applied at open; the HL2's
    // configured output comes back exactly as saved).
    if (engine_) engine_->restoreAudioRouteFromSettings();
    emit runningChanged();
    emit openChanged();
    emit telemetryChanged();
    emit frontEndChanged();
    emit txStateChanged();
}

} // namespace lyra::wire

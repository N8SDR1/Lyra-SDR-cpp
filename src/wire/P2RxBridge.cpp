// Lyra — Protocol 2 RX bridge implementation.  See P2RxBridge.h.

#include "P2RxBridge.h"

#include "P2Session.h"
#include "Router.h"
#include "hardware/HardwareCatalog.h"
#include "hl2_stream.h"
#include "rig/RigRegistry.h"
#include "wdsp_engine.h"

#include <QMetaObject>
#include <QSettings>
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
            [this](quint32, quint8, quint8, quint16, quint16, quint16,
                   quint16, quint16, quint16,
                   quint16 ain3Raw, quint16 ain4Raw) {
                if (!running_ || !hasVoltsAmps_) return;
                const double volts =
                    (ain3Raw / 4095.0) * 5.0 * ((22.0 + 1.0) / 1.1);
                const double mv = ain4Raw * 5000.0 / 4095.0;
                const double amps =
                    std::max(0.0, (mv - ampVoff_) / ampSens_);
                supplyV_ = std::isnan(supplyV_)
                               ? volts : supplyV_ + 0.5 * (volts - supplyV_);
                paA_     = std::isnan(paA_)
                               ? amps  : paA_     + 0.5 * (amps  - paA_);
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
    thread_.quit();
    thread_.wait(2000);
}

void P2RxBridge::pushDialToSession() {
    // open_ (not running_): safe/meaningful to push a frequency update
    // to the session as soon as it's opening, whether or not the
    // radio has confirmed the session yet (P2Session applies it on
    // its next HP tick regardless).
    if (!open_ || !stream_) return;
    quint32 rx = stream_->rx1FreqHz();
    if (stream_->ritEnabled())
        rx = static_cast<quint32>(
            std::max<qint64>(0, qint64(rx) + stream_->ritOffsetHz()));
    const quint32 tx = stream_->rx1FreqHz();
    auto *s = session_;
    QMetaObject::invokeMethod(s, [s, rx, tx]() {
        s->setDdcFrequencyHz(0, rx);
        s->setDucFrequencyHz(tx);
    });
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

    rateKhz_ = chooseRateKhz();
    // Seed DDC0 from the dial (rx/freqHz persists across launches; 0
    // only on a truly fresh install → park on 20 m).
    quint32 hz = stream_ ? stream_->rx1FreqHz() : 0;
    if (hz == 0) hz = 14'100'000u;

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
    if (modelKey.isEmpty())
        modelKey = QSettings().value(QStringLiteral("radio/hardwareModel"),
                                     QStringLiteral("ANAN-G2")).toString();
    const auto *hw = lyra::hardware::modelByKey(modelKey);
    if (!hw) hw = lyra::hardware::defaultModelForBoard(10, /*p2=*/true);
    // Saturn is the ONLY bench-verified P2 model — if the rig's own
    // recorded family says otherwise (a Brick, most likely) but we're
    // still about to apply Saturn's constants, say so instead of
    // silently treating a guess as fact (bench finding 2026-07-20).
    if (hw && hw->expectedBoard == 10 && prof.isValid() &&
        prof.family != lyra::rig::RadioFamily::AnanP2 &&
        prof.family != lyra::rig::RadioFamily::Unknown)
        emit logLine(QStringLiteral(
            "P2: no bench-verified hardware profile for this %1 — using "
            "Saturn (ANAN-G2) telemetry/front-end defaults as a "
            "placeholder; set the correct model in Settings → Hardware.")
            .arg(lyra::rig::capabilitiesFor(prof.family).familyName));
    // Telemetry conversion constants for this session's model.
    hasVoltsAmps_ = hw && hw->hasVolts && hw->hasAmps;
    ampVoff_ = hw ? hw->voltOff  : 360.f;
    ampSens_ = hw ? hw->voltSens : 120.f;
    supplyV_ = std::numeric_limits<double>::quiet_NaN();
    paA_     = std::numeric_limits<double>::quiet_NaN();
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

    // Front-end (Alex) word-generator profile for this model — was
    // unconditionally Saturn's regardless of the Settings selection
    // (p2ProfileForBoard() ignored its argument; nothing here ever
    // called session_->setProfile()).  Now resolved from the selected
    // model's expected board and actually applied.  See
    // p2ProfileForBoard()'s own comment for which boards share
    // Saturn's word tables (the classic Alex HPF/LPF/ANT bit layout
    // is common to the whole family per Thetis's own
    // setBPF1ForOrionIISaturn sharing) versus genuinely unverified.
    const auto *p2hw = hw
        ? lyra::wire::p2ProfileForBoard(hw->expectedBoard) : nullptr;
    if (!p2hw)
        emit logLine(QStringLiteral(
            "P2: no verified antenna/filter profile for %1 — front end "
            "may not be configured correctly; verify manually")
            .arg(hw ? QLatin1String(hw->displayName) : modelKey));

    auto *s = session_;
    const quint16 rate = rateKhz_;
    QMetaObject::invokeMethod(s, [s, ip, hz, rate, ant, p2hw]() {
        if (p2hw) s->setProfile(p2hw);
        s->setTrxAntenna(ant);
        s->setDdcFrequencyHz(0, hz);
        s->setDucFrequencyHz(hz);
        s->enableDdc(0, rate);
        s->open(ip);
    });

    ip_   = ip;
    open_ = true;
    emit openChanged();
    emit logLine(QStringLiteral(
        "P2: opening %1 (DDC0 @ %2 kHz, %3 Hz)").arg(ip).arg(rateKhz_).arg(hz));
    // isRunning() stays false — and runningChanged() does NOT fire —
    // until P2Session::started() confirms the radio actually answered
    // (connected in the constructor).  See the open_/running_ split
    // in the header.
    pushDialToSession();   // refresh with RIT applied (seed was raw dial)
}

void P2RxBridge::close() {
    if (!open_) return;
    auto *s = session_;
    // BLOCKING: callers use close() to switch wire paths (Settings
    // Close, Start-button switch to an HL2) — the IQ feed must have
    // fully stopped before the P1 EP6 thread can start feeding the
    // same router sink.  The session thread is a pure event loop, so
    // this returns in microseconds.  Main-thread-only caller (would
    // deadlock if ever called on the session thread itself).
    QMetaObject::invokeMethod(s, &P2Session::close,
                              Qt::BlockingQueuedConnection);
    open_    = false;
    running_ = false;
    ip_.clear();
    supplyV_ = std::numeric_limits<double>::quiet_NaN();
    paA_     = std::numeric_limits<double>::quiet_NaN();
    if (engine_) engine_->setRadioAudioSink({});   // stop the speaker tee
    // Return audio to the persisted global route (a per-radio "pc"
    // session route may have been applied at open; the HL2's
    // configured output comes back exactly as saved).
    if (engine_) engine_->restoreAudioRouteFromSettings();
    emit runningChanged();
    emit openChanged();
}

} // namespace lyra::wire

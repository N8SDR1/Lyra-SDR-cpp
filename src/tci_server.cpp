// Lyra — TCI server.  See tci_server.h.

#include "tci_server.h"

#include "CwMacroModel.h"
#include "bandmemory.h"
#include "hl2_stream.h"
#include "logbuffer.h"
#include "metermodel.h"
#include "prefs.h"
#include "spotstore.h"
// TX-rip Phase 1 (Q2): tci_mic_source.h / tx_dsp_worker.h removed —
// TX DSP worker + TCI mic source are being rebuilt from empty files
// per docs/TX_ARCHITECTURAL_MAPPING.md §10.3.  Inbound TX_AUDIO_STREAM
// is dropped; R-H2 mic-source force/restore returns with the rebuild.
#include "tci/TciTxBridge.h"   // TCI TX-audio re-home sink (TX_ARCH §4.6/§10.3)
#include "wdsp_engine.h"

#include <QDateTime>
#include <QHostAddress>
#include <QSettings>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace lyra::ui {

namespace {
constexpr auto kKeyEnabled  = "tci/enabled";
constexpr auto kKeyPort     = "tci/port";
constexpr auto kKeyHost     = "tci/bind_host";
constexpr auto kKeyRate     = "tci/rate_limit_ms";
constexpr auto kKeyInitial  = "tci/send_initial_state";
constexpr auto kKeyEsdr3    = "tci/emulate_expertsdr3";
constexpr auto kKeySunSdr   = "tci/emulate_sunsdr2";
constexpr auto kKeyCwlu     = "tci/cwlu_becomes_cw";
constexpr auto kKeyCombo    = "tci/combo_sdrloggerplus";   // Lyra↔SDRLogger+ combo link
// vfo_limits advertised to clients (HL2 receive range, Hz).
constexpr qint64 kVfoLo = 10000;
constexpr qint64 kVfoHi = 55000000;

double dbToLinear(double db) {
    if (db <= -60.0) return 0.0;
    return std::pow(10.0, db / 20.0);
}

// #180 — per-rate default RX-audio packet size, verbatim from the
// reference (TCIServer.cs getDefaultAudioStreamSamples).  Applied when
// the client sets AUDIO_SAMPLERATE without an explicit AUDIO_STREAM_SAMPLES.
int defaultAudioStreamSamples(int rate) {
    switch (rate) {
        case 8000:  return 256;
        case 12000: return 512;
        case 24000: return 1024;
        case 48000:
        default:    return 2048;
    }
}
// #180 — the four LF-audio rates the spec/reference accept (TCI
// Protocol AUDIO_SAMPLERATE: 8/12/24/48 kHz; TCIServer.cs handleAudioSampleRate).
bool isSupportedAudioRate(int rate) {
    return rate == 8000 || rate == 12000 || rate == 24000 || rate == 48000;
}

// TCI v2.0 binary stream (§3.4): a 64-byte little-endian header (16×u32)
// followed by samples.  Header fields: [0]receiver [1]sample_rate
// [2]format [3]codec=0 [4]crc=0 [5]length(scalar count) [6]type
// [7]channels [8..15]reserved.
enum StreamType {
    STREAM_IQ        = 0,
    STREAM_RX_AUDIO  = 1,
    STREAM_TX_AUDIO  = 2,   // client → server, TX audio payload (Task #33)
    STREAM_TX_CHRONO = 3,   // server → client, "send me N samples" pull
    STREAM_LINEOUT   = 4
};
enum SampleFmt  { FMT_INT16 = 0, FMT_INT24 = 1, FMT_INT32 = 2, FMT_FLOAT32 = 3 };

void putU32(char *p, quint32 v) {            // little-endian store
    p[0] = char(v & 0xFF); p[1] = char((v >> 8) & 0xFF);
    p[2] = char((v >> 16) & 0xFF); p[3] = char((v >> 24) & 0xFF);
}
quint32 getU32(const char *p) noexcept {     // little-endian load
    return  quint32(quint8(p[0]))
         | (quint32(quint8(p[1])) <<  8)
         | (quint32(quint8(p[2])) << 16)
         | (quint32(quint8(p[3])) << 24);
}

// Task #33: bytes-per-sample for the four TCI sample formats.  Used to
// validate payload size against the header's `length` field before
// decoding.
int bytesPerSample(int fmt) {
    switch (fmt) {
        case FMT_INT16:   return 2;
        case FMT_INT24:   return 3;
        case FMT_INT32:   return 4;
        case FMT_FLOAT32: return 4;
        default:          return 0;
    }
}

// Task #33: decode an inbound TX_AUDIO_STREAM payload into mono
// float32 @ the source rate.  Handles all four sample formats, mono
// vs 2-channel (DIGU/DIGL complex IQ → real part = mono audio for
// the WDSP TXA chain, matching the working reference's behaviour for
// SSB/DIGU TX), and sanitizes NaN / ±Inf → 0 + clamps to ±4.0 per the
// working reference's `handleBinaryFrame` clamp at TCIServer.cs:
// 5661-5673.
//
// Returns the decoded mono samples (one float per frame, regardless
// of input channel count).  Returns empty if the input is malformed.
std::vector<float> decodeTciTxAudio(const char *data, int dataBytes,
                                    int fmt, int channels) {
    const int bps = bytesPerSample(fmt);
    if (bps == 0 || dataBytes < bps) return {};
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;          // TCI v2 spec: 1 or 2
    const int scalarCount = dataBytes / bps;
    const int frames = scalarCount / channels;
    if (frames <= 0) return {};

    std::vector<float> mono;
    mono.reserve(static_cast<std::size_t>(frames));

    auto decodeScalar = [&](int idx) -> float {
        const char *p = data + idx * bps;
        switch (fmt) {
            case FMT_INT16: {
                qint16 s = qint16(quint16(quint8(p[0])) | (quint16(quint8(p[1])) << 8));
                return float(s) / 32768.0f;
            }
            case FMT_INT24: {
                qint32 s = qint32(quint32(quint8(p[0]))
                                | (quint32(quint8(p[1])) << 8)
                                | (quint32(quint8(p[2])) << 16));
                if (s & 0x800000) s |= int(0xFF000000U); // sign-extend 24→32
                return float(s) / 8388608.0f;
            }
            case FMT_INT32: {
                qint32 s = qint32(getU32(p));
                return float(double(s) / 2147483648.0);
            }
            case FMT_FLOAT32: {
                float v;
                std::memcpy(&v, p, 4);
                return v;
            }
        }
        return 0.0f;
    };

    auto sanitize = [](float s) -> float {
        if (std::isnan(s) || std::isinf(s)) return 0.0f;
        if (s >  4.0f) return  4.0f;
        if (s < -4.0f) return -4.0f;
        return s;
    };

    if (channels == 1) {
        for (int f = 0; f < frames; ++f)
            mono.push_back(sanitize(decodeScalar(f)));
    } else {
        // 2-channel inbound: average left + right into a single mono
        // float per audio frame.  This matches the working reference's
        // default `TCITxStereoInputMode.Both` behaviour at
        // cmaster.cs:1412-1416 (`mono[i] = (float)((left + right) * 0.5)`).
        //
        // Earlier Lyra code took ONLY the left channel here on a
        // misread of the TCI spec §3.4 "complex signal if channels=2"
        // line — that line refers to TCI v2 IQ streams (a different
        // dispatch path), NOT TX_AUDIO_STREAM.  For TX audio the
        // 2-channel payload is plain stereo PCM and clients are free
        // to place the modulated waveform in either channel or both;
        // dropping the right channel silently lost the audio whenever
        // a client (MSHV-class digital-modes client) put the signal
        // in R-only or distributed it across both channels.  The
        // operator-bench symptom was a single vertical line on the
        // panadapter during FT8 transmit (suppressed-carrier residue
        // only) and zero PSKReporter spots.
        for (int f = 0; f < frames; ++f) {
            const float l = decodeScalar(f * 2);
            const float r = decodeScalar(f * 2 + 1);
            mono.push_back(sanitize(0.5f * (l + r)));
        }
    }
    return mono;
}
QByteArray streamHeader(quint32 receiver, quint32 rate, quint32 fmt,
                        quint32 length, quint32 type, quint32 channels) {
    QByteArray h(64, '\0');
    char *p = h.data();
    putU32(p + 0,  receiver);
    putU32(p + 4,  rate);
    putU32(p + 8,  fmt);
    putU32(p + 12, 0);          // codec
    putU32(p + 16, 0);          // crc
    putU32(p + 20, length);
    putU32(p + 24, type);
    putU32(p + 28, channels);
    // [8..15] reserved already zero.
    return h;
}

// Convert a mono float32 payload to the requested sample format, optionally
// duplicating to L=R for 2-channel audio.  Returns the sample bytes.
QByteArray audioPayload(const float *mono, int frames, int fmt, int chans) {
    const int scalars = frames * chans;
    auto dup = [&](auto writer) {
        for (int f = 0; f < frames; ++f)
            for (int c = 0; c < chans; ++c) writer(mono[f]);
    };
    // Integer formats round-to-nearest (as Thetis encodeSamples: Math.Round
    // before the cast), not truncate — a sub-LSB but byte-affecting nuance.
    QByteArray out;
    if (fmt == FMT_FLOAT32) {
        out.resize(scalars * 4); float *d = reinterpret_cast<float *>(out.data());
        int i = 0; dup([&](float v){ d[i++] = v; });
    } else if (fmt == FMT_INT32) {
        out.resize(scalars * 4); char *d = out.data(); int i = 0;
        dup([&](float v){ putU32(d + 4 * i++, quint32(qint32(std::lround(std::clamp(v,-1.f,1.f) * 2147483647.0f)))); });
    } else if (fmt == FMT_INT24) {
        out.resize(scalars * 3); char *d = out.data(); int i = 0;
        dup([&](float v){ qint32 s = qint32(std::lround(std::clamp(v,-1.f,1.f) * 8388607.0f));
            d[3*i] = char(s & 0xFF); d[3*i+1] = char((s>>8)&0xFF); d[3*i+2] = char((s>>16)&0xFF); ++i; });
    } else {  // FMT_INT16
        out.resize(scalars * 2); char *d = out.data(); int i = 0;
        dup([&](float v){ qint16 s = qint16(std::lround(std::clamp(v,-1.f,1.f) * 32767.0f));
            d[2*i] = char(s & 0xFF); d[2*i+1] = char((s>>8)&0xFF); ++i; });
    }
    return out;
}
}

TciServer::TciServer(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                     lyra::dsp::WdspEngine *engine, SpotStore *spots,
                     QObject *parent)
    : QObject(parent), prefs_(prefs), stream_(stream), engine_(engine),
      spots_(spots) {
    rateClock_.start();
    QSettings s;
    enabled_           = s.value(QString::fromLatin1(kKeyEnabled), false).toBool();
    // Default 40001 — the ExpertSDR3 / TCI convention AND, crucially, it sits
    // BELOW the Windows ephemeral range (49152-65535).  50001 (the old default)
    // is INSIDE that range, so any service asking the OS for a temporary port
    // at boot (e.g. Acronis' scheduler) can be randomly handed 50001 and win it
    // before Lyra — a rare, intermittent "TCI won't start" that looks like a
    // regression but is a port lottery.  40001 can't be grabbed that way.
    port_              = s.value(QString::fromLatin1(kKeyPort), 40001).toInt();
    bindHost_          = s.value(QString::fromLatin1(kKeyHost),
                                 QStringLiteral("127.0.0.1")).toString();
    rateLimitMs_       = s.value(QString::fromLatin1(kKeyRate), 20).toInt();
    sendInitialState_  = s.value(QString::fromLatin1(kKeyInitial), true).toBool();
    emulateExpertSdr3_ = s.value(QString::fromLatin1(kKeyEsdr3), false).toBool();
    emulateSunSdr2_    = s.value(QString::fromLatin1(kKeySunSdr), false).toBool();
    cwluBecomesCw_     = s.value(QString::fromLatin1(kKeyCwlu), true).toBool();
    comboEnabled_      = s.value(QString::fromLatin1(kKeyCombo), false).toBool();

    smeterTimer_ = new QTimer(this);
    smeterTimer_->setInterval(250);   // 4 Hz sensor cadence
    connect(smeterTimer_, &QTimer::timeout, this, &TciServer::onSmeterTick);

    // Keepalive ping + dead-socket prune.  A client that drops without a
    // clean WebSocket close (process killed, network blip, reconnect) won't
    // fire `disconnected` until a write fails — the ping forces that, and
    // the prune drops any socket no longer in the Connected state so the
    // client count stays accurate (fixes the "shows 1 when none / 2 when
    // one" stale-ghost drift).
    maintTimer_ = new QTimer(this);
    maintTimer_->setInterval(8000);
    connect(maintTimer_, &QTimer::timeout, this, &TciServer::onMaintenanceTick);

    // Trailing-edge flush for rate-limited broadcasts (A3): single-shot, armed
    // by broadcast() when it drops an update inside the rate-limit window.
    rlFlushTimer_ = new QTimer(this);
    rlFlushTimer_->setSingleShot(true);
    connect(rlFlushTimer_, &QTimer::timeout, this, &TciServer::onRlFlush);

    if (stream_) {
        connect(stream_, &lyra::ipc::HL2Stream::rx1FreqChanged,
                this, &TciServer::onFreqChanged);
        // TCI §3.1 synchronizer: the server must echo VFO B (vfo:0,1) back to
        // ALL clients on every change, same as VFO A.  Without this echo a
        // digital-mode client (WSJT-X) never gets its "did VFO B take?"
        // confirmation and re-sends VFO B in a loop → a stale old-band VFO B
        // can land after the RX moved = the cross-band / "2-3 clicks" bug.
        connect(stream_, &lyra::ipc::HL2Stream::vfoBHzChanged,
                this, &TciServer::onVfoBChanged);
        // §3.1 echo: whether split came from the operator's toggle or was
        // derived from a client's VFO B, confirm the new state to every client.
        connect(stream_, &lyra::ipc::HL2Stream::splitEnabledChanged, this, [this]() {
            if (!stream_) return;
            broadcast(QStringLiteral("split_enable:0"),
                      QStringLiteral("split_enable:0,%1")
                          .arg(stream_->splitEnabled() ? QStringLiteral("true")
                                                       : QStringLiteral("false")));
            // A split toggle flips the TX-freq source (VFO A↔B), so the actual
            // TX carrier changes even though no VFO moved — re-announce it, as
            // Thetis does via its dedicated OnTXFrequencyChanged on VFOBTX change
            // (TCIServer.cs:7417).  Without this tx_frequency goes stale until
            // the next tune.
            broadcastTxFrequency();
        });
        connect(stream_, &lyra::ipc::HL2Stream::runningChanged,
                this, &TciServer::onRunningChanged);
        // Task #33 — SyncTciPttToMox (TCIServer.cs:5560-5577).  If the
        // wire MOX bit drops externally (operator click, FSM keyup,
        // foot-switch release, §15.20 TX-timeout fire) AND we owned
        // the TX-audio path, release ownership + stop the CHRONO
        // pump.  Keeps TCI-server state coherent with the real wire.
        connect(stream_, &lyra::ipc::HL2Stream::moxActiveChanged,
                this, &TciServer::onMoxActiveChanged);
    }
    // Task #33 — TX_CHRONO outbound pump.  Always constructed; only
    // ticks when the timer is started (in tryAcquireActiveTxAudioListener).
    chronoTimer_ = new QTimer(this);
    chronoTimer_->setInterval(kChronoIntervalMs);
    // PreciseTimer: Qt's default CoarseTimer downgrades sub-50 ms
    // intervals to ~5 % accuracy, which at 2 ms means the kernel
    // could quantise the tick up to ~20 ms.  Precise mode keeps the
    // 2 ms cadence the reference's TX stream loop establishes
    // (cmaster.cs:1253 `Thread.Sleep(2)`).  Per-tick work is
    // microseconds — Qt PreciseTimer at 500 Hz is well within budget.
    chronoTimer_->setTimerType(Qt::PreciseTimer);
    connect(chronoTimer_, &QTimer::timeout, this, &TciServer::onChronoTick);
    if (prefs_)
        connect(prefs_, &Prefs::modeChanged, this, &TciServer::onModeChanged);

    // Task #75 — cache the RX-out gain as a linear multiplier from
    // Prefs.tciRxGainDb so the audio hot path doesn't pay std::pow
    // per packet.  Seeded from the persisted value at ctor; refreshed
    // on every tciRxGainDbChanged emit (operator drags the Settings
    // slider, future TX-profile recall, etc.).
    if (prefs_) {
        rxGainLinear_ =
            std::pow(10.0, prefs_->tciRxGainDb() / 20.0);
        connect(prefs_, &Prefs::tciRxGainDbChanged, this, [this]() {
            rxGainLinear_ =
                std::pow(10.0, prefs_->tciRxGainDb() / 20.0);
        });
        // Task #108 — symmetric INBOUND (MSHV → Lyra TXA) gain cache.
        // Seeded from the persisted value at ctor; refreshed on every
        // tciTxGainDbChanged emit (operator drags the Settings slider,
        // future TX-profile recall, etc.).  Hot path applies the
        // multiplier in onTciAudioBlock right before TciMicSource->
        // submitFromTci, AFTER decode + resample.
        txGainLinear_ =
            std::pow(10.0, prefs_->tciTxGainDb() / 20.0);
        connect(prefs_, &Prefs::tciTxGainDbChanged, this, [this]() {
            txGainLinear_ =
                std::pow(10.0, prefs_->tciTxGainDb() / 20.0);
        });
    }
    if (engine_) {
        connect(engine_, &lyra::dsp::WdspEngine::volumeChanged,
                this, &TciServer::onVolumeChanged);
        connect(engine_, &lyra::dsp::WdspEngine::mutedChanged,
                this, &TciServer::onMutedChanged);
        connect(engine_, &lyra::dsp::WdspEngine::passbandChanged,
                this, &TciServer::onPassbandChanged);
        // Binary streams arrive from the DSP worker thread — queued onto
        // ours so the QWebSocket sends happen on the right thread.
        connect(engine_, &lyra::dsp::WdspEngine::tciAudioBlock,
                this, &TciServer::onTciAudioBlock, Qt::QueuedConnection);
        connect(engine_, &lyra::dsp::WdspEngine::tciIqBlock,
                this, &TciServer::onTciIqBlock, Qt::QueuedConnection);
    }
    if (spots_) {
        // Operator clicked a spot on the panadapter → echo it to clients
        // so the logger can log the QSO (spot_activated + rx_clicked_on_spot).
        connect(spots_, &SpotStore::spotActivated, this,
                [this](const QString &call, const QString &mode,
                       qint64 hz, quint32 argb) {
            broadcastNow(QStringLiteral("spot_activated:%1,%2,%3,%4")
                             .arg(call, mode).arg(hz).arg(argb));
            broadcastNow(QStringLiteral("rx_clicked_on_spot:0,0,%1,%2")
                             .arg(call).arg(hz));
        });
    }
}

TciServer::~TciServer() { stop(); }

// ── Lyra ↔ SDRLogger+ Combo link (docs/architecture/combo_link_design.md) ──
void TciServer::setCwMacros(CwMacroModel *m) {
    if (cwMacros_ == m) return;
    cwMacros_ = m;
    if (cwMacros_) {
        connect(cwMacros_, &CwMacroModel::contactChanged,
                this, &TciServer::onCwContactChanged, Qt::UniqueConnection);
        // Combo Stage B: {LOG}-tagged macro → log the QSO in SDRLogger+.
        connect(cwMacros_, &CwMacroModel::logQsoRequested,
                this, &TciServer::onLogQsoRequested, Qt::UniqueConnection);
    }
}

void TciServer::setComboEnabled(bool on) {
    if (comboEnabled_ == on) return;
    comboEnabled_ = on;
    QSettings().setValue(QString::fromLatin1(kKeyCombo), comboEnabled_);
    // Announce the master state so a linked SDRLogger+ shows/hides its
    // "Lyra Combo: Linked" indicator and starts/stops acting.
    if (running())
        broadcastNow(QStringLiteral("lyra_combo:%1")
                         .arg(comboEnabled_ ? QStringLiteral("on")
                                            : QStringLiteral("off")));
    // On enable, push the current contact so the logger syncs immediately.
    if (comboEnabled_) onCwContactChanged();
}

void TciServer::onCwContactChanged() {
    // Broadcast the CW Console contact row to a linked SDRLogger+.  Skipped
    // while applying an inbound remote update (echo guard — the name-back
    // must not bounce), and when Combo is off / nothing is running / no call.
    if (!comboEnabled_ || comboApplyingRemote_ || !running() || !cwMacros_) return;
    const QString call = cwMacros_->hisCall().trimmed().toUpper();
    if (call.isEmpty()) return;
    // lyra_contact:<src>,<call>,<rstSent>,<rstRcvd>,<name>,<qth>,<grid>,<serial>
    // Lyra owns call / rstSent / name / serial; rstRcvd / qth / grid ride
    // empty (no Lyra field today — SDRLogger+ fills name back into <name>).
    broadcastNow(QStringLiteral("lyra_contact:lyra,%1,%2,,%3,,,%4")
                     .arg(call, cwMacros_->rst().trimmed(),
                          cwMacros_->opName().trimmed(),
                          QString::number(cwMacros_->serial())));
}

void TciServer::onLogQsoRequested() {
    // Combo Stage B: a {LOG}-tagged CW macro was sent → ask a linked SDRLogger+
    // to log the current QSO. Gated exactly like the contact broadcast (Combo
    // on + running + a callsign to log). Nothing to log without a His Call.
    if (!comboEnabled_ || !running() || !cwMacros_) return;
    const QString call = cwMacros_->hisCall().trimmed().toUpper();
    if (call.isEmpty()) return;
    const QString rst  = cwMacros_->rst().trimmed();            // single Lyra RST → sent+rcvd
    const QString mode = prefs_ ? prefs_->mode() : QStringLiteral("CW");
    const qint64  carrier = (stream_ ? qint64(stream_->rx1FreqHz()) : 0)
                            + (engine_ ? engine_->markerOffsetHz() : 0);
    // lyra_log:<call>,<rstSent>,<rstRcvd>,<mode>,<freqHz>
    broadcastNow(QStringLiteral("lyra_log:%1,%2,%3,%4,%5")
                     .arg(call).arg(rst).arg(rst).arg(mode).arg(carrier));
}

bool TciServer::running() const { return server_ && server_->isListening(); }

void TciServer::setPort(int p) {
    if (p <= 0 || p > 65535 || p == port_) return;
    port_ = p;
    QSettings().setValue(QString::fromLatin1(kKeyPort), p);
    if (running()) { stop(); start(); }
}
void TciServer::setBindHost(const QString &h) {
    if (h == bindHost_) return;
    bindHost_ = h;
    QSettings().setValue(QString::fromLatin1(kKeyHost), h);
    if (running()) { stop(); start(); }
}
void TciServer::setRateLimitMs(int ms) {
    rateLimitMs_ = qBound(0, ms, 1000);
    QSettings().setValue(QString::fromLatin1(kKeyRate), rateLimitMs_);
}
void TciServer::setSendInitialState(bool on) {
    sendInitialState_ = on;
    QSettings().setValue(QString::fromLatin1(kKeyInitial), on);
}
void TciServer::setEmulateExpertSdr3(bool on) {
    emulateExpertSdr3_ = on;
    QSettings().setValue(QString::fromLatin1(kKeyEsdr3), on);
}
void TciServer::setEmulateSunSdr2(bool on) {
    emulateSunSdr2_ = on;
    QSettings().setValue(QString::fromLatin1(kKeySunSdr), on);
}
void TciServer::setCwluBecomesCw(bool on) {
    cwluBecomesCw_ = on;
    QSettings().setValue(QString::fromLatin1(kKeyCwlu), on);
}
void TciServer::setEnabled(bool on) {
    enabled_ = on;
    QSettings().setValue(QString::fromLatin1(kKeyEnabled), on);
    if (on) start(); else stop();
}

bool TciServer::start() {
    if (running()) return true;
    if (!server_) {
        server_ = new QWebSocketServer(QStringLiteral("Lyra TCI"),
                                       QWebSocketServer::NonSecureMode, this);
        connect(server_, &QWebSocketServer::newConnection,
                this, &TciServer::onNewConnection);
    }
    QHostAddress addr;
    if (bindHost_.isEmpty() || bindHost_ == QStringLiteral("0.0.0.0")
        || bindHost_ == QStringLiteral("*"))
        addr = QHostAddress::Any;
    else
        addr = QHostAddress(bindHost_);
    const bool ok = server_->listen(addr, quint16(port_));
    if (ok) {
        maintTimer_->start();
        bindError_.clear();
    } else {
        // Make the failure LOUD — port collisions were previously a silent
        // checkbox-bounce with nothing in the log.  Now it's in the log AND
        // the Settings → Network status line (via bindError()).
        bindError_ = tr("port %1 in use — %2 (try another port)")
                         .arg(port_).arg(server_->errorString());
        qWarning("[tci] failed to bind %s:%d — %s",
                 qPrintable(bindHost_), port_,
                 qPrintable(server_->errorString()));
    }
    emit statusMessage(ok ? tr("TCI server listening on %1:%2")
                                .arg(bindHost_).arg(port_)
                          : tr("TCI server failed to bind %1:%2 — %3")
                                .arg(bindHost_).arg(port_)
                                .arg(server_->errorString()));
    emit runningChanged();
    return ok;
}

void TciServer::stop() {
    smeterTimer_->stop();
    maintTimer_->stop();
    bindError_.clear();   // clean stop — no stale "port in use" left on screen
    // CODEX-P0 (2026-06-15) — release TX-audio ownership cleanly BEFORE the
    // owner sockets are deleteLater()'d below.  Without this, stop() (TCI
    // disable / profile-toggle that flips TCI-enable / shutdown) left
    // chronoTimer_ running against a soon-to-be-destroyed owner socket, so
    // onChronoTick dereferenced a dangling pointer (the ntdll 0xC0000005),
    // and could also leave the radio keyed + replay the previous
    // transmission's audio tail.  (txAudioOwner_ is now a QPointer too, so
    // the deref is crash-safe by construction; this is the matching clean
    // teardown.)
    chronoTimer_->stop();
    chronoOutstanding_ = 0;
    if (txAudioOwner_) {
        txAudioOwner_ = nullptr;
        if (stream_) stream_->requestMoxFromTci(false);
    }
    lyra::tci::TciTxBridge::instance().clear();   // drop stale TX backlog (no replay next session)
    sensorsEnabled_ = false;
    streams_.clear();
    recomputeStreaming();    // turn the engine taps back off
    destroyTxResampler();    // Task #68 — free WDSP resampler if any
    destroyRxResampler();    // #180 — free RX-out resampler if any
    if (!server_) return;
    for (QWebSocket *ws : std::as_const(clients_)) ws->deleteLater();
    clients_.clear();
    if (server_->isListening()) server_->close();
    emit clientCountChanged(0);
    emit runningChanged();
}

void TciServer::onNewConnection() {
    pruneDeadClients();   // clear any ghost before counting the new one
    while (server_ && server_->hasPendingConnections()) {
        QWebSocket *ws = server_->nextPendingConnection();
        connect(ws, &QWebSocket::textMessageReceived,
                this, &TciServer::onTextMessage);
        // Task #33: TCI v2 binary frames (TX_AUDIO_STREAM in, RX/IQ
        // streams out).  Inbound parsing handles only TX_AUDIO_STREAM
        // (Task #33); other types arrive harmlessly + ignored.
        connect(ws, &QWebSocket::binaryMessageReceived,
                this, &TciServer::onBinaryMessage);
        connect(ws, &QWebSocket::disconnected,
                this, &TciServer::onClientDisconnected);
        clients_.append(ws);
        sendInit(ws);
        emit clientCountChanged(clients_.size());
    }
}

void TciServer::onClientDisconnected() {
    auto *ws = qobject_cast<QWebSocket *>(sender());
    if (!ws) return;
    // Task #33 — release TX-audio ownership cleanly if the dropping
    // client was the owner.  Drops in-flight TX_AUDIO_STREAM frames
    // (binary handler gates on owner identity), stops the CHRONO
    // pump, and releases MOX via the source-tagged wrapper.
    if (ws == txAudioOwner_) {
        txAudioOwner_ = nullptr;
        chronoOutstanding_ = 0;
        chronoTimer_->stop();
        emit statusMessage(QStringLiteral(
            "TCI: TX-audio ownership released (client disconnected)"));
        if (stream_) stream_->requestMoxFromTci(false);
        // TX-rip Phase 1 (Q2): R-H2 activeMicSource_ save/restore
        // returns with the new TX DSP worker per
        // docs/TX_ARCHITECTURAL_MAPPING.md §10.3.
    }
    clients_.removeAll(ws);
    streams_.remove(ws);
    ws->deleteLater();
    if (clients_.isEmpty()) { smeterTimer_->stop(); sensorsEnabled_ = false; }
    recomputeStreaming();
    emit clientCountChanged(clients_.size());
}

void TciServer::pruneDeadClients() {
    const int before = clients_.size();
    for (int i = clients_.size() - 1; i >= 0; --i) {
        QWebSocket *ws = clients_[i];
        if (!ws || ws->state() != QAbstractSocket::ConnectedState) {
            clients_.removeAt(i);
            streams_.remove(ws);
            if (ws) ws->deleteLater();
        }
    }
    if (clients_.size() != before) {
        if (clients_.isEmpty()) { smeterTimer_->stop(); sensorsEnabled_ = false; }
        recomputeStreaming();
        emit clientCountChanged(clients_.size());
    }
}

std::vector<float> TciServer::resampleTxIn(const std::vector<float> &in,
                                           int inRate, int outRate) {
    // Reference: cmaster.cs:1431-1473 resampleTCITxSamples.
    if (in.empty() || inRate <= 0 || outRate <= 0) return {};
    if (inRate == outRate) return in;
    if (!engine_ || !engine_->wdspNative()) return {};
    const lyra::dsp::WdspApi &api = engine_->wdspNative()->api();
    if (!api.create_resampleFV || !api.xresampleFV
        || !api.destroy_resampleFV)
        return {};

    // Lazy create / recreate on rate change.
    if (!txResampler_
        || txResamplerInRate_  != inRate
        || txResamplerOutRate_ != outRate) {
        if (txResampler_) {
            api.destroy_resampleFV(txResampler_);
            txResampler_ = nullptr;
        }
        txResampler_       = api.create_resampleFV(inRate, outRate);
        txResamplerInRate_ = inRate;
        txResamplerOutRate_ = outRate;
        if (!txResampler_) return {};
    }

    // Output sizing: reference uses
    //   max(in.size + 64, ceil(in.size * outRate / inRate) + 64)
    // — generous slack so xresampleFV never overruns.
    const int inN = int(in.size());
    const int upperOut =
        int((qint64(inN) * outRate + inRate - 1) / inRate);
    const int maxOut = std::max(inN, upperOut) + 64;
    std::vector<float> out(size_t(maxOut), 0.0f);
    int outSamps = 0;
    // create_resampleFV takes a const input but the C signature is
    // non-const — safe to cast away since the resampler doesn't mutate.
    api.xresampleFV(const_cast<float *>(in.data()),
                    out.data(), inN, &outSamps, txResampler_);
    if (outSamps <= 0) return {};
    out.resize(size_t(outSamps));
    return out;
}

void TciServer::destroyTxResampler() {
    if (!txResampler_) return;
    if (engine_ && engine_->wdspNative()) {
        const lyra::dsp::WdspApi &api = engine_->wdspNative()->api();
        if (api.destroy_resampleFV) api.destroy_resampleFV(txResampler_);
    }
    txResampler_        = nullptr;
    txResamplerInRate_  = 0;
    txResamplerOutRate_ = 0;
}

// #180 — RX-out resampler, mirror of resampleTxIn for the outbound RX
// audio (engine 48 kHz → client AUDIO_SAMPLERATE).  Mono in / mono out;
// stateful across calls so the continuous RX stream resamples cleanly.
std::vector<float> TciServer::resampleRxOut(const std::vector<float> &in,
                                            int inRate, int outRate) {
    if (in.empty() || inRate <= 0 || outRate <= 0) return {};
    if (inRate == outRate) return in;                 // fast path (48k client)
    if (!engine_ || !engine_->wdspNative()) return {};
    const lyra::dsp::WdspApi &api = engine_->wdspNative()->api();
    if (!api.create_resampleFV || !api.xresampleFV || !api.destroy_resampleFV)
        return {};

    if (!rxResampler_
        || rxResamplerInRate_  != inRate
        || rxResamplerOutRate_ != outRate) {
        if (rxResampler_) {
            api.destroy_resampleFV(rxResampler_);
            rxResampler_ = nullptr;
        }
        rxResampler_        = api.create_resampleFV(inRate, outRate);
        rxResamplerInRate_  = inRate;
        rxResamplerOutRate_ = outRate;
        if (!rxResampler_) return {};
    }

    const int inN = int(in.size());
    const int upperOut =
        int((qint64(inN) * outRate + inRate - 1) / inRate);
    const int maxOut = std::max(inN, upperOut) + 64;
    std::vector<float> out(size_t(maxOut), 0.0f);
    int outSamps = 0;
    api.xresampleFV(const_cast<float *>(in.data()),
                    out.data(), inN, &outSamps, rxResampler_);
    if (outSamps <= 0) return {};
    out.resize(size_t(outSamps));
    return out;
}

void TciServer::destroyRxResampler() {
    if (!rxResampler_) return;
    if (engine_ && engine_->wdspNative()) {
        const lyra::dsp::WdspApi &api = engine_->wdspNative()->api();
        if (api.destroy_resampleFV) api.destroy_resampleFV(rxResampler_);
    }
    rxResampler_        = nullptr;
    rxResamplerInRate_  = 0;
    rxResamplerOutRate_ = 0;
}

void TciServer::recomputeStreaming() {
    bool anyAudio = false, anyIq = false;
    for (const StreamCfg &c : std::as_const(streams_)) {
        anyAudio = anyAudio || c.audio;
        anyIq    = anyIq || c.iq;
    }
    if (engine_) {
        engine_->setTciAudioStreaming(anyAudio);
        engine_->setTciIqStreaming(anyIq);
    }
    // Drop the RX-audio accumulator when no client subscribes so
    // a re-subscribe starts from a clean buffer instead of leaking
    // stale samples (worst case ≈ 42 ms of pre-disconnect audio).
    if (!anyAudio) audioPending_.clear();
}

void TciServer::onTciAudioBlock(const QByteArray &monoFloat, int rateHz) {
    if (monoFloat.isEmpty()) return;
    const int frames = monoFloat.size() / int(sizeof(float));
    if (frames <= 0) return;
    const float *mono =
        reinterpret_cast<const float *>(monoFloat.constData());

    // Accumulate-and-drain at the reference's per-client packet
    // size (TCIServer.cs:5437-5513 / m_audioStreamSamples = 2048).
    // WdspEngine delivers per-DSP-block bursts (typ 256 samples /
    // ~5.3 ms at 48 kHz) — left un-packetised, clients (MSHV /
    // JTDX / etc.) see audio at 8× the configured frame cadence
    // and mis-interpret energy / FT8 symbol timing (the operator-
    // bench-confirmed hot-waterfall symptom 2026-06-01).  Pack
    // into kAudioPacketSamples (= 2048) frames before emitting so
    // the wire matches what we advertise at handshake and what
    // the client's decoder expects.
    // #180 — resample the engine block (rateHz, typ 48 kHz) to the
    // client-negotiated AUDIO_SAMPLERATE before accumulating, exactly as
    // the reference does (TCIServer.cs PublishRxAudioSamples →
    // resampleRxAudioSamples → stamp targetSampleRate in the header).
    // 48 kHz clients (MSHV default) take the fast path; 12 kHz clients
    // (WSJT-X / JTDX) get a real 12 kHz stream + header instead of the
    // old hardcoded 48 kHz mismatch that left JTDX unable to use the
    // audio (#180).  inRate==outRate returns the block unchanged.
    std::vector<float> resampled;
    const float *src = mono;
    int srcN = frames;
    if (audioOutRate_ != rateHz) {
        resampled = resampleRxOut(std::vector<float>(mono, mono + frames),
                                  rateHz, audioOutRate_);
        if (resampled.empty()) return;   // resampler unavailable → drop
                                         // (better than a wrong-rate frame)
        src  = resampled.data();
        srcN = int(resampled.size());
    }
    audioPendingRate_ = audioOutRate_;
    // Task #75 — apply the operator's TCI RX-out gain ONCE at
    // ingress.  Unity (1.0) shortcuts the per-sample multiply for
    // the common default-OFF path so a 0 dB operator pays nothing.
    // Non-unity multiplies into the accumulator as samples are
    // copied in — clipping is not enforced here (3rd-party clients
    // FT8-decode in float; saturating to ±1.0 would clip-distort
    // strong signals and degrade decode SNR more than a soft float
    // overshoot would).
    const double g = rxGainLinear_;
    if (g == 1.0) {
        audioPending_.insert(audioPending_.end(), src, src + srcN);
    } else {
        const float gf = float(g);
        audioPending_.reserve(audioPending_.size() + size_t(srcN));
        for (int i = 0; i < srcN; ++i)
            audioPending_.push_back(src[i] * gf);
    }

    // Drop-oldest cap: never let the accumulator grow unbounded
    // (recomputeStreaming turns the engine tap off when no client
    // subscribes — this is belt-and-braces for the connect/
    // disconnect window).  Older samples are stale by definition;
    // discard them, keep the freshest tail.
    if (int(audioPending_.size()) > kAudioPendingCap) {
        const int drop = int(audioPending_.size()) - kAudioPendingCap;
        audioPending_.erase(audioPending_.begin(),
                            audioPending_.begin() + drop);
    }

    // Drain in a loop: a single onTciAudioBlock call might cross
    // the threshold more than once (e.g. a backlog from a paused
    // client returning).  Each iteration emits one fully-formed
    // audioOutSamples_ frame per audio-subscribed client (the
    // client-negotiated packet size, per-rate default or explicit
    // AUDIO_STREAM_SAMPLES).
    const int pkt = audioOutSamples_;
    while (int(audioPending_.size()) >= pkt) {
        const float *block = audioPending_.data();
        for (auto it = streams_.cbegin(); it != streams_.cend(); ++it) {
            if (!it.value().audio) continue;
            QWebSocket *ws = it.key();
            if (!ws || ws->state() != QAbstractSocket::ConnectedState)
                continue;
            const int chans = it.value().chans, fmt = it.value().fmt;
            QByteArray frame = streamHeader(
                0, quint32(audioPendingRate_), quint32(fmt),
                quint32(pkt * chans),
                STREAM_RX_AUDIO, quint32(chans));
            frame += audioPayload(block, pkt, fmt, chans);
            ws->sendBinaryMessage(frame);
        }
        audioPending_.erase(audioPending_.begin(),
                            audioPending_.begin() + pkt);
    }
}

void TciServer::onTciIqBlock(const QByteArray &iqFloat, int rateHz) {
    if (iqFloat.isEmpty()) return;
    const int scalars = iqFloat.size() / int(sizeof(float));   // 2×frames
    // IQ is always float32, 2 channels (I,Q interleaved) — pass through.
    QByteArray header = streamHeader(0, quint32(rateHz), FMT_FLOAT32,
                                     quint32(scalars), STREAM_IQ, 2);
    const QByteArray frame = header + iqFloat;
    for (auto it = streams_.cbegin(); it != streams_.cend(); ++it) {
        if (!it.value().iq) continue;
        QWebSocket *ws = it.key();
        if (!ws || ws->state() != QAbstractSocket::ConnectedState) continue;
        ws->sendBinaryMessage(frame);
    }
}

void TciServer::onBinaryMessage(const QByteArray &frame) {
    // Task #33: inbound TCI v2 binary frame.  Only TX_AUDIO_STREAM is
    // dispatched on the server side today; other types arrive
    // harmlessly + are ignored (the working reference returns early
    // on streamType != TX_AUDIO_STREAM at TCIServer.cs:5614).  See
    // docs/refs/mshv_tci/README.md for the architectural derivation.
    //
    // Single-active-listener gate (working reference pattern at
    // TCIServer.cs:5535-5557): only the client that owns TX-audio
    // ownership (acquired via trx:0,true,tci) may push frames into
    // the TX path.  This is also the "clearQueuedTxAudio" equivalent
    // for Lyra's no-intermediate-buffer architecture — releasing
    // ownership drops in-flight frames at the door without ever
    // reaching the decode / sanitize / push path.
    auto *ws = qobject_cast<QWebSocket *>(sender());
    // Task #33 commit 3.3 — ALWAYS-on binary-frame arrival diagnostic
    // (NOT verbose-gated).  Rate-limited to one line every ~250 ms so
    // the log doesn't drown if MSHV starts streaming continuously
    // (~20 TX_AUDIO_STREAM frames per second at 50 ms / 2400-sample
    // block cadence).  Counts frames received from the active TX-
    // audio owner vs others — helps the bench distinguish "MSHV not
    // sending anything" from "MSHV sending but Lyra not owner-
    // acquired."
    {
        static qint64 lastLogMs = 0;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (nowMs - lastLogMs > 250) {
            lastLogMs = nowMs;
            qInfo("[tci-rx-bin] frame %d bytes (owner=%s sender=%s)",
                  int(frame.size()),
                  txAudioOwner_ ? "set" : "null",
                  ws == txAudioOwner_ ? "owner" : "other");
        }
    }
    if (txAudioOwner_ == nullptr || ws != txAudioOwner_) return;
    // Note arrival of an inbound TX_AUDIO_STREAM block for the
    // CHRONO outstanding-counter timeout reset (working reference:
    // cmaster.cs:1303-1305 decrement on dequeue).
    if (chronoOutstanding_ > 0) --chronoOutstanding_;
    chronoLastInboundMs_ = QDateTime::currentMSecsSinceEpoch();

    if (frame.size() < 64) return;
    const char *p = frame.constData();
    const quint32 sampleRate = getU32(p + 4);
    const quint32 fmt        = getU32(p + 8);
    const quint32 length     = getU32(p + 20);  // scalar count requested
    const quint32 type       = getU32(p + 24);
    const quint32 headerChan = getU32(p + 28);
    if (type != STREAM_TX_AUDIO || length == 0) return;

    const int dataOffset = 64;
    const int dataBytes  = frame.size() - dataOffset;
    const int bps        = bytesPerSample(int(fmt));
    if (bps == 0 || dataBytes < bps) return;

    // Channel inference — modern (1 or 2) per the working reference's
    // header convention.  Legacy clients send channels=0 in the
    // header and rely on payload size to disambiguate (TCIServer.cs:
    // 5630-5652): if payload has 2× length scalars, it's stereo
    // interleaved; otherwise mono.  Preserve that compatibility so
    // older JTDX-style clients also work.
    int channels = int(headerChan);
    if (channels != 1 && channels != 2) {
        const int actualScalars = dataBytes / bps;
        channels = (actualScalars >= int(length) * 2) ? 2 : 1;
    }

    // Cap decode to whatever the header advertised in `length`, but
    // never read past the actual payload.  `length` is in scalars
    // (samples × channels) per the working reference's convention.
    int decodeScalars = std::min<int>(int(length), dataBytes / bps);
    if (channels == 2) decodeScalars -= decodeScalars % 2;  // pair-align
    if (decodeScalars <= 0) return;

    std::vector<float> mono = decodeTciTxAudio(p + dataOffset,
                                               decodeScalars * bps,
                                               int(fmt), channels);
    if (mono.empty()) return;

    // Match the reference (cmaster.cs:1431-1473 resampleTCITxSamples)
    // — accept any sample rate, resample to the TXA input rate
    // (kTciTxTargetRate = 48 kHz) via the WDSP polyphase
    // float-vector resampler.  Fast-path when rate matches.
    // Earlier hard-drop on non-48 kHz was a Lyra divergence that
    // would have silently broken any TCI client streaming at a
    // non-48 kHz native rate (uncommon for digital-modes clients
    // — MSHV / JTDX / WSJT-X all use 48 kHz — but a divergence
    // from the reference for zero benefit).
    if (sampleRate != kTciTxTargetRate) {
        mono = resampleTxIn(mono, int(sampleRate), kTciTxTargetRate);
        if (mono.empty()) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                qWarning("[tci] TX_AUDIO_STREAM rate %u → %d resample "
                         "failed (WDSP resampler unavailable?)",
                         unsigned(sampleRate), kTciTxTargetRate);
            }
            return;
        }
    }

    // TCI TX-audio re-home (TX_ARCHITECTURAL_MAPPING.md §4.6 data model):
    // the decoded + gained (#108) + resampled-to-TXA-rate (#68) mono goes
    // into the TciTxBridge.  The verbatim cm_main TX pump drains it through
    // the InboundTCITxAudio seam (CMaster.cpp xcmaster case 1) when the
    // operator has selected TCI as the mic source (use_tci_audio); on the
    // wire only while MOX (sendProtocol1Samples zeroes outIQbufp at !XmitBit).
    lyra::tci::TciTxBridge::instance().pushMono(mono);
}

void TciServer::onChronoTick() {
    // Task #64 — TX_CHRONO outbound pump implementing the reference
    // dynamic-pull formula (cmaster.cs:1289-1359).  Only runs when
    // MOX is ON AND we have an active TX-audio owner.  Per tick:
    // compute predictedPacketSamples (the size of one inbound TX
    // audio frame after resampling to the TXA input rate), compute
    // targetQueuedSamples (the buffer depth we want to hold based
    // on the client's bufferingMs hint), compare against the
    // current queue depth + outstanding-requests * predicted size,
    // and emit requestsNeeded CHRONO requests this tick (bounded
    // by kTciTxMaxOutstanding).
    //
    // Replaces the earlier fixed "one request every 50 ms asking
    // for 2400 samples" pump which produced mis-paced TX audio on
    // the wire — MSHV's FT8 symbol cadence ended up wrong because
    // it was delivering audio at our cadence rather than its own
    // (the operator-bench-confirmed FT8 zero-spots symptom
    // 2026-06-01).
    if (!txAudioOwner_ || !stream_ || !stream_->moxActive()) return;
    if (txAudioOwner_->state() != QAbstractSocket::ConnectedState) {
        // Owner dropped without disconnect signal yet — release now.
        txAudioOwner_ = nullptr;
        chronoTimer_->stop();
        if (stream_) stream_->requestMoxFromTci(false);
        return;
    }

    // Timeout reset (cmaster.cs:1329-1330): if no audio inbound for
    // max(kChronoTimeoutMs, bufferingMs*4) ms while we have
    // outstanding requests, assume the client died on the audio
    // side and clear the counter so we resume pumping fresh
    // requests.
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 timeoutMs = std::max<qint64>(kChronoTimeoutMs,
                                              qint64(bufferingMs_) * 4);
    if (chronoOutstanding_ > 0
        && (nowMs - chronoLastInboundMs_) > timeoutMs) {
        chronoOutstanding_ = 0;
    }

    // Pull the live client-negotiated values + Lyra constants.
    const int requestRate    = requestRate_    > 0 ? requestRate_
                                                   : kTciTxTargetRate;
    const int requestSamples = requestSamples_ > 0 ? requestSamples_
                                                   : kChronoFallbackRequestSamples;
    const int bufferingMs    = bufferingMs_    >= 50 ? bufferingMs_
                                                     : kChronoFallbackBufferingMs;
    const int targetRate     = kTciTxTargetRate;
    const int txBlock        = kTciTxBlockSamples;

    // predictedPacketSamples = the resampled size of one inbound
    // frame: ceil(requestSamples * targetRate / requestRate), with
    // a floor at txBlock so we never under-shoot the DSP block size.
    const int predictedPacketSamples = std::max(
        txBlock,
        int((qint64(requestSamples) * targetRate
             + qint64(requestRate) - 1) / std::max(1, requestRate)));

    // targetQueuedSamples = how many samples we want held in the
    // queue at all times: ceil((bufferingMs + extra) * targetRate /
    // 1000), floored at txBlock*4 so we never under-buffer.
    const int targetQueuedSamples = std::max(
        txBlock * 4,
        int((qint64(bufferingMs + kTciTxExtraBufferMs) * targetRate
             + 999) / 1000));

    // queuedSamples = current TX-mic-source inbound depth = the
    // TciTxBridge backlog the cm_main pump hasn't drained yet.  Feeds the
    // reference CHRONO dynamic-pull so the client is backpressured.
    const int queuedSamples = lyra::tci::TciTxBridge::instance().queuedSamples();

    // futureSamples = what we'll have after all outstanding requests
    // get answered with predictedPacketSamples each.
    const qint64 futureSamples =
        qint64(queuedSamples)
        + qint64(chronoOutstanding_) * qint64(predictedPacketSamples);

    // requestsNeeded = the gap, rounded up by predictedPacketSamples.
    int requestsNeeded = 0;
    if (futureSamples < targetQueuedSamples) {
        const qint64 gap = qint64(targetQueuedSamples) - futureSamples;
        requestsNeeded = int((gap + predictedPacketSamples - 1)
                             / predictedPacketSamples);
    }

    // Emit up to requestsNeeded CHRONO requests this tick, bounded
    // by kTciTxMaxOutstanding (reference TCI_TX_MAX_OUTSTANDING).
    //
    // Channels + length matches the reference's SendTxChrono
    // (TCIServer.cs:5515-5532):
    //   - channels field = negotiated requestChannels_ (1 or 2)
    //   - length = useModernLengthSemantics ? samples * channels
    //                                       : samples
    //
    // Modern flag flips true the moment a client sends
    // AUDIO_STREAM_CHANNELS or AUDIO_STREAM_SAMPLE_TYPE during
    // handshake (TCIServer.cs:5930 + 5946).  Legacy/JTDX-style
    // clients leave it false; they expect length = samples
    // scalars regardless of channels.  Both are protocol-correct.
    //
    // Earlier Lyra hardcoded channels=1 / length=samples
    // regardless of negotiation — for MSHV-class clients that
    // negotiated channels=2 this meant Lyra was asking MSHV for
    // half-sized response frames, which MSHV may interpret as a
    // "send me less" hint and reduce its TX-audio rate to match.
    const int chFld = (requestChannels_ == 2) ? 2 : 1;
    const quint32 lenFld =
        seenModernTxNeg_
            ? quint32(predictedPacketSamples * chFld)
            : quint32(predictedPacketSamples);
    while (requestsNeeded > 0
           && chronoOutstanding_ < kTciTxMaxOutstanding) {
        QByteArray hdr = streamHeader(0,
                                      /*rate=*/quint32(targetRate),
                                      /*fmt=*/FMT_FLOAT32,
                                      /*length=*/lenFld,
                                      /*type=*/STREAM_TX_CHRONO,
                                      /*channels=*/quint32(chFld));
        txAudioOwner_->sendBinaryMessage(hdr);
        ++chronoOutstanding_;
        chronoLastInboundMs_ = nowMs;
        --requestsNeeded;
    }
}

void TciServer::onMoxActiveChanged(bool on) {
    // #172 — announce the wire TX state to EVERY connected client on
    // each MOX edge, regardless of source (operator MOX button, hardware
    // PTT / foot switch, TUN, OR a TCI client).  Without this a logger
    // watching Lyra over TCI (SDRLogger+, Log4OM, …) never learns the
    // rig keyed when the operator presses MOX locally — it only ever saw
    // the direct trx: reply to a TCI-originated key.  Mirrors the working
    // reference's SyncTciPttToMox (TCIServer.cs:5560-5577), which fires
    // trx:<rx>,<state> to all listeners from the MOX-change emit sites.
    // broadcastNow (NOT the rate-limited broadcast): a key edge is a
    // discrete event that must never be coalesced or dropped.  Idempotent
    // for a TCI owner that already got its direct trx: reply — it just
    // re-confirms the now-live wire state.
    // Do NOT broadcast tx_enable on the MOX edge.  The Expert TCI spec defines
    // TX_ENABLE as TX *permission* per band — "Sent to the client when connected
    // and when the band is changed" (Unidirectional control), NOT a per-key TX-
    // state signal.  Thetis sends it on every MOX edge anyway; MSHV follows the
    // spec and reads each mid-cycle tx_enable:0,false as "TX no longer permitted",
    // which wedged it in TX after its own TCI TX cycle (deaf until MSHV/Lyra
    // restart; WSJT-X reacts looser → a return-to-RX lag).  trx alone is the
    // RX/TX switch every client needs on the edge; tx_enable stays off it.
    // Spec- AND operator-verified 2026-07-18 (MSHV vs WSJT-X).
    broadcastNow(QStringLiteral("trx:0,%1").arg(
        on ? QStringLiteral("true") : QStringLiteral("false")));

    // TCI-tune mirror: if this key edge is a TCI `tune:` command, emit the
    // tune: confirmation HERE, on the real wire edge — Thetis emits tune: from
    // its TuneChange event on the edge (TCIServer.cs:1096-1099), never a pre-
    // edge reply.  Clear the flag on the falling edge.
    if (tciTuneActive_) {
        broadcastNow(QStringLiteral("tune:0,%1").arg(
            on ? QStringLiteral("true") : QStringLiteral("false")));
        if (!on) tciTuneActive_ = false;
    }

    // Task #33 — SyncTciPttToMox (working reference at TCIServer.cs:
    // 5560-5577, called from the MOX-change emit sites at :6884/6899).
    // If the wire MOX bit dropped externally AND we owned the TX-audio
    // path AND that PTT came from us, release ownership cleanly —
    // stops the CHRONO pump + notifies the client.  No-op on MOX rise
    // (we don't grab ownership on a non-TCI keydown).
    if (on) return;
    if (!txAudioOwner_) return;
    if (!stream_) return;
    // Only release if the source that just released was Tci — an
    // operator-MOX or HW-PTT keydown that overlapped with a TCI
    // session should not auto-release the TCI listener.  In normal
    // operation those overlap windows are vanishingly rare (TCI
    // sessions are pure FT8 cycles with no operator-keying expected),
    // but the check is correctness for the corner case.
    if (stream_->pttSource() != lyra::ipc::HL2Stream::PttSource::Tci)
        return;
    QWebSocket *was = txAudioOwner_;
    txAudioOwner_ = nullptr;
    chronoOutstanding_ = 0;
    chronoTimer_->stop();
    emit statusMessage(QStringLiteral(
        "TCI: TX-audio ownership released (wire MOX dropped externally)"));
    // Do NOT send an extra trx:0,false to the released owner here — the
    // onMoxActiveChanged broadcast above already emitted trx:0,false to EVERY
    // client including this owner.  Thetis's SyncTciPttToMox tears down ownership
    // WITHOUT emitting trx (TCIServer.cs:5560-5577): exactly one trx per edge,
    // from the wire edge only.
    (void)was;
}

void TciServer::onMaintenanceTick() {
    // Ping live clients (forces a write so a dead peer surfaces), then
    // prune anything no longer connected.
    for (QWebSocket *ws : std::as_const(clients_))
        if (ws && ws->state() == QAbstractSocket::ConnectedState) ws->ping();
    pruneDeadClients();
}

QString TciServer::modulationsList() const {
    // Order + entries match Thetis (TCIServer.cs:2544, uppercased):
    // am,sam,dsb,lsb,usb,nfm,fm,digl,digu,cwl,cwu + the "cw" alias when the pref
    // is on.  (NFM is advertised too — a client's NFM maps to Lyra's FM.)
    QString list = QStringLiteral("AM,SAM,DSB,LSB,USB,NFM,FM,DIGL,DIGU,CWL,CWU");
    if (cwluBecomesCw_) list += QStringLiteral(",CW");   // legacy-client alias
    return list;
}

void TciServer::sendInit(QWebSocket *ws) {
    const int rate = engine_ ? engine_->sampleRate() : 192000;
    const qint64 half = rate / 2;
    const QString mode = prefs_ ? toTciMode(prefs_->mode()) : QStringLiteral("USB");
    const qint64 hz = stream_ ? qint64(stream_->rx1FreqHz()) : 7074000;
    const bool run = stream_ && stream_->isRunning();

    const QString proto = emulateExpertSdr3_ ? QStringLiteral("ExpertSDR3")
                                             : QStringLiteral("Lyra");
    const QString dev = emulateSunSdr2_ ? QStringLiteral("SunSDR2PRO")
                                        : QStringLiteral("HermesLite2");

    sendTo(ws, QStringLiteral("protocol:%1,2.0").arg(proto));
    sendTo(ws, QStringLiteral("device:%1").arg(dev));
    sendTo(ws, QStringLiteral("receive_only:false"));
    sendTo(ws, QStringLiteral("trx_count:1"));
    sendTo(ws, QStringLiteral("channels_count:1"));   // RX1 only (no RX2 yet) — reference sendInit spelling
    sendTo(ws, QStringLiteral("vfo_limits:%1,%2").arg(kVfoLo).arg(kVfoHi));
    sendTo(ws, QStringLiteral("if_limits:%1,%2").arg(-half).arg(half));
    sendTo(ws, QStringLiteral("modulations_list:") + modulationsList());
    // Audio-stream parameter advertisements — the reference sends
    // all five at connect (TCIServer.cs:2493-2496 plus
    // sendTxStreamAudioBuffering on TCI v2 handshake) so the
    // client knows the wire format up front instead of having to
    // query / infer from inspected binary-frame headers.  MSHV in
    // particular pre-configures its decoder from these values; a
    // mismatch with what Lyra actually emits results in mis-paced
    // audio interpretation (the operator-bench-confirmed hot-
    // waterfall symptom 2026-06-01).  Values match Lyra's actual
    // emit shape: 48 kHz float32 stereo (StreamCfg defaults
    // chans=2, fmt=3) at the matched packet cadence kAudio
    // PacketSamples = 2048 enforced by onTciAudioBlock.  TX-side
    // buffering hint matches MSHV's ms_settings default 50 ms.
    sendTo(ws, QStringLiteral("audio_samplerate:%1").arg(audioOutRate_));
    sendTo(ws, QStringLiteral("audio_stream_sample_type:float32"));
    sendTo(ws, QStringLiteral("audio_stream_channels:2"));
    sendTo(ws, QStringLiteral("audio_stream_samples:%1")
                   .arg(audioOutSamples_));
    sendTo(ws, QStringLiteral("tx_stream_audio_buffering:50"));
    sendTo(ws, QStringLiteral("ready"));

    // Lyra ↔ SDRLogger+ Combo link: tell a freshly-connected client whether
    // Combo is on, so its "Lyra Combo: Linked" indicator is correct from the
    // first frame (independent of send_initial_state, which gates radio state).
    if (comboEnabled_) sendTo(ws, QStringLiteral("lyra_combo:on"));

    // Frequency block — gated by send-initial-state, exactly like Thetis's
    // `bSend` (SendInitialFrequencyStateOnConnect) which gates ONLY the
    // dds/if/vfo/tx_frequency seed (TCIServer.cs:2368-2382).  Everything AFTER
    // this block is seeded unconditionally, below.
    const qint64 marker = engine_ ? engine_->markerOffsetHz() : 0;
    if (sendInitialState_) {
        const qint64 carrier = hz + marker;
        sendTo(ws, QStringLiteral("dds:0,%1").arg(hz));        // DDS centre
        sendTo(ws, QStringLiteral("vfo:0,0,%1").arg(carrier)); // operating carrier (VFO A)
        const qint64 vfobCarrier =
            (stream_ ? qint64(stream_->vfoBHz()) : hz) + marker;
        sendTo(ws, QStringLiteral("vfo:0,1,%1").arg(vfobCarrier));  // VFO B
        sendTo(ws, txFrequencyLine());                         // logged QSO freq (LogHX3 &c.)
        sendTo(ws, txFrequencyThetisLine());
    }
    // Mode / filter / volume / mute / split / tx_enable / MOX / run-state are
    // ALWAYS seeded — Thetis sends these OUTSIDE the bSend gate (TCIServer.cs:
    // 2385-2507) so a client relying on the connect seed for mode/run isn't
    // left blind when send-initial-state is off.
    sendTo(ws, QStringLiteral("modulation:0,%1").arg(mode));
    if (engine_) {
        sendTo(ws, QStringLiteral("rx_filter_band:0,%1,%2")
                       .arg(qRound(engine_->passbandLowHz()))
                       .arg(qRound(engine_->passbandHighHz())));
        {   // Thetis sendVolume: one-decimal (F1), skip out-of-[-60,0].
            const double v = engine_->volumeDb();
            if (v >= -60.0 && v <= 0.0)
                sendTo(ws, QStringLiteral("volume:%1").arg(QString::number(v, 'f', 1)));
        }
        sendTo(ws, QStringLiteral("mute:%1")
                       .arg(engine_->muted() ? QStringLiteral("true")
                                             : QStringLiteral("false")));
    }
    const bool keyed = stream_ && stream_->moxActive();
    sendTo(ws, QStringLiteral("split_enable:0,%1")               // Thetis sendSplit(0,VFOSplit)
                   .arg((stream_ && stream_->splitEnabled())
                            ? QStringLiteral("true") : QStringLiteral("false")));
    sendTo(ws, QStringLiteral("tx_enable:0,%1")                  // Thetis sendTXEnable(0,!MOX)
                   .arg(keyed ? QStringLiteral("false") : QStringLiteral("true")));
    sendTo(ws, QStringLiteral("trx:0,%1")                        // Thetis sendMOX(0,MOX)
                   .arg(keyed ? QStringLiteral("true") : QStringLiteral("false")));
    // Remaining unconditional seeds Thetis sends (TCIServer.cs:2391/2486/2489/
    // 2492): RX-enable (= !MOX), tune state, IQ stream stopped, IQ sample rate.
    sendTo(ws, QStringLiteral("rx_enable:0,%1")                  // sendRXEnable(0,!MOX)
                   .arg(keyed ? QStringLiteral("false") : QStringLiteral("true")));
    sendTo(ws, QStringLiteral("tune:0,false"));                 // sendTune(0,TUN) — not tuning at connect
    sendTo(ws, QStringLiteral("iq_stop:0"));                    // sendIQStartStop(0,false)
    sendTo(ws, QStringLiteral("iq_samplerate:%1")               // sendIQSampleRate (clamp 48k..384k)
                   .arg(std::clamp(rate, 48000, 384000)));
    sendTo(ws, run ? QStringLiteral("start") : QStringLiteral("stop"));
}

void TciServer::sendTo(QWebSocket *ws, const QString &line) {
    if (ws) ws->sendTextMessage(line + QLatin1Char(';'));
}
void TciServer::broadcastNow(const QString &line) {
    const QString msg = line + QLatin1Char(';');
    for (QWebSocket *ws : std::as_const(clients_)) ws->sendTextMessage(msg);
}
void TciServer::broadcast(const QString &key, const QString &line) {
    if (clients_.isEmpty()) return;
    const qint64 now = rateClock_.elapsed();
    const qint64 last = lastSent_.value(key, -1000000);
    if (now - last < rateLimitMs_) {
        // Inside the window: stash the LATEST line and arm the trailing-edge
        // flush so this value is delivered when the window elapses — never
        // silently dropped (Thetis schedules a per-type trailing timer).
        pendingLine_[key] = line;
        if (rlFlushTimer_ && !rlFlushTimer_->isActive())
            rlFlushTimer_->start(int(rateLimitMs_ - (now - last)) + 1);
        return;
    }
    lastSent_[key] = now;
    pendingLine_.remove(key);   // this send supersedes any queued trailing value
    broadcastNow(line);
}

void TciServer::onRlFlush() {
    if (pendingLine_.isEmpty()) return;
    const qint64 now = rateClock_.elapsed();
    qint64 soonest = -1;   // earliest ms-from-now a not-yet-ready key comes due
    for (auto it = pendingLine_.begin(); it != pendingLine_.end(); ) {
        const qint64 last = lastSent_.value(it.key(), -1000000);
        if (now - last >= rateLimitMs_) {
            lastSent_[it.key()] = now;
            broadcastNow(it.value());
            it = pendingLine_.erase(it);
        } else {
            const qint64 due = rateLimitMs_ - (now - last);
            if (soonest < 0 || due < soonest) soonest = due;
            ++it;
        }
    }
    if (!pendingLine_.isEmpty() && rlFlushTimer_)
        rlFlushTimer_->start(int(soonest > 0 ? soonest : rateLimitMs_) + 1);
}

QString TciServer::softGet(const QString &key, const QString &def) const {
    return soft_.value(key, def);
}
void TciServer::softSet(const QString &key, const QString &val) {
    soft_[key] = val;
}

int TciServer::parseChannel(const QString &s, bool *ok) {
    const int ch = s.trimmed().toInt(ok);
    if (*ok && (ch == 0 || ch == 1)) return ch;
    *ok = false;
    return -1;
}
bool TciServer::parseBool(const QString &s) {
    // Thetis uses bool.TryParse throughout — accepts only "true"/"false"
    // (case-insensitive), NOT "1"/"0".  Match that (was also treating "1" as
    // true).  Note: a non-"true" token yields false, as before.
    return s.trimmed().compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}

QString TciServer::toTciMode(const QString &m) const {
    // Report the REAL CW sideband (CWL/CWU) so a CW TCI client knows which
    // sideband Lyra is on — collapse to "CW" ONLY when the compatibility pref
    // is set, exactly as Thetis sendMode: `if (CWLUbecomesCW && (CWL||CWU))
    // sMode="cw"` else report the actual mode.  Was unconditionally collapsing.
    if (m == QStringLiteral("CWL") || m == QStringLiteral("CWU"))
        return cwluBecomesCw_ ? QStringLiteral("CW") : m;
    // FM reported as FM, SAM as SAM — both advertised in modulations_list, so
    // report the real mode (SAM was being collapsed to AM, inconsistent with
    // the advertised list).  USB/LSB/DIGU/DIGL/AM/DSB pass through unchanged.
    return m;
}
QString TciServer::fromTciMode(const QString &t, qint64 freqHz) const {
    const QString u = t.trimmed().toUpper();
    if (u == QStringLiteral("CW"))
        return freqHz >= 10000000 ? QStringLiteral("CWU") : QStringLiteral("CWL");
    if (u == QStringLiteral("NFM") || u == QStringLiteral("WFM"))
        return QStringLiteral("FM");
    // Validate against Lyra's mode set — an UNKNOWN token returns "" so the
    // caller skips the set, exactly as Thetis (unrecognized → DSPMode.FIRST →
    // `if (mode != FIRST)` guard skips it, TCIServer.cs:3909-3913).  Was passing
    // arbitrary strings straight to setMode.
    const bool known =
        u == QStringLiteral("USB")  || u == QStringLiteral("LSB")
     || u == QStringLiteral("CWU")  || u == QStringLiteral("CWL")
     || u == QStringLiteral("AM")   || u == QStringLiteral("SAM")
     || u == QStringLiteral("DSB")  || u == QStringLiteral("FM")
     || u == QStringLiteral("DIGU") || u == QStringLiteral("DIGL");
    return known ? u : QString();
}

void TciServer::onTextMessage(const QString &raw) {
    auto *ws = qobject_cast<QWebSocket *>(sender());
    QString msg = raw.trimmed();
    while (msg.endsWith(QLatin1Char(';'))) msg.chop(1);
    msg = msg.trimmed();
    if (msg.isEmpty()) return;
    // Task #33 commit 3.3 — ALWAYS-on dump of every inbound TCI text
    // command (NOT verbose-gated).  Operator-bench-feedback 2026-06-01:
    // the bench needs to know exactly what MSHV is emitting on TX TUNE
    // / FT8 cycle attempts WITHOUT the operator having to remember to
    // flip the verbose toggle first.  TCI text traffic is sparse (a
    // few commands per second at most during a TX cycle), so the
    // always-on cost is negligible and the diagnostic value is high.
    // Once the first-light bench is closed and the audio path is
    // working, this can move under the verbose gate.
    qInfo("[tci-rx-text] %s", qPrintable(msg));
    QString cmd;
    QStringList args;
    const int colon = msg.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        cmd = msg.left(colon).trimmed().toUpper();
        const QStringList parts = msg.mid(colon + 1).split(QLatin1Char(','));
        for (const QString &p : parts) args << p.trimmed();
    } else {
        cmd = msg.toUpper();
    }
    dispatch(ws, cmd, args);
}

void TciServer::dispatch(QWebSocket *ws, const QString &cmd,
                         const QStringList &args) {
    const qint64 curHz = stream_ ? qint64(stream_->rx1FreqHz()) : 0;

    // ── lifecycle ────────────────────────────────────────────────
    if (cmd == QStringLiteral("START")) { emit startRequested(); return; }
    if (cmd == QStringLiteral("STOP"))  { emit stopRequested();  return; }
    if (cmd == QStringLiteral("SET_IN_FOCUS")) return;   // no-op

    // ── Lyra ↔ SDRLogger+ Combo link ─────────────────────────────
    // Inbound from a linked SDRLogger+: the callbook name-back (and, later,
    // reverse call push).  Apply to the CW Console contact row under the echo
    // guard so it doesn't bounce straight back out.  Lyra is the master for
    // the on/off state, so an inbound lyra_combo is ignored.
    if (cmd == QStringLiteral("LYRA_COMBO")) return;
    if (cmd == QStringLiteral("LYRA_CONTACT")) {
        // args: <src>,<call>,<rstSent>,<rstRcvd>,<name>,<qth>,<grid>,<serial>
        if (!comboEnabled_ || !cwMacros_ || args.size() < 2) return;
        if (args[0].compare(QStringLiteral("sdrlog"), Qt::CaseInsensitive) != 0) return;
        comboApplyingRemote_ = true;
        const QString call = args.value(1).trimmed().toUpper();
        if (!call.isEmpty()) cwMacros_->setHisCall(call);
        const QString name = args.value(4).trimmed();
        if (!name.isEmpty()) cwMacros_->setOpName(name);   // → {NAME} token
        comboApplyingRemote_ = false;
        return;
    }

    // ── frequency: VFO / DDS / IF ────────────────────────────────
    if (cmd == QStringLiteral("DDS")) {
        bool okc = false; const int ch = args.size() >= 1 ? parseChannel(args[0], &okc) : -1;
        if (!okc) return;
        if (args.size() >= 2) {
            if (ch == 0 && stream_) { bool f=false; qint64 v=args[1].toLongLong(&f);
                if (f) stream_->setRx1FreqHz(quint32(v)); }
        } else if (ch == 0) sendTo(ws, QStringLiteral("dds:0,%1").arg(curHz));
        return;
    }
    if (cmd == QStringLiteral("VFO")) {
        // VFO = operating (carrier) freq.  In CW the carrier sits pitch Hz
        // off the tuned DDS centre, so convert carrier↔DDS both ways
        // (markerOffsetHz = VFO−DDS; 0 outside CW).  Channel 0 = VFO A (RX +
        // simplex TX); channel 1 = VFO B (the SPLIT TX freq).  A digital-mode
        // client running rig/hardware split (WSJT-X et al.) sets its TX freq on
        // VFO B — honour it via setVfoBHz so split TX actually follows the
        // client (was ch-0-only, silently dropping VFO B).
        const qint64 off = engine_ ? engine_->markerOffsetHz() : 0;
        bool okc = false; const int ch = args.size() >= 2 ? parseChannel(args[1], &okc) : -1;
        if (!okc) return;
        if (args.size() >= 3) {
            bool f=false; const qint64 v = args[2].toLongLong(&f);
            if (f && stream_) {
                // As Thetis (handleVFOMessage): chan 0 → VFO A, chan 1 → VFO B.
                // Setting VFO B ONLY sets the freq — it does NOT touch split.
                // Split is the client's explicit split_enable command (below),
                // never derived from where VFO B sits.
                if (ch == 0)      stream_->setRx1FreqHz(quint32(v - off));  // carrier → DDS (VFO A)
                else if (ch == 1) stream_->setVfoBHz(quint32(v - off));     // carrier → DDS (VFO B)
            }
        } else if (ch == 0) {
            sendTo(ws, QStringLiteral("vfo:0,0,%1").arg(curHz + off));      // DDS → carrier (VFO A)
        } else if (ch == 1 && stream_) {
            sendTo(ws, QStringLiteral("vfo:0,1,%1").arg(stream_->vfoBHz() + off));  // VFO B read-back
        }
        return;
    }
    if (cmd == QStringLiteral("IF")) {
        // As Thetis (handleIF): IF is the ABSOLUTE offset of the VFO from the
        // panorama CENTRE (vfo = CentreFrequency + offset), NOT cumulative on
        // the live VFO.  Lyra's centre = the locked DDC centre under CTUN, else
        // it tracks the VFO (centre == VFO) — exactly like Thetis when centre-
        // lock is off.  chan 0 → VFO A, chan 1 → VFO B.  rx index 1 (RX2) not
        // wired yet.  Centre/VFO are DDS-domain here; the CW marker offset
        // applies equally to both so it cancels in the SET carrier math.
        if (args.size() < 2 || !stream_) return;
        bool okr=false; const int rxi = args[0].trimmed().toInt(&okr);
        bool okc=false; const int ch  = parseChannel(args[1], &okc);
        if (!okr || !okc || rxi != 0) return;
        const qint64 centre = stream_->ctuneEnabled()
                                  ? qint64(stream_->ctuneCenterHz())
                                  : qint64(stream_->rx1FreqHz());
        if (args.size() >= 3) {                        // SET — vfo = centre + off
            bool oko=false; const qint64 off = args[2].toLongLong(&oko);
            if (!oko) return;
            const qint64 target = centre + off;
            if (target <= 0) return;
            if (ch == 0)      stream_->setRx1FreqHz(quint32(target));
            else if (ch == 1) stream_->setVfoBHz(quint32(target));
        } else {                                       // GET — offset = vfo − centre
            // Mirror of Thetis sendIF: reply = (VFO − Centre) − cwPitchShift.
            // Lyra's cwPitchShift equivalent is −markerOffsetHz (markerOffset =
            // VFO−DDS = +pitch for CWU / −pitch for CWL; GetDSPcwPitchShiftToZero
            // = −pitch for CWU / +pitch for CWL), so −cwPitchShift = +markerOffset.
            // 0 outside CW → identical in every non-CW mode.
            const qint64 vfo = (ch == 1) ? qint64(stream_->vfoBHz())
                                         : qint64(stream_->rx1FreqHz());
            const qint64 mk  = engine_ ? engine_->markerOffsetHz() : 0;
            sendTo(ws, QStringLiteral("if:0,%1,%2").arg(ch).arg((vfo - centre) + mk));
        }
        return;
    }

    // ── mode + filter ────────────────────────────────────────────
    if (cmd == QStringLiteral("MODULATION")) {
        bool okc = false; const int ch = args.size() >= 1 ? parseChannel(args[0], &okc) : -1;
        if (!okc) return;
        if (args.size() >= 2) {
            // Skip the set on an UNKNOWN mode token (fromTciMode returns "") —
            // as Thetis ignores an unrecognized modulation instead of applying it.
            const QString lm = fromTciMode(args[1], curHz);
            if (ch == 0 && prefs_ && !lm.isEmpty()) prefs_->setMode(lm);
        } else if (ch == 0) {
            const QString m = prefs_ ? toTciMode(prefs_->mode()) : QStringLiteral("USB");
            sendTo(ws, QStringLiteral("modulation:0,%1").arg(m));
        }
        return;
    }
    if (cmd == QStringLiteral("RX_FILTER_BAND")) {
        bool okc = false; const int ch = args.size() >= 1 ? parseChannel(args[0], &okc) : -1;
        if (!okc) return;
        if (args.size() >= 3 && engine_) {
            bool a=false,b=false; const qint64 lo=args[1].toLongLong(&a);
            const qint64 hi=args[2].toLongLong(&b);
            if (a && b && ch == 0) engine_->setBandwidth(int(qAbs(hi - lo)));
        } else if (ch == 0 && engine_) {
            sendTo(ws, QStringLiteral("rx_filter_band:0,%1,%2")
                           .arg(qRound(engine_->passbandLowHz()))
                           .arg(qRound(engine_->passbandHighHz())));
        }
        return;
    }

    // ── audio: VOLUME / MUTE / RX_VOLUME / RX_MUTE ───────────────
    if (cmd == QStringLiteral("VOLUME")) {
        if (!args.isEmpty() && engine_) { bool f=false; double db=args[0].toDouble(&f);
            if (f) engine_->setVolume(dbToLinear(db)); }
        else if (engine_) sendTo(ws, QStringLiteral("volume:%1").arg(qRound(engine_->volumeDb())));
        return;
    }
    if (cmd == QStringLiteral("MUTE")) {
        if (!args.isEmpty() && engine_) engine_->setMuted(parseBool(args[0]));
        else if (engine_) sendTo(ws, QStringLiteral("mute:%1")
                       .arg(engine_->muted() ? QStringLiteral("true") : QStringLiteral("false")));
        return;
    }
    if (cmd == QStringLiteral("RX_VOLUME")) {
        bool okc=false; const int ch = args.size()>=2 ? parseChannel(args[1],&okc) : -1;
        if (!okc) return;
        if (args.size() >= 3 && engine_) { bool f=false; double db=args[2].toDouble(&f);
            if (f && ch==0) engine_->setVolume(dbToLinear(db)); }
        else if (ch==0 && engine_)
            sendTo(ws, QStringLiteral("rx_volume:0,0,%1").arg(qRound(engine_->volumeDb())));
        return;
    }
    if (cmd == QStringLiteral("RX_MUTE")) {
        bool okc=false; const int ch = args.size()>=1 ? parseChannel(args[0],&okc) : -1;
        if (!okc) return;
        if (args.size() >= 2 && engine_) { if (ch==0) engine_->setMuted(parseBool(args[1])); }
        else if (ch==0 && engine_)
            sendTo(ws, QStringLiteral("rx_mute:0,%1")
                       .arg(engine_->muted() ? QStringLiteral("true") : QStringLiteral("false")));
        return;
    }
    if (cmd == QStringLiteral("CW_PITCH")) {
        if (!args.isEmpty() && engine_) {
            // CW_PITCH:rx[,hz] — last arg is the pitch.
            bool f=false; const int hz = args.last().toInt(&f);
            if (f && args.size() >= 2) engine_->setCwPitchHz(hz);
            else if (args.size() == 1) {
                bool g=false; const int p = args[0].toInt(&g);
                if (g) engine_->setCwPitchHz(p);
            }
        }
        if (args.size() < 2 && engine_)
            sendTo(ws, QStringLiteral("cw_pitch:0,%1").arg(engine_->cwPitchHz()));
        return;
    }
    // #105 CW-4 — host CW keying over TCI.  Verified against the EESDR TCI
    // protocol spec (docs/TCI Protocol.pdf §3.2) + Thetis TCIServer.cs +
    // SDRLogger+ (hamlog/main.py).  Routes into the CW-3 host keyer; no-op
    // outside CW mode (HL2Stream::sendCw gates on txMode).  sendCw/abortCw
    // are marshalled to the stream thread (both Q_INVOKABLE) so the keyer
    // is only ever touched there — no race vs the QML console's sendCw.
    //
    // Reserved-char un-escape (spec §3.2.1): clients replace : , ; in macro
    // text with ^ ~ * so they don't break protocol framing — convert back.
    auto cwUnescape = [](QString s) {
        return s.replace(QLatin1Char('^'), QLatin1Char(':'))
                .replace(QLatin1Char('~'), QLatin1Char(','))
                .replace(QLatin1Char('*'), QLatin1Char(';'));
    };
    if (cmd == QStringLiteral("CW_MACROS_SPEED") ||
        cmd == QStringLiteral("CW_KEYER_SPEED")) {
        if (stream_ && !args.isEmpty()) {                 // Set
            bool ok = false; const int wpm = args.last().toInt(&ok);
            if (ok) stream_->setCwKeyerSpeedWpm(wpm);
        } else if (stream_) {                             // Read -> Reply (bidirectional)
            sendTo(ws, QStringLiteral("cw_macros_speed:%1")
                          .arg(stream_->cwKeyerSpeedWpm()));
        }
        return;
    }
    if (cmd == QStringLiteral("CW_MACROS_STOP")) {         // immediate stop (spec §3.2.2)
        if (stream_)
            QMetaObject::invokeMethod(stream_, "abortCw", Qt::QueuedConnection);
        return;
    }
    if (cmd == QStringLiteral("CW_MACROS")) {
        // cw_macros:<tcvr>,<text> — arg[0] = transceiver index (ignored).
        // Raw commas in text are escaped to ~, so the text is a single arg;
        // the join+unescape is robust regardless.  Inline | | prosign-combine
        // and < > speed-step are macro-text niceties (follow-on); plain text
        // keys correctly today (CwMorse skips unknown glyphs like | < >).
        if (stream_ && args.size() >= 2) {
            const QString text = cwUnescape(args.mid(1).join(QLatin1Char(',')));
            if (!text.isEmpty())
                QMetaObject::invokeMethod(stream_, "sendCw",
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, text));
        }
        return;
    }
    if (cmd == QStringLiteral("CW_MSG")) {
        // cw_msg:<tcvr>,<prefix>,<callsign>,<suffix> (spec §3.2.2) — the
        // contest exchange; "$N" after the callsign repeats it N times.
        // The 1-arg callsign-edit form (cw_msg:<tcvr>;) + the CW_MSG-over-
        // CW_MACROS interrupt/priority semantics are a follow-on (this cut
        // assembles + sends the full message).
        if (stream_ && args.size() >= 4) {
            QString call = args[2];
            int rep = 1;
            const int dollar = call.indexOf(QLatin1Char('$'));
            if (dollar >= 0) {
                bool ok = false; const int n = call.mid(dollar + 1).toInt(&ok);
                if (ok && n > 0) rep = n;
                call = call.left(dollar);
            }
            QStringList parts;
            if (!args[1].isEmpty()) parts << cwUnescape(args[1]);      // prefix
            for (int i = 0; i < rep; ++i) parts << cwUnescape(call);   // callsign ×N
            if (!args[3].isEmpty()) parts << cwUnescape(args[3]);      // suffix
            const QString text = parts.join(QLatin1Char(' '));
            if (!text.isEmpty())
                QMetaObject::invokeMethod(stream_, "sendCw",
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, text));
        }
        return;
    }

    // ── sensors / S-meter ────────────────────────────────────────
    if (cmd == QStringLiteral("RX_SENSORS_ENABLE")) {
        sensorsEnabled_ = !args.isEmpty() && parseBool(args[0]);
        if (sensorsEnabled_ && !clients_.isEmpty()) smeterTimer_->start();
        else smeterTimer_->stop();
        return;
    }
    if (cmd == QStringLiteral("TX_SENSORS_ENABLE")) return;  // RX-only

    // ── Task #33 commit 3.4: TCI handshake-config commands MUST be
    //    echoed back so the client knows the SET took effect.  MSHV
    //    bench-discovered 2026-06-01: without `tx_enable:0,true`
    //    echo, MSHV considers TX-on-channel-0 not permitted and
    //    refuses to fire `trx:` for any operator press.  Working
    //    reference TCIServer.cs:5028-5050 has dedicated handlers for
    //    each of these — all echo the SET back to the client.
    //
    //    These commands are CONFIG (one-shot at handshake, sometimes
    //    re-queried later) — they don't trigger MOX or any wire
    //    activity.  Accept + echo + store the soft value.
    if (cmd == QStringLiteral("TX_ENABLE")
        || cmd == QStringLiteral("RX_ENABLE")) {
        bool okc = false;
        const int ch = args.size() >= 1 ? parseChannel(args[0], &okc) : -1;
        if (!okc || ch != 0) return;
        const bool on = args.size() >= 2 ? parseBool(args[1])
                                         : true;   // 1-arg query → default true
        softSet(cmd, on ? QStringLiteral("true") : QStringLiteral("false"));
        sendTo(ws, cmd.toLower()
            + QStringLiteral(":0,%1").arg(on ? QStringLiteral("true")
                                             : QStringLiteral("false")));
        return;
    }
    // Audio-stream config — echo back the operator-set value so MSHV
    // confirms the streaming parameters took effect AND capture the
    // value into the live state the CHRONO formula consumes (matches
    // the reference's TCIServer.cs:5019-5050 + the consuming side at
    // cmaster.cs:1289-1359 which reads requestRate / requestSamples
    // / bufferingMs every tick).  Without this, the formula sat on
    // stale defaults and Lyra's CHRONO pump asked MSHV for a wrong
    // number of TX-audio samples per tick.
    if (cmd == QStringLiteral("AUDIO_SAMPLERATE")
        || cmd == QStringLiteral("AUDIO_STREAM_SAMPLES")
        || cmd == QStringLiteral("AUDIO_STREAM_CHANNELS")
        || cmd == QStringLiteral("AUDIO_STREAM_SAMPLE_TYPE")
        || cmd == QStringLiteral("TX_STREAM_AUDIO_BUFFERING")) {
        const QString val = args.isEmpty() ? QStringLiteral("0") : args[0];
        bool okv = false;
        const int n = val.trimmed().toInt(&okv);
        bool samplesAutoChanged = false;   // #180 — re-echo audio_stream_samples
        if (okv && n > 0) {
            if (cmd == QStringLiteral("AUDIO_SAMPLERATE")) {
                requestRate_ = n;     // TX-CHRONO (unchanged)
                // #180 — RX-out: honour the client-chosen LF-audio rate
                // (8/12/24/48 kHz), mirror of the reference's
                // handleAudioSampleRate (TCIServer.cs).  On a real change,
                // recompute the per-rate default packet size (unless the
                // client pinned one) and clear the accumulator + resampler
                // (== clearRxAudioStreamState).
                if (isSupportedAudioRate(n) && n != audioOutRate_) {
                    audioOutRate_ = n;
                    if (!audioOutSamplesExplicit_) {
                        const int def = defaultAudioStreamSamples(n);
                        if (def != audioOutSamples_) {
                            audioOutSamples_ = def;
                            samplesAutoChanged = true;
                        }
                    }
                    audioPending_.clear();
                    destroyRxResampler();
                }
            } else if (cmd == QStringLiteral("AUDIO_STREAM_SAMPLES")) {
                requestSamples_ = n;  // TX-CHRONO (unchanged)
                // #180 — RX-out packet size, explicit operator choice.
                // Spec range 100..2048 (TCI Protocol AUDIO_STREAM_SAMPLES).
                const int s = std::clamp(n, 100, 2048);
                audioOutSamplesExplicit_ = true;
                if (s != audioOutSamples_) {
                    audioOutSamples_ = s;
                    audioPending_.clear();   // packet size changed
                }
            } else if (cmd == QStringLiteral("AUDIO_STREAM_CHANNELS")) {
                // Reference clamps to {1, 2} (TCIServer.cs:5942-5943).
                if (n == 1 || n == 2)
                    requestChannels_ = n;
                seenModernTxNeg_ = true;
            } else if (cmd == QStringLiteral("AUDIO_STREAM_SAMPLE_TYPE")) {
                // Sample-type negotiation also flips the modern flag
                // per TCIServer.cs:5930.  Stored value handled by the
                // softSet echo path; we just need the flag here.
                seenModernTxNeg_ = true;
            } else { // TX_STREAM_AUDIO_BUFFERING
                bufferingMs_ = std::max(n, 50);   // reference floor
            }
        }
        softSet(cmd, val);
        // Echo the APPLIED value for the RX-out params (reference echoes
        // m_audioSampleRate / m_audioStreamSamples, not the raw arg), and
        // re-send audio_stream_samples when a rate change auto-shifted it.
        if (cmd == QStringLiteral("AUDIO_SAMPLERATE")) {
            sendTo(ws, QStringLiteral("audio_samplerate:%1").arg(audioOutRate_));
            if (samplesAutoChanged)
                sendTo(ws, QStringLiteral("audio_stream_samples:%1")
                               .arg(audioOutSamples_));
        } else if (cmd == QStringLiteral("AUDIO_STREAM_SAMPLES")) {
            sendTo(ws, QStringLiteral("audio_stream_samples:%1")
                           .arg(audioOutSamples_));
        } else {
            sendTo(ws, cmd.toLower() + QStringLiteral(":%1").arg(val));
        }
        return;
    }
    // TUNE — TCI v1.9/v2 separate TX path for tune-carrier (operator
    // hits a "TUNE" button in the client).  Working reference's
    // handleTune (TCIServer.cs:4343-4364) sets a TUN flag distinct
    // from MOX, but Lyra doesn't yet have a separate tune-carrier
    // mode — treat tune like trx for MOX engagement so MSHV's TUNE
    // button keys the rig.  Future v0.2.x: wire a real tune-carrier
    // generator (Task #50/#52 chain) for proper TUN behaviour.
    if (cmd == QStringLiteral("TUNE")) {
        bool okc = false;
        const int ch = args.size() >= 1 ? parseChannel(args[0], &okc) : -1;
        if (!okc || ch != 0 || !stream_) {
            sendTo(ws, QStringLiteral("tune:0,false"));
            return;
        }
        if (args.size() < 2) {
            // 1-arg query — report current state (we don't track a
            // separate TUN flag yet; report based on wire MOX).
            sendTo(ws, QStringLiteral("tune:0,%1").arg(
                stream_->moxActive() ? QStringLiteral("true")
                                     : QStringLiteral("false")));
            return;
        }
        const bool wantsTune = parseBool(args[1]);
        if (wantsTune) {
            // Acquire TCI audio ownership IF operator has Mic source =
            // tci (so the client's audio reaches the WDSP TXA chain;
            // matches the trx:0,true behaviour below).
            const bool wantTciAudio =
                prefs_ && prefs_->micSource() == QStringLiteral("tci");
            if (wantTciAudio) {
                if (txAudioOwner_ != nullptr && txAudioOwner_ != ws) {
                    sendTo(ws, QStringLiteral("tune:0,false"));
                    return;
                }
                if (txAudioOwner_ == nullptr) {
                    txAudioOwner_ = ws;
                    chronoOutstanding_ = 0;
                    chronoLastInboundMs_ =
                        QDateTime::currentMSecsSinceEpoch();
                    chronoTimer_->start();
                    emit statusMessage(QStringLiteral(
                        "TCI: TX-audio ownership ACQUIRED (tune)"));
                }
            }
            // Mark this as a TCI-tune key so the wire-edge onMoxActiveChanged
            // mirrors it on the tune: channel — NO pre-edge direct reply (same
            // Thetis single-on-edge-authority model as TRX; handleTune sets TUN
            // and returns, sendTune fires from TuneChange on the real edge —
            // TCIServer.cs:4343-4362 → 7040 → 1096-1099).
            tciTuneActive_ = true;
            stream_->requestMoxFromTci(true);
        } else {
            if (txAudioOwner_ == ws) {
                txAudioOwner_ = nullptr;
                chronoOutstanding_ = 0;
                chronoTimer_->stop();
                emit statusMessage(QStringLiteral(
                    "TCI: TX-audio ownership RELEASED (tune)"));
            }
            // NO pre-edge direct reply — the tune: confirmation is mirrored on
            // the real wire edge in onMoxActiveChanged (Thetis model).  Leave
            // tciTuneActive_ set; the falling edge clears it after broadcasting.
            stream_->requestMoxFromTci(false);
        }
        return;
    }

    // ── Task #33: TRX — TCI keying funnel (working reference's
    //    handleTrxMessage at TCIServer.cs:3459-3550).  Wired ONLY when
    //    the request comes with the "tci" source token (per TCI v2
    //    §3.3 TRX command).  Other forms (operator-tooling MOX requests
    //    via TRX without source, TUNE / RIT_ENABLE / XIT_ENABLE /
    //    SPLIT_ENABLE) keep the v0.2.2 acknowledge-inactive stub.
    if (cmd == QStringLiteral("TRX")) {
        bool okc = false; const int ch = args.size() >= 1 ? parseChannel(args[0], &okc) : -1;
        const bool wantsTx = args.size() >= 2 && parseBool(args[1]);
        const bool useTciAudio = args.size() >= 3
                              && args[2].compare(QStringLiteral("tci"),
                                                 Qt::CaseInsensitive) == 0;
        if (!okc || ch != 0 || !stream_) {
            const QString idx = args.isEmpty() ? QStringLiteral("0") : args[0];
            sendTo(ws, QStringLiteral("trx:%1,false").arg(idx));
            return;
        }
        // Thetis handleTRX: a 1-arg `trx:<rx>` is a QUERY — report current MOX,
        // change NOTHING (TCIServer.cs:3555-3558 → sendMOX(rx, MOX,
        // m_txUsesTCIAudio)).  sendMOX appends the `,tci` source token when the
        // active TX is using TCI audio; Lyra's equivalent is "a TCI client owns
        // the TX-audio path" (txAudioOwner_ set).  The old code let a bare
        // `trx:0` fall through to the un-key path below and drop TX.
        if (args.size() < 2) {
            const bool mox = stream_->moxActive();
            const QString tok = (mox && txAudioOwner_) ? QStringLiteral(",tci")
                                                       : QString();
            sendTo(ws, QStringLiteral("trx:0,%1%2")
                           .arg(mox ? QStringLiteral("true") : QStringLiteral("false"))
                           .arg(tok));
            return;
        }
        if (wantsTx) {
            // Thetis-faithful: ALWAYS key MOX on trx:0,true regardless
            // of source token.  Working reference TCIServer.cs:3536-3537
            // sets TCIPTT = bMox unconditionally; the source token
            // (tci / mic1 / nothing) controls ONLY audio routing.
            // Earlier commit-3 incorrectly filtered MOX on
            // useTciAudio — MSHV (and other TCI clients) send plain
            // `trx:0,true` without the source token and expect the
            // radio to key anyway.  Operator-bench-confirmed
            // 2026-06-01 ("MSHV keys Thetis").
            //
            // Acquire TCI audio ownership if EITHER:
            //   (a) source token explicitly says "tci" (auto-detected
            //       digital-modes client), OR
            //   (b) operator has Mic source = "tci" in Settings
            //       (operator opted in via the picker — covers bare
            //       trx:0,true from clients that don't include the
            //       source token).
            const bool wantTciAudio = useTciAudio
                || (prefs_ && prefs_->micSource()
                                  == QStringLiteral("tci"));
            if (wantTciAudio) {
                if (txAudioOwner_ != nullptr && txAudioOwner_ != ws) {
                    emit statusMessage(QStringLiteral(
                        "TCI: TX-audio acquire denied — another client "
                        "owns TX audio"));
                    sendTo(ws, QStringLiteral("trx:0,false"));
                    return;
                }
                if (txAudioOwner_ == nullptr) {
                    txAudioOwner_ = ws;
                    chronoOutstanding_ = 0;
                    chronoLastInboundMs_ =
                        QDateTime::currentMSecsSinceEpoch();
                    chronoTimer_->start();
                    emit statusMessage(QStringLiteral(
                        "TCI: TX-audio ownership ACQUIRED"));
                }
                // Auto-flip the operator picker ONLY on the explicit
                // ",tci" token — bare trx:0,true with operator-set
                // Mic source=tci already matches, and we don't want
                // to flip the picker on a generic TCI-client keying
                // request that didn't specifically ask for TCI audio.
                if (useTciAudio && prefs_)
                    prefs_->setMicSource(QStringLiteral("tci"));
            }
            // R-H2 — diagnostic snapshot + token-agnostic source force.
            //
            // Mechanism guarded against: bare `trx:0,true` from MSHV /
            // JTDX / WSJT-X (no `,tci` token) WITH operator's
            // Prefs::micSource != "tci" produces:
            //   wantTciAudio = false → no audio ownership → no picker
            //   flip → activeMicSource_ stays at its previous value →
            //   TXA chain runs with zero audio input → SSB modulator
            //   outputs only the suppressed-carrier residue (a single
            //   tone at the bp0 passband center, visible as one bright
            //   vertical line on the panadapter and zero PSKReporter
            //   spots because there is no FT8 content in the I/Q).
            //
            // Fix: when ANY TCI client keys via trx:0,true (token-
            // agnostic), force activeMicSource_ to Tci for the
            // keydown lifetime so TCI binary-frame samples reach the
            // TXA chain.  Operator's persistent Prefs::micSource() is
            // NOT mutated (the picker UI does not flicker / does not
            // get rewritten); only the worker's runtime atomic is
            // forced, restored on keyup or owner disconnect.
            //
            // The unconditional statusMessage records the live state
            // at every keydown so the operator can verify post-bench
            // whether the force fired (and whether the original was
            // already Tci, in which case R-H2 is a no-op and the
            // diagnosis must descend to the next reconciled
            // deviation).  See docs/THETIS_VS_LYRA_RECONCILED.md R-H2.
            {
                // TX-rip Phase 1 (Q2): R-H2 activeMicSource_ readout +
                // force-to-Tci returns with the new TX DSP worker per
                // docs/TX_ARCHITECTURAL_MAPPING.md §10.3.
                const QString pickerStr = prefs_ ? prefs_->micSource()
                                                 : QStringLiteral("(null)");
                emit statusMessage(QString(
                    "TCI: keydown — prefs.micSource=%1 useTciAudio=%2 "
                    "(TX subsystem rebuilding)")
                        .arg(pickerStr)
                        .arg(useTciAudio ? QStringLiteral("1")
                                         : QStringLiteral("0")));
            }
            // Key MOX (Thetis-faithful) — records PttSource::Tci on
            // the FSM since the request arrived via the TCI WebSocket.
            stream_->requestMoxFromTci(true);
            // NO direct/pre-edge trx reply here.  As Thetis (handleTrxMessage
            // sets TCIPTT and returns with NO reply, TCIServer.cs:3459-3559), the
            // SOLE trx confirmation to clients is the on-wire-edge broadcast from
            // onMoxActiveChanged.  requestMoxFromTci is ASYNC — replying trx:0,true
            // here confirms the key BEFORE the radio is actually keyed, so a spec-
            // following client (MSHV) starts its cycle early and then classifies the
            // real on-edge messages as mid-cycle events → wedged in TX.  One trx per
            // edge, from the true edge, only.  (2026-07-18 TCI-audit root cause.)
            return;
        }
        // wantsTx == false — release MOX + ownership if held.
        // Working reference parallel: clearQueuedTxAudio + Release
        // ActiveTxAudioListener at TCIServer.cs:3479-3486 + 5578-5585.
        // The binary handler gates on owner identity so in-flight
        // TX_AUDIO_STREAM frames are discarded the moment ownership
        // clears (Lyra's clearQueuedTxAudio equivalent).
        if (txAudioOwner_ == ws) {
            txAudioOwner_ = nullptr;
            chronoOutstanding_ = 0;
            chronoTimer_->stop();
            emit statusMessage(
                QStringLiteral("TCI: TX-audio ownership RELEASED"));
        }
        // TX-rip Phase 1 (Q2): R-H2 keyup-restore returns with the
        // new TX DSP worker per docs/TX_ARCHITECTURAL_MAPPING.md §10.3.
        stream_->requestMoxFromTci(false);
        // NO direct/pre-edge trx reply — the on-wire-edge onMoxActiveChanged
        // broadcast is the sole trx authority (as Thetis).  Replying trx:0,false
        // here, before the async unkey takes effect, told MSHV "back to RX" while
        // the wire was still keyed, decoupling its return-to-RX from reality.
        return;
    }
    // ── RIT/XIT: acknowledge inactive (Lyra RIT/XIT aren't driven over TCI).
    //   NOTE (WSJT-X TCI, verified from live [tci-rx-text] captures 2026-07-18):
    //   WSJT-X drives its "split" ENTIRELY through VFO B (`vfo:0,1,<dial+500>`)
    //   and spams `split_enable:0,false` ~1/s — it NEVER sends split_enable:true.
    //   SPLIT_ENABLE below is Thetis-literal (honors the client's explicit
    //   value, never derives split from VFO positions), so WSJT-X simply never
    //   asks Lyra to enable split — exactly as it never asks Thetis to.
    if (cmd == QStringLiteral("RIT_ENABLE") || cmd == QStringLiteral("XIT_ENABLE")) {
        const QString idx = args.isEmpty() ? QStringLiteral("0") : args[0];
        sendTo(ws, cmd.toLower() + QStringLiteral(":%1,false").arg(idx));
        return;
    }
    // SPLIT_ENABLE — as Thetis (handleSplitEnableMessage): 2 args = SET, 1 arg =
    // GET.  Honor the client's explicit split command literally (true AND false);
    // split is NEVER derived from VFO positions.  On a SET the state change is
    // echoed to all clients by the splitEnabledChanged handler (ctor lambda),
    // matching Thetis's OnSplitChanged→sendSplit.
    if (cmd == QStringLiteral("SPLIT_ENABLE")) {
        // Thetis int.TryParse(args[0], out rx) gate: arg 0 MUST be a numeric rx
        // index.  WSJT-X's 1-arg `split_enable:false` fails that parse, so
        // Thetis ignores it entirely (no set, no reply) — match that exactly
        // rather than mis-reading "false" as an rx index.
        bool okr = false;
        const int rx = args.isEmpty() ? -1 : args[0].trimmed().toInt(&okr);
        if (!okr) return;
        if (args.size() >= 2 && stream_) {          // SET (2 args)
            const QString val = args[1].trimmed().toLower();
            stream_->setSplitEnabled(val == QStringLiteral("true")
                                     || val == QStringLiteral("1"));
        } else if (stream_) {                        // GET (1 numeric arg)
            sendTo(ws, QStringLiteral("split_enable:%1,%2")
                           .arg(rx)
                           .arg(stream_->splitEnabled() ? QStringLiteral("true")
                                                        : QStringLiteral("false")));
        }
        return;
    }
    // #172 — DRIVE / TUNE_DRIVE: operator TX-power setters over TCI
    // (single global 0..100 % value, bidirectional read-back).  Were
    // silent-accept stubs before #172.  DRIVE maps the percent to the
    // HL2 drive DAC (raw 0..255) with the SAME round-trip math as the
    // TxPanel slider — raw=round(pct*255/100), pct=round(raw*100/255) —
    // so a value set over TCI reads back identically in the UI and the
    // per-band BandMemory live-save (driven by txDriveLevelChanged)
    // fires exactly as for an operator slider drag.  TUNE_DRIVE is
    // already a percent in Prefs (Task #74), set directly.  Last arg is
    // the value (matches the CW_*_SPEED arg handling above; tolerates a
    // stray leading rx index a non-conformant client might prepend).
    // DRIVE / TUNE_DRIVE — as Thetis (handleDrivePower TCIServer.cs:4003-4029):
    // 2 args (rx,pct) = SET, 1 arg (rx) = GET (report, change nothing).  The
    // old `!args.isEmpty()` form treated a `drive:0` QUERY as "set drive 0%",
    // zeroing TX power.
    if (cmd == QStringLiteral("DRIVE")) {
        if (args.size() >= 2 && stream_) {
            bool f = false; const int pct = args[1].toInt(&f);
            if (f) stream_->setTxDriveLevel(
                       qRound(qBound(0, pct, 100) * 255.0 / 100.0));
        } else if (stream_) {
            sendTo(ws, QStringLiteral("drive:0,%1")   // Thetis sendDrivePower: drive:<rx>,<pwr>
                          .arg(qRound(stream_->txDriveLevel() * 100.0 / 255.0)));
        }
        return;
    }
    if (cmd == QStringLiteral("TUNE_DRIVE")) {
        if (args.size() >= 2 && prefs_) {
            bool f = false; const int pct = args[1].toInt(&f);
            if (f) prefs_->setTuneDrivePct(qBound(0, pct, 100));
        } else if (prefs_) {
            sendTo(ws, QStringLiteral("tune_drive:0,%1")  // Thetis sendTunePower: tune_drive:<rx>,<pwr>
                          .arg(prefs_->tuneDrivePct()));
        }
        return;
    }

    // TX-only setters with no read-back of interest — silently accept.
    // (TX_STREAM_AUDIO_BUFFERING moved to commit 3.4's echo path so
    // MSHV / Thetis-client gets confirmation the buffering value
    // applied.)
    if (cmd == QStringLiteral("MON_ENABLE") || cmd == QStringLiteral("MON_VOLUME")
        || cmd == QStringLiteral("RIT_OFFSET") || cmd == QStringLiteral("XIT_OFFSET")
        || cmd == QStringLiteral("KEYER")
        || cmd.startsWith(QStringLiteral("CW_MACROS"))
        || cmd == QStringLiteral("CW_MSG") || cmd == QStringLiteral("CW_TERMINAL")
        || cmd == QStringLiteral("CW_KEYER_SPEED"))
        return;

    // ── DX-cluster spots ─────────────────────────────────────────
    if (cmd == QStringLiteral("SPOT")) {
        // SPOT:call,mode,freqHz[,argb][,text]
        if (spots_ && args.size() >= 3) {
            bool f = false; const qint64 hz = args[2].toLongLong(&f);
            quint32 argb = 0xFFFFD700u;
            if (args.size() >= 4) {
                bool g = false; const quint32 c = args[3].toUInt(&g);
                if (g && c) argb = c;
            }
            const QString text = args.size() >= 5 ? args.mid(4).join(QLatin1Char(',')) : QString();
            if (f) spots_->addSpot(args[0], args[1], hz, argb, text);
        }
        return;
    }
    if (cmd == QStringLiteral("SPOT_DELETE")) {
        if (spots_ && !args.isEmpty()) spots_->deleteSpot(args[0]);
        return;
    }
    if (cmd == QStringLiteral("SPOT_CLEAR")) {
        if (spots_) spots_->clearAll();
        return;
    }

    // ── binary streams (TCI v2.0 §3.4) ───────────────────────────
    if (cmd.startsWith(QStringLiteral("AUDIO_")) || cmd.startsWith(QStringLiteral("IQ_"))
        || cmd.startsWith(QStringLiteral("LINE_OUT_"))) {
        if (!ws) return;
        StreamCfg &cfg = streams_[ws];
        const int rx = args.isEmpty() ? 0 : args[0].trimmed().toInt();
        // #180 — ECHO the start/stop ack back to the client, exactly as the
        // reference does (TCIServer.cs sendAudioStartStop → "audio_start:0;").
        // JTDX (WSJT-X family) WAITS for this ack to confirm the stream is
        // live; without it, it abandons the connection and reconnect-loops
        // (the operator's 3× `audio_start:0` capture).  MSHV tolerated the
        // missing echo (it just consumes the binary frames), so the gap was
        // invisible until JTDX.  Echo BEFORE recompute so the client sees the
        // ack promptly.
        // LINE_OUT is the HL2 codec line-out — alias of the RX audio stream.
        if (cmd == QStringLiteral("AUDIO_START") || cmd == QStringLiteral("LINE_OUT_START")) {
            sendTo(ws, cmd.toLower() + QStringLiteral(":%1").arg(rx));
            cfg.audio = true;  recomputeStreaming();
        } else if (cmd == QStringLiteral("AUDIO_STOP") || cmd == QStringLiteral("LINE_OUT_STOP")) {
            sendTo(ws, cmd.toLower() + QStringLiteral(":%1").arg(rx));
            cfg.audio = false; recomputeStreaming();
        } else if (cmd == QStringLiteral("IQ_START")) {
            sendTo(ws, QStringLiteral("iq_start:%1").arg(rx));
            cfg.iq = true;     recomputeStreaming();
        } else if (cmd == QStringLiteral("IQ_STOP")) {
            sendTo(ws, QStringLiteral("iq_stop:%1").arg(rx));
            cfg.iq = false;    recomputeStreaming();
        } else if (cmd == QStringLiteral("AUDIO_STREAM_SAMPLE_TYPE") && !args.isEmpty()) {
            const QString t = args[0].toLower();
            cfg.fmt = (t == QStringLiteral("int16"))   ? FMT_INT16
                    : (t == QStringLiteral("int24"))   ? FMT_INT24
                    : (t == QStringLiteral("int32"))   ? FMT_INT32 : FMT_FLOAT32;
        } else if (cmd == QStringLiteral("AUDIO_STREAM_CHANNELS") && !args.isEmpty()) {
            cfg.chans = (args[0].toInt() == 1) ? 1 : 2;
        }
        // AUDIO_SAMPLERATE / IQ_SAMPLERATE / AUDIO_STREAM_SAMPLES accepted
        // but Lyra streams at its native rate (48 kHz audio / the IQ rate)
        // and reports that rate in each frame header.
        return;
    }

    // ── DSP toggles Lyra doesn't yet expose: soft store + echo ───
    // Accept the write and round-trip it so client panels stay in sync;
    // wired to the real WDSP chain when the DSP+Audio panel lands.
    static const QStringList kSoft = {
        QStringLiteral("AGC_MODE"), QStringLiteral("AGC_GAIN"),
        QStringLiteral("SQL_ENABLE"), QStringLiteral("SQL_LEVEL"),
        QStringLiteral("RX_NB_ENABLE"), QStringLiteral("RX_NB_PARAM"),
        QStringLiteral("RX_NR_ENABLE"), QStringLiteral("RX_ANF_ENABLE"),
        QStringLiteral("RX_ANC_ENABLE"), QStringLiteral("RX_APF_ENABLE"),
        QStringLiteral("RX_BIN_ENABLE"), QStringLiteral("RX_DSE_ENABLE"),
        QStringLiteral("RX_NF_ENABLE"), QStringLiteral("RX_BALANCE"),
        QStringLiteral("DIGL_OFFSET"), QStringLiteral("DIGU_OFFSET"),
        QStringLiteral("LOCK"), QStringLiteral("VFO_LOCK"),
        QStringLiteral("RX_CHANNEL_ENABLE")
    };
    if (kSoft.contains(cmd)) {
        // Heuristic: a "channelled" command keeps arg[0] (the rx index) in
        // the reply; the value is everything the client sent.  On a bare
        // query (only the rx index, or nothing) echo the stored/default.
        const bool boolish = cmd.endsWith(QStringLiteral("_ENABLE"))
                             || cmd == QStringLiteral("LOCK")
                             || cmd == QStringLiteral("VFO_LOCK");
        const QString def = boolish ? QStringLiteral("false") : QStringLiteral("0");
        if (args.size() >= 2) {                       // write (rx + value…)
            softSet(cmd, args.mid(1).join(QLatin1Char(',')));
        } else {                                       // query
            const QString rx = args.isEmpty() ? QStringLiteral("0") : args[0];
            sendTo(ws, cmd.toLower()
                       + QStringLiteral(":%1,%2").arg(rx, softGet(cmd, def)));
        }
        return;
    }

    // Unknown command → silently ignore (spec-compliant).
    Q_UNUSED(ws);
}

// ── broadcast handlers (radio → clients) ─────────────────────────

// TX-frequency announce (reference TCIServer.cs::sendTXFrequencyChanged,
// :2246-2258).  The operating carrier == the TX VFO frequency when not split;
// Lyra has no SPLIT/RX2 yet so it equals the `vfo:0,0` value.  Emitting it is
// purely additive: clients that don't parse it ignore it, and clients that do
// (LogHX3 &c.) get the real QSO frequency instead of a stuck 0.  SDRLogger+ is
// unaffected — its Combo link uses the separate `lyra_*` messages, and this
// carries the SAME carrier value as `vfo:0,0`, so any freq a client reads is
// consistent whichever field it prefers.
QString TciServer::txFrequencyLine() const {
    if (!stream_) return QString();
    // Actual TX carrier: VFO B under split, else VFO A (txFreqHz() resolves it).
    const qint64 carrier = qint64(stream_->txFreqHz())
                           + (engine_ ? engine_->markerOffsetHz() : 0);
    return QStringLiteral("tx_frequency:%1").arg(carrier);
}
QString TciServer::txFrequencyThetisLine() const {
    if (!stream_) return QString();
    const qint64 carrier = qint64(stream_->txFreqHz())
                           + (engine_ ? engine_->markerOffsetHz() : 0);
    // Reference: tx_frequency_thetis:<freq>,<band>,<rx2_enabled>,<tx_vfob>.
    // Band token mirrors Thetis's enum ("b40m"/"b80m"/…); GEN out-of-band.
    QString band = BandMemory::bandNameFor(int(carrier));
    band = (band.isEmpty() || band.contains(QLatin1Char('_')))
               ? QStringLiteral("gen")   // Thetis Band.GEN → "gen" (no "b" prefix)
               : QStringLiteral("b") + band;
    const QLatin1String txVfoB = stream_->splitEnabled()
                                     ? QLatin1String("true")
                                     : QLatin1String("false");
    return QStringLiteral("tx_frequency_thetis:%1,%2,false,%3")
        .arg(carrier).arg(band).arg(txVfoB);
}
void TciServer::broadcastTxFrequency() {
    if (!stream_) return;
    broadcast(QStringLiteral("tx_frequency"),        txFrequencyLine());
    broadcast(QStringLiteral("tx_frequency_thetis"), txFrequencyThetisLine());
}

void TciServer::onFreqChanged() {
    if (!stream_) return;
    const qint64 hz = qint64(stream_->rx1FreqHz());          // DDS centre
    const qint64 carrier = hz + (engine_ ? engine_->markerOffsetHz() : 0);
    broadcast(QStringLiteral("dds:0"),   QStringLiteral("dds:0,%1").arg(hz));
    broadcast(QStringLiteral("vfo:0,0"), QStringLiteral("vfo:0,0,%1").arg(carrier));
    // tx_frequency reflects the ACTUAL TX freq (as Thetis, whose dedicated
    // OnTXFrequencyChanged signal fires only on a real TX-freq change).  A VFO-A
    // move only changes the TX freq when split is OFF (TX = VFO A).  Under split
    // TX = VFO B (unchanged here), so re-announcing tx_frequency on every VFO-A
    // move is a needless extra broadcast — skip it.  Mirror of onVfoBChanged.
    if (!stream_->splitEnabled())
        broadcastTxFrequency();
}
void TciServer::onVfoBChanged() {
    if (!stream_) return;
    // §3.1 synchronizer echo: VFO B moved (operator split, or a client set it) —
    // confirm it to ALL clients so WSJT-X et al. stop re-sending.
    const qint64 carrier = qint64(stream_->vfoBHz())
                           + (engine_ ? engine_->markerOffsetHz() : 0);
    broadcast(QStringLiteral("vfo:0,1"), QStringLiteral("vfo:0,1,%1").arg(carrier));
    // tx_frequency reflects the ACTUAL TX freq (as Thetis, whose VFO-data loop
    // only re-announces it when the TX freq truly changes).  A VFO-B move only
    // changes the TX freq when split is ON (TX follows VFO B).  When split is
    // OFF, TX = VFO A (unchanged here) — re-announcing tx_frequency on every
    // client VFO-B set (e.g. WSJT-X's dial+500 on every tune) is a needless
    // extra broadcast, so skip it.
    if (stream_->splitEnabled())
        broadcastTxFrequency();
}
void TciServer::onModeChanged() {
    if (!prefs_) return;
    broadcast(QStringLiteral("modulation:0"),
              QStringLiteral("modulation:0,%1").arg(toTciMode(prefs_->mode())));
    // The CW carrier offset flips with the mode, so the operating (VFO) freq
    // changes even though the DDS centre didn't — re-announce it (Lyra's
    // carrier=DDS+pitch convention; for non-CW the value is unchanged).  Thetis
    // emits ONLY `sendMode` from the mode path (ModeChange, TCIServer.cs:1439-
    // 1443) and never tx_frequency, so do NOT broadcast tx_frequency here.
    if (stream_) {
        const qint64 carrier = qint64(stream_->rx1FreqHz())
                               + (engine_ ? engine_->markerOffsetHz() : 0);
        broadcast(QStringLiteral("vfo:0,0"),
                  QStringLiteral("vfo:0,0,%1").arg(carrier));
    }
}
void TciServer::onVolumeChanged() {
    if (!engine_) return;
    const double v = engine_->volumeDb();
    if (v < -60.0 || v > 0.0) return;   // Thetis sendVolume skips out-of-range
    broadcast(QStringLiteral("volume"),
              QStringLiteral("volume:%1").arg(QString::number(v, 'f', 1)));  // F1
}
void TciServer::onMutedChanged() {
    if (!engine_) return;
    broadcastNow(QStringLiteral("mute:%1")
                     .arg(engine_->muted() ? QStringLiteral("true")
                                           : QStringLiteral("false")));
}
void TciServer::onPassbandChanged() {
    if (!engine_) return;
    broadcast(QStringLiteral("rx_filter_band:0"),
              QStringLiteral("rx_filter_band:0,%1,%2")
                  .arg(qRound(engine_->passbandLowHz()))
                  .arg(qRound(engine_->passbandHighHz())));
}
void TciServer::onRunningChanged() {
    if (!stream_) return;
    broadcastNow(stream_->isRunning() ? QStringLiteral("start")
                                      : QStringLiteral("stop"));
}
void TciServer::onSmeterTick() {
    if (!sensorsEnabled_ || clients_.isEmpty() || !engine_) return;
    // Broadcast the SAME calibrated RX S-meter dBm the on-screen meter
    // shows.  MeterModel owns the one calibration (WDSP RXA_S_PK in-passband
    // + operator calDb trim − LNA gain), so the front-panel meter and every
    // TCI client (SDRLogger+, WSJT-X, N1MM, …) agree on a real, S9=-73dBm-
    // referenced reading.
    //
    // WAS: audioDbFs − 30 — a POST-AGC audio-level dBFS with a fixed coarse
    // offset ("stopgap until per-band cal lands").  That is not an S-meter:
    // AGC compresses it toward full scale so it barely tracks signal
    // strength, and the −30 is arbitrary.  SDRLogger+'s meter reading being
    // "wildly off" traced entirely to this line reaching for the wrong
    // engine value while a fully-calibrated one already existed next door in
    // MeterModel.  Fall back to the raw uncalibrated engine reading only if
    // the meter model isn't wired (shouldn't happen in the full app).
    const double dbm = meter_ ? meter_->rxSMeterDbm() : engine_->sMeterDbm();
    broadcast(QStringLiteral("rx_channel_sensors:0,0"),
              QStringLiteral("rx_channel_sensors:0,0,%1")
                  .arg(QString::number(dbm, 'f', 1)));

    // Combo received-S auto-fill: also share the numeric SNR (dB) so a linked
    // SDRLogger+ can gate its auto RST-received suggestion — the S-meter alone
    // can't tell a real S9 from an S9 of pure noise. Lyra-namespaced and
    // combo-scoped (only sent while Combo is on, which is the only time it's
    // used); other TCI clients ignore the unknown command.
    if (comboEnabled_ && meter_)
        broadcast(QStringLiteral("lyra_snr"),
                  QStringLiteral("lyra_snr:%1")
                      .arg(QString::number(meter_->rxSnrDb(), 'f', 1)));
}

} // namespace lyra::ui

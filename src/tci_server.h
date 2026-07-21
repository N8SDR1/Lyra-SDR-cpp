// Lyra — TCI server (Transceiver Control Interface, public spec
// v1.9 / v2.0).  Clean-room implementation against the published
// spec (D:\sdrprojects\TCI Protocol.pdf) and the command surface
// the working reference exposes — no external code is used.
//
// Lets logging / cluster software (SDRLogger+, Log4OM, WSJT-X, …)
// control and read Lyra over a WebSocket.  Lyra is RX-only today, so
// TX commands (trx/tune/drive/cw_*/mon/rit/xit/split) are acknowledged
// inactive.  DSP toggles Lyra doesn't yet expose a control for
// (agc/squelch/nb/nr/anf/…) are accepted and round-tripped via a soft
// store so client panels stay in sync; they get wired to the real DSP
// when the DSP+Audio control panel lands.
//
// Channel convention: channel 0 = RX1 (the only receiver today);
// channel 1 (RX2) is parsed but ignored until RX2 exists, so we
// advertise channel_count:1.
//
// Everything runs on the Qt main thread (QWebSocketServer is
// signal/slot driven); radio change-signals drive the broadcasts.

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <vector>

class QWebSocketServer;
class QWebSocket;
class QTimer;

namespace lyra::ipc { class HL2Stream; }
namespace lyra::dsp { class WdspEngine; }  // TciMicSource / TxDspWorker ripped (Q2)

namespace lyra::ui {

class Prefs;
class SpotStore;
class MeterModel;
class CwMacroModel;

class TciServer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
public:
    TciServer(Prefs *prefs, lyra::ipc::HL2Stream *stream,
              lyra::dsp::WdspEngine *engine, SpotStore *spots,
              QObject *parent = nullptr);
    ~TciServer() override;

    bool running() const;
    int  clientCount() const { return clients_.size(); }
    // Human-readable reason the last start() failed to bind (empty = no error).
    // Surfaced in the Settings → Network status line so a port collision
    // (e.g. another app grabbed the TCI port) is visible, not a silent bounce.
    QString bindError() const { return bindError_; }

    // --- operator settings (persisted to QSettings "tci/...") ---
    bool    enabled() const   { return enabled_; }
    int     port() const      { return port_; }
    QString bindHost() const  { return bindHost_; }
    int     rateLimitMs() const { return rateLimitMs_; }
    bool    sendInitialState() const { return sendInitialState_; }
    bool    emulateExpertSdr3() const { return emulateExpertSdr3_; }
    bool    emulateSunSdr2() const    { return emulateSunSdr2_; }
    bool    cwluBecomesCw() const     { return cwluBecomesCw_; }

    void setPort(int p);
    void setBindHost(const QString &h);
    void setRateLimitMs(int ms);
    void setSendInitialState(bool on);
    void setEmulateExpertSdr3(bool on);
    void setEmulateSunSdr2(bool on);
    void setCwluBecomesCw(bool on);
    // Apply the enable toggle: start (or restart) / stop + persist.
    void setEnabled(bool on);

    bool start();
    void stop();

    // RX S-meter source for the TCI rx_channel_sensors broadcast.  Wired
    // once by MainWindow after both objects exist, so the value sent over
    // TCI is the SAME calibrated dBm the on-screen meter shows (MeterModel
    // owns the one calibration).  Null-safe: without it the broadcast falls
    // back to the raw uncalibrated engine reading.
    void setMeterModel(MeterModel *m) { meter_ = m; }

    // Lyra ↔ SDRLogger+ "Combo" link (docs/architecture/combo_link_design.md).
    // The CW Console contact row is the shared object; when Combo is on Lyra
    // broadcasts it as `lyra_contact:lyra,…` and honours the inbound
    // `lyra_contact:sdrlog,…` (callbook name-back).  Wired once by MainWindow
    // after both objects exist.
    void setCwMacros(CwMacroModel *m);
    bool comboEnabled() const { return comboEnabled_; }
    void setComboEnabled(bool on);   // persist + announce lyra_combo:on/off

    // TX-rip Phase 1 (Q2): setTciMicSource / setTxDspWorker removed
    // here and at main.cpp.  Inbound TCI TX_AUDIO_STREAM frames are
    // silently dropped until the new TX path lands per
    // docs/TX_ARCHITECTURAL_MAPPING.md §10.3.

signals:
    void runningChanged();
    void clientCountChanged(int n);
    void statusMessage(const QString &msg);
    // Lyra is RX-only and the TCI server can't reach the connect flow
    // directly; MainWindow wires these to its Start/Stop handler.
    void startRequested();
    void stopRequested();

private slots:
    void onNewConnection();
    void onTextMessage(const QString &msg);
    void onClientDisconnected();
    void onSmeterTick();
    void onMaintenanceTick();   // ping clients + prune dead sockets
    void onRlFlush();           // trailing-edge flush of rate-limited broadcasts
    // Task #33: TCI v2 §3.4 TX_CHRONO outbound pump — periodic when a
    // TX-audio listener is active AND wire MOX is on; emits a type=3
    // frame asking the client to deliver N samples next.
    void onChronoTick();
    // Task #33: hook to HL2Stream::moxActiveChanged so we release
    // TX-audio ownership cleanly when MOX drops externally (operator
    // click, FSM keyup, foot-switch release, §15.20 TX-timeout fire).
    // Equivalent to the working reference's SyncTciPttToMox.
    void onMoxActiveChanged(bool on);
    // Combo link: the CW Console contact row changed → broadcast it to a
    // linked SDRLogger+ as `lyra_contact:lyra,…` (suppressed while applying
    // an inbound remote update, so name-back can't bounce — echo guard).
    void onCwContactChanged();
    // Combo Stage B: a {LOG}-tagged CW macro was sent → broadcast lyra_log so a
    // linked SDRLogger+ logs the current QSO. Wired to CwMacroModel in setCwMacros.
    void onLogQsoRequested();
    // TCI v2.0 binary streams (from the DSP worker, queued onto this thread).
    void onTciAudioBlock(const QByteArray &monoFloat, int rateHz);
    void onTciIqBlock(const QByteArray &iqFloat, int rateHz);
    // Task #33: inbound TCI v2 binary frame from any connected client.
    // Parses the 64-byte Stream header (TCI Protocol §3.4) and
    // dispatches per type — TX_AUDIO_STREAM goes to the TciMicSource
    // (with sanitization), other types are ignored.
    void onBinaryMessage(const QByteArray &frame);

private:
    void sendInit(QWebSocket *ws);                 // handshake burst
    // Task #68 — resample inbound TX_AUDIO_STREAM audio from any
    // client-side sample rate to the TXA input rate (typ 48 kHz).
    // Lazy-creates the WDSP resampler on first non-48k frame and
    // on rate change; returns the resampled vector (or `in` by
    // value if rate matches / WDSP missing — fast-path).
    std::vector<float> resampleTxIn(const std::vector<float> &in,
                                    int inRate, int outRate);
    void               destroyTxResampler();
    // #180 — resample the outbound RX audio from the engine rate (48 kHz)
    // to the client-negotiated AUDIO_SAMPLERATE (8/12/24/48 kHz) before
    // packetising, mirroring the reference's resampleRxAudioSamples
    // (TCIServer.cs:995 — WDSP create_resampleFV / xresampleFV).  Stateful
    // across calls (polyphase history); lazy-created, recreated on rate
    // change.  Returns `in` unchanged when inRate==outRate (fast path).
    std::vector<float> resampleRxOut(const std::vector<float> &in,
                                     int inRate, int outRate);
    void               destroyRxResampler();
    void dispatch(QWebSocket *ws, const QString &cmd, const QStringList &args);
    void sendTo(QWebSocket *ws, const QString &line);
    void broadcast(const QString &key, const QString &line); // rate-limited
    void broadcastNow(const QString &line);        // unthrottled (edges)
    void pruneDeadClients();                       // drop non-connected sockets
    void recomputeStreaming();                     // enable engine taps iff a client wants them
    // Radio-signal handlers → broadcasts.
    void onFreqChanged();
    void onVfoBChanged();      // echo vfo:0,1 (VFO B) to clients — TCI §3.1 sync
    void onModeChanged();
    void onRunningChanged();
    void onVolumeChanged();
    void onMutedChanged();
    void onPassbandChanged();
    // TX-frequency announce — reference TCIServer.cs::sendTXFrequencyChanged
    // (the standard `tx_frequency` + the Thetis-bespoke `tx_frequency_thetis`).
    // Loggers (LogHX3, …) take the logged QSO frequency from `tx_frequency`;
    // Lyra never emitted it, so those clients logged 0.  Carrier = operating
    // VFO freq (no SPLIT/RX2 yet → rx2=false, vfob=false).
    QString txFrequencyLine() const;
    QString txFrequencyThetisLine() const;
    void    broadcastTxFrequency();
    // Soft-store helpers (DSP params Lyra doesn't truly control yet).
    QString softGet(const QString &key, const QString &def) const;
    void    softSet(const QString &key, const QString &val);
    // Mode mapping between Lyra modes and TCI tokens.
    QString        toTciMode(const QString &lyraMode) const;
    QString        fromTciMode(const QString &tciMode, qint64 freqHz) const;
    QString        modulationsList() const;
    static int     parseChannel(const QString &s, bool *ok);
    static bool    parseBool(const QString &s);

    Prefs                 *prefs_         = nullptr;
    lyra::ipc::HL2Stream  *stream_        = nullptr;
    lyra::dsp::WdspEngine *engine_        = nullptr;
    SpotStore             *spots_         = nullptr;
    MeterModel            *meter_         = nullptr;   // RX S-meter dBm source (calibrated)
    CwMacroModel          *cwMacros_      = nullptr;   // Combo link — shared CW contact row
    bool                   comboEnabled_  = false;     // "SDRLogger+ Combo" master toggle
    bool                   comboApplyingRemote_ = false; // echo guard while applying inbound
    // TX-rip Phase 1 (Q2): tciMicSource_ / txWorker_ / savedMicSource_
    // / micSourceForcedTci_ removed — TX DSP worker + TCI mic source
    // are being rebuilt from empty files per
    // docs/TX_ARCHITECTURAL_MAPPING.md §10.3.  Inbound TX_AUDIO_STREAM
    // is dropped; R-H2 mic-source force/restore returns with the
    // new wiring.
    QWebSocketServer      *server_        = nullptr;
    QList<QWebSocket *>    clients_;
    // Per-client binary-stream subscription state (TCI v2.0 §3.4).
    struct StreamCfg {
        bool audio = false;
        bool iq    = false;
        int  fmt   = 3;   // 0 int16 / 1 int24 / 2 int32 / 3 float32
        int  chans = 2;   // audio channels (1 or 2); IQ is always 2
    };
    QHash<QWebSocket *, StreamCfg> streams_;
    QTimer                *smeterTimer_ = nullptr;
    QTimer                *maintTimer_  = nullptr;   // ping + prune dead clients
    bool                   sensorsEnabled_ = false;
    QString                bindError_;   // last listen() failure reason ("" = ok)

    // Match the reference's RX-audio packetisation cadence (per
    // TCIServer.cs:5437-5513 PublishRxAudioSamples): accumulate
    // the per-DSP-block mono floats arriving from WdspEngine and
    // emit one binary frame only when audioStreamSamples_ samples
    // are queued.  At 48 kHz × 2048 ≈ 42.6 ms blocks, matching the
    // advertised `audio_stream_samples:2048` value clients (MSHV /
    // JTDX / etc.) configure their decoders for.  Without this
    // packetisation Lyra emits 8× more frames at 8× shorter blocks
    // and clients mis-interpret the energy density / FT8 symbol
    // cadence.
    std::vector<float>     audioPending_;
    int                    audioPendingRate_  = 48000;
    static constexpr int   kAudioPacketSamples = 2048;   // 48 kHz default

    // #180 — client-negotiated RX-audio-out parameters (server-wide single
    // value, last-writer-wins, exactly like the reference's m_audioSampleRate
    // / m_audioStreamSamples — TCIServer.cs:779/782).  audioOutRate_ is set
    // from AUDIO_SAMPLERATE (validated to 8/12/24/48 kHz); audioOutSamples_
    // tracks the per-rate default (defaultAudioStreamSamples) unless the
    // client explicitly set AUDIO_STREAM_SAMPLES.  The RX-out path resamples
    // the engine's 48 kHz audio to audioOutRate_ and packetises at
    // audioOutSamples_, stamping audioOutRate_ in the stream header — the gap
    // that left JTDX (12 kHz) unable to use the audio while MSHV (48 kHz
    // default) worked.
    int                    audioOutRate_           = 48000;
    int                    audioOutSamples_        = kAudioPacketSamples;
    bool                   audioOutSamplesExplicit_ = false;
    // RX-out resampler (WDSP resampleFV) — mirror of txResampler_ for the
    // inbound TX path.  Owned by the Qt main thread (all WebSocket + engine
    // tap signals queue here), so no mutex.
    void                  *rxResampler_        = nullptr;
    int                    rxResamplerInRate_  = 0;
    int                    rxResamplerOutRate_ = 0;

    // TX-in resampler (Task #68) — lazy-created when an inbound
    // TX_AUDIO_STREAM frame arrives at a rate other than the TXA
    // input rate (kTciTxTargetRate=48000); destroyed + recreated
    // on rate change.  Matches the reference (cmaster.cs:1431-1473
    // resampleTCITxSamples) using the same WDSP polyphase
    // float-vector resampler.  Held opaque void* per WDSP's PORT
    // signature.  Owned by the TciServer (Qt main thread); the
    // inbound binary handler also runs on Qt main, so no mutex.
    void                  *txResampler_           = nullptr;
    int                    txResamplerInRate_     = 0;
    int                    txResamplerOutRate_    = 0;
    // Drop-oldest cap so a stuck/idle client can't grow the
    // accumulator unbounded (recomputeStreaming turns the engine
    // tap off when no clients subscribed; cap is belt-and-braces).
    static constexpr int   kAudioPendingCap   = 4 * kAudioPacketSamples;

    // Task #33: single active TX-audio listener (Thetis-faithful per
    // TCIServer.cs:3503/3510 TryAcquire/Release pattern).  Only ONE
    // TCI client owns the TX audio path at a time; a second client
    // asking for ownership gets denied.  Owned by the Qt main thread
    // (all WebSocket signals queue here) — no mutex needed.
    // QPointer auto-nulls when the owning QWebSocket is destroyed (deleteLater),
    // so every deref below is crash-safe against a dropped owner (CODEX-P0
    // dangling-deref / onChronoTick 0xC0000005 fix, 2026-06-15).
    QPointer<QWebSocket>   txAudioOwner_;
    // TX_CHRONO outbound pump (TCI v2 §3.4) — fires when txAudioOwner_
    // is set AND wire MOX is active.  Implements the reference's
    // dynamic-pull formula (cmaster.cs:1289-1359) — at each tick,
    // compute how many samples we will have once outstanding requests
    // come in, compare against the target buffer depth (a function
    // of the client's bufferingMs hint + the TXA input rate), and
    // emit `requestsNeeded` CHRONO requests this tick (bounded by
    // kTciTxMaxOutstanding).  Replaces the earlier fixed "one
    // request every 50 ms" pump which produced mis-paced TX audio
    // on the wire (the operator-bench-confirmed FT8 zero-spots
    // symptom 2026-06-01).
    QTimer                *chronoTimer_       = nullptr;
    int                    chronoOutstanding_ = 0;
    qint64                 chronoLastInboundMs_    = 0;    // monotonic ms
    // Dynamic client-negotiated parameters (default to MSHV values).
    // Updated when the client sends AUDIO_SAMPLERATE / AUDIO_STREAM_
    // SAMPLES / TX_STREAM_AUDIO_BUFFERING; consumed every CHRONO tick.
    int                    requestRate_      = 48000;
    int                    requestSamples_   = 2048;
    int                    bufferingMs_      = 50;
    // Negotiated audio_stream_channels — captured from inbound
    // AUDIO_STREAM_CHANNELS text command (handshake or live update)
    // and used in the CHRONO request `channels` field + modern
    // length semantics (length = samples * channels).  Mirrors
    // m_audioStreamChannels in the reference (TCIServer.cs:781
    // default 2; :5935-5949 handleAudioStreamChannels; :5526
    // CHRONO consumer).  Default 2 matches Lyra's outbound
    // advertisement on Connect (sendInit line ~949).
    int                    requestChannels_  = 2;
    // Modern-length-semantics flag, mirrors the reference's
    // m_seenModernTxAudioNegotiation (TCIServer.cs:5528 + :5946).
    // Set true the moment any modern handshake command arrives
    // (AUDIO_STREAM_CHANNELS / AUDIO_STREAM_SAMPLE_TYPE) — clients
    // that talk that vocabulary expect CHRONO length = samples *
    // channels.  Legacy/JTDX-style clients leave this false and
    // get the older length = samples behaviour.
    bool                   seenModernTxNeg_  = false;
    // Lyra-side TXA input constants — txa input rate matches Lyra's
    // open-TX-channel in-rate (48 kHz) and block size matches
    // TxChannel::kInSize (64 samples post-R-H1 / ~1.33 ms).  The
    // TXA input block-size rule is `in_size = 64 * rate / 48000`;
    // at 48 kHz this is 64.  R-H1 (2026-06-04) reverted from the
    // §15.29 (2026-06-03) bump to 128 back to 64 (§15.29 was
    // invisible for SSB voice but collapsed multi-tone modes —
    // FT8/MSHV via TCI; see docs/THETIS_VS_LYRA_RECONCILED.md R-H1).
    // For the typical MSHV cadence (requestSamples=2048,
    // requestRate=48000), predictedPacketSamples=2048 and
    // targetQueuedSamples=4800 — the Lyra txBlock does not affect
    // the formula at this client cadence.
    static constexpr int   kTciTxTargetRate   = 48000;
    static constexpr int   kTciTxBlockSamples = 64;
    static constexpr int   kTciTxMaxOutstanding = 64;   // ref TCI_TX_MAX_OUTSTANDING
    static constexpr int   kTciTxExtraBufferMs = 50;    // ref TCI_TX_EXTRA_BUFFER_MS
    // CHRONO tick cadence — matches the reference's TX stream service
    // loop sleep (cmaster.cs:1253 `Thread.Sleep(2)`).  Was 50 ms; that
    // 25× slower cadence let MSHV's audio buffer drain between Lyra
    // pull requests, producing audible alternating multi-tone / carrier-
    // residue on the panadapter during FT8 transmits (operator bench
    // 2026-06-04 mid-day, post-R-FT8 stereo-decode fix `8cddfa5`).
    // At 2 ms the per-tick work is microseconds (a few integer ops +
    // 0-3 ~64-byte WebSocket binary frames), well within Qt's
    // PreciseTimer envelope.
    static constexpr int   kChronoIntervalMs   = 2;     // tick cadence
    static constexpr int   kChronoTimeoutMs    = 250;   // outstanding clear
    static constexpr int   kChronoFallbackRequestSamples = 480;
    static constexpr int   kChronoFallbackBufferingMs    = 50;

    // settings
    bool     enabled_          = false;
    int      port_             = 50001;
    QString  bindHost_         = QStringLiteral("127.0.0.1");
    int      rateLimitMs_      = 20;      // ~50 Hz
    bool     sendInitialState_ = true;
    bool     emulateExpertSdr3_= false;
    bool     emulateSunSdr2_   = false;
    bool     cwluBecomesCw_    = true;
    // Set true while a TCI `tune:` command holds the key, so the wire-edge
    // onMoxActiveChanged mirrors the edge on the `tune:` channel (Thetis emits
    // tune: from its TuneChange event on the real edge, never a pre-edge reply).
    // Cleared on the falling edge.
    bool     tciTuneActive_    = false;
    // Task #75 — TCI RX-out linear-gain multiplier.  Cached from
    // Prefs.tciRxGainDb on every tciRxGainDbChanged emit so the
    // hot path in onTciAudioBlock is one std::atomic load (or a
    // single non-atomic double read on the main thread; tcp
    // ingress + main-thread Prefs setter never race).  Default
    // 1.0 = unity = pre-#75 byte-identical wire.
    double   rxGainLinear_     = 1.0;
    // Task #108 — symmetric INBOUND gain cache (MSHV → Lyra TXA).
    // Cached from Prefs.tciTxGainDb on every tciTxGainDbChanged emit
    // so the per-sample multiply in onTciAudioBlock doesn't pay
    // std::pow.  Default 1.0 = unity = pre-#108 byte-identical wire.
    double   txGainLinear_     = 1.0;

    // Per-key broadcast rate limiter + soft DSP-parameter store.
    QHash<QString, qint64> lastSent_;
    // Rate-limit trailing-edge: when a broadcast is dropped inside the window,
    // stash the LATEST line per key and a one-shot timer delivers it after the
    // window elapses — so the final value of a burst is never lost (mirror of
    // Thetis's per-type trailing System.Threading.Timer, TCIServer.cs:6016-6075).
    QHash<QString, QString> pendingLine_;
    QTimer                *rlFlushTimer_ = nullptr;
    QHash<QString, QString> soft_;
    QElapsedTimer          rateClock_;
};

} // namespace lyra::ui

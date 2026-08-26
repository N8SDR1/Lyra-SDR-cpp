// Lyra — WDSP RX channel engine implementation (Step 3c-ii).
// See wdsp_engine.h for the locked scope + the §14.2 gotcha list.

#include "wdsp_engine.h"

#include "capturedprofile.h"
#include "noisereducer.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QByteArray>

#include "wire/AAMix.h"  // P0.c direct port (reference aamix.h verbatim)
#include "wire/ObBuffs.h" // P4.b — OutBound(0,…): RX audio → r1 ring → ob_main → sendOutbound (the asioOUT-pattern tee, cmasio.c:137-145)
#include "wire/Ivac.h"   // #158 — create_ivac / destroy_ivac / xvacOUT / xvacIN / ivacGet
#include "dsp/ParamEq.h"     // #59 — RX EQ engine (processMonoDup / bypassed)
#include "dsp/EqAnalyzer.h"  // #59 — RX EQ analyzer feed (pre/post)
#include "wire/CMaster.h" // #158 Stage 4 — SendpInboundVacTxAudio (VAC-in seam)
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QStandardPaths>
#include <QIODevice>
#include <QMediaDevices>
#include <QSettings>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
#include <cstring>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#  include <excpt.h>   // __try / __except SEH filter constants
#endif

namespace {

// C++ half of the guarded enumeration — kept OUT of the __try function below
// because it allocates QList / QAudioDevice temporaries, and an SEH frame that
// unwinds can't hold objects with destructors (MSVC C2712).
void enumerateOutputsRaw(QList<QAudioDevice> *outs, QAudioDevice *def) {
    *outs = QMediaDevices::audioOutputs();
    *def  = QMediaDevices::defaultAudioOutput();
}

// Enumerate the PC audio outputs, but never let a wedged virtual audio device
// (VoiceMeeter / VB-Cable / a half-disconnected USB interface) take the whole
// app down.  This runs in the WdspEngine ctor — BEFORE the main window exists —
// so an unhandled fault here is an invisible "spins, then no window" lockout
// with no way back in (tester report, 2026-07-17).  On Windows the OS
// enumeration is a COM call that can fault inside a bad device's endpoint;
// contain it (SEH) and return false so the caller falls back to an empty list,
// which every consumer already treats as "use the HL2 codec / system default".
bool enumerateOutputsGuarded(QList<QAudioDevice> *outs, QAudioDevice *def) {
#if defined(_WIN32)
    __try {
        enumerateOutputsRaw(outs, def);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    enumerateOutputsRaw(outs, def);
    return true;
#endif
}

}  // namespace

namespace lyra::dsp {

namespace {

// P0.c direct port — the engine instance the aamixOutbound static
// member dispatches into.  AAMix's Outbound is the reference raw
// `void(*)(int,int,double*)` fn ptr, so the RX hand-off uses the
// reference's own free-function + global-context shape (the
// reference registers free functions that reach globals; cf.
// netInterface's OutBound consumers).  Written on the Qt thread in
// openRx1 BEFORE create_aamix (so it is live before mix_main can
// fire) and cleared in closeRx1 AFTER destroy_aamix (the mixer
// thread is gone by then per aamix.c:215-218).  Single-engine
// today; an RX2-era second engine registers its own outbound at
// its own paamix id exactly as the reference banks per-id mixers.
lyra::dsp::WdspEngine* g_aamixOutboundSelf = nullptr;

// #158 (#161 UAF fix) — vacInboundCb (the VAC-in → TX bridge registered via
// SendpInboundVacTxAudio) is now a WdspEngine static member, defined beside
// aamixOutbound below so it can gate xvacIN under vacMtx_ + vac1Active_ (the
// VAC device-change / enable-disable use-after-free fix).  A free function
// here couldn't reach those private members, and a member can't be defined
// inside this anonymous namespace.

// Mode / AGC integer constants, verified against the Python tree's
// wdsp_native.py (RxaMode / AgcMode) which in turn matches the
// upstream WDSP source.  Do NOT change without re-verifying — these
// map directly onto WDSP's RXA mode + wcpAGC mode switch statements.
constexpr int    kRxaModeUSB = 1;   // RxaMode.USB
// wcpAGC mode enum (WDSP AGC modes): FIXD=0 LONG=1 SLOW=2 MED=3 FAST=4.
// "off" maps to FIXD (fixed gain) — the operator-facing AGC-off.
constexpr int    kAgcModeOff  = 0;  // AgcMode.FIXD (fixed gain)
constexpr int    kAgcModeSlow = 2;  // AgcMode.SLOW
constexpr int    kAgcModeMed  = 3;  // AgcMode.MED
constexpr int    kAgcModeFast = 4;  // AgcMode.FAST
constexpr double kUsbLowHz   = 0.0;   // SSB/DIG low cut starts at centre (op req)
constexpr double kUsbHighHz  = 3000.0;

// Step 3e level-cal (mirrors the bench-proven Python _open_wdsp_rx).
// slope 35 -> var_gain = 10^(35/200) ~ 1.5 (WDSP/industry soft-knee);
// threshold -100 dBFS gives the AGC ~70 dB of headroom to boost weak
// signals without over-driving the output past 0 dBFS.  size 4096 +
// the IQ input rate are SetRXAAGCThresh's noise-bandwidth conversion
// args.  Do NOT also call SetRXAAGCTop — it writes the same max_gain
// field SetRXAAGCThresh computes and would clobber it.
constexpr int    kAgcSlope         = 35;
constexpr double kAgcThreshDbFs    = -100.0;
constexpr double kAgcThreshFftSize = 4096.0;
// AGC fixed-gain (dB) for mode 0 / FIXD ("AGC OFF").  WDSP's
// create-time default is 1000.0 linear = +60 dB (RXA.c:366) which
// makes AGC OFF audibly LOUDER than FAST/MED/SLOW that actively
// reduce gain on signal — operator-reported "backwards" behaviour.
// +20 dB matches the reference's RXFixedAGC default and gives
// modest amplification operators can dial up via AF/Vol.  Pushed
// on every AGC-mode flip (cheap; WDSP only USES fixed_gain when
// mode=FIXD).  Operator-tunable surface ships in sub-commit B.
constexpr double kAgcFixedGainDb   = 20.0;

// Step 5: WDSP spectral analyzer (panadapter) config.  Values mirror
// the standard HL2 RX analyzer setup (fft 4096, window 4, kaiser 14).  pixels is
// fixed (the panadapter scales it to its on-screen width).  overlap =
// fft - feed-block so one FFT advances per IQ feed (smooth).  Detector
// peak + recursive time-average for a stable display.  All tunable on
// the bench.
constexpr int    kAnDisp        = 0;       // analyzer id (RX1)
constexpr int    kAnMaxFft      = 32768;   // max fft (must be >= max_w)
constexpr int    kAnFftSize     = 4096;
// Full FFT resolution out of the analyzer (1 pixel per bin) so the
// zoom CROP in copySpectrum has the same source detail old Lyra's raw
// 4096-bin FFT had — cropping the centre 1/zoom of THIS stays smooth
// (a 2048 pre-binned output cropped + stretched looked "chunky").
constexpr int    kAnPixels      = 4096;    // dB output points
constexpr int    kAnWindow      = 4;       // soft-knee window (ref default)
constexpr double kAnKaiserPi    = 14.0;
constexpr int    kAnFrameRate   = 60;      // display fps — drives overlap,
                                           // max_w + averaging frame count.
                                           // Higher = more responsive trace
                                           // (tau holds the smoothing TIME
                                           // constant).  Operator-adjustable
                                           // control to come.
constexpr int    kAnDetector    = 0;       // 0 = peak
// Time-averaging OFF by default (reference ships AverageOn=false) — a
// LIVE trace, no smoothing lag, so it stays in sync with the audio.
// The smooth "fluid" look comes from the curve RENDERING (no lag), not
// from time-averaging.  Flip kAnAvgMode != 0 + tune kAnTau for an
// operator AVG toggle later.
constexpr int    kAnAvgMode     = 0;       // 0 = off (live)
constexpr double kAnTau         = 0.12;    // avg time constant (s) when on

// Step 3e audio-ring sizing.  The reference Python SoundDeviceSink
// pre-fills 100 ms (= 4800 frames @ 48 kHz) because Python/GIL
// delivered audio in bursty ~43 ms lumps that drained the ring.  The
// C++ RX worker pushes audio every ~5.3 ms with no GIL, so we run a
// SMALLER pre-fill.  Both tunable here.
constexpr int    kRingMs       = 200;  // ring capacity (ms)
constexpr int    kPrefillMs    = 30;   // startup silence pre-fill (ms)
// Explicit QAudioSink device-buffer depth.  Qt's default is large
// (~100-200 ms on WASAPI) and was the dominant audio latency that made
// the audio lag the panadapter.  ~40 ms keeps it tight without
// underrunning (operator-tunable on the bench).
constexpr int    kSinkBufferMs = 40;

// Step 3e: perceptual volume taper.  Slider position (0..1) -> dB gain
// so comfortable listening sits mid-slider instead of bunched at the
// bottom.  pos=1 -> 0 dB (unity), pos=0.5 -> -30 dB, pos->0 -> silence.
// Floor -60 dB (operator bench 2026-06-02: -40 dB floor was too
// shallow — Vol slider near the bottom still hotter than other apps).
// Operator-bench-confirmed louder=quieter feel works in this range;
// re-applied 2026-06-02 PM after the post-#71 audio commit was reverted
// for an unrelated MOX-path regression (this taper change was verified
// safe in that bench).
constexpr double kMinVolDb = -60.0;

double posToDb(double pos) {
    return (pos <= 0.0) ? kMinVolDb : kMinVolDb * (1.0 - pos);
}

double posToGain(double pos) {
    if (pos <= 0.0) return 0.0;
    if (pos >= 1.0) return 1.0;
    return std::pow(10.0, posToDb(pos) / 20.0);
}

// The HL2 AK4951 codec output runs hotter than the PC sound-card chain,
// so the SAME volume slider was much louder on the HL2 jack (operator:
// "13 on PC ≈ 35 on HL2" → ~8.8 dB).  Attenuate the HL2-jack path so
// both outputs read roughly the same at a given slider position.
constexpr double kHl2OutAtten = 0.30;   // ≈ -10.5 dB

// Demod-mode name -> WDSP RxaMode int (matches the bench-proven Python
// wdsp_native.py RxaMode enum).  Unknown -> USB.
int modeToWdsp(const QString &m) {
    if (m == QLatin1String("LSB"))  return 0;
    if (m == QLatin1String("USB"))  return 1;
    if (m == QLatin1String("DSB"))  return 2;
    if (m == QLatin1String("CWL"))  return 3;
    if (m == QLatin1String("CWU"))  return 4;
    if (m == QLatin1String("FM"))   return 5;
    if (m == QLatin1String("AM"))   return 6;
    if (m == QLatin1String("DIGU")) return 7;
    if (m == QLatin1String("DIGL")) return 9;
    if (m == QLatin1String("SAM"))  return 10;   // synchronous AM detector
    return 1;   // USB
}

} // namespace

// ---------------------------------------------------------------
// AudioRing — the QIODevice the QAudioSink pulls from (pull mode,
// the Qt-native equivalent of the reference's CallbackASIO /
// PortAudio callback).  Producer = the RX worker thread via push();
// consumer = Qt's audio backend thread via readData().  A std::mutex
// guards the ring indices (hold time = a memcpy of a few hundred
// int16, sub-ms).  On underrun readData pads silence and always
// returns the full requested length so the sink never stops.
// ---------------------------------------------------------------
class AudioRing : public QIODevice {
public:
    explicit AudioRing(int capacityFrames)
        : capFrames_(capacityFrames),
          buf_(static_cast<size_t>(capacityFrames) * 2, 0) {}

    // Producer (RX worker thread): push interleaved stereo int16.
    void push(const qint16 *lr, int nframes) {
        std::lock_guard<std::mutex> lk(mtx_);
        for (int f = 0; f < nframes; ++f) {
            if (countFrames_ >= capFrames_) {
                rd_ = (rd_ + 1) % capFrames_;   // overrun: drop oldest
                --countFrames_;
                ++overruns_;
            }
            buf_[static_cast<size_t>(wr_) * 2 + 0] = lr[f * 2 + 0];
            buf_[static_cast<size_t>(wr_) * 2 + 1] = lr[f * 2 + 1];
            wr_ = (wr_ + 1) % capFrames_;
            ++countFrames_;
        }
    }

    // Seed the ring with `frames` of silence so the sink has headroom
    // before the worker produces its first block.
    void prefillSilence(int frames) {
        std::lock_guard<std::mutex> lk(mtx_);
        rd_ = wr_ = countFrames_ = 0;
        const int n = std::min(frames, capFrames_);
        for (int i = 0; i < n; ++i) {
            buf_[static_cast<size_t>(wr_) * 2 + 0] = 0;
            buf_[static_cast<size_t>(wr_) * 2 + 1] = 0;
            wr_ = (wr_ + 1) % capFrames_;
            ++countFrames_;
        }
    }

    qint64 overruns()  const { std::lock_guard<std::mutex> lk(mtx_); return overruns_;  }
    qint64 underruns() const { std::lock_guard<std::mutex> lk(mtx_); return underruns_; }

    // --- QIODevice overrides (no Q_OBJECT needed: only virtuals) ---
    bool   isSequential() const override { return true; }
    qint64 writeData(const char *, qint64) override { return -1; }

    // Report a large, always-positive availability so the sink keeps
    // pulling on its own cadence (we pad silence inside readData).
    qint64 bytesAvailable() const override {
        return static_cast<qint64>(capFrames_) * kBytesPerFrame
               + QIODevice::bytesAvailable();
    }

    qint64 readData(char *data, qint64 maxlen) override {
        const qint64 maxFrames = maxlen / kBytesPerFrame;
        if (maxFrames <= 0) {
            return 0;
        }
        qint16 *out = reinterpret_cast<qint16 *>(data);
        bool padded = false;
        std::lock_guard<std::mutex> lk(mtx_);
        for (qint64 f = 0; f < maxFrames; ++f) {
            if (countFrames_ > 0) {
                out[f * 2 + 0] = buf_[static_cast<size_t>(rd_) * 2 + 0];
                out[f * 2 + 1] = buf_[static_cast<size_t>(rd_) * 2 + 1];
                rd_ = (rd_ + 1) % capFrames_;
                --countFrames_;
            } else {
                out[f * 2 + 0] = 0;   // underrun -> silence
                out[f * 2 + 1] = 0;
                padded = true;
            }
        }
        if (padded) {
            ++underruns_;
        }
        return maxFrames * kBytesPerFrame;   // always full -> sink stays active
    }

private:
    static constexpr qint64 kBytesPerFrame = 2 * sizeof(qint16);  // stereo
    mutable std::mutex  mtx_;
    std::vector<qint16> buf_;        // interleaved L,R; capFrames_*2
    int                 capFrames_;
    int                 rd_ = 0, wr_ = 0, countFrames_ = 0;
    qint64              overruns_ = 0, underruns_ = 0;
};

WdspEngine::WdspEngine(WdspNative *wdsp, QObject *parent)
    : QObject(parent), wdsp_(wdsp)
{
    // out_size = in_size * out_rate / in_rate (when in_rate >= out_rate).
    // With 1024 @ 192k -> 48k that is 256 frames per fexchange0 call.
    if (cfg_.inRate >= cfg_.outRate) {
        outSize_ = cfg_.inSize / (cfg_.inRate / cfg_.outRate);
    } else {
        outSize_ = cfg_.inSize * (cfg_.outRate / cfg_.inRate);
    }
    // Mirror cfg_.inRate into the atomic spanHz() reads, so the
    // getter doesn't race against setSampleRate's mutex-protected
    // store (Task #44 v2.2 amendment A.6).
    inRateAtomic_.store(cfg_.inRate, std::memory_order_relaxed);

    // Learn/Harvest have no UI chips.  Learn (RBN-confirmed calls -> local SCP)
    // is on for everyone by default; set LYRA_CW_LEARN=0 to opt out.  Harvest
    // (trust-tiered CW audio capture for DeepFist training) is a developer tool,
    // off unless LYRA_CW_HARVEST is set to a non-zero value.  Read once here.
    cwLearnEnabled_   = qEnvironmentVariable("LYRA_CW_LEARN",   "1") != QLatin1String("0");
    cwHarvestEnabled_ = qEnvironmentVariable("LYRA_CW_HARVEST", "0") != QLatin1String("0");

    loadDspFilterTypes();   // #159 — per-family filter-type prefs (default Linear)

    // fexchange0 output buffer: 2 * outSize_ doubles (interleaved L/R).
    outBuf_.assign(static_cast<size_t>(2 * outSize_), 0.0);
    // int16 stereo scratch for the audio-ring push (Step 3e).
    pcm16_.assign(static_cast<size_t>(2 * outSize_), 0);
    lrWire_.assign(static_cast<size_t>(2 * outSize_), 0.0);  // P4.b OutBound(0) tee buffer
    // Headroom for one in_size block + a couple of EP6 datagrams so
    // feedIq's append never reallocates in steady state.
    accum_.reserve(static_cast<size_t>(2 * (cfg_.inSize + 128)));

    // #173 — RX CW decoder wiring (faithful fldigi port).  Owned here (the
    // RX-audio + cwPitch + mode home); the pre-RX-EQ tap in dispatchAudioFrame
    // feeds it CW-mode-gated 48 kHz mono, which the decoder decimates to
    // fldigi's 8 kHz internally.  Callbacks fire on the audio thread; the emits
    // cross to the GUI via the default queued connection (CW decoder panel).
    // The detection tone == the operator's CW pitch (they tune the signal to it,
    // exactly as in fldigi — no AFC).
    cwDecoder_.setSampleRate(cfg_.outRate);
    cwDecoder_.setToneHz(cwPitchHz_);
    cwDecoder_.onText = [this](const std::string& s) {
        if (cwCaptureOn_.load(std::memory_order_relaxed))
            cwHarvester_->feedText(/*fromClassic=*/true, s);
        if (cwEngine_.load(std::memory_order_relaxed) == 2) {
            cwArbiter_.pushClassic(s);
            return;
        }
        emit cwDecodedChar(QString::fromUtf8(s.c_str(),
                                             static_cast<int>(s.size())), 1.0);
    };
    cwDecoder_.onWpm = [this](int w) {
        if (cwCaptureOn_.load(std::memory_order_relaxed))
            cwHarvester_->feedWpm(w);
        // Auto: only the engine that owns the display drives the WPM readout.
        if (cwEngine_.load(std::memory_order_relaxed) == 2 &&
            cwArbiter_.owner() != lyra::dsp::CwArbiter::Source::Classic) return;
        emit cwRxWpmChanged(w);
    };

    // DeepFist neural CW decoder (second engine).  Same 48 kHz input rate; the
    // model is loaded lazily on first switch to Neural (setCwDecodeEngine).  Its
    // callback delivers newly-committed characters (frame-timed streaming) to
    // append to the transcript.  Fires on the decoder's worker thread; the
    // queued signal marshals it to the GUI.
    neuralCw_.setSampleRate(cfg_.outRate);
    neuralCw_.onText = [this](const std::string& s) {
        if (cwCaptureOn_.load(std::memory_order_relaxed))
            cwHarvester_->feedText(/*fromClassic=*/false, s);
        if (cwEngine_.load(std::memory_order_relaxed) == 2) {
            cwArbiter_.pushDeepFist(s);
            return;
        }
        emit cwNeuralText(QString::fromUtf8(s.c_str(),
                                            static_cast<int>(s.size())));
    };
    // DeepFist CTC-lattice callsign rescorer is available but not wired to a UI
    // (the per-window "Calls" chips were removed) — leaving onCalls null skips
    // the rescore work entirely.  Re-wire here if a callsign UI returns.
    // Estimated RX WPM → the same cwRxWpmChanged surface the classic decoder uses.
    neuralCw_.onWpm = [this](int wpm) {
        if (cwCaptureOn_.load(std::memory_order_relaxed))
            cwHarvester_->feedWpm(wpm);
        // Auto: only the engine that owns the display drives the WPM readout.
        if (cwEngine_.load(std::memory_order_relaxed) == 2 &&
            cwArbiter_.owner() != lyra::dsp::CwArbiter::Source::DeepFist) return;
        emit cwRxWpmChanged(wpm);
    };
    // Auto engine: DeepFist's keying ratio drives the arbiter's fade detector,
    // and the arbiter's unified output becomes the Auto transcript (queued to
    // the GUI thread; fallback flag dims Classic-during-fade runs in the panel).
    neuralCw_.onKeying = [this](float r) {
        if (cwEngine_.load(std::memory_order_relaxed) == 2)
            cwArbiter_.updateKeying(r);
        if (cwCaptureOn_.load(std::memory_order_relaxed))
            cwHarvester_->feedKeying(r, QDateTime::currentSecsSinceEpoch());
    };
    cwArbiter_.onOutput = [this](const std::string& s, bool fallback) {
        emit cwAutoText(QString::fromUtf8(s.c_str(),
                                          static_cast<int>(s.size())), fallback);
    };
    // Phase 2 harvest: a DeepFist->Classic ownership switch IS the fade event.
    cwArbiter_.onOwnerChange = [this](lyra::dsp::CwArbiter::Source from,
                                      lyra::dsp::CwArbiter::Source to) {
        if (from == lyra::dsp::CwArbiter::Source::DeepFist &&
            to   == lyra::dsp::CwArbiter::Source::Classic  &&
            cwCaptureOn_.load(std::memory_order_relaxed))
            cwHarvester_->triggerFade(QDateTime::currentSecsSinceEpoch());
    };

    // 5 Hz UI poll: emit levelsChanged so the QML audioDbFs binding
    // re-reads the atomic (mirrors HL2Stream's statsTimer cadence).
    levelsTimer_.setInterval(200);
    connect(&levelsTimer_, &QTimer::timeout, this, [this]() {
        emit levelsChanged();
        // Refresh the zero-beat needle at the meter cadence while it's active
        // (the estimator updates the atomics ~12 Hz on the RX worker).
        if (zeroBeatEnabled_.load(std::memory_order_relaxed))
            emit zeroBeatChanged();
        // Captured-profile capture progress: the RX worker updates the
        // atomics inside feedIq; surface them to the UI on this poll.
        // Emit while a capture is running and once on its falling edge.
        const bool cap = noiseCapturing_.load(std::memory_order_relaxed);
        if (cap || cap != npLastCapturing_) {
            npLastCapturing_ = cap;
            emit noiseCaptureChanged();
        }
    });

    // Step 3e: enumerate the operator's PC output devices + default.  Guarded,
    // and skippable via LYRA_SAFE (safe-boot): a wedged virtual audio device is
    // a prime cause of a pre-window startup fault, and this runs before the main
    // window exists.  An empty list degrades to the HL2 codec / system default —
    // every deviceIndex_ consumer bounds-checks it.
    if (qEnvironmentVariableIsSet("LYRA_SAFE")) {
        qWarning("[audio] LYRA_SAFE set — skipping PC output-device enumeration");
        devices_.clear();
        deviceIndex_ = 0;
    } else {
        qInfo("[audio] enumerating PC output devices...");
        QAudioDevice def;
        if (enumerateOutputsGuarded(&devices_, &def)) {
            qInfo("[audio] %lld PC output device(s) enumerated",
                  static_cast<long long>(devices_.size()));
            for (int i = 0; i < devices_.size(); ++i) {
                if (devices_[i].id() == def.id()) {
                    deviceIndex_ = i;
                    break;
                }
            }
        } else {
            qWarning("[audio] output-device enumeration FAULTED — continuing "
                     "with no PC output devices; pick one in Settings -> Audio. "
                     "A wedged virtual audio device (e.g. VoiceMeeter) is the "
                     "usual cause.");
            devices_.clear();
            deviceIndex_ = 0;
        }
    }

    // Radio memory: restore the operator's volume + output device.
    // Restore the operator's last mute state (default UNMUTED — the
    // audio path is now level-matched and the volume taper keeps the
    // startup level moderate).  Device matched by description; falls
    // back to the system default (set above) if it's no longer present.
    QSettings s;
    muted_.store(s.value(QStringLiteral("audio/muted"), false).toBool(),
                 std::memory_order_relaxed);
    // Auto-mute-on-TX (task #26) defaults TRUE — safer first-launch
    // posture.  Operator can turn it off in Settings -> Hardware ->
    // Transmit for ESSB monitoring or any other listen-to-myself
    // workflow.  txMuted_ stays false at boot (no live MOX yet).
    autoMuteOnTx_.store(
        s.value(QStringLiteral("audio/autoMuteOnTx"), true).toBool(),
        std::memory_order_relaxed);
    // RX-on-unkey delay: deferred un-mute on the keyup MOX-off edge.
    rxResumeDelayMs_.store(std::clamp(
        s.value(QStringLiteral("audio/rxResumeDelayMs"), 50).toInt(), 0, 500),
        std::memory_order_relaxed);
    rxResumeTimer_.setSingleShot(true);
    connect(&rxResumeTimer_, &QTimer::timeout, this,
            [this]() { applyTxMuted_(false); });
    volume_.store(std::clamp(
        s.value(QStringLiteral("audio/volume"), 0.65).toDouble(), 0.0, 1.0),
        std::memory_order_relaxed);
    afGainDb_ = std::clamp(
        s.value(QStringLiteral("audio/afGainDb"), 0.0).toDouble(), 0.0, 40.0);
    balance_.store(std::clamp(
        s.value(QStringLiteral("audio/balance"), 0.0).toDouble(), -1.0, 1.0),
        std::memory_order_relaxed);
    // #90 TX monitor — restore the MON toggle + level (default OFF, 0.5).
    monEnabled_.store(s.value(QStringLiteral("audio/monEnabled"), false).toBool(),
                      std::memory_order_relaxed);
    monVolume_.store(std::clamp(
        s.value(QStringLiteral("audio/monVolume"), 0.5).toDouble(), 0.0, 1.0),
        std::memory_order_relaxed);
    const QString savedDev =
        s.value(QStringLiteral("audio/deviceName")).toString();
    if (!savedDev.isEmpty()) {
        for (int i = 0; i < devices_.size(); ++i) {
            if (devices_[i].description() == savedDev) {
                deviceIndex_ = i;
                break;
            }
        }
    }
    // Output routing: HL2 onboard codec (default — old Lyra's HL2 path)
    // unless the operator previously chose a PC device.
    hl2Out_ = s.value(QStringLiteral("audio/output"),
                      QStringLiteral("hl2")).toString() != QLatin1String("pc");
    // #158 — VAC1 persisted state (Settings → Audio).  Applied at stream
    // open (rebuildVac1 in openRx1) + live via the setters below.
    vac1Enabled_  = s.value(QStringLiteral("vac1/enabled"), false).toBool();
    vac1AutoDigital_ = s.value(QStringLiteral("vac1/autoDigital"), false).toBool();
    vac1OutName_  = s.value(QStringLiteral("vac1/outputDevice")).toString();
    vac1InName_   = s.value(QStringLiteral("vac1/inputDevice")).toString();
    // #158 DL-3 — chosen PortAudio host API ("Driver"); empty → first WASAPI.
    vac1HostApiName_ = s.value(QStringLiteral("vac1/hostApi")).toString();
    vac1RxGainDb_ = std::clamp(
        s.value(QStringLiteral("vac1/rxGainDb"), 0.0).toDouble(), -60.0, 20.0);
    vac1TxGainDb_ = std::clamp(
        s.value(QStringLiteral("vac1/txGainDb"), 3.0).toDouble(), -60.0, 20.0);
    // VAC latency posture (#158 follow-up) — rmatchV ring depth (ms) + PA block
    // size (frames).  Operator-tunable to squeeze ARQ turnaround (VarAC); also
    // carried per-profile (schema v5).  Defaults = reference (120 ms / 2048).
    vac1LatencyMs_ = std::clamp(
        s.value(QStringLiteral("vac1/latencyMs"), 120).toInt(), 5, 500);
    vac1VacSize_ = std::clamp(
        s.value(QStringLiteral("vac1/vacSize"), 2048).toInt(), 64, 8192);
    // Mono-combine the captured VAC input (I=Q=L+R) before the TX modulator,
    // matching the reference VAC "combine input" + the TCI mic convention
    // (#67).  Default ON so a mic routed to either VAC channel reaches the
    // SSB modulator; OFF feeds raw stereo L->I / R->Q.
    vac1CombineInput_ = s.value(QStringLiteral("vac1/combineInput"), true).toBool();
    // #161 — mute also silences the VAC RX feed (reference MuteWillMuteVAC1).
    // Default ON so the operator mute behaves like the reference out of the box
    // (digital ops who want the cable to keep flowing while muting the room
    // turn it OFF in Settings → Audio).
    muteWillMuteVac_.store(
        s.value(QStringLiteral("vac1/muteWillMuteVac"), true).toBool(),
        std::memory_order_relaxed);
    cwPitchHz_ = std::clamp(
        s.value(QStringLiteral("dsp/cwPitchHz"), 600).toInt(), 200, 1500);
    // RX DSP operator state (NR + AGC mode).  Defaults match old Lyra's
    // first-light: NR off, Mode 3 (MMSE-LSA), AEPF on, NPE OSMS, AGC med.
    nrEnabled_   = s.value(QStringLiteral("dsp/nrEnabled"), false).toBool();
    nrMode_      = std::clamp(
        s.value(QStringLiteral("dsp/nrMode"), 3).toInt(), 1, 4);
    aepfEnabled_ = s.value(QStringLiteral("dsp/aepf"), true).toBool();
    npeMethod_   = std::clamp(
        s.value(QStringLiteral("dsp/npeMethod"), 0).toInt(), 0, 1);
    agcMode_     = s.value(QStringLiteral("dsp/agcMode"),
                           QStringLiteral("med")).toString();
    anfEnabled_  = s.value(QStringLiteral("dsp/anfEnabled"), false).toBool();
    lmsEnabled_  = s.value(QStringLiteral("dsp/lmsEnabled"), false).toBool();
    lmsStrength_ = std::clamp(
        s.value(QStringLiteral("dsp/lmsStrength"), 0.5).toDouble(), 0.0, 1.0);
    // Manual notches — each persisted as "offsetHz|widthHz|active".
    squelchEnabled_ = s.value(QStringLiteral("dsp/squelchEnabled"), false).toBool();
    squelchThreshold_ = std::clamp(
        s.value(QStringLiteral("dsp/squelchThreshold"), 0.20).toDouble(), 0.0, 1.0);
    nbEnabled_  = s.value(QStringLiteral("dsp/nbEnabled"), false).toBool();
    nbStrength_ = std::clamp(
        s.value(QStringLiteral("dsp/nbStrength"), 0.5).toDouble(), 0.0, 1.0);
    apfEnabled_ = s.value(QStringLiteral("dsp/apfEnabled"), false).toBool();
    apfGainDb_  = std::clamp(
        s.value(QStringLiteral("dsp/apfGainDb"), 12.0).toDouble(), 0.0, 24.0);
    binEnabled_ = s.value(QStringLiteral("dsp/binEnabled"), false).toBool();
    binDepth_   = std::clamp(
        s.value(QStringLiteral("dsp/binDepth"), 0.7).toDouble(), 0.0, 1.0);
    buildBinaural();   // Hilbert FIR + history (rate-agnostic, built once)
    notchEnabled_ = s.value(QStringLiteral("dsp/notchEnabled"), false).toBool();
    const QStringList savedNotches =
        s.value(QStringLiteral("dsp/notches")).toStringList();
    for (const QString &entry : savedNotches) {
        const QStringList p = entry.split(QLatin1Char('|'));
        if (p.size() == 3)
            notches_.push_back({p[0].toDouble(),
                                std::max(10.0, p[1].toDouble()),
                                p[2].toInt() != 0});
    }
    recomputePassband();   // seed passband edges from the loaded mode/bw/pitch
    // Captured-profile settings + saved profiles.
    {
        const int fft = s.value(QStringLiteral("dsp/npFftSize"), 4096).toInt();
        npFftSize_ = (fft == 2048 || fft == 4096 || fft == 8192) ? fft : 4096;
        const double sec = s.value(QStringLiteral("dsp/npCaptureSeconds"), 5.0).toDouble();
        npCaptureSeconds_ = (sec == 3.0 || sec == 5.0 || sec == 10.0) ? sec : 5.0;
        npAlpha_     = std::clamp(s.value(QStringLiteral("dsp/npAlpha"), 1.0).toDouble(), 1.0, 5.0);
        npFloorDb_   = std::clamp(s.value(QStringLiteral("dsp/npFloorDb"), -12.0).toDouble(), -30.0, -3.0);
        npSmoothing_ = std::clamp(s.value(QStringLiteral("dsp/npSmoothing"), 0.6).toDouble(), 0.0, 0.95);
    }
    loadProfiles();
}

WdspEngine::~WdspEngine()
{
    // Reverse-destruction-order hazard: neuralCw_ is declared (wdsp_engine.h)
    // BEFORE cwArbiter_ and the cwHarvest*/cwHarvester_ members, so members
    // destruct in the OPPOSITE order — cwHarvester_, cwHarvestDecim_,
    // cwHarvestRing_ and cwArbiter_ would all tear down BEFORE neuralCw_'s own
    // destructor (which stops its worker thread) ever runs. neuralCw_'s
    // worker fires onText/onWpm/onKeying synchronously off ITS OWN thread
    // (wired in setCwDecodeEngine above), and those lambdas dereference
    // cwHarvester_ (when cwCaptureOn_ is true) and unconditionally call
    // cwArbiter_.updateKeying — so without stopping that worker first, a
    // decode callback can land on an already-(or mid-)destroyed
    // harvester/arbiter. Explicitly stopping+joining it HERE, before any
    // harvest/arbiter member is touched, guarantees no callback is left in
    // flight; it covers cwArbiter_ "for free" since the worker thread is the
    // only caller of updateKeying(). neuralCw_'s own destructor calls
    // stopWorker() again once neuralCw_ itself destructs further down — that
    // second call is a safe no-op (stop() -> stopWorker() is idempotent via
    // running_.exchange(false)).
    cwCaptureOn_.store(false, std::memory_order_relaxed);  // no new harvester work
    neuralCw_.stop();                                      // join: no callback in flight after this

    // Phase 2 harvest — stop the pump worker before the rest of teardown so
    // it can't touch a half-torn-down engine.
    cwHarvestRun_.store(false);
    if (cwHarvestWorker_.joinable()) cwHarvestWorker_.join();
    closeRx1();
}

void WdspEngine::emitLog(const QString &line)
{
    qInfo("%s", qPrintable(line));
    emit logLine(line);
}

// ── Task #44 Phase 2 — analyzer (re)configuration helpers ────────────
//
// Factored out of openRx1()'s original inline SetAnalyzer block.  The
// MOX-edge swap (Phase 2) calls configureAnalyzerForTx() on keydown
// to retune kAnDisp for the WDSP TX sip1 ring (96 kHz dsp_rate,
// ~256-sample per-call reads), and configureAnalyzerForRx() on keyup
// to restore the RX-side config (cfg_.inRate, cfg_.inSize block).
//
// PRECONDITION (both helpers): channelMtx_ held by caller.  Matches
// the openRx1() convention (openRx1 expects setSampleRate or whoever
// drove the reopen to hold the lock; these helpers run on the same
// thread under that same lock).  Both helpers are no-ops if
// analyzerOpen_ is false (XCreateAnalyzer never landed) or the
// SetAnalyzer cdef isn't resolved.

void WdspEngine::configureAnalyzerForRx() noexcept
{
    if (!analyzerOpen_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    if (!api.SetAnalyzer) return;

    // Take analyzerMtx_ so feedTxSpectrumFromSip1() can't slip a
    // Spectrum0 between this SetAnalyzer reconfigure and the
    // txAnalyzerBfSize_ atomic store below (amendment A.5).
    std::lock_guard<std::mutex> lk(analyzerMtx_);

    // overlap + max_w per the frame-rate formula.  max_w sizes an
    // internal display-history buffer — passing 0 makes WDSP crash on
    // a zero-size allocation.  overlap clamps to 0 at 192 kHz / 4096
    // (samples-per-frame >> fft).
    const int overlap =
        std::max(0, kAnFftSize - cfg_.inRate / kAnFrameRate);
    const int maxW = kAnFftSize +
        std::min(cfg_.inRate / 10, kAnFftSize * kAnFrameRate / 10);
    // flp = per-FFT high-side-LO flags (int* vector).  One FFT, not
    // high-side -> {0}.  MUST be a real pointer (passing an int
    // crashes WDSP — it dereferences flp[i]).
    int flp[1] = {0};
    api.SetAnalyzer(
        kAnDisp,                    // disp
        1,                          // n_pixout
        1,                          // n_fft (spur-elim ffts)
        1,                          // typ = complex IQ
        flp,                        // flp (int* high-side LO flags)
        kAnFftSize,                 // sz (fft size)
        cfg_.inSize,                // bf_sz (RX worker feed block)
        kAnWindow,                  // win_type
        kAnKaiserPi,                // pi (kaiser)
        overlap,                    // ovrlp
        0,                          // clp (clip bins/side)
        0.0, 0.0,                   // fsc_lin, fsc_hin (DOUBLE)
        kAnPixels,                  // n_pix (display points)
        1,                          // n_stch (stitches)
        0,                          // calset
        0.0, 0.0,                   // fmin, fmax
        maxW);                      // max_w (history buffer size)
    // Tell the analyzer its sample rate explicitly (matches the
    // reference SpecHPSDR.SampleRate setter at specHPSDR.cs:453
    // which calls SetDisplaySampleRate).  Required so that any
    // WDSP-internal rate-derived math (freq bin width, etc.) is
    // correct after a rate change.  (Task #44 Phase 2 v2.3.1 fix —
    // operator bench showed broken spectrum without this call.)
    if (api.SetDisplaySampleRate) {
        api.SetDisplaySampleRate(kAnDisp, cfg_.inRate);
    }
    txAnalyzerBfSize_.store(cfg_.inSize, std::memory_order_relaxed);
    if (api.SetDisplayDetectorMode) {
        api.SetDisplayDetectorMode(kAnDisp, 0, kAnDetector);
    }
    if (api.SetDisplayAverageMode) {
        api.SetDisplayAverageMode(kAnDisp, 0, kAnAvgMode);
    }
    if (kAnAvgMode != 0) {
        const double avb =
            std::exp(-1.0 / (kAnFrameRate * kAnTau));
        const int numAvg = std::max(2,
            std::min(60, static_cast<int>(kAnFrameRate * kAnTau)));
        if (api.SetDisplayAvBackmult) {
            api.SetDisplayAvBackmult(kAnDisp, 0, avb);
        }
        if (api.SetDisplayNumAverage) {
            api.SetDisplayNumAverage(kAnDisp, 0, numAvg);
        }
    }
}

void WdspEngine::configureAnalyzerForTx() noexcept
{
    if (!analyzerOpen_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    if (!api.SetAnalyzer) return;

    // analyzerMtx_ — symmetric with configureAnalyzerForRx().
    std::lock_guard<std::mutex> lk(analyzerMtx_);

    // TX-state sizing — REFERENCE MECHANISM (v2.3): the WDSP sip1
    // siphon auto-feeds Spectrum0 via TXASetSipMode(1)+TXASetSipDisplay
    // (set in setTxOwnsAnalyzer below).  bf_sz MUST match what xsiphon
    // delivers per call = a->insize = the TX channel's dsp_size (set
    // via setSize_siphon at TXA.c:733).  Lyra's TX channel uses
    // kDspSize=4096 (tx_channel.cpp:57).  v2.3.0 used kBlockTx=256
    // (leftover from the v2.2 manual-pull mechanism's per-call size)
    // — wrong by 16x → operator bench showed comb-of-spurs across the
    // visible band.  v2.3.1 fixes by matching the actual feed size.
    //
    // sip1 lives at dsp_rate=96 kHz (TXA.c:586
    // captures pre-iqc + pre-rsmpout); Lyra TX worker pulls ~256
    // samples per EP2-cadence tick via TXAGetaSipF1 — matches the
    // sip1 production rate (96000 / 381 Hz ≈ 252).
    //
    // §15.29 (2026-06-03): TX-state analyzer parameters match the
    // verified reference's Setup → Display → TX values EXACTLY:
    //   * FFT size 32768 (Bin Width 2.93 Hz at 96 kHz) — 8× finer
    //     resolution than the RX default 4096.  Reference uses this
    //     finer-bin display only on TX state (the operator-captured
    //     Setup screenshot shows 32768 in the TX panel; RX1 + RX2
    //     panels are configured separately at 4096 there too).
    //   * Averaging Log Recursive (av_mode=3) with 30 ms tau.  The
    //     reference's dB-space IIR (analyzer.c:540-549) smooths the
    //     per-frame Peak detector output across time, damping
    //     transient bin excursions that otherwise read as a wide
    //     red/orange "wash" on voice + dead-key content.
    // Both addressed together because they interact: coarser bins
    // (4096) concentrate per-pixel peaks more dramatically, and
    // without averaging those peaks fan out across the trace.
    // Fixing only one would only partially close the gap to
    // reference visual parity.  See §15.29 for the diagnostic chain.
    constexpr int    kRateTx       = 96000;
    constexpr int    kBlockTx      = 4096;   // = TX dsp_size; matches xsiphon a->insize feed
    constexpr int    kTxFftSize    = 32768;  // matches reference's Display → TX FFT Size
    constexpr int    kTxAvgMode    = 3;      // Log Recursive (dB-space IIR)
    constexpr double kTxPanaTauSec = 0.030;  // 30 ms — reference's panadapter tau
    constexpr double kTxWfTauSec   = 0.120;  // 120 ms — reference's waterfall tau (4× smoother)
    const int overlap =
        std::max(0, kTxFftSize - kRateTx / kAnFrameRate);
    const int maxW = kTxFftSize +
        std::min(kRateTx / 10, kTxFftSize * kAnFrameRate / 10);
    int flp[1] = {0};
    api.SetAnalyzer(
        kAnDisp,
        2,                          // §15.29 C1 — n_pixout=2 (was 1); pixout 0
                                    // = panadapter, pixout 1 = waterfall.
                                    // Reference does the same — independent
                                    // detector/avg/tau per pixout per
                                    // specHPSDR.cs:262-263/308/320/361-362/
                                    // 377-378.  copyWaterfallSpectrum reads
                                    // pixout=1 during TX; copySpectrum
                                    // stays on pixout=0 (panadapter).
        1, 1, flp,
        kTxFftSize,                 // §15.29 — 32768 (was kAnFftSize=4096)
        kBlockTx,                   // bf_sz (TX worker per-call sip1 read)
        kAnWindow, kAnKaiserPi,
        overlap, 0,
        0.0, 0.0,
        kAnPixels, 1, 0,
        0.0, 0.0,
        maxW);
    // Tell the analyzer the TX-state sample rate (matches reference
    // SpecHPSDR.SampleRate setter at specHPSDR.cs:453 + the explicit
    // radio.cs:2618 `.SampleRate = 96000` for the TX disp).  This is
    // the WDSP-internal rate the analyzer uses for bin-width math.
    if (api.SetDisplaySampleRate) {
        api.SetDisplaySampleRate(kAnDisp, kRateTx);
    }
    txAnalyzerBfSize_.store(kBlockTx, std::memory_order_relaxed);
    txSpanHz_.store(kRateTx, std::memory_order_relaxed);

    // §15.29 C1 — configure BOTH pixout=0 (panadapter) AND pixout=1
    // (waterfall) independently.  Reference architecture per
    // specHPSDR.cs:262-263/308/320/361-362/377-378 — same detector
    // mode but different averaging tau (30 ms pana / 120 ms wf) so
    // the waterfall is ~4× smoother than the panadapter.  WDSP keeps
    // separate averaging state per pixout; copyWaterfallSpectrum
    // reads pixout=1 during TX, leaves pixout=0 to copySpectrum
    // (panadapter caller).
    //
    // avb (IIR backmult) formula matches reference specHPSDR.cs:
    // 357-364: avb = exp(-1/(frame_rate * tau)).  Lyra's 60 fps:
    //   pana 30 ms  → avb = exp(-1/(60*0.030))  ≈ 0.534
    //   wf   120 ms → avb = exp(-1/(60*0.120))  ≈ 0.871 (heavier IIR)
    // numAvg = display-history depth, clamped [2, 60] per reference.
    auto pushAvgConfig = [&](int pixout, double tau) {
        if (api.SetDisplayDetectorMode) {
            api.SetDisplayDetectorMode(kAnDisp, pixout, kAnDetector);
        }
        if (api.SetDisplayAverageMode) {
            api.SetDisplayAverageMode(kAnDisp, pixout, kTxAvgMode);
        }
        if (kTxAvgMode != 0) {
            const double avb = std::exp(-1.0 / (kAnFrameRate * tau));
            const int numAvg = std::max(2,
                std::min(60, static_cast<int>(kAnFrameRate * tau)));
            if (api.SetDisplayAvBackmult) {
                api.SetDisplayAvBackmult(kAnDisp, pixout, avb);
            }
            if (api.SetDisplayNumAverage) {
                api.SetDisplayNumAverage(kAnDisp, pixout, numAvg);
            }
        }
    };
    pushAvgConfig(0, kTxPanaTauSec);   // panadapter 30 ms
    pushAvgConfig(1, kTxWfTauSec);     // waterfall  120 ms

    // §15.29 C1 — signal the waterfall caller that pixout=1 is now
    // configured + ready to read.  copyWaterfallSpectrum checks this
    // flag (via txOwnsAnalyzer_ which is set after this function
    // returns, in setTxOwnsAnalyzer) and switches its GetPixels
    // index from 0 to 1.  RX state stays on n_pixout=1 (pixout=0
    // only) — waterfall continues to share the panadapter's
    // pixout=0 in RX (no operator-visible issue there per §15.29
    // C2 deferred).
}

void WdspEngine::setTxOwnsAnalyzer(bool on)
{
    {
        // channelMtx_ — lifecycle-vs-feed serializer (matches the
        // openRx1 / configureAnalyzer caller-holds convention).
        // Inside the block, configureAnalyzerForRx/Tx ALSO take
        // analyzerMtx_ for the SetAnalyzer-vs-feed serializer.
        // Lock-order: channelMtx_ BEFORE analyzerMtx_ (documented
        // in wdsp_engine.h members section, never violated).
        std::lock_guard<std::mutex> lk(channelMtx_);
        if (on) {
            configureAnalyzerForTx();
        } else {
            configureAnalyzerForRx();
        }
    }
    // Reference-faithful TX panadapter feed mechanism (cmaster.cs:
    // 539-540 + wdsp/siphon.c:130).  TXASetSipMode(1, mode) puts
    // the WDSP TX sip1 siphon in mode=1 "internally call Spectrum0
    // every xsiphon" mode (when mode=1) or back to mode=0 idle.
    // TXASetSipDisplay(1, disp) points the auto-feed at our
    // panadapter analyzer (kAnDisp).  WDSP then feeds the analyzer
    // for free every xtxa cycle at dsp_rate (96 kHz) — which only
    // fires while the TX channel is running (= during MOX).  Zero
    // per-block CPU cost on Lyra's side; matches reference exactly.
    //
    // Pre-iqc tap point (xsiphon at TXA.c:586 is upstream of xiqc
    // at TXA.c:587), so the panadapter trace stays clean whether
    // PureSignal is on or off — reference's PS-faithful posture.
    //
    // TX channel must be open (txa[1].sip1.p valid) for these
    // calls to be safe.  setTxOwnsAnalyzer fires from the
    // moxActiveChanged signal AFTER the TR-settled edge (post
    // mox_delay + rf_delay on keydown; post ptt_out_delay on
    // keyup) — by the time we run, the TX channel has been
    // opened (Lyra opens it at startup and keeps it open across
    // the session) so the siphon pointer is valid.
    if (wdsp_) {
        const WdspApi &api = wdsp_->api();
        if (api.TXASetSipMode && api.TXASetSipDisplay) {
            // Defensive ordering: when turning ON, set the target
            // disp FIRST (so any imminent xsiphon call finds the
            // right disp), then enable mode=1.  When turning OFF,
            // disable mode=0 first (so no further auto-feeds), then
            // leave the disp setting alone (idempotent if we re-arm).
            if (on) {
                api.TXASetSipDisplay(/*channel=*/1, kAnDisp);
                api.TXASetSipMode(/*channel=*/1, 1);
            } else {
                api.TXASetSipMode(/*channel=*/1, 0);
            }
        }
    }
    // Release-store so the SetAnalyzer reconfigure side-effects
    // happen-before any reader's acquire-load of the flag (per
    // amendment A.2).  Used by RX worker's feedIq to skip its
    // own Spectrum0 call while TX owns the analyzer (otherwise
    // both feeds collide on the same disp).
    txOwnsAnalyzer_.store(on, std::memory_order_release);
    emit spanChanged();
}

bool WdspEngine::openRx1()
{
    if (opened_) {
        return true;  // idempotent
    }
    if (!wdsp_ || !wdsp_->isLoaded()) {
        emitLog(QStringLiteral(
            "[wdsp] engine: cannot open RX1 — DLL not loaded"));
        return false;
    }

    const WdspApi &api = wdsp_->api();
    if (!api.OpenChannel || !api.SetChannelState || !api.SetRXAMode ||
        !api.RXASetPassband || !api.SetRXAAGCMode ||
        !api.SetRXAPanelBinaural) {
        emitLog(QStringLiteral(
            "[wdsp] engine: cannot open RX1 — required symbols not "
            "resolved"));
        return false;
    }

    // OpenChannel(channel, in_size, dsp_size, in_rate, dsp_rate,
    //             out_rate, type=RX(0), state=stopped(0),
    //             tdelayup, tslewup, tdelaydown, tslewdown, block).
    api.OpenChannel(channel_, cfg_.inSize, cfg_.dspSize,
                    cfg_.inRate, cfg_.dspRate, cfg_.outRate,
                    0,   // type = RX
                    0,   // state = stopped — we start it explicitly below
                    cfg_.tDelayUp, cfg_.tSlewUp,
                    cfg_.tDelayDown, cfg_.tSlewDown,
                    cfg_.block);
    opened_ = true;
    applyDspFilterTypes();   // #159 — apply per-family filter type on open
    // Binaural OFF (arg 0) => WDSP panel.copy=1 => I copied to Q at the
    // panel output => mono on BOTH L/R regardless of any upstream stage
    // that zeroed Q (e.g. EMNR).  This is the verified-correct default
    // + the AM/FM/DSB right-channel-silent fix (CLAUDE.md §14.10).
    api.SetRXAPanelBinaural(channel_, 0);

    // Demod mode + passband from the operator's current selection
    // (default USB 2.4 kHz; persisted via Prefs and applied before the
    // channel started running).  RXASetPassband updates NBP0
    // (front-of-chain, where sideband selection lives + always runs) +
    // BP1 + the SNBA filter in one call (§14.2).
    applyModeFilter();

    // AGC mode (operator-selected, persisted; default med).
    pushAgcMode();

    // Level calibration: replace WDSP's hot create-time AGC default
    // (max_gain = 10000 / 80 dB, which overshoots 0 dBFS) with a
    // threshold-computed ceiling.  SetRXAAGCThresh derives max_gain
    // from (thresh, size, rate) + the slope-derived var_gain; we must
    // NOT also call SetRXAAGCTop (same field, would clobber).
    if (api.SetRXAAGCSlope) {
        api.SetRXAAGCSlope(channel_, kAgcSlope);
    }
    if (api.SetRXAAGCThresh) {
        api.SetRXAAGCThresh(channel_, kAgcThreshDbFs, kAgcThreshFftSize,
                            static_cast<double>(cfg_.inRate));
    }
    // Panel (post-DSP makeup) gain = the operator's AF gain (dB → linear;
    // 0 dB = unity = WDSP create_panel default).
    if (api.SetRXAPanelGain1) {
        api.SetRXAPanelGain1(channel_, std::pow(10.0, afGainDb_ / 20.0));
    }

    // RX noise reduction (EMNR) + auto-notch + LMS — persisted state.
    pushNrState();
    pushAnfState();
    pushLmsState();
    pushNotches();
    pushSquelchState();

    // Noise blanker (EXT NOB-II): create the per-channel blanker sized to
    // our IQ block + rate, then push threshold/run.  feedIq splices
    // xnobEXT on the raw IQ before fexchange0 when NB is on.  Impulse-only
    // tuning (tight slew/hang/adv); destroyed in closeRx1.
    if (api.create_nobEXT && !nbCreated_) {
        nbBuf_.assign(static_cast<size_t>(2 * cfg_.inSize), 0.0);
        api.create_nobEXT(channel_, 0, 0, cfg_.inSize,
                          static_cast<double>(cfg_.inRate),
                          0.0001, 0.0001, 0.0001, 0.020, 20.0);
        nbCreated_ = true;
    }
    pushNbState();
    pushApfState();

    // Step 5: create + configure the spectral analyzer (panadapter
    // source).  Independent of the DSP channel; fed the same IQ.
    // XCreateAnalyzer is one-time; SetAnalyzer + detector + average
    // mode config now factored into configureAnalyzerForRx() so the
    // Task #44 Phase 2 MOX-edge swap path can call it (or
    // configureAnalyzerForTx() for the TX-state sizing) under
    // channelMtx_ without copy-pasting the body.
    if (api.XCreateAnalyzer && api.SetAnalyzer) {
        int success = 0;
        char appDataPath[] = "";   // empty app-data path (no temp files)
        api.XCreateAnalyzer(kAnDisp, &success, kAnMaxFft, 1, 1, appDataPath);
        if (success == 0) {
            analyzerOpen_ = true;
            configureAnalyzerForRx();
            emitLog(QStringLiteral(
                "[wdsp] analyzer: %1 pixels, fft %2, window %3 "
                "(panadapter source)")
                .arg(kAnPixels).arg(kAnFftSize).arg(kAnWindow));
        } else {
            emitLog(QStringLiteral(
                "[wdsp] analyzer: XCreateAnalyzer failed (%1) — no "
                "panadapter").arg(success));
        }
    }

    // Step 3e: bring up PC sound-card playback BEFORE running_ goes
    // true, so feedIq sees a live ring the moment IQ starts flowing.
    // Non-fatal: if the audio device is missing/unsupported we log and
    // continue (the dBFS meter still works, just no sound).
    startAudio();

    // Stage B.6.b-retry (2026-06-08) — REFERENCE-FAITHFUL AAMix
    // initialization.  The previous B.6.b shortcut (active=0x01 at
    // create-time) shipped + silently produced NO audio at bench
    // (operator confirmed on both HL2-jack + PC sound card paths).
    // Root cause class: the reference NEVER creates an aamix with
    // inputs immediately active.  Both create_aamix call sites in
    // cmaster.c (anti-vox mixer :159-175 + global mixer :297-313)
    // pass active=0 and let SetAAudioMixState activate streams
    // LATER -- which goes through the close_mixer/open_mixer slew
    // atom + start_mixthread inside open_mixer.  My shortcut
    // bypassed that path and started mix_main directly in
    // create_aamix.  That exercises an untested code path in the
    // port (the "active at create" branch of my Stage B.2 code is
    // never exercised by the reference).
    //
    // This retry follows the reference INITIALIZATION SEQUENCE
    // verbatim:
    //   1. create_aamix(active=0)   -- NO mix_main started yet
    //   2. SetAAudioMixOutputPointer -- defensive Outbound re-set
    //                                   (cmaster.c:411)
    //   3. SetAAudioMixState(stream=0, active=1)
    //                                -- triggers close_mixer +
    //                                   activate + open_mixer,
    //                                   which calls start_mixthread
    //                                   inside the slew-orchestrated
    //                                   open atom (cmaster.c:534-536
    //                                   pattern via update_aamix_*).
    //
    // Plus reference-exact param values:
    //   - ring_size = literal 4096   (cmaster.c:306 -- 4x my prior
    //                                 4*outSize_=1024)
    //   - slew      = 0/10ms/0/10ms  (cmaster.c:310-313 -- vs my
    //                                 prior 5/5/5/5)
    //
    // Construction is here -- AFTER startAudio() (audioRing_ live
    // when Outbound first fires on PC path) and BEFORE
    // SetChannelState(channel,1) below (AAMix initialised, but
    // dormant since active=0 + nactive=0 -> no mix_main yet).
    if (aaMix_ != nullptr) {
        // Defensive: a previous closeRx1 should have cleared this.
        lyra::wire::destroy_aamix(aaMix_, 0);
        aaMix_ = nullptr;
    }
    {
        int inrates[1] = { cfg_.outRate };
        // P0.c direct port: AAMix's Outbound is the reference raw
        // fn ptr, so the hand-off is the aamixOutbound static
        // member (free-function shape) reaching this engine through
        // the TU-scope self pointer -- the same free-function +
        // global-context pattern the reference's outbound consumers
        // use.  Registered BEFORE create_aamix so the pointer is
        // live before mix_main can ever fire (mix_main starts
        // inside SetAAudioMixState's open_mixer below).  WdspEngine
        // outlives AAMix (destroyed in closeRx1 before the dtor).
        g_aamixOutboundSelf = this;
        aaMix_ = (lyra::wire::AAMIX) lyra::wire::create_aamix(
            /*id*/             0,
            /*outbound_id*/    0,
            /*ringinsize*/     outSize_,
            /*outsize*/        outSize_,
            /*ninputs*/        1,
            /*active*/         0L,         // REFERENCE: 0 not 0x01
            /*what*/           0x01L,
            /*volume*/         1.0,
            /*ring_size*/      4096,       // REFERENCE: literal 4096
            /*inrates*/        inrates,
            /*outrate*/        cfg_.outRate,
            /*Outbound*/       &WdspEngine::aamixOutbound,
            /*tdelayup*/       0.000,      // REFERENCE: 0
            /*tslewup*/        0.010,      // REFERENCE: 10ms
            /*tdelaydown*/     0.000,      // REFERENCE: 0
            /*tslewdown*/      0.010);     // REFERENCE: 10ms

        // Reference cmaster.c:411 defensive Outbound re-set --
        // safe regardless of whether create_aamix already stored it.
        lyra::wire::SetAAudioMixOutputPointer(aaMix_, 0,
                                              &WdspEngine::aamixOutbound);

        // Reference activation pattern (cmaster.c:534-536):
        //   SetAAudioMixState(ptr, id, mix_in_id, 0)   // ensure inactive
        //   SetAAudioStreamRate(ptr, id, mix_in_id, rate)  // rate match
        //   SetAAudioMixState(ptr, id, mix_in_id, 1)   // activate
        // We skip the explicit inactive step (already inactive from
        // create_aamix(active=0)) and the rate setter (inrate ==
        // outrate so SetAAudioStreamRate is a no-op for stream 0).
        // The activation call below is what triggers the
        // close_mixer/open_mixer atom -- the bench-validated
        // reference path that brings mix_main online.
        lyra::wire::SetAAudioMixState(aaMix_, 0, /*stream*/ 0,
                                      /*active*/ 1);
    }
    emitLog(QStringLiteral(
        "[wdsp] aamix: id=0 1-input ref-faithful (outSize=%1 frames "
        "@ %2 Hz, ring=4096, slew=0/10ms; activated via "
        "SetAAudioMixState)").arg(outSize_).arg(cfg_.outRate));

    // Start the channel (state=running, dmode=0).
    api.SetChannelState(channel_, 1, 0);
    running_ = true;
    emit runningChanged();
    emit spanChanged();     // QML freq scale re-reads spanHz on start
    levelsTimer_.start();   // begin the 5 Hz audioDbFs UI poll

    emitLog(QStringLiteral(
        "[wdsp] channel 0 opened (192k IQ -> 48k audio, USB "
        "200-3000 Hz, AGC MED thr -100 dBFS, binaural mono); "
        "out_size=%1 frames").arg(outSize_));

    // #158 Stage 3 — (re)build VAC1 VAC-out now the channel + AAMix are
    // up and outSize_ is final.  applyVacEnvOnce() reads the bench enable
    // hook (LYRA_VAC1_OUT) on the first open; rebuildVac1() is idempotent
    // so a sample-rate reopen rebuilds at the new audio_size/audio_rate.
    applyVacEnvOnce();
    rebuildVac1();
    return true;
}

void WdspEngine::closeRx1()
{
    if (!opened_) {
        return;  // idempotent
    }
    // #158 Stage 3 — tear down VAC1 FIRST: clears vac1Active_ under
    // vacMtx_ (so the mix thread stops feeding xvacOUT), stops IvacAudio
    // (joins the sink → no more rmatchOUT drains), then destroy_ivac
    // frees the rings — all before the channel/AAMix teardown below.
    teardownVac1();

    const WdspApi &api = wdsp_->api();
    // Stop with dmode=1 (blocking flush) so in-flight buffers drain
    // before CloseChannel tears the channel down.
    if (api.SetChannelState) {
        api.SetChannelState(channel_, 0, 1);
    }
    // Stage B.6.b-retry: destroy AAMix BEFORE CloseChannel so
    // mix_main exits (its Outbound stops calling dispatchAudioFrame)
    // while WdspEngine sink-dispatch state remains valid.
    // destroy_aamix's first action (aamix.c:215-218) = clear run +
    // release every Ready semaphore + Sleep(2) for the thread to die.
    if (aaMix_ != nullptr) {
        lyra::wire::destroy_aamix(aaMix_, 0);
        aaMix_ = nullptr;
        // P0.c: clear the outbound context AFTER the mixer thread
        // is gone (destroy_aamix's Sleep(2) posture) so a late
        // Outbound can never run against a stale engine.
        g_aamixOutboundSelf = nullptr;
    }
    if (nbCreated_ && api.destroy_nobEXT) {
        api.destroy_nobEXT(channel_);
        nbCreated_ = false;
    }
    if (api.CloseChannel) {
        api.CloseChannel(channel_);
    }
    opened_ = false;
    if (running_) {
        running_ = false;
        emit runningChanged();
    }
    levelsTimer_.stop();
    stopAudio();
    if (analyzerOpen_ && api.DestroyAnalyzer) {
        api.DestroyAnalyzer(kAnDisp);
        analyzerOpen_ = false;
    }
    audioDbFs_.store(-200.0, std::memory_order_relaxed);
    accum_.clear();
    emitLog(QStringLiteral("[wdsp] channel 0 closed"));
}

// ── #158 Stage 3 — VAC1 VAC-out lifecycle ────────────────────────────

// Optional dev/bench env OVERRIDE of the persisted (Settings) VAC1 state,
// applied once at first stream open.  LYRA_VAC1_OUT = enable + select the
// PC output device (exact or substring of the description); LYRA_VAC1_
// VAC_SIZE = the PC-side rmatch block (64..8192); LYRA_VAC1_RX_GAIN_DB =
// RX gain (dB).  Normally unset — the Settings → Audio VAC1 controls are
// the source of truth (loaded from QSettings in the ctor).
void WdspEngine::applyVacEnvOnce()
{
    if (vacEnvApplied_) {
        return;
    }
    vacEnvApplied_ = true;

    const QByteArray sel = qgetenv("LYRA_VAC1_OUT");
    if (!sel.isEmpty()) {
        vac1Enabled_ = true;
        vac1OutName_ = QString::fromLocal8Bit(sel).trimmed();
        emitLog(QStringLiteral("[vac1] env override LYRA_VAC1_OUT='%1'")
                    .arg(vac1OutName_));
    }
    const QByteArray vs = qgetenv("LYRA_VAC1_VAC_SIZE");
    if (!vs.isEmpty()) {
        bool ok = false;
        const int n = vs.toInt(&ok);
        if (ok) {
            vac1VacSize_ = std::clamp(n, 64, 8192);
        }
    }
    const QByteArray rg = qgetenv("LYRA_VAC1_RX_GAIN_DB");
    if (!rg.isEmpty()) {
        bool ok = false;
        const double db = rg.toDouble(&ok);
        if (ok) {
            vac1RxGainDb_ = std::clamp(db, -60.0, 20.0);
        }
    }
}

// Resolve the persisted device description to a current QMediaDevices
// output index: exact match first, then case-insensitive substring (so an
// env/legacy short name still resolves).  -1 if not present.
// #158 DL-2 — resolve the operator's chosen VAC device NAMES (still the
// Qt QMediaDevices descriptions the Settings combos list; DL-3 swaps those
// for native PA enumeration) to PortAudio (host-API, host-API-RELATIVE
// device) indices for StartAudioIVAC.  The reference runs the VAC as ONE
// full-duplex stream on ONE host API, so input AND output must resolve
// under the SAME host API — we use WASAPI (the operator's VAC cables + the
// reference's posture; PA WASAPI device names are the Windows endpoint
// friendly names Qt also shows, so the strings match for VB/VAC cables).
// Name match: exact first, then case-insensitive either-contains.
struct VacPaSel { int hostApi = -1; int outDev = -1; int inDev = -1; };

// Host-API-relative device index whose PA name matches `name` and that has
// the wanted direction's channels; -1 if none.
static int matchPaDeviceInHostApi(int hostApi, const QString &name, bool wantOutput)
{
    if (name.isEmpty()) {
        return -1;
    }
    const PaHostApiInfo *ha = Pa_GetHostApiInfo(hostApi);
    if (!ha) {
        return -1;
    }
    int partial = -1;
    for (int j = 0; j < ha->deviceCount; ++j) {
        const int dev = Pa_HostApiDeviceIndexToDeviceIndex(hostApi, j);
        if (dev < 0) {
            continue;
        }
        const PaDeviceInfo *di = Pa_GetDeviceInfo(dev);
        if (!di) {
            continue;
        }
        if (wantOutput ? di->maxOutputChannels <= 0 : di->maxInputChannels <= 0) {
            continue;
        }
        const QString dn = QString::fromUtf8(di->name);
        if (dn == name) {
            return j;  // exact wins
        }
        if (partial < 0 &&
            (dn.contains(name, Qt::CaseInsensitive) ||
             name.contains(dn, Qt::CaseInsensitive))) {
            partial = j;
        }
    }
    return partial;
}

// #158 DL-3 — PA host-API index for a stored host-API NAME (the operator's
// "Driver" pick).  Empty/unmatched name → first WASAPI (the DL-2 default +
// migration path for configs saved before DL-3).  -1 if even WASAPI absent.
// Ensures PortAudio is initialized first (idempotent), so the Settings
// pickers populate even before the radio is connected.
static int paHostApiIndexForName(const QString &name)
{
    lyra::wire::ivacInitPortAudio();
    const int nApi = Pa_GetHostApiCount();
    if (!name.isEmpty()) {
        for (int i = 0; i < nApi; ++i) {
            const PaHostApiInfo *ha = Pa_GetHostApiInfo(i);
            if (ha && QString::fromUtf8(ha->name) == name) {
                return i;
            }
        }
    }
    for (int i = 0; i < nApi; ++i) {   // fallback: first WASAPI
        const PaHostApiInfo *ha = Pa_GetHostApiInfo(i);
        if (ha && ha->type == paWASAPI) {
            return i;
        }
    }
    return -1;
}

// #158 DL-3 — host APIs exposing >=1 device: display names + parallel PA
// indices (for the Settings "Driver" picker, Thetis-faithful).
static void paHostApisWithDevices(QStringList &names, QList<int> &paIndices)
{
    lyra::wire::ivacInitPortAudio();
    const int nApi = Pa_GetHostApiCount();
    for (int i = 0; i < nApi; ++i) {
        const PaHostApiInfo *ha = Pa_GetHostApiInfo(i);
        if (ha && ha->deviceCount > 0) {
            names << QString::fromUtf8(ha->name);
            paIndices << i;
        }
    }
}

// #158 DL-3 — device display names under one PA host API, filtered by
// direction, in host-API-relative order (the order StartAudioIVAC's
// Pa_HostApiDeviceIndexToDeviceIndex expects).
static QStringList paDevicesForHostApi(int paHostApi, bool wantOutput)
{
    lyra::wire::ivacInitPortAudio();
    QStringList out;
    const PaHostApiInfo *ha = Pa_GetHostApiInfo(paHostApi);
    if (!ha) {
        return out;
    }
    for (int j = 0; j < ha->deviceCount; ++j) {
        const int dev = Pa_HostApiDeviceIndexToDeviceIndex(paHostApi, j);
        if (dev < 0) {
            continue;
        }
        const PaDeviceInfo *di = Pa_GetDeviceInfo(dev);
        if (!di) {
            continue;
        }
        if (wantOutput ? di->maxOutputChannels <= 0 : di->maxInputChannels <= 0) {
            continue;
        }
        out << QString::fromUtf8(di->name);
    }
    return out;
}

// Resolve both VAC device names under the operator's chosen host API (by
// name; DL-3).  Empty/unmatched host API → first WASAPI (DL-2 behavior).
// Both must resolve (full-duplex reference model) → true.
static bool resolveVacPaSel(const QString &hostApiName, const QString &outName,
                            const QString &inName, VacPaSel &sel)
{
    const int api = paHostApiIndexForName(hostApiName);
    if (api < 0) {
        return false;
    }
    const int o  = matchPaDeviceInHostApi(api, outName, /*wantOutput*/ true);
    const int in = matchPaDeviceInHostApi(api, inName,  /*wantOutput*/ false);
    if (o < 0 || in < 0) {
        return false;
    }
    sel.hostApi = api;
    sel.outDev  = o;
    sel.inDev   = in;
    return true;
}

// (Re)build the VAC1 engine + Qt device layer for the CURRENT RX audio
// config.  Idempotent: tears down any prior instance first, so it can be
// called on every openRx1 (incl. a sample-rate reopen, which changes
// outSize_/outRate).  Main thread only.  No-op unless VAC1 is enabled
// and the channel is up (outSize_ final).
void WdspEngine::rebuildVac1()
{
    teardownVac1();   // idempotent

    if (!vac1ShouldBeOn() || outSize_ <= 0) {
        return;
    }
    // #158 DL-2 — resolve the chosen VAC devices to PortAudio indices.  The
    // reference VAC is ONE full-duplex stream, so BOTH an input AND an output
    // device are required, under one WASAPI host API.  The shipped
    // transmit-only "(none)" output is NOT available here — TX-without-feedback
    // is restored properly at DL-4 (MOX/MON gating mutes RX→VAC during TX),
    // not by half-opening the stream.
    VacPaSel sel;
    if (!resolveVacPaSel(vac1HostApiName_, vac1OutName_, vac1InName_, sel)) {
        emitLog(QStringLiteral("[vac1] not started — need a WASAPI-resolvable "
                               "input AND output device (out='%1' in='%2'); "
                               "DL-2 is full-duplex (both required)")
                    .arg(vac1OutName_.isEmpty() ? QStringLiteral("(unset)") : vac1OutName_,
                         vac1InName_.isEmpty()  ? QStringLiteral("(unset)") : vac1InName_));
        return;
    }

    using namespace lyra::wire;
    // mic side (VAC-in → TX): mic_rate = the TX channel input rate (48 kHz)
    // and mic_size MUST equal the TX channel's xcm_insize that the cm_main
    // pump hands InboundVacTxAudio — getbuffsize(48000) = 64 — NOT the RX-out
    // block (outSize_, 256).  A wrong mic_size mis-sizes the rmatchIN ring
    // and the TX mic buffer.  (Reference: cmsetup getbuffsize(rate).)
    const int kTxInRate = 48000;
    const int micSize   = 64 * kTxInRate / 48000;   // = getbuffsize(48000) = 64
    // Radio side: audio_rate = WDSP RX out rate (48 kHz), audio_size =
    // the per-block frame count dispatchAudioFrame feeds (== outSize_).
    // PC side: vac_rate = same nominal 48 kHz (the rmatchV rings still
    // drift-correct the two independent crystals), vac_size = the PC-side
    // block.  iq_type=0 (audio, not raw IQ); stereo=1.
    create_ivac(kVac1Id, /*run*/1, /*iq_type*/0, /*stereo*/1,
                /*iq_rate*/   cfg_.inRate,
                /*mic_rate*/  kTxInRate,
                /*audio_rate*/cfg_.outRate,
                /*txmon_rate*/cfg_.outRate,
                /*vac_rate*/  cfg_.outRate,
                /*mic_size*/  micSize,
                /*iq_size*/   outSize_,
                /*audio_size*/outSize_,
                /*txmon_size*/outSize_,
                /*vac_size*/  vac1VacSize_);

    // CRITICAL: create_ivac builds the rmatchV rings from a->in_latency /
    // a->out_latency, which the reference's C# layer sets (via the VAC-setup
    // latency) BEFORE audio starts — but create_ivac itself leaves them 0,
    // so the initial rings come out ZERO-LENGTH and rmatchOUT can never
    // buffer/produce a sample (= dead-silent VAC).  Push a real latency now;
    // the setters destroy+rebuild the rings at the proper size
    // (OUTringsize = 2 * vac_rate * out_latency).  Matches the reference's
    // operator-configurable VAC latency (default ~120 ms).
    {
        const double latSec = vac1LatencyMs_ / 1000.0;
        SetIVACOutLatency(kVac1Id, latSec, /*reset*/1);   // RX audio -> VAC (Stage 3)
        SetIVACInLatency(kVac1Id,  latSec, /*reset*/1);   // VAC -> TX mic (Stage 4)
    }

    // VAC RX gain (reference "Gain RX (dB)" → vac_rx_scale → mixer input-0
    // gain).  dB → linear; 0 dB = unity (the reference default).
    SetIVACrxscale(kVac1Id, std::pow(10.0, vac1RxGainDb_ / 20.0));
    // VAC TX gain (reference "Gain TX (dB)" → vac_preamp, applied by xvacIN
    // on the captured mic before the TX seam).  Default +3 dB.
    SetIVACpreamp(kVac1Id, std::pow(10.0, vac1TxGainDb_ / 20.0));
    // VAC mono-combine (reference vac_combine_input).  ON sums the captured
    // L+R into I=Q=(L+R) before the TX modulator — the I=Q=mono mic form the
    // SSB chain wants (same convention as the TCI mic path, #67), robust to
    // which channel a routed mic lands on.  OFF feeds raw stereo L->I/R->Q.
    SetIVACcombine(kVac1Id, vac1CombineInput_ ? 1 : 0);
    // Register the VAC-in → TX bridge so the cm_main TX pump can pull mic
    // audio from rmatchIN when the mic source is "PC Soundcard (VAC1)"
    // (use_vac_audio).  Idempotent (just stores the fn ptr).
    SendpInboundVacTxAudio(&WdspEngine::vacInboundCb);
    SendpTxMonitorTap(&WdspEngine::txMonitorTapCb);   // #90 post-rack TX-monitor tap

    // Silence block for the mixer's TX-monitor input (stream 2) — sized to
    // the same audio block the RX tee feeds (2*outSize_ doubles).  Assigned
    // BEFORE vac1Active_ goes true, so the mix-thread read is always valid.
    vacMonSilence_.assign(static_cast<size_t>(2 * outSize_), 0.0);

    // #158 DL-2 — device layer = the reference's ONE full-duplex PortAudio
    // stream (CallbackIVAC does mic→rmatchIN + rmatchOUT→RX every interrupt on
    // ONE clock), replacing the two independent Qt streams.  Push the PA
    // device selection onto the engine, then StartAudioIVAC opens + starts the
    // duplex stream.  pa_*_latency = the PA suggestedLatency hint (separate
    // from the rmatchV ring depth set above); exclusive off (VAC cables run
    // shared-mode).
    SetIVAChostAPIindex(kVac1Id, sel.hostApi);
    SetIVACoutputDEVindex(kVac1Id, sel.outDev);
    SetIVACinputDEVindex(kVac1Id, sel.inDev);
    {
        const double paLatSec = vac1LatencyMs_ / 1000.0;
        SetIVACPAOutLatency(kVac1Id, paLatSec, /*reset*/1);
        SetIVACPAInLatency(kVac1Id,  paLatSec, /*reset*/1);
    }
    SetIVACExclusiveOut(kVac1Id, 0);
    SetIVACExclusiveIn(kVac1Id, 0);

    const int rc = StartAudioIVAC(kVac1Id);   // 1 = open + start OK
    if (rc != 1) {
        emitLog(QStringLiteral("[vac1] PortAudio open FAILED (rc=%1; out='%2' "
                               "in='%3' hostApi=%4 out#%5 in#%6); VAC off this "
                               "session").arg(rc).arg(vac1OutName_, vac1InName_)
                    .arg(sel.hostApi).arg(sel.outDev).arg(sel.inDev));
        StopAudioIVAC(kVac1Id);   // null-safe close if OpenStream succeeded then StartStream failed
        destroy_ivac(kVac1Id);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(vacMtx_);
        vac1Active_.store(true, std::memory_order_release);
    }
    // #158 DL-4 — re-apply the current MOX state (a fresh ivac starts mox=0);
    // keeps RX muted out of VAC if we rebuilt mid-TX.
    if (vacMox_) {
        SetIVACmox(kVac1Id, 1);
    }
    // #90 Route 2 — a fresh ivac starts mon=0 / unity mon-vol; re-apply the
    // operator's MON state + Monitor level so the VAC monitor survives a
    // (re)build (e.g. a sample-rate reopen mid-session).
    SetIVACmon(kVac1Id, monEnabled_.load(std::memory_order_relaxed) ? 1 : 0);
    SetIVACmonVol(kVac1Id, monVolume_.load(std::memory_order_relaxed));
    emitLog(QStringLiteral("[vac1] LIVE (PortAudio duplex): out='%1' (RX gain "
                           "%2 dB) | in='%3' (TX gain %4 dB) | %5 Hz, vac %6")
                .arg(vac1OutName_).arg(vac1RxGainDb_)
                .arg(vac1InName_).arg(vac1TxGainDb_)
                .arg(cfg_.outRate).arg(vac1VacSize_));
}

// Stop the VAC-out tee + device + engine instance.  Idempotent.  Main
// thread only.  Order is the load-bearing part: clear vac1Active_ under
// vacMtx_ FIRST (so the mix thread's dispatchAudioFrame stops calling
// xvacOUT), THEN StopAudioIVAC (Pa_CloseStream joins the duplex callback so
// it stops draining rmatchOUT / filling rmatchIN), THEN destroy_ivac frees
// the rings.
void WdspEngine::teardownVac1()
{
    {
        std::lock_guard<std::mutex> lk(vacMtx_);
        vac1Active_.store(false, std::memory_order_release);
    }
    // #158 DL-2 — close the PortAudio duplex stream before freeing the rings.
    // StopAudioIVAC is null-safe if the stream was never opened (calloc-zeroed
    // Stream + PA's pointer validation).
    if (lyra::wire::ivacGet(kVac1Id)) {
        lyra::wire::StopAudioIVAC(kVac1Id);
        lyra::wire::destroy_ivac(kVac1Id);
    }
}

// ── #158 — VAC1 operator controls (Settings → Audio) ─────────────────

// #158 DL-3 — device lists are now PortAudio-backed (Thetis-faithful),
// scoped to the operator's chosen "Driver" (host API).  The no-arg forms
// (kept for the Q_INVOKABLE/QML contract) enumerate the SELECTED host API;
// the *For(hostApi) forms drive the Settings "Driver"→device repopulation.
QStringList WdspEngine::vac1OutputDevices() const
{
    return paDevicesForHostApi(paHostApiIndexForName(vac1HostApiName_),
                               /*wantOutput*/ true);
}

QStringList WdspEngine::vac1HostApiNames() const
{
    QStringList names;
    QList<int>  idx;
    paHostApisWithDevices(names, idx);
    return names;
}

QList<int> WdspEngine::vac1HostApiPaIndices() const
{
    QStringList names;
    QList<int>  idx;
    paHostApisWithDevices(names, idx);
    return idx;
}

QStringList WdspEngine::vac1OutputDevicesFor(int paHostApi) const
{
    return paDevicesForHostApi(paHostApi, /*wantOutput*/ true);
}

QStringList WdspEngine::vac1InputDevicesFor(int paHostApi) const
{
    return paDevicesForHostApi(paHostApi, /*wantOutput*/ false);
}

void WdspEngine::setVac1HostApi(const QString &name)
{
    if (vac1HostApiName_ == name) {
        return;
    }
    vac1HostApiName_ = name;
    QSettings().setValue(QStringLiteral("vac1/hostApi"), name);
    // Device names are resolved under this host API at rebuild; reopen if the
    // VAC should currently be running.
    if (vac1ShouldBeOn()) {
        rebuildVac1();
    }
    emit vac1Changed();
}

// Desired live state: in auto-digital mode VAC1 follows the operating mode
// (on for DIGU/DIGL); otherwise the operator's manual Enable.
bool WdspEngine::vac1ShouldBeOn() const
{
    if (vac1AutoDigital_) {
        return mode_.startsWith(QLatin1String("DIG"), Qt::CaseInsensitive);
    }
    return vac1Enabled_;
}

void WdspEngine::setVacMox(bool on)
{
    vacMox_ = on;
    // #158 DL-4 — RX→VAC muted out of the mixer during TX (reference
    // SetIVACmox what-flag gating: with MON off, both mixer "what" bits go 0
    // on the air → VAC output is silent during TX = the no-feedback behavior).
    // No-op when VAC1 isn't live.  Runs on the Qt main thread (MOX edge), so
    // it's serialized with rebuild/teardown by the event loop; the mix thread
    // reads the what-mask atomically.
    if (lyra::wire::ivacGet(kVac1Id)) {
        lyra::wire::SetIVACmox(kVac1Id, on ? 1 : 0);
    }
}

void WdspEngine::setVac1Enabled(bool on)
{
    if (vac1Enabled_ == on) {
        return;
    }
    vac1Enabled_ = on;
    QSettings().setValue(QStringLiteral("vac1/enabled"), on);
    rebuildVac1();   // reconcile (respects vac1ShouldBeOn / channel state)
    emit vac1Changed();
}

void WdspEngine::setVac1AutoDigital(bool on)
{
    if (vac1AutoDigital_ == on) {
        return;
    }
    vac1AutoDigital_ = on;
    QSettings().setValue(QStringLiteral("vac1/autoDigital"), on);
    rebuildVac1();   // reconcile to the new desired state for the current mode
    emit vac1Changed();
}

QStringList WdspEngine::vac1InputDevices() const
{
    return paDevicesForHostApi(paHostApiIndexForName(vac1HostApiName_),
                               /*wantOutput*/ false);
}

void WdspEngine::setVac1InputDeviceName(const QString &name)
{
    if (vac1InName_ == name) {
        return;
    }
    vac1InName_ = name;
    QSettings().setValue(QStringLiteral("vac1/inputDevice"), name);
    if (vac1ShouldBeOn()) {   // DL-3 (CODEX-P2): also reopen in auto-digital mode
        rebuildVac1();   // reopen capture on the new device
    }
    emit vac1Changed();
}

void WdspEngine::setVac1TxGainDb(double db)
{
    db = std::clamp(db, -60.0, 20.0);
    if (std::abs(db - vac1TxGainDb_) < 1e-9) {
        return;
    }
    vac1TxGainDb_ = db;
    QSettings().setValue(QStringLiteral("vac1/txGainDb"), db);
    // LIVE — push the new VAC TX preamp straight to the running engine
    // (reference vac_preamp), guarded against a concurrent teardown.
    {
        std::lock_guard<std::mutex> lk(vacMtx_);
        if (vac1Active_.load(std::memory_order_relaxed) &&
            lyra::wire::ivacGet(kVac1Id)) {
            lyra::wire::SetIVACpreamp(kVac1Id, std::pow(10.0, db / 20.0));
        }
    }
    emit vac1Changed();
}

void WdspEngine::setVac1OutputDeviceName(const QString &name)
{
    if (vac1OutName_ == name) {
        return;
    }
    vac1OutName_ = name;
    QSettings().setValue(QStringLiteral("vac1/outputDevice"), name);
    // Device change = reopen on the new device (if VAC should be live).
    if (vac1ShouldBeOn()) {   // DL-3 (CODEX-P2): also reopen in auto-digital mode
        rebuildVac1();
    }
    emit vac1Changed();
}

void WdspEngine::setVac1RxGainDb(double db)
{
    db = std::clamp(db, -60.0, 20.0);
    if (std::abs(db - vac1RxGainDb_) < 1e-9) {
        return;
    }
    vac1RxGainDb_ = db;
    QSettings().setValue(QStringLiteral("vac1/rxGainDb"), db);
    // LIVE — no rebuild: push the new scale straight to the running mixer
    // input-0 gain (reference vac_rx_scale).  Guarded against a concurrent
    // teardown so destroy_ivac can't free the instance mid-call.
    {
        std::lock_guard<std::mutex> lk(vacMtx_);
        if (vac1Active_.load(std::memory_order_relaxed) &&
            lyra::wire::ivacGet(kVac1Id)) {
            lyra::wire::SetIVACrxscale(kVac1Id, std::pow(10.0, db / 20.0));
        }
    }
    emit vac1Changed();
}

void WdspEngine::setVac1LatencyMs(int ms)
{
    ms = std::clamp(ms, 5, 500);
    if (ms == vac1LatencyMs_) {
        return;
    }
    vac1LatencyMs_ = ms;
    QSettings().setValue(QStringLiteral("vac1/latencyMs"), ms);
    // The rmatchV ring depth + PA suggested latency are set at create_ivac /
    // SetIVAC*Latency time, so a change only takes effect on a VAC rebuild.
    // Reopen if live (same reconcile path as a device change).
    if (vac1ShouldBeOn()) {
        rebuildVac1();
    }
    emit vac1Changed();
}

void WdspEngine::setVac1VacSize(int frames)
{
    // Reference VAC buffer choices are powers of two; the UI offers those.
    // Clamp to the engine's create_ivac range; non-pow2 still works.
    frames = std::clamp(frames, 64, 8192);
    if (frames == vac1VacSize_) {
        return;
    }
    vac1VacSize_ = frames;
    QSettings().setValue(QStringLiteral("vac1/vacSize"), frames);
    if (vac1ShouldBeOn()) {
        rebuildVac1();   // vac_size feeds create_ivac → reopen to apply
    }
    emit vac1Changed();
}

QVariantMap WdspEngine::vac1Diags()
{
    QVariantMap m;
    std::lock_guard<std::mutex> lk(vacMtx_);
    const bool active = vac1Active_.load(std::memory_order_relaxed) &&
                        lyra::wire::ivacGet(kVac1Id) != nullptr;
    m.insert(QStringLiteral("active"), active);
    if (!active) {
        return m;
    }
    auto read = [&](int type, const char *uKey, const char *oKey,
                    const char *pctKey) {
        int under = 0, over = 0, ringsize = 0, nring = 0;
        double var = 1.0;
        lyra::wire::getIVACdiags(kVac1Id, type, &under, &over, &var,
                                 &ringsize, &nring);
        m.insert(QString::fromLatin1(uKey), under);
        m.insert(QString::fromLatin1(oKey), over);
        m.insert(QString::fromLatin1(pctKey),
                 ringsize > 0 ? (nring * 100 / ringsize) : 0);
    };
    read(0, "outUnder", "outOver", "outPct");   // rmatchOUT = TO VAC (RX→cable)
    read(1, "inUnder",  "inOver",  "inPct");    // rmatchIN  = FROM VAC (cable→TX)
    return m;
}

void WdspEngine::setVac1CombineInput(bool on)
{
    if (vac1CombineInput_ == on) {
        return;
    }
    vac1CombineInput_ = on;
    QSettings().setValue(QStringLiteral("vac1/combineInput"), on);
    // LIVE — no rebuild: push straight to the running engine (xvacIN reads
    // vac_combine_input each block).  Guarded against a concurrent teardown.
    {
        std::lock_guard<std::mutex> lk(vacMtx_);
        if (vac1Active_.load(std::memory_order_relaxed) &&
            lyra::wire::ivacGet(kVac1Id)) {
            lyra::wire::SetIVACcombine(kVac1Id, on ? 1 : 0);
        }
    }
    emit vac1Changed();
}

void WdspEngine::setMuteWillMuteVac(bool on)
{
    if (muteWillMuteVac_.load(std::memory_order_relaxed) == on) {
        return;
    }
    muteWillMuteVac_.store(on, std::memory_order_relaxed);
    QSettings().setValue(QStringLiteral("vac1/muteWillMuteVac"), on);
    // LIVE — dispatchAudioFrame reads muteWillMuteVac_ every block; nothing to
    // push to the engine.  Refresh the Settings mirror.
    emit vac1Changed();
}

void WdspEngine::setZoom(double z)
{
    z = std::max(1.0, std::min(32.0, z));
    if (std::abs(z - zoom_.load(std::memory_order_relaxed)) < 1e-6) {
        return;
    }
    zoom_.store(z, std::memory_order_relaxed);
    // No analyzer reconfiguration — the next copySpectrum() crops to the
    // new zoom.  spanChanged() refreshes the QML frequency scale.
    emit zoomChanged();
    emit spanChanged();
}

void WdspEngine::computePassband(double *lo, double *hi) const
{
    // Per-mode passband edges (offsets from the tuned centre), matching
    // old Lyra's _wdsp_filter_for.  These map onto the HL2 mirrored
    // baseband so the sideband comes out correct (§14.2).
    //
    // Task #53 — the asymmetric SSB/DIG low edge is now operator-
    // tunable via filterLow_ (was hardcoded 0).  USB/DIGU get the
    // low cut at +filterLow_ (positive baseband side); LSB/DIGL
    // mirror to -filterLow_.  filterLow_=0 reproduces the
    // pre-Task-#53 behaviour exactly (low cut at the carrier
    // centre).  CW filters are pitch-centred, low edge doesn't
    // apply meaningfully — left unchanged.  AM/DSB/FM are
    // symmetric around DC — also left unchanged.
    const double bw   = static_cast<double>(bw_);
    const double half = bw / 2.0;
    const double flo  = filterLow_;   // 0..500 Hz operator-tunable
    if (mode_ == QLatin1String("USB") || mode_ == QLatin1String("DIGU")) {
        *lo = flo;                *hi = bw;
    } else if (mode_ == QLatin1String("LSB") || mode_ == QLatin1String("DIGL")) {
        *lo = -bw;                *hi = -flo;
    } else if (mode_ == QLatin1String("CWU")) {
        *lo = cwPitchHz_ - half;  *hi = cwPitchHz_ + half;
    } else if (mode_ == QLatin1String("CWL")) {
        *lo = -cwPitchHz_ - half; *hi = -cwPitchHz_ + half;
    } else {                       // AM / DSB / FM (symmetric around DC)
        *lo = -half;              *hi = half;
    }
}

void WdspEngine::recomputePassband()
{
    double lo, hi;
    computePassband(&lo, &hi);
    if (lo != passbandLowHz_ || hi != passbandHighHz_) {
        passbandLowHz_  = lo;
        passbandHighHz_ = hi;
        emit passbandChanged();
    }
}

// #174 CTUNE step 1 — RXA receiver-oscillator shift (the Thetis RXOsc
// analog).  Demodulate `hz` away from the DDC centre so the DDC can stay
// locked while the VFO moves within the captured IQ span.  hz == 0 turns the
// shift off; nonzero turns it on at that offset.  No-op when the channel is
// closed.  INERT in step 1 — nothing calls this yet; the freq-path
// decomposition wires it in step 2 (sign vs the HL2 mirrored baseband
// bench-verified there first).  Mirrors applyModeFilter's guard posture
// (RXA setters are safe alongside fexchange0 — WDSP serializes internally).
void WdspEngine::setRxShiftHz(double hz)
{
    if (!opened_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    if (!api.SetRXAShiftFreq || !api.SetRXAShiftRun) return;
    if (hz == 0.0) {
        api.SetRXAShiftRun(channel_, 0);
    } else {
        api.SetRXAShiftFreq(channel_, hz);
        api.SetRXAShiftRun(channel_, 1);
    }
    // #174 CTUNE — keep the notch bandpass (NBP0 / manual notches) shifted in
    // step with the demod, the way Thetis drives both from RXOsc
    // (radio.cs:1417-1418).  Same signed Hz as SetRXAShiftFreq; null-safe (only
    // shipped wdsp.dll builds that export it get notch-tracking).
    if (api.RXANBPSetShiftFrequency)
        api.RXANBPSetShiftFrequency(channel_, hz);
}

void WdspEngine::applyModeFilter()
{
    if (!opened_) {
        return;   // applied on the next openRx1()
    }
    const WdspApi &api = wdsp_->api();
    if (api.SetRXAMode) {
        api.SetRXAMode(channel_, modeToWdsp(mode_));
    }
    double lo, hi;
    computePassband(&lo, &hi);
    if (api.RXASetPassband) {
        api.RXASetPassband(channel_, lo, hi);
    }
}

// ── #159 slim DSP — per-mode-family filter type (Linear / Low Latency) ─────
WdspEngine::DspFamily WdspEngine::dspFamilyForMode(const QString &m)
{
    if (m == QLatin1String("FM"))                                return DspFamily::FM;
    if (m == QLatin1String("CWU") || m == QLatin1String("CWL"))  return DspFamily::CW;
    if (m == QLatin1String("DIGU") || m == QLatin1String("DIGL")
        || m == QLatin1String("DRM"))                            return DspFamily::Dig;
    return DspFamily::Phone;   // USB/LSB/AM/SAM/DSB/SPEC
}

static const char *dspFamKey(WdspEngine::DspFamily f)
{
    switch (f) {
        case WdspEngine::DspFamily::FM:  return "fm";
        case WdspEngine::DspFamily::CW:  return "cw";
        case WdspEngine::DspFamily::Dig: return "dig";
        default:                         return "phone";
    }
}

void WdspEngine::loadDspFilterTypes()
{
    QSettings s;
    for (int f = 0; f < 4; ++f) {
        const auto fam = static_cast<DspFamily>(f);
        dspFiltMp_[f][0] = s.value(QStringLiteral("dsp/%1_rx_mp")
                                       .arg(QLatin1String(dspFamKey(fam))),
                                   false).toBool();
        // CW has no TX filter combo — leave its TX slot at the default.
        if (fam != DspFamily::CW)
            dspFiltMp_[f][1] = s.value(QStringLiteral("dsp/%1_tx_mp")
                                           .arg(QLatin1String(dspFamKey(fam))),
                                       false).toBool();
    }
}

bool WdspEngine::dspFilterMinPhase(DspFamily fam, bool tx) const
{
    return dspFiltMp_[static_cast<int>(fam)][tx ? 1 : 0];
}

void WdspEngine::setDspFilterMinPhase(DspFamily fam, bool tx, bool minPhase)
{
    const int fi = static_cast<int>(fam);
    if (dspFiltMp_[fi][tx ? 1 : 0] == minPhase) return;
    dspFiltMp_[fi][tx ? 1 : 0] = minPhase;
    QSettings().setValue(QStringLiteral("dsp/%1_%2_mp")
                             .arg(QLatin1String(dspFamKey(fam)),
                                  tx ? QStringLiteral("tx") : QStringLiteral("rx")),
                         minPhase);
    applyDspFilterTypes();   // re-push if this family is the active mode
}

void WdspEngine::applyDspFilterTypes()
{
    if (!opened_) return;   // re-applied on the next openRx1()
    const WdspApi &api = wdsp_->api();
    const DspFamily fam = dspFamilyForMode(mode_);
    const int fi = static_cast<int>(fam);
    if (api.RXASetMP)
        api.RXASetMP(channel_, dspFiltMp_[fi][0] ? 1 : 0);
    // CW TX is keyer/firmware — no TXA filter to set (reference parity).
    // The TXA channel (chid 1) is created lazily by create_xmtr() at
    // stream-connect; calling TXASetMP before that = AV on a null txa[1].
    if (api.TXASetMP && fam != DspFamily::CW && txaChannelOpen_.load())
        api.TXASetMP(/*chid(1,0)=*/1, dspFiltMp_[fi][1] ? 1 : 0);
}

void WdspEngine::setTxaChannelOpen(bool open)
{
    txaChannelOpen_.store(open);
    if (open) applyDspFilterTypes();   // push the TX filter type now txa[1] exists
}

int WdspEngine::bandwidthForEdge(double edgeOffsetHz) const
{
    const double a = std::abs(edgeOffsetHz);
    double bw;
    if (mode_ == QLatin1String("USB") || mode_ == QLatin1String("DIGU") ||
        mode_ == QLatin1String("LSB") || mode_ == QLatin1String("DIGL")) {
        bw = a;                                   // asymmetric: edge = cutoff
    } else if (mode_ == QLatin1String("CWU")) {
        bw = 2.0 * std::abs(edgeOffsetHz - cwPitchHz_);
    } else if (mode_ == QLatin1String("CWL")) {
        bw = 2.0 * std::abs(edgeOffsetHz + cwPitchHz_);
    } else {                                       // symmetric around DC
        bw = 2.0 * a;
    }
    // Upper cap matches setBandwidth()'s 20 kHz ceiling so a dragged AM/DSB
    // passband edge can reach the wide (16/20 k) AM presets, not stop at 12 k.
    return std::clamp(static_cast<int>(bw + 0.5), 50, 20000);
}

int WdspEngine::markerOffsetHz() const
{
    return cwMarkerOffsetForMode(mode_);
}

int WdspEngine::cwMarkerOffsetForMode(const QString &mode) const
{
    // VFO − DDS: the carrier sits +pitch above the DDS in CWU, −pitch
    // below in CWL; identity for every other mode.
    const int p = static_cast<int>(cwPitchHz_ + 0.5);
    if (mode == QLatin1String("CWU")) return  p;
    if (mode == QLatin1String("CWL")) return -p;
    return 0;
}

bool WdspEngine::zeroBeatActive() const
{
    return running_ && zeroBeatEnabled_.load(std::memory_order_relaxed) &&
           zeroBeatModeOk_.load(std::memory_order_relaxed);
}

bool WdspEngine::zeroBeatValid() const
{
    return zeroBeatActive() && zbValid_.load(std::memory_order_relaxed);
}

double WdspEngine::zeroBeatOffsetHz() const
{
    if (!zeroBeatValid()) return 0.0;
    // Display (panadapter/RF) offset from the operator's dialled carrier.
    // HL2 baseband is spectrally reversed, so the signal's on-screen position
    // is −zbRawHz_; the marker sits at markerOffsetHz.  Centre = zero-beat.
    return -zbRawHz_.load(std::memory_order_relaxed) -
           static_cast<double>(markerOffsetHz());
}

void WdspEngine::setZeroBeatEnabled(bool on)
{
    if (on == zeroBeatEnabled_.load(std::memory_order_relaxed)) return;
    zeroBeatEnabled_.store(on, std::memory_order_relaxed);
    if (!on) zbValid_.store(false, std::memory_order_relaxed);
    emit zeroBeatChanged();
}

void WdspEngine::setCwPitchHz(int hz)
{
    hz = std::clamp(hz, 200, 1500);
    if (hz == static_cast<int>(cwPitchHz_ + 0.5)) {
        return;
    }
    cwPitchHz_ = static_cast<double>(hz);
    cwDecoder_.setToneHz(cwPitchHz_);   // #173 — bind decoder AFC centre to the pitch
    zbMarkerHz_.store(cwMarkerOffsetForMode(mode_), std::memory_order_relaxed);
    QSettings().setValue(QStringLiteral("dsp/cwPitchHz"), hz);
    recomputePassband();   // CW filter recentres on the new pitch
    applyModeFilter();
    pushApfState();        // APF peak tracks the CW pitch
    emit cwPitchChanged();
    emit markerOffsetChanged();   // VFO↔DDS offset changed (CW modes)
}

// #173 CW-5a — RX CW decoder enable.  Resets the decoder on the rising edge
// (clean floor/peak/timing) and seeds its AFC centre from the current pitch;
// the dispatchAudioFrame tap then runs while this AND CW mode are both true.
void WdspEngine::setCwDecodeEnabled(bool on)
{
    if (on == cwDecodeOn_.load(std::memory_order_relaxed)) {
        return;
    }
    if (on) {
        cwDecoder_.setToneHz(cwPitchHz_);
        cwDecoder_.reset();
    }
    cwDecodeOn_.store(on, std::memory_order_relaxed);
    emit cwDecodeEnabledChanged();
}

// Phase 3 — lazy-load the local RBN-confirmed call list from AppData (created
// on first note).  GUI thread only; cheap after the first call.  Runs (and
// mkpaths the app dir) regardless of the Learn gate, but load() never
// creates the file and note() is QML-gated by WdspEngine.cwLearnEnabled — the
// FILE, not the directory, is the §6.6 privacy boundary.
void WdspEngine::ensureCwScpLocal()
{
    if (cwScpLocalLoaded_) return;
    cwScpLocalLoaded_ = true;
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    cwScpLocal_.load((dir + QStringLiteral("/scp_local.txt")).toStdString());
}

void WdspEngine::cwNoteConfirmedCall(const QString& call)
{
    ensureCwScpLocal();
    cwScpLocal_.note(call.trimmed().toUpper().toStdString(),
                     QDateTime::currentSecsSinceEpoch());
    if (cwCaptureOn_.load(std::memory_order_relaxed))
        cwHarvester_->triggerGoldRbn(call.trimmed().toUpper().toStdString(),
                                     QDateTime::currentSecsSinceEpoch());
}

// Phase 2 — opt-in harvest lifecycle.  First enable allocates the 60 s ring,
// its own 48k->3200 decimator (parallel to the neural engine's, so capture
// works in EVERY engine mode), and the harvester; a 1 Hz worker pumps
// post-rolls + retention.  Disable stops the worker; objects stay for cheap
// re-enable.  GUI thread only.
void WdspEngine::setCwCaptureEnabled(bool on)
{
    if (on == cwCaptureOn_.load(std::memory_order_relaxed)) return;
    if (on) {
        if (!cwHarvester_) {
            const QString dir =
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                + QStringLiteral("/Lyra/cw_harvest");
            QDir().mkpath(dir);
            cwHarvestRing_  = std::make_unique<lyra::dsp::CwHarvestRing>(3200 * 60);
            cwHarvestDecim_ =
                std::make_unique<lyra::dsp::DeepFistResampler>(cfg_.outRate, 3200.0);
            cwHarvester_    = std::make_unique<lyra::dsp::CwCaptureHarvester>(
                *cwHarvestRing_, dir.toStdString());
        }
        cwHarvestRun_.store(true);
        cwHarvestWorker_ = std::thread([this] {
            while (cwHarvestRun_.load()) {
                cwHarvester_->pump(QDateTime::currentSecsSinceEpoch());
                for (int i = 0; i < 10 && cwHarvestRun_.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        cwCaptureOn_.store(true, std::memory_order_relaxed);   // tap feeds from here
    } else {
        cwCaptureOn_.store(false, std::memory_order_relaxed);  // tap stops first
        cwHarvestRun_.store(false);
        if (cwHarvestWorker_.joinable()) cwHarvestWorker_.join();
    }
}

// DeepFist — select the active CW decode engine (0=Classic fldigi, 1=Neural,
// 2=Auto).  The neural model is loaded lazily on the first switch to Neural OR
// Auto (both need it).  Ordering is deliberate for audio-thread safety: we
// finish loading + reset every consumer of the new engine BEFORE storing
// cwEngine_ (the audio tap only feeds them once cwEngine_ flips), so it never
// touches a half-constructed ONNX session or stale decoder/arbiter state —
// Auto additionally resets cwDecoder_ and cwArbiter_ since it runs both
// engines.  If the model can't load we stay on the current engine.
void WdspEngine::setCwDecodeEngine(int engine)
{
    const int cur = cwEngine_.load(std::memory_order_relaxed);
    if (engine == cur) return;

    if (engine == 1 || engine == 2) {           // both need the neural model
        if (!neuralCw_.ready()) {
            // Resolve the model dir: env override, then <exeDir>/models.
            QString dir = qEnvironmentVariable("DEEPFIST_MODEL_DIR");
            if (dir.isEmpty() ||
                !QFileInfo::exists(dir + "/deepfist.onnx")) {
                dir = QCoreApplication::applicationDirPath() + "/models";
            }
            ensureCwScpLocal();
            const bool ok = neuralCw_.loadModel(dir.toStdString(),
                                                cwScpLocal_.calls());
            emit cwNeuralAvailableChanged();
            if (!ok) {
                qWarning("DeepFist: neural CW model not loaded (%s) — staying on "
                         "the classic decoder.  %s",
                         qUtf8Printable(dir), neuralCw_.lastError().c_str());
                return;   // keep current engine
            }
            qInfo("DeepFist: neural CW model loaded from %s (SCP rescorer: %d calls, "
                  "blank_pen %.1f)",
                  qUtf8Printable(dir), neuralCw_.scpCount(), neuralCw_.blankPenalty());
        }
        // Reset every consumer BEFORE storing the new engine, so the audio tap
        // (which only feeds them once cwEngine_ flips) never sees stale state.
        neuralCw_.reset();
        if (engine == 2) {
            cwDecoder_.reset();
            cwArbiter_.reset();
        }
        cwEngine_.store(engine, std::memory_order_relaxed);
    } else {
        cwEngine_.store(0, std::memory_order_relaxed);
        cwDecoder_.reset();
    }
    emit cwDecodeEngineChanged();
}

void WdspEngine::setCwBlankPenalty(double p)
{
    p = std::clamp(p, -1.0, 1.0);
    if (std::abs(p - neuralCw_.blankPenalty()) < 1e-6) return;
    neuralCw_.setBlankPenalty(static_cast<float>(p));   // live, audio-thread-safe
    emit cwBlankPenaltyChanged();
}

// Task #53 — shared RX+TX filter low edge.  RX-side application:
// triggers recomputePassband() + applyModeFilter() so the WDSP
// RXASetPassband picks up the new low edge live.  TX-side is
// driven separately via HL2Stream::setTxBandpass (the C++-side
// Prefs.filterLow signal connect lives in main.cpp).  Clamp
// 0..500 Hz matches Prefs::setFilterLow.
void WdspEngine::setFilterLowHz(int hz)
{
    hz = std::clamp(hz, 0, 500);
    if (hz == static_cast<int>(filterLow_ + 0.5)) {
        return;
    }
    filterLow_ = static_cast<double>(hz);
    recomputePassband();   // SSB/DIG passband shifts; CW/AM/DSB/FM unaffected
    applyModeFilter();
}

void WdspEngine::setMode(const QString &m)
{
    if (m.isEmpty() || m == mode_) {
        return;
    }
    mode_ = m;
    // #59 RX EQ digital-mode auto-bypass — audio-thread-safe atomic, set here
    // on the UI thread alongside mode_ (mirrors the TX rack's DIGU/DIGL gate;
    // avoids reading the mode_ QString from the audio thread).
    rxEqModeBypass_.store(m == QLatin1String("DIGU") || m == QLatin1String("DIGL"),
                          std::memory_order_relaxed);
    // #173 CW-5a — gate the decoder tap to CW modes (audio-thread atomic, set
    // here on the UI thread alongside mode_).  Reset the decoder on the rising
    // edge into CW so it starts from a clean floor/peak/timing state.
    {
        const bool cwNow = (m == QLatin1String("CWU") || m == QLatin1String("CWL"));
        const bool cwWas = cwModeActive_.load(std::memory_order_relaxed);
        cwModeActive_.store(cwNow, std::memory_order_relaxed);
        if (cwNow && !cwWas) cwDecoder_.reset();
    }
    // Zero-beat aid runs only in lockable-carrier modes.  Set the atomic on
    // the UI thread alongside mode_; the RX worker resets the estimator on the
    // rising edge.  Modes without a single carrier (SSB/DIGU/DIGL/DSB/SPEC) off.
    zeroBeatModeOk_.store(m == QLatin1String("CWU") || m == QLatin1String("CWL") ||
                          m == QLatin1String("AM")  || m == QLatin1String("SAM") ||
                          m == QLatin1String("FM"),
                          std::memory_order_relaxed);
    zbMarkerHz_.store(cwMarkerOffsetForMode(m), std::memory_order_relaxed);
    recomputePassband();   // overlay edges (even when closed)
    applyModeFilter();     // SetRXAMode + new passband (no-op if closed)
    applyDspFilterTypes(); // #159 — push the new family's RX/TX filter type
    pushSquelchState();    // re-route SSQL/FMSQ/AMSQ for the new mode
    pushApfState();        // APF engages only in CW — re-gate on mode change
    // VAC1 auto-enable: follow the new mode (live in DIGU/DIGL, off else).
    // rebuildVac1 reconciles against vac1ShouldBeOn(); no-op if auto is off.
    if (vac1AutoDigital_) {
        rebuildVac1();
    }
    emit modeChanged();
    emit markerOffsetChanged();   // CW carrier offset flips with mode
    emit zeroBeatChanged();       // active + target flip with mode
}

void WdspEngine::setBandwidth(int hz)
{
    hz = std::clamp(hz, 10, 20000);
    if (hz == bw_) {
        return;
    }
    bw_ = hz;
    recomputePassband();   // overlay edges
    applyModeFilter();     // re-push passband for the new bandwidth
    emit bandwidthChanged();
}

void WdspEngine::setSampleRate(int hz)
{
    // HL2 P1 IQ rates only.  48k is intentionally excluded (EP2 cadence
    // — the same reason old Lyra dropped it).
    if (hz != 96000 && hz != 192000 && hz != 384000) {
        return;
    }
    if (hz == cfg_.inRate) {
        return;
    }
    // Hold channelMtx_ across the whole reopen so feedIq (RX worker)
    // can't run fexchange0 on a half-torn-down channel / stale outBuf_.
    std::lock_guard<std::mutex> lk(channelMtx_);
    const bool wasOpen = opened_;
    if (wasOpen) {
        closeRx1();          // blocking flush + close channel/analyzer/audio
    }
    cfg_.inRate = hz;
    inRateAtomic_.store(hz, std::memory_order_relaxed);  // mirror for spanHz() race-free read (A.6)
    // Re-size the fexchange0 output + int16 scratch for the new rate
    // (out_size = in_size * out_rate / in_rate).
    if (cfg_.inRate >= cfg_.outRate) {
        outSize_ = cfg_.inSize / (cfg_.inRate / cfg_.outRate);
    } else {
        outSize_ = cfg_.inSize * (cfg_.outRate / cfg_.inRate);
    }
    outBuf_.assign(static_cast<size_t>(2 * outSize_), 0.0);
    pcm16_.assign(static_cast<size_t>(2 * outSize_), 0);
    lrWire_.assign(static_cast<size_t>(2 * outSize_), 0.0);  // P4.b OutBound(0) tee buffer
    accum_.clear();
    if (wasOpen) {
        openRx1();           // reopen at the new rate (recreates analyzer)
    }
    emit spanChanged();      // panadapter span = inRate / zoom
    emitLog(QStringLiteral("[wdsp] IQ sample rate -> %1 kHz").arg(hz / 1000));
}

int WdspEngine::spectrumPixelCount() const
{
    return kAnPixels;
}

double WdspEngine::txAnalyzerOffBins() const noexcept
{
    // Only TUN (TX owns the analyzer + a nonzero NCO−dial offset) shifts the
    // crop; RX and voice TX (NCO == dial) leave it centred.
    if (!txOwnsAnalyzer_.load(std::memory_order_acquire)) return 0.0;
    const int off = txAnalyzerOffsetHz_.load(std::memory_order_relaxed);
    if (off == 0) return 0.0;
    const int span = txSpanHz_.load(std::memory_order_relaxed);
    if (span <= 0) return 0.0;
    return static_cast<double>(off) * kAnPixels / static_cast<double>(span);
}

void WdspEngine::cropSpectrum(const float *full, float *dst, int n,
                              double offBins) const noexcept
{
    // Centre the displayed window on (analyzer DC − offBins) bins.  With
    // offBins==0 this is the legacy centred zoom crop.  During TUN
    // offBins = (NCO−dial) in bins, so the gen1 carrier (at −cw_pitch
    // baseband relative to the NCO, i.e. AT the dial on air) lands at the
    // display centre — on the SSB marker — instead of NCO-relative.
    const double z    = zoom_.load(std::memory_order_relaxed);
    const double keep = (z <= 1.0) ? static_cast<double>(kAnPixels)
                                   : static_cast<double>(kAnPixels) / z;
    const double lo    = (kAnPixels - keep) * 0.5 - offBins;
    const double span  = (keep > 1.0) ? (keep - 1.0) : 1.0;
    const int    denom = std::max(1, n - 1);
    for (int i = 0; i < n; ++i) {
        const double srcf = lo + (static_cast<double>(i) / denom) * span;
        int    i0   = static_cast<int>(std::floor(srcf));
        double frac = srcf - i0;
        if (i0 < 0)              { i0 = 0;             frac = 0.0; }
        if (i0 >= kAnPixels - 1) { i0 = kAnPixels - 2; frac = 1.0; }
        dst[i] = static_cast<float>(full[i0] * (1.0 - frac)
                                    + full[i0 + 1] * frac);
    }
}

int WdspEngine::copySpectrum(float *dst, int maxN)
{
    if (!analyzerOpen_ || dst == nullptr) {
        return 0;
    }
    const WdspApi &api = wdsp_->api();
    if (!api.GetPixels) {
        return 0;
    }
    const int n = std::min(maxN, kAnPixels);

    // Pull the latest full-resolution spectrum into the persistent cache.
    // GetPixels memcpy's + clears its ready-flag on a fresh frame, and
    // leaves the buffer UNTOUCHED when there's none (flag=0).  Because the
    // panadapter AND the waterfall both call this each frame, the second
    // caller always gets flag=0 — so reading into a cache that retains the
    // last good frame is what keeps BOTH consumers fed with valid data
    // (an uninitialised per-call buffer here is what produced the red
    // waterfall garbage).  First-time init to a quiet floor.
    if (static_cast<int>(specCache_.size()) != kAnPixels) {
        specCache_.assign(kAnPixels, -200.0f);
    }
    int flag = 0; double ref = 0.0;
    api.GetPixels(kAnDisp, 0, specCache_.data(), &flag, &ref);
    const float *full = specCache_.data();

    const double z = zoom_.load(std::memory_order_relaxed);
    const double offBins = txAnalyzerOffBins();   // 0 unless TUN active
    if (z <= 1.0 && offBins == 0.0) {
        // Full span, no TUN shift — hand the cached spectrum straight back.
        std::memcpy(dst, full, static_cast<size_t>(n) * sizeof(float));
        return n;
    }

    // Zoomed and/or TUN-shifted (old-Lyra crop method + P4.b offset): crop
    // the 1/zoom slice about a centre shifted by the NCO−dial offset and
    // linearly resample up to n display points.  Pure display-side crop —
    // the analyzer is never reconfigured, so the trace can't be corrupted
    // by a live re-setup.
    cropSpectrum(full, dst, n, offBins);
    return n;
}

// §15.29 C1 — waterfall-specific spectrum read.  Mirrors copySpectrum's
// pattern (caching, zoom crop, dual-consumer ready-flag handling) but
// reads pixout=1 (waterfall) during TX state — where configureAnalyzerForTx
// has set up n_pixout=2 with a separate 120 ms tau IIR averaging on
// pixout=1.  In RX state (txOwnsAnalyzer_=false), pixout=1 isn't
// configured (n_pixout=1 in configureAnalyzerForRx), so we fall through
// to copySpectrum's pixout=0 path — waterfall shares the panadapter's
// 30 ms-averaged buffer the same way Lyra did before §15.29.  This
// keeps RX-state behaviour byte-identical until §15.29 C2 (deferred
// per phased scope choice) adds pixout=1 RX averaging.
int WdspEngine::copyWaterfallSpectrum(float *dst, int maxN)
{
    if (!analyzerOpen_ || dst == nullptr) {
        return 0;
    }
    const WdspApi &api = wdsp_->api();
    if (!api.GetPixels) {
        return 0;
    }

    // RX state — fall through to copySpectrum (which reads pixout=0).
    // The waterfall and panadapter share that buffer in RX, matching
    // pre-§15.29 behaviour.
    if (!txOwnsAnalyzer_.load(std::memory_order_acquire)) {
        return copySpectrum(dst, maxN);
    }

    // TX state — pixout=1 is configured + IIR-smoothed at 120 ms tau
    // (configureAnalyzerForTx).  Read into the waterfall-specific
    // cache so the second consumer (if any) still gets valid data
    // when GetPixels' ready-flag clears.
    const int n = std::min(maxN, kAnPixels);
    if (static_cast<int>(wfCache_.size()) != kAnPixels) {
        wfCache_.assign(kAnPixels, -200.0f);
    }
    int flag = 0; double ref = 0.0;
    api.GetPixels(kAnDisp, 1, wfCache_.data(), &flag, &ref);
    const float *full = wfCache_.data();

    const double z = zoom_.load(std::memory_order_relaxed);
    const double offBins = txAnalyzerOffBins();   // 0 unless TUN active
    if (z <= 1.0 && offBins == 0.0) {
        std::memcpy(dst, full, static_cast<size_t>(n) * sizeof(float));
        return n;
    }
    // Zoom and/or TUN-shift crop — same math as copySpectrum.
    cropSpectrum(full, dst, n, offBins);
    return n;
}

bool WdspEngine::startAudio()
{
    // HL2 onboard-codec output: no QAudioSink — RX audio reaches the
    // jack via dispatchAudioFrame → OutBound(0) (the verbatim EP2 writer)
    // whenever hl2Out_ is set; no separate injection arming needed.
    if (hl2Out_) {
        emitLog(QStringLiteral(
            "[wdsp] audio: HL2 audio jack (AK4951) — EP2 injection, "
            "MUTED, volume %1 dB")
            .arg(static_cast<int>(posToDb(volume_.load()))));
        return true;
    }

    QAudioFormat fmt;
    fmt.setSampleRate(cfg_.outRate);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);

    if (devices_.isEmpty() ||
        deviceIndex_ < 0 || deviceIndex_ >= devices_.size()) {
        emitLog(QStringLiteral(
            "[wdsp] audio: no output device — playback disabled "
            "(dBFS meter still live)"));
        return false;
    }
    const QAudioDevice dev = devices_[deviceIndex_];
    if (!dev.isFormatSupported(fmt)) {
        emitLog(QStringLiteral(
            "[wdsp] audio: '%1' does not support 48 kHz/stereo/int16 — "
            "playback disabled (dBFS meter still live)")
            .arg(dev.description()));
        return false;
    }

    const int capFrames     = cfg_.outRate * kRingMs    / 1000;
    const int prefillFrames = cfg_.outRate * kPrefillMs / 1000;
    {
        std::lock_guard<std::mutex> lk(audioMtx_);
        if (audioSink_) {
            return true;  // already up
        }
        audioRing_ = new AudioRing(capFrames);
        audioRing_->prefillSilence(prefillFrames);
        audioRing_->open(QIODevice::ReadOnly);

        audioSink_ = new QAudioSink(dev, fmt, this);
        // Cap the device buffer so audio latency stays low (else Qt's
        // large default makes audio lag the panadapter).  Must be set
        // before start().
        audioSink_->setBufferSize(
            cfg_.outRate * kSinkBufferMs / 1000 * 2 *
            static_cast<int>(sizeof(qint16)));
        audioSink_->start(audioRing_);   // pull mode
    }

    emitLog(QStringLiteral(
        "[wdsp] audio: '%1' @ 48 kHz stereo int16 (ring %2 ms, "
        "prefill %3 ms) — MUTED, volume %4 dB")
        .arg(dev.description()).arg(kRingMs).arg(kPrefillMs)
        .arg(static_cast<int>(posToDb(volume_.load()))));
    return true;
}

void WdspEngine::stopAudio()
{
    // sink->stop() quiesces Qt's audio thread (no more readData) before
    // we delete the ring; audioMtx_ blocks the RX worker's push().
    std::lock_guard<std::mutex> lk(audioMtx_);
    if (audioSink_) {
        audioSink_->stop();
        delete audioSink_;
        audioSink_ = nullptr;
    }
    if (audioRing_) {
        audioRing_->close();
        delete audioRing_;
        audioRing_ = nullptr;
    }
}

double WdspEngine::volumeDb() const
{
    return posToDb(volume_.load(std::memory_order_relaxed));
}

double WdspEngine::sMeterDbm() const
{
    if (!running_ || !wdsp_) return -200.0;
    const WdspApi &api = wdsp_->api();
    if (!api.GetRXAMeter) return -200.0;
    // 0 = RXA_S_PK (peak signal strength).  WDSP computes this in the
    // RX worker; GetRXAMeter just reads the latest stored value, so it
    // is safe to poll from the UI thread.
    return api.GetRXAMeter(channel_, 0);
}

double WdspEngine::agcGainDb() const
{
    if (!running_ || !wdsp_) return 0.0;
    const WdspApi &api = wdsp_->api();
    if (!api.GetRXAMeter) return 0.0;
    return api.GetRXAMeter(channel_, 4);   // 4 = RXA_AGC_GAIN (gain action, dB)
}

// #158 (post-DL) TX dynamics-meter read — re-homes the MIC/ALC/LVL meters
// (NaN-stubbed since the TX wire rebuild) onto the wire-live TXA channel.
// txaMeterType is a WDSP txaMeterType ordinal (TXA.h): MIC_PK=0, LVLR_PK=4,
// LVLR_GAIN=6, ALC_PK=12, ALC_GAIN=14.  The TX channel is chid(1,0)=1
// (create_xmtr); GetTXAMeter reads the latest value the TXA chain stored, so
// it's safe to poll from the UI thread.  Only meaningful while transmitting —
// the metermodel gates the TX computes on moxActive (TX channel runs only on
// the air).  NaN → the meter renders "—".
double WdspEngine::txMeterRaw(int txaMeterType) const
{
    if (!wdsp_) return std::numeric_limits<double>::quiet_NaN();
    const WdspApi &api = wdsp_->api();
    if (!api.GetTXAMeter) return std::numeric_limits<double>::quiet_NaN();
    constexpr int kTxaChannel = 1;   // chid(1,0) — the wire-live TXA channel
    return api.GetTXAMeter(kTxaChannel, txaMeterType);
}

double WdspEngine::agcThreshDb() const
{
    return kAgcThreshDbFs;                 // fixed first-light threshold
}

void WdspEngine::setVolume(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    volume_.store(v, std::memory_order_relaxed);
    QSettings().setValue(QStringLiteral("audio/volume"), v);
    emit volumeChanged();
}

// #90 TX monitor — operator MON toggle + level.  Stage 1 stores + persists;
// dispatchAudioFrame consumes monEnabled_/monVolume_ on the audio thread in
// Stage 3 (when MOX is up + MON on, emit the post-rack monitor tap scaled by
// monVolume_ in place of the auto-muted RX audio).  Inert until then.
void WdspEngine::setMonEnabled(bool on)
{
    const bool prev = monEnabled_.exchange(on, std::memory_order_relaxed);
    if (prev == on) return;
    QSettings().setValue(QStringLiteral("audio/monEnabled"), on);
    emit monEnabledChanged();
    // #90 Route 2 — drive the IVAC monitor routing (RX vs TX-monitor into the
    // VAC-out mixer).  No-op when VAC1 isn't live; rebuildVac1 re-applies on
    // (re)build.  Main-thread call, serialized with rebuild/teardown by the
    // event loop — same gate as setVacMox.
    if (lyra::wire::ivacGet(kVac1Id)) {
        lyra::wire::SetIVACmon(kVac1Id, on ? 1 : 0);
    }
}

void WdspEngine::setMonVolume(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    monVolume_.store(v, std::memory_order_relaxed);
    QSettings().setValue(QStringLiteral("audio/monVolume"), v);
    emit monVolumeChanged();
    // #90 Route 2 — set the VAC monitor mixer level (mixer input 1 scale).
    if (lyra::wire::ivacGet(kVac1Id)) {
        lyra::wire::SetIVACmonVol(kVac1Id, v);
    }
}

void WdspEngine::setTxMuted(bool m)
{
    // Live MOX-driven mute — wired in main.cpp from
    // HL2Stream::moxActiveChanged.  No persistence, no settings write
    // (transient TX state, not an operator preference).  The gain calc
    // gates this through autoMuteOnTx_ so the operator's master switch
    // takes effect immediately if they toggle it mid-TX.
    //
    // RX-on-unkey delay: mute IMMEDIATELY on the keydown edge (m=true),
    // but on the keyup edge (m=false) hold RX muted an extra
    // rxResumeDelayMs_ so the TX-coupled tail in the still-running RX DSP
    // pipeline drains out as silence + the T/R settles before audio
    // resumes (kills the unkey thud + the quick echo of own TX).  A
    // re-key during that window cancels the pending resume and re-mutes.
    if (m) {
        rxResumeTimer_.stop();           // cancel any pending un-mute
        applyTxMuted_(true);
        return;
    }
    const int d = rxResumeDelayMs_.load(std::memory_order_relaxed);
    if (d > 0) {
        rxResumeTimer_.start(d);         // deferred un-mute
    } else {
        applyTxMuted_(false);            // delay disabled — resume instantly
    }
}

void WdspEngine::applyTxMuted_(bool m)
{
    const bool prev = txMuted_.exchange(m, std::memory_order_relaxed);
    if (prev != m) emit txMutedChanged();
}

void WdspEngine::setRxResumeDelayMs(int ms)
{
    ms = std::clamp(ms, 0, 500);
    const int prev = rxResumeDelayMs_.exchange(ms, std::memory_order_relaxed);
    if (prev == ms) return;
    QSettings().setValue(QStringLiteral("audio/rxResumeDelayMs"), ms);
    emit rxResumeDelayMsChanged();
}

void WdspEngine::setAutoMuteOnTx(bool on)
{
    const bool prev = autoMuteOnTx_.exchange(on, std::memory_order_relaxed);
    if (prev == on) return;
    QSettings().setValue(QStringLiteral("audio/autoMuteOnTx"), on);
    emit autoMuteOnTxChanged();
    // If the operator flips this OFF while currently transmitting, the
    // change takes effect on the NEXT audio block — the gain calc reads
    // both atomics fresh, so the gate releases without an audible pop
    // (the volume taper handles the transition).  Symmetric on flip ON.
}

void WdspEngine::setMuted(bool m)
{
    muted_.store(m, std::memory_order_relaxed);
    QSettings().setValue(QStringLiteral("audio/muted"), m);
    emit mutedChanged();
    emitLog(m ? QStringLiteral("[wdsp] audio: muted")
              : QStringLiteral("[wdsp] audio: unmuted (volume %1 dB)")
                    .arg(static_cast<int>(posToDb(volume_.load()))));
}

void WdspEngine::setAfGainDb(double db)
{
    db = std::clamp(db, 0.0, 40.0);
    if (std::abs(db - afGainDb_) < 1e-6) return;
    afGainDb_ = db;
    QSettings().setValue(QStringLiteral("audio/afGainDb"), db);
    {   // push to WDSP live (serialise against feedIq's fexchange0)
        std::lock_guard<std::mutex> lk(channelMtx_);
        if (opened_ && wdsp_ && wdsp_->api().SetRXAPanelGain1)
            wdsp_->api().SetRXAPanelGain1(channel_, std::pow(10.0, db / 20.0));
    }
    emit afGainChanged();
}

void WdspEngine::setBalance(double b)
{
    b = std::clamp(b, -1.0, 1.0);
    balance_.store(b, std::memory_order_relaxed);   // applied in feedIq
    QSettings().setValue(QStringLiteral("audio/balance"), b);
    emit balanceChanged();
}

// ── RX DSP operator controls ──────────────────────────────────────
// Each setter: clamp/validate, store, persist (QSettings), push to
// WDSP when the channel is open, emit NOTIFY.  The push helpers below
// are channel-parameterized (use channel_) so RX2 reuses them verbatim.

void WdspEngine::pushNrState()
{
    if (!opened_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    if (!api.SetRXAEMNRRun) return;   // EMNR not resolved -> nothing to push
    // Mode 1..4 (UI) -> WDSP gain_method 0..3.
    if (api.SetRXAEMNRgainMethod)
        api.SetRXAEMNRgainMethod(channel_, std::clamp(nrMode_, 1, 4) - 1);
    if (api.SetRXAEMNRnpeMethod)
        api.SetRXAEMNRnpeMethod(channel_, std::clamp(npeMethod_, 0, 1));
    if (api.SetRXAEMNRaeRun)
        api.SetRXAEMNRaeRun(channel_, aepfEnabled_ ? 1 : 0);
    // AEPF also engages WDSP's post-filter ("post2") — the dedicated
    // anti-musical-noise stage that stock WDSP leaves off.  Params stay
    // at WDSP's gentle create defaults (0.15/0.15/5.0/0.12); MMSE-LSA
    // upstream keeps the voice natural, so the extra stage removes
    // musical artifacts without a robotic character.
    if (api.SetRXAEMNRpost2Run)
        api.SetRXAEMNRpost2Run(channel_, aepfEnabled_ ? 1 : 0);
    if (api.SetRXAEMNRPosition)
        api.SetRXAEMNRPosition(channel_, 1);   // after AGC (standard position)
    api.SetRXAEMNRRun(channel_, nrEnabled_ ? 1 : 0);
}

void WdspEngine::pushAgcMode()
{
    if (!opened_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    if (!api.SetRXAAGCMode) return;
    int mode = kAgcModeMed;
    if      (agcMode_ == QLatin1String("off"))  mode = kAgcModeOff;
    else if (agcMode_ == QLatin1String("fast")) mode = kAgcModeFast;
    else if (agcMode_ == QLatin1String("slow")) mode = kAgcModeSlow;
    else                                        mode = kAgcModeMed;
    api.SetRXAAGCMode(channel_, mode);
    // Set the time constants EXPLICITLY per mode (the standard per-mode
    // values) so Fast/Med/Slow are unmistakably distinct — the audible
    // difference is mostly the HANG (Fast/Med = none, Slow = 1 s hold)
    // plus the decay rate.  SetRXAAGCMode sets WDSP internal defaults
    // too, but pushing them ourselves removes any doubt about the values.
    // (Off = FIXD/fixed gain — decay/hang are irrelevant, left alone.)
    //
    // Operator-reported "AGC OFF louder than FAST/MED/SLOW" (#76A):
    // mode 0 / FIXD applies fixed_gain as a static multiplier instead
    // of envelope-tracked gain.  WDSP create-time default is 1000.0
    // linear (+60 dB) which produces the backwards-loudness.  Push
    // kAgcFixedGainDb on EVERY mode change regardless of current mode
    // so a subsequent flip to OFF inherits the +20 dB reference-match
    // value instead of WDSP's hot default.  Inert when mode != FIXD;
    // a no-op when SetRXAAGCFixed didn't resolve (null guard).
    if (api.SetRXAAGCFixed) {
        api.SetRXAAGCFixed(channel_, kAgcFixedGainDb);
    }
    if (mode != kAgcModeOff) {
        int decayMs = 250, hangMs = 0, hangThr = 100;   // med
        if (mode == kAgcModeFast)      { decayMs =  50; hangMs =    0; hangThr = 100; }
        else if (mode == kAgcModeSlow) { decayMs = 500; hangMs = 1000; hangThr =   0; }
        if (api.SetRXAAGCDecay)         api.SetRXAAGCDecay(channel_, decayMs);
        if (api.SetRXAAGCHang)          api.SetRXAAGCHang(channel_, hangMs);
        if (api.SetRXAAGCHangThreshold) api.SetRXAAGCHangThreshold(channel_, hangThr);
    }
}

void WdspEngine::setNrEnabled(bool on)
{
    if (nrEnabled_ == on) return;
    nrEnabled_ = on;
    QSettings().setValue(QStringLiteral("dsp/nrEnabled"), on);
    pushNrState();
    emit nrChanged();
    emitLog(QStringLiteral("[wdsp] NR %1 (Mode %2)")
                .arg(on ? QStringLiteral("on") : QStringLiteral("off"))
                .arg(nrMode_));
}

void WdspEngine::setNrMode(int mode)
{
    mode = std::clamp(mode, 1, 4);
    if (nrMode_ == mode) return;
    nrMode_ = mode;
    QSettings().setValue(QStringLiteral("dsp/nrMode"), mode);
    pushNrState();
    emit nrChanged();
}

void WdspEngine::setAepfEnabled(bool on)
{
    if (aepfEnabled_ == on) return;
    aepfEnabled_ = on;
    QSettings().setValue(QStringLiteral("dsp/aepf"), on);
    pushNrState();
    emit nrChanged();
}

void WdspEngine::setNpeMethod(int method)
{
    method = std::clamp(method, 0, 1);
    if (npeMethod_ == method) return;
    npeMethod_ = method;
    QSettings().setValue(QStringLiteral("dsp/npeMethod"), method);
    pushNrState();
    emit nrChanged();
}

void WdspEngine::setAgcMode(const QString &mode)
{
    const QString m = mode.toLower();
    if (m != QLatin1String("off") && m != QLatin1String("fast") &&
        m != QLatin1String("slow") && m != QLatin1String("med"))
        return;   // ignore unknown values
    if (agcMode_ == m) return;
    agcMode_ = m;
    QSettings().setValue(QStringLiteral("dsp/agcMode"), m);
    pushAgcMode();
    emit agcModeChanged();
    emitLog(QStringLiteral("[wdsp] AGC mode %1").arg(m));
}

void WdspEngine::pushAnfState()
{
    if (!opened_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    if (!api.SetRXAANFRun) return;
    if (api.SetRXAANFVals)            // carrier-null defaults (taps/delay/gain/leak)
        api.SetRXAANFVals(channel_, 64, 16, 1.0e-3, 1.0e-7);
    api.SetRXAANFRun(channel_, anfEnabled_ ? 1 : 0);
}

void WdspEngine::pushLmsState()
{
    if (!opened_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    if (!api.SetRXAANRRun) return;
    if (api.SetRXAANRVals) {
        // strength 0..1 -> taps 32..128 + adapt rate (gain) 8e-5..16e-4.
        const double s    = std::clamp(lmsStrength_, 0.0, 1.0);
        const int    taps = 32 + static_cast<int>(std::lround(96.0 * s));
        const double gain = 8.0e-5 + (16.0e-4 - 8.0e-5) * s;
        api.SetRXAANRVals(channel_, taps, 16, gain, 1.0e-7);
    }
    api.SetRXAANRRun(channel_, lmsEnabled_ ? 1 : 0);
}

void WdspEngine::setAnfEnabled(bool on)
{
    if (anfEnabled_ == on) return;
    anfEnabled_ = on;
    QSettings().setValue(QStringLiteral("dsp/anfEnabled"), on);
    pushAnfState();
    emit anfChanged();
    emitLog(QStringLiteral("[wdsp] ANF %1")
                .arg(on ? QStringLiteral("on") : QStringLiteral("off")));
}

void WdspEngine::setLmsEnabled(bool on)
{
    if (lmsEnabled_ == on) return;
    lmsEnabled_ = on;
    QSettings().setValue(QStringLiteral("dsp/lmsEnabled"), on);
    pushLmsState();
    emit lmsChanged();
    emitLog(QStringLiteral("[wdsp] LMS %1 (strength %2%)")
                .arg(on ? QStringLiteral("on") : QStringLiteral("off"))
                .arg(static_cast<int>(lmsStrength_ * 100.0 + 0.5)));
}

void WdspEngine::setLmsStrength(double s)
{
    s = std::clamp(s, 0.0, 1.0);
    if (std::abs(s - lmsStrength_) < 1e-6) return;
    lmsStrength_ = s;
    QSettings().setValue(QStringLiteral("dsp/lmsStrength"), s);
    pushLmsState();   // re-push taps/gain even while enabled (live tune)
    emit lmsChanged();
}

// ── Manual notches (NBP database) ─────────────────────────────────

QVariantList WdspEngine::notches() const
{
    QVariantList out;
    for (const Notch &n : notches_) {
        QVariantMap m;
        m[QStringLiteral("offsetHz")] = n.offsetHz;
        m[QStringLiteral("widthHz")]  = n.widthHz;
        m[QStringLiteral("active")]   = n.active;
        out.append(m);
    }
    return out;
}

void WdspEngine::persistNotches()
{
    QStringList items;
    items.reserve(static_cast<int>(notches_.size()));
    for (const Notch &n : notches_)
        items << QStringLiteral("%1|%2|%3")
                     .arg(n.offsetHz).arg(n.widthHz).arg(n.active ? 1 : 0);
    QSettings().setValue(QStringLiteral("dsp/notches"), items);
}

void WdspEngine::pushNotches()
{
    if (!opened_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    if (!api.RXANBPAddNotch || !api.RXANBPSetNotchesRun) return;
    // Rebuild the WDSP DB from the Lyra list (the DB is recreated empty
    // on every channel open).  Delete existing high->low to avoid the
    // index reshuffle that deleting index 0 repeatedly would cause.
    if (api.RXANBPDeleteNotch) {
        for (int i = wdspNotchCount_ - 1; i >= 0; --i)
            api.RXANBPDeleteNotch(channel_, i);
    }
    for (int i = 0; i < static_cast<int>(notches_.size()); ++i) {
        const Notch &n = notches_[i];
        api.RXANBPAddNotch(channel_, i, n.offsetHz, n.widthHz,
                           n.active ? 1 : 0);
    }
    wdspNotchCount_ = static_cast<int>(notches_.size());
    api.RXANBPSetNotchesRun(
        channel_, (notchEnabled_ && !notches_.empty()) ? 1 : 0);
}

void WdspEngine::setNotchEnabled(bool on)
{
    if (notchEnabled_ == on) return;
    notchEnabled_ = on;
    QSettings().setValue(QStringLiteral("dsp/notchEnabled"), on);
    pushNotches();
    emit notchesChanged();
}

int WdspEngine::addNotch(double offsetHz, double widthHz)
{
    notches_.push_back({offsetHz, std::max(10.0, widthHz), true});
    notchEnabled_ = true;   // dropping a notch implies NF on
    QSettings().setValue(QStringLiteral("dsp/notchEnabled"), true);
    persistNotches();
    pushNotches();
    emit notchesChanged();
    return static_cast<int>(notches_.size()) - 1;
}

void WdspEngine::removeNotch(int index)
{
    if (index < 0 || index >= static_cast<int>(notches_.size())) return;
    notches_.erase(notches_.begin() + index);
    persistNotches();
    pushNotches();
    emit notchesChanged();
}

void WdspEngine::moveNotch(int index, double offsetHz)
{
    if (index < 0 || index >= static_cast<int>(notches_.size())) return;
    notches_[index].offsetHz = offsetHz;
    persistNotches();
    pushNotches();
    emit notchesChanged();
}

void WdspEngine::setNotchWidth(int index, double widthHz)
{
    if (index < 0 || index >= static_cast<int>(notches_.size())) return;
    notches_[index].widthHz = std::clamp(widthHz, 10.0, 20000.0);
    persistNotches();
    pushNotches();
    emit notchesChanged();
}

void WdspEngine::carveNotches(float *db, int n, double floorDb) const
{
    if (!db || n < 2 || !notchEnabled_ || notches_.empty()) return;
    const double span = static_cast<double>(spanHz());
    if (span <= 0.0) return;
    const float floorF = static_cast<float>(floorDb);
    for (const Notch &nt : notches_) {
        if (!nt.active) continue;
        const double cCol  = (nt.offsetHz / span + 0.5) * (n - 1);
        double       halfW = 0.5 * (nt.widthHz / span) * (n - 1);
        if (halfW < 0.75) halfW = 0.75;             // ≥ ~1.5 columns visible
        int lo = static_cast<int>(std::floor(cCol - halfW));
        int hi = static_cast<int>(std::ceil (cCol + halfW));
        if (lo < 0)      lo = 0;
        if (hi > n - 1)  hi = n - 1;
        for (int i = lo; i <= hi; ++i)
            if (db[i] > floorF) db[i] = floorF;     // clamp down to floor
    }
}

int WdspEngine::notchNear(double offsetHz, double tolHz) const
{
    int best = -1;
    double bestD = tolHz;
    for (int i = 0; i < static_cast<int>(notches_.size()); ++i) {
        const double d = std::abs(notches_[i].offsetHz - offsetHz);
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

// ── All-mode squelch (SSQL / FMSQ / AMSQ, routed by mode) ─────────

void WdspEngine::pushSquelchState()
{
    if (!opened_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    const bool fm  = (mode_ == QLatin1String("FM"));
    const bool am  = (mode_ == QLatin1String("AM") ||
                      mode_ == QLatin1String("SAM") ||
                      mode_ == QLatin1String("DSB"));
    const bool ssb = !fm && !am;   // USB/LSB/CWU/CWL/DIGU/DIGL/SPEC
    const bool on  = squelchEnabled_;
    const double t = std::clamp(squelchThreshold_, 0.0, 1.0);

    // SSQL — SSB/CW/DIG voice-presence squelch.  *0.65 scale puts the
    // WU2O-tested-good default (~0.16) at a comfortable slider zone; the
    // tau pair gives a snappy unmute + a hang that doesn't clamp between
    // syllables (bench-tunable).
    if (api.SetRXASSQLRun) {
        if (api.SetRXASSQLTauMute)   api.SetRXASSQLTauMute(channel_, 0.7);
        if (api.SetRXASSQLTauUnMute) api.SetRXASSQLTauUnMute(channel_, 0.1);
        if (api.SetRXASSQLThreshold) api.SetRXASSQLThreshold(channel_, t * 0.65);
        api.SetRXASSQLRun(channel_, (on && ssb) ? 1 : 0);
    }
    // FM squelch (noise-level threshold; log map so the slider feels even).
    if (api.SetRXAFMSQRun) {
        if (api.SetRXAFMSQThreshold)
            api.SetRXAFMSQThreshold(channel_, std::pow(10.0, -2.0 * t));
        api.SetRXAFMSQRun(channel_, (on && fm) ? 1 : 0);
    }
    // AM squelch (carrier-level threshold, ~-160..-30 dB; short tail).
    if (api.SetRXAAMSQRun) {
        if (api.SetRXAAMSQMaxTail)   api.SetRXAAMSQMaxTail(channel_, 0.5);
        if (api.SetRXAAMSQThreshold)
            api.SetRXAAMSQThreshold(channel_, -160.0 + t * 130.0);
        api.SetRXAAMSQRun(channel_, (on && am) ? 1 : 0);
    }
}

void WdspEngine::setSquelchEnabled(bool on)
{
    if (squelchEnabled_ == on) return;
    squelchEnabled_ = on;
    QSettings().setValue(QStringLiteral("dsp/squelchEnabled"), on);
    pushSquelchState();
    emit squelchChanged();
    emitLog(QStringLiteral("[wdsp] squelch %1")
                .arg(on ? QStringLiteral("on") : QStringLiteral("off")));
}

void WdspEngine::setSquelchThreshold(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    if (std::abs(t - squelchThreshold_) < 1e-6) return;
    squelchThreshold_ = t;
    QSettings().setValue(QStringLiteral("dsp/squelchThreshold"), t);
    pushSquelchState();
    emit squelchChanged();
}

// ── Noise blanker (EXT NOB-II) ────────────────────────────────────

void WdspEngine::pushNbState()
{
    if (!opened_ || !nbCreated_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    if (api.SetEXTNOBThreshold) {
        // strength 0..1 -> threshold 12..2.5 (light..heavy).  LOWER
        // threshold = more aggressive blanking; clamp to the working
        // 1.5..50 range (light≈10, heavy≈3 ported from the Python tree).
        double th = 12.0 - 9.5 * std::clamp(nbStrength_, 0.0, 1.0);
        th = std::clamp(th, 1.5, 50.0);
        api.SetEXTNOBThreshold(channel_, th);
    }
    if (api.SetEXTNOBRun)
        api.SetEXTNOBRun(channel_, nbEnabled_ ? 1 : 0);
}

void WdspEngine::setNbEnabled(bool on)
{
    if (nbEnabled_ == on) return;
    nbEnabled_ = on;
    QSettings().setValue(QStringLiteral("dsp/nbEnabled"), on);
    pushNbState();
    emit nbChanged();
    emitLog(QStringLiteral("[wdsp] NB %1 (strength %2%)")
                .arg(on ? QStringLiteral("on") : QStringLiteral("off"))
                .arg(static_cast<int>(nbStrength_ * 100.0 + 0.5)));
}

void WdspEngine::setNbStrength(double s)
{
    s = std::clamp(s, 0.0, 1.0);
    if (std::abs(s - nbStrength_) < 1e-6) return;
    nbStrength_ = s;
    QSettings().setValue(QStringLiteral("dsp/nbStrength"), s);
    pushNbState();   // re-tune threshold live
    emit nbChanged();
}

// ── APF (CW audio peaking filter — in-chain biquad) ───────────────

void WdspEngine::pushApfState()
{
    if (!opened_ || !wdsp_) return;
    const WdspApi &api = wdsp_->api();
    if (!api.SetRXABiQuadRun) return;
    const bool cw = (mode_ == QLatin1String("CWU") ||
                     mode_ == QLatin1String("CWL"));
    // Peak centred on the CW pitch, ~75 Hz wide, operator-set gain (dB→linear).
    if (api.SetRXABiQuadFreq)      api.SetRXABiQuadFreq(channel_, cwPitchHz_);
    if (api.SetRXABiQuadBandwidth) api.SetRXABiQuadBandwidth(channel_, 75.0);
    if (api.SetRXABiQuadGain)
        api.SetRXABiQuadGain(channel_, std::pow(10.0, apfGainDb_ / 20.0));
    api.SetRXABiQuadRun(channel_, (apfEnabled_ && cw) ? 1 : 0);
}

void WdspEngine::setApfEnabled(bool on)
{
    if (apfEnabled_ == on) return;
    apfEnabled_ = on;
    QSettings().setValue(QStringLiteral("dsp/apfEnabled"), on);
    pushApfState();
    emit apfChanged();
    emitLog(QStringLiteral("[wdsp] APF %1")
                .arg(on ? QStringLiteral("on") : QStringLiteral("off")));
}

void WdspEngine::setApfGainDb(double db)
{
    db = std::clamp(db, 0.0, 24.0);
    if (std::abs(db - apfGainDb_) < 1e-6) return;
    apfGainDb_ = db;
    QSettings().setValue(QStringLiteral("dsp/apfGainDb"), db);
    pushApfState();   // re-apply the biquad gain live
    emit apfChanged();
}

// ── BIN — binaural pseudo-stereo (Lyra-native Hilbert post-proc) ──

void WdspEngine::buildBinaural()
{
    constexpr double kPi = 3.14159265358979323846;
    const int N = 63;                       // taps (group delay 31)
    binH_.assign(static_cast<size_t>(N), 0.0);
    const int n = (N - 1) / 2;
    // Truncated 2/(pi*k) Hilbert response (odd k), Hamming-windowed.
    for (int k = -n; k <= n; ++k) {
        if (k != 0 && (k % 2) != 0)
            binH_[static_cast<size_t>(k + n)] = 2.0 / (kPi * k);
    }
    for (int i = 0; i < N; ++i)
        binH_[static_cast<size_t>(i)] *=
            0.54 - 0.46 * std::cos(2.0 * kPi * i / (N - 1));
    binHist_.assign(static_cast<size_t>(N), 0.0);
    binPos_ = 0;
}

void WdspEngine::resetBinaural()
{
    std::fill(binHist_.begin(), binHist_.end(), 0.0);
    binPos_ = 0;
}

void WdspEngine::binauralStep(double mono, double *l, double *r)
{
    const int N = static_cast<int>(binHist_.size());   // 63
    binHist_[static_cast<size_t>(binPos_)] = mono;
    // Hilbert-shifted = FIR conv over the history (newest at binPos_).
    double shifted = 0.0;
    int idx = binPos_;
    for (int k = 0; k < N; ++k) {
        shifted += binH_[static_cast<size_t>(k)] *
                   binHist_[static_cast<size_t>(idx)];
        if (--idx < 0) idx = N - 1;
    }
    // In-phase path delayed by the group delay (31) to align with shifted.
    int didx = binPos_ - 31;
    if (didx < 0) didx += N;
    const double delayed = binHist_[static_cast<size_t>(didx)];
    if (++binPos_ >= N) binPos_ = 0;
    // Sum/diff mix + equal-loudness normalization (d=0 → mono).
    const double d  = binDepth_;
    const double ds = d * shifted;
    const double norm = 1.0 / std::sqrt(1.0 + d * d);
    *l = (delayed - ds) * norm;
    *r = (delayed + ds) * norm;
}

void WdspEngine::setBinEnabled(bool on)
{
    if (binEnabled_ == on) return;
    binEnabled_ = on;
    if (on) resetBinaural();   // clean state on engage (click-free)
    QSettings().setValue(QStringLiteral("dsp/binEnabled"), on);
    emit binChanged();
    emitLog(QStringLiteral("[wdsp] BIN %1")
                .arg(on ? QStringLiteral("on") : QStringLiteral("off")));
}

void WdspEngine::setBinDepth(double d)
{
    d = std::clamp(d, 0.0, 1.0);
    if (std::abs(d - binDepth_) < 1e-6) return;
    binDepth_ = d;
    QSettings().setValue(QStringLiteral("dsp/binDepth"), d);
    emit binChanged();
}

void WdspEngine::startNoiseCapture(double seconds)
{
    // Hold channelMtx_ so we can't arm/reset the profile while feedIq is
    // mid-block on the RX worker thread (it feeds noiseProfile_ under the
    // same lock).  (Re)create the profile if its FFT size changed.
    {
        std::lock_guard<std::mutex> lk(channelMtx_);
        if (!noiseProfile_ || noiseProfile_->fftSize() != npFftSize_) {
            noiseProfile_ = std::make_unique<CapturedProfile>(npFftSize_);
        }
        noiseProfile_->begin(cfg_.inRate, seconds);
        noiseProfileValid_.store(false, std::memory_order_relaxed);
        noiseProgress_.store(0.0, std::memory_order_relaxed);
        noiseCapturing_.store(true, std::memory_order_relaxed);
    }
    emitLog(QStringLiteral("Noise capture: %1 s @ %2 Hz, FFT %3")
                .arg(seconds).arg(cfg_.inRate).arg(npFftSize_));
    emit noiseCaptureChanged();
}

void WdspEngine::cancelNoiseCapture()
{
    {
        std::lock_guard<std::mutex> lk(channelMtx_);
        noiseCapturing_.store(false, std::memory_order_relaxed);
        if (noiseProfile_) noiseProfile_->cancel();
    }
    emit noiseCaptureChanged();
}

void WdspEngine::setNoiseApply(bool on)
{
    {
        std::lock_guard<std::mutex> lk(channelMtx_);
        if (on) {
            // (Re)create the reducer at the current captured-profile FFT
            // size and load the current profile if one is valid + matches.
            if (!reducer_ || reducer_->fftSize() != npFftSize_) {
                reducer_ = std::make_unique<NoiseReducer>(npFftSize_);
            }
            applyReducerParams();
            if (noiseProfile_ && noiseProfile_->valid() &&
                noiseProfile_->fftSize() == npFftSize_) {
                reducer_->setProfile(noiseProfile_->noisePower());
            }
        }
        applyEnabled_.store(on, std::memory_order_relaxed);
    }
    // Remember the operator's NR-C on/off across restarts (main.cpp restores
    // it at launch after auto-loading the chosen profile).
    QSettings().setValue(QStringLiteral("dsp/noiseApplyEnabled"), on);
    emitLog(on ? QStringLiteral("Noise apply: ON")
               : QStringLiteral("Noise apply: OFF"));
    emit noiseApplyChanged();
}

void WdspEngine::applyReducerParams()
{
    // Caller holds channelMtx_.  Pushes the persisted tunables onto the
    // reducer (no-op if it doesn't exist yet).
    if (!reducer_) return;
    reducer_->setAlpha(npAlpha_);
    reducer_->setFloorDb(npFloorDb_);
    reducer_->setSmoothing(npSmoothing_);
}

void WdspEngine::setNoiseStrength(double a)
{
    a = std::clamp(a, 1.0, 5.0);
    if (std::abs(a - npAlpha_) < 1e-6) return;
    npAlpha_ = a;
    QSettings().setValue(QStringLiteral("dsp/npAlpha"), a);
    {
        std::lock_guard<std::mutex> lk(channelMtx_);
        if (reducer_) reducer_->setAlpha(a);
    }
    emit noiseTuningChanged();
}

void WdspEngine::setNoiseFloorDb(double db)
{
    db = std::clamp(db, -30.0, -3.0);
    if (std::abs(db - npFloorDb_) < 1e-6) return;
    npFloorDb_ = db;
    QSettings().setValue(QStringLiteral("dsp/npFloorDb"), db);
    {
        std::lock_guard<std::mutex> lk(channelMtx_);
        if (reducer_) reducer_->setFloorDb(db);
    }
    emit noiseTuningChanged();
}

void WdspEngine::setNoiseSmoothing(double sm)
{
    sm = std::clamp(sm, 0.0, 0.95);
    if (std::abs(sm - npSmoothing_) < 1e-6) return;
    npSmoothing_ = sm;
    QSettings().setValue(QStringLiteral("dsp/npSmoothing"), sm);
    {
        std::lock_guard<std::mutex> lk(channelMtx_);
        if (reducer_) reducer_->setSmoothing(sm);
    }
    emit noiseTuningChanged();
}

QStringList WdspEngine::noiseProfiles() const
{
    QStringList names;
    for (const auto &p : profiles_) names << p.name;
    return names;
}

QString WdspEngine::noiseProfileInfo(const QString &name) const
{
    for (const auto &p : profiles_) {
        if (p.name == name) {
            const QString r = (p.rate % 1000 == 0)
                ? QStringLiteral("%1k").arg(p.rate / 1000)
                : QString::number(p.rate);
            return QStringLiteral("%1 · %2 · %3").arg(r).arg(p.fft).arg(p.date);
        }
    }
    return QString();
}

void WdspEngine::setNoiseFftSize(int n)
{
    if (n != 2048 && n != 4096 && n != 8192) return;
    if (n == npFftSize_) return;
    {
        std::lock_guard<std::mutex> lk(channelMtx_);
        npFftSize_ = n;
        // A profile is FFT-size-specific: drop the live capture, the
        // reducer, and apply — the operator re-captures at the new size.
        noiseCapturing_.store(false, std::memory_order_relaxed);
        if (noiseProfile_) noiseProfile_->cancel();
        noiseProfileValid_.store(false, std::memory_order_relaxed);
        reducer_.reset();
        applyEnabled_.store(false, std::memory_order_relaxed);
        npActiveName_.clear();
    }
    QSettings().setValue(QStringLiteral("dsp/npFftSize"), n);
    emit noiseSettingsChanged();
    emit noiseCaptureChanged();
    emit noiseApplyChanged();
    emit noiseProfilesChanged();
}

void WdspEngine::setNoiseCaptureSeconds(double sec)
{
    if (sec != 3.0 && sec != 5.0 && sec != 10.0) return;
    if (sec == npCaptureSeconds_) return;
    npCaptureSeconds_ = sec;
    QSettings().setValue(QStringLiteral("dsp/npCaptureSeconds"), sec);
    emit noiseSettingsChanged();
}

QString WdspEngine::profilesDir() const
{
    // Beside the FFT-wisdom folder (wdsp_native): N8SDR/Lyra-cpp/profiles
    // under the platform's generic data location.  Created on demand.
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation);
    const QString dir = QDir::cleanPath(
        base + QStringLiteral("/N8SDR/Lyra-cpp/profiles"));
    QDir().mkpath(dir);
    return dir;
}

QString WdspEngine::noiseProfilesDir() const
{
    return QDir::toNativeSeparators(profilesDir());
}

bool WdspEngine::saveNoiseProfile(const QString &name)
{
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return false;
    StoredProfile sp;
    {
        std::lock_guard<std::mutex> lk(channelMtx_);
        if (!noiseProfile_ || !noiseProfile_->valid()) return false;
        sp.rate  = noiseProfile_->sampleRate();
        sp.fft   = noiseProfile_->fftSize();
        sp.power = noiseProfile_->noisePower();
    }
    sp.name = nm;
    sp.date = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd"));
    // Overwrite a same-named profile (reuse its file), else append.
    StoredProfile *slot = nullptr;
    for (auto &p : profiles_) {
        if (p.name == nm) { sp.file = p.file; p = sp; slot = &p; break; }
    }
    if (!slot) { profiles_.push_back(sp); slot = &profiles_.back(); }
    if (!writeProfileFile(*slot)) return false;
    npActiveName_ = nm;
    emit noiseProfilesChanged();
    return true;
}

bool WdspEngine::loadNoiseProfile(const QString &name)
{
    const StoredProfile *found = nullptr;
    for (const auto &p : profiles_) {
        if (p.name == name) { found = &p; break; }
    }
    if (!found) return false;
    if (found->rate != cfg_.inRate) {
        emitLog(QStringLiteral("Profile '%1' was captured at %2 Hz; current "
                               "rate is %3 Hz — recapture at this rate.")
                    .arg(name).arg(found->rate).arg(cfg_.inRate));
        return false;   // rate mismatch — caller surfaces the hint
    }
    {
        std::lock_guard<std::mutex> lk(channelMtx_);
        if (found->fft != npFftSize_) {
            npFftSize_ = found->fft;   // adopt the profile's FFT size
            QSettings().setValue(QStringLiteral("dsp/npFftSize"), npFftSize_);
        }
        if (!reducer_ || reducer_->fftSize() != npFftSize_) {
            reducer_ = std::make_unique<NoiseReducer>(npFftSize_);
        }
        applyReducerParams();
        reducer_->setProfile(found->power);
        npActiveName_ = name;
    }
    emit noiseSettingsChanged();
    emit noiseProfilesChanged();
    return true;
}

void WdspEngine::deleteNoiseProfile(const QString &name)
{
    for (auto it = profiles_.begin(); it != profiles_.end(); ++it) {
        if (it->name == name) {
            removeProfileFile(*it);
            profiles_.erase(it);
            if (npActiveName_ == name) npActiveName_.clear();
            emit noiseProfilesChanged();
            return;
        }
    }
}

bool WdspEngine::promptSaveProfile()
{
    if (!noiseProfileValid_.load(std::memory_order_relaxed)) {
        QMessageBox::information(nullptr, tr("Save noise profile"),
            tr("Capture a profile first — press 📷 Cap on a quiet, "
               "signal-free frequency."));
        return false;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(
        nullptr, tr("Save noise profile"),
        tr("Name (e.g. 40m-night, 20m-ESSB):"),
        QLineEdit::Normal, npActiveName_, &ok);
    if (!ok || name.trimmed().isEmpty()) return false;
    return saveNoiseProfile(name);
}

void WdspEngine::loadProfileOrWarn(const QString &name)
{
    if (!loadNoiseProfile(name)) {
        QMessageBox::information(nullptr, tr("Profile not loaded"),
            tr("“%1” was captured at a different sample rate than the "
               "radio is using now.\nSwitch to that rate, or recapture at the "
               "current rate.").arg(name));
    }
}

bool WdspEngine::renameNoiseProfile(const QString &oldName, const QString &newName)
{
    const QString nn = newName.trimmed();
    if (nn.isEmpty() || nn == oldName) return false;
    StoredProfile *p = nullptr;
    for (auto &q : profiles_) if (q.name == oldName) { p = &q; break; }
    if (!p) return false;
    for (const auto &q : profiles_)            // reject collision
        if (&q != p && q.name == nn) return false;
    const QString oldFile = p->file;
    p->name = nn;
    p->file.clear();                           // force a new filename
    if (!writeProfileFile(*p)) { p->name = oldName; p->file = oldFile; return false; }
    if (!oldFile.isEmpty() && oldFile != p->file) QFile::remove(oldFile);
    if (npActiveName_ == oldName) npActiveName_ = nn;
    emit noiseProfilesChanged();
    return true;
}

void WdspEngine::loadProfiles()
{
    profiles_.clear();
    const QDir dir(profilesDir());
    const QStringList files = dir.entryList(
        QStringList{QStringLiteral("*.lnp")}, QDir::Files, QDir::Name);
    for (const QString &fn : files) {
        const QString path = dir.filePath(fn);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QDataStream ds(&f);
        ds.setVersion(QDataStream::Qt_6_0);
        quint32 magic = 0, ver = 0;
        ds >> magic >> ver;
        if (magic != 0x4C4E5031u) continue;        // "LNP1"
        StoredProfile sp;
        qint32 rate = 0, fft = 0;
        QByteArray pw;
        ds >> sp.name >> rate >> fft >> sp.date >> pw;
        if (ds.status() != QDataStream::Ok) continue;
        sp.rate = rate; sp.fft = fft; sp.file = path;
        if (sp.name.isEmpty() || fft <= 0) continue;
        if (pw.size() == static_cast<int>(fft * sizeof(double))) {
            sp.power.resize(static_cast<size_t>(fft));
            std::memcpy(sp.power.data(), pw.constData(),
                        static_cast<size_t>(pw.size()));
            profiles_.push_back(std::move(sp));
        }
    }
}

bool WdspEngine::writeProfileFile(StoredProfile &p)
{
    if (p.file.isEmpty()) {
        // Derive a safe, unique filename from the (operator) name.
        QString safe;
        for (const QChar c : p.name)
            safe += (c.isLetterOrNumber() || c == QLatin1Char('_') ||
                     c == QLatin1Char('-') || c == QLatin1Char(' '))
                        ? c : QLatin1Char('_');
        safe = safe.trimmed();
        if (safe.isEmpty()) safe = QStringLiteral("profile");
        const QString dir = profilesDir();
        QString cand = QDir(dir).filePath(safe + QStringLiteral(".lnp"));
        int n = 2;
        while (QFileInfo::exists(cand)) {
            bool ownedByOther = false;     // collision with a DIFFERENT profile
            for (const auto &q : profiles_)
                if (&q != &p && q.file == cand) { ownedByOther = true; break; }
            if (!ownedByOther) break;      // free / ours -> overwrite is fine
            cand = QDir(dir).filePath(
                QStringLiteral("%1_%2.lnp").arg(safe).arg(n++));
        }
        p.file = cand;
    }
    QFile f(p.file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emitLog(QStringLiteral("Profile save FAILED — cannot write %1").arg(p.file));
        return false;
    }
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << quint32(0x4C4E5031u) << quint32(1)
       << p.name << qint32(p.rate) << qint32(p.fft) << p.date;
    const QByteArray pw(reinterpret_cast<const char *>(p.power.data()),
                        static_cast<int>(p.power.size() * sizeof(double)));
    ds << pw;
    f.close();
    emitLog(QStringLiteral("Profile saved → %1").arg(p.file));
    return true;
}

void WdspEngine::removeProfileFile(const StoredProfile &p)
{
    if (!p.file.isEmpty()) QFile::remove(p.file);
}

QStringList WdspEngine::audioOutputDevices() const
{
    // Index 0 is always the HL2 onboard codec (old Lyra's default HL2
    // path); the operator's PC output devices follow.
    QStringList names;
    names << QStringLiteral("HL2 audio jack (AK4951)");
    for (const QAudioDevice &d : devices_) {
        names << d.description();
    }
    return names;
}

void WdspEngine::setAudioOutputDevice(int index)
{
    if (index == 0) {
        // HL2 onboard codec: stop the PC sink, route audio to EP2.
        if (!hl2Out_) {
            hl2Out_ = true;
            QSettings().setValue(QStringLiteral("audio/output"),
                                 QStringLiteral("hl2"));
            if (running_) stopAudio();          // drop the QAudioSink
            emit audioDeviceChanged();
            emitLog(QStringLiteral(
                "[wdsp] audio: output -> HL2 audio jack (AK4951)"));
        }
        return;
    }
    const int devIdx = index - 1;   // PC device list is offset by 1
    if (devIdx < 0 || devIdx >= devices_.size()) {
        return;
    }
    const bool wasHl2 = hl2Out_;
    if (!wasHl2 && devIdx == deviceIndex_) {
        return;   // no change
    }
    hl2Out_      = false;
    deviceIndex_ = devIdx;
    QSettings().setValue(QStringLiteral("audio/output"),
                         QStringLiteral("pc"));
    QSettings().setValue(QStringLiteral("audio/deviceName"),
                         devices_[devIdx].description());
    emit audioDeviceChanged();
    emitLog(QStringLiteral("[wdsp] audio: output device -> %1")
                .arg(devices_[devIdx].description()));
    // Live switch: (re)build the sink on the chosen device.
    if (running_) {
        stopAudio();
        startAudio();
    }
}

void WdspEngine::setRadioAudioSink(
    std::function<void(const qint16 *lr, int nframes)> sink)
{
    std::lock_guard<std::mutex> lk(radioSinkMtx_);
    radioSink_ = std::move(sink);
}

void WdspEngine::applyAudioRouteTransient(bool hl2,
                                          const QString &deviceName)
{
    // setAudioOutputDevice minus the QSettings writes — see header.
    if (hl2) {
        if (!hl2Out_) {
            hl2Out_ = true;
            if (running_) stopAudio();
            emit audioDeviceChanged();
            emitLog(QStringLiteral(
                "[wdsp] audio: output -> HL2 audio jack (session route)"));
        }
        return;
    }
    if (devices_.isEmpty()) return;     // no PC outputs to route to
    int devIdx = -1;
    if (!deviceName.isEmpty()) {
        for (int i = 0; i < devices_.size(); ++i)
            if (devices_[i].description() == deviceName) { devIdx = i; break; }
    }
    if (devIdx < 0) {
        // Fall back to the operator's persisted PC device choice.
        const QString saved =
            QSettings().value(QStringLiteral("audio/deviceName")).toString();
        for (int i = 0; i < devices_.size(); ++i)
            if (devices_[i].description() == saved) { devIdx = i; break; }
    }
    if (devIdx < 0)
        devIdx = (deviceIndex_ >= 0 && deviceIndex_ < devices_.size())
                     ? deviceIndex_ : 0;
    if (!hl2Out_ && devIdx == deviceIndex_) return;   // already there
    hl2Out_      = false;
    deviceIndex_ = devIdx;
    emit audioDeviceChanged();
    emitLog(QStringLiteral("[wdsp] audio: output -> %1 (session route)")
                .arg(devices_[devIdx].description()));
    if (running_) {
        stopAudio();
        startAudio();
    }
}

void WdspEngine::restoreAudioRouteFromSettings()
{
    QSettings s;
    const bool hl2 =
        s.value(QStringLiteral("audio/output"), QStringLiteral("hl2"))
            .toString() != QLatin1String("pc");
    applyAudioRouteTransient(
        hl2, s.value(QStringLiteral("audio/deviceName")).toString());
}

// Stage B.6.a (2026-06-08) -- pure extraction from feedIq's inline
// audio-dispatch tail.  Operator gain/mute/balance/BIN/HL2-atten +
// int16 quantization + sink dispatch.  EVERY state read uses the
// same memory order, EVERY operation runs in the same sequence,
// EVERY conditional is the same as the pre-refactor inline body
// at wdsp_engine.cpp:2402-2456 (pre-B.6.a).  This is a byte-
// identical relocation -- no cleanups, no reorderings, no "while
// I'm here" changes.  The pre-refactor inline body and this helper
// are operationally indistinguishable; the operator HL2 bench-gate
// for Stage B.6.a is "audio sounds exactly like it did 5 minutes
// ago".
//
// Why: Stage B.6.b will swap the inline `dispatchAudioFrame(
// outBuf_.data(), outSize_)` call in feedIq for an
// `xMixAudio(0, 0, outBuf_.data())` push into the ported AAMix at
// id=0, with this same helper bound as the AAMix's Outbound
// callback.  Pre-extracting it into a sharable function means
// B.6.b is a 1-line call-site swap + an AAMix construction at RX
// open; no audio-dispatch logic moves in B.6.b -- it just runs
// from mix_main's Outbound dispatch instead of inline.  Splitting
// the refactor (B.6.a) from the wire-up (B.6.b) keeps each step
// individually revertable + individually bench-gateable per the
// locked methodology.
// P0.c direct port — AAMix's raw-fn-ptr Outbound target.  Static
// member (exact `void(*)(int,int,double*)` type; reaches the
// private dispatchAudioFrame) routed through the TU-scope
// g_aamixOutboundSelf context — the reference's free-function +
// global-context outbound shape.  Lifetime contract documented at
// the g_aamixOutboundSelf definition.
void WdspEngine::aamixOutbound(int /*id*/, int nsamples, double *buff)
{
    WdspEngine *self = g_aamixOutboundSelf;
    if (self != nullptr) {
        self->dispatchAudioFrame(buff, nsamples);
    }
}

// #90 TX-monitor tap — registered via SendpTxMonitorTap.  Runs on the cm_main
// TX pump thread (xcmaster case 1) at the mic block rate, on the post-rack mic
// (== the fexchange0 input, "what you sound like").  READ-ONLY: copies the
// mono I-lane (buff is interleaved I/Q doubles, nsamples complex; mic is fed
// I=Q=mono) into the lock-free monitorRing_; dispatchAudioFrame drains it onto
// the jack when MOX is up + MON on (Stage 3).  NEVER writes buff.  Self via
// g_aamixOutboundSelf (same lifetime contract as vacInboundCb/aamixOutbound —
// cleared after destroy_aamix in closeRx1, so a late TX-thread call no-ops).
void WdspEngine::txMonitorTapCb(int nsamples, double *buff)
{
    WdspEngine *self = g_aamixOutboundSelf;
    if (self == nullptr || buff == nullptr || nsamples <= 0) {
        return;
    }
    // Only fill the ring when MON is on — saves the copy on every TX block
    // when the operator isn't monitoring, and keeps the ring from
    // accumulating latency (dispatchAudioFrame also flushes it when idle).
    if (!self->monEnabled_.load(std::memory_order_relaxed)) {
        return;
    }
    // Stack scratch sized to the mic block (xcm_insize, typ. <= 1024).  Clamp
    // rather than ever allocate on the real-time TX thread.
    constexpr int kMaxMono = 4096;
    if (nsamples > kMaxMono) nsamples = kMaxMono;
    double mono[kMaxMono];
    for (int i = 0; i < nsamples; ++i) {
        mono[i] = buff[2 * i];
    }
    self->monitorRing_.push(mono, static_cast<std::size_t>(nsamples));
}

// #158 (#161 UAF fix) — VAC-in → TX bridge, registered via
// SendpInboundVacTxAudio.  Runs on the cm_main TX pump thread (xcmaster
// case 1) at the mic block rate when the xmtr's use_vac_audio is set.
// Gate EXACTLY like the mix-side tee (dispatchAudioFrame): hold vacMtx_
// and re-check vac1Active_ before xvacIN.  teardownVac1/rebuildVac1 flip
// vac1Active_ under vacMtx_ around the create/resize/destroy of the single
// full-duplex ivac + its rmatchIN ring, so this can never xvacIN a freed
// or mid-rebuilt ring — the VAC device-change / enable-disable heap fault.
// (The old free-function form guarded only on a racy ivacGet()!=null
// check, which the device-change teardown+recreate TOCTOU'd straight
// through: pvac[id] was nulled before the free, but nothing ordered the
// pump's check-then-xvacIN against destroy_ivac's null-then-free.)
void WdspEngine::vacInboundCb(int nsamples, double *buff)
{
    WdspEngine *self = g_aamixOutboundSelf;
    if (self == nullptr) {
        return;  // engine closed (self cleared after destroy_aamix in closeRx1)
    }
    {
        std::lock_guard<std::mutex> lk(self->vacMtx_);
        // vac1Active_ is true ONLY between rebuildVac1's StartAudioIVAC and
        // teardownVac1's clear — i.e. exactly when the rmatchIN ring is fully
        // built and valid.  ivacGet is belt-and-suspenders.  When off, leave
        // buff untouched: the cm_main pump's pcm->in already holds the codec
        // mic (SAFETY — never deref a null/half-built ivac).
        if (!self->vac1Active_.load(std::memory_order_relaxed) ||
            lyra::wire::ivacGet(kVac1Id) == nullptr) {
            return;
        }
        lyra::wire::xvacIN(kVac1Id, buff, /*bypass*/0);
    }
    // #158 diag — fires only when VAC1 is the live TX source; the peak
    // proves real audio arrived.  buff is the caller's TX-mic block, safe to
    // scan outside the lock.  Rate-limited ~1/s (mic rate 48 kHz); INFO so it
    // lands in lyra-log.txt without debug logging.
    static long long drnSamps = 0;
    static long long drnCalls = 0;
    static double    drnPeak  = 0.0;
    const int n2 = 2 * nsamples;
    for (int i = 0; i < n2; ++i) {
        const double v = buff[i] < 0.0 ? -buff[i] : buff[i];
        if (v > drnPeak) drnPeak = v;
    }
    ++drnCalls;
    drnSamps += nsamples;
    if (drnSamps >= 48000) {
        qInfo("[vac1] xvacIN drain (TX mic): %lld calls/s, peak %.4f",
              drnCalls, drnPeak);
        drnCalls = 0; drnSamps = 0; drnPeak = 0.0;
    }
}

// #59 RX EQ — point the post-RXA audio at the RX EqModel's engine + analyzer
// (nullptr to detach).  release-store so the audio thread sees a fully-built
// engine; the analyzer is stored first so a non-null engine implies it's set.
void WdspEngine::setRxEqEngine(lyra::dsp::ParamEq *eng,
                               lyra::dsp::EqAnalyzer *analyzer)
{
    rxEqAnalyzer_.store(analyzer, std::memory_order_release);
    rxEq_.store(eng, std::memory_order_release);
}

void WdspEngine::setRxRecordTap(std::function<void(const double *, int)> tap)
{
    // Set once before the audio thread runs (MainWindow ctor) — no lock needed.
    rxRecordTap_ = std::move(tap);
}

namespace {
// #187 — passive WAV capture of the EXACT mono audio fed to the CW decoder, for
// offline filter tuning against a real off-air signal.  Armed only when the env
// var LYRA_CW_WAV=<path> is set; writes 48 kHz mono 32-bit IEEE-float (unclamped).
// Audio-thread only (single producer); the size fields are re-patched on every
// write so the file stays a valid WAV even if the process is killed before the
// destructor runs (static-local dtors aren't guaranteed on a Qt app close).
// Zero cost when the env var is unset (the static QByteArray is empty).
class CwWavCapture {
public:
    CwWavCapture(const QString &path, quint32 sampleRate) {
        file_.setFileName(path);
        if (!file_.open(QIODevice::WriteOnly)) return;
        writeHeader(sampleRate);
        ok_ = true;
    }
    ~CwWavCapture() {
        if (!ok_) return;
        patchSizes();
        file_.close();
    }
    void write(const float *s, int n) {
        if (!ok_) return;
        // Raw 32-bit float, UNCLAMPED — so the true decoder-input level +
        // envelope are visible (a clamped 16-bit capture hides over-drive).
        file_.write(reinterpret_cast<const char *>(s),
                    static_cast<qint64>(n) * 4);
        dataBytes_ += static_cast<quint32>(n) * 4;
        // Keep the RIFF/data size fields current so a hard-closed capture is
        // still a valid, playable WAV.  Throttled to ~1 s of audio so this adds
        // a couple of tiny seeks/writes per second, not per block.
        if (dataBytes_ - lastPatched_ >= 192000) {   // 1 s mono @48k × 4 bytes
            patchSizes();
            lastPatched_ = dataBytes_;
        }
    }
private:
    // Patch the RIFF + data size fields, then return to the append position so
    // writing continues seamlessly.
    void patchSizes() {
        const qint64 end = 44 + static_cast<qint64>(dataBytes_);
        file_.seek(4);  writeU32(36 + dataBytes_);   // RIFF chunk size
        file_.seek(40); writeU32(dataBytes_);         // data sub-chunk size
        file_.seek(end);
    }
    void writeU32(quint32 v) {
        const char b[4] = { char(v & 0xFF), char((v >> 8) & 0xFF),
                            char((v >> 16) & 0xFF), char((v >> 24) & 0xFF) };
        file_.write(b, 4);
    }
    void writeU16(quint16 v) {
        const char b[2] = { char(v & 0xFF), char((v >> 8) & 0xFF) };
        file_.write(b, 2);
    }
    void writeHeader(quint32 sr) {
        file_.write("RIFF", 4); writeU32(0);   // patched on close
        file_.write("WAVE", 4);
        file_.write("fmt ", 4); writeU32(16);
        writeU16(3);            // IEEE float
        writeU16(1);            // mono
        writeU32(sr);
        writeU32(sr * 4);       // byte rate (mono × 4 bytes)
        writeU16(4);            // block align
        writeU16(32);           // bits/sample
        file_.write("data", 4); writeU32(0);   // patched on close
    }
    QFile   file_;
    quint32 dataBytes_   = 0;
    quint32 lastPatched_ = 0;
    bool    ok_ = false;
};
} // namespace

void WdspEngine::setFreqCalMeasuring(bool on)
{
    if (on) {
        // Search a wide, comfortable audio window (~100 Hz .. 2.3 kHz) so the
        // operator can place the time-station carrier anywhere in the SSB
        // passband.  8192-sample windows at the audio rate settle in ~1-2 s.
        freqCal_.setSampleRate(static_cast<double>(cfg_.outRate));
        freqCal_.setTarget(1200.0, 1100.0);
        freqCal_.setSnrGateDb(10.0);
        freqCal_.start();
        fcalLastWin_ = -1;
        freqCalOn_.store(true, std::memory_order_relaxed);
    } else {
        freqCalOn_.store(false, std::memory_order_relaxed);
    }
}

void WdspEngine::dispatchAudioFrame(const double *audio, int nframes)
{
    // Frequency calibration (Stage 3b) — feed the SAME pre-EQ RX audio to the
    // carrier-tone estimator while a measurement is armed.  Independent of CW
    // mode; off = one relaxed bool read (zero impact).  Emits one
    // freqCalUpdated per analysis window so the UI ticks live even on a weak
    // carrier (windows()=strong-only would go silent on a dead band).
    if (freqCalOn_.load(std::memory_order_relaxed) && nframes > 0) {
        if (static_cast<int>(fcalMono_.size()) != nframes)
            fcalMono_.assign(static_cast<size_t>(nframes), 0.0f);
        for (int f = 0; f < nframes; ++f)
            fcalMono_[static_cast<size_t>(f)] = static_cast<float>(audio[2 * f]);
        freqCal_.process(fcalMono_.data(), nframes);
        const int an = freqCal_.analyzed();
        if (an != fcalLastWin_) {
            fcalLastWin_ = an;
            emit freqCalUpdated(freqCal_.measuredHz(), freqCal_.snrDb(),
                                freqCal_.windows());
        }
    }

    // #173 CW-5a — RX CW decoder tap.  The FIRST consumer of the post-demod
    // RX audio, taken BEFORE the RX-EQ block below repoints `audio` — so a
    // user's CW-mode EQ curve can't distort the decode.  Gated to CW mode +
    // operator enable; when off this is one relaxed bool read (zero impact).
    if (cwDecodeOn_.load(std::memory_order_relaxed) &&
        cwModeActive_.load(std::memory_order_relaxed) && nframes > 0) {
        if (static_cast<int>(cwMonoBuf_.size()) != nframes)
            cwMonoBuf_.assign(static_cast<size_t>(nframes), 0.0f);
        for (int f = 0; f < nframes; ++f)
            cwMonoBuf_[static_cast<size_t>(f)] = static_cast<float>(audio[2 * f]);
        // Route to the selected engine.  Neural is only reachable after its model
        // is fully loaded (see setCwDecodeEngine), so it is safe to call here.
        // Auto (2) fans out to BOTH; the arbiter picks who drives the display.
        const int eng = cwEngine_.load(std::memory_order_relaxed);
        if (eng == 2) {
            cwDecoder_.process(cwMonoBuf_.data(), nframes);
            neuralCw_.process(cwMonoBuf_.data(), nframes);
        } else if (eng == 1) {
            neuralCw_.process(cwMonoBuf_.data(), nframes);
        } else {
            cwDecoder_.process(cwMonoBuf_.data(), nframes);
        }

        // #187 — optional capture of the EXACT decoder input to a WAV for
        // offline filter tuning.  Armed by LYRA_CW_WAV=<path>; passive, the
        // file is finalised on app exit.  Unset = one empty-QByteArray check.
        static const QByteArray cwWavPath = qgetenv("LYRA_CW_WAV");
        if (!cwWavPath.isEmpty()) {
            static CwWavCapture cwWav(QString::fromLocal8Bit(cwWavPath),
                                      static_cast<quint32>(cfg_.outRate));
            cwWav.write(cwMonoBuf_.data(), nframes);
        }

        // Phase 2 harvest: parallel decimate into the capture ring (opt-in).
        if (cwCaptureOn_.load(std::memory_order_relaxed)) {
            cwHarvestTmp_.clear();
            cwHarvestDecim_->process(cwMonoBuf_.data(), nframes, cwHarvestTmp_);
            if (!cwHarvestTmp_.empty())
                cwHarvestRing_->push(cwHarvestTmp_.data(),
                                     static_cast<int>(cwHarvestTmp_.size()));
        }
    }

    // #59 RX EQ — shape the post-RXA receive audio (mono-dup L==R) BEFORE all
    // tees (jack / PC sink / TCI / VAC), the way the reference EQs inside RXA.
    // `audio` is const + feeds every consumer, so EQ a mutable copy and
    // repoint the local pointer at it.  Gated: engine present, operator not
    // bypassing (the panel ON/OFF, any mode), and not a digital mode
    // (DIGU/DIGL stay flat for the decoders).  Analyzer fed pre/post (panel).
    if (auto *rxeq = rxEq_.load(std::memory_order_acquire);
        rxeq && nframes > 0 && !rxeq->bypassed() &&
        !rxEqModeBypass_.load(std::memory_order_relaxed)) {
        const int n2 = 2 * nframes;
        if (static_cast<int>(rxEqBuf_.size()) != n2)
            rxEqBuf_.assign(static_cast<size_t>(n2), 0.0);
        std::copy(audio, audio + n2, rxEqBuf_.begin());
        constexpr int kRxEqMaxBlk = 4096;
        auto *rxan = rxEqAnalyzer_.load(std::memory_order_acquire);
        if (rxan && nframes <= kRxEqMaxBlk) {
            thread_local double preBuf[kRxEqMaxBlk];
            for (int k = 0; k < nframes; ++k)
                preBuf[k] = rxEqBuf_[static_cast<size_t>(2 * k)];
            rxeq->processMonoDup(rxEqBuf_.data(), nframes);
            rxan->feed(preBuf, rxEqBuf_.data(), nframes);
        } else {
            rxeq->processMonoDup(rxEqBuf_.data(), nframes);
        }
        audio = rxEqBuf_.data();   // every tee below now reads the EQ'd audio
    }

    // #89 Stage C2 — RX recorder tap.  `audio` is now the post-RX-DSP audio the
    // operator HEARS (post-EQ, mono-dup).  Feeds the ClipRecorder while an RX
    // clip records; a lock-free no-op otherwise (one relaxed bool read).
    if (rxRecordTap_ && nframes > 0) {
        rxRecordTap_(audio, nframes);
    }

    // #91 VOX anti-VOX — running RMS of the RX audio the operator HEARS
    // (post-EQ, mono-dup: audio[2*f] is the L==R sample).  A studio-
    // monitor setup can spill this into the mic; the VOX gate reads this
    // level (WdspEngine::voxRxAudioRmsLin) to suppress opening on it.
    // EWMA ~40 ms (α=0.2 per block); single writer (this audio thread) →
    // atomic for the Qt-main VOX poll's provider pull.  One cheap pass,
    // negligible vs the DSP already run; always computed so the level is
    // live the instant VOX is enabled.
    if (nframes > 0) {
        double sumsq = 0.0;
        for (int f = 0; f < nframes; ++f) {
            const double s = audio[2 * f];
            sumsq += s * s;
        }
        const double rms  = std::sqrt(sumsq / nframes);
        const double prev = voxRxRmsLin_.load(std::memory_order_relaxed);
        voxRxRmsLin_.store(prev + 0.2 * (rms - prev),
                           std::memory_order_relaxed);
    }

    // #90 TX monitor — drain the post-rack tap ONCE per block into monScratch_
    // (mono), shared by Route 1 (HL2 jack, in the output loop below) and
    // Route 2 (VAC stream-2 feed in the tee block).  monActive = MOX up + MON
    // on.  An underrun zero-pads (never blocks); when inactive the ring is
    // flushed so the next key-up is fresh + low-latency.  Pre-WDSP-ALC/bandpass
    // by design (your rack processing, not the radio's corrective ALC).
    const bool monActive = monEnabled_.load(std::memory_order_relaxed) &&
                           txMuted_.load(std::memory_order_relaxed);
    if (monActive) {
        if (static_cast<int>(monScratch_.size()) != nframes) {
            monScratch_.assign(static_cast<size_t>(nframes), 0.0);
        } else {
            std::fill(monScratch_.begin(), monScratch_.end(), 0.0);
        }
        monitorRing_.pop(monScratch_.data(), static_cast<size_t>(nframes));
    } else {
        monitorRing_.clear();
    }

    // #90 Route 3 — TCI RX-audio stream tap (centralized here with the jack +
    // VAC monitor routes: one ring drain, one thread; tciAudioBlock is a
    // QueuedConnection so the emit thread is irrelevant).  Mono float32, pre
    // operator-volume.  Reference SetTCIRxAudioMox/SetTCIRxAudioMon
    // (audio.cs:368-404): on the air the RX is muted out of the TCI stream,
    // and replaced by the TX monitor (post-rack mic × Monitor vol) when MON
    // is on.  txMuted_ is the raw wire-MOX flag (TCI mutes on MOX regardless
    // of the autoMuteOnTx operator pref, matching the reference).
    if (tciAudioOn_.load(std::memory_order_relaxed)) {
        QByteArray b(nframes * int(sizeof(float)), Qt::Uninitialized);
        float *d = reinterpret_cast<float *>(b.data());
        if (monActive) {
            const double mv = monVolume_.load(std::memory_order_relaxed);
            for (int f = 0; f < nframes; ++f)
                d[f] = static_cast<float>(monScratch_[static_cast<size_t>(f)] * mv);
        } else if (txMuted_.load(std::memory_order_relaxed)) {
            std::fill_n(d, nframes, 0.0f);   // RX muted out of TCI on the air
        } else {
            for (int f = 0; f < nframes; ++f)
                d[f] = static_cast<float>(audio[static_cast<size_t>(2 * f)]);
        }
        emit tciAudioBlock(b, cfg_.outRate);
    }

    // #158 (#161) — VAC1 RX-out tee.  Feed the POST-RXA receiver audio
    // (post AGC/NR/demod/AF-panel-gain) into the IVAC RX-audio mixer input
    // (xvacOUT stream 1), scaled by the operator monitor VOLUME — and muted
    // when the operator chose "Mute will mute VAC".  This matches the
    // reference, whose RX output gain is applied PRE-tap (RXOutputGain) so the
    // Vol slider rides the VAC feed, and whose MuteWillMuteVAC1 option zeroes
    // that same pre-tap gain.  (An earlier comment here claimed the reference
    // keeps VAC level-independent of the monitor volume — WRONG; tester A/B vs
    // the reference disproved it, #161.)  The VAC RX gain (SetIVACrxscale)
    // stays the independent cable trim on top.
    // Gated by vac1Active_ (cheap relaxed read on the hot path); the
    // xvacOUT itself runs under vacMtx_ so a main-thread teardown can't
    // destroy_ivac the rmatchOUT ring mid-call.  The size guard matches
    // the AAMix insize (audio_size == outSize_); a mismatched block is
    // skipped rather than fed wrong-sized into xMixAudio.
    const double vacVolGain = posToGain(volume_.load(std::memory_order_relaxed));
    const bool   vacMuted   = muted_.load(std::memory_order_relaxed) &&
                              muteWillMuteVac_.load(std::memory_order_relaxed);
    const double vacGain    = vacMuted ? 0.0 : vacVolGain;
    if (vac1Active_.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lk(vacMtx_);
        if (vac1Active_.load(std::memory_order_relaxed) &&
            nframes == outSize_) {
            // The IVAC mixer is a 2-input AAMix (active=3): its mix_main
            // WaitForMultipleObjects(…, TRUE) won't produce a block until
            // BOTH inputs are Ready.  Feed input 0 = RX audio (stream 1)
            // AND input 1 = TX monitor (stream 2) with silence — otherwise
            // the mixer starves and the VAC output is intermittent/dead.
            // RX audio is scaled by the monitor volume/mute into a reusable
            // buffer so the sink loop below still gets the raw `audio`.
            const int vacN2 = 2 * nframes;
            if (static_cast<int>(vacRxScaled_.size()) != vacN2) {
                vacRxScaled_.assign(static_cast<size_t>(vacN2), 0.0);
            }
            for (int i = 0; i < vacN2; ++i) {
                vacRxScaled_[static_cast<size_t>(i)] = audio[i] * vacGain;
            }
            lyra::wire::xvacOUT(kVac1Id, /*stream*/1, vacRxScaled_.data());
            // #90 Route 2 — feed the TX monitor into VAC stream-2 (mixer
            // input 1) when monitoring; else silence so the 2-input mixer
            // never starves.  Unscaled: SetIVACmonVol applies the Monitor
            // level on the mixer side, and SetIVACmox/SetIVACmon route RX vs
            // monitor (RX muted out of the VAC on the air when MON is on).
            if (monActive &&
                static_cast<int>(monScratch_.size()) == nframes) {
                if (static_cast<int>(vacMonStereo_.size()) != 2 * nframes) {
                    vacMonStereo_.assign(static_cast<size_t>(2 * nframes), 0.0);
                }
                for (int i = 0; i < nframes; ++i) {
                    const double m = monScratch_[static_cast<size_t>(i)];
                    vacMonStereo_[static_cast<size_t>(2 * i + 0)] = m;
                    vacMonStereo_[static_cast<size_t>(2 * i + 1)] = m;
                }
                lyra::wire::xvacOUT(kVac1Id, /*stream*/2, vacMonStereo_.data());
            } else if (static_cast<int>(vacMonSilence_.size()) == 2 * nframes) {
                lyra::wire::xvacOUT(kVac1Id, /*stream*/2,
                                    vacMonSilence_.data());
            }
        }
    }

    // Two mute paths OR together: the operator's manual mute_
    // (Audio panel) AND the auto-mute-on-TX gate (live wire MOX
    // bit, gated by autoMuteOnTx_ pref).  Either silences audio
    // without disturbing the other's persistent state -- so a
    // keyup releases the TX gate and audio resumes at the
    // operator's pre-TX volume immediately.  Both reads are
    // memory_order_relaxed because the gain calc tolerates a
    // one-block stale read (worst case = one ~21 ms output
    // block of pre-edge audio, ear-imperceptible).
    const bool m_manual = muted_.load(std::memory_order_relaxed);
    const bool m_tx     = txMuted_.load(std::memory_order_relaxed) &&
                          autoMuteOnTx_.load(std::memory_order_relaxed);
    double gain =
        (m_manual || m_tx)
            ? 0.0
            : posToGain(volume_.load(std::memory_order_relaxed));
    if (hl2Out_) {
        gain *= kHl2OutAtten;   // tame the hotter AK4951 output
    }
    // Stereo balance (−1 left … +1 right): attenuate the opposite
    // channel.  1.0 each at centre.  Applied as the very last stage.
    const double bal = balance_.load(std::memory_order_relaxed);
    const double lBal = (bal > 0.0) ? (1.0 - bal) : 1.0;
    const double rBal = (bal < 0.0) ? (1.0 + bal) : 1.0;
    // #90 Route 1 jack gain for the monitor (drained at the top into
    // monScratch_): Monitor volume + the same HL2-jack attenuation the RX
    // path uses.  BIN/balance are skipped below (mono self-monitor).
    const double monGain = monVolume_.load(std::memory_order_relaxed)
                         * (hl2Out_ ? kHl2OutAtten : 1.0);
    for (int f = 0; f < nframes; ++f) {
        double l, r;
        if (monActive) {
            // #90 monitor: mono self-listen, same on both channels; no BIN /
            // balance (those are RX niceties).  Replaces the muted RX entirely.
            const double m = monScratch_[static_cast<size_t>(f)] * monGain;
            l = m;
            r = m;
        } else {
            l = audio[static_cast<size_t>(2 * f + 0)] * gain;
            r = audio[static_cast<size_t>(2 * f + 1)] * gain;
            // BIN -- synthesize the stereo pair from the mono (L=R)
            // signal via the Hilbert post-processor (last stage).
            if (binEnabled_)
                binauralStep(l, &l, &r);
            l *= lBal;
            r *= rBal;
        }
        l = std::clamp(l, -1.0, 1.0);
        r = std::clamp(r, -1.0, 1.0);
        // Processed RX-audio doubles for the verbatim OutBound(0) tee
        // (P4.b); pcm16_ still feeds the PC sound-card ring.
        lrWire_[static_cast<size_t>(2 * f + 0)] = l;
        lrWire_[static_cast<size_t>(2 * f + 1)] = r;
        pcm16_[static_cast<size_t>(2 * f + 0)] =
            static_cast<qint16>(l * 32767.0);
        pcm16_[static_cast<size_t>(2 * f + 1)] =
            static_cast<qint16>(r * 32767.0);
    }
    // P2 radio-speaker tee: the final stereo int16 block, whatever the
    // PC/HL2 routing below decides (the bridge silences the PC path via
    // the transient hl2 route when this is active, so it isn't doubled).
    {
        std::lock_guard<std::mutex> lk(radioSinkMtx_);
        if (radioSink_) radioSink_(pcm16_.data(), nframes);
    }
    // P4.b — RX audio → the verbatim EP2 writer via OutBound(0), per the
    // reference asioOUT P1 posture (cmasio.c:137-145) + design §2.
    // OutBound(0) appends to obbuffs ring 0 → ob_main → sendOutbound →
    // prn->outLRbufp + ReleaseSemaphore(hsendLRSem); the writer
    // (sendProtocol1Samples) does the reference 16-bit round-nearest
    // quantize.  Called HERE — the AAMix Outbound consumer is the
    // operator-approved hybrid registration site; create_rnet does NOT
    // SendpOutboundRx (that no-op-stub registration was the B.6.b
    // clobber, commit 8b8e0da — must stay deleted).
    if (hl2Out_) {
        // HL2 onboard codec (HERMES posture): the EP2 LR bytes ARE the
        // headphone-jack audio — hand the processed doubles straight to
        // OutBound(0).  Replaces the OLD hl2AudioPush_ EP2 ring (retired
        // with txWorkerLoop).
        lyra::wire::OutBound(0, nframes, lrWire_.data());
    } else {
        // PC sound card (ASIO posture): real audio to the PC ring, then
        // ZERO the LR and OutBound(0) the zeroed buffer so the wire stays
        // paced (the writer needs hsendLRSem every cycle) while the HL2
        // jack is silent — exactly `memset(buff,0); OutBound(0,…)` at
        // cmasio.c:143-144.
        {
            std::lock_guard<std::mutex> lk(audioMtx_);
            if (audioRing_) audioRing_->push(pcm16_.data(), nframes);
        }
        std::fill_n(lrWire_.data(), static_cast<size_t>(2 * nframes), 0.0);
        lyra::wire::OutBound(0, nframes, lrWire_.data());
    }
}

void WdspEngine::feedIq(const double *iq, int nframes)
{
    // Drop IQ until the channel is live (e.g. samples arriving in the
    // window between stream-open and the deferred openRx1, or after a
    // close).  fexchange0 on a closed channel is undefined.
    if (!running_ || nframes <= 0) {
        return;
    }
    const WdspApi &api = wdsp_->api();
    if (!api.fexchange0) {
        return;
    }

    // Serialise against a main-thread channel reopen (sample-rate
    // switch): hold channelMtx_ for the whole process so the channel +
    // outBuf_ can't be torn down / resized mid-fexchange0.  Re-check the
    // state under the lock (a reopen may have just toggled it).
    std::lock_guard<std::mutex> lk(channelMtx_);
    if (!running_ || !opened_) {
        return;
    }

    // Append this datagram's interleaved IQ to the accumulator.
    accum_.insert(accum_.end(), iq,
                  iq + static_cast<size_t>(2 * nframes));

    // Drain whole in_size blocks through WDSP.  in_size frames =
    // 2*in_size interleaved doubles in, 2*outSize_ doubles out.
    const size_t blockDoubles = static_cast<size_t>(2 * cfg_.inSize);
    while (accum_.size() >= blockDoubles) {
        // Noise blanker (EXT): splice the impulse blanker on the raw IQ
        // block BEFORE WDSP's RXA chain — xnobEXT cleans accum_ front into
        // nbBuf_, and the cleaned block then drives BOTH the demod and the
        // analyzer so audio + panadapter match.  NB off → use accum_.
        double *blockPtr = accum_.data();
        if (nbEnabled_ && nbCreated_ && api.xnobEXT &&
            nbBuf_.size() >= blockDoubles) {
            api.xnobEXT(channel_, accum_.data(), nbBuf_.data());
            blockPtr = nbBuf_.data();
        }
        // Captured noise profile (slice 2): during a capture window,
        // average this post-NB IQ block's per-bin noise power.  feed()
        // only READS blockPtr (runs its own STFT internally) and never
        // alters it — observe-only, audio is unchanged.  noiseProfile_ is
        // touched here + in startNoiseCapture, both under channelMtx_.
        if (noiseCapturing_.load(std::memory_order_relaxed) && noiseProfile_) {
            noiseProfile_->feed(blockPtr, cfg_.inSize);
            noiseProgress_.store(noiseProfile_->progress(),
                                 std::memory_order_relaxed);
            if (!noiseProfile_->capturing()) {
                noiseProfileValid_.store(noiseProfile_->valid(),
                                         std::memory_order_relaxed);
                noiseCapturing_.store(false, std::memory_order_relaxed);
                // Auto-load a freshly-captured profile if apply is on.
                if (applyEnabled_.load(std::memory_order_relaxed) && reducer_ &&
                    noiseProfile_->valid() &&
                    reducer_->fftSize() == noiseProfile_->fftSize()) {
                    reducer_->setProfile(noiseProfile_->noisePower());
                }
            }
        }
        // Slice 3 apply: when enabled + a valid profile is loaded, clean
        // the post-NB IQ block (Wiener-from-profile) and run WDSP + the
        // analyzer on the CLEANED block so audio and panadapter match.
        // Same-count interface → just swap the pointer (one window of
        // latency lives inside the reducer).  Off/no-profile → unchanged.
        double *dspPtr = blockPtr;
        if (applyEnabled_.load(std::memory_order_relaxed) && reducer_ &&
            reducer_->ready()) {
            if (static_cast<int>(cleanBuf_.size()) < 2 * cfg_.inSize) {
                cleanBuf_.resize(static_cast<size_t>(2 * cfg_.inSize));
            }
            reducer_->process(blockPtr, cfg_.inSize, cleanBuf_.data());
            dspPtr = cleanBuf_.data();
        }
        api.fexchange0(channel_, dspPtr, outBuf_.data(), &fexErr_);

        // Step 5: feed the SAME block WDSP saw (cleaned when applying) to
        // the panadapter so the trace matches the audio.
        //
        // Task #44 Phase 2: skip the RX feed when TX owns the analyzer —
        // the TX worker is then feeding pre-iqc TX I/Q via
        // feedTxSpectrumFromSip1 (tx_dsp_worker.cpp post-sip1Ring write).
        // RX-side processing continues (operator may un-mute, audio
        // path stays alive) but the spectrum trace shows TX content
        // during MOX.  Flag set/cleared by setTxOwnsAnalyzer() on the
        // moxActiveChanged edge from main.cpp.
        if (analyzerOpen_ && api.Spectrum0
                && !txOwnsAnalyzer_.load(std::memory_order_acquire)) {
            api.Spectrum0(1, kAnDisp, 0, 0, dspPtr);
        }

        // Zero-beat carrier tracker (RX-only tuning aid): the SAME block WDSP
        // + the panadapter see.  Runs only when enabled AND in a lockable
        // carrier mode (CW/AM/SAM/FM).  All estimator mutation stays on this
        // RX-worker thread; only the result atomics cross to the UI.
        {
            const bool zbRun = zeroBeatEnabled_.load(std::memory_order_relaxed) &&
                               zeroBeatModeOk_.load(std::memory_order_relaxed);
            if (zbRun) {
                if (zbRate_ != cfg_.inRate) {
                    zeroBeat_.setSampleRate(cfg_.inRate);   // resizes + resets
                    zbRate_ = cfg_.inRate;
                } else if (!zbRunPrev_) {
                    zeroBeat_.reset();                       // clean start on re-arm
                }
                // Scan around the MARKER so we lock the operator's signal, not
                // the loudest neighbour.  HL2 baseband is spectrally reversed,
                // so the on-freq carrier sits at −markerOffset.
                const double mk = zbMarkerHz_.load(std::memory_order_relaxed);
                zeroBeat_.setSearchCenter(-mk);
                zeroBeat_.process(dspPtr, cfg_.inSize);
                zbRawHz_.store(zeroBeat_.offsetHz(), std::memory_order_relaxed);
                zbValid_.store(zeroBeat_.valid(),    std::memory_order_relaxed);
            } else if (zbRunPrev_) {
                zbValid_.store(false, std::memory_order_relaxed);
            }
            zbRunPrev_ = zbRun;
        }

        // TCI IQ stream tap: copy this block's interleaved I,Q as float32
        // (queued signal → TCI server frames + sends).  Native inRate.
        if (tciIqOn_.load(std::memory_order_relaxed)) {
            const int n = 2 * cfg_.inSize;          // interleaved scalars
            QByteArray b(n * int(sizeof(float)), Qt::Uninitialized);
            float *d = reinterpret_cast<float *>(b.data());
            for (int i = 0; i < n; ++i) d[i] = static_cast<float>(accum_[size_t(i)]);
            emit tciIqBlock(b, cfg_.inRate);
        }

        // Peak |sample| across L+R as the audio-level proxy (Step 3d
        // is a measurement step — no playback).  20*log10(peak) dBFS.
        double peak = 0.0;
        const int outDoubles = 2 * outSize_;
        for (int i = 0; i < outDoubles; ++i) {
            const double a = std::fabs(outBuf_[static_cast<size_t>(i)]);
            if (a > peak) {
                peak = a;
            }
        }
        const double db = (peak > 0.0) ? 20.0 * std::log10(peak) : -200.0;
        audioDbFs_.store(db, std::memory_order_relaxed);

        // TCI audio stream tap MOVED to dispatchAudioFrame (#90 Route 3) so
        // all monitor routes drain the one ring on one thread.  The
        // tciAudioBlock signal is QueuedConnection, so emitting from the mix
        // thread instead of here is behavior-identical for the client.

        // Step 3e/5 — Stage B.6.b-retry: push the WDSP RX channel's
        // output block into the ported AAMix's stream-0 ring via
        // xMixAudio.  AAMix's mix_main thread wakes on the Ready[0]
        // semaphore release, runs xaamix (1-input passthrough mix,
        // tvol[0]=1.0), and invokes Outbound -> dispatchAudioFrame
        // (the Stage B.6.a helper) on the SAME bytes that used to
        // be passed inline.  Net audio result intended to be byte-
        // identical to the pre-B.6.b inline path; only the dispatch
        // thread changes (feedIq's RX thread -> AAMix's mix_main).
        //
        // Null-check: aaMix_ is briefly nullptr during the close-
        // then-reopen race window; skip the push (one block of
        // silence ~21 ms, operator-imperceptible).
        if (aaMix_ != nullptr) {
            lyra::wire::xMixAudio(aaMix_, 0, /*stream*/ 0,
                                  outBuf_.data());
        }

        // Drop the consumed block; shift the small remainder down.
        accum_.erase(accum_.begin(),
                     accum_.begin() + static_cast<std::ptrdiff_t>(blockDoubles));
    }
}

} // namespace lyra::dsp

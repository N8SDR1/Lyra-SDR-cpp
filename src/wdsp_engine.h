// Lyra — WDSP RX channel engine (Step 3c-ii).
//
// Wraps a single WDSP receiver channel: OpenChannel + the locked
// first-light config (USB 200-3000 Hz, AGC MED, binaural mono) +
// SetChannelState start, with a matching close on teardown.
//
// Scope of Step 3c-ii is CHANNEL LIFECYCLE ONLY — open the channel,
// configure it, start it, prove it opens + closes without crashing.
// No IQ flows through fexchange0 yet (that's Step 3d), no audio is
// produced yet (Step 3e).
//
// Every parameter here is mirrored from the bench-proven Python tree
// (lyra/dsp/wdsp_engine.py RxConfig + RxChannel._open) so the C++
// rebuild starts from a known-good WDSP setup rather than re-deriving
// it.  See CLAUDE.md §14.2 for the load-bearing gotchas:
//   * OpenChannel 13th arg (block) MUST be 1 (block-until-output).
//   * out_size = in_size * out_rate / in_rate (NOT in_size).
//   * Sideband select lives in NBP0 — use RXASetPassband, not
//     SetRXABandpassFreqs (BP1 is bypassed with all DSP off).
//   * SetRXAPanelBinaural(ch, 0) => panel.copy=1 => mono on both
//     L/R; fixes the AM/FM/DSB right-channel-silent bug (§14.10).

#pragma once

#include "wdsp_native.h"

#include <QAudioDevice>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "dsp/MonitorRing.h"   // #90 — TX-monitor SPSC ring (value member)
#include "dsp/CwDecoder.h"     // #173 CW-5a — RX CW decoder (value member)
#include "dsp/deepfist/NeuralCwDecoder.h" // DeepFist neural CW decoder (2nd engine)
#include "dsp/CwArbiter.h"          // Auto-engine ownership arbiter (Phase 1)
#include "dsp/deepfist/ScpLocal.h"  // Phase 3: RBN-confirmed local call list
#include "dsp/deepfist/CwCaptureHarvester.h"  // Phase 2: training harvest
#include "dsp/deepfist/CwHarvestRing.h"
#include "dsp/FreqCalMeasure.h" // freq calibration — carrier tone estimator
#include "dsp/ZeroBeat.h"       // zero-beat carrier-offset tuning aid (value member)

class QAudioSink;

// P0.c direct port — the reference `aamix, *AAMIX` twin typedef
// (full definition in wire/AAMix.h; repeating the typedef against
// the incomplete struct here keeps windows.h out of this header).
namespace lyra::wire { struct _aamix; typedef struct _aamix aamix, *AAMIX; }

namespace lyra::dsp {

// Defined in wdsp_engine.cpp — a QIODevice the QAudioSink pulls audio
// from, backed by a mutex-protected stereo int16 ring.
class AudioRing;

// #59 — the RX-side parametric EQ engine + its analyzer (owned by the RX
// EqModel in lyra::ui).  WdspEngine applies the EQ to the post-RXA audio and
// feeds the analyzer.  Forward-declared here; full types in dsp/ParamEq.h +
// dsp/EqAnalyzer.h (included in the .cpp).
class ParamEq;
class EqAnalyzer;

// Captured noise profile (slice 2) + reducer (slice 3).  Held by
// unique_ptr; only the .cpp needs the complete types.
class CapturedProfile;
class NoiseReducer;

// Per-channel sample rates + buffer size.  Defaults match the working
// HL2 setup: 1024-frame 192 kHz IQ in, 4096-sample
// internal DSP buffer at 48 kHz, 48 kHz audio out.
struct RxConfig {
    int    inSize     = 1024;     // frames per fexchange0 call
    int    dspSize    = 4096;     // internal DSP buffer size
    int    inRate     = 192000;   // IQ input rate (HL2 default)
    int    dspRate    = 48000;    // WDSP internal DSP rate
    int    outRate    = 48000;    // audio output rate (AK4951 fixed)
    // Slew envelope (avoids click on start/stop).
    double tDelayUp   = 0.010;
    double tSlewUp    = 0.025;
    double tDelayDown = 0.000;
    double tSlewDown  = 0.010;
    // 1 = fexchange0 blocks until the DSP thread has produced the next
    // output buffer.  Required for a steady cadence (§14.2).
    int    block      = 1;
};

class WdspEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool   running   READ isRunning  NOTIFY runningChanged)
    // Step 3d: peak audio level out of the DSP chain, dBFS.  Sentinel
    // -200.0 = "no audio produced yet".  Written by the RX worker
    // thread in feedIq(); sampled by the UI at ~5 Hz via levelsChanged.
    Q_PROPERTY(double audioDbFs READ audioDbFs  NOTIFY levelsChanged)
    // Step 3e: operator audio controls.  SAFETY: starts MUTED at a low
    // default volume so the first listen can never be a full-scale
    // blast (operator-reported speaker damage in the Python tree).
    // volume is the SLIDER POSITION (0..1); the actual gain is a
    // perceptual dB taper of it (see wdsp_engine.cpp).  volumeDb is
    // that position expressed in dB, for the UI readout.
    Q_PROPERTY(double volume      READ volume      NOTIFY volumeChanged)
    Q_PROPERTY(double volumeDb    READ volumeDb    NOTIFY volumeChanged)
    Q_PROPERTY(bool   muted       READ muted       NOTIFY mutedChanged)
    // Auto-mute-on-TX (task #26): when the wire MOX bit settles true,
    // RX1 audio is force-muted so the operator doesn't self-deafen
    // off their own TX coupling.  Separate from the operator's manual
    // mute (muted_): both are OR'd into the gain calc so either path
    // silences audio without disturbing the other's state.  txMuted is
    // the live MOX-driven flag; autoMuteOnTx is the operator's master
    // switch (persisted, default ON — sane safety posture).  When
    // autoMuteOnTx is OFF, txMuted has no effect on the gain.
    Q_PROPERTY(bool   txMuted     READ txMuted     NOTIFY txMutedChanged)
    Q_PROPERTY(bool   autoMuteOnTx READ autoMuteOnTx NOTIFY autoMuteOnTxChanged)
    Q_PROPERTY(int    rxResumeDelayMs READ rxResumeDelayMs WRITE setRxResumeDelayMs NOTIFY rxResumeDelayMsChanged)
    // AF makeup gain (WDSP RXA panel gain), 0..+40 dB — a pre-volume
    // output trim so you can set a comfortable WDSP level and ride Volume
    // on top.  Default 0 dB (unity).  Stereo BALANCE (−1 left … +1 right)
    // pans the mono demod across L/R in the output stage (Lyra-side).
    Q_PROPERTY(double afGainDb    READ afGainDb    NOTIFY afGainChanged)
    Q_PROPERTY(double balance     READ balance     NOTIFY balanceChanged)
    // #90 TX monitor — hear your own post-rack (Speech/EQ/Combinator/
    // Plating) TX audio on the headphone jack while transmitting.
    // monEnabled is the MON toggle; monVolume (0..1, perceptual like
    // volume_) sets the level.  Both persisted.  Consumed on the audio
    // thread in dispatchAudioFrame when MOX is up + MON on (Stage 3);
    // inert until that stage lands.
    Q_PROPERTY(bool   monEnabled  READ monEnabled  NOTIFY monEnabledChanged)
    Q_PROPERTY(double monVolume   READ monVolume   NOTIFY monVolumeChanged)
    Q_PROPERTY(int    audioDeviceIndex READ audioDeviceIndex NOTIFY audioDeviceChanged)
    // Panadapter frequency span (Hz) = the IQ sample rate DIVIDED BY the
    // zoom factor — the displayed bandwidth, centred on the RX1 DDC freq.
    // Used by the QML frequency scale / drag-pan / cursor readout, which
    // all track zoom for free because the displayed span shrinks as zoom
    // rises.  NOTIFY spanChanged (emitted on a rate OR a zoom change).
    Q_PROPERTY(int    spanHz READ spanHz NOTIFY spanChanged)
    // Panadapter zoom: 1.0 = full IQ span; higher magnifies the band
    // CENTRE.  Done old-Lyra-style — the analyzer always produces a
    // full-span, full-resolution spectrum; copySpectrum crops the centre
    // 1/zoom slice and resamples it to display width.  No live analyzer
    // reconfiguration (that corrupts the trace), pure display-side crop.
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    // Demod mode (USB/LSB/CWU/CWL/DSB/AM/FM/DIGU/DIGL) + RX filter
    // bandwidth (Hz).  setMode/setBandwidth drive SetRXAMode +
    // RXASetPassband live, using old Lyra's per-mode passband sign rules
    // (sideband-correct against the HL2 mirrored baseband).
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(int bandwidth READ bandwidth WRITE setBandwidth
               NOTIFY bandwidthChanged)
    // Current RX filter passband edges (Hz OFFSET from the tuned centre)
    // for the panadapter overlay — derived from mode + bandwidth (+ CW
    // pitch) by the same rules applyModeFilter pushes to WDSP.
    Q_PROPERTY(double passbandLowHz  READ passbandLowHz  NOTIFY passbandChanged)
    Q_PROPERTY(double passbandHighHz READ passbandHighHz NOTIFY passbandChanged)
    // CW tone pitch (Hz).  In CW the displayed VFO is the signal CARRIER;
    // the DDS is offset by ±pitch so the carrier lands in the pitch-
    // centred filter (standard HF SDR convention).
    Q_PROPERTY(int cwPitchHz READ cwPitchHz WRITE setCwPitchHz
               NOTIFY cwPitchChanged)
    // Zero-beat tuning aid (Visuals → Show Zero-beat markers).  Signed offset
    // (Hz) of the strongest carrier from the operator's dialled frequency;
    // valid only with a lockable carrier present; active only when enabled +
    // in CW / AM / SAM / FM + running.  RX-only, no effect on audio / TX.
    Q_PROPERTY(double zeroBeatOffsetHz READ zeroBeatOffsetHz NOTIFY zeroBeatChanged)
    Q_PROPERTY(bool   zeroBeatValid    READ zeroBeatValid    NOTIFY zeroBeatChanged)
    Q_PROPERTY(bool   zeroBeatActive   READ zeroBeatActive   NOTIFY zeroBeatChanged)
    // VFO − DDS, Hz: +pitch in CWU, −pitch in CWL, 0 otherwise.  The
    // tuning layer uses it (VFO = DDS + markerOffset) and the panadapter
    // draws the carrier marker offset by it.
    Q_PROPERTY(int markerOffsetHz READ markerOffsetHz NOTIFY markerOffsetChanged)
    // #173 CW-5a — RX CW decoder enable.  The decode tap in dispatchAudioFrame
    // is gated on this AND CW mode (CWU/CWL only); off == zero RX-audio impact.
    // Decoded text / WPM / AFC-lock surface via the cwDecoded* signals to the
    // separate CW decoder panel (CW-5b).
    Q_PROPERTY(bool cwDecodeEnabled READ cwDecodeEnabled WRITE setCwDecodeEnabled
               NOTIFY cwDecodeEnabledChanged)
    // DeepFist — which CW decode engine is active (0=Classic fldigi, 1=Neural)
    // and whether the neural model actually loaded (drives the panel's toggle
    // + "model not found" status).
    Q_PROPERTY(int cwDecodeEngine READ cwDecodeEngine WRITE setCwDecodeEngine
               NOTIFY cwDecodeEngineChanged)
    Q_PROPERTY(bool cwNeuralAvailable READ cwNeuralAvailable
               NOTIFY cwNeuralAvailableChanged)
    // Learn / Harvest are no longer user-facing chips.  Learn (record RBN-
    // confirmed calls into the local SCP list) is ON for everyone by default,
    // disabled only with LYRA_CW_LEARN=0.  Harvest (write trust-tiered CW audio
    // segments to disk for DeepFist training) is a developer tool, OFF unless
    // LYRA_CW_HARVEST is set truthy.  Both are read once at construction, so the
    // panel binds them as CONSTANT.
    Q_PROPERTY(bool cwLearnEnabled READ cwLearnEnabled CONSTANT)
    Q_PROPERTY(bool cwHarvestEnabled READ cwHarvestEnabled CONSTANT)
    // DeepFist — live CTC blank-logit penalty (0..5).  Higher recovers dropped
    // chars on weak audio, adds spurious chars on strong signals.
    Q_PROPERTY(double cwBlankPenalty READ cwBlankPenalty WRITE setCwBlankPenalty
               NOTIFY cwBlankPenaltyChanged)
    // ── RX DSP operator controls (ported from old Lyra's DSP+AUDIO
    // panel).  Noise reduction = WDSP EMNR.  nrMode 1..4 picks the
    // gain function (Wiener+SPP / Wiener / MMSE-LSA / trained); AEPF is
    // the anti-musical-noise post-filter; NPE 0=OSMS 1=MCRA picks the
    // noise-power estimator.  All persisted via QSettings; pushed to
    // WDSP on every change and re-applied on channel (re)open. ───────
    Q_PROPERTY(bool nrEnabled READ nrEnabled NOTIFY nrChanged)
    Q_PROPERTY(int  nrMode     READ nrMode    NOTIFY nrChanged)
    Q_PROPERTY(bool aepfEnabled READ aepfEnabled NOTIFY nrChanged)
    Q_PROPERTY(int  npeMethod  READ npeMethod  NOTIFY nrChanged)
    // AGC mode as an operator-facing string: off / fast / med / slow.
    // (Long / Auto / Custom land with the rest of the AGC surface.)
    Q_PROPERTY(QString agcMode READ agcMode NOTIFY agcModeChanged)
    // Live AGC gain action (WDSP RXA_AGC_GAIN), dB — re-read at the 5 Hz
    // levels poll.  agcThreshDb is the (currently fixed) AGC threshold the
    // readout shows alongside it, matching old Lyra's "thr / gain" cells.
    Q_PROPERTY(double agcGainDb   READ agcGainDb   NOTIFY levelsChanged)
    Q_PROPERTY(double agcThreshDb READ agcThreshDb CONSTANT)
    // ANF — auto-notch (LMS predictor that nulls carriers/heterodynes).
    Q_PROPERTY(bool anfEnabled READ anfEnabled NOTIFY anfChanged)
    // LMS — line enhancer (ANR predictor that lifts CW/tones).  strength
    // 0..1 scales taps (32..128) + adapt rate; 0.5 ≈ WDSP-class default.
    Q_PROPERTY(bool   lmsEnabled  READ lmsEnabled  NOTIFY lmsChanged)
    Q_PROPERTY(double lmsStrength READ lmsStrength NOTIFY lmsChanged)
    // Manual notches (NF) — master run + the notch list.  Each notch is
    // a QVariantMap { offsetHz, widthHz, active } where offsetHz is the
    // baseband offset from the tuned centre (= what the panadapter
    // shows).  The panadapter draws the red bands + does right-click /
    // drag / wheel placement against this list.
    Q_PROPERTY(bool notchEnabled READ notchEnabled NOTIFY notchesChanged)
    Q_PROPERTY(QVariantList notches READ notches NOTIFY notchesChanged)
    // All-mode squelch (SQ).  One operator threshold 0..1; the engine
    // routes it to SSQL / FMSQ / AMSQ per the current demod mode.
    Q_PROPERTY(bool squelchEnabled READ squelchEnabled NOTIFY squelchChanged)
    Q_PROPERTY(double squelchThreshold READ squelchThreshold NOTIFY squelchChanged)
    // Noise blanker (NB) — impulse blanker on the raw IQ.  strength 0..1
    // maps to the NOB detection threshold (higher strength = lower
    // threshold = more aggressive blanking).
    Q_PROPERTY(bool nbEnabled READ nbEnabled NOTIFY nbChanged)
    Q_PROPERTY(double nbStrength READ nbStrength NOTIFY nbChanged)
    // APF — CW audio peaking filter, centred on the CW pitch.  Engages
    // only in CWU/CWL; the button is live in any mode but does nothing
    // outside CW (gated by the engine).
    Q_PROPERTY(bool apfEnabled READ apfEnabled NOTIFY apfChanged)
    // APF peak gain (dB) the operator picks for how hard the CW peak lifts.
    Q_PROPERTY(double apfGainDb READ apfGainDb NOTIFY apfChanged)
    // BIN — binaural pseudo-stereo (headphone soundstage widening).  A
    // Lyra-native Hilbert post-processor on the mono output (not a WDSP
    // call).  depth 0..1: 0 = mono, 1 = full Hilbert pair.
    Q_PROPERTY(bool binEnabled READ binEnabled NOTIFY binChanged)
    Q_PROPERTY(double binDepth READ binDepth NOTIFY binChanged)
    // Captured noise profile (IQ-domain spectral subtraction — Lyra's
    // signature NR).  Slice 2 = capture only: noiseCapturing is true
    // while a 📷 Cap window is averaging the band's noise power;
    // noiseCaptureProgress is 0..1; noiseProfileValid flips true once a
    // capture completes.  Apply (the toggle that actually subtracts) and
    // the named-profile store land in slices 3/4.
    Q_PROPERTY(bool noiseCapturing READ noiseCapturing NOTIFY noiseCaptureChanged)
    Q_PROPERTY(double noiseCaptureProgress READ noiseCaptureProgress
               NOTIFY noiseCaptureChanged)
    Q_PROPERTY(bool noiseProfileValid READ noiseProfileValid
               NOTIFY noiseCaptureChanged)
    // Slice 3: the apply toggle.  When on AND a valid profile is loaded,
    // feedIq cleans the IQ (Wiener-from-profile) before WDSP.  Default
    // OFF — the operator opts in; adds ~one STFT window of RX latency.
    Q_PROPERTY(bool noiseApplyEnabled READ noiseApplyEnabled
               NOTIFY noiseApplyChanged)
    // Slice 4: capture FFT size (2048/4096/8192) + capture duration
    // (3/5/10 s), and the named-profile store (manual-curated; each
    // profile is rate + FFT-size tagged).  noiseProfiles = saved names;
    // noiseActiveProfile = the loaded one ("" = none / unsaved live).
    Q_PROPERTY(int noiseFftSize READ noiseFftSize NOTIFY noiseSettingsChanged)
    Q_PROPERTY(double noiseCaptureSeconds READ noiseCaptureSeconds
               NOTIFY noiseSettingsChanged)
    Q_PROPERTY(QStringList noiseProfiles READ noiseProfiles
               NOTIFY noiseProfilesChanged)
    Q_PROPERTY(QString noiseActiveProfile READ noiseActiveProfile
               NOTIFY noiseProfilesChanged)
    // Slice 4B: apply tunables (live).  strength = over-subtraction α
    // (1 = standard, higher = more aggressive); floorDb = max attenuation
    // (deeper = more noise cut, more musical-noise risk); smoothing =
    // per-bin mask smoothing 0..0.95 (higher = steadier / less twinkle).
    Q_PROPERTY(double noiseStrength READ noiseStrength NOTIFY noiseTuningChanged)
    Q_PROPERTY(double noiseFloorDb READ noiseFloorDb NOTIFY noiseTuningChanged)
    Q_PROPERTY(double noiseSmoothing READ noiseSmoothing NOTIFY noiseTuningChanged)

public:
    explicit WdspEngine(WdspNative *wdsp, QObject *parent = nullptr);
    ~WdspEngine() override;

    bool isRunning() const { return running_; }
    // TciServer Task #68 needs the WdspApi to call the polyphase
    // float-vector resampler (create_resampleFV / xresampleFV /
    // destroy_resampleFV).  Read-only access; engine retains
    // ownership.  May be null when the engine is constructed
    // before WdspNative has finished loading — callers must check.
    WdspNative *wdspNative() const { return wdsp_; }

    double audioDbFs() const {
        return audioDbFs_.load(std::memory_order_relaxed);
    }

    // #91 VOX anti-VOX — latest RMS (linear [0,1)) of the heard RX audio.
    // Thread-safe (atomic); the HL2Stream VOX poll pulls this to suppress
    // keying on studio-monitor bleed.
    double voxRxAudioRmsLin() const {
        return voxRxRmsLin_.load(std::memory_order_relaxed);
    }

    // In-passband RX signal strength (WDSP RXA_S_PK), in WDSP's raw
    // dBm-ish units — the standard WDSP S-meter source.
    // Returns -200 when the RX channel isn't running.  Safe to call
    // from the UI thread (just reads WDSP's latest stored meter value).
    double sMeterDbm() const;
    // Live AGC gain action in dB (WDSP RXA_AGC_GAIN); 0 when not running.
    double agcGainDb() const;
    // #158 (post-DL) TX dynamics meters re-homed onto the wire-live TXA
    // channel.  txaMeterType = WDSP txaMeterType ordinal (TXA.h): MIC_PK=0,
    // LVLR_PK=4, LVLR_GAIN=6, ALC_PK=12, ALC_GAIN=14.  NaN when not available.
    double txMeterRaw(int txaMeterType) const;
    // The configured AGC threshold the readout shows (dBFS, currently the
    // fixed first-light value; becomes operator-tunable with the AGC
    // threshold control later).
    double agcThreshDb() const;

    double volume() const { return volume_.load(std::memory_order_relaxed); }
    double volumeDb() const;   // slider position -> dB (for UI readout)
    bool   muted()  const { return muted_.load(std::memory_order_relaxed); }
    bool   txMuted() const { return txMuted_.load(std::memory_order_relaxed); }
    bool   autoMuteOnTx() const { return autoMuteOnTx_.load(std::memory_order_relaxed); }
    int    rxResumeDelayMs() const { return rxResumeDelayMs_.load(std::memory_order_relaxed); }
    double afGainDb() const { return afGainDb_; }
    double balance()  const { return balance_.load(std::memory_order_relaxed); }
    bool   monEnabled() const { return monEnabled_.load(std::memory_order_relaxed); }
    double monVolume()  const { return monVolume_.load(std::memory_order_relaxed); }
    // Output-list index: 0 = HL2 audio jack, 1..N = PC devices.
    int    audioDeviceIndex() const {
        return hl2Out_ ? 0 : deviceIndex_ + 1;
    }
    double zoom() const { return zoom_.load(std::memory_order_relaxed); }
    // Span reported to QML's freq-scale binding.  RX state: rate /
    // zoom (same as before).  TX state (txOwnsAnalyzer_=true, Task
    // #44 Phase 2 MOX-edge swap): the WDSP TX sip1 dsp_rate
    // (txSpanHz_), still divided by zoom.  Span change emitted on
    // every MOX edge via setTxOwnsAnalyzer().
    //
    // inRate read is via inRateAtomic_ (mirror of cfg_.inRate
    // updated under channelMtx_ in setSampleRate) so this getter is
    // race-free against rate-change without taking the lock — per
    // amendment A.6 in the reconciled doc.
    int    spanHz() const {
        const double z = zoom_.load(std::memory_order_relaxed);
        const double zClamp = (z > 1.0 ? z : 1.0);
        if (txOwnsAnalyzer_.load(std::memory_order_acquire)) {
            const int r = txSpanHz_.load(std::memory_order_relaxed);
            return static_cast<int>(r / zClamp);
        }
        const int r = inRateAtomic_.load(std::memory_order_relaxed);
        return static_cast<int>(r / zClamp);
    }
    // Task #44 Phase 2 — analyzer ownership accessor for TxDspWorker
    // (called per block-pack tick to decide whether to feed the TX
    // sip1 samples into Spectrum0).  Wired in step 5; reads as false
    // until the MOX-edge swap flips it.
    bool   txOwnsAnalyzer() const {
        return txOwnsAnalyzer_.load(std::memory_order_acquire);
    }
    // Task #44 Phase 2 — MOX-edge analyzer source swap.  Called
    // from main.cpp on every moxActiveChanged signal:
    //   on=true  (keydown post-mox_delay+rf_delay): reconfigure
    //     kAnDisp for the 96 kHz sip1 feed via
    //     configureAnalyzerForTx(), then store txOwnsAnalyzer_=true
    //     (release).  RX worker's feedIq sees the flag on next
    //     block + skips Spectrum0; TX worker's feed call gates on
    //     it + starts feeding pre-iqc samples.
    //   on=false (keyup post-ptt_out_delay): restore the RX-side
    //     analyzer config via configureAnalyzerForRx(), then store
    //     txOwnsAnalyzer_=false (release).  RX feed resumes on
    //     next block.
    // Order: SetAnalyzer reconfigure FIRST under both channelMtx_
    // (lifecycle vs feed) AND analyzerMtx_ (taken inside the
    // configure helpers), THEN release-store the flag.  Per
    // amendment A.2 (reconciled doc).  Emits spanChanged() so the
    // QML freq scale picks up the new (or restored) span via
    // spanHz() in both directions.
    void   setTxOwnsAnalyzer(bool on);

    // Frames fexchange0 writes per process() call (= in_size *
    // out_rate / in_rate).  Step 3d sizes its output buffer to this.
    int outSize() const { return outSize_; }

    // Step 5: WDSP spectral analyzer (panadapter source).  The IQ fed
    // to the audio chain is also fed to the analyzer; copySpectrum
    // pulls the latest display-width dB array (called from the
    // panadapter's render thread — WDSP serialises feed-vs-read
    // internally, as standard SDR apps rely on).  Plain C++ (not Q_INVOKABLE):
    // the panadapter is a C++ QQuickPaintedItem, not QML JS.
    int  spectrumPixelCount() const;
    int  copySpectrum(float *dst, int maxN);
    // §15.29 C1 — waterfall-specific spectrum read.  During TX state,
    // the analyzer is configured with n_pixout=2 (configureAnalyzerForTx)
    // — pixout=0 = panadapter (30 ms tau IIR), pixout=1 = waterfall
    // (120 ms tau IIR, 4× smoother).  This method reads pixout=1
    // during TX; falls through to pixout=0 during RX (no separate
    // waterfall pixout in RX state — §15.29 C2 deferred).  Internal
    // cache mirror of copySpectrum's pattern so dual consumers stay
    // fed across GetPixels' single-read ready-flag.
    int  copyWaterfallSpectrum(float *dst, int maxN);
    // P4.b TUN display-honesty.  During TUN the TX analyzer's DC bin sits
    // at the TX NCO (dial ∓ cw_pitch), but the panadapter axis is centred
    // on the RX DDS (= dial).  Without correction the gen1 tune carrier
    // (at −cw_pitch baseband relative to the NCO, i.e. AT the dial on air)
    // paints cw_pitch Hz off the marker.  This setter feeds copySpectrum /
    // copyWaterfallSpectrum the NCO−dial offset (Hz) so the TX-state crop
    // is shifted to render the carrier at its true RF (on the SSB marker).
    // 0 in RX and in voice TX (NCO == dial).  Set from HL2Stream tune state.
    void setTxAnalyzerOffsetHz(int hz) {
        txAnalyzerOffsetHz_.store(hz, std::memory_order_relaxed);
    }
    // Display-only notch cut: pull the columns under each
    // active manual notch down to floorDb in a display dB array of `n`
    // points spanning the CURRENT displayed span.  Called by the
    // panadapter (floor = its noise floor) + waterfall (floor = scale
    // bottom) AFTER their noise-floor / auto-scale math, so the notch
    // visibly carves the trace + waterfall without skewing those
    // estimates.  No-op when NF is off or there are no notches.
    void carveNotches(float *db, int n, double floorDb) const;

    // Step 3e operator audio controls (call from the UI / main thread).
    // setVolume: linear gain 0.0..1.0 applied before int16 conversion.
    // setMuted:  hard mute (gain 0) without losing the volume setting.
    // audioOutputDevices: the operator's PC output devices, by name.
    // setAudioOutputDevice: switch output device live (restarts sink).
    Q_INVOKABLE void setVolume(double v);
    Q_INVOKABLE void setMuted(bool m);
    // Auto-mute-on-TX driver: setTxMuted is wired to HL2Stream's
    // moxActiveChanged(bool) signal in main.cpp — fires true at the end
    // of the keydown TR-delay (wire MOX bit settled) and false at the
    // end of the keyup ptt_out_delay (RF gone).  No persistence — pure
    // transient state.  setAutoMuteOnTx is the operator's master switch
    // (persisted; default ON).
    Q_INVOKABLE void setTxMuted(bool m);
    Q_INVOKABLE void setAutoMuteOnTx(bool on);
    // RX-on-unkey delay: ms to hold RX audio muted AFTER the keyup MOX-off
    // edge, so the TX-coupled tail flushes + the T/R settles before audio
    // resumes.  Persisted; 0 = resume instantly (old behaviour).
    Q_INVOKABLE void setRxResumeDelayMs(int ms);
    Q_INVOKABLE void setAfGainDb(double db);   // 0..+40 dB makeup (WDSP panel gain)
    Q_INVOKABLE void setBalance(double b);     // -1 (L) .. +1 (R)
    Q_INVOKABLE void setMonEnabled(bool on);   // #90 TX monitor toggle
    Q_INVOKABLE void setMonVolume(double v);   // #90 monitor level 0..1

    // RX DSP operator controls.  Getters are cheap reads of the
    // persisted state; setters store, persist (QSettings), push to WDSP
    // when the channel is open, and emit the NOTIFY signal.
    bool nrEnabled()   const { return nrEnabled_; }
    int  nrMode()      const { return nrMode_; }
    bool aepfEnabled() const { return aepfEnabled_; }
    int  npeMethod()   const { return npeMethod_; }
    QString agcMode()  const { return agcMode_; }
    Q_INVOKABLE void setNrEnabled(bool on);
    Q_INVOKABLE void setNrMode(int mode);        // 1..4
    Q_INVOKABLE void setAepfEnabled(bool on);
    Q_INVOKABLE void setNpeMethod(int method);   // 0=OSMS 1=MCRA
    Q_INVOKABLE void setAgcMode(const QString &mode);  // off/fast/med/slow
    bool anfEnabled()    const { return anfEnabled_; }
    bool lmsEnabled()    const { return lmsEnabled_; }
    double lmsStrength() const { return lmsStrength_; }
    Q_INVOKABLE void setAnfEnabled(bool on);
    Q_INVOKABLE void setLmsEnabled(bool on);
    Q_INVOKABLE void setLmsStrength(double s);   // 0..1
    // ── Manual notches ──
    bool notchEnabled() const { return notchEnabled_; }
    QVariantList notches() const;                 // {offsetHz,widthHz,active}…
    Q_INVOKABLE void setNotchEnabled(bool on);
    // Add a notch at `offsetHz` from centre, default 200 Hz wide.
    // Returns the new notch index.  Auto-enables NF.
    Q_INVOKABLE int  addNotch(double offsetHz, double widthHz = 200.0);
    Q_INVOKABLE void removeNotch(int index);
    Q_INVOKABLE void moveNotch(int index, double offsetHz);
    Q_INVOKABLE void setNotchWidth(int index, double widthHz);
    Q_INVOKABLE int  notchCount() const { return static_cast<int>(notches_.size()); }
    // Nearest notch to `offsetHz` within `tolHz`, else -1 (panadapter
    // hit-testing for drag / wheel / right-click-remove).
    Q_INVOKABLE int  notchNear(double offsetHz, double tolHz) const;
    // ── All-mode squelch ──
    bool   squelchEnabled()   const { return squelchEnabled_; }
    double squelchThreshold() const { return squelchThreshold_; }
    Q_INVOKABLE void setSquelchEnabled(bool on);
    Q_INVOKABLE void setSquelchThreshold(double t);   // 0..1
    bool   nbEnabled()  const { return nbEnabled_; }
    double nbStrength() const { return nbStrength_; }
    Q_INVOKABLE void setNbEnabled(bool on);
    Q_INVOKABLE void setNbStrength(double s);          // 0..1
    bool apfEnabled() const { return apfEnabled_; }
    double apfGainDb() const { return apfGainDb_; }
    Q_INVOKABLE void setApfEnabled(bool on);
    Q_INVOKABLE void setApfGainDb(double db);   // peak gain, dB
    bool binEnabled() const { return binEnabled_; }
    double binDepth() const { return binDepth_; }
    Q_INVOKABLE void setBinEnabled(bool on);
    Q_INVOKABLE void setBinDepth(double d);     // 0..1
    // ── Captured noise profile (slice 2: capture) ──
    bool   noiseCapturing() const {
        return noiseCapturing_.load(std::memory_order_relaxed);
    }
    double noiseCaptureProgress() const {
        return noiseProgress_.load(std::memory_order_relaxed);
    }
    bool   noiseProfileValid() const {
        return noiseProfileValid_.load(std::memory_order_relaxed);
    }
    // Arm a noise capture over `seconds` (3/5/10) at the current IQ rate.
    // Observe-only: averages the band's per-bin noise power; does NOT
    // alter audio.  Safe to call while running (serialised via channelMtx_).
    Q_INVOKABLE void startNoiseCapture(double seconds);
    Q_INVOKABLE void cancelNoiseCapture();
    // Slice 3: turn captured-profile noise reduction on/off.  Loads the
    // current captured profile into the reducer on enable; a fresh
    // capture completed while enabled is auto-loaded.  No-op-on-audio
    // until a valid profile exists.
    bool noiseApplyEnabled() const {
        return applyEnabled_.load(std::memory_order_relaxed);
    }
    Q_INVOKABLE void setNoiseApply(bool on);
    // ── Captured-profile settings + named-profile store (slice 4) ──
    int    noiseFftSize()        const { return npFftSize_; }
    double noiseCaptureSeconds() const { return npCaptureSeconds_; }
    QStringList noiseProfiles()  const;
    QString noiseActiveProfile() const { return npActiveName_; }
    // FFT size 2048/4096/8192 — changing it cancels any capture, drops the
    // reducer + apply (a profile is size-specific), and persists.
    Q_INVOKABLE void setNoiseFftSize(int n);
    Q_INVOKABLE void setNoiseCaptureSeconds(double s);  // 3/5/10
    // Capture using the configured duration (convenience for the UI).
    Q_INVOKABLE void startNoiseCaptureDefault() { startNoiseCapture(npCaptureSeconds_); }
    // Save the current (just-captured) profile under `name`; false if no
    // valid capture exists.  Overwrites a same-named profile.
    Q_INVOKABLE bool saveNoiseProfile(const QString &name);
    // Load a saved profile into the reducer.  Adopts the profile's FFT
    // size; refuses (returns false) if its capture RATE differs from the
    // current IQ rate (caller shows a recapture hint).
    Q_INVOKABLE bool loadNoiseProfile(const QString &name);
    Q_INVOKABLE void deleteNoiseProfile(const QString &name);
    // Rename a saved profile (and its file).  False if not found or the
    // new name is empty / already in use.
    Q_INVOKABLE bool renameNoiseProfile(const QString &oldName,
                                        const QString &newName);
    // UI helpers that use NATIVE top-level dialogs (QInputDialog /
    // QMessageBox) — QML Popups/Dialogs are clipped to the panel's
    // QQuickWidget rect, so the name prompt + warnings must be native.
    Q_INVOKABLE bool promptSaveProfile();                 // ask name → save
    Q_INVOKABLE void loadProfileOrWarn(const QString &name);  // load, warn on rate mismatch
    // Human-readable tag for a saved profile ("192k · 4096 · 2026-05-25").
    Q_INVOKABLE QString noiseProfileInfo(const QString &name) const;
    // Folder where profiles are stored (one .lnp file each), so the UI
    // can tell the operator where to find / back them up.
    Q_INVOKABLE QString noiseProfilesDir() const;
    // Apply tunables (slice 4B) — persisted; pushed to the reducer live
    // and re-applied whenever the reducer is (re)created.
    double noiseStrength()  const { return npAlpha_; }
    double noiseFloorDb()   const { return npFloorDb_; }
    double noiseSmoothing() const { return npSmoothing_; }
    Q_INVOKABLE void setNoiseStrength(double a);    // 1.0 .. 3.0
    Q_INVOKABLE void setNoiseFloorDb(double db);    // -24 .. -3
    Q_INVOKABLE void setNoiseSmoothing(double s);   // 0 .. 0.95
    // Set the panadapter zoom (1.0 = full span).  Stores the factor; the
    // crop happens in copySpectrum.  No analyzer reconfiguration.
    Q_INVOKABLE void setZoom(double z);
    // Demod mode + RX bandwidth.  Stored when the channel is closed and
    // applied on open; applied live (SetRXAMode + RXASetPassband) while
    // running.
    QString mode() const { return mode_; }
    Q_INVOKABLE void setMode(const QString &m);

    // Frequency calibration (Stage 3b) — arm/disarm the carrier-tone
    // measurement.  While on, the post-demod RX audio is also fed to the
    // FreqCalMeasure estimator, which reports the dominant tone via
    // freqCalUpdated(measuredHz, snrDb, windows).  Off = one relaxed bool
    // read on the audio path (zero impact).  See freq_calibration_design.md.
    Q_INVOKABLE void setFreqCalMeasuring(bool on);

    // #159 slim DSP — per-mode-family filter type (Linear Phase vs Low
    // Latency / minimum phase).  Opt-in: defaults all Linear Phase
    // (mp=false) = byte-identical to legacy behaviour.  Auto-applied on
    // mode change + channel open; persisted to QSettings dsp/<fam>_<dir>_mp.
    // CW family is RX-only (no CW-TX filter — keyer/firmware handles CW TX).
    enum class DspFamily { Phone = 0, FM = 1, CW = 2, Dig = 3 };
    bool dspFilterMinPhase(DspFamily fam, bool tx) const;
    void setDspFilterMinPhase(DspFamily fam, bool tx, bool minPhase);
    // #159 — the TXA channel (chid 1) is created lazily by the wire
    // layer's create_xmtr() at stream-connect, NOT at startup.  main()
    // flips this around create_xmtr()/destroy_xmtr() so applyDspFilterTypes
    // never calls TXASetMP() on a non-existent txa[1] (would AV inside
    // wdsp.dll).  Setting it true re-applies the TX filter type.
    void setTxaChannelOpen(bool open);

    int  bandwidth() const { return bw_; }
    Q_INVOKABLE void setBandwidth(int hz);
    // IQ sample rate (Hz).  Switching reopens the WDSP channel +
    // analyzer at the new rate (panadapter span follows).  Safe to call
    // while running — serialised against feedIq via channelMtx_.
    int  sampleRate() const { return cfg_.inRate; }
    Q_INVOKABLE void setSampleRate(int hz);
    double passbandLowHz()  const { return passbandLowHz_; }
    double passbandHighHz() const { return passbandHighHz_; }
    int  cwPitchHz() const { return static_cast<int>(cwPitchHz_ + 0.5); }
    Q_INVOKABLE void setCwPitchHz(int hz);

    // Zero-beat tuning aid (Q_PROPERTY-backed, RX-only).  The estimator runs
    // on the RX worker (feedIq); these read published atomics on the UI thread.
    double zeroBeatOffsetHz() const;   // signed Hz from dialled carrier (0 if invalid)
    bool   zeroBeatValid()    const;   // a lockable carrier is present
    bool   zeroBeatActive()   const;   // enabled + CW/AM/SAM/FM + running
    Q_INVOKABLE void setZeroBeatEnabled(bool on);  // driven by Prefs.zeroBeatMarkers

    // #173 CW-5a — RX CW decoder controls.  Enable gates the dispatchAudioFrame
    // tap (CW-mode-gated); the knob forwards are plain stores on the decoder
    // (audio-thread-safe, benign one-block staleness).  Decode outputs arrive
    // via the cwDecoded* signals.  AFC centre tracks cwPitchHz_ automatically.
    bool cwDecodeEnabled() const { return cwDecodeOn_.load(std::memory_order_relaxed); }
    Q_INVOKABLE void setCwDecodeEnabled(bool on);
    // fldigi CW-receiver controls — the ONLY knobs fldigi's CW RX exposes.
    Q_INVOKABLE void setCwDecodeBandwidthHz(int hz)    { cwDecoder_.setBandwidthHz(hz); }
    Q_INVOKABLE void setCwDecodeSpeedWpm(int wpm)      { cwDecoder_.setSpeedWpm(wpm); }
    Q_INVOKABLE void setCwDecodeTracking(bool on)      { cwDecoder_.setTracking(on); }
    Q_INVOKABLE void setCwDecodeMatchedFilter(bool on) { cwDecoder_.setMatchedFilter(on); }
    Q_INVOKABLE void setCwDecodeSquelch(bool on, double value) { cwDecoder_.setSquelch(on, value); }
    // Live squelch signal metric (SNR 0..100) for the panel bar; polled by QML.
    Q_INVOKABLE double cwDecodeMetric() const { return cwDecoder_.squelchMetric(); }
    int  cwRxWpm() const { return cwDecoder_.rxWpm(); }

    // DeepFist neural CW decoder — second, selectable engine.  Both engines
    // share the CW-mode-gated audio tap; only the selected one runs.  Unlike
    // the classic engine (incremental cwDecodedChar), the neural engine emits
    // the FULL decoded text of the current 6 s window via cwNeuralText.
    int  cwDecodeEngine() const { return cwEngine_.load(std::memory_order_relaxed); }
    Q_INVOKABLE void setCwDecodeEngine(int engine);
    bool cwNeuralAvailable() const { return neuralCw_.ready(); }
    bool cwLearnEnabled()   const { return cwLearnEnabled_; }
    bool cwHarvestEnabled() const { return cwHarvestEnabled_; }

    // CW panel — is this decoded token a KNOWN-REAL callsign?  Exact
    // MASTER.SCP membership (loaded with the neural model), OR'd with the
    // Phase-3 local list of RBN-confirmed calls heard at this station (live —
    // a call noted this session ambers immediately; the rescorer picks it up
    // at the next model load).
    Q_INVOKABLE bool cwCallKnown(const QString& call) {
        const std::string u = call.trimmed().toUpper().toStdString();
        if (neuralCw_.scpKnows(u)) return true;
        ensureCwScpLocal();
        return cwScpLocal_.contains(u);
    }

    // CW panel — remember an RBN/cluster-confirmed call copied off the air
    // (the cwLearnEnabled gate lives in QML; this always records).  GUI
    // thread only.
    Q_INVOKABLE void cwNoteConfirmedCall(const QString& call);

    // Phase 2 harvest — opt-in capture of trust-tiered CW segments for
    // DeepFist training (spec §6).  Enabling creates <Documents>/Lyra/
    // cw_harvest, allocates the ring+harvester and starts a 1 Hz pump worker;
    // disabling stops the worker.  GUI thread.
    Q_INVOKABLE void setCwCaptureEnabled(bool on);
    Q_INVOKABLE int  cwCaptureCount() const {
        return cwHarvester_ ? cwHarvester_->segmentsWritten() : 0;
    }

    double cwBlankPenalty() const { return neuralCw_.blankPenalty(); }
    Q_INVOKABLE void setCwBlankPenalty(double p);
    // Task #53 — shared RX+TX filter low edge.  Affects only the
    // ASYMMETRIC SSB / DIG modes (USB/LSB/DIGU/DIGL).  CW filter
    // is centred on the pitch (low edge isn't a meaningful axis);
    // AM/DSB/FM are symmetric around DC.  Live-applied via
    // applyModeFilter() — no channel restart needed.
    int  filterLowHz() const { return static_cast<int>(filterLow_ + 0.5); }
    Q_INVOKABLE void setFilterLowHz(int hz);
    // #174 CTUNE step 1 (INERT — nothing calls this yet).  Apply the RXA
    // receiver-oscillator shift: demodulate `hz` away from the locked DDC
    // centre (the in-IQ-span offset that lets the DDC stay put while the VFO
    // moves).  hz == 0 turns the shift off; nonzero turns it on at that
    // offset.  No-op when the channel is closed.  The freq-path
    // decomposition wires this in step 2, where the sign convention vs the
    // HL2 mirrored baseband is bench-verified before any caller exists.
    void setRxShiftHz(double hz);
    int  markerOffsetHz() const;     // VFO − DDS (CW carrier convention)
    // Same VFO−DDS offset for an ARBITRARY mode — used when tuning to a CW
    // spot/VFO whose freq is the carrier (DDS = carrier − this).
    Q_INVOKABLE int cwMarkerOffsetForMode(const QString &mode) const;
    // Bandwidth (Hz) that results from dragging a passband edge to
    // `edgeOffsetHz` (offset from the tuned centre) in the current mode.
    // The panadapter edge-drag calls this, then writes Prefs.rxBandwidth.
    Q_INVOKABLE int bandwidthForEdge(double edgeOffsetHz) const;
    Q_INVOKABLE QStringList audioOutputDevices() const;
    Q_INVOKABLE void setAudioOutputDevice(int index);

    // P2 "radio speaker" audio return (ANAN G2 on-radio amp): when
    // set, dispatchAudioFrame hands each final stereo int16 block to
    // this sink ON THE RX FEEDER THREAD (= the P2 session thread
    // while a Saturn is the live radio — the sink may drive the
    // session socket directly).  The P2 bridge sets it around a
    // session whose profile audioRoute is "radio"; empty clears.
    void setRadioAudioSink(
        std::function<void(const qint16 *lr, int nframes)> sink);

    // Per-radio audio routing (Layer-2 radio profiles).  Applies an
    // output route for the SESSION only — unlike setAudioOutputDevice
    // it never writes the audio/output|deviceName settings, so the
    // persisted global config (the HL2's home setup — retention
    // rules, docs/P2_HARDWARE_PROFILES_PLAN.md) stays untouched.
    // hl2=false resolves <deviceName> when found, else the persisted
    // PC device, else the current/first output.
    void applyAudioRouteTransient(bool hl2, const QString &deviceName);
    // Re-apply the persisted global route (called when a P2 session
    // closes, returning audio exactly to the configured state).
    void restoreAudioRouteFromSettings();

    // ── #158 — VAC1 (virtual audio cable) operator controls (Settings →
    // Audio).  enable + output-device (by description) + RX gain (dB).
    // Persisted (QSettings vac1/*); applied live (rebuild on enable/device,
    // SetIVACrxscale on gain).  vac1OutputDevices() is the PC output list
    // for the picker (= IvacAudio::outputDevices()).
    bool    vac1Enabled() const           { return vac1Enabled_; }
    QString vac1OutputDeviceName() const  { return vac1OutName_; }
    double  vac1RxGainDb() const          { return vac1RxGainDb_; }
    // Reference "Auto Enable for Digital modes": when on, VAC1 follows the
    // operating mode (live in DIGU/DIGL, off otherwise) and the manual
    // Enable is the moot baseline; when off, the manual Enable applies.
    bool    vac1AutoDigital() const       { return vac1AutoDigital_; }
    Q_INVOKABLE QStringList vac1OutputDevices() const;
    // #158 DL-3 — Thetis-faithful "Driver" (host-API) + PortAudio device
    // pickers.  vac1HostApiNames() lists host APIs that have devices (parallel
    // PA indices via vac1HostApiPaIndices()); the *For(hostApi) lists drive the
    // Settings Driver→device repopulation; the no-arg vac1{Out,In}putDevices()
    // above enumerate the currently-selected host API.
    Q_INVOKABLE QStringList vac1HostApiNames() const;
    QList<int>              vac1HostApiPaIndices() const;
    Q_INVOKABLE QStringList vac1OutputDevicesFor(int paHostApi) const;
    Q_INVOKABLE QStringList vac1InputDevicesFor(int paHostApi) const;
    QString                 vac1HostApiName() const { return vac1HostApiName_; }
    Q_INVOKABLE void        setVac1HostApi(const QString &name);
    Q_INVOKABLE void setVac1Enabled(bool on);
    Q_INVOKABLE void setVac1OutputDeviceName(const QString &name);
    Q_INVOKABLE void setVac1RxGainDb(double db);
    Q_INVOKABLE void setVac1AutoDigital(bool on);
    // VAC-in (PC → TX): input device + TX gain (reference "Gain TX (dB)" →
    // vac_preamp).  The captured PC audio reaches TX only when the mic-source
    // selector picks "PC Soundcard (VAC1)" (use_vac_audio).
    QString vac1InputDeviceName() const   { return vac1InName_; }
    double  vac1TxGainDb() const          { return vac1TxGainDb_; }
    Q_INVOKABLE QStringList vac1InputDevices() const;
    Q_INVOKABLE void setVac1InputDeviceName(const QString &name);
    Q_INVOKABLE void setVac1TxGainDb(double db);
    // VAC latency posture (#158 follow-up) — operator-tunable to squeeze the
    // TX→RX turnaround for ARQ digital modes (VarAC).  vac1LatencyMs = rmatchV
    // ring depth (+ PA suggested latency); vac1VacSize = PortAudio block.  Both
    // are create-time / ring-rebuild params, so the setters reopen VAC when
    // live (like the device setters).  Carried per-profile (schema v5).
    int     vac1LatencyMs() const         { return vac1LatencyMs_; }
    int     vac1VacSize() const           { return vac1VacSize_; }
    Q_INVOKABLE void setVac1LatencyMs(int ms);
    Q_INVOKABLE void setVac1VacSize(int frames);
    // Live rmatchV ring diagnostics for the Settings VAC tab (reference VAC1
    // Monitor: overflows / underflows / ring fill %, per direction) — lets the
    // operator drop the latency until the counters just twitch, then back off.
    // Keys: {active, outUnder, outOver, outPct, inUnder, inOver, inPct}.
    // type 0 = rmatchOUT = TO VAC (RX→cable); type 1 = rmatchIN = FROM VAC
    // (cable→TX).  (The reference ivac.c comment reverses these; the code's
    // rmatchOUT/IN mapping is authoritative — Ivac.cpp:797-800.)
    Q_INVOKABLE QVariantMap vac1Diags();
    bool    vac1CombineInput() const      { return vac1CombineInput_; }
    Q_INVOKABLE void setVac1CombineInput(bool on);
    // #161 — "Mute will mute VAC".  Reference MuteWillMuteVAC1: when set, the
    // operator mute also zeroes the RX→VAC feed (the monitor volume rides it
    // unconditionally — see dispatchAudioFrame).  Default ON.
    bool    muteWillMuteVac() const       { return muteWillMuteVac_.load(std::memory_order_relaxed); }
    Q_INVOKABLE void setMuteWillMuteVac(bool on);
    // #158 DL-4 — RX→VAC muted during TX (reference SetIVACmox what-flag
    // gating).  Driven off the HL2Stream MOX edge (connected in main.cpp);
    // no-op when VAC1 isn't live.
    void setVacMox(bool on);

    // #59 RX EQ — point the post-RXA audio at the RX EqModel's engine +
    // analyzer (nullptr to detach).  dispatchAudioFrame applies it (mono-dup,
    // L==R) before ALL RX tees, gated on the engine's own !bypassed() AND not
    // a digital (DIGU/DIGL) mode.  The analyzer is fed pre/post for the panel.
    void setRxEqEngine(ParamEq *eng, EqAnalyzer *analyzer);

    // #89 Stage C2 — RX recorder tap.  Installed once (before the audio thread
    // starts) with the interleaved mono-dup RX audio (post-RX-DSP "what you
    // heard", 48 kHz) that dispatchAudioFrame produces, AFTER the RX-EQ block.
    // The ClipRecorder feed it points at is a lock-free no-op unless the
    // operator is recording an RX clip, so it costs nothing otherwise.
    void setRxRecordTap(std::function<void(const double *audio, int nframes)> tap);

    // Step 3d: feed interleaved baseband IQ — (I,Q,I,Q,…) doubles
    // already normalized to [-1,1) — from the RX worker thread.
    // Accumulates into in_size blocks; each full block runs
    // fexchange0 IN-LINE on the caller's thread (block=1 returns as
    // soon as the DSP thread has the output ready, ~187 calls/sec)
    // and updates audioDbFs.  MUST be called from a SINGLE thread
    // (the HL2Stream RX worker) — not thread-safe within the channel.
    // No audio is played yet (Step 3e); this only measures.
    void feedIq(const double *iq, int nframes);

    // TCI streaming taps (off unless a TCI client subscribes).  When on,
    // feedIq emits a copy of each post-DSP audio block (mono float32 @
    // outRate) / raw IQ block (interleaved I,Q float32 @ inRate) as a
    // queued signal so the (main-thread) TCI server can frame + send it.
    void setTciAudioStreaming(bool on) {
        tciAudioOn_.store(on, std::memory_order_relaxed);
    }
    void setTciIqStreaming(bool on) {
        tciIqOn_.store(on, std::memory_order_relaxed);
    }

    // Open RX1 (channel 0), apply the locked first-light config and
    // start the channel.  Idempotent; returns true on success.
    Q_INVOKABLE bool openRx1();

    // Stop (blocking flush) + close channel 0.  Idempotent.  Called
    // automatically on destruction.
    Q_INVOKABLE void closeRx1();

signals:
    void runningChanged();
    void levelsChanged();
    void volumeChanged();
    void mutedChanged();
    void txMutedChanged();
    void autoMuteOnTxChanged();
    void rxResumeDelayMsChanged();
    void afGainChanged();
    void balanceChanged();
    void monEnabledChanged();
    void monVolumeChanged();
    void audioDeviceChanged();
    void vac1Changed();   // #158 — VAC1 enable / device / RX gain
    void zoomChanged();
    void spanChanged();   // displayed span changed (rate OR zoom)
    void modeChanged();
    void bandwidthChanged();
    void passbandChanged();
    void cwPitchChanged();
    void markerOffsetChanged();
    void zeroBeatChanged();   // zero-beat offset / valid / active updated
    // #173 CW-5a — RX CW decoder outputs (emitted from the audio thread; the
    // CW-5b panel connects with the default queued connection).
    void cwDecodeEnabledChanged();
    void cwDecodedChar(QString ch, double confidence);  // decoded unit (conf always 1)
    void cwRxWpmChanged(int wpm);            // fldigi RX speed
    // DeepFist — engine selection changed; neural model availability resolved;
    // and the full current-window neural decode (replace-mode display).
    void cwDecodeEngineChanged();
    void cwNeuralAvailableChanged();
    void cwBlankPenaltyChanged();
    void cwNeuralText(QString windowText);
    // Auto engine — unified arbiter output; fallback == true when the Classic
    // safety net produced it (panel dims that run).
    void cwAutoText(QString text, bool fallback);
    // DeepFist CTC-lattice callsign verdict (confident only): best = the
    // lattice-preferred call, orig = the greedy decode (== best when confirmed).
    void cwNeuralCall(QString best, QString orig, double marginNats);
    // Freq calibration — one emit per analysis window while measuring.
    void freqCalUpdated(double measuredHz, double snrDb, int windows);
    void nrChanged();        // NR enable / mode / AEPF / NPE
    void agcModeChanged();
    void anfChanged();
    void lmsChanged();       // LMS enable / strength
    void notchesChanged();   // NF run / list add / remove / edit
    void squelchChanged();   // SQ enable / threshold
    void nbChanged();        // NB enable / strength
    void apfChanged();       // APF enable
    void binChanged();       // BIN enable / depth
    void noiseCaptureChanged();   // captured-profile capture state/progress
    void noiseApplyChanged();     // captured-profile apply toggle
    void noiseSettingsChanged();  // FFT size / capture duration
    void noiseProfilesChanged();  // saved-profile list / active profile
    void noiseTuningChanged();    // strength / floor / smoothing
    // TCI streaming: post-DSP mono audio (float32 @ rateHz) and raw IQ
    // (interleaved I,Q float32 @ rateHz).  Emitted from the RX worker
    // thread → delivered to the TCI server via a queued connection.
    void tciAudioBlock(const QByteArray &monoFloat, int rateHz);
    void tciIqBlock(const QByteArray &iqFloat, int rateHz);
    void logLine(QString line);

private:
    void emitLog(const QString &line);   // mirror logLine -> qInfo console
    bool startAudio();   // create + start the QAudioSink (Step 3e)
    void stopAudio();    // stop + tear down the QAudioSink

    // Stage B.6.a refactor (pure extraction, no behaviour change):
    // applies operator volume/mute/balance/BIN/HL2-atten to the supplied
    // interleaved L/R double buffer + quantizes into pcm16_ + dispatches
    // to the active sink (HL2 EP2 via hl2AudioPush_ OR PC sound card via
    // audioRing_).  Today called inline from feedIq with
    // (outBuf_.data(), outSize_); Stage B.6.b swaps to feeding AAMix's
    // out buffer instead.  EVERY state read + operation order is
    // byte-identical to the pre-refactor inline body
    // (wdsp_engine.cpp:2402-2456 pre-B.6.a) -- this is a pure relocation
    // so the helper can be called from either feedIq or an AAMix
    // Outbound callback unchanged.
    void dispatchAudioFrame(const double *audio, int nframes);
    // P0.c direct port: AAMix's Outbound is the reference raw
    // `void(*)(int,int,double*)` fn ptr, so the RX hand-off is a
    // static member (converts to that exact fn-ptr type; can reach
    // the private dispatchAudioFrame) routed through a TU-scope
    // self pointer in wdsp_engine.cpp — the same free-function +
    // global-context shape the reference's outbound consumers use.
    // Set before create_aamix in openRx1; cleared after
    // destroy_aamix in closeRx1.
    static void aamixOutbound(int id, int nsamples, double *buff);
    // #158 (#161 UAF fix) — VAC-in → TX bridge.  Like aamixOutbound, a
    // static member so the wire's plain void(*)(int,double*) fn ptr can
    // still reach the private vac1Active_/vacMtx_ via g_aamixOutboundSelf.
    // The cm_main TX pump calls it at the mic block rate when the mic
    // source is VAC1; the vacMtx_+vac1Active_ gate (mirroring
    // dispatchAudioFrame) keeps xvacIN off a freed / mid-rebuilt rmatchIN
    // during a VAC device change or enable/disable.
    static void vacInboundCb(int nsamples, double *buff);
    // #90 — READ-ONLY post-rack TX-monitor tap (registered via
    // SendpTxMonitorTap).  Like vacInboundCb, a static member so the
    // raw fn-ptr reaches monitorRing_ via g_aamixOutboundSelf.  Runs on
    // the cm_main TX pump thread (xcmaster) at the mic block rate; copies
    // the mono I-lane into monitorRing_ (Stage 2).  dispatchAudioFrame
    // drains the ring onto the jack when MOX + MON (Stage 3).
    static void txMonitorTapCb(int nsamples, double *buff);
    // Push the current mode_/bw_ to WDSP (SetRXAMode + RXASetPassband).
    // No-op when the channel isn't open (applied on the next openRx1).
    void applyModeFilter();
    // Push the current NR (EMNR) state to WDSP — run + gain method +
    // NPE method + AEPF + position.  No-op when the channel is closed
    // (re-applied on the next openRx1).  Channel-parameterized so RX2
    // can reuse it unchanged.
    void pushNrState();
    // Push the current AGC mode (SetRXAAGCMode).  No-op when closed.
    void pushAgcMode();
    void ensureCwScpLocal();    // lazy load of scp_local.txt (GUI thread)
    // Push ANF (auto-notch) + LMS (line enhancer) run/vals.  No-op when
    // closed; channel-parameterized for RX2 reuse.
    void pushAnfState();
    void pushLmsState();
    // Rebuild the WDSP NBP notch database from notches_ + set the run
    // flag.  No-op when closed; re-applied on openRx1 (the DB is recreated
    // empty on every channel open).  Channel-parameterized for RX2.
    void pushNotches();
    void persistNotches();   // serialize notches_ to QSettings
    // Route the squelch to SSQL / FMSQ / AMSQ per the current mode_ and
    // push run + threshold; disables the inactive modules.  No-op closed.
    void pushSquelchState();
    // Push NB threshold (from strength) + run.  No-op until the EXT
    // blanker is created in openRx1.
    void pushNbState();
    // Push the APF (CW peaking biquad) shape + run; engages only in CW.
    void pushApfState();
    // BIN: Lyra-native Hilbert pseudo-stereo.  buildBinaural() makes the
    // FIR (once); binauralStep() turns one mono sample into an L/R pair
    // with persistent FIR + delay state; resetBinaural() clears it.
    void buildBinaural();
    void binauralStep(double mono, double *l, double *r);
    void resetBinaural();
    // Passband edges (Hz offsets from centre) for mode_ + bw_ + pitch.
    void computePassband(double *lo, double *hi) const;
    void recomputePassband();   // store + emit passbandChanged

    // ── Task #44 Phase 2 — analyzer (re)config helpers ───────────────
    // Both factor the SetAnalyzer + detector/average mode setup that
    // openRx1() originally did inline.  Same body shape; the RX vs TX
    // variants differ in sample-rate, block size (bf_sz), overlap, and
    // max_w — sized for the source feeding Spectrum0 in each MOX state.
    // configureAnalyzerForTx() is sized for the WDSP TX sip1 ring:
    // 96 kHz dsp_rate, 256-sample per-call reads, overlap 2496,
    // max_w 13696.  Both helpers are pure WDSP reconfiguration — the
    // analyzer ID + lifecycle stay owned by openRx1/closeRx1.
    //
    // PRECONDITION (both): channelMtx_ held by caller.  Matches the
    // openRx1 caller-holds convention.  Both ALSO acquire
    // analyzerMtx_ internally to serialize SetAnalyzer vs Spectrum0
    // feeds (amendment A.5).  No-op if analyzerOpen_ is false.
    void configureAnalyzerForRx() noexcept;
    void configureAnalyzerForTx() noexcept;

    // P4.b TUN display-honesty crop helpers (shared by copySpectrum +
    // copyWaterfallSpectrum).  txAnalyzerOffBins() converts the live
    // NCO−dial offset (Hz) into analyzer bins across the TX span; 0 unless
    // TX owns the analyzer with a nonzero offset (= TUN active).
    // cropSpectrum() crops+resamples the full kAnPixels dB array to n
    // display points about a centre shifted left by offBins.
    double txAnalyzerOffBins() const noexcept;
    void   cropSpectrum(const float *full, float *dst, int n,
                        double offBins) const noexcept;

    WdspNative *wdsp_    = nullptr;
    RxConfig    cfg_;
    // Serialises the WDSP channel lifecycle (open/close on the main
    // thread, e.g. a sample-rate switch) against feedIq()'s fexchange0
    // on the RX worker thread — so a rate change can't tear the channel
    // down mid-process.
    std::mutex  channelMtx_;
    int         channel_ = 0;
    int         outSize_ = 0;
    bool        opened_  = false;
    bool        running_ = false;
    bool        analyzerOpen_ = false;   // Step 5 panadapter analyzer
    // ── Task #44 Phase 2 atomics ──────────────────────────────────
    // txOwnsAnalyzer_ : true while the MOX-edge swap has retuned
    //   kAnDisp for the TX sip1 feed (per setTxOwnsAnalyzer()).
    //   Read by RX worker feedIq() (to skip Spectrum0 during MOX)
    //   and TX worker block-pack site (to gate the feed call).
    //   memory_order_release on store + acquire on read so the
    //   SetAnalyzer reconfigure side-effects happen-before any
    //   observation of the new flag value.
    // txSpanHz_       : span reported by spanHz() when TX owns the
    //   analyzer (96 kHz dsp_rate of WDSP TX sip1).  Divided by
    //   zoom in the getter.
    // txAnalyzerBfSize_ : current bf_sz the analyzer is configured
    //   for (set by configureAnalyzerForRx/Tx); read by
    //   feedTxSpectrumFromSip1() to drop frames during a
    //   mid-reconfigure transient.
    // inRateAtomic_   : mirror of cfg_.inRate updated under
    //   channelMtx_ in setSampleRate.  spanHz() reads it
    //   atomically without taking the lock (was a latent race).
    std::atomic<bool> txOwnsAnalyzer_{false};
    std::atomic<int>  txSpanHz_{96000};
    // P4.b TUN display-honesty: NCO−dial offset (Hz) applied to the
    // TX-state spectrum crop (setTxAnalyzerOffsetHz).  0 = no shift.
    std::atomic<int>  txAnalyzerOffsetHz_{0};
    std::atomic<int>  txAnalyzerBfSize_{0};
    std::atomic<int>  inRateAtomic_{192000};
    // ── Task #44 Phase 2 — fine-grained analyzer mutex (amendment
    // A.5).  Separate from channelMtx_ (which RX worker feedIq()
    // holds whole-method scope — would wedge the TX worker for
    // tens of ms if reused here).  Scope: serializes SetAnalyzer
    // reconfigure (configureAnalyzerForRx/Tx) vs Spectrum0 feeds
    // (feedTxSpectrumFromSip1).  Held briefly: microseconds for
    // the Spectrum0 call; small ms for SetAnalyzer (only on MOX
    // edge).  Order: ALWAYS take channelMtx_ BEFORE analyzerMtx_
    // when both needed (none of the current call paths need
    // both, but document the order for future-proofing).
    mutable std::mutex analyzerMtx_;

    // Step 3d DSP buffers (all sized in the constructor).
    // accum_ : interleaved IQ doubles awaiting a full in_size block.
    // outBuf_: fexchange0 output (2 * outSize_ doubles, L/R).
    std::vector<double> accum_;
    std::vector<double> outBuf_;
    int                 fexErr_ = 0;
    std::atomic<double> audioDbFs_{-200.0};
    // #91 VOX anti-VOX — running RMS (linear [0,1)) of the RX audio the
    // operator HEARS (post-EQ mono).  Written by the audio thread in
    // dispatchAudioFrame; read via voxRxAudioRmsLin() by the Qt-main VOX
    // poll (HL2Stream, provider-pull).  Studio-monitor bleed into the mic
    // is suppressed by VOX using this level.
    std::atomic<double> voxRxRmsLin_{0.0};
    std::atomic<bool>   tciAudioOn_{false};   // TCI audio stream tap on
    std::atomic<bool>   tciIqOn_{false};      // TCI IQ stream tap on
    // 5 Hz UI poll — emits levelsChanged so the QML audioDbFs binding
    // re-reads the atomic.  Lives on the main thread (WdspEngine is a
    // main-thread object); started in openRx1, stopped in closeRx1.
    QTimer              levelsTimer_;

    // Step 3e: PC sound-card playback.  audioRing_ is the QIODevice the
    // sink pulls from (fed by feedIq on the RX worker thread);
    // audioSink_ is the QAudioSink driving the chosen output device.
    // audioMtx_ guards the audioRing_/audioSink_ pointers so the RX
    // worker's push() never races a main-thread device switch / teardown.
    std::mutex          audioMtx_;
    AudioRing          *audioRing_ = nullptr;
    QAudioSink         *audioSink_ = nullptr;
    std::vector<qint16> pcm16_;
    // P2 radio-speaker tee (setRadioAudioSink) — mutex-guarded; the
    // dispatch-path lock is uncontended except during set/clear.
    std::mutex          radioSinkMtx_;
    std::function<void(const qint16 *, int)> radioSink_;
    // P4.b — interleaved L/R doubles handed to the verbatim
    // OutBound(0) RX-audio tee (dispatchAudioFrame; the §2 asioOUT
    // pattern).  sendProtocol1Samples does the reference 16-bit
    // round-nearest quantize, so the wire path consumes doubles, not
    // pcm16_.  Sized 2*outSize_ alongside pcm16_.
    std::vector<double> lrWire_;
    // Slider defaults to 0.65 (≈ -14 dB taper, plus the HL2 path's extra
    // ~-10.5 dB) — a moderate, non-blast startup level.  muted_ is
    // restored from QSettings in the ctor (default UNMUTED).
    std::atomic<double> volume_{0.65};
    std::atomic<bool>   muted_{false};
    // Auto-mute-on-TX (task #26).  txMuted_ tracks the live wire MOX bit
    // (false at boot; toggled by HL2Stream::moxActiveChanged).  Not
    // persisted — pure transient.  autoMuteOnTx_ is the operator's
    // master switch (persisted under audio/autoMuteOnTx, default ON =
    // safe posture: a brand-new install can't self-deafen on first key).
    // Gain calc OR's (muted_) with (autoMuteOnTx_ AND txMuted_).
    std::atomic<bool>   txMuted_{false};
    std::atomic<bool>   autoMuteOnTx_{true};
    // RX-on-unkey delay (queued thud/echo fix).  On the keyup MOX-off edge
    // (setTxMuted(false)) the un-mute is deferred by rxResumeDelayMs_ via
    // rxResumeTimer_ (single-shot, main-thread like levelsTimer_) so the
    // RX DSP pipeline drains the TX-coupled tail as silence + the T/R
    // settles before audio resumes.  Thetis stops/restarts the RX channel
    // across TX; Lyra mutes-through, so it needs this flush window.
    // applyTxMuted_ is the actual store+emit; setTxMuted gates it.  A
    // keydown (setTxMuted(true)) cancels any pending resume and mutes now.
    std::atomic<int>    rxResumeDelayMs_{50};   // persisted audio/rxResumeDelayMs
    QTimer              rxResumeTimer_;
    void applyTxMuted_(bool m);
    // AF makeup gain (dB, main-thread; pushed to WDSP SetRXAPanelGain1 as
    // a linear gain).  Balance −1..+1 is applied Lyra-side in feedIq, so
    // it's an atomic the RX worker reads.
    double              afGainDb_ = 0.0;
    std::atomic<double> balance_{0.0};

    // #159 slim DSP — per-family [RX=0, TX=1] minimum-phase flag (false =
    // Linear Phase default).  Loaded from QSettings in the ctor; applied to
    // RXASetMP(channel_) / TXASetMP(chid 1) for the active mode's family.
    bool dspFiltMp_[4][2] = {{false, false}, {false, false},
                             {false, false}, {false, false}};
    std::atomic<bool> txaChannelOpen_{false};  // #159 — txa[1] live? (set by main)
    void              loadDspFilterTypes();          // QSettings -> dspFiltMp_
    void              applyDspFilterTypes();          // push for the active mode
    static DspFamily  dspFamilyForMode(const QString &mode);
    // #90 TX monitor (Stage 1: persisted + UI; consumed in Stage 3).
    std::atomic<bool>   monEnabled_{false};
    std::atomic<double> monVolume_{0.5};
    // Panadapter zoom (1.0 = full span).  Written from the UI/main
    // thread; read by spanHz() (UI) + copySpectrum() (crop).
    std::atomic<double> zoom_{1.0};
    // Demod mode + RX bandwidth (Hz) + CW pitch.  Main-thread only
    // (UI setters + openRx1).  Default USB 2.4 kHz; CW centres on pitch.
    QString mode_       = QStringLiteral("USB");
    int     bw_         = 2400;
    double  cwPitchHz_  = 600.0;
    // Task #53 — shared RX+TX filter low edge (operator-tunable).
    // Default 100 Hz; setFilterLowHz clamps to [0, 500].
    double  filterLow_  = 100.0;
    double  passbandLowHz_  = 200.0;    // edges for the panadapter overlay
    double  passbandHighHz_ = 2400.0;
    // RX DSP operator state (main-thread only; restored from QSettings
    // in the ctor, pushed to WDSP on open + on each setter).  NR off by
    // default; Mode 3 (MMSE-LSA, WDSP default) + AEPF on + NPE OSMS
    // match old Lyra's first-light defaults.  AGC med.
    bool    nrEnabled_   = false;
    int     nrMode_      = 3;            // 1..4 (UI) -> gain_method 0..3
    bool    aepfEnabled_ = true;
    int     npeMethod_   = 0;            // 0=OSMS 1=MCRA
    QString agcMode_     = QStringLiteral("med");
    bool    anfEnabled_  = false;
    bool    lmsEnabled_  = false;
    double  lmsStrength_ = 0.5;          // 0..1 (0.5 ≈ WDSP-class default)
    // Manual notch list (offsets in Hz from the tuned centre).  Lyra-side
    // is the source of truth; pushNotches() rebuilds the WDSP DB from it.
    struct Notch { double offsetHz; double widthHz; bool active; };
    std::vector<Notch> notches_;
    bool    notchEnabled_  = false;      // NF master run
    int     wdspNotchCount_ = 0;         // notches currently in the WDSP DB
    bool    squelchEnabled_   = false;
    double  squelchThreshold_ = 0.20;    // 0..1 operator knob
    bool    nbEnabled_   = false;
    double  nbStrength_  = 0.5;          // 0..1 (higher = more blanking)
    bool    nbCreated_   = false;        // EXT NOB created for this channel
    std::vector<double> nbBuf_;          // xnobEXT output (2*inSize doubles)
    bool    apfEnabled_  = false;        // CW peaking filter (CW-gated)
    double  apfGainDb_   = 12.0;         // operator-set peak gain (dB)
    // Captured noise profile (slice 2: capture).  Created lazily at the
    // operator's captured-profile FFT size (slice 4 makes npFftSize_
    // settable; default 4096).  noiseProfile_ is created/armed on the
    // main thread and fed on the RX worker — both under channelMtx_, so
    // a capture can't be armed mid-fexchange0.  The scalar
    // capturing/progress/valid mirrors are atomics the UI polls.
    std::unique_ptr<CapturedProfile> noiseProfile_;
    int                 npFftSize_ = 4096;          // 2048/4096/8192
    std::atomic<bool>   noiseCapturing_{false};
    std::atomic<double> noiseProgress_{0.0};
    std::atomic<bool>   noiseProfileValid_{false};
    bool                npLastCapturing_ = false;   // main-thread edge latch
    // Slice 3: apply.  reducer_ created on enable (and recreated on FFT-
    // size change); applyEnabled_ gates the feedIq route; cleanBuf_ holds
    // one cleaned block.  All touched under channelMtx_ except the atomic.
    std::unique_ptr<NoiseReducer> reducer_;
    std::atomic<bool>   applyEnabled_{false};
    std::vector<double> cleanBuf_;                  // 2*inSize cleaned IQ
    double              npCaptureSeconds_ = 5.0;    // 3/5/10
    double              npAlpha_     = 1.0;          // over-subtraction
    double              npFloorDb_   = -12.0;        // max attenuation
    double              npSmoothing_ = 0.6;          // mask smoothing
    QString             npActiveName_;              // loaded profile ("" = none)
    void applyReducerParams();   // push α/floor/smoothing to reducer_ (lock held)
    // Named-profile store (manual-curated).  One ".lnp" file per profile
    // in a findable folder (see noiseProfilesDir) — browsable / backup-
    // able / shareable.  Each is rate + FFT-size tagged.  Main-thread only.
    struct StoredProfile {
        QString             name;
        int                 rate = 0;
        int                 fft  = 0;
        QString             date;
        QString             file;    // absolute path of its .lnp
        std::vector<double> power;   // per-bin noise power (length fft)
    };
    std::vector<StoredProfile> profiles_;
    QString profilesDir() const;            // ensures the folder exists
    void    loadProfiles();                 // ctor: scan *.lnp
    bool    writeProfileFile(StoredProfile &p);   // write one (sets p.file)
    void    removeProfileFile(const StoredProfile &p);  // delete its file
    // BIN — 63-tap Hilbert FIR pseudo-stereo (mono in → L/R out).
    bool    binEnabled_  = false;
    double  binDepth_    = 0.7;          // 0 = mono, 1 = full Hilbert pair
    std::vector<double> binH_;           // Hilbert FIR taps (63)
    std::vector<double> binHist_;        // ring history (63) — FIR + delay tap
    int     binPos_      = 0;            // newest-sample index in binHist_
    // Last good full-resolution spectrum (kAnPixels dB points).  WDSP's
    // GetPixels resets its ready-flag on read, so with TWO consumers
    // (panadapter + waterfall) the second sees no data; caching here lets
    // both always read a consistent, valid frame (no uninitialised
    // garbage feeding the zoom crop).  GUI-thread only (the QQuickWidget
    // panadapter + waterfall reads are serialised).
    std::vector<float> specCache_;
    // §15.29 C1 — pixout=1 waterfall cache, mirrors specCache_ but
    // populated by copyWaterfallSpectrum's GetPixels(pixout=1) during
    // TX state.  Separate cache because pixout=0 and pixout=1 have
    // independent ready-flags in WDSP, and the same dual-consumer
    // GUI-thread serialisation contract applies (panadapter reads
    // specCache_ via copySpectrum, waterfall reads wfCache_ via
    // copyWaterfallSpectrum).  In RX state copyWaterfallSpectrum
    // falls through to specCache_'s data (single-pixout RX).
    std::vector<float> wfCache_;
    QList<QAudioDevice> devices_;       // operator's PC output devices
    int                 deviceIndex_ = 0;
    // Output routing: HL2 onboard codec (default — old Lyra's HL2 path)
    // vs a PC sound device.  When hl2Out_ the QAudioSink is not used;
    // RX audio reaches the jack via dispatchAudioFrame → OutBound(0).
    bool                hl2Out_ = true;

    // Stage B.6.b-retry (2026-06-08): ported AAMix instance for the RX
    // audio path.  THIS RETRY follows the bench-validated reference
    // initialization pattern verbatim (cmaster.c:297-313 + :411 +
    // :534-536): create with active=0 (NO mix_main yet), then
    // SetAAudioMixOutputPointer to re-set Outbound defensively,
    // then SetAAudioMixState(stream=0, active=1) to trigger the
    // close_mixer/open_mixer atom which is the reference's bench-
    // validated path to start mix_main.  The previous B.6.b
    // shortcut (active=0x01 at create-time) silently failed at
    // bench -- mode of failure unknown but the reference path
    // sidesteps whatever code path of the port my shortcut
    // exercised.
    //
    // Constructed in openRx1() after startAudio() / before
    // SetChannelState(channel,1); destroyed in closeRx1() after
    // SetChannelState(0,1) blocking-flush / before CloseChannel.
    // Null-check in feedIq covers the brief close-then-reopen race.
    // P0.c: `AAMIX` is the reference twin typedef (= aamix*) from
    // the verbatim wire/AAMix.h direct port.
    lyra::wire::AAMIX aaMix_ = nullptr;

    // ── #158 Stage 3 — VAC1 VAC-out (radio RX audio → a PC output
    // device, for WSJT-X / MSHV / etc.).  The IVAC engine instance
    // (wire/Ivac, id kVac1Id) + its Qt device layer (IvacAudio).  RX
    // audio is teed POST-RXA — the dispatchAudioFrame `audio` param,
    // BEFORE the operator's monitor volume/mute/balance — so a digital
    // app receives steady-level audio regardless of how the operator
    // rides their speaker volume (matches the reference's separate
    // VAC vs AF-gain paths).  Fed via xvacOUT(stream=1); IvacAudio
    // drains rmatchOUT to the sink.  vac1Active_ gates the hot-path
    // tee; vacMtx_ serialises the mix-thread xvacOUT against
    // main-thread (re)build/teardown so destroy_ivac can never free a
    // ring mid-xvacOUT.  Lifecycle hangs off openRx1/closeRx1, so a
    // sample-rate reopen rebuilds at the new audio_size/audio_rate.
    // Bench-enabled via LYRA_VAC1_OUT (Stage 6 adds the Settings UI);
    // VAC-in (mic → TX) is Stage 4.
    static constexpr int kVac1Id = 0;
    void rebuildVac1();      // reconcile: teardown then (re)start iff should-be-on
    void teardownVac1();     // StopAudioIVAC + destroy_ivac; idempotent
    void applyVacEnvOnce();  // read LYRA_VAC1_OUT / _VAC_SIZE once (bench hook)
    // Desired live state: auto-digital mode → on iff the current mode is
    // DIGU/DIGL; otherwise the operator's manual Enable.
    bool vac1ShouldBeOn() const;
    // #158 DL-2 — the device layer is the reference's single full-duplex
    // PortAudio stream owned by the wire/Ivac engine instance (StartAudioIVAC /
    // StopAudioIVAC on kVac1Id), NOT a Qt IvacAudio member.  IvacAudio's static
    // outputDevices()/inputDevices() still feed the Settings combos until DL-3.
    std::atomic<bool>     vac1Active_{false};
    std::mutex            vacMtx_;
    // #161 — operator "Mute will mute VAC" (reference MuteWillMuteVAC1).
    // Read on the audio thread in dispatchAudioFrame; default ON.
    std::atomic<bool>     muteWillMuteVac_{true};

    // #59 RX EQ — engine + analyzer (RX EqModel-owned; atomic for the audio
    // thread), the digital-mode auto-bypass flag (set in setMode), and the
    // mutable scratch the const RX audio is EQ'd into before the tees.
    std::atomic<lyra::dsp::ParamEq *>    rxEq_{nullptr};
    std::atomic<lyra::dsp::EqAnalyzer *> rxEqAnalyzer_{nullptr};
    std::atomic<bool>                    rxEqModeBypass_{false};
    std::function<void(const double *, int)> rxRecordTap_;   // #89 C2 RX recorder tap
    std::vector<double>                  rxEqBuf_;

    // #173 CW-5a — RX CW decoder.  Fed from the TOP of dispatchAudioFrame
    // (pre-RX-EQ) so a user's CW-mode EQ curve can't distort the decode.
    // cwModeActive_ (set in setMode) gates to CWU/CWL; cwDecodeOn_ is the
    // operator enable.  cwMonoBuf_ holds the de-interleaved mono block.
    lyra::dsp::CwDecoder                 cwDecoder_;

    // DeepFist neural CW decoder — second engine sharing the same tap.
    // cwEngine_: 0 = Classic (fldigi), 1 = Neural (DeepFist), 2 = Auto (arbiter).
    lyra::dsp::NeuralCwDecoder           neuralCw_;
    lyra::dsp::CwArbiter                 cwArbiter_;   // Auto: owns display handoff
    std::atomic<int>                     cwEngine_{0};
    lyra::dsp::ScpLocal                  cwScpLocal_;       // Phase 3 local calls
    bool                                 cwScpLocalLoaded_ = false;

    // Phase 2 harvest — allocated on first enable (opt-in, default off).
    std::unique_ptr<lyra::dsp::CwHarvestRing>       cwHarvestRing_;
    std::unique_ptr<lyra::dsp::DeepFistResampler>   cwHarvestDecim_;
    std::unique_ptr<lyra::dsp::CwCaptureHarvester>  cwHarvester_;
    std::vector<float>                              cwHarvestTmp_;
    std::atomic<bool>                               cwCaptureOn_{false};
    std::thread                                     cwHarvestWorker_;
    std::atomic<bool>                               cwHarvestRun_{false};
    // Env-gated at construction (no UI chips): Learn on unless LYRA_CW_LEARN=0;
    // Harvest off unless LYRA_CW_HARVEST is set truthy (developer capture).
    bool                                            cwLearnEnabled_{true};
    bool                                            cwHarvestEnabled_{false};

    // Zero-beat tuning aid.  zeroBeat_ is touched ONLY on the RX worker
    // (feedIq); zbRunPrev_/zbRate_ are worker-only edge trackers.  The result
    // atomics (zbRawHz_ = carrier offset from DDS, zbValid_) cross to the UI,
    // read by the zeroBeat* getters.  enabled_/modeOk_ are set on the UI thread.
    lyra::dsp::ZeroBeat  zeroBeat_;
    bool                 zbRunPrev_ = false;   // worker-only run edge
    int                  zbRate_    = 0;        // worker-only last IQ rate
    std::atomic<bool>    zeroBeatEnabled_{false};
    std::atomic<bool>    zeroBeatModeOk_{false};
    std::atomic<double>  zbRawHz_{0.0};       // measured carrier baseband offset
    std::atomic<double>  zbMarkerHz_{0.0};    // marker offset (VFO−DDS), UI→worker
    std::atomic<bool>    zbValid_{false};

    // Freq calibration (Stage 3b) — carrier-tone estimator + arm flag +
    // de-interleave scratch + last-emitted window counter (throttle).
    lyra::dsp::FreqCalMeasure            freqCal_;
    std::atomic<bool>                    freqCalOn_{false};
    std::vector<float>                   fcalMono_;
    int                                  fcalLastWin_ = -1;
    std::atomic<bool>                    cwDecodeOn_{false};
    std::atomic<bool>                    cwModeActive_{false};
    std::vector<float>                   cwMonoBuf_;
    bool                  vac1Enabled_   = false;  // operator opt-in (Settings / env)
    bool                  vac1AutoDigital_ = false; // auto-enable for DIGU/DIGL
    QString               vac1OutName_;            // PC output device description ("" = none)
    QString               vac1InName_;             // PC input device description ("" = none)
    QString               vac1HostApiName_;        // #158 DL-3 chosen PA host API ("" = first WASAPI)
    bool                  vacMox_ = false;         // #158 DL-4 last MOX state (re-applied on rebuild)
    double                vac1TxGainDb_  = 3.0;    // VAC TX gain (reference default +3 dB) → vac_preamp
    bool                  vac1CombineInput_ = true; // mono-combine VAC-in I=Q=(L+R) (reference vac_combine_input)
    int                   vac1VacSize_   = 2048;   // VAC buffer (reference default 2048)
    int                   vac1LatencyMs_ = 120;    // rmatchV ring depth (reference VAC default)
    // VAC RX gain (reference "Gain RX (dB)" → vac_rx_scale → SetIVACrxscale
    // → the mixer's input-0 gain).  Reference default 0 dB (unity).
    // Operator-adjustable (env LYRA_VAC1_RX_GAIN_DB now; a dB spinner in
    // Settings at Stage 6, mirroring the Thetis VAC dialog).  Stored as the
    // linear scale SetIVACrxscale wants (0 dB = 1.0).  NOTE: the Stage-3 tap
    // is post-RXA so it also tracks AF Gain — keep AF Gain nominal, then
    // trim here, until a pre-AF tap lands.
    double                vac1RxGainDb_  = 0.0;     // reference default 0 dB
    bool                  vacEnvApplied_ = false;
    // The IVAC mixer is a 2-input AAMix (RX audio + TX monitor) created
    // active=3, so its mix_main WaitForMultipleObjects(…, TRUE) blocks
    // until BOTH inputs signal Ready each block.  Stage 3 has no TX-monitor
    // tap yet, so we feed input 1 (xvacOUT stream 2) a silence block every
    // RX frame to keep the mixer synced — exactly as the reference feeds a
    // silent TX monitor during RX.  Sized 2*outSize_ in rebuildVac1.
    std::vector<double>   vacMonSilence_;
    // #90 TX monitor — post-rack mic captured on the cm_main TX thread
    // (txMonitorTapCb), drained on the audio thread (dispatchAudioFrame,
    // Stage 3).  Lock-free SPSC; ~340 ms @ 48 k mono.
    lyra::dsp::MonitorRing monitorRing_;
    std::vector<double>    monScratch_;   // #90 audio-thread drain scratch (mono)
    std::vector<double>    vacMonStereo_; // #90 Route 2 — VAC stream-2 stereo feed
    // #161 — RX audio scaled by the monitor volume(+mute) for the VAC tee
    // (reference RXOutputGain is pre-tap).  Kept separate so the sink loop
    // still receives the raw post-RXA `audio`.  Sized 2*outSize_ on first use.
    std::vector<double>   vacRxScaled_;
};

} // namespace lyra::dsp

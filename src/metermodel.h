// Lyra — RX signal-strength meter model.
//
// Feeds the meter panels (Horizon Arc / Plasma Bar).  Reads the RX
// S-meter level from the WDSP RXA_S_PK accessor (`WdspEngine::sMeterDbm`
// — the reference-faithful path, matches the reference's `GetRXAMeter`
// poll posture exactly).  Applies an operator calibration trim to
// land it on the S-unit scale, and derives everything the QML
// renderers need:
//   • level  — current value, 0..1 along the S1…S9+60 scale (smoothed)
//   • peak   — peak-hold marker (holds ~0.8 s then decays)
//   • glow   — phosphor afterglow (trails the DROP, fast decay)
//   • text   — "S9+20" style readout
//   • dbmText— calibrated dBm readout
//   • history— recent level ring for the arc's trailing sparkline
//
// Today the only source is the RX S-meter.  PWR/SWR/ALC/MIC/PA/Temp
// slot in as sibling sources (with click-to-cycle) when TX + HL2
// telemetry land — so there are no inert placeholders now.

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <deque>

namespace lyra::ipc { class HL2Stream; }
namespace lyra::dsp { class WdspEngine; }  // TxDspWorker ripped (Q2)
namespace lyra::wire { class P2RxBridge; }

namespace lyra::ui {

class MeterModel : public QObject {
    Q_OBJECT
public:
    // Meter source — what physical quantity the model reads + renders.
    // RX_SMETER is the original / default (WDSP RXA_S_PK in-passband
    // dBm).  PWR/SWR feed from HL2Stream EP6 telemetry (fwd_power /
    // rev_power ADCs).  PA_CURRENT/PA_VOLTS/TEMP are HL2 board
    // telemetry — useful in BOTH MOX states.  ALC/MIC/COMP are TX-side
    // DSP signals that arrive when the WDSP TXA chain lands.  The
    // QML renderers (HorizonArc / PlasmaBar) consume the same Meter
    // context property regardless of source — only the per-tick value
    // computation, scale ticks, text formatting, and zone-threshold
    // semantics change per source.
    // TX dynamics-meter set (Task #71 §2, 2026-06-02; CFC/COMP
    // pruned 2026-06-02 PM per operator §15.19 reminder).
    //
    // Lyra's TXA chain runs leveler + ALC; the multiband-shaping
    // role the reference's CFC block fills is taken by the Lyra-
    // native Combinator (Task #51, v0.2.1) which sits as a pre-
    // processor BEFORE the WDSP TXA chain — NOT a WDSP block,
    // hence not addressed by any TXA_* meter index.  Same for
    // the single-band downstream compressor: Lyra doesn't enable
    // WDSP's compress.c block (#51's design feeds Combinator
    // output straight to the WDSP ALC).  So the reference's
    // CFC_PK / CFC_GAIN / COMP_PK meters would read off-state
    // FOREVER in Lyra — those would be inert-UI entries
    // (operator §15.19 / Task #51 directive: no-inert-UI rule
    // applies, and a "Combinator placeholder" must wait for
    // Combinator-native meter sources that don't exist yet).
    //
    // The Lyra-native Combinator's per-band + total meters will
    // attach to NEW Source enum entries (COMBINATOR_PK / _G or
    // similar) when #51 ships in v0.2.1.  Until then, the set
    // exposes exactly what's actively running.
    //
    // Values 10 / 11 / 12 are LEFT UNUSED to reserve room for the
    // future Combinator meter sources (no renumber required).
    //
    // ⚠ Value 6 (was Source::ALC) and value 8 (was Source::COMP)
    // are RENAMED — same numeric values for QSettings stability,
    // honest labels for what they actually read.  Old refs at
    // the enum-identifier level (case ALC: / case COMP:) MUST
    // be updated to the new names.
    enum Source {
        RX_SMETER    = 0,    // WDSP RXA_S_PK — the existing RX behavior
        PWR          = 1,    // forward power (W) — fwd_power ADC + cal
        SWR          = 2,    // (1+rho)/(1-rho) where rho = sqrt(rev/fwd)
        PA_CURRENT   = 3,    // HL2 PA bias current (A)
        PA_VOLTS     = 4,    // HL2 PA supply volts
        TEMP         = 5,    // HL2 board temperature (°C)
        ALC_G        = 6,    // WDSP TXA_ALC_GAIN  (ALC gain reduction, dB)
        MIC          = 7,    // WDSP TXA_MIC_PK    (mic peak, dBFS)
        LVL_G        = 8,    // WDSP TXA_LVLR_GAIN (leveler boost, dB)
        LVL_PK       = 9,    // WDSP TXA_LVLR_PK   (leveler output, dBFS)
        // 10, 11, 12 reserved for the future Lyra-native
        // Combinator meter sources (#51, v0.2.1).  No CFC/COMP
        // WDSP meters in Lyra — Combinator replaces both roles.
        ALC_PK       = 13,   // WDSP TXA_ALC_PK    (ALC output, dBFS)
        ALC_GROUP    = 14,   // ALC_PK + ALC_GAIN composite
    };
    Q_ENUM(Source)

    // PWR meter ballistic — three named modes the operator picks
    // from Settings → Meter.  All three share the same underlying
    // per-band-calibrated fwd_power → watts conversion; they differ in
    // the post-conversion smoothing / peak-detection wired into level_:
    //
    //   PEP   — sliding-window MAX, FIXED 500 ms hold (10 samples ×
    //           50 ms tick).  Tight, snappy ballistic — each voice
    //           burst kicks the bar up and it drops back between
    //           syllables.  Best for contest-style peak watching
    //           where every burst should be readable; ignores the
    //           PWR Peak Hold spin box.
    //   PEAK  — sliding-window MAX, hold = pwrPeakHoldMs_ (operator-
    //           tunable in Settings, default 3000 ms = 60 samples).
    //           Compensates for the digital bar's lack of inherent
    //           damping — peaks park long enough to read at leisure.
    //           General-purpose default.  This is the existing
    //           shipped behaviour; default for backward compatibility.
    //   AVG   — IIR smoother (single time constant ~200 ms τ).  Calm,
    //           slow-moving needle — does NOT track speech peaks,
    //           tracks the running average of forward power.  Best
    //           for sustained-tone gain-structure work and ragchew
    //           where a steady reading beats peak-chasing.
    //
    // All three use the same peak_ / maxPeak_ secondary indicators
    // on the renderer (the outside-the-needle peak pip + max-hold
    // marker), driven off level_ — so even AVG mode still shows the
    // recent fast peaks via those auxiliary markers.  The MODE only
    // controls the inner needle / bar fill ballistic.
    enum PwrBallistic {
        PWR_PEP  = 0,        // fast: 500 ms window
        PWR_PEAK = 1,        // slow: operator-tunable window (default)
        PWR_AVG  = 2,        // IIR smoothed
    };
    Q_ENUM(PwrBallistic)

private:
    // One NOTIFY for the fast-changing values — QML repaints on `updated`.
    Q_PROPERTY(double level    READ level    NOTIFY updated)
    Q_PROPERTY(double peak     READ peak     NOTIFY updated)
    // Max-hold "high-water mark" — a second marker that latches the
    // highest level seen, holds for maxHoldMs, then eases down gently
    // (slower than the fast peak pip).  Distinct marker in the renderers.
    Q_PROPERTY(double maxPeak  READ maxPeak  NOTIFY updated)
    Q_PROPERTY(bool maxPeakEnabled READ maxPeakEnabled WRITE setMaxPeakEnabled
               NOTIFY maxPeakCfgChanged)
    Q_PROPERTY(int  maxHoldMs READ maxHoldMs WRITE setMaxHoldMs
               NOTIFY maxPeakCfgChanged)
    Q_PROPERTY(double glow     READ glow     NOTIFY updated)
    Q_PROPERTY(QString text    READ text     NOTIFY updated)
    Q_PROPERTY(QString dbmText READ dbmText  NOTIFY updated)
    Q_PROPERTY(QVariantList history READ history NOTIFY updated)
    // Noise-floor marker (0..1 on the scale) + signal-above-noise readout.
    // The floor is a slow rolling-minimum of the level; SNR = signal-floor.
    Q_PROPERTY(double noiseLevel READ noiseLevel NOTIFY updated)
    Q_PROPERTY(QString snrText   READ snrText    NOTIFY updated)
    // The normalized position of the S9 boundary — renderers paint the
    // over-S9 region red.  Tracks the HF/VHF scale (S9 shifts at 30 MHz).
    Q_PROPERTY(double normAtS9 READ normAtS9 NOTIFY updated)
    // Operator-selectable visual style: 0 = Horizon Arc (default),
    // 1 = Plasma Bar, 2 = Vertical Ladder.  `style` is the RX (default)
    // style; `txStyle` is used on TX when `separateStyle` is on.  The
    // renderer + header toggle read `activeStyle`, which swaps on the MOX
    // edge (mirrors the panadapter dB-range swap).
    Q_PROPERTY(int style READ style WRITE setStyle NOTIFY styleChanged)
    Q_PROPERTY(int txStyle READ txStyle WRITE setTxStyle NOTIFY txStyleChanged)
    Q_PROPERTY(bool separateStyle READ separateStyle WRITE setSeparateStyle
               NOTIFY separateStyleChanged)
    Q_PROPERTY(int activeStyle READ activeStyle NOTIFY activeStyleChanged)
    // dBFS→dBm calibration trim (operator-tunable; persisted).
    Q_PROPERTY(double calDb READ calDb WRITE setCalDb NOTIFY calChanged)
    // Peak-hold dwell time in ms before the peak marker starts to decay
    // (operator-tunable in Settings → Meter; persisted).
    Q_PROPERTY(int peakHoldMs READ peakHoldMs WRITE setPeakHoldMs
               NOTIFY peakHoldChanged)
    // PWR meter MAX-window hold time (operator bench 2026-05-31 PM:
    // "still seems SLOW to react" after the initial 500 ms -> 3 s
    // bump — operator wants live tunability so they can dial in their
    // preferred ballistic).  Distinct from peakHoldMs above: that one
    // controls the small peak-cap indicator's hang/decay; THIS one
    // controls the MAIN needle / fill-bar hold via the sliding-window
    // MAX detector's window length.  Live-apply (no restart).
    // Persisted.  Range 100-10000 ms.  Default 3000 ms.
    //
    // Honest scope note: the HARDWARE attack characteristic of the HL2
    // forward-power sensing chain (directional coupler analog
    // integrator + gateware ADC sample rate) is what it is — no
    // software fix can shorten the rise time from key-down to peak.
    // Operator-observed "slow to react" is partly the HW ramp curve
    // (the needle visibly sweeps UP through ADC samples as they
    // climb toward the peak); the MAX detector itself is instant-
    // attack at the software layer.  This knob only changes how LONG
    // the captured peak holds before decaying — not how fast it
    // climbs to peak in the first place.
    Q_PROPERTY(int pwrPeakHoldMs READ pwrPeakHoldMs WRITE setPwrPeakHoldMs
               NOTIFY pwrPeakHoldMsChanged)
    // PWR meter ballistic mode (PEP / PEAK / AVG).  See PwrBallistic
    // enum decl for the per-mode semantics.  Default PWR_PEAK = current
    // shipped behavior (no regression).  Live-apply: a mode change
    // takes effect on the next computePwr() tick (≤50 ms).  Persisted.
    Q_PROPERTY(int pwrBallistic READ pwrBallistic WRITE setPwrBallistic
               NOTIFY pwrBallisticChanged)
    // PWR calibration knob (task #34).  pwrRatedMaxW sets where the
    // red zone starts on the meter face (== operator's expected max
    // forward power: ~5 W for a bare HL2+ on-board PA, ~100 W or more
    // with an external amp).  Matching the displayed watts to an
    // external watt-meter is done per-band in HL2Stream (the PA Gain
    // tab's "PWR meter calibration"), not by a global scale here.
    Q_PROPERTY(double pwrRatedMaxW READ pwrRatedMaxW WRITE setPwrRatedMaxW
               NOTIFY pwrCalChanged)
    // TX secondary digital readout (task #36).  When a TX source (PWR
    // / SWR / ALC / etc.) is the primary, the operator can pick a
    // SECOND source to render as a small digital readout under the
    // main needle (reusing the existing snrText slot the renderers
    // already display — RX uses it for SNR, TX uses it for the chosen
    // secondary).  -1 = none / hide; otherwise a Source enum value.
    // Hidden automatically when the chosen secondary equals the
    // current primary (showing "PWR 4.2 W" under a PWR meter would
    // be a tautology — Settings prevents the selection but the model
    // gates defensively).
    Q_PROPERTY(int txSecondary READ txSecondary WRITE setTxSecondary
               NOTIFY txSecondaryChanged)
    // Second TX digital readout (task #37) — brings TX into RX-parity 3-line
    // layout (primary big number / secondary #1 / secondary #2 == RX's
    // primary / dBm / SNR).  Renders via the new secondary2Text property
    // that HorizonArc / PlasmaBar both display under the existing snrText
    // line.  -1 = none / hide; otherwise a Source enum value.  Hidden when
    // it equals the current primary OR equals the first secondary (no
    // duplicate lines).
    Q_PROPERTY(int txSecondary2 READ txSecondary2 WRITE setTxSecondary2
               NOTIFY txSecondary2Changed)
    Q_PROPERTY(QString secondary2Text READ secondary2Text NOTIFY updated)
    // Ladder rows (task #38 — Multi-source view).  Populated each tick
    // with per-source state for the Vertical Ladder renderer.  Each
    // entry is a QVariantMap with keys:
    //   label    : "PWR" / "SWR" / "PA" / ...
    //   value    : "4.2 W" / "1.4:1" / "1.76 A" / ...
    //   level    : 0..1 normalized fill (palette gradient keyed off
    //              this source's own danger point)
    //   danger   : 0..1 position of the red-zone threshold for this row
    // Computed in MOX-aware fashion: TX = primary TX sources stacked;
    // RX = degraded 3-row view (S-meter / noise floor / SNR).  Only
    // populated when the active style is Ladder — saves work otherwise.
    Q_PROPERTY(QVariantList ladderRows READ ladderRows NOTIFY updated)
    // Active source — what the renderer is showing RIGHT NOW.  Derived
    // from the operator's RX/TX preferences and the live wire MOX bit:
    //   * moxActive=false → source = rxSource
    //   * moxActive=true  → source = txSource
    // The setter (used by click-to-cycle, task #35) updates whichever
    // preference applies to the current MOX state — so the operator's
    // choice persists into the right slot.  Auto-swap on MOX edge is
    // wired in the ctor via HL2Stream::moxActiveChanged.
    Q_PROPERTY(int source READ source WRITE setSource NOTIFY sourceChanged)
    // Operator's RX-state and TX-state source preferences — what to
    // show at rest (rxSource, default RX_SMETER) and what to swap to
    // when the wire MOX bit settles (txSource, default PWR).  Each is
    // persisted independently; Settings → Meter exposes both via
    // dropdowns.  Changing either preference takes effect immediately
    // if the current MOX state matches that preference's slot.
    Q_PROPERTY(int rxSource READ rxSource WRITE setRxSource
               NOTIFY rxSourceChanged)
    Q_PROPERTY(int txSource READ txSource WRITE setTxSource
               NOTIFY txSourceChanged)

public:
    explicit MeterModel(lyra::ipc::HL2Stream *stream,
                        lyra::dsp::WdspEngine *wdsp,
                        QObject *parent = nullptr);

    // P2 (Saturn / ANAN) telemetry source — when the P2 bridge is
    // running, PA volts/current read from it (converted with the
    // radio's hardware-profile constants) instead of the idle
    // HL2Stream's NaN.  Same meters, either wire path.
    void setP2Bridge(lyra::wire::P2RxBridge *b) { p2_ = b; }

    // TX-rip Phase 1 (Q2): setTxDspWorker removed — TX DSP worker is
    // being rebuilt from empty files per the signed Phase 0 mapping
    // (docs/TX_ARCHITECTURAL_MAPPING.md §10.3).  TX meter taps go
    // silent until the new TxDspWorker lands.

    double  level()    const { return level_; }
    double  peak()     const { return peak_; }
    double  maxPeak()  const { return maxPeak_; }
    bool    maxPeakEnabled() const { return maxPeakEnabled_; }
    int     maxHoldMs() const { return maxHoldMs_; }
    void    setMaxPeakEnabled(bool on);
    void    setMaxHoldMs(int ms);
    double  glow()     const { return glow_; }
    double  noiseLevel() const { return noiseLevel_; }
    QString snrText()  const { return snrText_; }
    QString text()     const { return text_; }
    QString dbmText()  const { return dbmText_; }
    QVariantList history() const { return history_; }
    double  normAtS9() const;
    // Calibrated instantaneous RX S-meter dBm — the SAME calibration the
    // on-screen meter is built from (WDSP RXA_S_PK + operator calDb trim −
    // current LNA gain).  Exposed so the TCI server broadcasts a real,
    // calibrated dBm over rx_channel_sensors instead of a post-AGC audio
    // level.  Source-independent: always returns the RX S-meter reading
    // regardless of which Source the face is currently displaying (the TCI
    // rx_channel_sensors field is defined as the receiver's signal
    // strength).  Sentinel-safe: returns an S0-region floor when the stream
    // is not running (RXA_S_PK ≈ −200).
    double  rxSMeterDbm() const;

    // Numeric in-passband SNR (dB) — the SAME value the on-screen SNR readout
    // shows (dispDbm_ − noiseFloorDbm_, floored at 0). Shared over TCI as
    // `lyra_snr` so a linked SDRLogger+ (Combo) can GATE its auto RST-received
    // suggestion: only trust the S-unit when the signal is clearly above the
    // noise floor. Calibration-independent — a difference of two same-domain
    // dBm values, so the operator's calDb trim cancels out.
    double  rxSnrDb() const {
        return dispDbm_ > noiseFloorDbm_ ? dispDbm_ - noiseFloorDbm_ : 0.0;
    }

    int  style() const { return style_; }
    void setStyle(int s);
    int  txStyle() const { return txStyle_; }
    void setTxStyle(int s);
    bool separateStyle() const { return separateStyle_; }
    void setSeparateStyle(bool on);
    // The style the renderer + header toggle actually use: txStyle on TX
    // (when separateStyle is on), else style.  Swaps on the MOX edge.
    int  activeStyle() const;
    // Header Arc/Bar/Ladder buttons: set the style of whatever state is
    // showing NOW (TX style if separate+keyed, else the RX/default style).
    Q_INVOKABLE void setActiveStyle(int s);
    double calDb() const { return calDb_; }
    void setCalDb(double d);
    int  peakHoldMs() const { return peakHoldMs_; }
    void setPeakHoldMs(int ms);
    int  pwrPeakHoldMs() const { return pwrPeakHoldMs_; }
    void setPwrPeakHoldMs(int ms);
    int  pwrBallistic() const { return int(pwrBallistic_); }
    void setPwrBallistic(int m);
    int  source() const { return int(source_); }
    void setSource(int s);
    int  rxSource() const { return int(rxSource_); }
    int  txSource() const { return int(txSource_); }
    void setRxSource(int s);
    void setTxSource(int s);
    double pwrRatedMaxW() const { return pwrRatedMaxW_; }
    void setPwrRatedMaxW(double w);
    int  txSecondary() const { return txSecondary_; }
    void setTxSecondary(int s);
    int  txSecondary2() const { return txSecondary2_; }
    void setTxSecondary2(int s);
    QString secondary2Text() const { return secondary2Text_; }
    QVariantList ladderRows() const { return ladderRows_; }

    // Tick marks for the scale: list of { pos: 0..1, label: "9"/"+20", major: bool }.
    Q_INVOKABLE QVariantList tickMarks() const;

signals:
    void updated();
    void styleChanged();
    void txStyleChanged();
    void separateStyleChanged();
    void activeStyleChanged();
    void calChanged();
    void peakHoldChanged();
    void pwrPeakHoldMsChanged();
    void pwrBallisticChanged();
    void maxPeakCfgChanged();
    void sourceChanged();
    void rxSourceChanged();
    void txSourceChanged();
    void pwrCalChanged();
    void txSecondaryChanged();
    void txSecondary2Changed();

private:
    void tick();
    // Swap the displayed source to the RX or TX preference based on the
    // current keyed state (wire MOX OR CW message-level keying), resetting
    // the per-source peak/hold/history so a stale RX peak doesn't render as
    // a bogus TX peak across the edge.  Wired to BOTH
    // HL2Stream::moxActiveChanged and ::cwKeyingActiveChanged.
    void applyKeyStateSource();
    // Per-source tick computations.  Each fills the model's Q_PROPERTY
    // fields (level_/peak_/maxPeak_/glow_/text_/dbmText_/snrText_/
    // history_/noiseLevel_/floorDbm_/ceilDbm_/s9Dbm_) appropriately for
    // that source's scale + semantics.  computeSMeter is the existing
    // RX behavior (extracted unchanged from the old tick() body); other
    // computes land in subsequent commits.
    void computeSMeter();
    void computePwr();
    void computeSwr();
    // Task #35 fillers — HL2 telemetry trio.  Pure read of the
    // already-decoded EP6 status slots (paCurrentA / hl2SupplyV /
    // hl2TempC); no new wire surface, no new DSP.  ID = PA bias
    // current; VDD = PA supply volts; TEMP = HL2 board temperature.
    // Each is meaningful regardless of MOX state (the HL2 emits the
    // ADCs whether keyed or not), so operators can pick any of them
    // for the RX-side default if they want — e.g. PA Current at rest
    // surfaces the idle bias draw without keying.
    void computePaCurrent();
    void computePaVolts();
    void computeTemp();
    // Task #69 + #71 — TX-side TXA meter computes (CFC/COMP pruned
    // 2026-06-02 PM — Combinator replaces both per #51 design).
    //
    // The three LEVEL meters (MIC, LVL_PK, ALC_PK) share
    // computeLevelMeterFromDb(): take a WDSP-returned dBFS reading,
    // normalize to bar 0..1 over [-60 dB, 0 dB], apply reference-
    // faithful exponential peak decay (DecayRatio 0.20 per 50 ms
    // tick), no UI-side IIR (WDSP's internal 100 ms τ_av is
    // sufficient).
    //
    // The two GAIN meters (LVL_G, ALC_G) share
    // computeGainMeterFromDb(): take a dB reading (LVL_G typically
    // positive for boost, ALC_G typically negative for reduction),
    // normalize to bar 0..1 over the action magnitude [0..20 dB],
    // same decay model.
    //
    // ALC_GROUP is a composite (ALC_PK + ALC_GAIN) — the level
    // meter post-stage WITH the gain action added back, showing
    // what the wire would have seen if the ALC weren't acting.
    void computeMic();
    void computeLvlPk();
    void computeLvlG();         // formerly computeComp() — value 8
    void computeAlcPk();
    void computeAlcG();         // formerly computeAlc() — value 6
    void computeAlcGroup();
    // Shared helpers — single source of truth for level + gain
    // meter ballistic + rendering.  rawDb is the already-dB
    // reading from WDSP (NaN if no MOX / no TX worker).
    void computeLevelMeterFromDb(double rawDb,
                                  double scaleMin,
                                  double dangerDb,
                                  const QString &unitSuffix);
    void computeGainMeterFromDb(double rawDb,
                                 double scaleMax,
                                 double dangerDb);
    // Format a small "PWR 4.2 W" / "SWR 1.3:1" / "PA 1.8 A" / etc.
    // text snapshot for the given source, suitable for the secondary
    // digital readout under a TX primary.  Reads raw values from
    // stream_ without mutating any model state — safe to call from
    // any compute fn after the primary has finished its tick.
    QString formatSecondaryText(int src) const;
    // #160: live TX-dynamics secondary readouts (replaced the stale
    // TX-rip "—" placeholders).  Read WDSP TXA GetTXAMeter directly,
    // gated on transmitting → "—" at RX rest / floor.  Mirror the
    // primary compute fns (computeLevelMeterFromDb / computeAlcG).
    QString formatTxLevelSecondary(const QString &label, int meterIdx,
                                   double floorDb) const;
    QString formatTxAlcGSecondary() const;
    // Build the per-row Ladder data based on the current MOX state.
    // Called from tick() when the active style is Ladder.  Reads raw
    // values via stream_ getters and the formatSecondaryText helper —
    // no per-row state retained (peak-hold etc. is a future polish).
    void   buildLadderRows();
    // Compute normalized level + danger threshold for a given Source
    // value.  Used by buildLadderRows().  Returns {level, danger}
    // pairs in 0..1 range so the QML renderer can paint the bar +
    // zone coloring uniformly across all rows.
    void   ladderRowFor(int src, double *level, double *danger) const;
    double normForDbm(double dbm) const;
    // SINGLE source of truth for the RX S-meter calibration:
    // raw RXA_S_PK dBm + operator calDb trim − current LNA gain.  Called by
    // BOTH computeSMeter() (the on-screen meter) and rxSMeterDbm() (the TCI
    // export) so the face and the wire can never disagree.
    double calibratedSMeterDbm(double raw) const;
    void   updateScale();              // pick HF/VHF endpoints from the VFO freq
    QString sLabel(double dbm) const;  // standard HF dBm→S-unit table
    // PWR cal: per-band watt calibration lives in HL2Stream
    // (fwdPowerCalW() = raw ADC->W formula x the per-band trim the
    // operator sets on the PA Gain tab).  The meter displays that
    // calibrated value directly, so the meter, the watts cap and the
    // operator's external watt-meter all agree.  Persisted under
    // meter/pwrRatedMaxW (default 5.0 W = HL2+ on-board PA).
    double pwrRatedMaxW_ = 5.0;     // danger-zone (red) starts here
    double pwrScaleMaxW_ = 10.0;    // full-scale watts (== 2 * rated max)
    int    txSecondary_  = -1;      // -1 = hide; else Source enum value
    int    txSecondary2_ = -1;      // -1 = hide; else Source enum value
    QString secondary2Text_;        // populated by computePwr/computeSwr
    QVariantList ladderRows_;       // Multi-source rows for Ladder style
    // Source-agnostic "danger zone start" position (0..1 along the
    // scale).  S-meter computes from dBm math via the existing path;
    // PWR/SWR/etc. write directly.  normAtS9() Q_PROPERTY returns
    // this for non-SMETER sources (kept the historical name so the
    // QML renderers' zone-coloring logic works unchanged for all
    // sources — only the threshold's meaning differs per source).
    double normDanger_ = 0.0;

    lyra::ipc::HL2Stream   *stream_   = nullptr;
    lyra::dsp::WdspEngine  *wdsp_     = nullptr;
    lyra::wire::P2RxBridge *p2_       = nullptr;   // P2 telemetry source
    // TX-rip Phase 1 (Q2): txWorker_ removed; field returns with the
    // new TX DSP worker (docs/TX_ARCHITECTURAL_MAPPING.md §10.3).
    QTimer timer_;

    // Active scale endpoints (recomputed each tick from the VFO freq;
    // S9 = -73 dBm below 30 MHz, -93 dBm above — standard HF/VHF split).
    double floorDbm_ = -124.0;
    double ceilDbm_  = -13.0;
    double s9Dbm_    = -73.0;
    bool   aboveS9_  = false;

    double level_ = 0.0;       // smoothed normalized value
    double peak_  = 0.0;       // peak-hold normalized
    double maxPeak_ = 0.0;     // max-hold "high-water mark" normalized
    double glow_  = 0.0;       // afterglow normalized
    int    holdCtr_ = 0;       // peak-hold dwell counter
    int    maxHoldCtr_ = 0;    // max-hold dwell counter
    double dispDbm_ = -140.0;  // smoothed dBm for the readout (S-meter)

    // PWR meter sliding-window MAX state (operator bench 2026-05-31
    // identified the meter ballistic bug: brief whistles read low on
    // the needle because Lyra used IIR-smoothing while the verified
    // reference uses a sliding-window MAX detector — sustained tone
    // converges either way but brief peaks are averaged-out by IIR
    // and properly held by MAX).  Ring buffer of the last
    // kPwrWindowSamples ticks of measured watts; the needle reads
    // max() over the ring.  Zero-init = needle starts at 0 W and
    // rises as samples accumulate (correct first-TX behaviour).
    //
    // Window length tuning (operator bench follow-up 2026-05-31 PM):
    // initial fix used 10 samples × 50 ms = 500 ms (verified-
    // reference default).  Operator-observed peak amplitudes were
    // correct (4.6 W Lyra vs 4.3 W Palstar) but the visual hold
    // "felt instant" — a digital bar holding the same pixel value
    // for 500 ms reads as a flash because there's no visible decay
    // motion like an analog needle.  Bumped to 60 samples × 50 ms =
    // 3000 ms = 3 sec hold, matching typical Bird-Palstar PEAK
    // ballistic where a peak parks for several seconds so the
    // operator can read it at leisure.  Trade-off: rapid-fire CW
    // dits or fast voice peaks will show the highest single peak
    // for up to 3 sec — operationally normal for a peak-power
    // meter, less useful for rapidly-changing power.  If operator
    // prefers tighter hold (closer to verified-reference 500 ms)
    // OR wants live tuning, a follow-up adds a Settings → Meter
    // "PWR Peak Hold" spin box mapped to this constant.
    // Fixed-max buffer sized for the worst-case hold time (10 sec at
    // 50 ms tick = 200 slots).  Operator setter (setPwrPeakHoldMs)
    // updates pwrWinSamples_ to the CURRENT active count; only the
    // first pwrWinSamples_ entries are scanned/written.  Avoids
    // dynamic alloc on hold-time changes; cost is 200×8 B = 1.6 KB
    // member overhead, fine.
    static constexpr int kPwrWindowSamplesMax = 200;   // 10 sec at 50ms
    double pwrWinHist_[kPwrWindowSamplesMax] = {};     // zero-initialized
    int    pwrWinIdx_ = 0;
    int    pwrWinSamples_ = 60;   // active count, derived from
                                  // pwrPeakHoldMs_ / kTickMs.  Default
                                  // 60 = 3 sec; updated by the setter.
    int    pwrPeakHoldMs_ = 3000;
    // PWR ballistic mode selector.  Default PWR_PEAK = current
    // shipped behavior (sliding-window MAX, operator-tunable hold).
    // PEP shortens the window to a fixed 500 ms; AVG swaps the
    // detector for an IIR smoother (pwrAvgSmooth_ controls τ).
    PwrBallistic pwrBallistic_ = PWR_PEAK;
    // AVG-mode IIR smoothing coefficient.  α in level += α·(in − level).
    // At 50 ms tick rate, α = 0.22 gives ~200 ms time constant
    // (calm needle, tracks running average of forward power without
    // chasing speech peaks).
    double pwrAvgAlpha_ = 0.22;
    // PEP-mode fixed sample count = 500 ms / 50 ms tick = 10.
    static constexpr int kPwrPepSamples = 10;
    double noiseFloorDbm_ = -140.0;  // rolling-minimum noise floor (dBm)
    double noiseLevel_ = 0.0;  // floor position on the scale (0..1)
    QString snrText_ = QStringLiteral("—");

    double  calDb_ = 0.0;
    int     peakHoldMs_    = 800;   // dwell before decay (operator-tunable)
    int     peakHoldTicks_ = 16;    // = peakHoldMs_ / tick interval
    bool    maxPeakEnabled_ = true; // show the max-hold high-water marker
    int     maxHoldMs_      = 3000; // max-hold dwell (operator-tunable)
    int     maxHoldTicks_   = 60;   // = maxHoldMs_ / tick interval
    int     style_ = 0;              // RX / default visual style (Arc/Bar/Ladder)
    int     txStyle_ = 0;            // TX visual style (used when separateStyle_)
    bool    separateStyle_ = false;  // swap style on the MOX edge?
    Source  source_ = RX_SMETER;     // currently-displayed source (derived)
    Source  rxSource_ = RX_SMETER;   // RX-state preference (persisted)
    Source  txSource_ = PWR;         // TX-state preference (persisted)
    QString text_    = QStringLiteral("S0");
    QString dbmText_ = QStringLiteral("—");

    QVariantList     history_;
    std::deque<double> hist_;
};

} // namespace lyra::ui

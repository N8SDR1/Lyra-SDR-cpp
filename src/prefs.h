// Lyra — shared UI preferences object.
//
// A single C++ QObject that owns operator-tunable display settings and
// persists them to QSettings.  Exposed to QML as the `Prefs` context
// property AND read/written by the QtWidgets Settings dialog — so the
// dialog and the live QML panadapter stay in sync through one source
// of truth (no reaching into QML items from C++).  All native C++;
// cross-platform.
//
// Panadapter visual options (palette, fill mode, glow, etc.) land here
// as they're built, so the Settings → Visuals tab drives them.

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace lyra::ui {

class Prefs : public QObject {
    Q_OBJECT
    Q_PROPERTY(int gridLevel READ gridLevel WRITE setGridLevel
               NOTIFY gridLevelChanged)
    Q_PROPERTY(int targetFps READ targetFps WRITE setTargetFps
               NOTIFY targetFpsChanged)
    Q_PROPERTY(double dbMin READ dbMin WRITE setDbMin NOTIFY dbMinChanged)
    Q_PROPERTY(double dbMax READ dbMax WRITE setDbMax NOTIFY dbMaxChanged)
    // Task #44 Phase 1 — separate TX-side panadapter dB range.
    // Lyra swaps in/out on every moxActiveChanged edge: the
    // operator's drag-the-right-edge muscle memory updates whichever
    // state is currently active; the panadapter saves/restores
    // between (dbMin,dbMax) and (txDbMin,txDbMax) Prefs pairs on
    // every MOX edge.  Defaults +20 / -80 dBFS — chosen to frame a
    // clean tune-carrier line at typical HL2 TX drive levels.
    Q_PROPERTY(double txDbMin READ txDbMin WRITE setTxDbMin
               NOTIFY txDbMinChanged)
    Q_PROPERTY(double txDbMax READ txDbMax WRITE setTxDbMax
               NOTIFY txDbMaxChanged)
    // Task #74 / #95 — what drive level the TUN button keys at.
    // tuneDriveMode selects the source (orchestrated in main.cpp on
    // Stream::tuneEnabledChanged; the pre-tune drive is restored on
    // tune-off):
    //   0 TuneDriveSlider — TUN keys at the operator's live TX Drive %
    //                       (no swap; legacy behaviour).
    //   1 TuneDriveTune   — TUN keys at tuneDrivePct, the per-band,
    //                       live-adjustable tune slider (#74/#78).
    //   2 TuneDriveFixed  — TUN keys at fixedTuneDrivePct, a single
    //                       fixed value independent of either slider.
    // Defaults: tuneDriveMode=0, tuneDrivePct=25, fixedTuneDrivePct=25
    // — "tune at a quarter power into a dummy load" starting points.
    Q_PROPERTY(int  tuneDriveMode READ tuneDriveMode WRITE setTuneDriveMode
               NOTIFY tuneDriveModeChanged)
    Q_PROPERTY(int  tuneDrivePct READ tuneDrivePct WRITE setTuneDrivePct
               NOTIFY tuneDrivePctChanged)
    Q_PROPERTY(int  fixedTuneDrivePct READ fixedTuneDrivePct
               WRITE setFixedTuneDrivePct NOTIFY fixedTuneDrivePctChanged)
    // Task #75 — TCI RX-out gain (dB).  Applied in TciServer on the
    // RX audio path before binary-frame emit, so 3rd-party clients
    // (digital-mode skimmers like MSHV / JTDX / WSJT-X) can be
    // attenuated when Lyra's RX runs hotter than what the client
    // expects.  Range -40..+10 dB; default 0 dB = byte-identical to
    // pre-#75 behaviour.  Stopgap until #49/#55 land per-mode RX/TX
    // gain profile bundles.
    Q_PROPERTY(double tciRxGainDb READ tciRxGainDb WRITE setTciRxGainDb
               NOTIFY tciRxGainDbChanged)
    // Task #108 — symmetric mirror of Task #75 on the INBOUND side.
    // MSHV / JTDX / WSJT-X often send TX audio at levels that overdrive
    // Lyra's TXA ALC — operator-flagged 2026-06-03 ("looks like it
    // might be overdriving it slightly").  Applied in TciServer at
    // audio-block ingress, AFTER decode + resample and BEFORE handing
    // samples to TciMicSource.  Range -40..+10 dB, default 0 dB =
    // unity = byte-identical to pre-#108 behaviour.  Stopgap until
    // per-profile TCI gain ships under #49 + #55.
    Q_PROPERTY(double tciTxGainDb READ tciTxGainDb WRITE setTciTxGainDb
               NOTIFY tciTxGainDbChanged)
    // #175 — waterfall-ID controls.  wfIdLevel = burst level (0..0.065;
    // deliberately low — a full-duty multitone ID is effectively full-power
    // digital into the finals, so a hot level splatters/IMDs and stresses an
    // SS amp).  wfIdEnabled = auto-ID armed (NON-persistent: OFF every
    // session for safety).  wfIdIntervalMin = auto-cadence 0..20 min
    // (0 = once on arm; persisted).
    Q_PROPERTY(double wfIdLevel READ wfIdLevel WRITE setWfIdLevel
               NOTIFY wfIdLevelChanged)
    Q_PROPERTY(bool wfIdEnabled READ wfIdEnabled WRITE setWfIdEnabled
               NOTIFY wfIdEnabledChanged)
    Q_PROPERTY(int  wfIdIntervalMin READ wfIdIntervalMin WRITE setWfIdIntervalMin
               NOTIFY wfIdIntervalMinChanged)
    // Auto-fit the panadapter dB range (ignores dbMin/dbMax).
    Q_PROPERTY(bool dbAuto READ dbAuto WRITE setDbAuto NOTIFY dbAutoChanged)
    Q_PROPERTY(int traceMode READ traceMode WRITE setTraceMode
               NOTIFY traceModeChanged)
    Q_PROPERTY(QString traceColor READ traceColor WRITE setTraceColor
               NOTIFY traceColorChanged)
    Q_PROPERTY(int palette READ palette WRITE setPalette NOTIFY paletteChanged)
    Q_PROPERTY(QString strengthColor READ strengthColor WRITE setStrengthColor
               NOTIFY strengthColorChanged)
    Q_PROPERTY(bool fillEnabled READ fillEnabled WRITE setFillEnabled
               NOTIFY fillEnabledChanged)
    Q_PROPERTY(QString fillColor READ fillColor WRITE setFillColor
               NOTIFY fillColorChanged)
    Q_PROPERTY(int smoothing READ smoothing WRITE setSmoothing
               NOTIFY smoothingChanged)
    Q_PROPERTY(int glow READ glow WRITE setGlow NOTIFY glowChanged)
    Q_PROPERTY(int glassSheen READ glassSheen WRITE setGlassSheen
               NOTIFY glassSheenChanged)
    // Peak-hold markers over the trace (old-Lyra parity).  peakHoldSecs
    // encodes the mode: 0 = Off, -1 = Hold (infinite), -2 = Live,
    // >0 = hold-then-decay seconds.  peakStyle: 0 line / 1 dots /
    // 2 triangles.  peakShowDb toggles numeric dB labels at the
    // strongest in-view peaks.
    Q_PROPERTY(bool peakEnabled READ peakEnabled WRITE setPeakEnabled
               NOTIFY peakEnabledChanged)
    Q_PROPERTY(double peakHoldSecs READ peakHoldSecs WRITE setPeakHoldSecs
               NOTIFY peakHoldSecsChanged)
    Q_PROPERTY(double peakDecayDbps READ peakDecayDbps WRITE setPeakDecayDbps
               NOTIFY peakDecayDbpsChanged)
    Q_PROPERTY(int peakStyle READ peakStyle WRITE setPeakStyle
               NOTIFY peakStyleChanged)
    Q_PROPERTY(QString peakColor READ peakColor WRITE setPeakColor
               NOTIFY peakColorChanged)
    Q_PROPERTY(bool peakShowDb READ peakShowDb WRITE setPeakShowDb
               NOTIFY peakShowDbChanged)
    // #173 CW-5b — CW decoder display prefs (decoded-text colour + font px).
    Q_PROPERTY(QString cwDecodeColor READ cwDecodeColor WRITE setCwDecodeColor
               NOTIFY cwDecodeColorChanged)
    Q_PROPERTY(int cwDecodeFontSize READ cwDecodeFontSize WRITE setCwDecodeFontSize
               NOTIFY cwDecodeFontSizeChanged)
    // #173 — fldigi CW-receiver knobs (persisted so tuned values recall).
    Q_PROPERTY(int cwDecodeBandwidth READ cwDecodeBandwidth WRITE setCwDecodeBandwidth
               NOTIFY cwDecodeBandwidthChanged)
    Q_PROPERTY(int cwDecodeSpeed READ cwDecodeSpeed WRITE setCwDecodeSpeed
               NOTIFY cwDecodeSpeedChanged)
    Q_PROPERTY(bool cwDecodeTracking READ cwDecodeTracking WRITE setCwDecodeTracking
               NOTIFY cwDecodeTrackingChanged)
    Q_PROPERTY(bool cwDecodeMatchedFilter READ cwDecodeMatchedFilter WRITE setCwDecodeMatchedFilter
               NOTIFY cwDecodeMatchedFilterChanged)
    Q_PROPERTY(bool cwDecodeSquelchOn READ cwDecodeSquelchOn WRITE setCwDecodeSquelchOn
               NOTIFY cwDecodeSquelchOnChanged)
    Q_PROPERTY(double cwDecodeSquelchValue READ cwDecodeSquelchValue WRITE setCwDecodeSquelchValue
               NOTIFY cwDecodeSquelchValueChanged)
    // Noise-floor reference line on the panadapter (old-Lyra parity):
    // a dashed line at the rolling ~20th-percentile floor + an
    // "NF -NN dBFS" label.  On/off + colour are operator-tunable.
    Q_PROPERTY(bool noiseFloorEnabled READ noiseFloorEnabled
               WRITE setNoiseFloorEnabled NOTIFY noiseFloorEnabledChanged)
    Q_PROPERTY(QString noiseFloorColor READ noiseFloorColor
               WRITE setNoiseFloorColor NOTIFY noiseFloorColorChanged)
    Q_PROPERTY(bool watermark READ watermark WRITE setWatermark
               NOTIFY watermarkChanged)
    Q_PROPERTY(bool meteors READ meteors WRITE setMeteors NOTIFY meteorsChanged)
    Q_PROPERTY(int meteorGap READ meteorGap WRITE setMeteorGap
               NOTIFY meteorGapChanged)
    Q_PROPERTY(int meteorGold READ meteorGold WRITE setMeteorGold
               NOTIFY meteorGoldChanged)
    // Waterfall has its OWN palette (independent of the by-strength
    // trace palette) + its own custom-ramp base colour, plus a scroll
    // speed (rows per second).
    Q_PROPERTY(int waterfallPalette READ waterfallPalette
               WRITE setWaterfallPalette NOTIFY waterfallPaletteChanged)
    Q_PROPERTY(QString waterfallColor READ waterfallColor
               WRITE setWaterfallColor NOTIFY waterfallColorChanged)
    Q_PROPERTY(int waterfallSpeed READ waterfallSpeed
               WRITE setWaterfallSpeed NOTIFY waterfallSpeedChanged)
    // Waterfall heat-map dB range — INDEPENDENT of the panadapter's
    // dB range (dbMin/dbMax), so the waterfall contrast can be tuned on
    // its own to make detail pop (old-Lyra parity).
    Q_PROPERTY(double waterfallDbMin READ waterfallDbMin
               WRITE setWaterfallDbMin NOTIFY waterfallDbMinChanged)
    Q_PROPERTY(double waterfallDbMax READ waterfallDbMax
               WRITE setWaterfallDbMax NOTIFY waterfallDbMaxChanged)
    // §15.30 — separate TX-state waterfall dB range so an operator who
    // likes a dark RX waterfall with colours that POP on weak SSB
    // doesn't see "blowout" the instant they key.  Same MOX-edge swap
    // pattern as the Task #44 panadapter pair: waterfallDbMin/Max
    // return the LIVE pair (TX while moxActive, else RX) and writes
    // route to the matching persistence key.  Defaults +30 / -70 dBFS
    // frame a clean tune-carrier line at typical HL2 TX drive levels.
    Q_PROPERTY(double txWaterfallDbMin READ txWaterfallDbMin
               WRITE setTxWaterfallDbMin NOTIFY txWaterfallDbMinChanged)
    Q_PROPERTY(double txWaterfallDbMax READ txWaterfallDbMax
               WRITE setTxWaterfallDbMax NOTIFY txWaterfallDbMaxChanged)
    // Auto-fit the waterfall dB range (ignores waterfallDbMin/Max).
    Q_PROPERTY(bool waterfallDbAuto READ waterfallDbAuto
               WRITE setWaterfallDbAuto NOTIFY waterfallDbAutoChanged)
    // Floating frequency readout that follows the cursor over the
    // panadapter (on by default; operator-toggleable).
    Q_PROPERTY(bool cursorReadout READ cursorReadout WRITE setCursorReadout
               NOTIFY cursorReadoutChanged)
    // Zero-beat tuning aid: a small ± needle under the freq readout (CW / AM /
    // SAM / FM).  Off by default; RX-only display.
    Q_PROPERTY(bool zeroBeatMarkers READ zeroBeatMarkers WRITE setZeroBeatMarkers
               NOTIFY zeroBeatMarkersChanged)
    // Panel layout (Settings → Visuals): group the DSP / Options tool panels
    // into one saved "rack" window each, instead of each floating individually
    // from its own header chip.  Read at startup; takes effect on next launch.
    Q_PROPERTY(bool dspPanelsGrouped READ dspPanelsGrouped WRITE setDspPanelsGrouped
               NOTIFY dspPanelsGroupedChanged)
    Q_PROPERTY(bool optionsPanelsGrouped READ optionsPanelsGrouped
               WRITE setOptionsPanelsGrouped NOTIFY optionsPanelsGroupedChanged)
    // Panadapter zoom: 1.0 = full IQ span, higher magnifies the centre.
    // Drives WdspEngine.zoom (display-side crop, old-Lyra method).
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    // Demod mode (USB/LSB/CWU/…) + RX filter bandwidth (Hz) for the
    // CURRENT mode.  Bandwidth is remembered per-mode (old-Lyra style):
    // switching mode recalls that mode's last bandwidth.  Drives
    // WdspEngine.mode / .bandwidth.
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(int rxBandwidth READ rxBandwidth WRITE setRxBandwidth
               NOTIFY rxBandwidthChanged)
    // TX Component 8c — TX filter bandwidth (Hz) for the CURRENT mode.
    // Per-mode just like rxBandwidth.  For SSB the value is the high
    // edge (low fixed at 200 Hz via TxChannel::open() default until a
    // separate Low spinbox lands per design doc §9.2).  Drives the
    // TX channel via HL2Stream::setTxBwHz.  See bwLocked for the
    // mirror-to-RX semantics.
    Q_PROPERTY(int txBandwidth READ txBandwidth WRITE setTxBandwidth
               NOTIFY txBandwidthChanged)
    // TX Component 8c — RX↔TX bandwidth mirror.  When ON, setting
    // either side updates the other (both directions, single-step,
    // no recursion).  Toggling ON pulls RX into TX (matches old-Lyra
    // direction).  Per-mode storage is unchanged; the mirror only
    // touches the CURRENT mode's pair.
    Q_PROPERTY(bool bwLocked READ bwLocked WRITE setBwLocked
               NOTIFY bwLockedChanged)
    // Task #53 — shared RX+TX filter low edge (Hz, single global
    // value).  Replaces the previously-hardcoded 0 (RX SSB low)
    // and 200 (TX SSB low).  Operator-tunable for ESSB-profile
    // low-end body (50-70 Hz) vs narrow-comms mains rejection
    // (100-200 Hz).  Interim — when TX Profile Manager (#49) lands,
    // each named profile carries its own (rx_lo, rx_hi, tx_lo,
    // tx_hi) tuple that overrides this default on profile load.
    Q_PROPERTY(int filterLow READ filterLow WRITE setFilterLow
               NOTIFY filterLowChanged)
    // IQ sample rate (Hz): 96000 / 192000 / 384000.  Drives the HL2 wire
    // speed bits + the WDSP channel rate (panadapter span follows).
    Q_PROPERTY(int sampleRate READ sampleRate WRITE setSampleRate
               NOTIFY sampleRateChanged)
    // Waterfall collapsed (old-Lyra triangle toggle) — when true the
    // waterfall pane is hidden and the panadapter takes the full height.
    Q_PROPERTY(bool waterfallCollapsed READ waterfallCollapsed
               WRITE setWaterfallCollapsed NOTIFY waterfallCollapsedChanged)
    // Opaque serialized state of the panadapter/waterfall SplitView
    // (the divider position).  QMainWindow.saveState() does NOT cover
    // splitters inside a dock's QML content, so it's persisted here as
    // the QML SplitView.saveState()/restoreState() blob.  QVariant so
    // the value round-trips through QSettings byte-exact.
    Q_PROPERTY(QVariant panadapterSplit READ panadapterSplit
               WRITE setPanadapterSplit NOTIFY panadapterSplitChanged)
    // Operator / station identity (Settings → Hardware).  Callsign feeds
    // TCI/logging; the location (grid, or manual lat/lon override) feeds
    // the weather sources + solar panel.  Effective lat/lon = grid when
    // valid, else the manual override.
    Q_PROPERTY(QString callsign READ callsign WRITE setCallsign
               NOTIFY callsignChanged)
    Q_PROPERTY(QString gridSquare READ gridSquare WRITE setGridSquare
               NOTIFY gridSquareChanged)
    // Amateur band-plan region for panadapter overlays (sub-band
    // segments, landmarks, out-of-band advisories).  Values:
    // US / IARU_R1 / IARU_R3 / NONE.
    Q_PROPERTY(QString bandPlanRegion READ bandPlanRegion
               WRITE setBandPlanRegion NOTIFY bandPlanRegionChanged)
    // Optional country override layered on top of the region base table,
    // for countries whose allocation deviates from their IARU region on
    // some bands (e.g. UK / Canada 60m).  "AUTO" = region only.
    Q_PROPERTY(QString bandPlanCountry READ bandPlanCountry
               WRITE setBandPlanCountry NOTIFY bandPlanCountryChanged)
    // Band-plan panadapter-overlay layer toggles (Settings → Hardware →
    // Band plan).  All default ON; the master Region=NONE hides everything.
    Q_PROPERTY(bool bandPlanSegments READ bandPlanSegments
               WRITE setBandPlanSegments NOTIFY bandPlanSegmentsChanged)
    Q_PROPERTY(bool bandPlanLandmarks READ bandPlanLandmarks
               WRITE setBandPlanLandmarks NOTIFY bandPlanLandmarksChanged)
    Q_PROPERTY(bool bandPlanBeacons READ bandPlanBeacons
               WRITE setBandPlanBeacons NOTIFY bandPlanBeaconsChanged)
    Q_PROPERTY(bool bandPlanEdges READ bandPlanEdges
               WRITE setBandPlanEdges NOTIFY bandPlanEdgesChanged)
    // US license-class phone-edge markers on the panadapter overlay.
    // US-only (the QML gates on region == "US"); default OFF — it is a
    // decluttered opt-in reference cue, not on by default.
    Q_PROPERTY(bool bandPlanClassEdges READ bandPlanClassEdges
               WRITE setBandPlanClassEdges NOTIFY bandPlanClassEdgesChanged)
    // Advisory toast at key-down when the effective TX frequency is outside
    // any amateur band for the current region.  Default ON, and DECOUPLED
    // from the visual bandPlanEdges toggle — decluttering the overlay must
    // never silently disable the transmit-safety warning.  Advisory only:
    // it never inhibits TX.
    Q_PROPERTY(bool bandPlanTxWarn READ bandPlanTxWarn
               WRITE setBandPlanTxWarn NOTIFY bandPlanTxWarnChanged)
    // Show the 11m / CB band row on the Band panel (Settings → Hardware).
    Q_PROPERTY(bool cbBandEnabled READ cbBandEnabled
               WRITE setCbBandEnabled NOTIFY cbBandEnabledChanged)
    // Panadapter mouse-wheel "Panafall" scroll step (Hz) — distinct from
    // the fine VFO step on the Tuning panel; this skims across a band.
    Q_PROPERTY(int panScrollStepHz READ panScrollStepHz
               WRITE setPanScrollStepHz NOTIFY panScrollStepHzChanged)
    // When true, panadapter freq-set gestures (click/drag/wheel) round the
    // resulting (operator-facing) frequency to the nearest 100 Hz.
    Q_PROPERTY(bool panRound100 READ panRound100
               WRITE setPanRound100 NOTIFY panRound100Changed)
    // Verbose diagnostic logging (Settings → Hardware → Diagnostics).
    // OFF (default) keeps only warnings/errors in the log; ON also
    // captures Debug/Info for when we need the full picture.
    Q_PROPERTY(bool debugLogging READ debugLogging WRITE setDebugLogging
               NOTIFY debugLoggingChanged)
    // Task #36 — Hardware PTT input (foot switch / hand mic / mic button)
    // forwarder.  Default OFF — per the project-memory §10 Q#1 finding,
    // N8SDR's HL2+/AK4951 carries a non-zero EP6 C0 bit 0 (ptt_in) at
    // RX rest, so an always-on forwarder mis-reads it as a foot-switch
    // press → spurious MOX → phantom-TX surge.  Operator opts in via
    // Settings → TX → Advanced AFTER bench-verifying that ptt_in is a
    // clean 0 at RX rest on their specific unit (or after confirming
    // they have a real foot-switch / mic-button wired to the HL2 PTT
    // input).  Persisted: tx/hw_ptt_enabled.
    Q_PROPERTY(bool hwPttEnabled READ hwPttEnabled WRITE setHwPttEnabled
               NOTIFY hwPttEnabledChanged)

    // Task #157 — space-bar PTT enable.  The space bar keys MOX (hold to
    // transmit) when the main window has focus and no text field is being
    // edited.  Default TRUE (the historical behaviour); operators who keep
    // triggering TX by accidentally hitting space untick it — the on-screen
    // MOX button, a hardware foot switch, and TCI/CAT keying are unaffected.
    // Persisted: tx/space_bar_ptt_enabled.
    // Auto-start (auto-connect) on launch.  Default TRUE (historical
    // behaviour: Lyra opens the last radio at startup).  Operators who
    // prefer to launch and click Start themselves untick it.  Persisted:
    // hw/autoStartOnLaunch.
    Q_PROPERTY(bool autoStartOnLaunch READ autoStartOnLaunch
               WRITE setAutoStartOnLaunch NOTIFY autoStartOnLaunchChanged)
    Q_PROPERTY(bool spaceBarPttEnabled READ spaceBarPttEnabled
               WRITE setSpaceBarPttEnabled NOTIFY spaceBarPttEnabledChanged)
    // Process priority — SetPriorityClass on the Lyra process.  Helps Lyra
    // hold the HL2 wire cadence when a busy foreground app (browser, etc.)
    // is competing for the CPU.  0 = Normal (Windows default) / 1 = Above
    // Normal / 2 = High.  Per-PC (machine-local), NOT per-rig.  Applied at
    // startup + on change.  Persisted: hw/processPriority.
    Q_PROPERTY(int processPriority READ processPriority
               WRITE setProcessPriority NOTIFY processPriorityChanged)
    // Reduce TX drive in the digital data modes (DIGU/DIGL).  Opt-in
    // (digitalDriveEnabled, default OFF).  digitalDrivePct is the fraction
    // of the band's set drive to transmit at while in DIGU/DIGL (10..100,
    // default 100 = no change).  The reduction is transient (emit-time) —
    // the operator's per-band set drive + dial are untouched, and it stays
    // under the Max-Drive cap.  Persisted: tx/digitalDriveEnabled +
    // tx/digitalDrivePct.
    Q_PROPERTY(bool digitalDriveEnabled READ digitalDriveEnabled
               WRITE setDigitalDriveEnabled NOTIFY digitalDriveEnabledChanged)
    Q_PROPERTY(int  digitalDrivePct READ digitalDrivePct
               WRITE setDigitalDrivePct NOTIFY digitalDrivePctChanged)

    // Task #33 — TX mic source.  Operator-selected via Settings → TX
    // → Mic Source.  Token strings match the TCI v2 TRX source-token
    // convention (TCI spec §3.3) so a TCI client that sends
    // `trx:0,true,tci` selects the TCI source automatically (commit
    // 3's TRX handler auto-flips this).  Persisted: tx/mic_source
    // (default "mic1" = current v0.2.0..v0.2.2 codec-mic path).
    //
    // Recognised tokens:
    //   "mic1"    — HL2/HL2+ codec mic input (Hl2Ep6MicSource)
    //   "tci"     — inbound TCI v2 TX_AUDIO_STREAM (TciMicSource)
    //   "micpc"   — host PC audio capture (future v0.2.x VAC1)
    //   "micpc2"  — second host PC capture device (future VAC2)
    //
    // Unknown tokens fall back to "mic1" (safety: never end up
    // routing to an inactive source).
    Q_PROPERTY(QString micSource READ micSource WRITE setMicSource
               NOTIFY micSourceChanged)

    // Global tooltips on/off (Settings → Visuals).  Default true.  Every
    // QML ToolTip.visible binding ANDs this in, and an app-wide event
    // filter swallows QWidget (Settings dialog) tooltips too, so the
    // toggle is genuinely global.  Persisted: ui/tooltips_enabled.
    Q_PROPERTY(bool tooltipsEnabled READ tooltipsEnabled
               WRITE setTooltipsEnabled NOTIFY tooltipsEnabledChanged)

public:
    explicit Prefs(QObject *parent = nullptr);

    // Palette display names (presets + "Custom color…") for QML pickers
    // — index-aligned with the palette/waterfallPalette int values.
    Q_INVOKABLE QStringList paletteNames() const;

    // Fire-and-forget "clear the peak-hold buffer" request from the
    // Display panel's Clear button — the panadapter (a different dock)
    // listens on peakClearRequested and calls clearPeaks().
    Q_INVOKABLE void requestClearPeaks() { emit peakClearRequested(); }

    int  gridLevel() const { return gridLevel_; }
    void setGridLevel(int v);
    int  targetFps() const { return targetFps_; }
    void setTargetFps(int v);
    // Task #44 Phase 1 — dbMin/dbMax return the LIVE pair (TX while
    // moxActive, else RX); writes route to the matching persistence
    // key.  setMoxActive() is wired to Stream::moxActiveChanged and
    // emits dbMin/dbMaxChanged so the QML panadapter binding re-reads
    // the swapped values automatically.  rxDbMin/rxDbMax (the
    // pre-Phase-1 RX-only values) remain on the kDbMin/kDbMax keys
    // unchanged for backward QSettings compatibility.
    double dbMin() const { return moxActive_ ? txDbMin_ : rxDbMin_; }
    void   setDbMin(double v);
    double dbMax() const { return moxActive_ ? txDbMax_ : rxDbMax_; }
    void   setDbMax(double v);
    double rxDbMin() const { return rxDbMin_; }
    void   setRxDbMin(double v);
    double rxDbMax() const { return rxDbMax_; }
    void   setRxDbMax(double v);
    double txDbMin() const { return txDbMin_; }
    void   setTxDbMin(double v);
    double txDbMax() const { return txDbMax_; }
    void   setTxDbMax(double v);
    bool   moxActive() const { return moxActive_; }
    // TUN drive source (see the tuneDriveMode property comment).
    enum TuneDriveMode { TuneDriveSlider = 0, TuneDriveTune = 1,
                         TuneDriveFixed = 2 };
    int    tuneDriveMode() const { return tuneDriveMode_; }
    void   setTuneDriveMode(int v);
    int    tuneDrivePct() const { return tuneDrivePct_; }
    void   setTuneDrivePct(int v);
    int    fixedTuneDrivePct() const { return fixedTuneDrivePct_; }
    void   setFixedTuneDrivePct(int v);
    double tciRxGainDb() const { return tciRxGainDb_; }
    void   setTciRxGainDb(double v);
    double tciTxGainDb() const { return tciTxGainDb_; }
    void   setTciTxGainDb(double v);
    double wfIdLevel() const { return wfIdLevel_; }       // #175
    void   setWfIdLevel(double v);                        // #175 (clamps 0..0.065)
    bool   wfIdEnabled() const { return wfIdEnabled_; }   // #175 (non-persist)
    void   setWfIdEnabled(bool on);
    int    wfIdIntervalMin() const { return wfIdIntervalMin_; }  // #175 (0..20)
    void   setWfIdIntervalMin(int m);
public slots:
    void   setMoxActive(bool on);
public:
    bool dbAuto() const { return dbAuto_; }
    void setDbAuto(bool v);
    int  traceMode() const { return traceMode_; }
    void setTraceMode(int v);
    QString traceColor() const { return traceColor_; }
    void    setTraceColor(const QString &hex);
    int  palette() const { return palette_; }
    void setPalette(int v);
    QString strengthColor() const { return strengthColor_; }
    void    setStrengthColor(const QString &hex);
    bool fillEnabled() const { return fillEnabled_; }
    void setFillEnabled(bool v);
    QString fillColor() const { return fillColor_; }
    void    setFillColor(const QString &hex);
    int  smoothing() const { return smoothing_; }
    void setSmoothing(int v);
    int  glow() const { return glow_; }
    void setGlow(int v);
    int  glassSheen() const { return glassSheen_; }
    void setGlassSheen(int v);
    bool peakEnabled() const { return peakEnabled_; }
    void setPeakEnabled(bool v);
    double peakHoldSecs() const { return peakHoldSecs_; }
    void   setPeakHoldSecs(double v);
    double peakDecayDbps() const { return peakDecayDbps_; }
    void   setPeakDecayDbps(double v);
    int  peakStyle() const { return peakStyle_; }
    void setPeakStyle(int v);
    QString peakColor() const { return peakColor_; }
    void    setPeakColor(const QString &hex);
    QString cwDecodeColor() const { return cwDecodeColor_; }
    void    setCwDecodeColor(const QString &hex);
    int     cwDecodeFontSize() const { return cwDecodeFontSize_; }
    void    setCwDecodeFontSize(int px);
    int     cwDecodeBandwidth() const { return cwDecodeBandwidth_; }
    void    setCwDecodeBandwidth(int hz);
    int     cwDecodeSpeed() const { return cwDecodeSpeed_; }
    void    setCwDecodeSpeed(int wpm);
    bool    cwDecodeTracking() const { return cwDecodeTracking_; }
    void    setCwDecodeTracking(bool on);
    bool    cwDecodeMatchedFilter() const { return cwDecodeMatchedFilter_; }
    void    setCwDecodeMatchedFilter(bool on);
    bool    cwDecodeSquelchOn() const { return cwDecodeSquelchOn_; }
    void    setCwDecodeSquelchOn(bool on);
    double  cwDecodeSquelchValue() const { return cwDecodeSquelchValue_; }
    void    setCwDecodeSquelchValue(double v);
    bool peakShowDb() const { return peakShowDb_; }
    void setPeakShowDb(bool v);
    bool noiseFloorEnabled() const { return noiseFloorEnabled_; }
    void setNoiseFloorEnabled(bool v);
    QString noiseFloorColor() const { return noiseFloorColor_; }
    void    setNoiseFloorColor(const QString &hex);
    bool watermark() const { return watermark_; }
    void setWatermark(bool v);
    bool meteors() const { return meteors_; }
    void setMeteors(bool v);
    int  meteorGap() const { return meteorGap_; }
    void setMeteorGap(int v);
    int  meteorGold() const { return meteorGold_; }
    void setMeteorGold(int v);
    int  waterfallPalette() const { return waterfallPalette_; }
    void setWaterfallPalette(int v);
    QString waterfallColor() const { return waterfallColor_; }
    void    setWaterfallColor(const QString &hex);
    int  waterfallSpeed() const { return waterfallSpeed_; }
    void setWaterfallSpeed(int v);
    // §15.30 — waterfallDbMin/Max return the LIVE pair (TX while
    // moxActive, else RX); writes route to the matching backing.
    // rxWaterfallDbMin_/Max_ remain on the kWfDbMin/kWfDbMax keys
    // unchanged for backward QSettings compatibility (the operator's
    // existing RX waterfall scaling is preserved across upgrade).
    double waterfallDbMin() const {
        return moxActive_ ? txWaterfallDbMin_ : rxWaterfallDbMin_;
    }
    void   setWaterfallDbMin(double v);
    double waterfallDbMax() const {
        return moxActive_ ? txWaterfallDbMax_ : rxWaterfallDbMax_;
    }
    void   setWaterfallDbMax(double v);
    // Direct accessors for the RX-only and TX-only pairs (mirrors the
    // panadapter Task #44 surface — rxDbMin/setRxDbMin etc.).
    double rxWaterfallDbMin() const { return rxWaterfallDbMin_; }
    void   setRxWaterfallDbMin(double v);
    double rxWaterfallDbMax() const { return rxWaterfallDbMax_; }
    void   setRxWaterfallDbMax(double v);
    double txWaterfallDbMin() const { return txWaterfallDbMin_; }
    void   setTxWaterfallDbMin(double v);
    double txWaterfallDbMax() const { return txWaterfallDbMax_; }
    void   setTxWaterfallDbMax(double v);
    bool waterfallDbAuto() const { return waterfallDbAuto_; }
    void setWaterfallDbAuto(bool v);
    QVariant panadapterSplit() const { return panadapterSplit_; }
    void     setPanadapterSplit(const QVariant &v);
    bool cursorReadout() const { return cursorReadout_; }
    void setCursorReadout(bool v);
    bool zeroBeatMarkers() const { return zeroBeatMarkers_; }
    void setZeroBeatMarkers(bool v);
    bool dspPanelsGrouped() const { return dspPanelsGrouped_; }
    void setDspPanelsGrouped(bool v);
    bool optionsPanelsGrouped() const { return optionsPanelsGrouped_; }
    void setOptionsPanelsGrouped(bool v);
    double zoom() const { return zoom_; }
    void   setZoom(double v);
    QString mode() const { return mode_; }
    void    setMode(const QString &m);
    int  rxBandwidth() const;            // bandwidth for the current mode
    void setRxBandwidth(int hz);
    // TX Component 8c — current-mode TX bandwidth, mirroring rxBandwidth.
    int  txBandwidth() const;
    void setTxBandwidth(int hz);
    bool bwLocked() const { return bwLocked_; }
    void setBwLocked(bool v);
    // Task #53 — shared RX+TX filter low edge.
    int  filterLow() const { return filterLow_; }
    void setFilterLow(int hz);
    int  sampleRate() const { return sampleRate_; }
    void setSampleRate(int hz);
    bool waterfallCollapsed() const { return waterfallCollapsed_; }
    void setWaterfallCollapsed(bool v);
    // --- operator / station identity ---
    QString callsign() const { return callsign_; }
    void    setCallsign(const QString &c);
    QString gridSquare() const { return gridSquare_; }
    void    setGridSquare(const QString &g);   // normalized; "" if invalid
    double  manualLat() const { return manualLat_; }   // NaN = unset
    double  manualLon() const { return manualLon_; }
    // Set/clear the manual lat/lon override (pass NaN,NaN to clear).
    Q_INVOKABLE void setManualLatLon(double lat, double lon);
    // Effective operator location: grid (if valid) else manual override.
    // Returns false (and leaves *lat/*lon untouched) when neither is set.
    bool operatorLocation(double *lat, double *lon) const;
    QString bandPlanRegion() const { return bandPlanRegion_; }
    void    setBandPlanRegion(const QString &r);
    QString bandPlanCountry() const { return bandPlanCountry_; }
    void    setBandPlanCountry(const QString &c);
    bool bandPlanSegments() const { return bandPlanSegments_; }
    void setBandPlanSegments(bool v);
    bool bandPlanLandmarks() const { return bandPlanLandmarks_; }
    void setBandPlanLandmarks(bool v);
    bool bandPlanBeacons() const { return bandPlanBeacons_; }
    void setBandPlanBeacons(bool v);
    bool bandPlanEdges() const { return bandPlanEdges_; }
    void setBandPlanEdges(bool v);
    bool bandPlanClassEdges() const { return bandPlanClassEdges_; }
    void setBandPlanClassEdges(bool v);
    bool bandPlanTxWarn() const { return bandPlanTxWarn_; }
    void setBandPlanTxWarn(bool v);
    bool cbBandEnabled() const { return cbBandEnabled_; }
    void setCbBandEnabled(bool v);
    int  panScrollStepHz() const { return panScrollStepHz_; }
    void setPanScrollStepHz(int hz);
    bool panRound100() const { return panRound100_; }
    void setPanRound100(bool v);
    // Per-mode-kind band-plan segment colour (override of the defaults).
    // kind = "CW"/"DIG"/"SSB"/"FM"/"MIX"/"BC".  Always returns a colour.
    QString bandPlanColor(const QString &kind) const;
    void    setBandPlanColor(const QString &kind, const QString &hex);
    static QString defaultBandPlanColor(const QString &kind);
    bool debugLogging() const { return debugLogging_; }
    void setDebugLogging(bool on);
    // Task #36 — Hardware PTT input opt-in.  Default false (the §10 Q#1
    // safety-first posture).  Setter persists + emits.
    bool hwPttEnabled() const { return hwPttEnabled_; }
    void setHwPttEnabled(bool on);
    // Task #157 — space-bar PTT opt-out.  Default true (historical
    // behaviour).  Setter persists + emits.
    bool spaceBarPttEnabled() const { return spaceBarPttEnabled_; }
    void setSpaceBarPttEnabled(bool on);
    bool autoStartOnLaunch() const { return autoStartOnLaunch_; }
    void setAutoStartOnLaunch(bool on);
    int  processPriority() const { return processPriority_; }
    void setProcessPriority(int level);
    bool digitalDriveEnabled() const { return digitalDriveEnabled_; }
    void setDigitalDriveEnabled(bool on);
    int  digitalDrivePct() const { return digitalDrivePct_; }
    void setDigitalDrivePct(int pct);

    QString micSource() const { return micSource_; }
    void    setMicSource(const QString &token);
    bool tooltipsEnabled() const { return tooltipsEnabled_; }
    void setTooltipsEnabled(bool on);
    // Task #33 — operator-facing token list with display labels.  Used
    // by the Settings → TX → Mic Source dropdown.  Order matches the
    // dropdown order (Mic In default first).  Disabled entries get a
    // tooltip from micSourceTooltip() below.
    static QStringList micSourceTokens();
    static QString     micSourceLabel(const QString &token);
    static bool        micSourceEnabled(const QString &token);
    static QString     micSourceTooltip(const QString &token);
    // Built-in default RX bandwidth for a mode (first run / unset).
    static int defaultBandwidthFor(const QString &mode);
    // Bandwidth-memory key: collapses to the mode FAMILY so USB/LSB share
    // one "SSB" bandwidth, CWU/CWL share "CW", DIGU/DIGL share "Digital"
    // (AM/SAM/DSB/FM stand alone).  A sideband flip then changes no
    // remembered bandwidth — it neither re-filters nor dirties an active
    // TX profile (a profile owns its bandwidth; sideband is not a profile
    // field).  MUST stay in sync with ProfileManager::modeFamily.
    static QString bwFamilyKey(const QString &mode);

signals:
    void gridLevelChanged();
    void targetFpsChanged();
    void dbMinChanged();
    void dbMaxChanged();
    void txDbMinChanged();
    void txDbMaxChanged();
    void tuneDriveModeChanged();
    void tuneDrivePctChanged();
    void fixedTuneDrivePctChanged();
    void tciRxGainDbChanged();
    void tciTxGainDbChanged();
    void wfIdLevelChanged();   // #175
    void wfIdEnabledChanged();
    void wfIdIntervalMinChanged();
    void dbAutoChanged();
    void traceModeChanged();
    void traceColorChanged();
    void paletteChanged();
    void strengthColorChanged();
    void fillEnabledChanged();
    void fillColorChanged();
    void smoothingChanged();
    void glowChanged();
    void glassSheenChanged();
    void peakEnabledChanged();
    void peakHoldSecsChanged();
    void peakDecayDbpsChanged();
    void peakStyleChanged();
    void peakColorChanged();
    void peakShowDbChanged();
    void cwDecodeColorChanged();
    void cwDecodeFontSizeChanged();
    void cwDecodeBandwidthChanged();
    void cwDecodeSpeedChanged();
    void cwDecodeTrackingChanged();
    void cwDecodeMatchedFilterChanged();
    void cwDecodeSquelchOnChanged();
    void cwDecodeSquelchValueChanged();
    void peakClearRequested();
    void noiseFloorEnabledChanged();
    void noiseFloorColorChanged();
    void watermarkChanged();
    void meteorsChanged();
    void meteorGapChanged();
    void meteorGoldChanged();
    void waterfallPaletteChanged();
    void waterfallColorChanged();
    void waterfallSpeedChanged();
    void waterfallDbMinChanged();
    void waterfallDbMaxChanged();
    void txWaterfallDbMinChanged();
    void txWaterfallDbMaxChanged();
    void waterfallDbAutoChanged();
    void panadapterSplitChanged();
    void cursorReadoutChanged();
    void zeroBeatMarkersChanged();
    void dspPanelsGroupedChanged();
    void optionsPanelsGroupedChanged();
    void zoomChanged();
    void modeChanged();
    void rxBandwidthChanged();
    void txBandwidthChanged();
    void bwLockedChanged();
    void filterLowChanged();
    void sampleRateChanged();
    void waterfallCollapsedChanged();
    void callsignChanged();
    void gridSquareChanged();
    void locationChanged();   // effective lat/lon changed (grid or manual)
    void bandPlanRegionChanged();
    void bandPlanCountryChanged();
    void bandPlanSegmentsChanged();
    void bandPlanLandmarksChanged();
    void bandPlanBeaconsChanged();
    void bandPlanEdgesChanged();
    void bandPlanClassEdgesChanged();
    void bandPlanTxWarnChanged();
    void bandPlanColorsChanged();
    void cbBandEnabledChanged();
    void panScrollStepHzChanged();
    void panRound100Changed();
    void debugLoggingChanged();
    void hwPttEnabledChanged();
    void spaceBarPttEnabledChanged();
    void autoStartOnLaunchChanged();
    void processPriorityChanged();
    void digitalDriveEnabledChanged();
    void digitalDrivePctChanged();
    void micSourceChanged();
    void tooltipsEnabledChanged();

private:
    int     gridLevel_;
    int     targetFps_;
    // Task #44 Phase 1 — rxDbMin_/rxDbMax_ are the persistent RX
    // values (on kDbMin/kDbMax keys for backward compat); txDbMin_/
    // txDbMax_ are the TX-state pair; moxActive_ selects which the
    // public dbMin/dbMax accessors return.
    double  rxDbMin_;
    double  rxDbMax_;
    double  txDbMin_;
    double  txDbMax_;
    bool    moxActive_ = false;
    // Task #74 — TUN separate-drive state (defaults applied in ctor).
    int     tuneDriveMode_ = 0;   // 0 slider / 1 tune / 2 fixed
    int     tuneDrivePct_ = 25;
    int     fixedTuneDrivePct_ = 25;
    // Task #75 — TCI RX-out gain (dB); default 0 = unity.
    double  tciRxGainDb_ = 0.0;
    // Task #108 — symmetric INBOUND TCI gain (MSHV → Lyra TXA).
    // Default 0 dB = unity = byte-identical to pre-#108 behaviour.
    double  tciTxGainDb_ = 0.0;
    double  wfIdLevel_       = 0.06;   // #175 — waterfall-ID burst level (0..0.065)
    bool    wfIdEnabled_     = false;  // #175 — NON-persist; OFF every session
    int     wfIdIntervalMin_ = 0;      // #175 — auto-cadence 0..20 (0 = once)
    bool    dbAuto_;
    int     traceMode_;
    QString traceColor_;
    int     palette_;
    QString strengthColor_;
    bool    fillEnabled_;
    QString fillColor_;
    int     smoothing_;
    int     glow_;
    int     glassSheen_;
    bool    peakEnabled_;
    double  peakHoldSecs_;
    double  peakDecayDbps_;
    int     peakStyle_;
    QString peakColor_;
    bool    peakShowDb_;
    QString cwDecodeColor_;
    int     cwDecodeFontSize_;
    int     cwDecodeBandwidth_;
    int     cwDecodeSpeed_;
    bool    cwDecodeTracking_;
    bool    cwDecodeMatchedFilter_;
    bool    cwDecodeSquelchOn_;
    double  cwDecodeSquelchValue_;
    bool    noiseFloorEnabled_;
    QString noiseFloorColor_;
    bool    watermark_;
    bool    meteors_;
    int     meteorGap_;
    int     meteorGold_;
    int     waterfallPalette_;
    QString waterfallColor_;
    int     waterfallSpeed_;
    // §15.30 — rxWaterfallDbMin_/Max_ are the persistent RX values
    // (on the kWfDbMin/kWfDbMax keys for backward compat — that's
    // what the operator's current "dark RX waterfall" config lives
    // on); txWaterfallDbMin_/Max_ are the TX-state pair (defaults
    // -70 / +30 dBFS = the reference TX waterfall Low / High Level).
    // moxActive_ selects which the public waterfallDbMin/Max
    // accessors return.
    double  rxWaterfallDbMin_;
    double  rxWaterfallDbMax_;
    double  txWaterfallDbMin_;
    double  txWaterfallDbMax_;
    bool    waterfallDbAuto_;
    QVariant panadapterSplit_;
    bool    cursorReadout_;
    bool    zeroBeatMarkers_ = false;
    int     processPriority_ = 0;   // 0 Normal / 1 Above Normal / 2 High
    bool    digitalDriveEnabled_ = false;
    int     digitalDrivePct_ = 100;   // 10..100 % of set drive in DIGU/DIGL
    bool    dspPanelsGrouped_ = false;
    bool    optionsPanelsGrouped_ = false;
    double  zoom_;
    QString mode_;
    // Bandwidth memory keyed by mode FAMILY (bwFamilyKey): USB/LSB share
    // one "SSB" slot, CWU/CWL "CW", DIGU/DIGL "Digital" (AM/SAM/DSB/FM
    // stand alone).  A sideband flip changes no remembered bandwidth.
    // (Hash name kept for churn; the KEY is now the family, not the mode.)
    QHash<QString, int> bwByMode_;   // per-family RX bandwidth memory
    // TX Component 8c — per-family TX bandwidth memory + the RX↔TX
    // lock flag.  Defaults come from defaultBandwidthFor() if the
    // operator hasn't picked a TX BW for that family yet (fresh install).
    QHash<QString, int> txBwByMode_;
    bool                bwLocked_ = false;
    // Task #53 — shared RX+TX filter low edge.  Default 100 Hz
    // (safe mains-rejection middle ground).
    int                 filterLow_ = 100;
    int     sampleRate_;
    bool    waterfallCollapsed_;
    QString callsign_;
    QString gridSquare_;
    double  manualLat_;   // NaN = unset
    double  manualLon_;   // NaN = unset
    QString bandPlanRegion_;
    QString bandPlanCountry_;
    bool    bandPlanSegments_  = true;
    bool    bandPlanLandmarks_ = true;
    bool    bandPlanBeacons_   = true;
    bool    bandPlanEdges_     = true;
    bool    bandPlanClassEdges_ = false;   // US license-class edges, opt-in
    bool    bandPlanTxWarn_    = true;     // TX out-of-band advisory, on
    QHash<QString, QString> bandPlanColors_;   // kind → override hex (sparse)
    bool    cbBandEnabled_ = false;
    int     panScrollStepHz_ = 1000;
    bool    panRound100_ = false;
    bool    debugLogging_ = false;
    // Task #36 — HW PTT opt-in.  Default false; gated forwarder in
    // HL2Stream's RX loop reads the mirrored atomic.
    bool    hwPttEnabled_ = false;
    // Task #157 — space-bar PTT opt-out.  Default true (historical
    // behaviour); MainWindow's space-bar keydown/keyup gate reads it.
    bool    spaceBarPttEnabled_ = true;
    bool    autoStartOnLaunch_ = true;   // auto-connect last radio at launch
    // Task #33 — TX mic source token.  Default "mic1" matches the
    // v0.2.0..v0.2.2 ship behaviour.  Persisted: tx/mic_source.
    QString micSource_   = QStringLiteral("mic1");
    bool    tooltipsEnabled_ = true;   // Settings → Visuals; ui/tooltips_enabled
};

} // namespace lyra::ui

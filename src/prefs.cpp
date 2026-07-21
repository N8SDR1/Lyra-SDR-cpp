// Lyra — shared UI preferences.  See prefs.h.

#include "prefs.h"

#include "palettes.h"
#include "grid.h"
#include "rig/RigScope.h"   // multi-rig Stage 4d — per-rig panadapter dB-scale keys
#include "win_perf.h"       // process-priority apply (Settings → Hardware → Performance)

#include <QSettings>

#include <algorithm>
#include <cmath>
#include <limits>

namespace lyra::ui {

namespace {
// Multi-rig Stage 4d: per-rig scope for the panadapter dB-SCALE keys only
// (RX+TX spectrum & waterfall floor/ceiling).  The rest of panadapter/ is
// UI prefs (palette, peak, glow, zoom…) that stay shared, so we wrap the
// specific scaling keys at their read/write sites, not the whole group.
// Returns rig/<activeId>/<flatKey> (or the flat key when no rig active).
QString scaledKey(const char *flatKey) {
    return lyra::rig::scope::rigKey(QLatin1String(flatKey));
}
constexpr auto kGrid   = "panadapter/gridLevel";
constexpr auto kFps    = "panadapter/targetFps";
constexpr auto kDbMin  = "panadapter/dbMin";
constexpr auto kDbMax  = "panadapter/dbMax";
// Task #44 Phase 1 — separate TX-state dB range pair so a drag
// during MOX persists per-state.  Defaults +20 / -80 dBFS frame
// a clean tune-carrier line at typical HL2 TX drive levels.
constexpr auto kTxDbMin = "panadapter/txDbMin";
constexpr auto kTxDbMax = "panadapter/txDbMax";
constexpr auto kDbAuto = "panadapter/dbAuto";
constexpr auto kTrace  = "panadapter/traceColor";
constexpr auto kMode   = "panadapter/traceMode";
constexpr auto kPal    = "panadapter/tracePalette";
constexpr auto kStr    = "panadapter/strengthColor";
constexpr auto kFill   = "panadapter/fillEnabled";
constexpr auto kFillCol = "panadapter/fillColor";
constexpr auto kSmooth = "panadapter/smoothing";
constexpr auto kGlow   = "panadapter/glow";
constexpr auto kSheen  = "panadapter/glassSheen";
constexpr auto kPkEn    = "panadapter/peakEnabled";
constexpr auto kPkHold  = "panadapter/peakHoldSecs";
constexpr auto kPkDecay = "panadapter/peakDecayDbps";
constexpr auto kPkStyle = "panadapter/peakStyle";
constexpr auto kPkColor = "panadapter/peakColor";
constexpr auto kCwDecColor = "cw/decodeColor";
constexpr auto kCwDecFont  = "cw/decodeFontSize";
constexpr auto kCwDecBw     = "cw/decodeBandwidth";
constexpr auto kCwDecSpeed  = "cw/decodeSpeed";
constexpr auto kCwDecTrack  = "cw/decodeTracking";
constexpr auto kCwDecMfilt  = "cw/decodeMatchedFilter";
constexpr auto kCwDecSqlOn  = "cw/decodeSquelchOn";
constexpr auto kCwDecSqlVal = "cw/decodeSquelchValue";
constexpr auto kPkShow  = "panadapter/peakShowDb";
constexpr auto kNfEn    = "panadapter/noiseFloorEnabled";
constexpr auto kNfColor = "panadapter/noiseFloorColor";
constexpr auto kWmark  = "panadapter/watermark";
constexpr auto kMet    = "panadapter/meteors";
constexpr auto kMetGap = "panadapter/meteorGap";
constexpr auto kMetGold = "panadapter/meteorGold";
constexpr auto kWfPal  = "panadapter/waterfallPalette";
constexpr auto kWfCol  = "panadapter/waterfallColor";
constexpr auto kWfSpd  = "panadapter/waterfallSpeed";
constexpr auto kWfDbMin = "panadapter/waterfallDbMin";
constexpr auto kWfDbMax = "panadapter/waterfallDbMax";
// §15.30 — separate TX-state waterfall dB range pair so a drag
// during MOX persists per-state.  Defaults +30 / -70 dBFS match
// the reference TX waterfall Low / High Level for HL2 TX drive
// levels (operator can re-tune in Settings → Visuals).
constexpr auto kTxWfDbMin = "panadapter/txWaterfallDbMin";
constexpr auto kTxWfDbMax = "panadapter/txWaterfallDbMax";
constexpr auto kWfDbAuto = "panadapter/waterfallDbAuto";
constexpr auto kPanSplit = "ui/panadapterSplit";
constexpr auto kCursorRdt = "panadapter/cursorReadout";
constexpr auto kZeroBeat = "visuals/zeroBeatMarkers";
constexpr auto kDspGrouped = "visuals/dspPanelsGrouped";
constexpr auto kOptGrouped = "visuals/optionsPanelsGrouped";
constexpr auto kZoom   = "panadapter/zoom";
constexpr auto kRxMode = "modefilter/mode";
constexpr auto kBwPrefix = "modefilter/bw/";   // + <MODE>
// TX Component 8c — per-mode TX bandwidth + the lock flag.  Prefix
// mirrors kBwPrefix so a future Settings sweep can read both with
// one wildcard.
constexpr auto kTxBwPrefix = "modefilter/tx_bw/";   // + <MODE>
constexpr auto kBwLocked   = "modefilter/bw_locked";
constexpr auto kFilterLow  = "modefilter/filter_low_hz";   // Task #53
constexpr auto kWfCollapse = "panadapter/waterfallCollapsed";
constexpr auto kSampRate = "radio/sampleRate";
constexpr auto kOpCall  = "operator/callsign";
constexpr auto kOpGrid  = "operator/grid";
constexpr auto kOpLat   = "operator/lat_manual";
constexpr auto kOpLon   = "operator/lon_manual";
constexpr auto kBandRegion = "band_plan/region";
constexpr auto kBandCountry = "band_plan/country";
constexpr auto kBpSegs     = "band_plan/segments";
constexpr auto kBpLand     = "band_plan/landmarks";
constexpr auto kBpBeacons  = "band_plan/beacons";
constexpr auto kBpEdges    = "band_plan/edges";
constexpr auto kBpClassEdges = "band_plan/class_edges";
constexpr auto kBpTxWarn   = "band_plan/tx_warn";
constexpr auto kBpColorPfx = "band_plan/color_";   // + <kind>
constexpr auto kCbBand     = "bands/cb_enabled";
constexpr auto kPanStep    = "panadapter/scroll_step_hz";
constexpr auto kPanRound   = "panadapter/round_100hz";
constexpr auto kDebugLog   = "debug/logging";
// Task #36 — Hardware PTT input opt-in (default OFF per §10 Q#1).
constexpr auto kHwPttEnabled = "tx/hw_ptt_enabled";
// Task #157 — space-bar PTT opt-out (default ON / historical behaviour).
constexpr auto kSpaceBarPttEnabled = "tx/space_bar_ptt_enabled";
constexpr auto kAutoStartOnLaunch  = "hw/autoStartOnLaunch";
constexpr auto kProcessPriority    = "hw/processPriority";
constexpr auto kDigitalDriveEnabled = "tx/digitalDriveEnabled";
constexpr auto kDigitalDrivePct     = "tx/digitalDrivePct";
constexpr auto kMicSource    = "tx/mic_source";
constexpr auto kTooltipsEnabled = "ui/tooltips_enabled";
// Task #74 — TUN separate-drive toggle + value.  Operator-tuned in
// Settings → TX (toggle) and on TxPanel's inline tune-drive stepper.
constexpr auto kUseTuneDrive = "tx/use_tune_drive";   // legacy (#74); migrated
constexpr auto kTuneDriveMode = "tx/tune_drive_mode";  // #95: 0/1/2
constexpr auto kTuneDrivePct = "tx/tune_drive_pct";
constexpr auto kFixedTuneDrive = "tx/fixed_tune_drive_pct";  // #95
// Task #75 — TCI RX-out gain in dB.  Default 0.0 = unity (byte-
// identical to pre-#75 behaviour).  Operator-tuned in Settings →
// TCI server group; clients see the gain change at the next emitted
// audio packet boundary (~43 ms at 48 kHz / 2048-sample frames).
constexpr auto kTciRxGainDb  = "tci/rx_gain_db";
// Task #108 — symmetric inbound (MSHV/JTDX/WSJT-X → Lyra TXA) gain.
constexpr auto kTciTxGainDb  = "tci/tx_gain_db";
constexpr auto kWfIdLevel       = "tx/wf_id_level";       // #175 (0..0.065)
constexpr auto kWfIdIntervalMin = "tx/wf_id_interval_min"; // #175 (0..20; enable is non-persist)
// Segment-kind default colours (mirror band_plan.py SEGMENT_COLORS).
const QHash<QString, QString> kBpDefaultColors = {
    {QStringLiteral("CW"),  QStringLiteral("#3c5a9c")},
    {QStringLiteral("DIG"), QStringLiteral("#9c3c9c")},
    {QStringLiteral("SSB"), QStringLiteral("#3c9c6a")},
    {QStringLiteral("FM"),  QStringLiteral("#c47a2a")},
    {QStringLiteral("MIX"), QStringLiteral("#5c8caa")},
    {QStringLiteral("BC"),  QStringLiteral("#7a7a3a")},
};
const QStringList kRegions = {
    QStringLiteral("US"), QStringLiteral("IARU_R1"),
    QStringLiteral("IARU_R3"), QStringLiteral("NONE"),
};
// Optional country override (layered on the region base table for bands
// that deviate from the IARU region, e.g. 60m).  "AUTO" = region only.
const QStringList kCountries = {
    QStringLiteral("AUTO"), QStringLiteral("UK"), QStringLiteral("CA"),
};
// Modes whose per-mode bandwidth we persist/restore.
const QStringList kModes = {
    QStringLiteral("LSB"),  QStringLiteral("USB"),
    QStringLiteral("CWL"),  QStringLiteral("CWU"),
    QStringLiteral("DSB"),  QStringLiteral("AM"),
    QStringLiteral("SAM"),  QStringLiteral("FM"),
    QStringLiteral("DIGU"), QStringLiteral("DIGL"),
};
} // namespace

Prefs::Prefs(QObject *parent) : QObject(parent) {
    QSettings s;
    gridLevel_  = std::clamp(s.value(kGrid, 35).toInt(), 0, 100);
    targetFps_  = std::clamp(s.value(kFps, 60).toInt(), 1, 240);
    rxDbMin_      = s.value(scaledKey(kDbMin), -130.0).toDouble();
    rxDbMax_      = s.value(scaledKey(kDbMax), -20.0).toDouble();
    txDbMin_    = s.value(scaledKey(kTxDbMin), -80.0).toDouble();
    txDbMax_    = s.value(scaledKey(kTxDbMax), 20.0).toDouble();
    // #95 TUN drive mode.  Migrate the legacy #74 bool when the new
    // key is absent: useTuneDrive ON → TuneDriveTune(1), OFF → slider(0).
    if (s.contains(kTuneDriveMode)) {
        tuneDriveMode_ = std::clamp(s.value(kTuneDriveMode, 0).toInt(), 0, 2);
    } else {
        tuneDriveMode_ = s.value(kUseTuneDrive, false).toBool() ? 1 : 0;
    }
    tuneDrivePct_ = std::clamp(
        s.value(kTuneDrivePct, 25).toInt(), 0, 100);
    fixedTuneDrivePct_ = std::clamp(
        s.value(kFixedTuneDrive, 25).toInt(), 0, 100);
    tciRxGainDb_  = std::clamp(
        s.value(kTciRxGainDb, 0.0).toDouble(), -40.0, 10.0);
    tciTxGainDb_  = std::clamp(
        s.value(kTciTxGainDb, 0.0).toDouble(), -40.0, 10.0);
    wfIdLevel_       = std::clamp(
        s.value(kWfIdLevel, 0.06).toDouble(), 0.0, 0.065);  // #175
    wfIdIntervalMin_ = std::clamp(
        s.value(kWfIdIntervalMin, 0).toInt(), 0, 20);       // #175
    // wfIdEnabled_ is NOT loaded — non-persistent, OFF every session (safety).
    dbAuto_     = s.value(kDbAuto, false).toBool();
    traceMode_  = std::clamp(s.value(kMode, 0).toInt(), 0, 1);
    traceColor_ = s.value(kTrace, QStringLiteral("#5ec8ff")).toString();
    palette_    = std::max(0, s.value(kPal, 0).toInt());
    strengthColor_ = s.value(kStr, QStringLiteral("#ff9b30")).toString();
    fillEnabled_ = s.value(kFill, true).toBool();
    fillColor_   = s.value(kFillCol, QStringLiteral("#5ec8ff")).toString();
    smoothing_   = std::clamp(s.value(kSmooth, 0).toInt(), 0, 10);
    glow_        = std::clamp(s.value(kGlow, 40).toInt(), 0, 100);
    glassSheen_  = std::clamp(s.value(kSheen, 20).toInt(), 0, 100);
    peakEnabled_   = s.value(kPkEn, false).toBool();
    peakHoldSecs_  = s.value(kPkHold, 0.0).toDouble();   // 0 = Off
    peakDecayDbps_ = std::clamp(s.value(kPkDecay, 10.0).toDouble(), 1.0, 120.0);
    peakStyle_     = std::clamp(s.value(kPkStyle, 1).toInt(), 0, 2);  // 1 dots
    peakColor_     = s.value(kPkColor, QStringLiteral("#ffbe5a")).toString();
    cwDecodeColor_    = s.value(kCwDecColor, QStringLiteral("#39ff14")).toString();
    cwDecodeFontSize_ = std::clamp(s.value(kCwDecFont, 15).toInt(), 10, 32);
    // fldigi CW-receiver defaults (fldigi configuration.h): BW 150, speed 18,
    // tracking on, matched filter off, squelch off (+ metric threshold).
    cwDecodeBandwidth_     = std::clamp(s.value(kCwDecBw, 150).toInt(), 50, 3000);
    cwDecodeSpeed_         = std::clamp(s.value(kCwDecSpeed, 18).toInt(), 5, 50);
    cwDecodeTracking_      = s.value(kCwDecTrack, true).toBool();
    cwDecodeMatchedFilter_ = s.value(kCwDecMfilt, false).toBool();
    cwDecodeSquelchOn_     = s.value(kCwDecSqlOn, false).toBool();
    cwDecodeSquelchValue_  = std::clamp(s.value(kCwDecSqlVal, 5.0).toDouble(), 0.0, 100.0);
    peakShowDb_    = s.value(kPkShow, false).toBool();
    noiseFloorEnabled_ = s.value(kNfEn, true).toBool();
    noiseFloorColor_   = s.value(kNfColor, QStringLiteral("#78c88c")).toString();
    watermark_   = s.value(kWmark, true).toBool();
    meteors_     = s.value(kMet, true).toBool();
    meteorGap_   = std::clamp(s.value(kMetGap, 30).toInt(), 5, 120);
    meteorGold_  = std::clamp(s.value(kMetGold, 15).toInt(), 0, 100);
    waterfallPalette_ = std::max(0, s.value(kWfPal, 0).toInt());
    waterfallColor_   = s.value(kWfCol, QStringLiteral("#30b0ff")).toString();
    waterfallSpeed_   = std::clamp(s.value(kWfSpd, 20).toInt(), 1, 120);
    // §15.30 — keep the operator's existing kWfDbMin/kWfDbMax values
    // (their tuned RX waterfall) on the SAME keys for byte-identical
    // upgrade; the new TX pair lives on kTxWfDbMin/kTxWfDbMax with
    // reference-faithful defaults +30 / -70 dBFS.
    rxWaterfallDbMin_ = s.value(scaledKey(kWfDbMin), -120.0).toDouble();
    rxWaterfallDbMax_ = s.value(scaledKey(kWfDbMax),  -20.0).toDouble();
    txWaterfallDbMin_ = s.value(scaledKey(kTxWfDbMin), -70.0).toDouble();
    txWaterfallDbMax_ = s.value(scaledKey(kTxWfDbMax),  30.0).toDouble();
    waterfallDbAuto_  = s.value(kWfDbAuto, false).toBool();
    panadapterSplit_  = s.value(kPanSplit);   // invalid (= QML undefined) if unset
    cursorReadout_    = s.value(kCursorRdt, true).toBool();
    zeroBeatMarkers_  = s.value(kZeroBeat, false).toBool();
    dspPanelsGrouped_     = s.value(kDspGrouped, false).toBool();
    optionsPanelsGrouped_ = s.value(kOptGrouped, false).toBool();
    zoom_             = std::clamp(s.value(kZoom, 1.0).toDouble(), 1.0, 32.0);
    mode_             = s.value(kRxMode, QStringLiteral("USB")).toString();
    // Per-FAMILY RX bandwidth (bwFamilyKey): USB/LSB share "SSB", etc.
    // Load the family key; if absent, migrate a legacy per-exact-mode
    // value (older installs stored "<prefix>USB" / "LSB" / …).  Families
    // with no stored value fall back to defaultBandwidthFor() on read.
    for (const QString &m : kModes) {
        const QString fam = bwFamilyKey(m);
        if (bwByMode_.contains(fam)) continue;            // family already set
        QVariant v = s.value(QString(kBwPrefix) + fam);   // new family key
        if (!v.isValid()) v = s.value(QString(kBwPrefix) + m);  // legacy exact-mode
        if (v.isValid()) bwByMode_.insert(fam, v.toInt());
    }
    // TX Component 8c — per-family TX bandwidth, same family-collapse +
    // legacy migration as RX BW.
    for (const QString &m : kModes) {
        const QString fam = bwFamilyKey(m);
        if (txBwByMode_.contains(fam)) continue;
        QVariant v = s.value(QString(kTxBwPrefix) + fam);
        if (!v.isValid()) v = s.value(QString(kTxBwPrefix) + m);
        if (v.isValid()) txBwByMode_.insert(fam, v.toInt());
    }
    bwLocked_ = s.value(kBwLocked, false).toBool();
    // Task #53 — shared RX+TX filter low edge.  Clamp on load
    // matches the setter's clamp so a manually-edited QSettings
    // value can't break later setter equality checks.
    filterLow_ = std::clamp(s.value(kFilterLow, 100).toInt(), 0, 500);
    waterfallCollapsed_ = s.value(kWfCollapse, false).toBool();
    callsign_   = s.value(kOpCall, QString()).toString();
    gridSquare_ = lyra::ham::normalizeGrid(s.value(kOpGrid).toString());
    {
        constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
        const QVariant la = s.value(kOpLat), lo = s.value(kOpLon);
        manualLat_ = la.isValid() ? la.toDouble() : kNaN;
        manualLon_ = lo.isValid() ? lo.toDouble() : kNaN;
    }
    bandPlanRegion_ = s.value(kBandRegion, QStringLiteral("US")).toString();
    if (!kRegions.contains(bandPlanRegion_)) bandPlanRegion_ = QStringLiteral("US");
    bandPlanCountry_ = s.value(kBandCountry, QStringLiteral("AUTO")).toString();
    if (!kCountries.contains(bandPlanCountry_))
        bandPlanCountry_ = QStringLiteral("AUTO");
    bandPlanSegments_  = s.value(kBpSegs, true).toBool();
    bandPlanLandmarks_ = s.value(kBpLand, true).toBool();
    bandPlanBeacons_   = s.value(kBpBeacons, true).toBool();
    bandPlanEdges_     = s.value(kBpEdges, true).toBool();
    bandPlanClassEdges_ = s.value(kBpClassEdges, false).toBool();
    bandPlanTxWarn_    = s.value(kBpTxWarn, true).toBool();
    for (auto it = kBpDefaultColors.cbegin(); it != kBpDefaultColors.cend(); ++it) {
        const QVariant v = s.value(QString(kBpColorPfx) + it.key());
        if (v.isValid() && !v.toString().isEmpty())
            bandPlanColors_.insert(it.key(), v.toString());
    }
    cbBandEnabled_ = s.value(kCbBand, false).toBool();
    panScrollStepHz_ = s.value(kPanStep, 1000).toInt();
    panRound100_ = s.value(kPanRound, false).toBool();
    debugLogging_ = s.value(kDebugLog, false).toBool();
    // Task #36 — HW PTT opt-in.  Default false (operator must explicitly
    // enable AFTER bench-verifying ptt_in is clean at RX rest on their
    // HL2 unit; see Settings → TX → Advanced tooltip).
    hwPttEnabled_ = s.value(kHwPttEnabled, false).toBool();
    // Task #157 — space-bar PTT.  Default true preserves the historical
    // always-on space-bar keying for existing operators.
    spaceBarPttEnabled_ = s.value(kSpaceBarPttEnabled, true).toBool();
    tooltipsEnabled_    = s.value(kTooltipsEnabled, true).toBool();
    autoStartOnLaunch_  = s.value(kAutoStartOnLaunch, true).toBool();
    // Process priority (per-PC).  Clamp to the valid 0..2 range, then apply
    // to the running process so the persisted choice takes effect at launch.
    processPriority_ = s.value(kProcessPriority, 0).toInt();
    if (processPriority_ < 0 || processPriority_ > 2) processPriority_ = 0;
    lyra::perf::applyProcessPriority(processPriority_);
    // Digital-mode TX-drive reduction (opt-in, default off; the pct is the
    // fraction of the band's set drive to run in DIGU/DIGL).  The wire-side
    // apply is pushed to HL2Stream by main.cpp on startup + on change.
    digitalDriveEnabled_ = s.value(kDigitalDriveEnabled, false).toBool();
    digitalDrivePct_ = s.value(kDigitalDrivePct, 100).toInt();
    if (digitalDrivePct_ < 10 || digitalDrivePct_ > 100) digitalDrivePct_ = 100;
    // Task #33 — TX mic source token.  Validate against the known
    // token list; an unknown value (older Lyra, mistyped QSettings)
    // falls back to "mic1" so we never autoload into an inactive
    // source path.
    {
        const QString tok = s.value(kMicSource, QStringLiteral("mic1")).toString();
        micSource_ = micSourceTokens().contains(tok) ? tok
                                                     : QStringLiteral("mic1");
    }
    sampleRate_ = s.value(kSampRate, 192000).toInt();
    if (sampleRate_ != 96000 && sampleRate_ != 192000 && sampleRate_ != 384000)
        sampleRate_ = 192000;
}

int Prefs::defaultBandwidthFor(const QString &mode) {
    if (mode == QStringLiteral("CWL") || mode == QStringLiteral("CWU"))
        return 250;
    if (mode == QStringLiteral("DSB")) return 5000;
    if (mode == QStringLiteral("AM") || mode == QStringLiteral("SAM")) return 6000;
    if (mode == QStringLiteral("FM"))  return 10000;
    if (mode == QStringLiteral("DIGL") || mode == QStringLiteral("DIGU"))
        return 3000;
    return 2400;   // SSB (USB/LSB) + anything else
}

QString Prefs::bwFamilyKey(const QString &mode) {
    // Bandwidth is one value per mode FAMILY, not per exact sideband:
    // USB/LSB -> "SSB", CWU/CWL -> "CW", DIGU/DIGL -> "Digital".  So an
    // operator's "4 kHz SSB" is 4 kHz on BOTH sidebands and a USB<->LSB
    // flip changes nothing remembered.  Kept in sync with
    // ProfileManager::modeFamily.
    const QString m = mode.toUpper();
    if (m == QStringLiteral("USB") || m == QStringLiteral("LSB"))
        return QStringLiteral("SSB");
    if (m == QStringLiteral("CWU") || m == QStringLiteral("CWL"))
        return QStringLiteral("CW");
    if (m == QStringLiteral("DIGU") || m == QStringLiteral("DIGL"))
        return QStringLiteral("Digital");
    return mode;   // AM / SAM / DSB / FM (+ unknown) stand alone
}

QStringList Prefs::paletteNames() const {
    QStringList names = lyra::palettes::names();
    names << tr("Custom color…");   // index == preset count
    return names;
}

void Prefs::setGridLevel(int v) {
    v = std::clamp(v, 0, 100);
    if (v != gridLevel_) {
        gridLevel_ = v;
        QSettings().setValue(kGrid, v);
        emit gridLevelChanged();
    }
}

void Prefs::setTargetFps(int v) {
    v = std::clamp(v, 1, 240);
    if (v != targetFps_) {
        targetFps_ = v;
        QSettings().setValue(kFps, v);
        emit targetFpsChanged();
    }
}

void Prefs::setDbMin(double v) {
    // Task #44 Phase 1 — route by MOX state so a drag during MOX
    // updates the TX pair (sticks across band changes; recalls
    // independently of the RX pair).
    if (moxActive_) {
        if (v != txDbMin_) {
            txDbMin_ = v;
            QSettings().setValue(scaledKey(kTxDbMin), v);
            emit txDbMinChanged();
            emit dbMinChanged();
        }
    } else {
        if (v != rxDbMin_) {
            rxDbMin_ = v;
            QSettings().setValue(scaledKey(kDbMin), v);
            emit dbMinChanged();
        }
    }
}

void Prefs::setDbMax(double v) {
    if (moxActive_) {
        if (v != txDbMax_) {
            txDbMax_ = v;
            QSettings().setValue(scaledKey(kTxDbMax), v);
            emit txDbMaxChanged();
            emit dbMaxChanged();
        }
    } else {
        if (v != rxDbMax_) {
            rxDbMax_ = v;
            QSettings().setValue(scaledKey(kDbMax), v);
            emit dbMaxChanged();
        }
    }
}

void Prefs::setRxDbMin(double v) {
    // Direct RX-pair writer for band-memory recall, so a recall
    // during MOX always updates the RX backing (takes effect on
    // MOX-off) rather than the live TX pair.
    if (v != rxDbMin_) {
        rxDbMin_ = v;
        QSettings().setValue(scaledKey(kDbMin), v);
        if (!moxActive_) emit dbMinChanged();
    }
}

void Prefs::setRxDbMax(double v) {
    if (v != rxDbMax_) {
        rxDbMax_ = v;
        QSettings().setValue(scaledKey(kDbMax), v);
        if (!moxActive_) emit dbMaxChanged();
    }
}

void Prefs::setTuneDriveMode(int v) {
    v = std::clamp(v, 0, 2);
    if (v == tuneDriveMode_) return;
    tuneDriveMode_ = v;
    QSettings().setValue(kTuneDriveMode, v);
    emit tuneDriveModeChanged();
}

void Prefs::setTuneDrivePct(int v) {
    v = std::clamp(v, 0, 100);
    if (v == tuneDrivePct_) return;
    tuneDrivePct_ = v;
    QSettings().setValue(kTuneDrivePct, v);
    emit tuneDrivePctChanged();
}

void Prefs::setFixedTuneDrivePct(int v) {
    v = std::clamp(v, 0, 100);
    if (v == fixedTuneDrivePct_) return;
    fixedTuneDrivePct_ = v;
    QSettings().setValue(kFixedTuneDrive, v);
    emit fixedTuneDrivePctChanged();
}

void Prefs::setTciRxGainDb(double v) {
    v = std::clamp(v, -40.0, 10.0);
    // Avoid spurious change ripples on identical-value sets — the
    // TciServer caches the linear value on the signal so a no-op
    // emit would recompute std::pow for nothing.
    if (v == tciRxGainDb_) return;
    tciRxGainDb_ = v;
    QSettings().setValue(kTciRxGainDb, v);
    emit tciRxGainDbChanged();
}

void Prefs::setTciTxGainDb(double v) {
    // Task #108 — same pattern as setTciRxGainDb.  Range -40..+10 dB,
    // skip no-op emits so TciServer's cached linear multiplier doesn't
    // recompute std::pow for identical-value drags.
    v = std::clamp(v, -40.0, 10.0);
    if (v == tciTxGainDb_) return;
    tciTxGainDb_ = v;
    QSettings().setValue(kTciTxGainDb, v);
    emit tciTxGainDbChanged();
}

void Prefs::setWfIdLevel(double v) {
    // #175 — clamped 0..0.065: a waterfall ID is full-duty multitone, so a hot
    // level = full-power-digital splatter + solid-state-amp stress.  0.065 is
    // the ceiling (operator bench: 0.08 was still too hot); persisted.
    v = std::clamp(v, 0.0, 0.065);
    if (v == wfIdLevel_) return;
    wfIdLevel_ = v;
    QSettings().setValue(kWfIdLevel, v);
    emit wfIdLevelChanged();
}

void Prefs::setWfIdEnabled(bool on) {
    // #175 — NON-persistent (no QSettings write): the auto-ID re-arms OFF
    // every session, so it can never key on a band you haven't tuned up on.
    if (on == wfIdEnabled_) return;
    wfIdEnabled_ = on;
    emit wfIdEnabledChanged();
}

void Prefs::setWfIdIntervalMin(int m) {
    // #175 — auto-cadence in minutes (0 = once on arm); persisted.
    m = std::clamp(m, 0, 20);
    if (m == wfIdIntervalMin_) return;
    wfIdIntervalMin_ = m;
    QSettings().setValue(kWfIdIntervalMin, m);
    emit wfIdIntervalMinChanged();
}

void Prefs::setMoxActive(bool on) {
    // Wired to Stream::moxActiveChanged in main.cpp.  The flag flip
    // changes which pair (rx vs tx) the dbMin/dbMax + waterfallDb
    // Min/Max accessors return; emitting both *Changed signals
    // causes QML's bindings to re-evaluate, so the panadapter AND
    // the waterfall render the swapped pairs without any extra
    // wiring (§15.30 added the waterfall pair to the swap).
    if (on == moxActive_) return;
    moxActive_ = on;
    emit dbMinChanged();
    emit dbMaxChanged();
    emit waterfallDbMinChanged();
    emit waterfallDbMaxChanged();
}

void Prefs::setTxDbMin(double v) {
    if (v != txDbMin_) {
        txDbMin_ = v;
        QSettings().setValue(scaledKey(kTxDbMin), v);
        emit txDbMinChanged();
        if (moxActive_) emit dbMinChanged();
    }
}

void Prefs::setTxDbMax(double v) {
    if (v != txDbMax_) {
        txDbMax_ = v;
        QSettings().setValue(scaledKey(kTxDbMax), v);
        emit txDbMaxChanged();
        if (moxActive_) emit dbMaxChanged();
    }
}

void Prefs::setDbAuto(bool v) {
    if (v != dbAuto_) {
        dbAuto_ = v;
        QSettings().setValue(kDbAuto, v);
        emit dbAutoChanged();
    }
}

void Prefs::setTraceMode(int v) {
    v = std::clamp(v, 0, 1);
    if (v != traceMode_) {
        traceMode_ = v;
        QSettings().setValue(kMode, v);
        emit traceModeChanged();
    }
}

void Prefs::setTraceColor(const QString &hex) {
    if (hex != traceColor_ && !hex.isEmpty()) {
        traceColor_ = hex;
        QSettings().setValue(kTrace, hex);
        emit traceColorChanged();
    }
}

void Prefs::setPalette(int v) {
    if (v < 0) v = 0;
    if (v != palette_) {
        palette_ = v;
        QSettings().setValue(kPal, v);
        emit paletteChanged();
    }
}

void Prefs::setCwDecodeColor(const QString &hex) {
    if (hex != cwDecodeColor_ && !hex.isEmpty()) {
        cwDecodeColor_ = hex;
        QSettings().setValue(kCwDecColor, hex);
        emit cwDecodeColorChanged();
    }
}

void Prefs::setCwDecodeFontSize(int px) {
    px = std::clamp(px, 10, 32);
    if (px != cwDecodeFontSize_) {
        cwDecodeFontSize_ = px;
        QSettings().setValue(kCwDecFont, px);
        emit cwDecodeFontSizeChanged();
    }
}

void Prefs::setCwDecodeBandwidth(int hz) {
    hz = std::clamp(hz, 50, 3000);
    if (hz != cwDecodeBandwidth_) {
        cwDecodeBandwidth_ = hz;
        QSettings().setValue(kCwDecBw, hz);
        emit cwDecodeBandwidthChanged();
    }
}

void Prefs::setCwDecodeSpeed(int wpm) {
    wpm = std::clamp(wpm, 5, 50);
    if (wpm != cwDecodeSpeed_) {
        cwDecodeSpeed_ = wpm;
        QSettings().setValue(kCwDecSpeed, wpm);
        emit cwDecodeSpeedChanged();
    }
}

void Prefs::setCwDecodeTracking(bool on) {
    if (on != cwDecodeTracking_) {
        cwDecodeTracking_ = on;
        QSettings().setValue(kCwDecTrack, on);
        emit cwDecodeTrackingChanged();
    }
}

void Prefs::setCwDecodeMatchedFilter(bool on) {
    if (on != cwDecodeMatchedFilter_) {
        cwDecodeMatchedFilter_ = on;
        QSettings().setValue(kCwDecMfilt, on);
        emit cwDecodeMatchedFilterChanged();
    }
}

void Prefs::setCwDecodeSquelchOn(bool on) {
    if (on != cwDecodeSquelchOn_) {
        cwDecodeSquelchOn_ = on;
        QSettings().setValue(kCwDecSqlOn, on);
        emit cwDecodeSquelchOnChanged();
    }
}

void Prefs::setCwDecodeSquelchValue(double v) {
    v = std::clamp(v, 0.0, 100.0);
    if (v != cwDecodeSquelchValue_) {
        cwDecodeSquelchValue_ = v;
        QSettings().setValue(kCwDecSqlVal, v);
        emit cwDecodeSquelchValueChanged();
    }
}

void Prefs::setStrengthColor(const QString &hex) {
    if (hex != strengthColor_ && !hex.isEmpty()) {
        strengthColor_ = hex;
        QSettings().setValue(kStr, hex);
        emit strengthColorChanged();
    }
}

void Prefs::setFillEnabled(bool v) {
    if (v != fillEnabled_) {
        fillEnabled_ = v;
        QSettings().setValue(kFill, v);
        emit fillEnabledChanged();
    }
}

void Prefs::setFillColor(const QString &hex) {
    if (hex != fillColor_ && !hex.isEmpty()) {
        fillColor_ = hex;
        QSettings().setValue(kFillCol, hex);
        emit fillColorChanged();
    }
}

void Prefs::setSmoothing(int v) {
    v = std::clamp(v, 0, 10);
    if (v != smoothing_) {
        smoothing_ = v;
        QSettings().setValue(kSmooth, v);
        emit smoothingChanged();
    }
}

void Prefs::setGlow(int v) {
    v = std::clamp(v, 0, 100);
    if (v != glow_) {
        glow_ = v;
        QSettings().setValue(kGlow, v);
        emit glowChanged();
    }
}

void Prefs::setGlassSheen(int v) {
    v = std::clamp(v, 0, 100);
    if (v != glassSheen_) {
        glassSheen_ = v;
        QSettings().setValue(kSheen, v);
        emit glassSheenChanged();
    }
}

void Prefs::setPeakEnabled(bool v) {
    if (v != peakEnabled_) {
        peakEnabled_ = v;
        QSettings().setValue(kPkEn, v);
        emit peakEnabledChanged();
    }
}

void Prefs::setPeakHoldSecs(double v) {
    if (v != peakHoldSecs_) {
        peakHoldSecs_ = v;
        QSettings().setValue(kPkHold, v);
        emit peakHoldSecsChanged();
    }
}

void Prefs::setPeakDecayDbps(double v) {
    v = std::clamp(v, 1.0, 120.0);
    if (v != peakDecayDbps_) {
        peakDecayDbps_ = v;
        QSettings().setValue(kPkDecay, v);
        emit peakDecayDbpsChanged();
    }
}

void Prefs::setPeakStyle(int v) {
    v = std::clamp(v, 0, 2);
    if (v != peakStyle_) {
        peakStyle_ = v;
        QSettings().setValue(kPkStyle, v);
        emit peakStyleChanged();
    }
}

void Prefs::setPeakColor(const QString &hex) {
    if (hex != peakColor_ && !hex.isEmpty()) {
        peakColor_ = hex;
        QSettings().setValue(kPkColor, hex);
        emit peakColorChanged();
    }
}

void Prefs::setPeakShowDb(bool v) {
    if (v != peakShowDb_) {
        peakShowDb_ = v;
        QSettings().setValue(kPkShow, v);
        emit peakShowDbChanged();
    }
}

void Prefs::setNoiseFloorEnabled(bool v) {
    if (v != noiseFloorEnabled_) {
        noiseFloorEnabled_ = v;
        QSettings().setValue(kNfEn, v);
        emit noiseFloorEnabledChanged();
    }
}

void Prefs::setNoiseFloorColor(const QString &hex) {
    if (hex != noiseFloorColor_ && !hex.isEmpty()) {
        noiseFloorColor_ = hex;
        QSettings().setValue(kNfColor, hex);
        emit noiseFloorColorChanged();
    }
}

void Prefs::setWatermark(bool v) {
    if (v != watermark_) {
        watermark_ = v;
        QSettings().setValue(kWmark, v);
        emit watermarkChanged();
    }
}

void Prefs::setMeteors(bool v) {
    if (v != meteors_) {
        meteors_ = v;
        QSettings().setValue(kMet, v);
        emit meteorsChanged();
    }
}

void Prefs::setMeteorGap(int v) {
    v = std::clamp(v, 5, 120);
    if (v != meteorGap_) {
        meteorGap_ = v;
        QSettings().setValue(kMetGap, v);
        emit meteorGapChanged();
    }
}

void Prefs::setMeteorGold(int v) {
    v = std::clamp(v, 0, 100);
    if (v != meteorGold_) {
        meteorGold_ = v;
        QSettings().setValue(kMetGold, v);
        emit meteorGoldChanged();
    }
}

void Prefs::setWaterfallPalette(int v) {
    if (v < 0) v = 0;
    if (v != waterfallPalette_) {
        waterfallPalette_ = v;
        QSettings().setValue(kWfPal, v);
        emit waterfallPaletteChanged();
    }
}

void Prefs::setWaterfallColor(const QString &hex) {
    if (hex != waterfallColor_ && !hex.isEmpty()) {
        waterfallColor_ = hex;
        QSettings().setValue(kWfCol, hex);
        emit waterfallColorChanged();
    }
}

void Prefs::setWaterfallSpeed(int v) {
    v = std::clamp(v, 1, 120);
    if (v != waterfallSpeed_) {
        waterfallSpeed_ = v;
        QSettings().setValue(kWfSpd, v);
        emit waterfallSpeedChanged();
    }
}

void Prefs::setWaterfallDbMin(double v) {
    // §15.30 — route by MOX state, mirroring the Task #44 panadapter
    // pair: a drag during MOX updates the TX backing (sticks across
    // band changes; recalls independently of the RX backing).
    if (moxActive_) {
        if (v != txWaterfallDbMin_) {
            txWaterfallDbMin_ = v;
            QSettings().setValue(scaledKey(kTxWfDbMin), v);
            emit txWaterfallDbMinChanged();
            emit waterfallDbMinChanged();
        }
    } else {
        if (v != rxWaterfallDbMin_) {
            rxWaterfallDbMin_ = v;
            QSettings().setValue(scaledKey(kWfDbMin), v);
            emit waterfallDbMinChanged();
        }
    }
}

void Prefs::setWaterfallDbMax(double v) {
    if (moxActive_) {
        if (v != txWaterfallDbMax_) {
            txWaterfallDbMax_ = v;
            QSettings().setValue(scaledKey(kTxWfDbMax), v);
            emit txWaterfallDbMaxChanged();
            emit waterfallDbMaxChanged();
        }
    } else {
        if (v != rxWaterfallDbMax_) {
            rxWaterfallDbMax_ = v;
            QSettings().setValue(scaledKey(kWfDbMax), v);
            emit waterfallDbMaxChanged();
        }
    }
}

void Prefs::setRxWaterfallDbMin(double v) {
    // Direct RX-pair writer for per-band-memory recall, so a recall
    // during MOX always updates the RX backing (takes effect on
    // MOX-off) rather than the live TX pair.
    if (v != rxWaterfallDbMin_) {
        rxWaterfallDbMin_ = v;
        QSettings().setValue(scaledKey(kWfDbMin), v);
        if (!moxActive_) emit waterfallDbMinChanged();
    }
}

void Prefs::setRxWaterfallDbMax(double v) {
    if (v != rxWaterfallDbMax_) {
        rxWaterfallDbMax_ = v;
        QSettings().setValue(scaledKey(kWfDbMax), v);
        if (!moxActive_) emit waterfallDbMaxChanged();
    }
}

void Prefs::setTxWaterfallDbMin(double v) {
    if (v != txWaterfallDbMin_) {
        txWaterfallDbMin_ = v;
        QSettings().setValue(scaledKey(kTxWfDbMin), v);
        emit txWaterfallDbMinChanged();
        if (moxActive_) emit waterfallDbMinChanged();
    }
}

void Prefs::setTxWaterfallDbMax(double v) {
    if (v != txWaterfallDbMax_) {
        txWaterfallDbMax_ = v;
        QSettings().setValue(scaledKey(kTxWfDbMax), v);
        emit txWaterfallDbMaxChanged();
        if (moxActive_) emit waterfallDbMaxChanged();
    }
}

void Prefs::setWaterfallDbAuto(bool v) {
    if (v != waterfallDbAuto_) {
        waterfallDbAuto_ = v;
        QSettings().setValue(kWfDbAuto, v);
        emit waterfallDbAutoChanged();
    }
}

void Prefs::setCursorReadout(bool v) {
    if (v != cursorReadout_) {
        cursorReadout_ = v;
        QSettings().setValue(kCursorRdt, v);
        emit cursorReadoutChanged();
    }
}

void Prefs::setZeroBeatMarkers(bool v) {
    if (v != zeroBeatMarkers_) {
        zeroBeatMarkers_ = v;
        QSettings().setValue(kZeroBeat, v);
        emit zeroBeatMarkersChanged();
    }
}

void Prefs::setDspPanelsGrouped(bool v) {
    if (v != dspPanelsGrouped_) {
        dspPanelsGrouped_ = v;
        QSettings().setValue(kDspGrouped, v);
        emit dspPanelsGroupedChanged();
    }
}

void Prefs::setOptionsPanelsGrouped(bool v) {
    if (v != optionsPanelsGrouped_) {
        optionsPanelsGrouped_ = v;
        QSettings().setValue(kOptGrouped, v);
        emit optionsPanelsGroupedChanged();
    }
}

void Prefs::setZoom(double v) {
    v = std::clamp(v, 1.0, 32.0);
    if (v != zoom_) {
        zoom_ = v;
        QSettings().setValue(kZoom, v);
        emit zoomChanged();
    }
}

int Prefs::rxBandwidth() const {
    return bwByMode_.value(bwFamilyKey(mode_), defaultBandwidthFor(mode_));
}

void Prefs::setMode(const QString &m) {
    if (m.isEmpty() || m == mode_) {
        return;
    }
    mode_ = m;
    QSettings().setValue(kRxMode, m);
    emit modeChanged();
    // The effective bandwidth is per-mode, so it changes with the mode.
    emit rxBandwidthChanged();
    // TX Component 8c — TX BW is ALSO per-mode (separate dict); mode
    // flip changes the effective TX BW too.
    emit txBandwidthChanged();
}

void Prefs::setRxBandwidth(int hz) {
    if (hz <= 0 || hz == rxBandwidth()) {
        return;
    }
    const QString key = bwFamilyKey(mode_);   // per-family, not per-sideband
    bwByMode_.insert(key, hz);
    QSettings().setValue(QString(kBwPrefix) + key, hz);
    emit rxBandwidthChanged();
    // TX Component 8c — when the RX↔TX lock is on, mirror this change
    // to TX without recursing into setTxBandwidth (we update the dict
    // + persist + emit directly, then exit).  Same pattern in
    // setTxBandwidth() for the reverse direction.
    if (bwLocked_ &&
        txBwByMode_.value(key, defaultBandwidthFor(mode_)) != hz) {
        txBwByMode_.insert(key, hz);
        QSettings().setValue(QString(kTxBwPrefix) + key, hz);
        emit txBandwidthChanged();
    }
}

// TX Component 8c — per-mode TX bandwidth.  Reader mirrors rxBandwidth().
int Prefs::txBandwidth() const {
    return txBwByMode_.value(bwFamilyKey(mode_), defaultBandwidthFor(mode_));
}

void Prefs::setTxBandwidth(int hz) {
    if (hz <= 0 || hz == txBandwidth()) {
        return;
    }
    const QString key = bwFamilyKey(mode_);   // per-family, not per-sideband
    txBwByMode_.insert(key, hz);
    QSettings().setValue(QString(kTxBwPrefix) + key, hz);
    emit txBandwidthChanged();
    // Mirror to RX when locked.  Direct dict update — no recursion.
    if (bwLocked_ &&
        bwByMode_.value(key, defaultBandwidthFor(mode_)) != hz) {
        bwByMode_.insert(key, hz);
        QSettings().setValue(QString(kBwPrefix) + key, hz);
        emit rxBandwidthChanged();
    }
}

// Task #53 — shared RX+TX filter low edge.  Single global value
// applied to BOTH the WDSP RX bandpass (replaces hardcoded 0 in
// WdspEngine::computePassband for SSB modes) AND the HL2Stream
// TX bandpass (replaces hardcoded 200 in setTxBwHz).  Interim
// until TX Profile Manager (#49) ships per-profile (lo, hi)
// pairs.  Clamp 0..500 Hz — 0 = no low cut (some operators
// want it for ultra-wide ESSB into a clean RF environment);
// 500 = upper-bound sanity (above this would clip voice body).
void Prefs::setFilterLow(int hz) {
    hz = std::clamp(hz, 0, 500);
    if (hz == filterLow_) {
        return;
    }
    filterLow_ = hz;
    QSettings().setValue(kFilterLow, hz);
    emit filterLowChanged();
}

void Prefs::setBwLocked(bool v) {
    if (v == bwLocked_) {
        return;
    }
    bwLocked_ = v;
    QSettings().setValue(kBwLocked, v);
    emit bwLockedChanged();
    // Toggling ON pulls RX into TX for the current mode (matches old
    // Lyra's lock-on direction).  Toggling OFF just flips the flag —
    // the per-mode dicts retain their independent values so the
    // operator's pre-lock TX BW is preserved on un-lock.
    if (v) {
        const int rx = rxBandwidth();
        if (rx != txBandwidth()) {
            const QString key = bwFamilyKey(mode_);
            txBwByMode_.insert(key, rx);
            QSettings().setValue(QString(kTxBwPrefix) + key, rx);
            emit txBandwidthChanged();
        }
    }
}

void Prefs::setSampleRate(int hz) {
    if (hz != 96000 && hz != 192000 && hz != 384000) {
        return;
    }
    if (hz != sampleRate_) {
        sampleRate_ = hz;
        QSettings().setValue(kSampRate, hz);
        emit sampleRateChanged();
    }
}

void Prefs::setWaterfallCollapsed(bool v) {
    if (v != waterfallCollapsed_) {
        waterfallCollapsed_ = v;
        QSettings().setValue(kWfCollapse, v);
        emit waterfallCollapsedChanged();
    }
}

void Prefs::setPanadapterSplit(const QVariant &v) {
    // Always persist + notify (even to an invalid QVariant — that's how
    // "Reset to default layout" tells QML to restore the factory split).
    panadapterSplit_ = v;
    if (v.isValid()) {
        QSettings().setValue(kPanSplit, v);
    } else {
        QSettings().remove(kPanSplit);
    }
    emit panadapterSplitChanged();
}

void Prefs::setCallsign(const QString &c) {
    const QString v = c.trimmed().toUpper();   // no format validation (old Lyra)
    if (v != callsign_) {
        callsign_ = v;
        QSettings().setValue(kOpCall, v);
        emit callsignChanged();
    }
}

void Prefs::setGridSquare(const QString &g) {
    const QString v = lyra::ham::normalizeGrid(g);   // "" if invalid
    if (v != gridSquare_) {
        gridSquare_ = v;
        QSettings().setValue(kOpGrid, v);
        emit gridSquareChanged();
        emit locationChanged();   // effective lat/lon follows the grid
    }
}

void Prefs::setManualLatLon(double lat, double lon) {
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    QSettings s;
    if (std::isnan(lat) || std::isnan(lon)) {        // clear the override
        const bool had = !std::isnan(manualLat_) || !std::isnan(manualLon_);
        manualLat_ = kNaN;
        manualLon_ = kNaN;
        s.remove(kOpLat);
        s.remove(kOpLon);
        if (had) emit locationChanged();
        return;
    }
    lat = std::clamp(lat, -90.0, 90.0);
    lon = std::clamp(lon, -180.0, 180.0);
    if (lat != manualLat_ || lon != manualLon_) {
        manualLat_ = lat;
        manualLon_ = lon;
        s.setValue(kOpLat, lat);
        s.setValue(kOpLon, lon);
        emit locationChanged();
    }
}

void Prefs::setBandPlanRegion(const QString &r) {
    const QString v = kRegions.contains(r) ? r : QStringLiteral("US");
    if (v != bandPlanRegion_) {
        bandPlanRegion_ = v;
        QSettings().setValue(kBandRegion, v);
        emit bandPlanRegionChanged();
    }
}

void Prefs::setBandPlanCountry(const QString &c) {
    const QString v = kCountries.contains(c) ? c : QStringLiteral("AUTO");
    if (v != bandPlanCountry_) {
        bandPlanCountry_ = v;
        QSettings().setValue(kBandCountry, v);
        emit bandPlanCountryChanged();
    }
}

void Prefs::setBandPlanSegments(bool v) {
    if (v != bandPlanSegments_) {
        bandPlanSegments_ = v;
        QSettings().setValue(kBpSegs, v);
        emit bandPlanSegmentsChanged();
    }
}

void Prefs::setBandPlanLandmarks(bool v) {
    if (v != bandPlanLandmarks_) {
        bandPlanLandmarks_ = v;
        QSettings().setValue(kBpLand, v);
        emit bandPlanLandmarksChanged();
    }
}

void Prefs::setBandPlanBeacons(bool v) {
    if (v != bandPlanBeacons_) {
        bandPlanBeacons_ = v;
        QSettings().setValue(kBpBeacons, v);
        emit bandPlanBeaconsChanged();
    }
}

void Prefs::setBandPlanEdges(bool v) {
    if (v != bandPlanEdges_) {
        bandPlanEdges_ = v;
        QSettings().setValue(kBpEdges, v);
        emit bandPlanEdgesChanged();
    }
}

void Prefs::setBandPlanClassEdges(bool v) {
    if (v != bandPlanClassEdges_) {
        bandPlanClassEdges_ = v;
        QSettings().setValue(kBpClassEdges, v);
        emit bandPlanClassEdgesChanged();
    }
}

void Prefs::setBandPlanTxWarn(bool v) {
    if (v != bandPlanTxWarn_) {
        bandPlanTxWarn_ = v;
        QSettings().setValue(kBpTxWarn, v);
        emit bandPlanTxWarnChanged();
    }
}

QString Prefs::defaultBandPlanColor(const QString &kind) {
    return kBpDefaultColors.value(kind, QStringLiteral("#5c8caa"));
}

QString Prefs::bandPlanColor(const QString &kind) const {
    return bandPlanColors_.value(kind, defaultBandPlanColor(kind));
}

void Prefs::setBandPlanColor(const QString &kind, const QString &hex) {
    const QString def = defaultBandPlanColor(kind);
    QSettings s;
    if (hex.isEmpty() || hex.compare(def, Qt::CaseInsensitive) == 0) {
        // Back to default → drop the override.
        if (bandPlanColors_.remove(kind) > 0) {
            s.remove(QString(kBpColorPfx) + kind);
            emit bandPlanColorsChanged();
        }
        return;
    }
    if (bandPlanColors_.value(kind) != hex) {
        bandPlanColors_.insert(kind, hex);
        s.setValue(QString(kBpColorPfx) + kind, hex);
        emit bandPlanColorsChanged();
    }
}

void Prefs::setCbBandEnabled(bool v) {
    if (v != cbBandEnabled_) {
        cbBandEnabled_ = v;
        QSettings().setValue(kCbBand, v);
        emit cbBandEnabledChanged();
    }
}

void Prefs::setPanScrollStepHz(int hz) {
    if (hz > 0 && hz != panScrollStepHz_) {
        panScrollStepHz_ = hz;
        QSettings().setValue(kPanStep, hz);
        emit panScrollStepHzChanged();
    }
}

void Prefs::setPanRound100(bool v) {
    if (v != panRound100_) {
        panRound100_ = v;
        QSettings().setValue(kPanRound, v);
        emit panRound100Changed();
    }
}

void Prefs::setDebugLogging(bool on) {
    if (on != debugLogging_) {
        debugLogging_ = on;
        QSettings().setValue(kDebugLog, on);
        emit debugLoggingChanged();
    }
}

void Prefs::setHwPttEnabled(bool on) {
    if (on != hwPttEnabled_) {
        hwPttEnabled_ = on;
        QSettings().setValue(kHwPttEnabled, on);
        emit hwPttEnabledChanged();
    }
}

void Prefs::setSpaceBarPttEnabled(bool on) {
    if (on != spaceBarPttEnabled_) {
        spaceBarPttEnabled_ = on;
        QSettings().setValue(kSpaceBarPttEnabled, on);
        emit spaceBarPttEnabledChanged();
    }
}

void Prefs::setTooltipsEnabled(bool on) {
    if (on != tooltipsEnabled_) {
        tooltipsEnabled_ = on;
        QSettings().setValue(kTooltipsEnabled, on);
        emit tooltipsEnabledChanged();
    }
}

void Prefs::setAutoStartOnLaunch(bool on) {
    if (on != autoStartOnLaunch_) {
        autoStartOnLaunch_ = on;
        QSettings().setValue(kAutoStartOnLaunch, on);
        emit autoStartOnLaunchChanged();
    }
}

void Prefs::setProcessPriority(int level) {
    if (level < 0 || level > 2) level = 0;
    if (level != processPriority_) {
        processPriority_ = level;
        QSettings().setValue(kProcessPriority, level);
        lyra::perf::applyProcessPriority(level);   // live-apply, no restart
        emit processPriorityChanged();
    }
}

void Prefs::setDigitalDriveEnabled(bool on) {
    if (on != digitalDriveEnabled_) {
        digitalDriveEnabled_ = on;
        QSettings().setValue(kDigitalDriveEnabled, on);
        emit digitalDriveEnabledChanged();   // main.cpp forwards to HL2Stream
    }
}

void Prefs::setDigitalDrivePct(int pct) {
    if (pct < 10 || pct > 100) pct = std::clamp(pct, 10, 100);
    if (pct != digitalDrivePct_) {
        digitalDrivePct_ = pct;
        QSettings().setValue(kDigitalDrivePct, pct);
        emit digitalDrivePctChanged();
    }
}

// Task #33 — TX mic source.  Validates token against the known set
// (silently falls back to "mic1" on unknown), persists, emits the
// change signal.  main.cpp wires the signal to dispatch into
// TxDspWorker::setActiveMicSource (and TciServer if needed).
void Prefs::setMicSource(const QString &token) {
    QString t = micSourceTokens().contains(token) ? token
                                                  : QStringLiteral("mic1");
    if (t != micSource_) {
        micSource_ = t;
        QSettings().setValue(kMicSource, t);
        emit micSourceChanged();
    }
}

// The known TX mic-source tokens, in operator-picker order.  Matches
// the TCI v2 §3.3 TRX source-token enum (mic1 / micpc / micpc2 / tci) —
// so when a TCI client sends `trx:0,true,<tok>` the picker can adopt
// the same string directly.  The enum's `mic2` ("Line In") is omitted:
// the HL2+ AK4951 board has no separate line-in jack (the single MIC
// jack is the codec mic input) — see #104 (closed won't-do).
QStringList Prefs::micSourceTokens() {
    return { QStringLiteral("mic1"),
             QStringLiteral("tci"),
             QStringLiteral("micpc"),
             QStringLiteral("micpc2") };
}

// Operator-visible label for the Settings → TX → Mic Source dropdown.
QString Prefs::micSourceLabel(const QString &token) {
    if (token == QLatin1String("mic1"))   return QStringLiteral("Mic In");
    if (token == QLatin1String("tci"))    return QStringLiteral("TCI (digital modes)");
    if (token == QLatin1String("micpc"))  return QStringLiteral("PC Soundcard (VAC1)");
    if (token == QLatin1String("micpc2")) return QStringLiteral("VAC2");
    return token;
}

// Which entries are operator-selectable in v0.2.x.  Disabled entries
// still render in the dropdown but with their tooltip explaining the
// future-version status — same no-inert-UI precedent as the v0.2.0
// Mode+Filter picker's COMP/ALC/MIC disabled-but-visible items.
bool Prefs::micSourceEnabled(const QString &token) {
    if (token == QLatin1String("mic1"))  return true;
    if (token == QLatin1String("tci"))   return true;
    if (token == QLatin1String("micpc")) return true;   // #158 Stage 4 — VAC1 in
    return false;   // micpc2 (VAC2) — future v0.2.x
}

QString Prefs::micSourceTooltip(const QString &token) {
    if (token == QLatin1String("mic1"))
        return QStringLiteral("HL2/HL2+ codec mic input — the default operator path.");
    if (token == QLatin1String("tci"))
        return QStringLiteral(
            "Inbound TCI v2 TX_AUDIO_STREAM from a digital-modes client "
            "(MSHV, JTDX, FlDigi, etc.).  Pick this when running an FT8 / "
            "FT4 / Q65 / MSK144 / WSPR session — Lyra acts as the radio, "
            "the digital app sends audio over TCI, your mic is bypassed.");
    if (token == QLatin1String("micpc"))
        return QStringLiteral(
            "PC audio in via VAC1 — a virtual cable from a digital app "
            "(MSHV / WSJT-X) or a USB microphone for voice.  Set the VAC1 "
            "Input device + TX gain in Settings → Audio; this routes that "
            "captured audio to the transmitter (your codec mic is bypassed).");
    if (token == QLatin1String("micpc2"))
        return QStringLiteral(
            "Second host PC audio capture device (VAC2) — pending v0.2.x.");
    return QString();
}

bool Prefs::operatorLocation(double *lat, double *lon) const {
    if (const auto ll = lyra::ham::gridToLatLon(gridSquare_)) {
        if (lat) *lat = ll->first;
        if (lon) *lon = ll->second;
        return true;
    }
    if (!std::isnan(manualLat_) && !std::isnan(manualLon_)) {
        if (lat) *lat = manualLat_;
        if (lon) *lon = manualLon_;
        return true;
    }
    return false;
}

} // namespace lyra::ui

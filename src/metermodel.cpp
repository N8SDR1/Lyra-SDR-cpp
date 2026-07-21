// Lyra — RX signal-strength meter model.  See metermodel.h.
//
// Calibration follows the standard HF SDR pattern: the source is
// WDSP's in-passband RXA_S_PK meter (WdspEngine::sMeterDbm), an
// operator trim (calDb) stands in for the per-radio meter-cal
// offset (+ the fixed HL2 LNA offset, until an LNA-gain control
// is ported), and dBm→S-units uses the standard HF dBm→S-unit
// table with the 30 MHz HF/VHF split (S9 = -73 dBm below 30 MHz,
// -93 dBm above).

#include "metermodel.h"

#include "hl2_stream.h"
#include "rig/RigScope.h"   // multi-rig Stage 4c — per-rig key routing (meter cal)
// TX-rip Phase 1 (Q2): tx_dsp_worker.h removed — TX DSP worker is being
// rebuilt from empty files per docs/TX_ARCHITECTURAL_MAPPING.md §10.3.
// TX meter taps (MIC / LVL / ALC cases below) return their "—"
// placeholder until the new worker lands.
#include "wdsp_engine.h"

#include <QSettings>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>

namespace lyra::ui {

namespace {
constexpr double kDbPerS  = 6.0;
constexpr double kS9Hf    = -73.0;   // S9 below 30 MHz
constexpr double kS9Vhf   = -93.0;   // S9 at/above 30 MHz
// Scale endpoints (S1 floor … S9+60 ceiling) for each regime.
constexpr double kFloorHf = -124.0, kCeilHf = -13.0;
constexpr double kFloorVhf = -144.0, kCeilVhf = -33.0;
constexpr double kS9FreqHz = 30.0e6;

constexpr int    kTickMs        = 50;     // 20 Hz
constexpr int    kPeakHoldTicks = 16;     // ~0.8 s dwell
constexpr double kPeakDecay     = 0.080;  // ~0.6 s fall
constexpr double kMaxDecay      = 0.012;  // max-hold gentle fall (~4 s)
constexpr double kGlowDecay     = 0.160;  // ~0.3 s afterglow
constexpr double kNoiseLeak     = 0.05;   // floor leaks up ~1 dB/s
constexpr double kSmooth        = 0.55;
constexpr int    kHistory       = 90;     // ~4.5 s trail

constexpr auto kKeyStyle    = "meter/style";
constexpr auto kKeyTxStyle  = "meter/txStyle";     // TX visual style
constexpr auto kKeySepStyle = "meter/separateStyle"; // swap style on MOX
constexpr auto kKeyCal      = "meter/calDb";
constexpr auto kKeyPeakHold    = "meter/peakHoldMs";
constexpr auto kKeyPwrPeakHold = "meter/pwrPeakHoldMs";
constexpr auto kKeyPwrBallistic = "meter/pwrBallistic";   // PEP/PEAK/AVG
constexpr auto kKeyMaxOn     = "meter/maxPeakEnabled";
constexpr auto kKeyMaxHold   = "meter/maxHoldMs";
constexpr auto kKeyRxSource  = "meter/rxSource"; // operator's RX-state preference
constexpr auto kKeyTxSource  = "meter/txSource"; // operator's TX-state preference
constexpr auto kKeyPwrRated  = "meter/pwrRatedMaxW";  // rated max (red zone start)

// Per-rig scoped forms of the two HARDWARE-cal keys (the rest of meter/ is
// UI prefs and stays shared).  rig/<activeId>/meter/{calDb,pwrRatedMaxW}
// (or the legacy flat key when no rig is active).
QString calKeyScoped() {
    return lyra::rig::scope::rigKey(QLatin1String(kKeyCal));
}
QString pwrKeyScoped() {
    return lyra::rig::scope::rigKey(QLatin1String(kKeyPwrRated));
}
constexpr auto kKeyTxSecond  = "meter/txSecondary";   // TX secondary readout source
constexpr auto kKeyTxSecond2 = "meter/txSecondary2";  // TX second secondary readout source

// PWR scale conventions.  pwrScaleMaxW = 2 * rated max so the danger
// zone sits at half-scale on the renderer's 0..1 axis.  Smoothing,
// peak-hold, and decay tuned slightly snappier than the RX S-meter
// because TX-watts wants to track speech peaks live; the RX S-meter
// trades response for visual smoothness.
constexpr double kPwrSmooth     = 0.65;        // 0..1; higher = snappier
constexpr double kPwrPeakDecay  = 0.10;
constexpr double kPwrMaxDecay   = 0.012;       // same gentle drop as S-meter
constexpr double kPwrGlowDecay  = 0.18;

// SWR scale conventions.  Below this guard-band fwd power the
// reflection-coefficient computation is dominated by ADC noise and
// would render garbage SWR (e.g. 8:1 on a perfectly matched dummy
// load) — the model displays "—" instead, matching the convention
// every commercial transceiver uses for low-power SWR readouts.
// Above the scale ceiling the meter pegs and the text shows "≥3:1".
constexpr double kSwrGuardW     = 0.5;         // fwd < this → no reading
constexpr double kSwrScaleMin   = 1.0;         // perfect match
constexpr double kSwrScaleMax   = 3.0;         // peg-and-show-≥ above this
constexpr double kSwrDanger     = 2.0;         // industry "high" threshold
constexpr double kSwrSmooth     = 0.40;        // calmer than PWR (operator
                                                // wants steady reading,
                                                // not speech-peak chasing)
constexpr double kSwrPeakDecay  = 0.06;
constexpr double kSwrGlowDecay  = 0.12;

// Standard HF dBm→S-unit table, compact form. Ascending
// (upperBound, label): return the first row whose dbm <= upperBound.
struct SRow { double upper; const char *label; };
const SRow kHfRows[] = {
    {-124, "S0"}, {-118, "S1"}, {-112, "S2"}, {-106, "S3"}, {-100, "S4"},
    {-94,  "S5"}, {-88,  "S6"}, {-82,  "S7"}, {-76,  "S8"}, {-70,  "S9"},
    {-66, "S9+5"}, {-60, "S9+10"}, {-56, "S9+15"}, {-46, "S9+20"},
    {-36, "S9+30"}, {-26, "S9+40"}, {-16, "S9+50"},
};
const SRow kVhfRows[] = {
    {-144, "S0"}, {-138, "S1"}, {-132, "S2"}, {-126, "S3"}, {-120, "S4"},
    {-114, "S5"}, {-108, "S6"}, {-102, "S7"}, {-96,  "S8"}, {-90,  "S9"},
    {-86, "S9+5"}, {-80, "S9+10"}, {-76, "S9+15"}, {-66, "S9+20"},
    {-56, "S9+30"}, {-46, "S9+40"}, {-36, "S9+50"},
};
}  // namespace

MeterModel::MeterModel(lyra::ipc::HL2Stream *stream,
                       lyra::dsp::WdspEngine *wdsp, QObject *parent)
    : QObject(parent), stream_(stream), wdsp_(wdsp) {
    QSettings s;
    style_ = std::clamp(s.value(QString::fromLatin1(kKeyStyle), 0).toInt(),
                        0, 2);
    txStyle_ = std::clamp(s.value(QString::fromLatin1(kKeyTxStyle), 0).toInt(),
                          0, 2);
    separateStyle_ = s.value(QString::fromLatin1(kKeySepStyle), false).toBool();
    calDb_ = s.value(calKeyScoped(), 0.0).toDouble();
    peakHoldMs_ = std::clamp(
        s.value(QString::fromLatin1(kKeyPeakHold), 800).toInt(), 100, 5000);
    peakHoldTicks_ = std::max(1, peakHoldMs_ / kTickMs);
    // Task #45-class — operator-tunable PWR meter MAX-window hold time
    // (separate from peakHoldMs_ above, which controls the small peak-
    // cap indicator's hang/decay; THIS one controls the main needle
    // hold via the sliding-window MAX detector's window length).
    pwrPeakHoldMs_ = std::clamp(
        s.value(QString::fromLatin1(kKeyPwrPeakHold), 3000).toInt(),
        100, 10000);
    pwrWinSamples_ = std::clamp(pwrPeakHoldMs_ / kTickMs,
                                 1, kPwrWindowSamplesMax);
    // PWR ballistic mode autoload — default PWR_PEAK (existing
    // shipped behavior).  Clamped to the valid enum range so a
    // stale/corrupted QSettings value can't surface as an
    // unhandled compute branch.
    {
        const int raw = s.value(QString::fromLatin1(kKeyPwrBallistic),
                                 int(PWR_PEAK)).toInt();
        switch (raw) {
        case PWR_PEP:  pwrBallistic_ = PWR_PEP;  break;
        case PWR_AVG:  pwrBallistic_ = PWR_AVG;  break;
        case PWR_PEAK:
        default:       pwrBallistic_ = PWR_PEAK; break;
        }
    }
    maxPeakEnabled_ = s.value(QString::fromLatin1(kKeyMaxOn), true).toBool();
    maxHoldMs_ = std::clamp(
        s.value(QString::fromLatin1(kKeyMaxHold), 3000).toInt(), 500, 60000);
    maxHoldTicks_ = std::max(1, maxHoldMs_ / kTickMs);
    // RX-state and TX-state source preferences.  Per the reference-
    // faithful split: RX dropdown is RX-signal-only (currently just
    // RX_SMETER); TX dropdown is the TX-chain / forward-power /
    // HL2-telemetry set.  Stale legacy values (e.g. PWR persisted for
    // the RX side before the split) get normalized to RX_SMETER /
    // PWR — those defaults match the picker's first-option fallback
    // in settingsdialog.cpp so the UI + model converge cleanly.
    auto clampRx = [](int raw) -> Source {
        switch (raw) {
        case RX_SMETER: return RX_SMETER;
        default:        return RX_SMETER;
        }
    };
    auto clampTx = [](int raw) -> Source {
        switch (raw) {
        case PWR:
        case SWR:
        case PA_CURRENT:
        case PA_VOLTS:
        case TEMP:
        case ALC_G:        // value 6 — was ALC
        case MIC:
        case LVL_G:        // value 8 — was COMP
        case LVL_PK:
        case ALC_PK:
        case ALC_GROUP:
            return Source(raw);
        default:
            return PWR;
        }
    };
    rxSource_ = clampRx(s.value(QString::fromLatin1(kKeyRxSource),
                                 int(RX_SMETER)).toInt());
    txSource_ = clampTx(s.value(QString::fromLatin1(kKeyTxSource),
                                 int(PWR)).toInt());
    // Initialize the active source from the live MOX state if we already
    // have a stream (we do — passed via the ctor arg).  moxActive is
    // false at construction time (the stream isn't open yet), so this
    // boils down to rxSource_ in practice — but the conditional keeps
    // the derivation rule explicit.
    source_ = (stream_ && (stream_->moxActive() || stream_->cwKeyingActive()))
                  ? txSource_ : rxSource_;
    pwrRatedMaxW_ = std::clamp(
        s.value(pwrKeyScoped(), 5.0).toDouble(), 0.5, 200.0);
    pwrScaleMaxW_ = pwrRatedMaxW_ * 2.0;
    // TX secondary digital readout default = -1 (none).  Clamp to the
    // valid Source enum range (or -1) so a stale value can't surface
    // as garbage text in the renderer's snrText slot.
    {
        const int raw = s.value(QString::fromLatin1(kKeyTxSecond), -1).toInt();
        txSecondary_ = (raw == -1 || (raw >= RX_SMETER && raw <= ALC_GROUP))
                           ? raw
                           : -1;
    }
    {
        const int raw = s.value(QString::fromLatin1(kKeyTxSecond2), -1).toInt();
        txSecondary2_ = (raw == -1 || (raw >= RX_SMETER && raw <= ALC_GROUP))
                            ? raw
                            : -1;
    }

    hist_.assign(kHistory, 0.0);
    history_.reserve(kHistory);
    for (int i = 0; i < kHistory; ++i) history_.append(0.0);

    updateScale();
    connect(&timer_, &QTimer::timeout, this, &MeterModel::tick);
    timer_.start(kTickMs);

    // MOX-edge auto-swap (task #33).  When the wire MOX bit settles
    // true (post TR-delay), swap to the operator's TX preference.
    // When it clears (post ptt_out_delay), swap back to the RX
    // preference.  Goes through setSource() so the per-source state
    // reset (peak/hold/history zeroing) happens on every swap — a
    // stale RX peak doesn't render as a bogus PWR peak across the
    // edge.  No direct QSettings write on the swap itself: the
    // operator's PREFS persist (rxSource/txSource), the CURRENT
    // displayed source is derived from them + the MOX state.
    if (stream_) {
        // The displayed source follows the keyed state: the wire MOX bit
        // (SSB/AM/FM/digital) OR the CW message-level keyed state.  QSK CW
        // keys the PA at the gateware and leaves the wire MOX low, so MOX
        // never rises — but the operator still expects the meter to flip to
        // TX and show forward power.  cwKeyingActive supplies that edge.
        connect(stream_, &lyra::ipc::HL2Stream::moxActiveChanged, this,
                [this](bool) { applyKeyStateSource(); });
        connect(stream_, &lyra::ipc::HL2Stream::cwKeyingActiveChanged, this,
                [this](bool) { applyKeyStateSource(); });
    }
}

void MeterModel::applyKeyStateSource() {
    if (!stream_) return;
    const bool keyed = stream_->moxActive() || stream_->cwKeyingActive();
    // Separate RX/TX meter STYLE — swap the visual style on the MOX edge
    // (independent of whether the source also changed; a mode where RX and
    // TX share a source still wants the style to flip).
    if (separateStyle_ && style_ != txStyle_) {
        if (activeStyle() == 2) buildLadderRows(); else ladderRows_.clear();
        emit activeStyleChanged();
    }
    const Source target = keyed ? txSource_ : rxSource_;
    if (target == source_) return;
    // Swap WITHOUT touching meter/source persistence — rxSource/txSource
    // are the canonical prefs; the CURRENT source is derived from them +
    // the keyed state.  Reset per-source peak/hold/history so a stale RX
    // peak doesn't render as a bogus TX peak across the edge.
    source_ = target;
    level_ = 0.0;
    peak_  = 0.0;
    maxPeak_ = 0.0;
    glow_  = 0.0;
    holdCtr_ = 0;
    maxHoldCtr_ = 0;
    dispDbm_ = -140.0;
    noiseFloorDbm_ = -140.0;
    noiseLevel_ = 0.0;
    snrText_ = QStringLiteral("—");
    secondary2Text_.clear();
    text_ = QStringLiteral("—");
    dbmText_ = QStringLiteral("—");
    std::fill(hist_.begin(), hist_.end(), 0.0);
    for (int i = 0; i < kHistory; ++i) history_[i] = 0.0;
    emit sourceChanged();
    emit updated();
}

void MeterModel::updateScale() {
    const double f = stream_ ? double(stream_->rx1FreqHz()) : 0.0;
    aboveS9_ = (f >= kS9FreqHz);
    floorDbm_ = aboveS9_ ? kFloorVhf : kFloorHf;
    ceilDbm_  = aboveS9_ ? kCeilVhf  : kCeilHf;
    s9Dbm_    = aboveS9_ ? kS9Vhf    : kS9Hf;
}

double MeterModel::normForDbm(double dbm) const {
    return std::clamp((dbm - floorDbm_) / (ceilDbm_ - floorDbm_), 0.0, 1.0);
}

double MeterModel::normAtS9() const {
    // Source-agnostic "danger zone start" position (0..1).  Historical
    // name kept so the QML renderers' zone-coloring logic (S9 boundary
    // → red for the S-meter) generalizes to other sources without
    // renaming the Q_PROPERTY: PWR puts rated-max-W here, SWR puts
    // 2.0 SWR, ALC puts the -3 dB threshold, etc.  S-meter retains
    // the original dBm-derived value; other sources read the cached
    // normDanger_ that their compute fn writes.
    if (source_ == RX_SMETER)
        return (s9Dbm_ - floorDbm_) / (ceilDbm_ - floorDbm_);
    return normDanger_;
}

QString MeterModel::sLabel(double dbm) const {
    const SRow *rows = aboveS9_ ? kVhfRows : kHfRows;
    const int n = aboveS9_ ? int(std::size(kVhfRows)) : int(std::size(kHfRows));
    for (int i = 0; i < n; ++i)
        if (dbm <= rows[i].upper) return QString::fromLatin1(rows[i].label);
    return QStringLiteral("S9+60");
}

int MeterModel::activeStyle() const {
    // The style the renderer actually shows: the TX style on TX (when the
    // operator has enabled a separate TX style), else the RX/default style.
    const bool keyed = stream_ &&
        (stream_->moxActive() || stream_->cwKeyingActive());
    return (separateStyle_ && keyed) ? txStyle_ : style_;
}

void MeterModel::setStyle(int s) {
    // 0=HorizonArc (default), 1=PlasmaBar, 2=VerticalLadder (Multi).
    // This is the RX / default style.
    s = std::clamp(s, 0, 2);
    if (s == style_) return;
    const int prevActive = activeStyle();
    style_ = s;
    QSettings().setValue(QString::fromLatin1(kKeyStyle), style_);
    // Populate/clear the Ladder rows for whatever style is showing NOW.
    if (activeStyle() == 2) buildLadderRows(); else ladderRows_.clear();
    emit styleChanged();
    if (activeStyle() != prevActive) emit activeStyleChanged();
    emit updated();
}

void MeterModel::setTxStyle(int s) {
    s = std::clamp(s, 0, 2);
    if (s == txStyle_) return;
    const int prevActive = activeStyle();
    txStyle_ = s;
    QSettings().setValue(QString::fromLatin1(kKeyTxStyle), txStyle_);
    if (activeStyle() == 2) buildLadderRows(); else ladderRows_.clear();
    emit txStyleChanged();
    if (activeStyle() != prevActive) emit activeStyleChanged();
    emit updated();
}

void MeterModel::setSeparateStyle(bool on) {
    if (on == separateStyle_) return;
    const int prevActive = activeStyle();
    separateStyle_ = on;
    QSettings().setValue(QString::fromLatin1(kKeySepStyle), separateStyle_);
    if (activeStyle() == 2) buildLadderRows(); else ladderRows_.clear();
    emit separateStyleChanged();
    if (activeStyle() != prevActive) emit activeStyleChanged();
    emit updated();
}

void MeterModel::setActiveStyle(int s) {
    // The header Arc/Bar/Ladder buttons: override whatever state is showing.
    const bool keyed = stream_ &&
        (stream_->moxActive() || stream_->cwKeyingActive());
    if (separateStyle_ && keyed) setTxStyle(s);
    else                         setStyle(s);
}

void MeterModel::setCalDb(double d) {
    d = std::clamp(d, -60.0, 60.0);
    if (std::abs(d - calDb_) < 1e-9) return;
    calDb_ = d;
    QSettings().setValue(calKeyScoped(), calDb_);
    emit calChanged();
}

void MeterModel::setPeakHoldMs(int ms) {
    ms = std::clamp(ms, 100, 5000);
    if (ms == peakHoldMs_) return;
    peakHoldMs_ = ms;
    peakHoldTicks_ = std::max(1, peakHoldMs_ / kTickMs);
    QSettings().setValue(QString::fromLatin1(kKeyPeakHold), peakHoldMs_);
    emit peakHoldChanged();
}

// Task #45-class — operator-tunable PWR meter MAX-window hold time.
// Live-apply: the new sample count takes effect on the next computePwr
// tick.  Buffer slots beyond the new pwrWinSamples_ become irrelevant
// (the scan + wraparound use pwrWinSamples_) — they'll naturally
// stop influencing the displayed value within one full window cycle
// even without an explicit reset.  pwrWinIdx_ is wrapped down if it
// would exceed the new range so the next write lands in a valid slot.
void MeterModel::setPwrPeakHoldMs(int ms) {
    ms = std::clamp(ms, 100, 10000);
    if (ms == pwrPeakHoldMs_) return;
    pwrPeakHoldMs_ = ms;
    pwrWinSamples_ = std::clamp(pwrPeakHoldMs_ / kTickMs,
                                 1, kPwrWindowSamplesMax);
    if (pwrWinIdx_ >= pwrWinSamples_) pwrWinIdx_ = 0;
    QSettings().setValue(QString::fromLatin1(kKeyPwrPeakHold), pwrPeakHoldMs_);
    emit pwrPeakHoldMsChanged();
}

void MeterModel::setPwrBallistic(int m) {
    // Validate against the enum — out-of-range falls back to PEAK
    // (current shipped default).  Switching modes resets the
    // detector state so a stale window-MAX or IIR value doesn't
    // bleed into the new ballistic's first sample.
    PwrBallistic ns;
    switch (m) {
    case PWR_PEP:  ns = PWR_PEP;  break;
    case PWR_AVG:  ns = PWR_AVG;  break;
    case PWR_PEAK:
    default:       ns = PWR_PEAK; break;
    }
    if (ns == pwrBallistic_) return;
    pwrBallistic_ = ns;
    // Reset the per-mode state so the first tick after a swap starts
    // clean — otherwise a 4 W AVG-mode value would persist into a
    // PEP ring-buffer reset and read as 4 W for one tick before
    // falling.  pwrWinHist_ is the MAX-detector ring; dispDbm_ is
    // the shared smoothed/MAX value the text readout reads.
    for (int i = 0; i < kPwrWindowSamplesMax; ++i) pwrWinHist_[i] = 0.0;
    pwrWinIdx_ = 0;
    dispDbm_ = 0.0;
    QSettings().setValue(QString::fromLatin1(kKeyPwrBallistic), int(ns));
    emit pwrBallisticChanged();
    if (source_ == PWR) emit updated();
}

void MeterModel::setMaxPeakEnabled(bool on) {
    if (on == maxPeakEnabled_) return;
    maxPeakEnabled_ = on;
    if (!on) { maxPeak_ = 0.0; maxHoldCtr_ = 0; }
    QSettings().setValue(QString::fromLatin1(kKeyMaxOn), maxPeakEnabled_);
    emit maxPeakCfgChanged();
}

void MeterModel::setMaxHoldMs(int ms) {
    ms = std::clamp(ms, 500, 60000);
    if (ms == maxHoldMs_) return;
    maxHoldMs_ = ms;
    maxHoldTicks_ = std::max(1, maxHoldMs_ / kTickMs);
    QSettings().setValue(QString::fromLatin1(kKeyMaxHold), maxHoldMs_);
    emit maxPeakCfgChanged();
}

void MeterModel::setSource(int s) {
    // Click-to-cycle / direct-set entry point (task #35 will hook the
    // meter-face click here).  Updates the appropriate preference slot
    // — RX or TX based on the live MOX state — so the operator's
    // choice persists into the right per-state pref, then derives the
    // active source.  Falls back to RX_SMETER for out-of-range values.
    if (s < RX_SMETER || s > ALC_GROUP) s = RX_SMETER;
    const Source ns = Source(s);
    const bool moxOn = stream_ && stream_->moxActive();
    if (moxOn) {
        if (txSource_ != ns) {
            txSource_ = ns;
            QSettings().setValue(QString::fromLatin1(kKeyTxSource), int(ns));
            emit txSourceChanged();
        }
    } else {
        if (rxSource_ != ns) {
            rxSource_ = ns;
            QSettings().setValue(QString::fromLatin1(kKeyRxSource), int(ns));
            emit rxSourceChanged();
        }
    }
    if (ns == source_) return;
    source_ = ns;
    // Reset per-source state so the new source starts from zero (a
    // stale S-meter peak would render as a bogus PWR peak on swap).
    level_ = 0.0;
    peak_  = 0.0;
    maxPeak_ = 0.0;
    glow_  = 0.0;
    holdCtr_ = 0;
    maxHoldCtr_ = 0;
    dispDbm_ = -140.0;
    noiseFloorDbm_ = -140.0;
    noiseLevel_ = 0.0;
    snrText_ = QStringLiteral("—");
    secondary2Text_.clear();
    text_ = QStringLiteral("—");
    dbmText_ = QStringLiteral("—");
    std::fill(hist_.begin(), hist_.end(), 0.0);
    for (int i = 0; i < kHistory; ++i) history_[i] = 0.0;
    emit sourceChanged();
    emit updated();
}

void MeterModel::setRxSource(int s) {
    // Per the reference-faithful split (task #35 follow-up): the RX
    // dropdown is RX-signal-only.  PWR/SWR/ALC/MIC/COMP are
    // TX-chain values that have no meaning at rest; PA telemetry
    // already surfaces on the dedicated HL2 banner chip + the
    // Vertical Ladder style.  Out-of-set values fall back to
    // RX_SMETER so a stale persisted setting from before the split
    // can't lock the picker on an unselectable option.
    Source ns;
    switch (s) {
    case RX_SMETER: ns = RX_SMETER; break;
    default:        ns = RX_SMETER; break;
    }
    if (ns == rxSource_) return;
    rxSource_ = ns;
    QSettings().setValue(QString::fromLatin1(kKeyRxSource), int(ns));
    emit rxSourceChanged();
    if (stream_ && !stream_->moxActive() && source_ != ns)
        setSource(int(ns));
}

void MeterModel::setTxSource(int s) {
    // TX dropdown set: PWR/SWR/PA Current/PA Volts/Temp + the
    // Lyra TX dynamics-meter set (Task #71 §2; CFC + COMP pruned
    // 2026-06-02 PM — Combinator replaces both per #51 design,
    // and Combinator meters will be Lyra-native entries not WDSP
    // TXA indices).  Active set: MIC / LVL_PK / LVL_G / ALC_PK /
    // ALC_G / ALC_GROUP.  Out-of-set values fall back to PWR.
    Source ns;
    switch (s) {
    case PWR:
    case SWR:
    case PA_CURRENT:
    case PA_VOLTS:
    case TEMP:
    case ALC_G:        // value 6 — was ALC
    case MIC:
    case LVL_G:        // value 8 — was COMP
    case LVL_PK:
    case ALC_PK:
    case ALC_GROUP:
        ns = Source(s);
        break;
    default:
        ns = PWR;
        break;
    }
    if (ns == txSource_) return;
    txSource_ = ns;
    QSettings().setValue(QString::fromLatin1(kKeyTxSource), int(ns));
    emit txSourceChanged();
    if (stream_ && stream_->moxActive() && source_ != ns)
        setSource(int(ns));
}

void MeterModel::setPwrRatedMaxW(double w) {
    // 0.5 W floor catches the QRP regime; 200 W ceiling covers
    // operators on legal-limit amps comfortably with the
    // scale-max = 2 * rated convention (~400 W full scale).
    w = std::clamp(w, 0.5, 200.0);
    if (std::abs(w - pwrRatedMaxW_) < 1e-6) return;
    pwrRatedMaxW_ = w;
    pwrScaleMaxW_ = w * 2.0;
    QSettings().setValue(pwrKeyScoped(), pwrRatedMaxW_);
    emit pwrCalChanged();
    // Force a re-emit so the renderer redraws the scale ticks +
    // danger-zone position with the new rated max even before the
    // next tick lands.
    if (source_ == PWR) emit updated();
}

void MeterModel::setTxSecondary(int s) {
    // -1 (none) or a valid Source enum value.  Out-of-range falls
    // back to -1 (hide) rather than RX_SMETER, because the operator's
    // intent for an invalid secondary is "show nothing" — picking
    // RX_SMETER silently would surprise them with an S-meter readout
    // under their PWR meter.
    if (s != -1 && (s < RX_SMETER || s > ALC_GROUP)) s = -1;
    if (s == txSecondary_) return;
    txSecondary_ = s;
    QSettings().setValue(QString::fromLatin1(kKeyTxSecond), s);
    emit txSecondaryChanged();
    // Re-emit so the renderer's snrText slot clears or refreshes on
    // the current frame instead of waiting for the next tick.
    emit updated();
}

void MeterModel::setTxSecondary2(int s) {
    // Sibling of setTxSecondary — second small line under the primary,
    // brings TX into RX-parity 3-line layout.  Same -1-or-Source-enum
    // contract.
    if (s != -1 && (s < RX_SMETER || s > ALC_GROUP)) s = -1;
    if (s == txSecondary2_) return;
    txSecondary2_ = s;
    QSettings().setValue(QString::fromLatin1(kKeyTxSecond2), s);
    emit txSecondary2Changed();
    emit updated();
}

QString MeterModel::formatSecondaryText(int src) const {
    // Format a one-line digital readout for the given source — used to
    // populate snrText_ as the "small text under the main needle" slot
    // when a TX primary has txSecondary_ set.  Pure read of the
    // underlying stream_ getters; never modifies model state.  Each
    // case mirrors the primary compute's text format so the operator
    // reads identical numbers (e.g. PWR-as-secondary uses the same
    // per-band-calibrated watts).  NaN / sentinel values render as "—".
    if (!stream_) return QString();
    switch (src) {
    case PWR: {
        const double raw = stream_->fwdPowerCalW();   // per-band-calibrated
        if (std::isnan(raw) || raw < 0.0) return QStringLiteral("PWR —");
        const double w = raw;                          // per-band trim is the cal
        return (w < 10.0)
                   ? QStringLiteral("PWR %1 W").arg(w, 0, 'f', 1)
                   : QStringLiteral("PWR %1 W").arg(std::lround(w));
    }
    case SWR: {
        const double fwd = stream_->fwdPowerW();
        const double rev = stream_->revPowerW();
        if (std::isnan(fwd) || std::isnan(rev) || fwd < kSwrGuardW)
            return QStringLiteral("SWR —");
        const double r = std::clamp(rev / std::max(fwd, 1e-9), 0.0, 0.999);
        const double rho = std::sqrt(r);
        const double swr = std::clamp((1.0 + rho) / std::max(1.0 - rho, 1e-9),
                                       kSwrScaleMin, 20.0);
        return (swr >= kSwrScaleMax)
                   ? QStringLiteral("SWR ≥%1:1").arg(kSwrScaleMax, 0, 'f', 1)
                   : QStringLiteral("SWR %1:1").arg(swr, 0, 'f', 2);
    }
    case PA_CURRENT: {
        const double a = stream_->paCurrentA();
        return std::isnan(a)
                   ? QStringLiteral("PA —")
                   : QStringLiteral("PA %1 A").arg(a, 0, 'f', 2);
    }
    case PA_VOLTS: {
        const double v = stream_->hl2SupplyV();
        return std::isnan(v)
                   ? QStringLiteral("V —")
                   : QStringLiteral("V %1 V").arg(v, 0, 'f', 1);
    }
    case TEMP: {
        const double c = stream_->hl2TempC();
        return std::isnan(c)
                   ? QStringLiteral("T —")
                   : QStringLiteral("T %1 °C").arg(c, 0, 'f', 1);
    }
    // ── TX dynamics meter set (#160) — live WDSP TXA reads, reference-
    // faithful (Thetis dsp.cs CalculateTXMeter + console multimeter,
    // verified 2026-06-15).  Thetis displays these as plain "dB" (NOT
    // "dBFS"), clamps each to a per-meter floor (and still shows the
    // clamped number, not "—", while keyed), and uses the AVERAGE meter
    // for ALC.  "—" only at RX rest / NaN.  Per-meter floors:
    //   MIC  TXA_MIC_PK (0)   floor -195   (Thetis multimeter MIC = peak)
    //   LEV  TXA_LVLR_AV (5)  floor -195   (Thetis "Leveler" = average)
    //   LVL  TXA_LVLR_GAIN(6) floor  0     (gain)
    //   ALC  TXA_ALC_AV (13)  floor -30    (Thetis "ALC" = average)
    case MIC:    return formatTxLevelSecondary(QStringLiteral("MIC"), 0,  -195.0);
    case LVL_PK: return formatTxLevelSecondary(QStringLiteral("LEV"), 5,  -195.0);
    case LVL_G:  return formatTxLevelSecondary(QStringLiteral("LVL"), 6,     0.0);
    case ALC_PK: return formatTxLevelSecondary(QStringLiteral("ALC"), 13,  -30.0);
    case ALC_G:  return formatTxAlcGSecondary();
    // ALC_GROUP (Σ): not a defined WDSP TXA meter index in this build
    // (computeAlcGroup feeds NaN) — honest placeholder until implemented.
    case ALC_GROUP: return QStringLiteral("ALC Σ —");
    case RX_SMETER:
    default:
        return QString();
    }
}

QString MeterModel::formatTxLevelSecondary(const QString &label,
                                           int meterIdx,
                                           double floorDb) const {
    // Reference-faithful (Thetis console multimeter): the TX level/gain
    // meters display the raw WDSP dB value clamped to a per-meter floor,
    // labelled plain "dB".  "—" ONLY at RX rest / NaN — while keyed a
    // quiet stage shows its floor (e.g. ALC → "-30.0 dB"), exactly as
    // Thetis renders Math.Max(floor, value).
    const bool keyed = stream_ && stream_->moxActive();
    if (!keyed || !wdsp_) return QStringLiteral("%1 —").arg(label);
    const double v = wdsp_->txMeterRaw(meterIdx);
    if (std::isnan(v)) return QStringLiteral("%1 —").arg(label);
    return QStringLiteral("%1 %2 dB").arg(label)
               .arg(std::max(v, floorDb), 0, 'f', 1);
}

QString MeterModel::formatTxAlcGSecondary() const {
    // ALC-G readout — Thetis-exact (operator decision 2026-06-15,
    // dsp.cs:1020 + console clamp): displayed = max(0, TXA_ALC_GAIN +
    // alcgain), where alcgain is the ALC-max-gain value handed to
    // SetTXAALCMaxGain, added DIRECTLY (Thetis adds its Setup-spinner
    // number as-is; it does NOT 20·log10-convert).  Lyra stores that
    // value as alcMaxGainLinear() (§15.27) and feeds the identical
    // number to SetTXAALCMaxGain, so adding it directly reproduces
    // Thetis bit-for-bit.  "—" only at RX rest / NaN.
    const bool keyed = stream_ && stream_->moxActive();
    if (keyed && wdsp_) {
        const double raw = wdsp_->txMeterRaw(14);
        if (!std::isnan(raw)) {
            const double alcMaxGain =
                stream_ ? stream_->alcMaxGainLinear() : 3.0;
            const double disp = std::max(0.0, raw + alcMaxGain);
            return QStringLiteral("ALC G %1 dB").arg(disp, 0, 'f', 1);
        }
    }
    return QStringLiteral("ALC G —");
}


QVariantList MeterModel::tickMarks() const {
    QVariantList out;
    auto addAt = [&](double pos, const QString &label, bool major) {
        QVariantMap m;
        m[QStringLiteral("pos")]   = std::clamp(pos, 0.0, 1.0);
        m[QStringLiteral("label")] = label;
        m[QStringLiteral("major")] = major;
        out.append(m);
    };
    // S-meter: existing S1/S3/S5/S7/S9/+20/+40/+60 — unchanged.
    if (source_ == RX_SMETER) {
        for (int sl = 1; sl <= 9; sl += 2)
            addAt(normForDbm(s9Dbm_ - (9 - sl) * kDbPerS),
                  QString::number(sl), sl == 9);
        addAt(normForDbm(s9Dbm_ + 20.0), QStringLiteral("+20"), false);
        addAt(normForDbm(s9Dbm_ + 40.0), QStringLiteral("+40"), false);
        addAt(normForDbm(s9Dbm_ + 60.0), QStringLiteral("+60"), false);
        return out;
    }
    if (source_ == PWR) {
        // Watts scale: 0 .. pwrScaleMaxW_ (= 2 * rated max).  Tick
        // spacing scales with the rated max so the labels stay readable
        // whether the operator is on a 5 W bare HL2+ or a 100 W amp.
        // Major ticks at 0, rated, 2*rated; minors at 0.25 / 0.5 /
        // 0.75 / 1.25 / 1.5 / 1.75 multiples — the renderer's "major"
        // flag drives a longer mark + the +20-style emphasis.
        const double r = pwrRatedMaxW_;
        const double m = pwrScaleMaxW_;
        const auto pos = [&](double w) { return std::clamp(w / m, 0.0, 1.0); };
        const auto fmt = [](double w) {
            return (w >= 10.0) ? QString::number(w, 'f', 0)
                                : QString::number(w, 'f', 1);
        };
        addAt(0.0,           QStringLiteral("0"),  true);
        addAt(pos(r * 0.25), fmt(r * 0.25),        false);
        addAt(pos(r * 0.5),  fmt(r * 0.5),         false);
        addAt(pos(r * 0.75), fmt(r * 0.75),        false);
        addAt(pos(r),        fmt(r),               true);   // rated max
        addAt(pos(r * 1.25), fmt(r * 1.25),        false);
        addAt(pos(r * 1.5),  fmt(r * 1.5),         false);
        addAt(pos(r * 1.75), fmt(r * 1.75),        false);
        addAt(1.0,           fmt(m),               true);   // scale max
        return out;
    }
    if (source_ == SWR) {
        // SWR scale: 1.0 (perfect match) on the left, 3.0+ on the right
        // (above 3:1 the meter pegs).  Danger threshold at 2.0 sits at
        // (2-1)/(3-1) = 0.5 on the renderer's 0..1 axis — half-scale,
        // matching the PWR convention.  Operator-relevant SWR values
        // (1.0/1.2/1.5/1.7/2.0/2.5/3.0) all get their own tick.
        const auto pos = [](double swr) {
            return std::clamp((swr - kSwrScaleMin) /
                              (kSwrScaleMax - kSwrScaleMin), 0.0, 1.0);
        };
        addAt(pos(1.0), QStringLiteral("1.0"), true);
        addAt(pos(1.2), QStringLiteral("1.2"), false);
        addAt(pos(1.5), QStringLiteral("1.5"), false);
        addAt(pos(1.7), QStringLiteral("1.7"), false);
        addAt(pos(2.0), QStringLiteral("2.0"), true);   // danger threshold
        addAt(pos(2.5), QStringLiteral("2.5"), false);
        addAt(pos(3.0), QStringLiteral("3.0"), true);
        return out;
    }
    if (source_ == PA_CURRENT) {
        // PA bias current scale: 0 .. 3.0 A (matches kPaScaleMaxA in
        // computePaCurrent's anonymous namespace).  Operator-relevant
        // landmarks: 0.2 A idle bias on a typical HL2+, 1.8 A full
        // tune anchor (verified-reference bench reading),
        // 2.5 A danger threshold.
        const double m = 3.0;
        const auto pos = [&](double a) { return std::clamp(a / m, 0.0, 1.0); };
        addAt(0.0,        QStringLiteral("0"),    true);
        addAt(pos(0.5),   QStringLiteral("0.5"),  false);
        addAt(pos(1.0),   QStringLiteral("1.0"),  false);
        addAt(pos(1.5),   QStringLiteral("1.5"),  false);
        addAt(pos(1.8),   QStringLiteral("1.8"),  true);   // full-tune anchor
        addAt(pos(2.0),   QStringLiteral("2.0"),  false);
        addAt(pos(2.5),   QStringLiteral("2.5"),  true);   // danger
        addAt(1.0,        QStringLiteral("3"),    true);
        return out;
    }
    if (source_ == PA_VOLTS) {
        // PA supply / VDD scale: 0 .. 16 V (matches kVScaleMaxV).
        // Operator landmarks: 12 V nominal HL2 supply, 13.8 V typical
        // 13.8 V bench supply, 14 V danger threshold.
        const double m = 16.0;
        const auto pos = [&](double v) { return std::clamp(v / m, 0.0, 1.0); };
        addAt(0.0,        QStringLiteral("0"),    true);
        addAt(pos(6.0),   QStringLiteral("6"),    false);
        addAt(pos(10.0),  QStringLiteral("10"),   false);
        addAt(pos(12.0),  QStringLiteral("12"),   true);   // nominal HL2 supply
        addAt(pos(13.8),  QStringLiteral("13.8"), false);
        addAt(pos(14.0),  QStringLiteral("14"),   true);   // danger
        addAt(pos(15.0),  QStringLiteral("15"),   false);
        addAt(1.0,        QStringLiteral("16"),   true);
        return out;
    }
    if (source_ == TEMP) {
        // HL2 board temperature: 0 .. 80 °C (matches kTempScaleMaxC).
        // Operator landmarks: 25 °C ambient/idle, 30 °C full-tune
        // typical, 60 °C danger threshold.
        const double m = 80.0;
        const auto pos = [&](double c) { return std::clamp(c / m, 0.0, 1.0); };
        addAt(0.0,        QStringLiteral("0"),   true);
        addAt(pos(20.0),  QStringLiteral("20"),  false);
        addAt(pos(25.0),  QStringLiteral("25"),  true);    // typical idle
        addAt(pos(30.0),  QStringLiteral("30"),  false);
        addAt(pos(40.0),  QStringLiteral("40"),  false);
        addAt(pos(50.0),  QStringLiteral("50"),  false);
        addAt(pos(60.0),  QStringLiteral("60"),  true);    // danger
        addAt(pos(70.0),  QStringLiteral("70"),  false);
        addAt(1.0,        QStringLiteral("80"),  true);
        return out;
    }
    // ALC / MIC / COMP: deferred (need WDSP TXA chain, v0.2.1 + comp).
    return out;
}

void MeterModel::ladderRowFor(int src, double *level, double *danger) const {
    // Per-source level + danger threshold for the Ladder rows.  Each
    // source gets its own normalization so the bar fill reads "as a
    // fraction of operator-relevant range" — e.g. SWR's 0..1 maps over
    // the 1.0..3.0 axis (with 2.0 = danger at 0.5), PWR over
    // 0..pwrScaleMaxW_ (with rated max at half-scale).
    *level = 0.0;
    *danger = 1.0;  // off-scale = no red zone
    if (!stream_) return;
    switch (src) {
    case PWR: {
        const double raw = stream_->fwdPowerCalW();
        const double w = (std::isnan(raw) || raw < 0.0) ? 0.0 : raw;
        *level = std::clamp(w / std::max(pwrScaleMaxW_, 1e-9), 0.0, 1.0);
        *danger = std::clamp(pwrRatedMaxW_ / std::max(pwrScaleMaxW_, 1e-9),
                              0.0, 1.0);
        return;
    }
    case SWR: {
        const double fwd = stream_->fwdPowerW();
        const double rev = stream_->revPowerW();
        if (std::isnan(fwd) || std::isnan(rev) || fwd < kSwrGuardW) return;
        const double r = std::clamp(rev / std::max(fwd, 1e-9), 0.0, 0.999);
        const double rho = std::sqrt(r);
        const double swr = std::clamp((1.0 + rho) / std::max(1.0 - rho, 1e-9),
                                       kSwrScaleMin, kSwrScaleMax);
        *level = std::clamp((swr - kSwrScaleMin) /
                            std::max(kSwrScaleMax - kSwrScaleMin, 1e-9),
                            0.0, 1.0);
        *danger = std::clamp((kSwrDanger - kSwrScaleMin) /
                              std::max(kSwrScaleMax - kSwrScaleMin, 1e-9),
                              0.0, 1.0);
        return;
    }
    case PA_CURRENT: {
        // HL2+ scale: idle ~0.2 A, full-tune anchor ~1.8 A.  Run the
        // row out to 3 A (covers external-PA telemetry too); danger
        // at 2.5 A (above operator's normal full-drive draw).
        const double a = stream_->paCurrentA();
        if (std::isnan(a)) return;
        constexpr double kPAFullScale = 3.0;
        constexpr double kPADanger    = 2.5;
        *level = std::clamp(a / kPAFullScale, 0.0, 1.0);
        *danger = kPADanger / kPAFullScale;
        return;
    }
    case PA_VOLTS: {
        // Center the bar on the 12-13 V nominal HL2 supply; full scale
        // 16 V leaves headroom for higher-VDD external supplies.
        const double v = stream_->hl2SupplyV();
        if (std::isnan(v)) return;
        constexpr double kVFullScale = 16.0;
        constexpr double kVDanger    = 14.0;
        *level = std::clamp(v / kVFullScale, 0.0, 1.0);
        *danger = kVDanger / kVFullScale;
        return;
    }
    case TEMP: {
        // HL2 board temp: idle ~25 °C, full-tune ~31 °C; red zone at
        // 60 °C (well below the gateware thermal-cutoff floor).
        const double c = stream_->hl2TempC();
        if (std::isnan(c)) return;
        constexpr double kTempFullScale = 80.0;
        constexpr double kTempDanger    = 60.0;
        *level = std::clamp(c / kTempFullScale, 0.0, 1.0);
        *danger = kTempDanger / kTempFullScale;
        return;
    }
    case MIC:
    case ALC_PK:
    case LVL_PK: {
        // #160 TX level meters (only listed in srcs while keyed): dB
        // over the -60..0 bar, danger at -6 — matches kMicDbMin/kMicDbMax
        // + computeLevelMeterFromDb.  MIC = TXA_MIC_PK (0); ALC =
        // TXA_ALC_AV (13); LEV = TXA_LVLR_AV (5) — the Thetis averages,
        // not peaks.  Floor / not-keyed → empty bar.
        const int idx = (src == MIC) ? 0 : (src == ALC_PK) ? 13 : 5;
        const double v = wdsp_ ? wdsp_->txMeterRaw(idx)
                               : std::numeric_limits<double>::quiet_NaN();
        if (std::isnan(v) || v <= -300.0) return;
        constexpr double kFloor = -60.0, kTop = 0.0, kDangerDb = -6.0;
        const double db = std::max(v, kFloor);
        *level  = std::clamp((db - kFloor) / (kTop - kFloor), 0.0, 1.0);
        *danger = std::clamp((kDangerDb - kFloor) / (kTop - kFloor), 0.0, 1.0);
        return;
    }
    case ALC_G: {
        // #160 ALC-G headroom: max(0, TXA_ALC_GAIN + 20·log10(maxLin))
        // over the 0..20 dB bar (|kGainDbMax|), danger at 6 dB —
        // matches computeAlcG + computeGainMeterFromDb.
        if (!wdsp_) return;
        const double raw = wdsp_->txMeterRaw(14);
        if (std::isnan(raw)) return;
        const double maxLin = stream_->alcMaxGainLinear();
        const double maxGainDb = 20.0 * std::log10(std::max(maxLin, 1e-9));
        const double disp = std::max(0.0, raw + maxGainDb);
        constexpr double kScale = 20.0, kDangerDb = 6.0;
        *level  = std::clamp(disp / kScale, 0.0, 1.0);
        *danger = kDangerDb / kScale;
        return;
    }
    case RX_SMETER: {
        // RX-mode hero row: reuse the active S-meter level/danger so
        // the Ladder's primary line matches what the Arc/Bar would show.
        *level = level_;
        *danger = normAtS9();
        return;
    }
    default:
        return;
    }
}

void MeterModel::buildLadderRows() {
    ladderRows_.clear();
    if (!stream_) return;
    // Operative source list per MOX state.  Hard-coded sensible defaults
    // for first ship; per-row operator picker is a follow-on task.
    // TX telemetry stack also applies during CW (gateware-keyed, host
    // stays RX on the wire) — follow the same keyed-state OR the single
    // meter uses, so the Ladder flips to the TX rows when sending CW.
    const bool tx = stream_->moxActive() || stream_->cwKeyingActive();
    QList<int> srcs;
    QStringList labels;
    if (tx) {
        // TX: full telemetry stack.  #160 — the TX audio chain has
        // landed, so the live MIC / ALC / ALC-G rows now sit between
        // SWR and the hardware telemetry (PA / VDD / T), matching the
        // reference TX multimeter.  Leveler rows stay off the stack
        // until the leveler runs (it's bypassed → would read floor);
        // they remain selectable as secondary readouts.
        srcs   = {PWR, SWR, MIC, ALC_PK, ALC_G, LVL_PK,
                  PA_CURRENT, PA_VOLTS, TEMP};
        labels = {QStringLiteral("PWR"), QStringLiteral("SWR"),
                  QStringLiteral("MIC"), QStringLiteral("ALC"),
                  QStringLiteral("ALC G"), QStringLiteral("LEV"),
                  QStringLiteral("PA"),  QStringLiteral("VDD"),
                  QStringLiteral("T")};
    } else {
        // RX: three-row degraded view.  The S-meter row is the hero
        // (matches the operator's RX expectation); noise floor + SNR
        // surface the other useful RX numbers the Arc/Bar bury inside
        // a single face.
        srcs   = {RX_SMETER};
        labels = {QStringLiteral("S")};
    }

    for (int i = 0; i < srcs.size(); ++i) {
        double level = 0.0, danger = 1.0;
        ladderRowFor(srcs[i], &level, &danger);
        QVariantMap row;
        row.insert(QStringLiteral("label"), labels[i]);
        row.insert(QStringLiteral("value"),
                   srcs[i] == RX_SMETER ? text_
                                        : formatSecondaryText(srcs[i]));
        row.insert(QStringLiteral("level"),  level);
        row.insert(QStringLiteral("danger"), danger);
        ladderRows_.append(row);
    }

    if (!tx) {
        // RX-only extra rows: noise floor (position on the scale) + SNR
        // delta.  No formatSecondaryText path because these are RX
        // S-meter sub-quantities, not Source enum values.
        QVariantMap nf;
        nf.insert(QStringLiteral("label"), QStringLiteral("NF"));
        nf.insert(QStringLiteral("value"),
                  QStringLiteral("%1 dBm").arg(std::lround(noiseFloorDbm_)));
        nf.insert(QStringLiteral("level"),  noiseLevel_);
        nf.insert(QStringLiteral("danger"), 1.0);
        ladderRows_.append(nf);

        QVariantMap snr;
        snr.insert(QStringLiteral("label"), QStringLiteral("SNR"));
        snr.insert(QStringLiteral("value"), snrText_);
        // SNR level: 0..60 dB axis, danger at 0 dB (signal at floor).
        const double snrDb = std::max(0.0, dispDbm_ - noiseFloorDbm_);
        snr.insert(QStringLiteral("level"),  std::clamp(snrDb / 60.0, 0.0, 1.0));
        snr.insert(QStringLiteral("danger"), 0.0);
        ladderRows_.append(snr);
    }
}

void MeterModel::tick() {
    // Dispatch to the source-specific compute.  Each compute fn fills
    // level_/peak_/text_/etc. and emits updated() at the end.  Sources
    // not yet implemented fall through to a passive no-op (render stays
    // at zero state until that source's compute lands in a later
    // commit) — this keeps the foundation commit visually unchanged
    // for the default RX_SMETER case while leaving the dispatch ready.

    // ── Counters-first TX-meter probe (#160) ──────────────────────────
    // LYRA_TXMETER_DEBUG=1 logs the raw GetTXAMeter values ~1 Hz while
    // transmitting, regardless of the selected meter source, so the next
    // key-up tells us whether WDSP is handing back live values or floor/
    // NaN — before we touch the display path.  Meter type indices (TXA.h):
    // MIC_PK=0 MIC_AV=1 LVLR_AV=5 LVLR_GAIN=6 ALC_PK=12 ALC_AV=13
    // ALC_GAIN=14 OUT_PK=15.  Zero cost when the env var is unset.
    static const bool kTxMeterDbg =
        qEnvironmentVariableIsSet("LYRA_TXMETER_DEBUG");
    if (kTxMeterDbg && wdsp_ && stream_ && stream_->moxActive()) {
        static int dbgCtr = 0;
        if (++dbgCtr >= 20) {           // ~1 Hz at the 50 ms tick
            dbgCtr = 0;
            // qWarning (not qInfo) so the line is captured in the in-app
            // log even when the Settings→Hardware DEBUG toggle is OFF —
            // this is a one-shot bench probe, gated by the env var above.
            qWarning("[TXMETER] MIC_PK=%.1f MIC_AV=%.1f ALC_PK=%.1f "
                  "ALC_AV=%.1f ALC_GAIN=%.1f LVLR_AV=%.1f LVLR_GAIN=%.1f "
                  "OUT_PK=%.1f",
                  wdsp_->txMeterRaw(0),  wdsp_->txMeterRaw(1),
                  wdsp_->txMeterRaw(12), wdsp_->txMeterRaw(13),
                  wdsp_->txMeterRaw(14), wdsp_->txMeterRaw(5),
                  wdsp_->txMeterRaw(6),  wdsp_->txMeterRaw(15));
        }
    }

    switch (source_) {
    case RX_SMETER:   computeSMeter();    return;
    case PWR:         computePwr();       return;
    case SWR:         computeSwr();       return;
    case PA_CURRENT:  computePaCurrent(); return;
    case PA_VOLTS:    computePaVolts();   return;
    case TEMP:        computeTemp();      return;
    // Lyra TX dynamics meter set (Task #71 §2; CFC + COMP pruned
    // 2026-06-02 PM — Combinator replaces both per #51).  All
    // share the level + gain helper bodies; per-source compute
    // is just "read the right WDSP index, route to the helper."
    case ALC_G:       computeAlcG();      return;   // value 6
    case MIC:         computeMic();       return;
    case LVL_G:       computeLvlG();      return;   // value 8
    case LVL_PK:      computeLvlPk();     return;
    case ALC_PK:      computeAlcPk();     return;
    case ALC_GROUP:   computeAlcGroup();  return;
    }
}

void MeterModel::computeSMeter() {
    // RX S-meter compute (extracted from the original tick() body
    // unchanged).  Reads WDSP RXA_S_PK, applies operator cal + the
    // current LNA gain compensation so the reading stays true-to-source
    // regardless of LNA setting, smooths, and maps to the S-unit scale.
    updateScale();

    const double raw = wdsp_ ? wdsp_->sMeterDbm() : -200.0;
    // Reference the reading back to the antenna so it stays TRUE TO SOURCE
    // regardless of LNA: RXA_S_PK is measured after the hardware PGA, so
    // subtract the current LNA gain.  calDb_ then trims the absolute level
    // once (set at any LNA setting); changing LNA no longer moves the S
    // reading.  (Sentinel raw≈−200 when not running → handled downstream.)
    // The calibration formula lives in calibratedSMeterDbm() — the SINGLE
    // source of truth shared with the TCI export (rxSMeterDbm()) so the
    // on-screen meter and the wire can never disagree.
    const double dbm = calibratedSMeterDbm(raw);

    dispDbm_ += kSmooth * (dbm - dispDbm_);
    const double n = normForDbm(dispDbm_);
    level_ = n;

    if (n >= peak_) { peak_ = n; holdCtr_ = peakHoldTicks_; }
    else if (holdCtr_ > 0) { --holdCtr_; }
    else { peak_ = std::max(0.0, peak_ - kPeakDecay); }

    // Max-hold "high-water mark": latch the highest level, hold for
    // maxHoldTicks_, then ease down gently (kMaxDecay << kPeakDecay) so a
    // fleeting DX / QSB crest stays readable long after the fast pip fell.
    if (maxPeakEnabled_) {
        if (n >= maxPeak_) { maxPeak_ = n; maxHoldCtr_ = maxHoldTicks_; }
        else if (maxHoldCtr_ > 0) { --maxHoldCtr_; }
        else { maxPeak_ = std::max(0.0, maxPeak_ - kMaxDecay); }
    } else {
        maxPeak_ = 0.0;
    }

    glow_ = (n >= glow_) ? n : std::max(n, glow_ - kGlowDecay);

    // Noise floor: rolling minimum that leaks up slowly so it follows the
    // band's quiet floor (the original, operator-validated behaviour).
    // THE FIX: skip the −200 "not running / no data" sentinel that
    // sMeterDbm() returns on stop/start + rate changes — feeding it pinned
    // the floor at −200 with no recovery, which is what jammed SNR at 99.
    if (raw > -190.0) {
        if (dispDbm_ < noiseFloorDbm_) noiseFloorDbm_ = dispDbm_;
        else noiseFloorDbm_ = std::min(dispDbm_, noiseFloorDbm_ + kNoiseLeak);
    }
    noiseLevel_ = normForDbm(noiseFloorDbm_);
    const double snr = dispDbm_ - noiseFloorDbm_;
    snrText_ = (level_ <= 0.01 || snr < 1.0)
                   ? QStringLiteral("—")
                   : QStringLiteral("SNR %1 dB").arg(std::lround(std::min(snr, 99.0)));
    // The second secondary readout (task #37) is a TX-only feature; RX
    // already shows dBm + SNR on the two small lines.  Clear it so a
    // stale TX-source value doesn't render under the S-meter on swap.
    secondary2Text_.clear();

    hist_.push_back(n);
    if (int(hist_.size()) > kHistory) hist_.pop_front();
    for (int i = 0; i < kHistory; ++i) history_[i] = hist_[i];

    text_ = sLabel(dispDbm_);
    dbmText_ = QStringLiteral("%1 dBm").arg(dispDbm_, 0, 'f', 0);

    if (activeStyle() == 2) buildLadderRows();
    emit updated();
}

double MeterModel::calibratedSMeterDbm(double raw) const {
    // SINGLE source of truth for the RX S-meter calibration (see the header
    // decl).  RXA_S_PK is measured after the hardware PGA, so subtract the
    // current LNA gain to stay true-to-source across LNA changes; calDb_
    // then trims the absolute level once.
    const double lna = stream_ ? static_cast<double>(stream_->lnaGainDb()) : 0.0;
    return raw + calDb_ - lna;
}

double MeterModel::rxSMeterDbm() const {
    const double raw = wdsp_ ? wdsp_->sMeterDbm() : -200.0;
    // RXA_S_PK returns ≈ −200 when the stream isn't running / between rate
    // changes.  Report a fixed S0-region floor rather than a
    // calibration-shifted sentinel so a TCI client's meter rests at the
    // bottom of its scale instead of jumping to an odd value.
    if (raw <= -190.0) return -140.0;
    return calibratedSMeterDbm(raw);
}

void MeterModel::computePwr() {
    // PWR — forward TX power in watts.  Reads HL2Stream::fwdPowerCalW
    // (raw ADC → W formula x the operator's per-band trim, so it already
    // matches the external watt-meter — the same value the watts cap
    // servo uses) and maps it onto a 0..pwrScaleMaxW_ scale.  Renderer's
    // "danger zone"
    // (the historical normAtS9 binding the QML uses for the red zone)
    // lands at the operator's rated max (pwrRatedMaxW_, default 5 W
    // for a bare HL2+ on-board PA — pwrScaleMaxW_ is 2× that).
    //
    // No noise floor / SNR concept — those are RX-specific.  The text
    // readouts are "X.X W" / "X.XX W peak" so the operator can read
    // the watt-meter without crunching the scale.
    const double raw = stream_ ? stream_->fwdPowerCalW()
                                : std::numeric_limits<double>::quiet_NaN();
    // Telemetry sentinel: NaN means the slot hasn't arrived yet (stream
    // not running, or pre-first-statsChanged).  Display zero state so
    // the renderer doesn't show stale-bogus levels from a previous
    // source's history.
    const double w = (std::isnan(raw) || raw < 0.0)
                          ? 0.0
                          : raw;

    // PWR ballistic — one of three modes the operator picks in
    // Settings → Meter:
    //
    //   PWR_PEAK (default) — sliding-window MAX over the last
    //     pwrWinSamples_ ticks (operator-tunable hold via the PWR
    //     Peak Hold spin box; default 60 samples × 50 ms = 3000 ms).
    //     The 2026-05-31 bench-fix ballistic: brief voice peaks are
    //     captured and held for the full window duration so the
    //     digital bar parks at the peak long enough to read
    //     (compensates for the lack of analog-needle damping in a
    //     digital renderer).
    //
    //   PWR_PEP — sliding-window MAX over a FIXED 10 ticks (= 500 ms
    //     at the 50 ms tick rate).  Tight, snappy "PEP" ballistic —
    //     each speech burst kicks the bar up and it drops back
    //     between syllables.  Ignores the PWR Peak Hold spin box.
    //
    //   PWR_AVG — IIR smoother with pwrAvgAlpha_ ≈ 200 ms τ.  Does
    //     NOT track speech peaks — tracks the running average of
    //     forward power.  Calm, slow needle for sustained-tone
    //     gain-structure work and ragchew.
    //
    // Note: the existing peak_ / maxPeak_ peak-pip / max-hold
    // indicators (rendered separately as marks above the needle for
    // longer-decay "what was the highest reading recently") STAY in
    // place regardless of mode — they're operating-time references
    // driven off level_ that always show recent peaks, even when the
    // primary needle is in AVG mode.
    double newLevelW = 0.0;
    if (pwrBallistic_ == PWR_AVG) {
        // IIR low-pass on the raw watts.  α ≈ 0.22 at 50 ms tick
        // ≈ 200 ms time constant.  dispDbm_ is the smoothed value
        // shared with the text-readout path.
        dispDbm_ += pwrAvgAlpha_ * (w - dispDbm_);
        newLevelW = std::max(0.0, dispDbm_);
    } else {
        // Sliding-window MAX path — covers both PEP (fixed 10-sample
        // window) and PEAK (operator-tunable window).  Window length
        // resolved per-tick from the mode so a runtime switch takes
        // effect immediately.
        const int samples = (pwrBallistic_ == PWR_PEP)
                                ? kPwrPepSamples
                                : pwrWinSamples_;
        pwrWinHist_[pwrWinIdx_] = w;
        pwrWinIdx_ = (pwrWinIdx_ + 1) % std::max(1, samples);
        double windowMax = 0.0;
        for (int i = 0; i < samples; ++i) {
            if (pwrWinHist_[i] > windowMax) windowMax = pwrWinHist_[i];
        }
        dispDbm_ = windowMax;
        newLevelW = windowMax;
    }
    const double n = std::clamp(newLevelW / pwrScaleMaxW_, 0.0, 1.0);
    level_ = n;

    if (n >= peak_) { peak_ = n; holdCtr_ = peakHoldTicks_; }
    else if (holdCtr_ > 0) { --holdCtr_; }
    else { peak_ = std::max(0.0, peak_ - kPwrPeakDecay); }

    if (maxPeakEnabled_) {
        if (n >= maxPeak_) { maxPeak_ = n; maxHoldCtr_ = maxHoldTicks_; }
        else if (maxHoldCtr_ > 0) { --maxHoldCtr_; }
        else { maxPeak_ = std::max(0.0, maxPeak_ - kPwrMaxDecay); }
    } else {
        maxPeak_ = 0.0;
    }

    glow_ = (n >= glow_) ? n : std::max(n, glow_ - kPwrGlowDecay);

    // No noise-floor marker for TX-side sources.  Set noiseLevel_ to
    // 0 so the renderer's `> 0.001` gate skips the marker draw cleanly.
    noiseLevel_ = 0.0;
    // Secondary digital readout (task #36): if the operator has picked
    // a secondary source AND it's distinct from the current primary,
    // render its formatted value into the snrText_ slot that the
    // renderers already display below the main needle.  No secondary
    // configured → leave snrText_ blank so the renderer hides the line.
    if (txSecondary_ >= 0 && Source(txSecondary_) != source_)
        snrText_ = formatSecondaryText(txSecondary_);
    else
        snrText_.clear();
    // Second secondary readout (task #37): same idea, fills the third
    // text line so TX matches RX's primary/dBm/SNR three-line layout.
    // Suppressed when it equals the primary OR equals the first
    // secondary (no duplicate lines — Settings prevents the selection
    // but the model gates defensively).
    if (txSecondary2_ >= 0
        && Source(txSecondary2_) != source_
        && txSecondary2_ != txSecondary_)
        secondary2Text_ = formatSecondaryText(txSecondary2_);
    else
        secondary2Text_.clear();

    // Danger-zone position for the renderer's red-zone coloring.  Equals
    // pwrRatedMaxW_ / pwrScaleMaxW_ = 0.5 with the default 5 W rated /
    // 10 W scale (rated max sits at half-scale visually).
    normDanger_ = std::clamp(pwrRatedMaxW_ / pwrScaleMaxW_, 0.0, 1.0);

    hist_.push_back(n);
    if (int(hist_.size()) > kHistory) hist_.pop_front();
    for (int i = 0; i < kHistory; ++i) history_[i] = hist_[i];

    // Text formatting: < 10 W shows one decimal (the QRP regime where
    // 0.1 W matters); >= 10 W shows whole watts.
    const double dispW = std::max(0.0, dispDbm_);
    text_ = (dispW < 10.0)
                ? QStringLiteral("%1 W").arg(dispW, 0, 'f', 1)
                : QStringLiteral("%1 W").arg(std::lround(dispW));
    dbmText_.clear();

    if (activeStyle() == 2) buildLadderRows();
    emit updated();
}

void MeterModel::computeSwr() {
    // SWR — antenna match readout.  rho = sqrt(rev / fwd); SWR = (1+rho)
    // / (1-rho).  Real-world quirks the model has to handle:
    //
    //   * Below kSwrGuardW forward power the ADC noise floor dominates
    //     both fwd and rev — rho computes as a near-random number and
    //     SWR shows e.g. 8:1 on a perfectly matched dummy load.  Every
    //     commercial rig hides the readout in this regime; we mirror
    //     that ("—" text + zero level so the renderer shows a blank
    //     scale instead of a phantom needle).
    //
    //   * rev > fwd is physically impossible but the ADCs can flip
    //     briefly during the keydown TR-relay transient.  We clamp rho
    //     so SWR doesn't divide by zero or go negative.
    //
    //   * SWR above kSwrScaleMax pegs the meter; the text shows "≥N:1"
    //     so the operator knows it's off-scale rather than reading the
    //     pegged level as 3:1.
    const double fwd = stream_ ? stream_->fwdPowerW()
                                 : std::numeric_limits<double>::quiet_NaN();
    const double rev = stream_ ? stream_->revPowerW()
                                 : std::numeric_limits<double>::quiet_NaN();
    const bool lowPwr = std::isnan(fwd) || std::isnan(rev) ||
                         fwd < kSwrGuardW;

    double swr = kSwrScaleMin;   // perfect-match default for the smoothing
    if (!lowPwr) {
        const double r = std::clamp(rev / std::max(fwd, 1e-9), 0.0, 0.999);
        const double rho = std::sqrt(r);
        swr = std::clamp((1.0 + rho) / std::max(1.0 - rho, 1e-9),
                          kSwrScaleMin, 20.0);  // hard cap before pegging
    }

    // dispDbm_ is the source-agnostic smoothed-value cache; here it
    // holds smoothed SWR.  Initialize on first sample so the smoother
    // doesn't ramp up from -140 (S-meter init value) — that would
    // render as a long sweep from the left end on the first tick after
    // a swap from SMETER → SWR.
    if (dispDbm_ < kSwrScaleMin || dispDbm_ > 100.0) dispDbm_ = swr;
    dispDbm_ += kSwrSmooth * (swr - dispDbm_);

    if (lowPwr) {
        // No usable SWR — render zero state, blank text.  The model
        // still keeps its smoothing primed (so the first valid sample
        // doesn't lurch the meter), but the operator sees "—" instead
        // of bogus values.
        level_ = 0.0;
        peak_ = std::max(0.0, peak_ - kSwrPeakDecay);
        if (maxPeakEnabled_)
            maxPeak_ = std::max(0.0, maxPeak_ - kPwrMaxDecay);
        else
            maxPeak_ = 0.0;
        glow_ = std::max(0.0, glow_ - kSwrGlowDecay);
        text_ = QStringLiteral("—");
    } else {
        const double n = std::clamp(
            (dispDbm_ - kSwrScaleMin) / (kSwrScaleMax - kSwrScaleMin),
            0.0, 1.0);
        level_ = n;
        if (n >= peak_) { peak_ = n; holdCtr_ = peakHoldTicks_; }
        else if (holdCtr_ > 0) { --holdCtr_; }
        else { peak_ = std::max(0.0, peak_ - kSwrPeakDecay); }
        if (maxPeakEnabled_) {
            if (n >= maxPeak_) { maxPeak_ = n; maxHoldCtr_ = maxHoldTicks_; }
            else if (maxHoldCtr_ > 0) { --maxHoldCtr_; }
            else { maxPeak_ = std::max(0.0, maxPeak_ - kPwrMaxDecay); }
        } else {
            maxPeak_ = 0.0;
        }
        glow_ = (n >= glow_) ? n : std::max(n, glow_ - kSwrGlowDecay);
        // Pegged-above-scale gets the ≥-prefix so operators don't read
        // a stuck peg as the actual ratio.
        text_ = (dispDbm_ >= kSwrScaleMax)
                    ? QStringLiteral("≥%1:1").arg(kSwrScaleMax, 0, 'f', 1)
                    : QStringLiteral("%1:1").arg(dispDbm_, 0, 'f', 2);
    }

    // Danger zone at the standard 2.0:1 threshold (sits at 0.5 on the
    // 1.0..3.0 axis = half-scale, matching the PWR red-zone convention).
    normDanger_ = std::clamp(
        (kSwrDanger - kSwrScaleMin) / (kSwrScaleMax - kSwrScaleMin),
        0.0, 1.0);
    noiseLevel_ = 0.0;
    // Secondary digital readout (task #36) — same handling as PWR's
    // compute: render the operator's chosen second source into the
    // snrText_ slot the renderers display below the main needle.
    if (txSecondary_ >= 0 && Source(txSecondary_) != source_)
        snrText_ = formatSecondaryText(txSecondary_);
    else
        snrText_.clear();
    // Second secondary readout (task #37) — fills the third text line
    // so TX matches RX's three-line layout.  Hidden when it equals the
    // primary or duplicates the first secondary.
    if (txSecondary2_ >= 0
        && Source(txSecondary2_) != source_
        && txSecondary2_ != txSecondary_)
        secondary2Text_ = formatSecondaryText(txSecondary2_);
    else
        secondary2Text_.clear();
    dbmText_.clear();

    hist_.push_back(level_);
    if (int(hist_.size()) > kHistory) hist_.pop_front();
    for (int i = 0; i < kHistory; ++i) history_[i] = hist_[i];

    if (activeStyle() == 2) buildLadderRows();
    emit updated();
}

// ─────────────────────────────────────────────────────────────────────
// Task #35 fillers — HL2-telemetry compute paths.
// ─────────────────────────────────────────────────────────────────────
//
// Shared design: each reads ONE EP6-decoded value (paCurrentA() /
// hl2SupplyV() / hl2TempC()), maps it onto a 0..1 scale tuned for the
// HL2+'s typical operating range, smooths with the same IIR family
// used by the SWR compute (these are slowly-changing physical
// quantities — a snappy ballistic would just chase ADC noise), and
// writes the full Q_PROPERTY set (level/peak/maxPeak/glow/text/
// noiseLevel/snrText/secondary*/normDanger).  NaN sentinels → blank
// text + zero level so a not-yet-arrived telemetry slot doesn't draw
// a phantom needle.  No noise-floor / SNR concept (those are S-meter
// exclusives) — the snrText_ slot doubles as the operator-chosen
// secondary readout when txSecondary_ is set.

namespace {
// PA current — HL2+ idle ~0.2 A, full-tune anchor ~1.8 A.  3 A
// full-scale gives headroom for external-PA telemetry (some companion
// boards route through here); 2.5 A danger threshold sits above the
// operator's normal full-drive draw, so the red zone only lights up
// when something's actually wrong.
constexpr double kPaScaleMaxA = 3.0;
constexpr double kPaDangerA   = 2.5;
// PA supply / VDD — HL2 nominal supply is 12-13 V.  16 V full-scale
// covers higher-VDD external supplies; 14 V danger flags an over-
// voltage condition (the HL2's input regulator can take 12-15 V).
constexpr double kVScaleMaxV  = 16.0;
constexpr double kVDangerV    = 14.0;
// HL2 board temperature.  Idle ~25 °C, full-tune climbs to ~31 °C;
// 80 °C full-scale (well above the gateware thermal-cutoff floor);
// 60 °C danger gives the operator a clear "back off" margin before
// the chip-protect kicks in.
constexpr double kTempScaleMaxC = 80.0;
constexpr double kTempDangerC   = 60.0;
// Slow IIR matching the SWR ballistic — these readings are physical
// integrals (PA current via the bias-sense resistor + filter caps,
// board temp via the on-board thermistor), so a fast smoother would
// just chase the sensor noise floor.  Operators want a calm needle
// they can read at a glance.
constexpr double kTelSmooth    = 0.30;
constexpr double kTelPeakDecay = 0.05;
constexpr double kTelGlowDecay = 0.10;
} // namespace

void MeterModel::computePaCurrent() {
    const double raw = stream_ ? stream_->paCurrentA()
                               : std::numeric_limits<double>::quiet_NaN();
    const bool valid = !std::isnan(raw);
    const double a = valid ? std::max(0.0, raw) : 0.0;

    if (dispDbm_ < -100.0 || dispDbm_ > 1000.0) dispDbm_ = a;
    dispDbm_ += kTelSmooth * (a - dispDbm_);
    const double n = std::clamp(dispDbm_ / kPaScaleMaxA, 0.0, 1.0);
    level_ = n;
    if (n >= peak_) { peak_ = n; holdCtr_ = peakHoldTicks_; }
    else if (holdCtr_ > 0) { --holdCtr_; }
    else { peak_ = std::max(0.0, peak_ - kTelPeakDecay); }
    if (maxPeakEnabled_) {
        if (n >= maxPeak_) { maxPeak_ = n; maxHoldCtr_ = maxHoldTicks_; }
        else if (maxHoldCtr_ > 0) { --maxHoldCtr_; }
        else { maxPeak_ = std::max(0.0, maxPeak_ - kPwrMaxDecay); }
    } else { maxPeak_ = 0.0; }
    glow_ = (n >= glow_) ? n : std::max(n, glow_ - kTelGlowDecay);
    noiseLevel_ = 0.0;
    normDanger_ = kPaDangerA / kPaScaleMaxA;
    text_ = valid ? QStringLiteral("%1 A").arg(dispDbm_, 0, 'f', 2)
                  : QStringLiteral("—");
    dbmText_.clear();
    if (txSecondary_ >= 0 && Source(txSecondary_) != source_)
        snrText_ = formatSecondaryText(txSecondary_);
    else
        snrText_.clear();
    if (txSecondary2_ >= 0
        && Source(txSecondary2_) != source_
        && txSecondary2_ != txSecondary_)
        secondary2Text_ = formatSecondaryText(txSecondary2_);
    else
        secondary2Text_.clear();
    hist_.push_back(n);
    if (int(hist_.size()) > kHistory) hist_.pop_front();
    for (int i = 0; i < kHistory; ++i) history_[i] = hist_[i];
    if (activeStyle() == 2) buildLadderRows();
    emit updated();
}

void MeterModel::computePaVolts() {
    const double raw = stream_ ? stream_->hl2SupplyV()
                               : std::numeric_limits<double>::quiet_NaN();
    const bool valid = !std::isnan(raw);
    const double v = valid ? std::max(0.0, raw) : 0.0;

    if (dispDbm_ < -100.0 || dispDbm_ > 1000.0) dispDbm_ = v;
    dispDbm_ += kTelSmooth * (v - dispDbm_);
    const double n = std::clamp(dispDbm_ / kVScaleMaxV, 0.0, 1.0);
    level_ = n;
    if (n >= peak_) { peak_ = n; holdCtr_ = peakHoldTicks_; }
    else if (holdCtr_ > 0) { --holdCtr_; }
    else { peak_ = std::max(0.0, peak_ - kTelPeakDecay); }
    if (maxPeakEnabled_) {
        if (n >= maxPeak_) { maxPeak_ = n; maxHoldCtr_ = maxHoldTicks_; }
        else if (maxHoldCtr_ > 0) { --maxHoldCtr_; }
        else { maxPeak_ = std::max(0.0, maxPeak_ - kPwrMaxDecay); }
    } else { maxPeak_ = 0.0; }
    glow_ = (n >= glow_) ? n : std::max(n, glow_ - kTelGlowDecay);
    noiseLevel_ = 0.0;
    normDanger_ = kVDangerV / kVScaleMaxV;
    text_ = valid ? QStringLiteral("%1 V").arg(dispDbm_, 0, 'f', 1)
                  : QStringLiteral("—");
    dbmText_.clear();
    if (txSecondary_ >= 0 && Source(txSecondary_) != source_)
        snrText_ = formatSecondaryText(txSecondary_);
    else
        snrText_.clear();
    if (txSecondary2_ >= 0
        && Source(txSecondary2_) != source_
        && txSecondary2_ != txSecondary_)
        secondary2Text_ = formatSecondaryText(txSecondary2_);
    else
        secondary2Text_.clear();
    hist_.push_back(n);
    if (int(hist_.size()) > kHistory) hist_.pop_front();
    for (int i = 0; i < kHistory; ++i) history_[i] = hist_[i];
    if (activeStyle() == 2) buildLadderRows();
    emit updated();
}

void MeterModel::computeTemp() {
    const double raw = stream_ ? stream_->hl2TempC()
                               : std::numeric_limits<double>::quiet_NaN();
    const bool valid = !std::isnan(raw);
    const double c = valid ? raw : 0.0;

    if (dispDbm_ < -100.0 || dispDbm_ > 1000.0) dispDbm_ = c;
    dispDbm_ += kTelSmooth * (c - dispDbm_);
    const double n = std::clamp(dispDbm_ / kTempScaleMaxC, 0.0, 1.0);
    level_ = n;
    if (n >= peak_) { peak_ = n; holdCtr_ = peakHoldTicks_; }
    else if (holdCtr_ > 0) { --holdCtr_; }
    else { peak_ = std::max(0.0, peak_ - kTelPeakDecay); }
    if (maxPeakEnabled_) {
        if (n >= maxPeak_) { maxPeak_ = n; maxHoldCtr_ = maxHoldTicks_; }
        else if (maxHoldCtr_ > 0) { --maxHoldCtr_; }
        else { maxPeak_ = std::max(0.0, maxPeak_ - kPwrMaxDecay); }
    } else { maxPeak_ = 0.0; }
    glow_ = (n >= glow_) ? n : std::max(n, glow_ - kTelGlowDecay);
    noiseLevel_ = 0.0;
    normDanger_ = kTempDangerC / kTempScaleMaxC;
    text_ = valid ? QStringLiteral("%1 °C").arg(dispDbm_, 0, 'f', 1)
                  : QStringLiteral("—");
    dbmText_.clear();
    if (txSecondary_ >= 0 && Source(txSecondary_) != source_)
        snrText_ = formatSecondaryText(txSecondary_);
    else
        snrText_.clear();
    if (txSecondary2_ >= 0
        && Source(txSecondary2_) != source_
        && txSecondary2_ != txSecondary_)
        secondary2Text_ = formatSecondaryText(txSecondary2_);
    else
        secondary2Text_.clear();
    hist_.push_back(n);
    if (int(hist_.size()) > kHistory) hist_.pop_front();
    for (int i = 0; i < kHistory; ++i) history_[i] = hist_[i];
    if (activeStyle() == 2) buildLadderRows();
    emit updated();
}

// Task #69 — TX-side TXA meter computes (MIC / COMP / ALC).
// All three follow the computeTemp template (smoothed dispDbm_
// → normalised level_, peak/maxPeak with hold + decay, glow,
// text/hist), differing only in the source scalar + the
// dBFS-to-bar mapping.  Renderers (HorizonArc / PlasmaBar /
// VerticalLadder) consume the same level/peak surface
// regardless of source.
//
// MIC: TXA_MIC_PK peak amplitude → dBFS.  Bar maps -60..0 dBFS
// to 0..1 (full-scale at 0 dBFS).  Operator wants to see strong
// MIC peaks land near the top without clipping.
//
// COMP/ALC: gain-reduction values (negative dB; 0 = no action,
// more negative = more reduction).  Bar maps 0..-20 dB
// reduction to 0..1 (full-scale at 20 dB of reduction).  The
// operator sees "the leveler / ALC is working" when the bar
// climbs from zero.

namespace {
constexpr double kMicDbMin    = -60.0;   // bar floor (silent) for all level meters
constexpr double kMicDbMax    =   0.0;   // bar full-scale (clipping)
constexpr double kGainDbMax   = -20.0;   // bar full-scale (heavy reduction) for gain meters
// Reference DecayRatio = 0.20 per 50 ms tick (the
// MeterManager peak-marker decay constant — Task #71 §3).  Per-tick
// multiplier (1 - 0.20) = 0.80 → ~155 ms 50% fall, ~515 ms 90% fall.
// Replaces the prior linear `peak_ -= 0.06` decay which produced an
// inconsistent / divergent ballistic.
constexpr double kRefPeakDecayRatio = 0.20;
} // namespace

// ── Reference-faithful dynamics-meter ballistic helpers ──────────
// Task #71 §3: WDSP's create_meter already applies a 100 ms IIR
// (tau_average + tau_peak_decay) inside the DLL before storing into
// the result[] array.  An earlier slice layered ANOTHER 100 ms-ish
// IIR on top (kTelSmooth = 0.30 per tick), producing ~240 ms total
// settling time vs the reference's ~100 ms.  Operator-visible
// effect: laggy needle on voice attack + under-read on transients
// (problem when gain-staging by watching peaks).
//
// These helpers do NO UI-side IIR — use the WDSP-returned value
// directly.  Peak marker decays exponentially at the reference's
// DecayRatio = 0.20 per 50 ms tick (no hold-then-decay phase).
// Max-hold marker stays on its existing 3 s hold + slow decay
// (Lyra extension, operator-toggleable, no reference equivalent —
// kept because it's useful for gain-staging review).

void MeterModel::computeLevelMeterFromDb(double rawDb,
                                          double scaleMin,
                                          double dangerDb,
                                          const QString &unitSuffix) {
    const bool valid = !std::isnan(rawDb) && rawDb > -300.0;
    const double dbFs = valid ? std::max(rawDb, scaleMin) : scaleMin;

    // No UI-side IIR — WDSP's internal 100 ms tau_av is sufficient.
    dispDbm_ = dbFs;

    const double n = std::clamp(
        (dispDbm_ - scaleMin) / (kMicDbMax - scaleMin), 0.0, 1.0);
    level_ = n;

    // Reference exponential peak decay — no hold phase.
    if (n >= peak_) peak_ = n;
    else peak_ = std::max(0.0, peak_ * (1.0 - kRefPeakDecayRatio));

    // Max-hold (Lyra extension; operator-toggleable, kept as-is).
    if (maxPeakEnabled_) {
        if (n >= maxPeak_) { maxPeak_ = n; maxHoldCtr_ = maxHoldTicks_; }
        else if (maxHoldCtr_ > 0) { --maxHoldCtr_; }
        else { maxPeak_ = std::max(0.0, maxPeak_ - kPwrMaxDecay); }
    } else { maxPeak_ = 0.0; }
    glow_ = (n >= glow_) ? n : std::max(n, glow_ - kTelGlowDecay);
    noiseLevel_ = 0.0;
    normDanger_ = std::clamp((dangerDb - scaleMin) / (kMicDbMax - scaleMin),
                              0.0, 1.0);

    text_ = valid ? QStringLiteral("%1 %2").arg(dispDbm_, 0, 'f', 1)
                                            .arg(unitSuffix)
                  : QStringLiteral("—");
    dbmText_.clear();
    if (txSecondary_ >= 0 && Source(txSecondary_) != source_)
        snrText_ = formatSecondaryText(txSecondary_);
    else
        snrText_.clear();
    if (txSecondary2_ >= 0
        && Source(txSecondary2_) != source_
        && txSecondary2_ != txSecondary_)
        secondary2Text_ = formatSecondaryText(txSecondary2_);
    else
        secondary2Text_.clear();
    hist_.push_back(n);
    if (int(hist_.size()) > kHistory) hist_.pop_front();
    for (int i = 0; i < kHistory; ++i) history_[i] = hist_[i];
    if (activeStyle() == 2) buildLadderRows();
    emit updated();
}

void MeterModel::computeGainMeterFromDb(double rawDb,
                                         double scaleMax,
                                         double dangerDb) {
    const bool valid = !std::isnan(rawDb);
    const double db = valid ? rawDb : 0.0;

    dispDbm_ = db;

    // Bar maps "magnitude of action" to 0..1.  Leveler gain is
    // typically positive (boost); CFC/ALC gain is typically negative
    // (reduction).  We take the absolute action magnitude so all
    // three gain meters share the same bar scale and dangerDb
    // semantics ("when |action| exceeds X dB you're in the red zone").
    const double action = std::abs(db);
    const double scale  = std::abs(scaleMax);
    const double n = (scale > 1e-9) ? std::clamp(action / scale, 0.0, 1.0)
                                    : 0.0;
    level_ = n;

    if (n >= peak_) peak_ = n;
    else peak_ = std::max(0.0, peak_ * (1.0 - kRefPeakDecayRatio));

    if (maxPeakEnabled_) {
        if (n >= maxPeak_) { maxPeak_ = n; maxHoldCtr_ = maxHoldTicks_; }
        else if (maxHoldCtr_ > 0) { --maxHoldCtr_; }
        else { maxPeak_ = std::max(0.0, maxPeak_ - kPwrMaxDecay); }
    } else { maxPeak_ = 0.0; }
    glow_ = (n >= glow_) ? n : std::max(n, glow_ - kTelGlowDecay);
    noiseLevel_ = 0.0;
    normDanger_ = (scale > 1e-9) ? std::clamp(std::abs(dangerDb) / scale,
                                                0.0, 1.0)
                                 : 0.0;

    text_ = valid ? QStringLiteral("%1 dB").arg(db, 0, 'f', 1)
                  : QStringLiteral("—");
    dbmText_.clear();
    if (txSecondary_ >= 0 && Source(txSecondary_) != source_)
        snrText_ = formatSecondaryText(txSecondary_);
    else
        snrText_.clear();
    if (txSecondary2_ >= 0
        && Source(txSecondary2_) != source_
        && txSecondary2_ != txSecondary_)
        secondary2Text_ = formatSecondaryText(txSecondary2_);
    else
        secondary2Text_.clear();
    hist_.push_back(n);
    if (int(hist_.size()) > kHistory) hist_.pop_front();
    for (int i = 0; i < kHistory; ++i) history_[i] = hist_[i];
    if (activeStyle() == 2) buildLadderRows();
    emit updated();
}

// TX-rip Phase 1 (Q2): TX meter compute paths report NaN (rendered as
// "—") until the new TxDspWorker lands per
// docs/TX_ARCHITECTURAL_MAPPING.md §10.3.
void MeterModel::computeMic() {
    // #158 (post-DL) re-home — TXA_MIC_PK (0): mic/VAC input peak.
    // #160: unit is plain "dB" to match Thetis (console.cs:23888); the
    // Thetis multimeter MIC reads the peak meter, same as here.
    computeLevelMeterFromDb(wdsp_ ? wdsp_->txMeterRaw(0)
                                  : std::numeric_limits<double>::quiet_NaN(),
                            kMicDbMin, /*danger=*/-6.0,
                            QStringLiteral("dB"));
}

void MeterModel::computeLvlPk() {
    // #160: Thetis "Leveler" meter reads the AVERAGE — TXA_LVLR_AV (5) —
    // labelled "dB" (dsp.cs:1011, console.cs:23888).  Reads its floor
    // while the leveler is bypassed; live once the leveler runs.
    computeLevelMeterFromDb(wdsp_ ? wdsp_->txMeterRaw(5)
                                  : std::numeric_limits<double>::quiet_NaN(),
                            kMicDbMin, /*danger=*/-6.0,
                            QStringLiteral("dB"));
}

// computeCfcPk / computeCfcG / computeComp intentionally absent —
// Combinator (Task #51, v0.2.1) replaces both WDSP CFC and
// compress.c blocks as a Lyra-native pre-processor; its meters
// will land on Lyra-native Source entries, not WDSP TXA indices.

void MeterModel::computeAlcPk() {
    // #160: Thetis "ALC" meter reads the AVERAGE — TXA_ALC_AV (13), NOT
    // the peak (12) — and labels it "dB" (dsp.cs:1005, console.cs:23888).
    // (Thetis only uses ALC_PK when the operator's peak-meter toggle is
    // on; Lyra has no such toggle, so we follow the average default.)
    computeLevelMeterFromDb(wdsp_ ? wdsp_->txMeterRaw(13)
                                  : std::numeric_limits<double>::quiet_NaN(),
                            kMicDbMin, /*danger=*/-6.0,
                            QStringLiteral("dB"));
}

void MeterModel::computeAlcGroup() {
    computeLevelMeterFromDb(std::numeric_limits<double>::quiet_NaN(),
                            kMicDbMin, /*danger=*/-6.0,
                            QStringLiteral("dB"));
}

void MeterModel::computeLvlG() {
    // #158 (post-DL) re-home — TXA_LVLR_GAIN (6): leveler gain applied, dB.
    computeGainMeterFromDb(wdsp_ ? wdsp_->txMeterRaw(6)
                                 : std::numeric_limits<double>::quiet_NaN(),
                           kGainDbMax, /*danger=*/12.0);
}

void MeterModel::computeAlcG() {
    // ALC-G headroom number — Thetis-EXACT (operator decision 2026-06-15,
    // supersedes the §15.27 20·log10 conversion below).
    //
    // Reference (dsp.cs:1019-1020 + console.cs:24779 clamp):
    //   raw       = GetTXAMeter(TXA_ALC_GAIN)
    //   displayed = max(0, raw + alcgain)
    // where `alcgain` is the operator's ALC-max-gain value — the SAME
    // number Thetis hands to SetTXAALCMaxGain — added DIRECTLY (Thetis
    // adds its Setup-spinner number as-is; it does NOT 20·log10-convert,
    // even though raw is in dB — a deliberate reference quirk).
    //
    // Lyra stores that value as alcMaxGainLinear() (§15.27) and feeds the
    // identical number to SetTXAALCMaxGain (main.cpp / Task #38), so
    // adding alcMaxGainLinear() directly reproduces Thetis bit-for-bit.
    // The earlier §15.27 `20·log10(linear)` step was dimensionally
    // "tidier" but DIVERGED from Thetis — removed for reference parity.
    double disp = std::numeric_limits<double>::quiet_NaN();
    if (wdsp_) {
        const double raw = wdsp_->txMeterRaw(14);
        if (!std::isnan(raw)) {
            const double alcMaxGain =
                stream_ ? stream_->alcMaxGainLinear() : 3.0;
            disp = std::max(0.0, raw + alcMaxGain);
        }
    }
    computeGainMeterFromDb(disp, kGainDbMax, /*danger=*/6.0);
}

} // namespace lyra::ui

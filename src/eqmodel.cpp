// Lyra — EqModel implementation.  See eqmodel.h.

#include "eqmodel.h"

#include <algorithm>

#include <QJsonArray>

#include <QTimer>
#include <QVariant>

namespace lyra::ui {

namespace {
using DType = lyra::dsp::ParamEq::Type;
constexpr double kFreqMin = 20.0;
constexpr double kFreqMax = 20000.0;
constexpr double kGainMax = 15.0;     // ±15 dB drag range (matches the curve axis)
constexpr double kQMin = 0.2;
constexpr double kQMax = 10.0;
// Largest mic block the analyzer pre-tap copies in one go (HL2 rack blocks
// are far smaller; oversize blocks just skip the analyzer, never the EQ).
constexpr int kMaxAnalyzerBlock = 4096;
}  // namespace

// Active engine + analyzer for the wire-layer TX pump (txProcessCb).  One
// EqModel exists for the app lifetime (MainWindow member); the TX thread
// reads these lock-free.
std::atomic<lyra::dsp::ParamEq *>    EqModel::s_txEngine{nullptr};
std::atomic<lyra::dsp::EqAnalyzer *> EqModel::s_txAnalyzer{nullptr};
EqModel *EqModel::s_self = nullptr;

EqModel::EqModel(Side side, QObject *parent) : QObject(parent), side_(side) {
    eq_.setSampleRate(48000.0);        // HL2 codec / TX rack / RX audio = 48 kHz
    analyzer_.setSampleRate(48000.0);
    // Only the TX side owns the process-global wire-pump statics (txProcessCb
    // routes the mic rack through them).  The RX side is wired directly into
    // WdspEngine (R-2) and must NOT clobber the TX statics.
    if (side_ == Side::Tx) {
        s_txEngine.store(&eq_, std::memory_order_release);
        s_txAnalyzer.store(&analyzer_, std::memory_order_release);
        s_self = this;
    }

    // Pre-size the cached bin lists so QML always sees a stable length.
    const int nb = analyzer_.bins();
    for (int i = 0; i < nb; ++i) { preList_.append(-140.0); postList_.append(-140.0); }

    // GUI-thread poll: drain the analyzer's latest frame ~25 Hz and emit
    // spectrumChanged so EqPanel repaints (no-op when no new frame, i.e. RX).
    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(40);
    connect(pollTimer_, &QTimer::timeout, this, &EqModel::pollSpectrum);
    pollTimer_->start();
}

EqModel::~EqModel() {
    // Stop routing TX audio through this engine/analyzer before destruction —
    // TX side only, so the RX instance's dtor can't null the TX statics.
    if (side_ == Side::Tx) {
        s_txEngine.store(nullptr, std::memory_order_release);
        s_txAnalyzer.store(nullptr, std::memory_order_release);
        if (s_self == this) s_self = nullptr;
    }
}

void EqModel::txProcessCb(int nSamples, double *iqPairs) {
    auto *e = s_txEngine.load(std::memory_order_acquire);
    auto *a = s_txAnalyzer.load(std::memory_order_acquire);
    if (a && nSamples > 0 && nSamples <= kMaxAnalyzerBlock) {
        // Snapshot the pre-EQ I channel, run the EQ in place, then feed both
        // pre + post to the analyzer in lockstep (sample-aligned frames).
        thread_local double preBuf[kMaxAnalyzerBlock];
        for (int k = 0; k < nSamples; ++k) preBuf[k] = iqPairs[2 * k];
        if (e) e->processInterleaved(iqPairs, nSamples);
        a->feed(preBuf, iqPairs, nSamples);
    } else if (e) {
        e->processInterleaved(iqPairs, nSamples);
    }
}

void EqModel::setAnalyzerMode(int mode) {
    mode = std::clamp(mode, 0, 2);
    if (mode == analyzerMode_) return;
    analyzerMode_ = mode;
    emit spectrumOptsChanged();
}
void EqModel::setAccumulate(bool on) {
    if (on == accumulate_) return;
    accumulate_ = on;
    emit spectrumOptsChanged();
}
void EqModel::setBeforeAfterMod(bool on) {
    if (on == beforeAfter_) return;
    beforeAfter_ = on;
    emit spectrumOptsChanged();
}
void EqModel::setPeakDecayMode(int mode) {
    mode = std::clamp(mode, 0, 4);
    if (mode == peakDecay_) return;
    peakDecay_ = mode;
    emit spectrumOptsChanged();
}

void EqModel::pollSpectrum() {
    if (!analyzer_.snapshot(preBins_, postBins_)) return;   // no new frame
    const int nb = static_cast<int>(postBins_.size());
    preList_.clear();  postList_.clear();
    preList_.reserve(nb);  postList_.reserve(nb);
    for (int i = 0; i < nb; ++i) {
        preList_.append(static_cast<double>(preBins_[i]));
        postList_.append(static_cast<double>(postBins_[i]));
    }
    emit spectrumChanged();
}

void EqModel::setBypass(bool on) {
    if (on == bypass_) return;
    bypass_ = on;
    eq_.setBypass(on);
    emit bypassChanged();
    emit bandsChanged();          // curve dims/flattens
}

void EqModel::setMakeupDb(double db) {
    db = std::clamp(db, -24.0, 24.0);
    if (db == makeupDb_) return;
    makeupDb_ = db;
    eq_.setMakeupDb(db);
    emit makeupDbChanged();
    emit bandsChanged();
}

void EqModel::setSelectedBand(int i) {
    if (i == selected_) return;
    selected_ = i;
    emit selectedBandChanged();
}

bool   EqModel::bandEnabled(int i) const { return valid(i) && eq_.band(i).enabled; }
int    EqModel::bandType(int i) const {
    return valid(i) ? static_cast<int>(eq_.band(i).type) : 0;
}
double EqModel::bandFreq(int i) const { return valid(i) ? eq_.band(i).freqHz : 0.0; }
double EqModel::bandGain(int i) const { return valid(i) ? eq_.band(i).gainDb : 0.0; }
double EqModel::bandQ(int i) const { return valid(i) ? eq_.band(i).q : 1.0; }

void EqModel::setBandEnabled(int i, bool on) {
    if (!valid(i)) return;
    auto b = eq_.band(i);
    if (b.enabled == on) return;
    b.enabled = on;
    eq_.setBand(i, b);
    emit bandsChanged();
}

void EqModel::setBandType(int i, int type) {
    if (!valid(i) || type < 0 || type > static_cast<int>(DType::Notch)) return;
    auto b = eq_.band(i);
    b.type = static_cast<DType>(type);
    eq_.setBand(i, b);
    emit bandsChanged();
}

void EqModel::setBandFreq(int i, double hz) {
    if (!valid(i)) return;
    hz = std::clamp(hz, kFreqMin, kFreqMax);
    auto b = eq_.band(i);
    b.freqHz = hz;
    eq_.setBand(i, b);
    emit bandsChanged();
}

void EqModel::setBandGain(int i, double db) {
    if (!valid(i)) return;
    db = std::clamp(db, -kGainMax, kGainMax);
    auto b = eq_.band(i);
    b.gainDb = db;
    eq_.setBand(i, b);
    emit bandsChanged();
}

void EqModel::setBandQ(int i, double q) {
    if (!valid(i)) return;
    q = std::clamp(q, kQMin, kQMax);
    auto b = eq_.band(i);
    b.q = q;
    eq_.setBand(i, b);
    emit bandsChanged();
}

void EqModel::resetBand(int i) {
    if (!valid(i)) return;
    lyra::dsp::ParamEq fresh;     // its constructor seeds the default layout
    eq_.setBand(i, fresh.band(i));
    emit bandsChanged();
}

void EqModel::resetAll() {
    lyra::dsp::ParamEq fresh;     // its constructor seeds the default layout
    for (int i = 0; i < lyra::dsp::ParamEq::kNumBands; ++i)
        eq_.setBand(i, fresh.band(i));
    emit bandsChanged();          // one redraw for the whole flatten
}

QJsonObject EqModel::saveState() const {
    QJsonObject o;
    o["bypass"]   = bypass_;
    o["makeupDb"] = makeupDb_;
    QJsonArray bands;
    for (int i = 0; i < lyra::dsp::ParamEq::kNumBands; ++i) {
        QJsonObject b;
        b["en"]   = bandEnabled(i);
        b["type"] = bandType(i);
        b["f"]    = bandFreq(i);
        b["g"]    = bandGain(i);
        b["q"]    = bandQ(i);
        bands.append(b);
    }
    o["bands"] = bands;
    return o;
}

void EqModel::loadState(const QJsonObject &o) {
    if (o.isEmpty()) return;                       // tolerant: no-op on empty
    if (o.contains("bypass"))   setBypass(o["bypass"].toBool(bypass_));
    if (o.contains("makeupDb")) setMakeupDb(o["makeupDb"].toDouble(makeupDb_));
    const QJsonArray bands = o["bands"].toArray();
    for (int i = 0; i < bands.size() && i < lyra::dsp::ParamEq::kNumBands; ++i) {
        const QJsonObject b = bands[i].toObject();
        if (b.contains("type")) setBandType(i, b["type"].toInt(bandType(i)));
        if (b.contains("f"))    setBandFreq(i, b["f"].toDouble(bandFreq(i)));
        if (b.contains("g"))    setBandGain(i, b["g"].toDouble(bandGain(i)));
        if (b.contains("q"))    setBandQ(i, b["q"].toDouble(bandQ(i)));
        if (b.contains("en"))   setBandEnabled(i, b["en"].toBool(bandEnabled(i)));
    }
}

double EqModel::magnitudeDb(double freqHz) const { return eq_.magnitudeDb(freqHz); }

QString EqModel::typeName(int type) const {
    switch (static_cast<DType>(type)) {
    case DType::Peak:      return QStringLiteral("Peak");
    case DType::LowShelf:  return QStringLiteral("Lo-Shelf");
    case DType::HighShelf: return QStringLiteral("Hi-Shelf");
    case DType::LowPass:   return QStringLiteral("Lo-Pass");
    case DType::HighPass:  return QStringLiteral("Hi-Pass");
    case DType::BandPass:  return QStringLiteral("Bandpass");
    case DType::Notch:     return QStringLiteral("Notch");
    }
    return QStringLiteral("?");
}

bool EqModel::typeUsesGain(int type) const {
    const auto t = static_cast<DType>(type);
    // Peak + shelves boost/cut; Notch is a cut whose DEPTH is its gain, so
    // its node rides gain too (drag down = deeper).  LP/HP/BP are slope/
    // width only — no gain handle.
    return t == DType::Peak || t == DType::LowShelf
           || t == DType::HighShelf || t == DType::Notch;
}

}  // namespace lyra::ui

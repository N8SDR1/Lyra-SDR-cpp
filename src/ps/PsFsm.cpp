#include "ps/PsFsm.h"
#include "ps/PsCalcThread.h"

#include "wire/wdspcalls.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace lyra::ps {

namespace {
constexpr int kTxa = 1;

int peakToDbfs(float peak, int spr) {
    if (spr <= 0) return -999;
    if (peak <= 1.0e-12f) return -120;
    return static_cast<int>(
        std::lround(20.0 * std::log10(static_cast<double>(peak))));
}
}

PsFsm::PsFsm(QObject *parent) : QObject(parent) {
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, &PsFsm::onTick);
}

QVariantList PsFsm::info() const {
    QVariantList out;
    out.reserve(16);
    for (int v : info_)
        out.append(v);
    return out;
}

void PsFsm::setAttnWriter(std::function<void(int)> fn) {
    attnWriter_ = std::move(fn);
}

void PsFsm::setAttnRange(int minDb, int maxDb, int moxSeed) {
    if (maxDb < minDb) return;
    attMinDb_ = minDb;
    attMaxDb_ = maxDb;
    moxSeedAttDb_ = std::clamp(moxSeed, minDb, maxDb);
    lastAttDb_ = moxSeedAttDb_;
    prevCalCount_ = -1;
}

void PsFsm::setFeedbackRateHz(int hz) {
    if (hz <= 0) return;
    feedbackRateHz_ = hz;
    if (armed_ && lyra::wire::SetPSFeedbackRate)
        lyra::wire::SetPSFeedbackRate(kTxa, feedbackRateHz_);
}

void PsFsm::setArmed(bool on) {
    if (armed_ == on) return;
    armed_ = on;
    if (on) pushArm();
    else pushDisarm();
}

void PsFsm::setMox(bool on) {
    mox_ = on;
    if (on) {
        // Keep last pad (Thetis ATTOnTX / DeskHPSDR band ps_tx_att). Do not
        // reseed to 0/31 on every PTT — that dumps FB to 300+ then hunts.
        prevCalCount_ = calCount_;
        if (attnWriter_)
            attnWriter_(lastAttDb_);
        emit telemetryChanged();
    }
    if (lyra::wire::SetPSMox)
        lyra::wire::SetPSMox(kTxa, on ? 1 : 0);
}

void PsFsm::reset() {
    if (!lyra::wire::SetPSControl) return;
    lyra::wire::SetPSControl(kTxa, 1, 0, 0, 0);
    if (armed_)
        lyra::wire::SetPSControl(kTxa, 0, 0, 1, 0);
}

void PsFsm::setHwPeak(double peak) {
    if (peak < 0.05) peak = 0.05;
    if (peak > 1.0) peak = 1.0;
    hwPeak_ = peak;
    if (lyra::wire::SetPSHWPeak)
        lyra::wire::SetPSHWPeak(kTxa, hwPeak_);
}

void PsFsm::captureGetPk() {
    getPkHold_ = maxTx_;
    emit telemetryChanged();
}

void PsFsm::setPlotHeld(bool on) {
    if (plotHeld_ == on) return;
    plotHeld_ = on;
    emit telemetryChanged();
}

void PsFsm::pushArm() {
    using namespace lyra::wire;
    if (SetPSHWPeak) SetPSHWPeak(kTxa, hwPeak_);
    if (SetPSMoxDelay) SetPSMoxDelay(kTxa, 0.2);
    if (SetPSFeedbackRate) SetPSFeedbackRate(kTxa, feedbackRateHz_);
    if (SetPSControl) {
        SetPSControl(kTxa, 1, 0, 0, 0);
        SetPSControl(kTxa, 0, 0, 1, 0);
    }
    if (SetPSRunCal) SetPSRunCal(kTxa, 1);
    timer_.start();
}

void PsFsm::pushDisarm() {
    using namespace lyra::wire;
    timer_.stop();
    if (SetPSRunCal) SetPSRunCal(kTxa, 0);
    if (SetPSControl) SetPSControl(kTxa, 1, 0, 0, 0);
    correcting_ = false;
    ddc0Dbfs_ = -999;
    ddc1Dbfs_ = -999;
    feedSpr_ = 0;
    maxTx_ = 0.0;
    std::memset(info_, 0, sizeof(info_));
    ampMagX_.clear();
    ampMagY_.clear();
    ampCorrX_.clear();
    ampCorrY_.clear();
    magEwma_.clear();
    corrEwma_.clear();
    plotHeld_ = false;
    emit telemetryChanged();
}

void PsFsm::onTick() {
    if (!armed_ || !lyra::wire::GetPSInfo) return;
    int info[16] = {};
    lyra::wire::GetPSInfo(kTxa, info);
    std::memcpy(info_, info, sizeof(info_));
    feedbackLevel_ = info[4];
    calCount_ = info[5];
    correcting_ = info[14] != 0;
    fsmState_ = info[15];
    const auto d = PsCalcThread::instance().takeFeedDiag();
    feedSpr_   = d.spr;
    ddc0Dbfs_  = peakToDbfs(d.peakDdc0, d.spr);
    ddc1Dbfs_  = peakToDbfs(d.peakDdc1, d.spr);
    if (lyra::wire::GetPSMaxTX)
        lyra::wire::GetPSMaxTX(kTxa, &maxTx_);
    pollAmpPlot();
    emit telemetryChanged();

    if (!mox_ || !attnWriter_) return;
    // Thetis only steps auto-att when info[5] (cal attempts) changes — not
    // on every 250 ms FB sample. SSB voice otherwise walks ATT with the
    // envelope (300 on peaks, ~0 in pauses).
    if (calCount_ == prevCalCount_) return;
    prevCalCount_ = calCount_;
    const int fb = feedbackLevel_;
    const bool need =
        fb > 181 || (fb <= 128 && lastAttDb_ > attMinDb_);
    if (!need) return;
    int delta = 0;
    if (fb > 256) {
        delta = (attMaxDb_ >= 31 && attMinDb_ >= 0) ? 15 : 10;
    } else if (fb > 0) {
        delta = static_cast<int>(
            std::lround(20.0 * std::log10(static_cast<double>(fb) / 152.293)));
    }
    int next = lastAttDb_ + delta;
    if (next < attMinDb_) next = attMinDb_;
    if (next > attMaxDb_) next = attMaxDb_;
    if (next == lastAttDb_) return;
    lastAttDb_ = next;
    attnWriter_(next);
    if (lyra::wire::SetPSControl) {
        lyra::wire::SetPSControl(kTxa, 1, 0, 0, 0);
        lyra::wire::SetPSControl(kTxa, 0, 0, 1, 0);
    }
    emit telemetryChanged();
}

void PsFsm::pollAmpPlot() {
    using namespace lyra::wire;
    if (!GetPSDisp || plotHeld_) return;

    constexpr int kInts = 16;
    constexpr int kSpi  = 256;
    constexpr int kN    = kInts * kSpi;
    constexpr int kCoef = 4 * kInts;
    constexpr int kCorr = 256;
    constexpr int kDispPts = 512;
    constexpr double kSmooth = 0.22;

    if (dispX_.size() != static_cast<size_t>(kN)) {
        dispX_.assign(kN, 0.0);
        dispYm_.assign(kN, 0.0);
        dispYc_.assign(kN, 0.0);
        dispYs_.assign(kN, 0.0);
        dispCm_.assign(kCoef, 0.0);
        dispCc_.assign(kCoef, 0.0);
        dispCs_.assign(kCoef, 0.0);
        dispXmCor_.assign(kDispPts, 0.0);
        dispYmCor_.assign(kDispPts, 0.0);
        dispXaCor_.assign(kDispPts, 0.0);
        dispYaCor_.assign(kDispPts, 0.0);
    }

    int nsamps = 0;
    int cpts = 0;
    double phsRef = 0.0;
    get_ps_disp(kTxa,
                dispX_.data(), dispYm_.data(), dispYc_.data(), dispYs_.data(),
                dispCm_.data(), dispCc_.data(), dispCs_.data(),
                dispXmCor_.data(), dispYmCor_.data(),
                dispXaCor_.data(), dispYaCor_.data(),
                &nsamps, &cpts, &phsRef);
    (void)phsRef;

    int scatterN = kN;
    if (nsamps > 0)
        scatterN = std::min(nsamps, kN);

    constexpr int kBins = 96;
    std::vector<double> binSum(kBins, 0.0);
    std::vector<int>    binN(kBins, 0);
    for (int i = 0; i < scatterN; ++i) {
        const double xin = dispX_[static_cast<size_t>(i)];
        const double g   = dispYm_[static_cast<size_t>(i)];
        if (xin <= 0.0 || g <= 0.0) continue;
        int b = static_cast<int>(xin * kBins);
        if (b < 0) b = 0;
        if (b >= kBins) b = kBins - 1;
        binSum[static_cast<size_t>(b)] += g * xin;
        binN[static_cast<size_t>(b)] += 1;
    }
    if (magEwma_.size() != static_cast<size_t>(kBins))
        magEwma_.assign(kBins, 0.0);
    ampMagX_.clear();
    ampMagY_.clear();
    ampMagX_.reserve(kBins);
    ampMagY_.reserve(kBins);
    for (int b = 0; b < kBins; ++b) {
        const size_t bi = static_cast<size_t>(b);
        if (binN[bi] > 0) {
            const double y = binSum[bi] / static_cast<double>(binN[bi]);
            magEwma_[bi] = (magEwma_[bi] <= 0.0)
                ? y
                : kSmooth * y + (1.0 - kSmooth) * magEwma_[bi];
        }
        if (magEwma_[bi] <= 0.0) continue;
        ampMagX_.append((static_cast<double>(b) + 0.5)
                        / static_cast<double>(kBins));
        ampMagY_.append(magEwma_[bi]);
    }

    ampCorrX_.clear();
    ampCorrY_.clear();
    if (cpts > 0) {
        const int n = std::min(cpts, kDispPts);
        if (corrEwma_.size() != static_cast<size_t>(n))
            corrEwma_.assign(static_cast<size_t>(n), 0.0);
        ampCorrX_.reserve(n);
        ampCorrY_.reserve(n);
        for (int i = 0; i < n; ++i) {
            const size_t ii = static_cast<size_t>(i);
            const double qx = dispXmCor_[ii];
            const double y  = dispYmCor_[ii];
            corrEwma_[ii] = (corrEwma_[ii] <= 0.0)
                ? y
                : kSmooth * y + (1.0 - kSmooth) * corrEwma_[ii];
            ampCorrX_.append(qx);
            ampCorrY_.append(corrEwma_[ii]);
        }
        return;
    }

    if (corrEwma_.size() != static_cast<size_t>(kCorr))
        corrEwma_.assign(kCorr, 0.0);
    ampCorrX_.reserve(kCorr);
    ampCorrY_.reserve(kCorr);
    for (int i = 0; i < kCorr; ++i) {
        const double qx = (kCorr > 1)
            ? static_cast<double>(i) / static_cast<double>(kCorr - 1)
            : 0.0;
        int k = static_cast<int>(qx * kInts);
        if (k > kInts - 1) k = kInts - 1;
        const double dx = qx - static_cast<double>(k) / static_cast<double>(kInts);
        const double *c = dispCm_.data() + 4 * k;
        const double qym = c[0] + dx * (c[1] + dx * (c[2] + dx * c[3]));
        const double y = qym * qx;
        const size_t ii = static_cast<size_t>(i);
        corrEwma_[ii] = (corrEwma_[ii] <= 0.0)
            ? y
            : kSmooth * y + (1.0 - kSmooth) * corrEwma_[ii];
        ampCorrX_.append(qx);
        ampCorrY_.append(corrEwma_[ii]);
    }
}

}  // namespace lyra::ps

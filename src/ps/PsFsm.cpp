#include "ps/PsFsm.h"
#include "ps/PsCalcThread.h"

#include "wire/wdspcalls.h"

#include <cmath>

namespace lyra::ps {

namespace {
constexpr int kTxa = 1;
constexpr double kHwPeak = 0.233;

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

void PsFsm::setAttnWriter(std::function<void(int)> fn) {
    attnWriter_ = std::move(fn);
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
    if (on)
        lastAttDb_ = 31;  // ATT-on-TX floor; auto-att writes the same actuator
    if (lyra::wire::SetPSMox)
        lyra::wire::SetPSMox(kTxa, on ? 1 : 0);
}

void PsFsm::reset() {
    if (!lyra::wire::SetPSControl) return;
    lyra::wire::SetPSControl(kTxa, 1, 0, 0, 0);
    if (armed_)
        lyra::wire::SetPSControl(kTxa, 0, 0, 1, 0);
}

void PsFsm::pushArm() {
    using namespace lyra::wire;
    if (SetPSHWPeak) SetPSHWPeak(kTxa, kHwPeak);
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
    emit telemetryChanged();
}

void PsFsm::onTick() {
    if (!armed_ || !lyra::wire::GetPSInfo) return;
    int info[16] = {};
    lyra::wire::GetPSInfo(kTxa, info);
    feedbackLevel_ = info[4];
    calCount_ = info[5];
    correcting_ = info[14] != 0;
    fsmState_ = info[15];
    const auto d = PsCalcThread::instance().takeFeedDiag();
    feedSpr_   = d.spr;
    ddc0Dbfs_  = peakToDbfs(d.peakDdc0, d.spr);
    ddc1Dbfs_  = peakToDbfs(d.peakDdc1, d.spr);
    emit telemetryChanged();

    if (!mox_ || !attnWriter_) return;
    const int fb = feedbackLevel_;
    const bool need =
        fb > 181 || (fb <= 128 && lastAttDb_ > -28);
    if (!need) return;
    int delta = 0;
    if (fb > 0) {
        delta = static_cast<int>(
            std::lround(20.0 * std::log10(static_cast<double>(fb) / 152.293)));
    }
    int next = lastAttDb_ + delta;
    if (next < -28) next = -28;
    if (next > 31) next = 31;
    if (next == lastAttDb_) return;
    lastAttDb_ = next;
    attnWriter_(next);
}

}  // namespace lyra::ps

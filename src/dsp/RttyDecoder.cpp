#include "dsp/RttyDecoder.h"

#include <algorithm>
#include <cmath>

namespace lyra::dsp {

namespace {
constexpr int    kNTaps   = 49;
constexpr double kCutHz   = 3400.0;
constexpr double kPi      = 3.14159265358979323846;

inline double sinc(double x) {
    return (std::fabs(x) < 1e-9) ? 1.0 : std::sin(kPi * x) / (kPi * x);
}
}  // namespace

RttyDecoder::RttyDecoder() {
    rx_.onText = [this](const std::string& s) { if (onText) onText(s); };
    rebuildDecimator();
}

void RttyDecoder::rebuildDecimator() {
    decim_ = std::max(1, (int)std::lround(inRate_ / 8000.0));

    firCoef_.assign(kNTaps, 0.0);
    const double fc = kCutHz / inRate_;
    const int    M  = kNTaps - 1;
    double sum = 0.0;
    for (int i = 0; i < kNTaps; ++i) {
        const double w = 0.54 - 0.46 * std::cos(2.0 * kPi * i / M);
        const double h = 2.0 * fc * sinc(2.0 * fc * (i - M / 2.0)) * w;
        firCoef_[i] = h;
        sum += h;
    }
    if (sum != 0.0) for (double& c : firCoef_) c /= sum;

    hist_.assign(kNTaps, 0.0);
    histPos_ = 0;
    phase_   = 0;
    out8k_.clear();
    out8k_.reserve(2048);
}

void RttyDecoder::setSampleRate(double hz) {
    if (hz > 0.0 && hz != inRate_) { inRate_ = hz; rebuildDecimator(); }
}
void RttyDecoder::setCenterHz(double hz) { rx_.setCenterHz(hz); }
void RttyDecoder::setShiftHz(double hz)  { rx_.setShiftHz(hz); }
void RttyDecoder::setBaud(double baud)   { rx_.setBaud(baud); }
void RttyDecoder::setReverse(bool on)    { rx_.setReverse(on); }
void RttyDecoder::setSquelch(bool on, double value) { rx_.setSquelch(on, value); }

void RttyDecoder::reset() {
    rx_.reset();
    hist_.assign(kNTaps, 0.0);
    histPos_ = 0;
    phase_   = 0;
    out8k_.clear();
}

void RttyDecoder::process(const float* mono, int nframes) {
    if (nframes <= 0) return;
    out8k_.clear();
    for (int i = 0; i < nframes; ++i) {
        hist_[histPos_] = (double)mono[i];
        histPos_ = (histPos_ + 1) % kNTaps;
        if (++phase_ >= decim_) {
            phase_ = 0;
            double y = 0.0;
            int idx = (histPos_ - 1 + kNTaps) % kNTaps;
            for (int k = 0; k < kNTaps; ++k) {
                y += firCoef_[k] * hist_[idx];
                idx = (idx - 1 + kNTaps) % kNTaps;
            }
            out8k_.push_back(y);
        }
    }
    if (!out8k_.empty())
        rx_.rxProcess(out8k_.data(), (int)out8k_.size());
}

}  // namespace lyra::dsp

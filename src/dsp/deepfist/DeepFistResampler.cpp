// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/DeepFistResampler.h"

#include <algorithm>
#include <cmath>

namespace lyra::dsp {

namespace {
// Matches the verified DeepFist reference port (diddle cw_neural.rs Decimator):
// 121 taps, 1440 Hz cutoff (~0.9 * the 1600 Hz output Nyquist).
constexpr int    kNTaps = 121;
constexpr double kCutHz = 1440.0;
constexpr double kPi    = 3.14159265358979323846;

inline double sinc(double x) {
    return (std::fabs(x) < 1e-9) ? 1.0 : std::sin(kPi * x) / (kPi * x);
}
}  // namespace

DeepFistResampler::DeepFistResampler(double inRate, double outRate)
    : inRate_(inRate), outRate_(outRate) {
    rebuild();
}

void DeepFistResampler::setInRate(double hz) {
    if (hz > 0.0 && hz != inRate_) { inRate_ = hz; rebuild(); }
}

void DeepFistResampler::rebuild() {
    decim_ = std::max(1, static_cast<int>(std::lround(inRate_ / outRate_)));

    fir_.assign(kNTaps, 0.0);
    const double fc = kCutHz / inRate_;       // normalised cutoff (0..0.5)
    const int    M  = kNTaps - 1;
    double sum = 0.0;
    for (int i = 0; i < kNTaps; ++i) {
        const double w = 0.54 - 0.46 * std::cos(2.0 * kPi * i / M);   // Hamming
        const double h = 2.0 * fc * sinc(2.0 * fc * (i - M / 2.0)) * w;
        fir_[i] = h;
        sum += h;
    }
    if (sum != 0.0) for (double& c : fir_) c /= sum;   // unity DC gain

    hist_.assign(kNTaps, 0.0);
    histPos_ = 0;
    phase_   = 0;
}

void DeepFistResampler::reset() {
    std::fill(hist_.begin(), hist_.end(), 0.0);
    histPos_ = 0;
    phase_   = 0;
}

void DeepFistResampler::process(const float* in, int nframes,
                                std::vector<float>& out) {
    if (!in || nframes <= 0) return;
    for (int i = 0; i < nframes; ++i) {
        hist_[histPos_] = static_cast<double>(in[i]);
        histPos_ = (histPos_ + 1) % kNTaps;
        if (++phase_ >= decim_) {
            phase_ = 0;
            double y = 0.0;
            int idx = (histPos_ - 1 + kNTaps) % kNTaps;   // newest sample
            for (int k = 0; k < kNTaps; ++k) {
                y += fir_[k] * hist_[idx];
                idx = (idx - 1 + kNTaps) % kNTaps;
            }
            out.push_back(static_cast<float>(y));
        }
    }
}

}  // namespace lyra::dsp

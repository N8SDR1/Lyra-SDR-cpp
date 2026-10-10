// Lyra — captured-profile noise reducer (apply).  See noisereducer.h.

#include "noisereducer.h"

#include <algorithm>
#include <cmath>

namespace lyra::dsp {

NoiseReducer::NoiseReducer(int fftSize) : stft_(fftSize), fftSize_(fftSize) {
    profPower_.assign(static_cast<size_t>(fftSize_), 0.0);
    gPrev_.assign(static_cast<size_t>(fftSize_), 1.0);
    buildGainHook();
    reset();
}

void NoiseReducer::buildGainHook() {
    stft_.setGain([this](std::vector<IqStft::Cplx> &spec) {
        if (!profileValid_) {
            return;   // no profile → identity (still WOLA-reconstructs)
        }
        const size_t n = spec.size();
        for (size_t k = 0; k < n; ++k) {
            const double py = std::norm(spec[k]);          // |Y|^2
            const double pn = profPower_[k];               // captured Pn
            // Power-subtraction → amplitude gain, phase preserved.
            double powerGain = 1.0 - alpha_ * pn / (py + 1e-20);
            double g = (powerGain > 0.0) ? std::sqrt(powerGain) : 0.0;
            if (g < floorLin_) g = floorLin_;
            if (g > 1.0)       g = 1.0;
            // Per-bin temporal smoothing (anti-musical-noise).
            g = smoothing_ * gPrev_[k] + (1.0 - smoothing_) * g;
            gPrev_[k] = g;
            spec[k] *= g;
        }
    });
}

void NoiseReducer::setProfile(const std::vector<double> &noisePower) {
    if (static_cast<int>(noisePower.size()) != fftSize_) {
        profileValid_ = false;
        return;
    }
    profPower_ = noisePower;
    profileValid_ = true;
    reset();
}

void NoiseReducer::setAlpha(double a) {
    alpha_ = std::clamp(a, 0.0, 6.0);
}

void NoiseReducer::setFloorDb(double db) {
    floorDb_  = std::clamp(db, -60.0, 0.0);
    floorLin_ = std::pow(10.0, floorDb_ / 20.0);
}

void NoiseReducer::setSmoothing(double s) {
    smoothing_ = std::clamp(s, 0.0, 0.99);
}

void NoiseReducer::reset() {
    stft_.reset();
    std::fill(gPrev_.begin(), gPrev_.end(), 1.0);
    // Prime the output FIFO with one full window of silence so the
    // same-count pull never underruns despite the WOLA hop granularity.
    outFifo_.assign(static_cast<size_t>(2 * fftSize_), 0.0);
}

void NoiseReducer::process(const double *in, int nframes, double *out) {
    if (nframes <= 0) {
        return;
    }
    scratch_.clear();
    stft_.process(in, nframes, scratch_);            // hook applies the gain
    outFifo_.insert(outFifo_.end(), scratch_.begin(), scratch_.end());
    const int want = 2 * nframes;
    int got = 0;
    while (got < want && !outFifo_.empty()) {
        out[got++] = outFifo_.front();
        outFifo_.pop_front();
    }
    while (got < want) {
        out[got++] = 0.0;   // FIFO underrun guard (should not occur post-prime)
    }
}

} // namespace lyra::dsp

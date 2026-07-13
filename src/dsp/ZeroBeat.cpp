// Lyra — zero-beat carrier-offset estimator.  See ZeroBeat.h.

#include "dsp/ZeroBeat.h"

#include <algorithm>
#include <cmath>

namespace lyra::dsp {

namespace {
constexpr double kPi        = 3.14159265358979323846;
constexpr double kScanStep  = 6.0;    // coarse scan step (Hz); parabola refines
constexpr double kWinSecs   = 0.043;  // analysis window (~23 updates/sec; ≥ a CW dit)
constexpr double kHoldSecs  = 0.35;   // hold the last lock this long across gaps
constexpr double kEwmaAlpha = 0.45;   // smoothing of the running offset
constexpr int    kWinMin    = 2048;
constexpr int    kWinMax    = 32768;
}  // namespace

void ZeroBeat::setSampleRate(double hz) {
    if (hz <= 0.0) return;
    if (std::abs(hz - sampleRate_) < 1.0 && !bufI_.empty()) return;
    sampleRate_ = hz;
    int n = static_cast<int>(std::lround(hz * kWinSecs));
    winSize_ = std::clamp(n, kWinMin, kWinMax);
    holdWindows_ = std::clamp(
        static_cast<int>(std::lround(kHoldSecs * hz / winSize_)), 2, 40);
    reset();
}

void ZeroBeat::setHalfRange(double hz) { halfRange_ = std::clamp(hz, 100.0, 5000.0); }
void ZeroBeat::setSnrGateDb(double db) { snrGate_ = db; }

void ZeroBeat::reset() {
    bufI_.assign(static_cast<size_t>(winSize_), 0.0);
    bufQ_.assign(static_cast<size_t>(winSize_), 0.0);
    fill_      = 0;
    valid_     = false;
    haveEwma_  = false;
    snrDb_     = 0.0;
    holdCount_ = 0;
}

void ZeroBeat::process(const double* iq, int nframes) {
    if (!iq || nframes <= 0) return;
    if (static_cast<int>(bufI_.size()) != winSize_) reset();
    for (int i = 0; i < nframes; ++i) {
        bufI_[static_cast<size_t>(fill_)] = iq[2 * i + 0];
        bufQ_[static_cast<size_t>(fill_)] = iq[2 * i + 1];
        if (++fill_ >= winSize_) {
            analyze_();
            fill_ = 0;
        }
    }
}

void ZeroBeat::analyze_() {
    const int    N  = winSize_;
    const double sr = sampleRate_;

    // Hann window in place (sidelobe control on the peak estimate).
    for (int i = 0; i < N; ++i) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (N - 1)));
        bufI_[static_cast<size_t>(i)] *= w;
        bufQ_[static_cast<size_t>(i)] *= w;
    }

    // Correlate the complex block against e^{-j2πf n/sr} for a scan of f,
    // using a per-bin sin/cos recurrence (no per-sample trig).  Complex input
    // ⇒ a carrier on either sideband shows as a signed offset.
    auto mag = [&](double freqHz) -> double {
        const double w  = 2.0 * kPi * freqHz / sr;
        const double cw = std::cos(w), sw = std::sin(w);
        double c = 1.0, s = 0.0, re = 0.0, im = 0.0;
        for (int i = 0; i < N; ++i) {
            const double I = bufI_[static_cast<size_t>(i)];
            const double Q = bufQ_[static_cast<size_t>(i)];
            re += I * c + Q * s;
            im += Q * c - I * s;
            const double cn = c * cw - s * sw;   // rotate (c,s) by +w
            s = s * cw + c * sw;
            c = cn;
        }
        return std::sqrt(re * re + im * im);
    };

    const double lo = std::max(-sr * 0.5 + 1.0, searchCenterHz_ - halfRange_);
    const double hi = std::min( sr * 0.5 - 1.0, searchCenterHz_ + halfRange_);
    if (hi <= lo) { valid_ = false; return; }
    const int nbins = static_cast<int>((hi - lo) / kScanStep) + 1;
    if (nbins < 3) { valid_ = false; return; }

    std::vector<double> m(static_cast<size_t>(nbins));
    int    peakIdx = 0;
    double peakMag = -1.0;
    for (int k = 0; k < nbins; ++k) {
        const double v = mag(lo + kScanStep * k);
        m[static_cast<size_t>(k)] = v;
        if (v > peakMag) { peakMag = v; peakIdx = k; }
    }

    // Noise floor = median of the scan (robust to the one strong carrier bin).
    std::vector<double> sorted(m);
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const double noise = std::max(1e-12, sorted[sorted.size() / 2]);
    snrDb_ = 20.0 * std::log10(peakMag / noise);

    if (snrDb_ >= snrGate_) {
        // Parabolic sub-bin refinement on the three bins around the peak.
        double peakHz = lo + kScanStep * peakIdx;
        if (peakIdx > 0 && peakIdx < nbins - 1) {
            const double a = m[static_cast<size_t>(peakIdx - 1)];
            const double b = m[static_cast<size_t>(peakIdx)];
            const double c = m[static_cast<size_t>(peakIdx + 1)];
            const double denom = (a - 2.0 * b + c);
            if (std::abs(denom) > 1e-12) {
                const double delta = 0.5 * (a - c) / denom;
                if (delta > -1.0 && delta < 1.0) peakHz += delta * kScanStep;
            }
        }
        if (!haveEwma_) { offsetHz_ = peakHz; haveEwma_ = true; }
        else            { offsetHz_ += kEwmaAlpha * (peakHz - offsetHz_); }
        valid_ = true;
        holdCount_ = holdWindows_;
    } else if (holdCount_ > 0) {
        --holdCount_;   // ride brief CW element gaps on the last good lock
        valid_ = true;
    } else {
        valid_ = false;
    }
}

} // namespace lyra::dsp

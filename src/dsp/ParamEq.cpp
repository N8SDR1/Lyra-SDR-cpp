// Lyra — native parametric EQ implementation.  See ParamEq.h.
//
// Coefficient math = Robert Bristow-Johnson "Audio EQ Cookbook" biquads
// (public-domain standard formulas).  Each band is one normalized biquad
// run in Transposed Direct Form II (good float behaviour, one mul/add per
// tap, state in z1/z2).

#include "dsp/ParamEq.h"

#include <cmath>
#include <complex>

namespace lyra::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Default starting bands — the EESDR3-style left->right layout from the
// approved mockup (HP, low-shelf, four peaks, high-shelf, LP).  Operator
// retunes from here; these are not locked (see project-lyra-cpp-eq memory).
constexpr ParamEq::Band kDefaults[ParamEq::kNumBands] = {
    { true, ParamEq::Type::HighPass,     60.0,  0.0, 0.707 },
    { true, ParamEq::Type::LowShelf,    150.0,  0.0, 0.707 },
    { true, ParamEq::Type::Peak,        300.0,  0.0, 1.0   },
    { true, ParamEq::Type::Peak,        500.0,  0.0, 1.0   },
    { true, ParamEq::Type::Peak,        850.0,  0.0, 1.0   },
    { true, ParamEq::Type::Peak,       1400.0,  0.0, 1.0   },
    { true, ParamEq::Type::Peak,       2300.0,  0.0, 1.0   },
    { true, ParamEq::Type::Peak,       3800.0,  0.0, 1.0   },
    { true, ParamEq::Type::HighShelf,  6000.0,  0.0, 0.707 },
    { true, ParamEq::Type::LowPass,   10000.0,  0.0, 0.707 },
};
}  // namespace

ParamEq::ParamEq() {
    for (int i = 0; i < kNumBands; ++i) bands_[i] = kDefaults[i];
    restage();
    active_ = staged_;          // start coherent (no first-block transient)
    stageDirty_.store(false, std::memory_order_relaxed);
}

ParamEq::Coeffs ParamEq::design(Type t, double fs, double f0, double q,
                                double gainDb) {
    Coeffs c;                                   // identity by default
    if (fs <= 0.0 || q <= 0.0) return c;
    // Clamp centre below Nyquist so cos/sin stay well-conditioned.
    const double nyq = 0.5 * fs;
    if (f0 < 1.0) f0 = 1.0;
    if (f0 > 0.995 * nyq) f0 = 0.995 * nyq;

    const double A   = std::pow(10.0, gainDb / 40.0);   // shelf/peak amplitude
    const double w0  = 2.0 * kPi * f0 / fs;
    const double cw  = std::cos(w0);
    const double sw  = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double sqrtA = std::sqrt(A);

    double b0, b1, b2, a0, a1, a2;
    switch (t) {
    case Type::Peak:
        b0 = 1.0 + alpha * A;  b1 = -2.0 * cw;  b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;  a1 = -2.0 * cw;  a2 = 1.0 - alpha / A;
        break;
    case Type::LowShelf:
        b0 =      A * ((A + 1) - (A - 1) * cw + 2 * sqrtA * alpha);
        b1 =  2 * A * ((A - 1) - (A + 1) * cw);
        b2 =      A * ((A + 1) - (A - 1) * cw - 2 * sqrtA * alpha);
        a0 =          (A + 1) + (A - 1) * cw + 2 * sqrtA * alpha;
        a1 =     -2 * ((A - 1) + (A + 1) * cw);
        a2 =          (A + 1) + (A - 1) * cw - 2 * sqrtA * alpha;
        break;
    case Type::HighShelf:
        b0 =      A * ((A + 1) + (A - 1) * cw + 2 * sqrtA * alpha);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 =      A * ((A + 1) + (A - 1) * cw - 2 * sqrtA * alpha);
        a0 =          (A + 1) - (A - 1) * cw + 2 * sqrtA * alpha;
        a1 =      2 * ((A - 1) - (A + 1) * cw);
        a2 =          (A + 1) - (A - 1) * cw - 2 * sqrtA * alpha;
        break;
    case Type::LowPass:
        b0 = (1.0 - cw) / 2.0;  b1 = 1.0 - cw;  b2 = (1.0 - cw) / 2.0;
        a0 = 1.0 + alpha;       a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    case Type::HighPass:
        b0 = (1.0 + cw) / 2.0;  b1 = -(1.0 + cw);  b2 = (1.0 + cw) / 2.0;
        a0 = 1.0 + alpha;       a1 = -2.0 * cw;    a2 = 1.0 - alpha;
        break;
    case Type::BandPass:                          // constant 0 dB peak gain
        b0 = alpha;        b1 = 0.0;        b2 = -alpha;
        a0 = 1.0 + alpha;  a1 = -2.0 * cw;  a2 = 1.0 - alpha;
        break;
    case Type::Notch:
        // True RBJ notch — a full null at f0; tunable is WIDTH (Q), not
        // depth (depth is inherently total).  Handle drags freq, wheel
        // sets Q; the UI clamps its node to the graph box so the null
        // stays grabbable at the bottom edge.
        b0 = 1.0;          b1 = -2.0 * cw;  b2 = 1.0;
        a0 = 1.0 + alpha;  a1 = -2.0 * cw;  a2 = 1.0 - alpha;
        break;
    default:
        return c;
    }
    c.b0 = b0 / a0;  c.b1 = b1 / a0;  c.b2 = b2 / a0;
    c.a1 = a1 / a0;  c.a2 = a2 / a0;
    return c;
}

void ParamEq::restage() {
    std::array<Coeffs, kNumBands> next{};
    for (int i = 0; i < kNumBands; ++i)
        next[i] = bands_[i].enabled
                  ? design(bands_[i].type, fs_, bands_[i].freqHz,
                           bands_[i].q, bands_[i].gainDb)
                  : Coeffs{};                     // disabled -> passthrough
    {
        std::lock_guard<std::mutex> lk(stageMtx_);
        staged_ = next;
    }
    stageDirty_.store(true, std::memory_order_release);
}

void ParamEq::setSampleRate(double fs) {
    if (fs > 0.0 && fs != fs_) { fs_ = fs; restage(); }
}

void ParamEq::setBypass(bool on) {
    bypass_.store(on, std::memory_order_relaxed);
}

void ParamEq::setMakeupDb(double db) {
    makeupLin_.store(std::pow(10.0, db / 20.0), std::memory_order_relaxed);
}

void ParamEq::setBand(int i, const Band &b) {
    if (i < 0 || i >= kNumBands) return;
    bands_[i] = b;
    restage();
}

ParamEq::Band ParamEq::band(int i) const {
    if (i < 0 || i >= kNumBands) return {};
    return bands_[i];
}

void ParamEq::reset() {
    for (auto &s : state_) { s.z1 = 0.0; s.z2 = 0.0; }
    for (auto &s : stateR_) { s.z1 = 0.0; s.z2 = 0.0; }
}

void ParamEq::process(float *x, int n) {
    // Publish any pending coeff update without blocking the audio thread.
    if (stageDirty_.load(std::memory_order_acquire)) {
        if (stageMtx_.try_lock()) {
            active_ = staged_;
            stageDirty_.store(false, std::memory_order_release);
            stageMtx_.unlock();
        }
    }
    if (bypass_.load(std::memory_order_relaxed)) return;
    const double mk = makeupLin_.load(std::memory_order_relaxed);

    for (int s = 0; s < n; ++s) {
        double y = static_cast<double>(x[s]);
        for (int b = 0; b < kNumBands; ++b) {
            const Coeffs &c = active_[b];
            State &st = state_[b];
            const double in = y;
            y = c.b0 * in + st.z1;                 // Transposed Direct Form II
            st.z1 = c.b1 * in - c.a1 * y + st.z2;
            st.z2 = c.b2 * in - c.a2 * y;
        }
        x[s] = static_cast<float>(y * mk);
    }
}

void ParamEq::processInterleaved(double *x, int n) {
    if (stageDirty_.load(std::memory_order_acquire)) {
        if (stageMtx_.try_lock()) {
            active_ = staged_;
            stageDirty_.store(false, std::memory_order_release);
            stageMtx_.unlock();
        }
    }
    if (bypass_.load(std::memory_order_relaxed)) return;
    const double mk = makeupLin_.load(std::memory_order_relaxed);

    for (int s = 0; s < n; ++s) {
        double y = x[2 * s];                   // I = mic; Q (2s+1) untouched
        for (int b = 0; b < kNumBands; ++b) {
            const Coeffs &c = active_[b];
            State &st = state_[b];
            const double in = y;
            y = c.b0 * in + st.z1;             // Transposed Direct Form II
            st.z1 = c.b1 * in - c.a1 * y + st.z2;
            st.z2 = c.b2 * in - c.a2 * y;
        }
        x[2 * s] = y * mk;
    }
}

void ParamEq::processMonoDup(double *x, int n) {
    if (stageDirty_.load(std::memory_order_acquire)) {
        if (stageMtx_.try_lock()) {
            active_ = staged_;
            stageDirty_.store(false, std::memory_order_release);
            stageMtx_.unlock();
        }
    }
    if (bypass_.load(std::memory_order_relaxed)) return;   // leave L==R untouched
    const double mk = makeupLin_.load(std::memory_order_relaxed);

    for (int s = 0; s < n; ++s) {
        double y = x[2 * s];                   // mono RX (L lane; R is the dup)
        for (int b = 0; b < kNumBands; ++b) {
            const Coeffs &c = active_[b];
            State &st = state_[b];
            const double in = y;
            y = c.b0 * in + st.z1;             // Transposed Direct Form II
            st.z1 = c.b1 * in - c.a1 * y + st.z2;
            st.z2 = c.b2 * in - c.a2 * y;
        }
        const double out = y * mk;
        x[2 * s + 0] = out;                    // write BOTH lanes — keep L==R
        x[2 * s + 1] = out;
    }
}

void ParamEq::processStereoIndependent(double *x, int n) {
    if (stageDirty_.load(std::memory_order_acquire)) {
        if (stageMtx_.try_lock()) {
            active_ = staged_;
            stageDirty_.store(false, std::memory_order_release);
            stageMtx_.unlock();
        }
    }
    if (bypass_.load(std::memory_order_relaxed)) return;
    const double mk = makeupLin_.load(std::memory_order_relaxed);

    for (int s = 0; s < n; ++s) {
        double yL = x[2 * s + 0];
        double yR = x[2 * s + 1];
        for (int b = 0; b < kNumBands; ++b) {
            const Coeffs &c = active_[b];
            {
                State &st = state_[b];
                const double in = yL;
                yL = c.b0 * in + st.z1;
                st.z1 = c.b1 * in - c.a1 * yL + st.z2;
                st.z2 = c.b2 * in - c.a2 * yL;
            }
            {
                State &st = stateR_[b];
                const double in = yR;
                yR = c.b0 * in + st.z1;
                st.z1 = c.b1 * in - c.a1 * yR + st.z2;
                st.z2 = c.b2 * in - c.a2 * yR;
            }
        }
        x[2 * s + 0] = yL * mk;
        x[2 * s + 1] = yR * mk;
    }
}

double ParamEq::magnitudeDb(double freqHz) const {
    const double mkDb = 20.0 * std::log10(
        makeupLin_.load(std::memory_order_relaxed));
    if (bypass_.load(std::memory_order_relaxed)) return mkDb;
    if (freqHz <= 0.0) freqHz = 1.0;

    const double w = 2.0 * kPi * freqHz / fs_;
    const std::complex<double> z1 = std::exp(std::complex<double>(0.0, -w));
    const std::complex<double> z2 = z1 * z1;

    double sumDb = mkDb;
    for (int i = 0; i < kNumBands; ++i) {
        if (!bands_[i].enabled) continue;
        const Coeffs c = design(bands_[i].type, fs_, bands_[i].freqHz,
                                bands_[i].q, bands_[i].gainDb);
        const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
        const std::complex<double> den = 1.0  + c.a1 * z1 + c.a2 * z2;
        const double mag = std::abs(num) / std::abs(den);
        // Floor (don't skip) a near-zero magnitude — a Notch's exact-centre
        // null is |H|->0, and skipping it made the summed curve read ~0 dB
        // at the null (the handle floated up out of its own notch).  Flooring
        // to -180 dB keeps the curve + node deep in the null.
        sumDb += 20.0 * std::log10(mag > 1e-9 ? mag : 1e-9);
    }
    return sumDb;
}

}  // namespace lyra::dsp

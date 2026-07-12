#include "dsp/deepfist/DeepFistConditioner.h"

#include <cmath>

namespace lyra::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

DeepFistConditioner::DeepFistConditioner() {
    // bit-reversal permutation for N = 4096 (12 bits)
    rev_.resize(kToneNfft);
    for (int i = 0; i < kToneNfft; ++i) {
        int r = 0, x = i;
        for (int b = 0; b < 12; ++b) { r = (r << 1) | (x & 1); x >>= 1; }
        rev_[i] = r;
    }
    tw_.resize(kToneNfft / 2);
    for (int k = 0; k < kToneNfft / 2; ++k)
        tw_[k] = std::complex<double>(std::cos(-2.0 * kPi * k / kToneNfft),
                                      std::sin(-2.0 * kPi * k / kToneNfft));
}

void DeepFistConditioner::fft(std::vector<std::complex<double>>& a) const {
    for (int i = 0; i < kToneNfft; ++i) {
        int j = rev_[i];
        if (j > i) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= kToneNfft; len <<= 1) {
        const int half = len >> 1;
        const int step = kToneNfft / len;
        for (int base = 0; base < kToneNfft; base += len) {
            for (int k = 0; k < half; ++k) {
                std::complex<double> w = tw_[k * step];
                std::complex<double> u = a[base + k];
                std::complex<double> v = a[base + k + half] * w;
                a[base + k]        = u + v;
                a[base + k + half] = u - v;
            }
        }
    }
}

float DeepFistConditioner::detectTone(const float* audio, int n) const {
    if (!audio || n < 8) return kOutPitch;
    const int m   = std::min(kToneNfft, n);
    const int off = n - m;                       // analyse the most recent samples

    std::vector<std::complex<double>> buf(kToneNfft, {0.0, 0.0});
    for (int i = 0; i < m; ++i)
        buf[i] = std::complex<double>(audio[off + i], 0.0);   // no window (matches Rust)
    fft(buf);

    const double binHz = static_cast<double>(kSr) / kToneNfft;
    int lo = std::max(1, static_cast<int>(std::floor(kBandLoHz / binHz)));
    int hi = std::min(kToneNfft / 2, static_cast<int>(std::ceil(kBandHiHz / binHz)));
    int    best = lo;
    double bv   = -1.0;
    for (int b = lo; b < hi; ++b) {
        const double mag = std::abs(buf[b]);
        if (mag > bv) { bv = mag; best = b; }
    }
    return static_cast<float>(best * binHz);
}

std::vector<float> DeepFistConditioner::condition(const float* audio, int n) const {
    return conditionAt(audio, n, detectTone(audio, n));
}

std::vector<float> DeepFistConditioner::conditionAt(const float* audio, int n,
                                                    float tone) const {
    if (!audio || n <= 0) return {};
    std::vector<float> out(static_cast<size_t>(n));
    if (n < kToneNfft) {                          // too short — pass through
        for (int k = 0; k < n; ++k) out[k] = audio[k];
        return out;
    }

    const float sr = static_cast<float>(kSr);
    const float pi = static_cast<float>(kPi);

    // 1. AGC — unit RMS
    float ss = 0.0f;
    for (int k = 0; k < n; ++k) ss += audio[k] * audio[k];
    const float rms = std::sqrt(ss / n) + 1e-9f;

    // 2/3. downconvert to baseband + two cascaded 1-pole LPFs (incremental phasors)
    const float alpha = 1.0f - std::exp(-2.0f * pi * (kCondBwHz * 0.5f) / sr);
    const float ds = std::sin(-2.0f * pi * tone / sr);
    const float dc = std::cos(-2.0f * pi * tone / sr);
    const float us = std::sin(2.0f * pi * kOutPitch / sr);
    const float uc = std::cos(2.0f * pi * kOutPitch / sr);
    float pr = 1.0f, pj = 0.0f;                   // downconvert phasor
    float ur = 1.0f, uj = 0.0f;                   // upconvert phasor
    float bI = 0.0f, bQ = 0.0f, cI = 0.0f, cQ = 0.0f;

    for (int k = 0; k < n; ++k) {
        const float s  = audio[k] / rms;
        const float di = s * pr, dq = s * pj;     // baseband I/Q
        bI += alpha * (di - bI);
        bQ += alpha * (dq - bQ);
        cI += alpha * (bI - cI);
        cQ += alpha * (bQ - cQ);
        out[k] = cI * ur - cQ * uj;                // recenter at kOutPitch, real part
        const float npr = pr * dc - pj * ds, npj = pr * ds + pj * dc;
        pr = npr; pj = npj;
        const float nur = ur * uc - uj * us, nuj = ur * us + uj * uc;
        ur = nur; uj = nuj;
    }

    // 5. peak-normalise (peak = max|out| + 1e-9, matching the Rust reference)
    float maxAbs = 0.0f;
    for (float v : out) maxAbs = std::max(maxAbs, std::fabs(v));
    const float peak = maxAbs + 1e-9f;
    for (float& v : out) v /= peak;
    return out;
}

}  // namespace lyra::dsp

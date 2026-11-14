// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/DeepFistConditioner.h"

#include <algorithm>
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
    return detectTone(audio, n, ToneSearch{});
}

float DeepFistConditioner::detectTone(const float* audio, int n,
                                      const ToneSearch& s) const {
    if (!audio || n < 8) return s.centerHz > 0.0f ? s.centerHz : kOutPitch;

    const double binHz = static_cast<double>(kSr) / kToneNfft;
    double loHz = kBandLoHz, hiHz = kBandHiHz;
    if (s.centerHz > 0.0f) {
        loHz = std::max<double>(kBandLoHz, s.centerHz - s.halfSpanHz);
        hiHz = std::min<double>(kBandHiHz, s.centerHz + s.halfSpanHz);
        if (hiHz <= loHz) return s.centerHz;     // pitch outside the band
    }
    const int lo = std::max(1, static_cast<int>(std::floor(loHz / binHz)));
    // Default range is the original exclusive [lo, hi); a pinned window is
    // inclusive of its top bin.
    const int hi = std::min(kToneNfft / 2,
                            static_cast<int>(std::ceil(hiHz / binHz))
                                + (s.centerHz > 0.0f ? 1 : 0));

    // Magnitude spectrum: newest 4096 samples (default, matches the trained
    // front-end), or the mean power over every 4096 block of the window.
    std::vector<double> mag(static_cast<size_t>(kToneNfft / 2 + 1), 0.0);
    std::vector<std::complex<double>> buf(kToneNfft);
    auto accumulate = [&](int off, int m) {
        std::fill(buf.begin(), buf.end(), std::complex<double>(0.0, 0.0));
        for (int i = 0; i < m; ++i)
            buf[i] = std::complex<double>(audio[off + i], 0.0);  // no window (matches Rust)
        fft(buf);
        for (int b = lo; b < hi; ++b) mag[static_cast<size_t>(b)] += std::norm(buf[b]);
    };
    if (s.wholeWindow && n > kToneNfft) {
        const int hop = kToneNfft / 2;
        for (int off = 0; off + kToneNfft <= n; off += hop) accumulate(off, kToneNfft);
    } else {
        const int m = std::min(kToneNfft, n);
        accumulate(n - m, m);                    // analyse the most recent samples
    }

    int    best = lo;
    double bv   = -1.0;
    for (int b = lo; b < hi; ++b) {
        if (mag[static_cast<size_t>(b)] > bv) { bv = mag[static_cast<size_t>(b)]; best = b; }
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

float DeepFistConditioner::keyingRatio(const float* audio, int n, float tone) const {
    if (!audio || n < kToneNfft) return 0.0f;
    const float sr = static_cast<float>(kSr);
    const float pi = static_cast<float>(kPi);
    // Downconvert `tone` to baseband + two cascaded 1-pole LPFs (same DSP as
    // conditionAt, but keep the baseband magnitude — the keying envelope — and
    // no AGC, since the ratio below is scale-free).
    const float alpha = 1.0f - std::exp(-2.0f * pi * (kCondBwHz * 0.5f) / sr);
    const float ds = std::sin(-2.0f * pi * tone / sr);
    const float dc = std::cos(-2.0f * pi * tone / sr);
    float pr = 1.0f, pj = 0.0f;
    float bI = 0.0f, bQ = 0.0f, cI = 0.0f, cQ = 0.0f;

    std::vector<float> env(static_cast<size_t>(n));
    for (int k = 0; k < n; ++k) {
        const float s = audio[k];
        const float di = s * pr, dq = s * pj;
        bI += alpha * (di - bI);
        bQ += alpha * (dq - bQ);
        cI += alpha * (bI - cI);
        cQ += alpha * (bQ - cQ);
        env[static_cast<size_t>(k)] = std::sqrt(cI * cI + cQ * cQ);
        const float npr = pr * dc - pj * ds, npj = pr * ds + pj * dc;
        pr = npr; pj = npj;
    }
    // Robust on/off levels: 90th / 10th percentiles so clicks/dropouts don't
    // skew.  ratio = p90/p10 (matches tools/squelch.py keying_ratio).
    std::sort(env.begin(), env.end());
    const float off = env[static_cast<size_t>(n / 10)];          // ~key-up gaps
    const float on  = env[static_cast<size_t>(n - 1 - n / 10)];  // ~key-down
    return on / (off + 1e-3f);
}

float DeepFistConditioner::estimateWpm(const float* audio, int n, float tone) const {
    if (!audio || n < kToneNfft) return 0.0f;
    const float sr = static_cast<float>(kSr);
    const float pi = static_cast<float>(kPi);
    const float alpha = 1.0f - std::exp(-2.0f * pi * (kCondBwHz * 0.5f) / sr);
    const float ds = std::sin(-2.0f * pi * tone / sr);
    const float dc = std::cos(-2.0f * pi * tone / sr);
    float pr = 1.0f, pj = 0.0f, bI = 0.0f, bQ = 0.0f, cI = 0.0f, cQ = 0.0f;

    std::vector<float> env(static_cast<size_t>(n));
    float peak = 1e-9f;
    for (int k = 0; k < n; ++k) {
        const float s = audio[k];
        const float di = s * pr, dq = s * pj;
        bI += alpha * (di - bI); bQ += alpha * (dq - bQ);
        cI += alpha * (bI - cI); cQ += alpha * (bQ - cQ);
        const float e = std::sqrt(cI * cI + cQ * cQ);
        env[static_cast<size_t>(k)] = e;
        peak = std::max(peak, e);
        const float npr = pr * dc - pj * ds, npj = pr * ds + pj * dc;
        pr = npr; pj = npj;
    }

    // Threshold at half peak → on/off; collect "on" run lengths (samples).
    const float thr = 0.5f * peak;
    std::vector<int> onRuns;
    int run = 0;
    for (int k = 0; k < n; ++k) {
        if (env[static_cast<size_t>(k)] > thr) ++run;
        else { if (run > 0) onRuns.push_back(run); run = 0; }
    }
    if (run > 0) onRuns.push_back(run);
    if (onRuns.size() < 3) return 0.0f;

    // Dot length ≈ short elements: 20th percentile of the on-run lengths (robust
    // against dashes being 3× longer and against stray clicks).
    std::sort(onRuns.begin(), onRuns.end());
    const int dotSamp = onRuns[onRuns.size() / 5];
    if (dotSamp <= 0) return 0.0f;
    const float dotMs = dotSamp * 1000.0f / sr;
    const float wpm = 1200.0f / dotMs;                // PARIS: dot(ms) = 1200/wpm
    return std::clamp(wpm, 5.0f, 60.0f);
}

}  // namespace lyra::dsp

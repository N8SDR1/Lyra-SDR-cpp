// Lyra — TX EQ live analyzer.  See EqAnalyzer.h.

#include "dsp/EqAnalyzer.h"

#include <algorithm>   // std::fill — MSVC pulled this in transitively
#include <cmath>

namespace lyra::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

EqAnalyzer::EqAnalyzer(int fftSize, double sampleRate)
    : n_(fftSize), fs_(sampleRate) {
    win_.resize(n_);
    double sum = 0.0;
    for (int i = 0; i < n_; ++i) {
        win_[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n_ - 1));  // Hann
        sum += win_[i];
    }
    // Amplitude-correct: a full-scale sine through a Hann window reads its
    // true peak when divided by (sum/2).
    winNorm_ = (sum > 0.0) ? (sum * 0.5) : 1.0;
    preAcc_.assign(n_, 0.0);
    postAcc_.assign(n_, 0.0);
    preAvg_.assign(bins(), 0.0);
    postAvg_.assign(bins(), 0.0);
    prePub_.assign(bins(), -140.0f);
    postPub_.assign(bins(), -140.0f);
}

void EqAnalyzer::reset() {
    fill_ = 0;
    std::fill(preAcc_.begin(), preAcc_.end(), 0.0);
    std::fill(postAcc_.begin(), postAcc_.end(), 0.0);
    avgSeeded_ = false;
}

void EqAnalyzer::feed(const double *pre, const double *postIq, int n) {
    for (int k = 0; k < n; ++k) {
        preAcc_[fill_]  = pre[k];
        postAcc_[fill_] = postIq[2 * k];
        if (++fill_ >= n_) {
            transform();
            fill_ = 0;
        }
    }
}

void EqAnalyzer::transform() {
    std::vector<float> reA(n_), imA(n_), reB(n_), imB(n_);
    for (int i = 0; i < n_; ++i) {
        const float w = static_cast<float>(win_[i]);
        reA[i] = static_cast<float>(preAcc_[i]) * w;  imA[i] = 0.0f;
        reB[i] = static_cast<float>(postAcc_[i]) * w; imB[i] = 0.0f;
    }
    fft(reA, imA);
    fft(reB, imB);

    const int nb = bins();
    const float norm = static_cast<float>(winNorm_);
    std::vector<float> preDb(nb), postDb(nb);
    const double tau = tau_.load(std::memory_order_relaxed);
    if (tau <= 0.0) {
        // Raw: publish this frame as-is (TX EQ — unchanged).
        avgSeeded_ = false;
        for (int i = 0; i < nb; ++i) {
            const float ma = std::sqrt(reA[i] * reA[i] + imA[i] * imA[i]) / norm;
            const float mb = std::sqrt(reB[i] * reB[i] + imB[i] * imB[i]) / norm;
            preDb[i]  = 20.0f * std::log10(ma + 1e-9f);
            postDb[i] = 20.0f * std::log10(mb + 1e-9f);
        }
    } else {
        // Averaged: one-pole IIR on per-bin POWER (|X|^2), not on dB — a
        // dB-domain average under-reads bursty audio (it averages the log)
        // and lets single-frame nulls drag the trace down.  The coefficient
        // comes from the frame period so the time constant holds at any fs
        // or FFT size: alpha = 1 - exp(-(n/fs) / tau).  The first frame
        // after enable/reset seeds the average so it doesn't ramp up from 0.
        const double frameSec = (fs_ > 0.0) ? n_ / fs_ : 0.0;
        const double alpha = (frameSec > 0.0) ? 1.0 - std::exp(-frameSec / tau) : 1.0;
        for (int i = 0; i < nb; ++i) {
            const double ma = static_cast<double>(reA[i]) / norm, mai = static_cast<double>(imA[i]) / norm;
            const double mb = static_cast<double>(reB[i]) / norm, mbi = static_cast<double>(imB[i]) / norm;
            const double pa = ma * ma + mai * mai;
            const double pb = mb * mb + mbi * mbi;
            if (avgSeeded_) {
                preAvg_[i]  += alpha * (pa - preAvg_[i]);
                postAvg_[i] += alpha * (pb - postAvg_[i]);
            } else {
                preAvg_[i]  = pa;
                postAvg_[i] = pb;
            }
            // 1e-18 power == the raw path's 1e-9 amplitude floor (-180 dB).
            preDb[i]  = static_cast<float>(10.0 * std::log10(preAvg_[i] + 1e-18));
            postDb[i] = static_cast<float>(10.0 * std::log10(postAvg_[i] + 1e-18));
        }
        avgSeeded_ = true;
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        prePub_.swap(preDb);
        postPub_.swap(postDb);
        dirty_ = true;
    }
}

bool EqAnalyzer::snapshot(std::vector<float> &preDb, std::vector<float> &postDb) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!dirty_) return false;
    preDb = prePub_;
    postDb = postPub_;
    dirty_ = false;
    return true;
}

// In-place iterative radix-2 Cooley-Tukey FFT (decimation-in-time).
void EqAnalyzer::fft(std::vector<float> &re, std::vector<float> &im) {
    const int n = static_cast<int>(re.size());
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / len;
        const float wr = static_cast<float>(std::cos(ang));
        const float wi = static_cast<float>(std::sin(ang));
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k, b = i + k + len / 2;
                const float tr = re[b] * cr - im[b] * ci;
                const float ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
                const float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

}  // namespace lyra::dsp

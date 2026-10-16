// Offline test for lyra::dsp::EqAnalyzer (src/dsp/EqAnalyzer.h) — the TX/RX
// EQ live analyzer: raw dBFS calibration + the RX power-domain averaging.
//   build: cmake --build build --target test_eq_analyzer
//   run:   build/test_eq_analyzer
#include "dsp/EqAnalyzer.h"

#include <cmath>
#include <cstdio>
#include <vector>

using lyra::dsp::EqAnalyzer;

namespace {

constexpr double kPi  = 3.14159265358979323846;
constexpr double kFs  = 48000.0;
constexpr int    kN   = 1024;
constexpr int    kBin = 64;                     // sine sits exactly on bin 64
constexpr double kHz  = kBin * kFs / kN;        // 3000 Hz
constexpr double kTau = 0.3;                    // the RX EqModel's setting

int g_failures = 0;

void check(const char *name, bool ok, double got)
{
    std::printf("  %-58s got=%9.3f  %s\n", name, got, ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

// Feed exactly one FFT frame of a phase-continuous sine (amplitude `amp`)
// into `a` (pre == post) and return the published post bins.
std::vector<float> frame(EqAnalyzer &a, double amp, long &phaseIdx)
{
    std::vector<double> pre(kN), postIq(2 * kN);
    for (int k = 0; k < kN; ++k, ++phaseIdx) {
        const double s = amp * std::sin(2.0 * kPi * kHz * phaseIdx / kFs);
        pre[k] = s;
        postIq[2 * k] = s;
        postIq[2 * k + 1] = 0.0;
    }
    a.feed(pre.data(), postIq.data(), kN);
    std::vector<float> p, q;
    a.snapshot(p, q);
    return q;
}

double dB(double amp) { return 20.0 * std::log10(amp); }

}  // namespace

int main()
{
    std::printf("test_eq_analyzer\n");
    const double framesPerTau = kTau * kFs / kN;   // ~14.06 frames

    // 1. Raw (default, TX): full-scale sine reads ~0 dBFS on its bin.
    {
        EqAnalyzer a(kN, kFs);
        long ph = 0;
        const auto b = frame(a, 1.0, ph);
        check("raw: full-scale sine bin ~0 dB (|err| < 0.1)",
              std::fabs(b[kBin]) < 0.1, b[kBin]);
        check("raw: averaging() defaults to off", a.averaging() == 0.0, a.averaging());
    }

    // 2. Averaged: full-scale sine still calibrated (seeded first frame).
    {
        EqAnalyzer a(kN, kFs);
        a.setAveraging(kTau);
        long ph = 0;
        std::vector<float> b;
        for (int f = 0; f < 5; ++f) b = frame(a, 1.0, ph);
        check("avg: steady full-scale sine bin ~0 dB (|err| < 0.1)",
              std::fabs(b[kBin]) < 0.1, b[kBin]);
    }

    // 3. Step up 20 dB: within 1 dB of the new level after ~3 tau, and
    //    clearly still settling after a single frame.
    {
        EqAnalyzer a(kN, kFs);
        a.setAveraging(kTau);
        long ph = 0;
        for (int f = 0; f < 100; ++f) frame(a, 0.1, ph);       // settle at -20
        const auto first = frame(a, 1.0, ph);                   // step to 0 dBFS
        check("avg: step +20 dB, 1 frame later still > 3 dB low",
              first[kBin] < -3.0, first[kBin]);
        const int n3 = static_cast<int>(std::ceil(3.0 * framesPerTau));
        std::vector<float> b = first;
        for (int f = 1; f < n3; ++f) b = frame(a, 1.0, ph);
        check("avg: step +20 dB, within 1 dB after 3 tau",
              std::fabs(b[kBin] - 0.0) < 1.0, b[kBin]);
    }

    // 4. Step down 6 dB: within 1 dB after ~3 tau (power-domain decay —
    //    a bigger drop takes longer; it falls ~4.34/tau dB/s).
    {
        EqAnalyzer a(kN, kFs);
        a.setAveraging(kTau);
        long ph = 0;
        for (int f = 0; f < 100; ++f) frame(a, 1.0, ph);
        const double lo = 0.5;
        const int n3 = static_cast<int>(std::ceil(3.0 * framesPerTau));
        std::vector<float> b;
        for (int f = 0; f < n3; ++f) b = frame(a, lo, ph);
        check("avg: step -6 dB, within 1 dB after 3 tau",
              std::fabs(b[kBin] - dB(lo)) < 1.0, b[kBin] - dB(lo));
    }

    // 5. One loud frame (0 dBFS) in a -40 dBFS stream is attenuated by
    //    roughly 10*log10(alpha) ≈ -11.6 dB (alpha = 1 - e^(-T/tau)).
    {
        EqAnalyzer a(kN, kFs);
        a.setAveraging(kTau);
        long ph = 0;
        for (int f = 0; f < 100; ++f) frame(a, 0.01, ph);
        const auto spike = frame(a, 1.0, ph);
        check("avg: single 0 dBFS frame reads < -10 dB",
              spike[kBin] < -10.0, spike[kBin]);
        check("avg: ... but still rises above the -40 dB floor (> -20)",
              spike[kBin] > -20.0, spike[kBin]);
        std::vector<float> b;
        for (int f = 0; f < 200; ++f) b = frame(a, 0.01, ph);
        check("avg: back to -40 dB after the spike (|err| < 0.5)",
              std::fabs(b[kBin] - dB(0.01)) < 0.5, b[kBin] - dB(0.01));
    }

    // 6. Averaging off reproduces the raw output bit-for-bit, including after
    //    it was on (no leftover state), across every bin.
    {
        EqAnalyzer raw(kN, kFs), toggled(kN, kFs);
        toggled.setAveraging(kTau);
        long ph1 = 0, ph2 = 0;
        for (int f = 0; f < 20; ++f) { frame(raw, 0.3, ph1); frame(toggled, 0.3, ph2); }
        toggled.setAveraging(0.0);
        int mism = 0;
        const double amps[] = {1.0, 0.02, 0.7};
        for (double amp : amps) {
            const auto r = frame(raw, amp, ph1);
            const auto t = frame(toggled, amp, ph2);
            for (size_t i = 0; i < r.size(); ++i)
                if (r[i] != t[i]) ++mism;
        }
        check("off-after-on: every bin == raw (mismatch count)", mism == 0, mism);
    }

    std::printf(g_failures ? "FAILED (%d)\n" : "ALL PASS (0 failures)\n", g_failures);
    return g_failures ? 1 : 0;
}

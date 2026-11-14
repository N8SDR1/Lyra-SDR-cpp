// Lyra — captured-profile noise reducer (captured-profile slice 3: apply).
//
// Takes a captured noise-power profile and subtracts it from the live IQ
// in the STFT domain via a Wiener-from-profile gain, BEFORE WDSP's RXA
// chain (the gain is installed on an IqStft frame hook; cleaned IQ goes
// to fexchange0).  The panadapter is fed the pre-NR-C block so a strong
// station does not paint a wide STFT slope.  This is the half that
// actually reduces noise; CapturedProfile (slice 2) produced the numbers
// it consumes.
//
// Per bin k, with live frame power |Y[k]|^2 and captured noise power
// Pn[k], the amplitude gain is the standard power-subtraction form
//     g = sqrt(max(0, 1 - alpha * Pn[k] / |Y[k]|^2))
// clamped to [floor, 1] (floor = max attenuation, e.g. -24 dB → 0.063 so
// the pre-AGC cut is still audible after WDSP AGC makeup; -12 was too
// gentle once the panadapter stopped showing the mask),
// then temporally smoothed per bin (g = s*gPrev + (1-s)*g) to stop the
// mask fluttering frame-to-frame.  Phase is preserved (gain scales the
// complex bin).  Display is a separate uncleaned feed — do not mix
// occupied bins toward unity here; that leak also lifts the audio floor.
//
// SAME-COUNT INTERFACE: process(in, n, out) writes exactly n cleaned
// frames per n input frames.  The WOLA emits a hop at a time, so an
// internal output FIFO primed with one window of silence hides that
// latency — the engine just swaps the block pointer in feedIq, no
// re-framing of the audio path.  Cost: one STFT window of latency
// (~21 ms at 4096) on the RX audio, only when apply is enabled.
//
// Qt-free + single-threaded (engine serialises via channelMtx_); unit-
// tests standalone like IqStft / CapturedProfile.

#pragma once

#include "iqstft.h"

#include <deque>
#include <vector>

namespace lyra::dsp {

class NoiseReducer {
public:
    explicit NoiseReducer(int fftSize);

    NoiseReducer(const NoiseReducer &) = delete;             // hook captures this
    NoiseReducer &operator=(const NoiseReducer &) = delete;

    int  fftSize() const { return fftSize_; }
    bool ready()   const { return profileValid_; }

    // Install the captured noise-power spectrum (length must == fftSize).
    // Resets the gain mask + re-primes the latency FIFO.  Ignored (and
    // marks not-ready) if the length doesn't match.
    void setProfile(const std::vector<double> &noisePower);

    // Operator tunables (live-safe).  alpha = over-subtraction (1..2);
    // floorDb = max attenuation in dB (e.g. -12); smoothing = per-bin
    // mask smoothing 0..0.99 (higher = steadier).
    void setAlpha(double a);
    void setFloorDb(double db);
    void setSmoothing(double s);

    double alpha()     const { return alpha_; }
    double floorDb()   const { return floorDb_; }
    double smoothing() const { return smoothing_; }

    // Clear transform + FIFO + mask state and re-prime the latency FIFO.
    void reset();

    // Subtract noise from `nframes` interleaved-IQ frames, writing
    // exactly `nframes` cleaned interleaved frames to `out` (caller-sized
    // 2*nframes doubles).  Pass-through (still latency-delayed) when no
    // valid profile is set.
    void process(const double *in, int nframes, double *out);

private:
    void buildGainHook();

    IqStft              stft_;
    int                 fftSize_     = 0;
    bool                profileValid_ = false;
    std::vector<double> profPower_;          // captured Pn[k] (length N)
    std::vector<double> gPrev_;              // smoothed mask state (length N)
    double              alpha_     = 1.0;
    double              floorLin_  = 0.063096; // 10^(-24/20)
    double              floorDb_   = -24.0;
    double              smoothing_ = 0.6;
    std::deque<double>  outFifo_;            // primed interleaved output
    std::vector<double> scratch_;            // stft output staging
};

} // namespace lyra::dsp

// Lyra — DeepFist neural CW decoder: front-end conditioner.
//
// Faithful C++ mirror of the verified live Rust conditioner
// (diddle cw_neural.rs::Conditioner).  The exp14+ DeepFist models are trained
// on CONDITIONED single-signal audio, so inference must condition too
// (train == inference).  Runs on the 3200 Hz window BEFORE the spectrogram:
//
//   1. AGC          — normalise to unit RMS (kills live-audio level swings)
//   2. tone AFC     — find the dominant CW tone in 400-1200 Hz (4096-pt FFT)
//   3. downconvert  — shift the tone to baseband, two cascaded 1-pole LPFs
//                     (a ~90 Hz matched-ish bandpass around the tone)
//   4. recenter     — shift back up to a fixed 600 Hz pitch
//   5. peak-norm    — scale to peak 1
//
// This isolates ONE signal (rejecting QRM upstream) and hands the model the
// fixed-pitch, level-normalised input it was trained on.  Uses incremental
// phasor rotation (not per-sample trig) to match the Rust numerics.
#pragma once

#include <complex>
#include <vector>

namespace lyra::dsp {

class DeepFistConditioner {
public:
    static constexpr int    kSr        = 3200;
    static constexpr int    kToneNfft  = 4096;
    static constexpr float  kOutPitch  = 600.0f;
    static constexpr float  kCondBwHz  = 90.0f;
    static constexpr float  kBandLoHz  = 400.0f;
    static constexpr float  kBandHiHz  = 1200.0f;

    DeepFistConditioner();

    // Dominant CW tone (Hz) in 400-1200 Hz, from the last 4096 samples.
    float detectTone(const float* audio, int n) const;

    // Isolate + normalise one CW signal (auto tone lock).  Returns audio of the
    // same length at kSr, peak ~1.  For n < 4096 returns the input unchanged.
    std::vector<float> condition(const float* audio, int n) const;
    std::vector<float> conditionAt(const float* audio, int n, float toneHz) const;

private:
    void fft(std::vector<std::complex<double>>& a) const;   // radix-2, size kToneNfft

    std::vector<int>                 rev_;   // bit-reversal permutation (4096)
    std::vector<std::complex<double>> tw_;   // twiddles (2048)
};

}  // namespace lyra::dsp

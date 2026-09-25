// Lyra — native parametric EQ (RBJ biquad cascade).  Task #50 (TX) / #59 (RX).
//
// Eight operator bands, each a selectable RBJ "Audio EQ Cookbook" filter
// type.  This is a Lyra-native DSP block (standard cookbook biquad math —
// NOT a WDSP port, so no attribution required; WDSP's own TXA EQ stays
// off, SetTXAEQRun(0), so we never double-EQ).  Runs as a pre-WDSP-TXA
// rack stage on the mic stream (and post-RXA on RX); see
// docs/architecture / project memory project-lyra-cpp-eq.
//
// Threading contract:
//   * The operator/UI thread calls setBand()/setSampleRate()/setBypass()/
//     setMakeupDb() and reads magnitudeDb()/band().
//   * The audio thread calls process() (and reset() while stopped).
//   * Coefficients are published to the audio thread via a staged->active
//     copy taken at block start under a try_lock — process() NEVER blocks
//     on the UI thread (a contended update is simply picked up next block).
//     Filter STATE (z1/z2) is audio-thread-owned.
#pragma once

#include <array>
#include <atomic>
#include <mutex>

namespace lyra::dsp {

class ParamEq {
public:
    enum class Type {
        Peak, LowShelf, HighShelf, LowPass, HighPass, BandPass, Notch
    };
    static constexpr int kNumBands = 10;

    struct Band {
        bool   enabled = true;
        Type   type    = Type::Peak;
        double freqHz  = 1000.0;
        double gainDb  = 0.0;    // used by Peak / LowShelf / HighShelf only
        double q       = 1.0;
    };

    ParamEq();

    void setSampleRate(double fs);          // re-stages all bands
    void setBypass(bool on);
    void setMakeupDb(double db);            // output trim, applied post-cascade
    void setBand(int i, const Band &b);     // re-stages band i (recomputes all)
    Band band(int i) const;
    int  numBands() const { return kNumBands; }

    void process(float *x, int n);          // in-place, mono, real-time safe
    // TX-rack variant: filter the REAL (I) channel of interleaved {I,Q}
    // doubles in place — the WDSP TX path carries the mic as I=mic, Q=0,
    // so we EQ the I samples and leave Q.  nPairs = number of (I,Q) pairs
    // (buffer length = 2*nPairs doubles).  Same RT-safe coeff publish +
    // bypass/makeup behaviour as process().
    void processInterleaved(double *iqPairs, int nPairs);
    // RX variant (#59): the post-RXA receive audio is interleaved stereo
    // L/R doubles, mono-dup (L==R) for a single RX.  Filter the L lane through
    // the cascade and write the result to BOTH L and R, so every downstream
    // tee (jack / sink / TCI / VAC) stays balanced.  nframes = number of L/R
    // pairs (buffer = 2*nframes doubles).  Same RT-safe coeff publish +
    // bypass/makeup as process().
    void processMonoDup(double *interleaved, int nframes);
    // SUB stereo: EQ L and R independently (RX1 left, RX2 right).  Do not
    // use processMonoDup here — that copies L onto R and wipes RX2.
    void processStereoIndependent(double *interleaved, int nframes);
    // Cheap audio-thread check so the RX path can skip the EQ copy+filter
    // entirely when the operator bypassed it (the panel ON/OFF, any mode).
    bool bypassed() const { return bypass_.load(std::memory_order_relaxed); }
    void reset();                           // clear filter state (call stopped)

    // Summed magnitude response in dB at freqHz (drives the UI curve).
    // UI-thread-only (reads the band params, recomputes coeffs locally).
    double magnitudeDb(double freqHz) const;

private:
    struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State  { double z1 = 0, z2 = 0; };

    // Normalized RBJ biquad coefficients for one band (identity if the
    // band contributes nothing).  Pure function — shared by restage() and
    // magnitudeDb() so the drawn curve is exactly what process() applies.
    static Coeffs design(Type t, double fs, double f0, double q, double gainDb);

    void restage();   // recompute staged_ from bands_, mark dirty

    double fs_ = 48000.0;
    std::atomic<bool>   bypass_{false};
    std::atomic<double> makeupLin_{1.0};        // 10^(makeupDb/20)
    std::array<Band, kNumBands> bands_{};        // UI-thread view of the bands

    mutable std::mutex stageMtx_;
    std::array<Coeffs, kNumBands> staged_{};     // guarded by stageMtx_
    std::atomic<bool> stageDirty_{false};

    std::array<Coeffs, kNumBands> active_{};     // audio-thread coeffs
    std::array<State,  kNumBands> state_{};       // L / mono-dup state
    std::array<State,  kNumBands> stateR_{};      // R lane (SUB only)
};

}  // namespace lyra::dsp

// Lyra — zero-beat carrier-offset estimator (tuning aid).
//
// Measures the frequency offset of the dominant carrier/tone in the RX
// passband relative to baseband centre (the DDS / LO), to a few hertz, from
// the SAME IQ block the panadapter sees (wdsp_engine::feedIq).  The
// coordinator (WdspEngine) subtracts the marker offset
// (WdspEngine::markerOffsetHz) so the display reads "how far off your dialled
// frequency" — one convention for CW (marker sits at ±pitch) and AM / SAM /
// FM (marker at centre).  Center the needle → dead-tuned / zero-beat.
//
// Modelled on FreqCalMeasure (Goertzel-style coarse scan + parabolic sub-bin
// + SNR gate + EWMA) but on COMPLEX IQ, so it locks a carrier on either
// sideband and reports a signed offset.  Qt-free, allocation-light on the hot
// path, unit-testable.  It ONLY answers "what is the strongest carrier's
// offset from centre, and is it strong enough to trust?".
#pragma once

#include <vector>

namespace lyra::dsp {

class ZeroBeat {
public:
    void setSampleRate(double hz);   // IQ (DDC) rate; resizes the window
    void setHalfRange(double hz);    // ± search window around the centre
    void setSearchCenter(double hz) { searchCenterHz_ = hz; }  // baseband Hz to scan around
    void setSnrGateDb(double db);    // valid() threshold

    void reset();                    // clear accumulators (mode / rate change)
    void process(const double* iq, int nframes);  // interleaved I,Q; RX-worker thread

    // Results — plain scalar reads (publish via atomics at the call site).
    double offsetHz() const { return offsetHz_; }  // signed, from centre
    double snrDb()    const { return snrDb_; }
    bool   valid()    const { return valid_; }

private:
    void analyze_();

    double sampleRate_    = 192000.0;
    double halfRange_     = 500.0;   // scan the marker ±this (locks YOUR signal)
    double searchCenterHz_ = 0.0;    // baseband centre to scan around (mirrored marker)
    double snrGate_    = 15.0;   // above the ~9 dB max/median noise baseline
    int    winSize_    = 8192;
    int    holdWindows_ = 8;     // keep the last lock ~350 ms across CW gaps
    int    holdCount_   = 0;

    std::vector<double> bufI_, bufQ_;   // interleaved-split accumulation
    int    fill_ = 0;

    double offsetHz_ = 0.0;   // EWMA of strong-window peak offsets
    double snrDb_    = 0.0;
    bool   valid_    = false;
    bool   haveEwma_ = false;
};

} // namespace lyra::dsp

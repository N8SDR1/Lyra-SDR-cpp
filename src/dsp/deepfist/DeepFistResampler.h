// Lyra — DeepFist neural CW decoder: input decimator.
//
// Streaming integer decimator from the RX audio rate (default 48 kHz) down to
// DeepFist's canonical model input rate of 3200 Hz (factor 15).  Hamming-
// windowed-sinc low-pass FIR, same design as the fldigi-port decimator in
// src/dsp/CwDecoder.cpp — this is glue in front of the model, not part of the
// model.  Cutoff sits above the 400-1200 Hz CW band and below the 1600 Hz
// output Nyquist so nothing aliases into the spectrogram band.
#pragma once

#include <vector>

namespace lyra::dsp {

class DeepFistResampler {
public:
    // outRate is the model rate (3200 Hz); inRate is the RX audio rate.
    DeepFistResampler(double inRate = 48000.0, double outRate = 3200.0);

    void setInRate(double hz);
    double outRate() const { return outRate_; }
    int    factor()  const { return decim_; }

    // Push `nframes` input samples; appends produced output samples (at outRate)
    // to `out`.  Streaming: filter history carries across calls.
    void process(const float* in, int nframes, std::vector<float>& out);

    void reset();

private:
    void rebuild();

    double inRate_  = 48000.0;
    double outRate_ = 3200.0;
    int    decim_   = 15;

    std::vector<double> fir_;    // low-pass taps (unity DC gain)
    std::vector<double> hist_;   // circular input history
    int histPos_ = 0;
    int phase_   = 0;            // decimation phase counter
};

}  // namespace lyra::dsp

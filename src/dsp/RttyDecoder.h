// Lyra — RX RTTY decoder adapter around the fldigi receive port.
//
// Decimates post-demod RX audio (48 kHz mono) to fldigi's 8 kHz RTTY rate
// and forwards operator knobs.  Same glue as CwDecoder (Hamming sinc LPF).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "dsp/rtty_fldigi/fldigi_rtty.h"

namespace lyra::dsp {

class RttyDecoder {
public:
    RttyDecoder();

    void setSampleRate(double hz);
    void setCenterHz(double hz);
    void setShiftHz(double hz);
    void setBaud(double baud);
    void setReverse(bool on);
    void setSquelch(bool on, double value);

    double centerHz() const { return rx_.centerHz(); }
    double shiftHz() const { return rx_.shiftHz(); }
    double baud() const { return rx_.baud(); }
    bool   reverse() const { return rx_.reverse(); }
    double squelchMetric() const { return rx_.squelchMetric(); }

    std::function<void(const std::string& text)> onText;

    void process(const float* mono, int nframes);
    void reset();

private:
    void rebuildDecimator();

    rttyfldigi::RttyRx rx_;

    double inRate_ = 48000.0;

    int                 decim_   = 6;
    std::vector<double> firCoef_;
    std::vector<double> hist_;
    int                 histPos_ = 0;
    int                 phase_   = 0;
    std::vector<double> out8k_;
};

}  // namespace lyra::dsp

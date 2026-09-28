// ----------------------------------------------------------------------------
// fldigi_rtty.h  --  faithful port of fldigi's RTTY *receive* chain.
//
// Ported from fldigi (GPL v3+):
//   src/include/rtty.h, src/cw_rtty/rtty.cxx
//     Dave Freese W1HKJ; Stefan Fendt DL1SMF; gmfsk origin Tomi Manninen OH2BNS
//   src/filters/fftfilt.cxx::rtty_filter  (via FftFilt::rtty_filter)
//
// RECEIVE only: dual mixer → raised-cosine FFT LPF → Optimal ATC v3 →
// start/data/stop FSM → Baudot.  Native rate 8000 Hz.  No TX, FSK, AFC,
// XY scope, or synop.  Metric() is the fldigi 0..100 formula without a
// waterfall FFT (filter envelopes vs noise floor, same 3000/delta scale).
// ----------------------------------------------------------------------------
#pragma once

#include "dsp/cw_fldigi/fldigi_cw.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lyra::dsp::rttyfldigi {

using lyra::dsp::cwfldigi::cmplx;
using lyra::dsp::cwfldigi::FftFilt;
using lyra::dsp::cwfldigi::PI;
using lyra::dsp::cwfldigi::TWOPI;

inline constexpr int    RTTY_SampleRate = 8000;
inline constexpr int    MAXBITS         = 2 * RTTY_SampleRate / 23 + 1;
inline constexpr int    LETTERS         = 0x100;
inline constexpr int    FIGURES         = 0x200;

enum RTTY_RX_STATE { IDLE, START, DATA, STOP };

class Mixer {
public:
    Mixer() = default;
    double freq() const { return freq_; }
    void   freq(double f) { freq_ = f; }
    cmplx  mix_block(cmplx in) {
        const cmplx z(std::cos(phase_), std::sin(phase_));
        phase_ -= TWOPI * freq_ / (double)RTTY_SampleRate;
        if (phase_ >  TWOPI) phase_ -= TWOPI;
        if (phase_ < -TWOPI) phase_ += TWOPI;
        return z * in;
    }
    void reset() { phase_ = 0.0; }
private:
    double freq_  = 0.0;
    double phase_ = 0.0;
};

class RttyRx {
public:
    RttyRx();
    ~RttyRx() = default;

    void reset();
    void rxProcess(const double* buf, int len);

    void setCenterHz(double hz);
    void setShiftHz(double hz);
    void setBaud(double baud);
    void setReverse(bool on) { reverse_ = on; }
    void setSquelch(bool on, double value) {
        sqlOn_ = on;
        sqlValue_ = value;
    }
    bool squelchOpen() const { return !sqlOn_ || metric_ >= sqlValue_; }

    double squelchMetric() const { return metric_; }
    double centerHz() const { return frequency_; }
    double shiftHz() const { return shift_; }
    double baud() const { return baud_; }
    bool   reverse() const { return reverse_; }

    std::function<void(const std::string&)> onText;

private:
    void restart();
    void resetFilters();
    void rx(bool bit);
    bool isMark();
    bool isMarkSpace(int& n);
    char baudotDec(unsigned char data);
    void emitChar(char c);
    void updateMetric();

    double frequency_ = 2210.0;  // audio centre (2125/2295 → 2210)
    double shift_     = 170.0;
    double baud_      = 45.45;
    bool   reverse_   = false;
    bool   sqlOn_     = true;
    double sqlValue_  = 18.0;
    double metric_    = 0.0;

    int    nbits_     = 5;
    double stopbits_  = 1.5;
    bool   msb_       = false;
    bool   uosRx_     = true;
    int    rxmode_    = LETTERS;

    int    filter_length_ = 0;
    int    symbollen_     = 0;
    int    stoplen_       = 0;
    int    counter_       = 0;
    int    bitcntr_       = 0;
    int    rxdata_        = 0;
    RTTY_RX_STATE rxstate_ = IDLE;

    Mixer  mark_phase_;
    Mixer  space_phase_;
    std::unique_ptr<FftFilt> mark_filt_;
    std::unique_ptr<FftFilt> space_filt_;

    double mark_env_   = 0.0;
    double space_env_  = 0.0;
    double mark_noise_ = 0.0;
    double space_noise_= 0.0;
    double noise_floor_= 0.0;
    double mark_mag_   = 0.0;
    double space_mag_  = 0.0;
    double sigpwr_     = 0.0;
    double noisepwr_   = 1e-10;
    char   lastchar_   = 0;

    std::vector<uint8_t> bit_buf_;
};

}  // namespace lyra::dsp::rttyfldigi

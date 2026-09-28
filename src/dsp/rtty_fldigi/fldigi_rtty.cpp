// fldigi_rtty.cpp — receive-only port of fldigi rtty.cxx.  See fldigi_rtty.h.
#include "dsp/rtty_fldigi/fldigi_rtty.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>

namespace lyra::dsp::rttyfldigi {

namespace {
static inline double decayavg(double average, double input, int weight) {
    if (weight <= 1) return input;
    return ((input - average) / (double)weight) + average;
}

static const char letters[32] = {
    '\0','E','\n','A',' ','S','I','U',
    '\r','D','R','J','N','F','C','K',
    'T','Z','L','W','H','Y','P','Q',
    'O','B','G',' ','M','X','V',' '
};
static const char figures[32] = {
    '\0','3','\n','-',' ','\a','8','7',
    '\r','$','4','\'',',','!',':','(',
    '5','"',' ',')','2','#','6','0',
    '1','9','?','&',' ','.','/',' '
};

int filtlenForBaud(double baud) {
    // fldigi rtty.cxx FILTLEN[] vs BAUD[]:
    // 45, 45.45, 50, 56, 75, 100, 110 → 512; 150 → 256; 200 → 128; 300 → 64
    if (baud < 130.0) return 512;
    if (baud < 175.0) return 256;
    if (baud < 250.0) return 128;
    return 64;
}
}  // namespace

RttyRx::RttyRx() {
    bit_buf_.assign(MAXBITS, 0);
    restart();
}

void RttyRx::reset() { restart(); }

void RttyRx::setCenterHz(double hz) {
    if (hz < 200.0) hz = 200.0;
    if (hz > 3500.0) hz = 3500.0;
    frequency_ = hz;
    mark_phase_.freq(frequency_ + shift_ / 2.0);
    space_phase_.freq(frequency_ - shift_ / 2.0);
}

void RttyRx::setShiftHz(double hz) {
    if (hz < 23.0) hz = 23.0;
    if (hz > 850.0) hz = 850.0;
    if (hz == shift_) return;
    shift_ = hz;
    restart();
}

void RttyRx::setBaud(double baud) {
    if (baud < 20.0) baud = 20.0;
    if (baud > 300.0) baud = 300.0;
    if (baud == baud_) return;
    baud_ = baud;
    restart();
}

void RttyRx::resetFilters() {
    filter_length_ = filtlenForBaud(baud_);
    const double f = baud_ / (double)RTTY_SampleRate;
    mark_filt_  = std::make_unique<FftFilt>(f, filter_length_);
    space_filt_ = std::make_unique<FftFilt>(f, filter_length_);
    mark_filt_->rtty_filter(f);
    space_filt_->rtty_filter(f);
}

void RttyRx::restart() {
    setCenterHz(frequency_);
    mark_phase_.reset();
    space_phase_.reset();
    resetFilters();
    rxstate_   = IDLE;
    rxmode_    = LETTERS;
    counter_   = 0;
    bitcntr_   = 0;
    rxdata_    = 0;
    symbollen_ = (int)(RTTY_SampleRate / baud_);
    stoplen_   = (int)(stopbits_ * RTTY_SampleRate / baud_);
    mark_env_ = space_env_ = mark_noise_ = space_noise_ = 0.0;
    noise_floor_ = mark_mag_ = space_mag_ = 1e-10;
    sigpwr_ = 0.0;
    noisepwr_ = 1e-10;
    metric_ = 0.0;
    lastchar_ = 0;
    bit_buf_.assign(MAXBITS, 0);
}

void RttyRx::rxProcess(const double* buf, int len) {
    if (!buf || len <= 0 || !mark_filt_ || !space_filt_) return;

    cmplx z, zmark, zspace, *zp_mark = nullptr, *zp_space = nullptr;

    while (len-- > 0) {
        z = cmplx(*buf, *buf);
        buf++;

        zmark  = mark_phase_.mix_block(z);
        mark_filt_->run(zmark, &zp_mark);

        zspace = space_phase_.mix_block(z);
        const int n_out = space_filt_->run(zspace, &zp_space);
        if (n_out) {
            for (int i = 0; i < n_out; i++) {
                mark_mag_  = std::abs(zp_mark[i]);
                mark_env_  = decayavg(mark_env_, mark_mag_,
                                      (mark_mag_ > mark_env_) ? symbollen_ / 4 : symbollen_ * 16);
                mark_noise_ = decayavg(mark_noise_, mark_mag_,
                                       (mark_mag_ < mark_noise_) ? symbollen_ / 4 : symbollen_ * 48);
                space_mag_  = std::abs(zp_space[i]);
                space_env_  = decayavg(space_env_, space_mag_,
                                       (space_mag_ > space_env_) ? symbollen_ / 4 : symbollen_ * 16);
                space_noise_ = decayavg(space_noise_, space_mag_,
                                        (space_mag_ < space_noise_) ? symbollen_ / 4 : symbollen_ * 48);
                noise_floor_ = std::min(space_noise_, mark_noise_);

                double mclipped = mark_mag_ > mark_env_ ? mark_env_ : mark_mag_;
                double sclipped = space_mag_ > space_env_ ? space_env_ : space_mag_;
                if (mclipped < noise_floor_) mclipped = noise_floor_;
                if (sclipped < noise_floor_) sclipped = noise_floor_;

                // Optimal ATC v3 (fldigi rtty.cxx)
                const double v3 =
                    (mclipped - noise_floor_) * (mark_env_ - noise_floor_) -
                    (sclipped - noise_floor_) * (space_env_ - noise_floor_) -
                    0.25 * ((mark_env_ - noise_floor_) * (mark_env_ - noise_floor_) -
                            (space_env_ - noise_floor_) * (space_env_ - noise_floor_));
                const bool mark = v3 > 0.0;

                updateMetric();
                rx(reverse_ ? !mark : mark);
            }
        }
    }
}

bool RttyRx::isMark() {
    return bit_buf_[symbollen_ / 2] != 0;
}

void RttyRx::updateMetric() {
    // fldigi rtty::Metric(): noise at mid-shift, signal = mark+space,
    // metric = clamp((3000/delta)*(sigpwr/noisepwr), 0, 100).  No waterfall
    // here — noise_floor_ / mark_env_ / space_env_ are the analogue.
    const double delta = baud_ / 8.0;
    const double np = noise_floor_ * (3000.0 / delta);
    const double sp = mark_env_ + space_env_ + 1e-10;
    sigpwr_   = decayavg(sigpwr_,   sp, sp > sigpwr_ ? 2 : 8);
    noisepwr_ = decayavg(noisepwr_, np, 16);
    double m = (3000.0 / delta) * (sigpwr_ / (noisepwr_ + 1e-20));
    if (m < 0.0) m = 0.0;
    if (m > 100.0) m = 100.0;
    metric_ = m;
}

bool RttyRx::isMarkSpace(int& n) {
    n = 0;
    // fldigi: idle-mark → start-space straddle, mark-count ≈ half a bit
    if (bit_buf_[0] && !bit_buf_[symbollen_ - 1]) {
        for (int i = 0; i < symbollen_; ++i) n += bit_buf_[i];
        if (std::abs(symbollen_ / 2 - n) < 6)
            return true;
    }
    return false;
}

void RttyRx::rx(bool bit) {
    for (int i = 1; i < symbollen_; ++i)
        bit_buf_[i - 1] = bit_buf_[i];
    bit_buf_[symbollen_ - 1] = bit ? 1 : 0;

    switch (rxstate_) {
    case IDLE:
        if (isMarkSpace(counter_))
            rxstate_ = START;
        break;
    case START:
        if (--counter_ == 0) {
            if (!isMark()) {
                rxstate_ = DATA;
                counter_ = symbollen_;
                bitcntr_ = rxdata_ = 0;
            } else {
                rxstate_ = IDLE;
            }
        }
        break;
    case DATA:
        if (--counter_ == 0) {
            rxdata_ |= (isMark() ? 1 : 0) << bitcntr_++;
            counter_ = symbollen_;
            if (bitcntr_ == nbits_) {
                if (msb_) {
                    unsigned char mask = 1, c = 0;
                    for (int i = 0; i < nbits_; ++i) {
                        if (rxdata_ & mask) c |= (0x80 >> i);
                        mask <<= 1;
                    }
                    rxdata_ = c & 0x1F;
                }
                rxstate_ = STOP;
                // fldigi: last data sample already set counter = symbollen
            }
        }
        break;
    case STOP:
        if (--counter_ == 0) {
            if (isMark() && squelchOpen())
                emitChar(baudotDec((unsigned char)(rxdata_ & 0x1F)));
            rxstate_ = IDLE;
        }
        break;
    }
}

char RttyRx::baudotDec(unsigned char data) {
    int out = 0;
    switch (data) {
    case 0x1F:
        rxmode_ = LETTERS;
        break;
    case 0x1B:
        rxmode_ = FIGURES;
        break;
    case 0x04:
        if (uosRx_) rxmode_ = LETTERS;
        return ' ';
    default:
        if (data > 31) return 0;
        out = (rxmode_ == LETTERS) ? letters[data] : figures[data];
        break;
    }
    return (char)out;
}

void RttyRx::emitChar(char c) {
    if (c == 0 || c == '\a') return;
    if (c == '\r' && lastchar_ == '\r') return;
    if (c == '\n' && lastchar_ == '\n') return;
    lastchar_ = c;
    if (c == '\r') c = '\n';
    if (onText) onText(std::string(1, c));
}

}  // namespace lyra::dsp::rttyfldigi

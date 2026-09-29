// ----------------------------------------------------------------------------
// fldigi_cw.h  --  faithful port of fldigi's CW (Morse) *receive* chain.
//
// Ported directly, with NO algorithmic drift, from fldigi 4.2.06:
//   src/cw_rtty/cw.cxx, cw.h        (Dave Freese W1HKJ; adaptive speed
//                                    tracking by Lawrence Glaister VE7IT)
//   src/cw_rtty/morse.cxx, morse.h  (W1HKJ)
//   src/filters/fftfilt.cxx         (W1HKJ, overlap-add FFT filter)
//   src/include/filters.h           (Cmovavg)
//   src/include/gfft.h              (used verbatim, see cw_fldigi/gfft.h)
// fldigi is GPL v3-or-later; lyra-cpp is GPL v3+ -> port-legal.  Attribution
// retained per the WDSP-port discipline.
//
// This is the RECEIVE path only (mixer -> fftfilt LPF -> magnitude ->
// decimate-by-16 -> decode_stream adaptive slicer -> handle_event timing
// FSM -> morse rx_lookup).  It runs at fldigi's native CW_SAMPLERATE = 8000
// Hz; the caller decimates its audio to 8 kHz first.  No GUI / no config
// globals: fldigi's progdefaults/progStatus become plain members seeded to
// fldigi's own defaults, with setters for the handful of controls fldigi
// actually exposes for RX (bandwidth, speed, tracking, matched filter,
// squelch).  Output is via callbacks: onText(one decoded unit) / onWpm.
// ----------------------------------------------------------------------------
#pragma once

#include <complex>
#include <functional>
#include <string>
#include <vector>

#include "dsp/cw_fldigi/gfft.h"

namespace lyra::dsp::cwfldigi {

using cmplx = std::complex<double>;

// ── fldigi constants (cw.h / modem.h / morse.h) ─────────────────────────────
inline constexpr int    CW_SAMPLERATE      = 8000;
inline constexpr int    CW_FFT_SIZE        = 2048;    // must be power of 2
inline constexpr int    DEC_RATIO          = 16;
inline constexpr int    KWPM               = 12 * CW_SAMPLERATE / 10;  // 9600
inline constexpr int    TRACKING_FILTER_SIZE = 16;
inline constexpr int    MAX_MORSE_ELEMENTS = 6;
inline constexpr double PI                 = 3.14159265358979323846;
inline constexpr double TWOPI              = 2.0 * PI;
inline constexpr char   CW_DOT_REPRESENTATION  = '.';
inline constexpr char   CW_DASH_REPRESENTATION = '-';

enum CW_RX_STATE { RS_IDLE = 0, RS_IN_TONE, RS_AFTER_TONE };
enum CW_EVENT { CW_RESET_EVENT, CW_KEYDOWN_EVENT, CW_KEYUP_EVENT, CW_QUERY_EVENT };
inline constexpr int CW_SUCCESS = 0;
inline constexpr int CW_ERROR   = -1;

// ── Cmovavg (filters.h/filters.cxx) — running-mean, snaps on first sample ───
class Cmovavg {
public:
    explicit Cmovavg(int filtlen = 64);
    ~Cmovavg();
    double run(double a);
    void   setLength(int filtlen);
    void   reset();
private:
    double* in_   = nullptr;
    double  out_  = 0.0;
    int     len_  = 0;
    int     pint_ = 0;
    bool    empty_ = true;
};

// ── fftfilt (fftfilt.cxx) — overlap-add FFT filter; we only need the LPF ────
class FftFilt {
public:
    FftFilt(double f, int len);   // low-pass at f (fraction of samplerate)
    ~FftFilt();
    void create_lpf(double f) { create_filter(0.0, f); }
    // fldigi fftfilt::rtty_filter — Feher raised-cosine LPF (fftfilt.cxx).
    // CW decoder uses this; f is cutoff as a fraction of sample rate.
    void rtty_filter(double f);
    // Returns 0, or flen/2 samples ready in *out.
    int  run(const cmplx& in, cmplx** out);
private:
    void create_filter(double f1, double f2);
    void init_filter();
    void clear_filter();
    static double fsinc(double fc, int i, int len) {
        return (i == len / 2) ? 2.0 * fc
                              : std::sin(2 * PI * fc * (i - len / 2)) /
                                    (PI * (i - len / 2));
    }
    static double blackman(int i, int len) {
        return 0.42 - 0.50 * std::cos(2.0 * PI * i / len) +
               0.08 * std::cos(4.0 * PI * i / len);
    }
    int    flen_, flen2_;
    g_fft<double>* fft_ = nullptr;
    cmplx* ht_ = nullptr;      cmplx* filter_ = nullptr;
    cmplx* timedata_ = nullptr; cmplx* freqdata_ = nullptr;
    cmplx* ovlbuf_ = nullptr;  cmplx* output_ = nullptr;
    int    inptr_ = 0;
    int    pass_ = 1;
};

// ── Morse table + rx_lookup (morse.cxx) ─────────────────────────────────────
class Morse {
public:
    Morse() { init(); }
    void        init();
    std::string rx_lookup(const std::string& rx);
private:
    struct Row { bool enabled; std::string chr; std::string prt; std::string rpr; };
    void enable(const std::string& s, bool val);
    std::vector<Row> table_;
};

// ── the CW receiver ─────────────────────────────────────────────────────────
class CwRx {
public:
    CwRx();
    ~CwRx();

    // configuration (operator/UI thread) — applied lazily in rxProcess, exactly
    // like fldigi's reset_rx_filter()/sync_parameters().
    void setToneHz(double hz)       { frequency_ = hz; }
    void setBandwidthHz(int hz)     { CWbandwidth_ = hz; }
    void setSpeedWpm(double wpm)    { CWspeed_ = wpm; }
    void setTracking(bool on)       { CWtrack_ = on; }
    void setMatchedFilter(bool on)  { CWmfilt_ = on; }
    void setSquelch(bool on, double value) { sqlonoff_ = on; sldrSquelchValue_ = value; }

    void reset();                              // == rx_init + CW_RESET_EVENT
    void rxProcess(const double* buf, int len); // 8 kHz real audio in

    int  rxWpm() const { return cw_receive_speed_; }

    // Live signal metric (fldigi's SNR-in-dB reading, 0..100, IIR-smoothed).
    // Read-only display value; the squelch gates on metric_ > threshold.  A
    // plain cross-thread read of the last-written value is benign here.
    double metric() const { return metric_; }

    std::function<void(const std::string&)> onText;  // one decoded unit
    std::function<void(int)>                onWpm;

private:
    // fldigi RX methods
    void   sync_parameters();
    void   reset_rx_filter();
    void   update_tracking(int dur_1, int dur_2);
    cmplx  mixer(cmplx in);
    void   decode_stream(double value);
    void   rx_FFTprocess(const double* buf, int len);
    int    handle_event(int cw_event, std::string& sc);
    static int usec_diff(unsigned earlier, unsigned later) {
        return (earlier >= later) ? 0 : (int)(later - earlier);
    }
    void   put_rx_char(const std::string& s);
    void   put_cwRcvWPM(int wpm);

    // ── config (fldigi progdefaults/progStatus -> members, fldigi defaults) ──
    double frequency_        = 700.0;   // tone (== operator CW pitch)
    const double samplerate_ = CW_SAMPLERATE;
    double CWspeed_          = 18.0;
    int    CWbandwidth_      = 150;
    bool   CWmfilt_          = false;   // matched filter off
    bool   CWtrack_          = true;    // adaptive tracking on
    int    CWrange_          = 10;
    int    CWlowerlimit_     = 5;
    int    CWupperlimit_     = 50;
    int    cwrx_attack_      = 1;       // MEDIUM -> 200
    int    cwrx_decay_       = 1;       // MEDIUM -> 1000
    bool   sqlonoff_         = false;
    double sldrSquelchValue_ = 5.0;
    char   CW_noise_         = '*';

    // ── applied-config mirrors (reset_rx_filter change detection) ──
    bool   use_matched_filter_ = false;
    double bandwidth_          = 150.0;
    int    cw_speed_           = 18;

    // ── DSP state ──
    double phaseacc_ = 0.0, FFTphase_ = 0.0, FFTvalue_ = 0.0;
    unsigned smpl_ctr_ = 0;
    double agc_peak_ = 1.0, noise_floor_ = 1.0, sig_avg_ = 0.0;
    double siglevel_ = 0.0, metric_ = 0.0;

    FftFilt* cw_FFT_filter_ = nullptr;
    Cmovavg  bitfilter_{1};
    Cmovavg  trackingfilter_{TRACKING_FILTER_SIZE};
    Morse    morse_;

    CW_RX_STATE cw_receive_state_ = RS_IDLE;
    CW_RX_STATE old_cw_receive_state_ = RS_IDLE;

    std::string rx_rep_buf_;
    unsigned cw_rr_start_timestamp_ = 0;
    unsigned cw_rr_end_timestamp_   = 0;

    long two_dots_ = 2 * KWPM / 18;
    long cw_noise_spike_threshold_ = 0;
    int  cw_send_speed_ = 18;
    int  cw_receive_speed_ = 18;
    long cw_receive_dot_length_  = KWPM / 18;
    long cw_receive_dash_length_ = 3 * KWPM / 18;
    int  cw_lower_limit_ = 0, cw_upper_limit_ = 0;
    double lowerwpm_ = 0.0, upperwpm_ = 0.0;
    bool   cwTrack_ = true;
    int    symbollen_ = 0;

    // handle_event statics -> members (reset on reset())
    bool he_space_sent_  = true;
    int  he_last_element_ = 0;

    // SOM buffer kept faithful even though SOM decoding is off by default
    float cw_buffer_[512] = {0};
    int   cw_ptr_ = 0;
};

}  // namespace lyra::dsp::cwfldigi

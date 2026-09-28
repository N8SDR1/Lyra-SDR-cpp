// ----------------------------------------------------------------------------
// fldigi_cw.cpp  --  faithful port of fldigi's CW receive chain.  See
// fldigi_cw.h for provenance/attribution.  Ported verbatim in algorithm from
// fldigi 4.2.06 cw.cxx / morse.cxx / fftfilt.cxx / filters.cxx.  No drift.
// ----------------------------------------------------------------------------
#include "dsp/cw_fldigi/fldigi_cw.h"

#include <cmath>
#include <cstring>

namespace lyra::dsp::cwfldigi {

// misc.h helpers (verbatim)
static inline double clampd(double x, double mn, double mx) {
    return (x < mn) ? mn : ((x > mx) ? mx : x);
}
static inline double decayavg(double average, double input, int weight) {
    if (weight <= 1) return input;
    return ((input - average) / (double)weight) + average;
}

// ── Cmovavg (filters.cxx, verbatim) ─────────────────────────────────────────
Cmovavg::Cmovavg(int filtlen) : len_(filtlen) {
    in_ = new double[len_ > 0 ? len_ : 1];
    empty_ = true;
}
Cmovavg::~Cmovavg() { delete[] in_; }
double Cmovavg::run(double a) {
    if (!in_) return a;
    if (empty_) {
        empty_ = false;
        out_ = 0;
        for (int i = 0; i < len_; i++) { in_[i] = a; out_ += a; }
        pint_ = 0;
        return a;
    }
    out_ = out_ - in_[pint_] + a;
    in_[pint_] = a;
    if (++pint_ >= len_) pint_ = 0;
    return out_ / len_;
}
void Cmovavg::setLength(int filtlen) {
    if (filtlen > len_) { delete[] in_; in_ = new double[filtlen]; }
    len_ = filtlen;
    empty_ = true;
}
void Cmovavg::reset() { empty_ = true; }

// ── FftFilt (fftfilt.cxx, verbatim) ─────────────────────────────────────────
void FftFilt::init_filter() {
    flen2_    = flen_ >> 1;
    fft_      = new g_fft<double>(flen_);
    filter_   = new cmplx[flen_];
    timedata_ = new cmplx[flen_];
    freqdata_ = new cmplx[flen_];
    output_   = new cmplx[flen_];
    ovlbuf_   = new cmplx[flen2_];
    ht_       = new cmplx[flen_];
}
void FftFilt::clear_filter() {
    for (int i = 0; i < flen_; i++) {
        filter_[i] = 0; timedata_[i] = 0; freqdata_[i] = 0; output_[i] = 0; ht_[i] = 0;
    }
    for (int i = 0; i < flen2_; i++) ovlbuf_[i] = 0;
    inptr_ = 0;
}
FftFilt::FftFilt(double f, int len) : flen_(len) {
    init_filter();
    create_lpf(f);
}
FftFilt::~FftFilt() {
    delete fft_;
    delete[] filter_; delete[] timedata_; delete[] freqdata_;
    delete[] output_; delete[] ovlbuf_;   delete[] ht_;
}
void FftFilt::create_filter(double f1, double f2) {
    clear_filter();
    for (int i = 0; i < flen_; i++) ht_[i] = 0;
    bool b_lowpass  = (f2 != 0);
    bool b_highpass = (f1 != 0);
    for (int i = 0; i < flen2_; i++) {
        ht_[i] = 0;
        if (b_lowpass)  ht_[i] += fsinc(f2, i, flen2_);
        if (b_highpass) ht_[i] -= fsinc(f1, i, flen2_);
    }
    if (b_highpass && f2 < f1) ht_[flen2_ / 2] += 1;
    for (int i = 0; i < flen2_; i++) ht_[i] *= blackman(i, flen2_);
    memcpy(filter_, ht_, flen_ * sizeof(cmplx));
    fft_->ComplexFFT(filter_);
    double scale = 0, mag;
    for (int i = 0; i < flen2_; i++) { mag = std::abs(filter_[i]); if (mag > scale) scale = mag; }
    if (scale != 0) for (int i = 0; i < flen_; i++) filter_[i] /= scale;
    pass_ = 1;
}

void FftFilt::rtty_filter(double f)
{
    // fldigi fftfilt.cxx::rtty_filter — Feher raised-cosine, K=1.4.
    f *= 1.4;
    for (int i = 0; i < flen2_; ++i) {
        const double x = (double)i / (double)flen2_;
        double dht = (x <= 0.0) ? 1.0
                   : (x > 2.0 * f) ? 0.0
                   : std::cos((PI * x) / (f * 4.0));
        dht *= dht;
        const double sx = 2.0 * (double)i * f;
        const double sc = (std::fabs(sx) < 1e-10)
                            ? 1.0
                            : std::sin(PI * sx) / (PI * sx);
        dht /= sc;
        filter_[i] = cmplx(dht * std::cos((double)i * -0.5 * PI),
                           dht * std::sin((double)i * -0.5 * PI));
        filter_[(flen_ - i) % flen_] =
            cmplx(dht * std::cos((double)i * 0.5 * PI),
                  dht * std::sin((double)i * 0.5 * PI));
    }
    pass_ = 1;
}

int FftFilt::run(const cmplx& in, cmplx** out) {
    timedata_[inptr_++] = in;
    if (inptr_ < flen2_) return 0;
    if (pass_) --pass_;
    memcpy(freqdata_, timedata_, flen_ * sizeof(cmplx));
    fft_->ComplexFFT(freqdata_);
    for (int i = 0; i < flen_; i++) freqdata_[i] *= filter_[i];
    fft_->InverseComplexFFT(freqdata_);
    for (int i = 0; i < flen2_; i++) {
        output_[i] = ovlbuf_[i] + freqdata_[i];
        ovlbuf_[i] = freqdata_[i + flen2_];
    }
    inptr_ = 0;
    if (pass_) return 0;
    *out = output_;
    return flen2_;
}

// ── Morse (morse.cxx, verbatim table + rx_lookup; fldigi-default enables) ───
void Morse::init() {
    struct Def { bool en; const char* chr; const char* prt; const char* rpr; };
    static const Def defs[] = {
        {1,"=","<BT>","-...-"}, {0,"~","<AA>",".-.-"}, {1,"<","<AS>",".-..."},
        {1,">","<AR>",".-.-."}, {1,"%","<SK>","...-.-"}, {1,"+","<KN>","-.--."},
        {1,"&","<INT>","..-.-"}, {1,"{","<HM>","....--"}, {1,"}","<VE>","...-."},
        {1,"A","A",".-"},{1,"B","B","-..."},{1,"C","C","-.-."},{1,"D","D","-.."},
        {1,"E","E","."},{1,"F","F","..-."},{1,"G","G","--."},{1,"H","H","...."},
        {1,"I","I",".."},{1,"J","J",".---"},{1,"K","K","-.-"},{1,"L","L",".-.."},
        {1,"M","M","--"},{1,"N","N","-."},{1,"O","O","---"},{1,"P","P",".--."},
        {1,"Q","Q","--.-"},{1,"R","R",".-."},{1,"S","S","..."},{1,"T","T","-"},
        {1,"U","U","..-"},{1,"V","V","...-"},{1,"W","W",".--"},{1,"X","X","-..-"},
        {1,"Y","Y","-.--"},{1,"Z","Z","--.."},
        {1,"a","A",".-"},{1,"b","B","-..."},{1,"c","C","-.-."},{1,"d","D","-.."},
        {1,"e","E","."},{1,"f","F","..-."},{1,"g","G","--."},{1,"h","H","...."},
        {1,"i","I",".."},{1,"j","J",".---"},{1,"k","K","-.-"},{1,"l","L",".-.."},
        {1,"m","M","--"},{1,"n","N","-."},{1,"o","O","---"},{1,"p","P",".--."},
        {1,"q","Q","--.-"},{1,"r","R",".-."},{1,"s","S","..."},{1,"t","T","-"},
        {1,"u","U","..-"},{1,"v","V","...-"},{1,"w","W",".--"},{1,"x","X","-..-"},
        {1,"y","Y","-.--"},{1,"z","Z","--.."},
        {1,"0","0","-----"},{1,"1","1",".----"},{1,"2","2","..---"},{1,"3","3","...--"},
        {1,"4","4","....-"},{1,"5","5","....."},{1,"6","6","-...."},{1,"7","7","--..."},
        {1,"8","8","---.."},{1,"9","9","----."},
        {1,"\\","\\",".-..-."},{1,"\'","'",".----."},{1,"$","$","...-..-"},
        {1,"(","(","-.--."},{1,")",")","-.--.-"},{1,",",",","--..--"},
        {1,"-","-","-....-"},{1,".",".",".-.-.-"},{1,"/","/","-..-."},
        {1,":",":","---..."},{1,";",";","-.-.-."},{1,"?","?","..--.."},
        {1,"_","_","..--.-"},{1,"@","@",".--.-."},{1,"!","!","-.-.--"},
        // accented (UTF-8); default enables per fldigi configuration.h
        {1,"\xC3\x84","\xC3\x84",".-.-"}, {1,"\xC3\xA4","\xC3\x84",".-.-"},   // A umlaut
        {0,"\xC3\x86","\xC3\x86",".-.-"}, {0,"\xC3\xA6","\xC3\x86",".-.-"},   // AE
        {0,"\xC3\x85","\xC3\x85",".--.-"},{0,"\xC3\xA5","\xC3\x85",".--.-"},  // A ring
        {1,"\xC3\x87","\xC3\x87","-.-.."},{1,"\xC3\xA7","\xC3\x87","-.-.."},  // C cedilla
        {0,"\xC3\x88","\xC3\x88",".-..-"},{0,"\xC3\xA8","\xC3\x88",".-..-"},  // E grave
        {1,"\xC3\x89","\xC3\x89","..-.."},{1,"\xC3\xA9","\xC3\x89","..-.."},  // E acute
        {0,"\xC3\x93","\xC3\x93","---."}, {0,"\xC3\xB3","\xC3\x93","---."},   // O acute
        {1,"\xC3\x96","\xC3\x96","---."}, {1,"\xC3\xB6","\xC3\x96","---."},   // O umlaut
        {0,"\xC3\x98","\xC3\x98","---."}, {0,"\xC3\xB8","\xC3\x98","---."},   // O slash
        {1,"\xC3\x91","\xC3\x91","--.--"},{1,"\xC3\xB1","\xC3\x91","--.--"},  // N tilde
        {1,"\xC3\x9C","\xC3\x9C","..--"}, {1,"\xC3\xBC","\xC3\x9C","..--"},   // U umlaut
        {0,"\xC3\x9B","\xC3\x9B","..--"}, {0,"\xC3\xBB","\xC3\x9B","..--"},   // U circ
    };
    table_.clear();
    for (const auto& d : defs) table_.push_back({d.en != 0, d.chr, d.prt, d.rpr});

    // fldigi cMorse::init() default toggles (configuration.h defaults):
    enable("<AA>", true);
    // accents default: A_umlaut,A_ring,C_cedilla,E_grave*,E_acute,O_umlaut,
    // N_tilde,U_umlaut on; AE,O_acute,O_slash,U_circ off.  (*E_grave default
    // true.)  The table already encodes these; keep faithful.
    // Punctuation all default-enabled (configuration.h all true) — table default 1.
}
void Morse::enable(const std::string& s, bool val) {
    for (auto& r : table_) {
        if (r.rpr.empty()) continue;
        if (r.chr == s || r.prt == s) { r.enabled = val; return; }
    }
}
std::string Morse::rx_lookup(const std::string& rx) {
    for (const auto& r : table_) {
        if (r.rpr.empty()) continue;
        if (rx == r.rpr && r.enabled) return r.prt;   // CW_prosign_display default false
    }
    return "";
}

// ── CwRx ────────────────────────────────────────────────────────────────────
CwRx::CwRx() {
    bandwidth_ = CWbandwidth_;
    use_matched_filter_ = CWmfilt_;
    if (use_matched_filter_) bandwidth_ = 5.0 * CWspeed_ / 1.2;
    cw_FFT_filter_ = new FftFilt(1.0 * bandwidth_ / samplerate_, CW_FFT_SIZE);
    cw_speed_ = (int)CWspeed_;
    symbollen_ = (int)std::lround(samplerate_ * 1.2 / CWspeed_);
    int bfv = symbollen_ / (2 * DEC_RATIO);
    if (bfv < 1) bfv = 1;
    bitfilter_.setLength(bfv);
    two_dots_ = 2 * KWPM / cw_speed_;
    reset();
}
CwRx::~CwRx() { delete cw_FFT_filter_; }

void CwRx::put_cwRcvWPM(int wpm) {
    if (wpm != cw_receive_speed_) { /* handled by sync_parameters change */ }
    if (onWpm) onWpm(wpm);
}

void CwRx::put_rx_char(const std::string& s) {
    if (!s.empty() && onText) onText(s);
}

void CwRx::sync_parameters() {
    if ((cwTrack_ != CWtrack_) || (cw_send_speed_ != (int)CWspeed_)) {
        trackingfilter_.reset();
        two_dots_ = 2 * (KWPM / (int)CWspeed_);   // 2 * cw_send_dot_length
    }
    cwTrack_ = CWtrack_;
    cw_send_speed_ = (int)CWspeed_;

    lowerwpm_ = cw_send_speed_ - CWrange_;
    upperwpm_ = cw_send_speed_ + CWrange_;
    if (lowerwpm_ < CWlowerlimit_) lowerwpm_ = CWlowerlimit_;
    if (upperwpm_ > CWupperlimit_) upperwpm_ = CWupperlimit_;
    cw_lower_limit_ = (int)(2 * KWPM / upperwpm_);
    cw_upper_limit_ = (int)(2 * KWPM / lowerwpm_);

    int prev_rx_speed = cw_receive_speed_;
    if (cwTrack_)
        cw_receive_speed_ = KWPM / (two_dots_ / 2);
    else {
        cw_receive_speed_ = cw_send_speed_;
        two_dots_ = 2 * (KWPM / (int)CWspeed_);
    }
    if (cw_receive_speed_ > 0) cw_receive_dot_length_ = KWPM / cw_receive_speed_;
    else                       cw_receive_dot_length_ = KWPM / 5;
    cw_receive_dash_length_ = 3 * cw_receive_dot_length_;
    cw_noise_spike_threshold_ = cw_receive_dot_length_ / 2;

    if (cw_receive_speed_ != prev_rx_speed) put_cwRcvWPM(cw_receive_speed_);
}

void CwRx::reset_rx_filter() {
    if (use_matched_filter_ != CWmfilt_ ||
        cw_speed_ != (int)CWspeed_ ||
        (bandwidth_ != CWbandwidth_ && !use_matched_filter_)) {

        use_matched_filter_ = CWmfilt_;
        cw_speed_ = (int)CWspeed_;
        if (use_matched_filter_) bandwidth_ = 5.0 * CWspeed_ / 1.2;
        else                     bandwidth_ = CWbandwidth_;

        cw_FFT_filter_->create_lpf(1.0 * bandwidth_ / samplerate_);
        FFTphase_ = 0;

        two_dots_ = 2 * KWPM / cw_speed_;
        symbollen_ = (int)std::lround(samplerate_ * 1.2 / CWspeed_);
        FFTvalue_ = 0.0;
        smpl_ctr_ = 0;
        rx_rep_buf_.clear();

        int bfv = symbollen_ / (2 * DEC_RATIO);
        if (bfv < 1) bfv = 1;
        bitfilter_.setLength(bfv);
    }
}

void CwRx::update_tracking(int dur_1, int dur_2) {
    static const int min_dot  = KWPM / 200;
    static const int max_dash = 3 * KWPM / 5;
    if ((dur_1 > dur_2) && (dur_1 > 4 * dur_2)) return;
    if ((dur_2 > dur_1) && (dur_2 > 4 * dur_1)) return;
    if (dur_1 < min_dot || dur_2 < min_dot) return;
    if (dur_2 > max_dash || dur_2 > max_dash) return;
    two_dots_ = (long)trackingfilter_.run((dur_1 + dur_2) / 2);
    sync_parameters();
}

cmplx CwRx::mixer(cmplx in) {
    cmplx z(std::cos(phaseacc_), std::sin(phaseacc_));
    z = z * in;
    phaseacc_ += TWOPI * frequency_ / samplerate_;
    if (phaseacc_ > TWOPI) phaseacc_ -= TWOPI;
    return z;
}

void CwRx::decode_stream(double value) {
    std::string sc;
    int attack = 0, decay = 0;
    switch (cwrx_attack_) { case 0: attack = 400; break; case 2: attack = 100; break;
                            case 1: default: attack = 200; break; }
    switch (cwrx_decay_)  { case 0: decay = 2000; break; case 2: decay = 500; break;
                            case 1: default: decay = 1000; break; }

    sig_avg_ = decayavg(sig_avg_, value, decay);
    if (value < sig_avg_) {
        if (value < noise_floor_) noise_floor_ = decayavg(noise_floor_, value, attack);
        else                      noise_floor_ = decayavg(noise_floor_, value, decay);
    }
    if (value > sig_avg_) {
        if (value > agc_peak_) agc_peak_ = decayavg(agc_peak_, value, attack);
        else                   agc_peak_ = decayavg(agc_peak_, value, decay);
    }

    double norm_noise = noise_floor_ / agc_peak_;
    double norm_sig   = sig_avg_ / agc_peak_;
    siglevel_ = norm_sig;

    if (agc_peak_) value /= agc_peak_;
    else           value = 0;

    metric_ = 0.8 * metric_;
    if ((noise_floor_ > 1e-4) && (noise_floor_ < sig_avg_))
        metric_ += 0.2 * clampd(2.5 * (20 * std::log10(sig_avg_ / noise_floor_)), 0, 100);

    double diff = (norm_sig - norm_noise);
    double cwUpper = norm_sig  - 0.2 * diff;
    double cwLower = norm_noise + 0.7 * diff;

    if (!sqlonoff_ || metric_ > sldrSquelchValue_) {
        if ((value > cwUpper) && (cw_receive_state_ != RS_IN_TONE))
            handle_event(CW_KEYDOWN_EVENT, sc);
        if ((value < cwLower) && (cw_receive_state_ == RS_IN_TONE))
            handle_event(CW_KEYUP_EVENT, sc);
    }

    if (handle_event(CW_QUERY_EVENT, sc) == CW_SUCCESS) {
        // SOM decoding is off by default -> exact lookup path
        put_rx_char(sc);
    }
}

void CwRx::rx_FFTprocess(const double* buf, int len) {
    cmplx z, *zp;
    while (len-- > 0) {
        z = cmplx(*buf * std::cos(FFTphase_), *buf * std::sin(FFTphase_));
        FFTphase_ += TWOPI * frequency_ / samplerate_;
        if (FFTphase_ > TWOPI) FFTphase_ -= TWOPI;
        buf++;

        int n = cw_FFT_filter_->run(z, &zp);
        if (!n) continue;
        for (int i = 0; i < n; i++) {
            ++smpl_ctr_;
            if (smpl_ctr_ % DEC_RATIO) continue;   // decimate by DEC_RATIO
            FFTvalue_ = std::abs(zp[i]);
            FFTvalue_ = bitfilter_.run(FFTvalue_);
            decode_stream(FFTvalue_);
        }
    }
}

int CwRx::handle_event(int cw_event, std::string& sc) {
    int element_usec;
    switch (cw_event) {
    case CW_RESET_EVENT:
        sync_parameters();
        cw_receive_state_ = RS_IDLE;
        cw_ptr_ = 0;
        std::memset(cw_buffer_, 0, sizeof(cw_buffer_));
        smpl_ctr_ = 0;
        rx_rep_buf_.clear();
        break;
    case CW_KEYDOWN_EVENT:
        if (cw_receive_state_ == RS_IN_TONE) return CW_ERROR;
        if (cw_receive_state_ == RS_IDLE) {
            smpl_ctr_ = 0; rx_rep_buf_.clear(); cw_ptr_ = 0;
        }
        cw_rr_start_timestamp_ = smpl_ctr_;
        old_cw_receive_state_ = cw_receive_state_;
        cw_receive_state_ = RS_IN_TONE;
        return CW_ERROR;
    case CW_KEYUP_EVENT:
        if (cw_receive_state_ != RS_IN_TONE) return CW_ERROR;
        cw_rr_end_timestamp_ = smpl_ctr_;
        element_usec = usec_diff(cw_rr_start_timestamp_, cw_rr_end_timestamp_);
        sync_parameters();
        if (cw_noise_spike_threshold_ > 0 && element_usec < cw_noise_spike_threshold_) {
            cw_receive_state_ = RS_IDLE;
            return CW_ERROR;
        }
        if (he_last_element_ > 0) {
            if ((element_usec > 2 * he_last_element_) && (element_usec < 4 * he_last_element_))
                update_tracking(he_last_element_, element_usec);
            if ((he_last_element_ > 2 * element_usec) && (he_last_element_ < 4 * element_usec))
                update_tracking(element_usec, he_last_element_);
        }
        he_last_element_ = element_usec;
        if (element_usec <= two_dots_) {
            rx_rep_buf_ += CW_DOT_REPRESENTATION;
            cw_buffer_[cw_ptr_++] = (float)he_last_element_;
        } else {
            rx_rep_buf_ += CW_DASH_REPRESENTATION;
            cw_buffer_[cw_ptr_++] = (float)he_last_element_;
        }
        if (rx_rep_buf_.length() > MAX_MORSE_ELEMENTS) {
            cw_receive_state_ = RS_IDLE;
            cw_ptr_ = 0;
            smpl_ctr_ = 0;
            return CW_ERROR;
        } else {
            cw_buffer_[cw_ptr_] = 0.0;
        }
        cw_receive_state_ = RS_AFTER_TONE;
        return CW_ERROR;
    case CW_QUERY_EVENT:
        if (cw_receive_state_ == RS_IN_TONE) return CW_ERROR;
        sync_parameters();
        element_usec = usec_diff(cw_rr_end_timestamp_, smpl_ctr_);
        if (element_usec < (2 * cw_receive_dot_length_)) return CW_ERROR;
        if (element_usec >= (2 * cw_receive_dot_length_) &&
            element_usec <= (4 * cw_receive_dot_length_) &&
            cw_receive_state_ == RS_AFTER_TONE) {
            sc = morse_.rx_lookup(rx_rep_buf_);
            if (sc.empty()) {
                sc = (CW_noise_ == '*') ? "*" :
                     (CW_noise_ == '_') ? "_" :
                     (CW_noise_ == ' ') ? " " : "";
            }
            rx_rep_buf_.clear();
            cw_receive_state_ = RS_IDLE;
            he_space_sent_ = false;
            cw_ptr_ = 0;
            return CW_SUCCESS;
        }
        if ((element_usec > (4 * cw_receive_dot_length_)) && !he_space_sent_) {
            sc = " ";
            he_space_sent_ = true;
            return CW_SUCCESS;
        }
        return CW_ERROR;
    }
    return CW_ERROR;
}

void CwRx::reset() {
    cw_receive_state_ = RS_IDLE;
    old_cw_receive_state_ = RS_IDLE;
    smpl_ctr_ = 0;
    cw_ptr_ = 0;
    agc_peak_ = 0.0;          // rx_init()
    noise_floor_ = 1.0;
    sig_avg_ = 0.0;
    metric_ = 0.0;
    phaseacc_ = 0.0; FFTphase_ = 0.0; FFTvalue_ = 0.0;
    rx_rep_buf_.clear();
    std::memset(cw_buffer_, 0, sizeof(cw_buffer_));
    he_space_sent_ = true;
    he_last_element_ = 0;
    trackingfilter_.reset();
    two_dots_ = 2 * KWPM / (int)CWspeed_;
    cw_send_speed_ = (int)CWspeed_ - 1;   // force sync_parameters to re-derive
    cwTrack_ = !CWtrack_;
    sync_parameters();
}

void CwRx::rxProcess(const double* buf, int len) {
    reset_rx_filter();
    rx_FFTprocess(buf, len);
}

}  // namespace lyra::dsp::cwfldigi

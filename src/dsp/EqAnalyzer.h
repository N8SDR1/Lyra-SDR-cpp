// Lyra — TX EQ live analyzer (#50 Stage 2b).
//
// A tiny real-input magnitude-FFT engine that backs the spectrum behind the
// EQ curve.  The CMaster TX mic-rack pump feeds it the I channel of each
// block TWICE per call — once pre-EQ, once post-EQ — through feed(); when a
// full FFT frame has accumulated it windows + transforms both and publishes
// dBFS magnitude bins for the GUI to draw.  Mic audio only flows while
// transmitting, so the analyzer animates on TX and freezes on RX (correct
// for shaping transmit audio).
//
// Qt-free + dependency-free so it unit-tests standalone; power-of-two FFT
// size only (default 1024 → 46.9 Hz bins at 48 kHz).  feed() runs on the TX
// thread; snapshot() runs on the GUI thread; the published buffers are
// guarded by a short mutex (the FFT math happens OUTSIDE the lock).

#pragma once

#include <mutex>
#include <vector>

namespace lyra::dsp {

class EqAnalyzer {
public:
    explicit EqAnalyzer(int fftSize = 1024, double sampleRate = 48000.0);

    void   setSampleRate(double fs) { fs_ = fs; }
    int    fftSize() const { return n_; }
    int    bins()    const { return n_ / 2 + 1; }   // DC … Nyquist inclusive
    double nyquist() const { return fs_ * 0.5; }

    // TX thread.  `pre` is a contiguous block of pre-EQ I samples (length n);
    // `postIq` is the post-EQ interleaved {I,Q} buffer (post sample k =
    // postIq[2*k]).  Both advance one shared fill counter in lockstep, so
    // the published pre/post frames are always sample-aligned.
    void feed(const double *pre, const double *postIq, int n);

    // GUI thread.  Copies the latest published dBFS bins into preDb/postDb
    // (each resized to bins()).  Returns true iff a new frame was published
    // since the previous snapshot (so the caller can skip idle repaints).
    bool snapshot(std::vector<float> &preDb, std::vector<float> &postDb);

    void reset();

private:
    void transform();   // window + FFT both accumulators, publish
    static void fft(std::vector<float> &re, std::vector<float> &im);

    int    n_;
    double fs_;
    std::vector<double> win_;        // Hann analysis window
    double winNorm_ = 1.0;           // amplitude-correct normaliser
    std::vector<double> preAcc_, postAcc_;
    int    fill_ = 0;

    std::mutex mtx_;
    std::vector<float> prePub_, postPub_;   // dBFS bins (length bins())
    bool   dirty_ = false;
};

}  // namespace lyra::dsp

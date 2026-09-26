// Host feed into WDSP pscc. WDSP owns the calc thread; this only
// accumulates EP6 DDC0/DDC1 pairs. sip1 is the TX panadapter, not this.

#pragma once

#include <atomic>
#include <mutex>
#include <vector>

namespace lyra::ps {

class PsCalcThread {
public:
    static PsCalcThread &instance();

    void setRun(bool on);
    bool running() const;

    // Interleaved I/Q, spr complex samples each. tx = DDC1, rx = DDC0.
    void feed(int spr, const double *rxDdc0, const double *txDdc1);

    // Last poll window: peak |z| on DDC0/DDC1 and complex-sample count
    // into this feed. n==0 means EP6 did not call feed (mux/pscc path idle).
    struct FeedDiag {
        float peakDdc0 = 0.f;
        float peakDdc1 = 0.f;
        int   spr      = 0;
    };
    FeedDiag takeFeedDiag();

private:
    PsCalcThread() = default;
    std::mutex mu_;
    std::vector<double> tx_;
    std::vector<double> rx_;
    std::atomic<bool> run_{false};
    float peakSq0_ = 0.f;
    float peakSq1_ = 0.f;
    int   sprAcc_  = 0;
};

}  // namespace lyra::ps

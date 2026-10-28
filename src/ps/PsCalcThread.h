// Host feed into WDSP pscc. WDSP owns the calc thread; this only
// accumulates coupler IQ plus the TX replica. P1 uses EP6 DDC0/DDC1.
// P2 uses radio DDC0 even (coupler) + host DUC IQ (replica). sip1 is
// the TX panadapter, not this.

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

    // Host TX IQ at the DUC rate (P2 = 192 kHz). The radio's interleaved
    // DDC0 odd samples are the TX-DAC echo; when those are empty, pscc
    // still needs this replica or collect never starts (D1 dead, FB 0).
    void pushHostTx(int spr, const double *txIq);

    // Interleaved I/Q, spr complex samples each. tx = replica, rx = coupler.
    void feed(int spr, const double *rxDdc0, const double *txDdc1);

    // Coupler from the radio; TX from the host DUC tap (2:1 when the
    // DDC0 UDP is interleaved). No-op until the tap has enough samples.
    void feedCoupler(int spr, const double *rxDdc0);

    // Last poll window: peak |z| on DDC0/DDC1 and complex-sample count
    // into this feed. n==0 means no host feed (P1 EP6 or P2 IQ pair idle).
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
    std::vector<double> hostTx_;
    std::atomic<bool> run_{false};
    float peakSq0_ = 0.f;
    float peakSq1_ = 0.f;
    int   sprAcc_  = 0;
};

// Map host DUC IQ onto coupler pairs. Interleaved DDC0 has one coupler
// sample per two DUC samples (238 slots → 119 pairs). Tail of the host
// ring so TX leads the delayed coupler.
inline int copyHostTxForCoupler(const double *host, int hostSpr,
                                double *txOut, int pairSpr) noexcept
{
    if (!host || !txOut || pairSpr <= 0 || hostSpr <= 0)
        return 0;
    if (hostSpr >= pairSpr * 2) {
        const int start = hostSpr - pairSpr * 2;
        for (int i = 0; i < pairSpr; ++i) {
            const int h = start + i * 2;
            txOut[2 * i]     = host[2 * h];
            txOut[2 * i + 1] = host[2 * h + 1];
        }
        return pairSpr;
    }
    if (hostSpr >= pairSpr) {
        const int start = hostSpr - pairSpr;
        for (int i = 0; i < pairSpr; ++i) {
            txOut[2 * i]     = host[2 * (start + i)];
            txOut[2 * i + 1] = host[2 * (start + i) + 1];
        }
        return pairSpr;
    }
    return 0;
}

}  // namespace lyra::ps

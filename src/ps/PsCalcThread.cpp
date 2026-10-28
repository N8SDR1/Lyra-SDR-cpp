#include "ps/PsCalcThread.h"

#include "wire/wdspcalls.h"

#include <cmath>
#include <cstddef>

namespace lyra::ps {

namespace {
constexpr int kBlock = 128;
}

PsCalcThread &PsCalcThread::instance() {
    static PsCalcThread g;
    return g;
}

void PsCalcThread::setRun(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    run_.store(on, std::memory_order_release);
    if (!on) {
        tx_.clear();
        rx_.clear();
        peakSq0_ = 0.f;
        peakSq1_ = 0.f;
        sprAcc_  = 0;
    }
}

void PsCalcThread::pushHostTx(int spr, const double *txIq) {
    if (spr <= 0 || !txIq) return;
    std::lock_guard<std::mutex> lk(mu_);
    hostTx_.insert(hostTx_.end(), txIq, txIq + static_cast<size_t>(spr) * 2);
    constexpr size_t kCap = 16384;  // 8192 complex @ 192 kHz ≈ 43 ms
    if (hostTx_.size() > kCap) {
        const size_t drop = hostTx_.size() - kCap;
        const size_t even = drop + (drop % 2);
        hostTx_.erase(hostTx_.begin(),
                      hostTx_.begin() + static_cast<std::ptrdiff_t>(even));
    }
}

bool PsCalcThread::running() const {
    return run_.load(std::memory_order_acquire);
}

void PsCalcThread::feed(int spr, const double *rxDdc0, const double *txDdc1) {
    if (spr <= 0 || !rxDdc0 || !txDdc1) return;
    std::lock_guard<std::mutex> lk(mu_);
    for (int i = 0; i < spr; ++i) {
        const double rI = rxDdc0[2 * i];
        const double rQ = rxDdc0[2 * i + 1];
        const double tI = txDdc1[2 * i];
        const double tQ = txDdc1[2 * i + 1];
        const float r2 = static_cast<float>(rI * rI + rQ * rQ);
        const float t2 = static_cast<float>(tI * tI + tQ * tQ);
        if (r2 > peakSq0_) peakSq0_ = r2;
        if (t2 > peakSq1_) peakSq1_ = t2;
    }
    sprAcc_ += spr;
    if (!lyra::wire::pscc) return;
    if (!run_.load(std::memory_order_relaxed)) return;
    tx_.reserve(tx_.size() + static_cast<size_t>(spr) * 2);
    rx_.reserve(rx_.size() + static_cast<size_t>(spr) * 2);
    for (int i = 0; i < spr; ++i) {
        rx_.push_back(rxDdc0[2 * i]);
        rx_.push_back(rxDdc0[2 * i + 1]);
        tx_.push_back(txDdc1[2 * i]);
        tx_.push_back(txDdc1[2 * i + 1]);
    }
    while (static_cast<int>(tx_.size()) >= kBlock * 2) {
        lyra::wire::pscc(1, kBlock, tx_.data(), rx_.data());
        tx_.erase(tx_.begin(), tx_.begin() + kBlock * 2);
        rx_.erase(rx_.begin(), rx_.begin() + kBlock * 2);
    }
}

void PsCalcThread::feedCoupler(int spr, const double *rxDdc0) {
    if (spr <= 0 || !rxDdc0) return;
    std::vector<double> tx(static_cast<size_t>(spr) * 2);
    {
        std::lock_guard<std::mutex> lk(mu_);
        const int hostSpr = static_cast<int>(hostTx_.size() / 2);
        if (copyHostTxForCoupler(hostTx_.data(), hostSpr, tx.data(), spr) != spr)
            return;
    }
    feed(spr, rxDdc0, tx.data());
}

PsCalcThread::FeedDiag PsCalcThread::takeFeedDiag() {
    std::lock_guard<std::mutex> lk(mu_);
    FeedDiag d;
    d.peakDdc0 = std::sqrt(peakSq0_);
    d.peakDdc1 = std::sqrt(peakSq1_);
    d.spr      = sprAcc_;
    peakSq0_ = 0.f;
    peakSq1_ = 0.f;
    sprAcc_  = 0;
    return d;
}

}  // namespace lyra::ps

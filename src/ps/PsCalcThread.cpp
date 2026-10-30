#include "ps/PsCalcThread.h"

#include "wire/wdspcalls.h"

#include <cmath>

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

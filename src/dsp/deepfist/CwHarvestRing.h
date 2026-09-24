// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — Phase 2 harvest: rolling mono audio ring at the DeepFist rate
// (3200 Hz).  The audio thread push()es decimated samples under a short mutex
// (same discipline as NeuralCwDecoder's window ring); the harvester worker
// snapshot()s the last N samples when a capture trigger fires.  Snapshots
// zero-pad the oldest side when less audio than requested has been pushed.
#pragma once

#include <algorithm>
#include <mutex>
#include <vector>

namespace lyra::dsp {

class CwHarvestRing {
public:
    explicit CwHarvestRing(int capacitySamples)
        : buf_(static_cast<size_t>(capacitySamples), 0.0f) {}

    // audio thread — memcpy under the lock, no allocation.
    void push(const float* x, int n) {
        if (!x || n <= 0) return;
        std::lock_guard<std::mutex> lk(mx_);
        const size_t cap = buf_.size();
        for (int i = std::max(0, n - static_cast<int>(cap)); i < n; ++i) {
            buf_[head_] = x[i];
            head_ = (head_ + 1) % cap;
        }
        total_ += n;
    }

    // any thread — newest `lastNSamples`, oldest first, zero-padded in front
    // when fewer samples have ever been pushed.
    std::vector<float> snapshot(int lastNSamples) const {
        std::vector<float> out(static_cast<size_t>(std::max(0, lastNSamples)), 0.0f);
        std::lock_guard<std::mutex> lk(mx_);
        const size_t cap = buf_.size();
        const long long have = std::min<long long>(total_, static_cast<long long>(cap));
        const long long take = std::min<long long>(have, lastNSamples);
        // copy the newest `take` samples to the END of out (front stays zero)
        for (long long i = 0; i < take; ++i) {
            const size_t src = (head_ + cap - static_cast<size_t>(take - i)) % cap;
            out[out.size() - static_cast<size_t>(take - i)] = buf_[src];
        }
        return out;
    }

    long long totalPushed() const {
        std::lock_guard<std::mutex> lk(mx_);
        return total_;
    }

private:
    mutable std::mutex mx_;
    std::vector<float> buf_;
    size_t             head_ = 0;
    long long          total_ = 0;
};

}  // namespace lyra::dsp

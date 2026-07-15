#include "dsp/CwArbiter.h"

namespace lyra::dsp {

CwArbiter::CwArbiter(Config cfg) : cfg_(cfg) {}

bool CwArbiter::isGap(const std::string& text) {
    return text.find(' ') != std::string::npos;
}

void CwArbiter::updateKeying(float ratio) {
    std::lock_guard<std::mutex> lk(mx_);
    if (!seeded_) {
        // Cold start (spec §5.3): the first ratio sample after reset() seeds
        // ownership directly — fading goes straight to the Classic safety net,
        // anything else to DeepFist — so Auto never enters silent mid-fade
        // waiting for a keying gap that the gated engine may never emit.
        seeded_ = true;
        owner_ = desired_ =
            (ratio < cfg_.tLow) ? Source::Classic : Source::DeepFist;
    }
    if (ratio < cfg_.tLow) {
        solidCount_ = 0;
        if (++fadeCount_ >= cfg_.nFall) desired_ = Source::Classic;
    } else if (ratio > cfg_.tHigh) {
        fadeCount_ = 0;
        if (++solidCount_ >= cfg_.nRise) desired_ = Source::DeepFist;
    }
    // Dead-band (tLow..tHigh): hold counts and desired_ — no progress either way.
}

void CwArbiter::pushDeepFist(const std::string& text) {
    std::string out;
    bool fire = false;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (owner_ != Source::DeepFist) return;          // muted -> drop
        out = text; fire = true;
        if (isGap(text) && desired_ != owner_) owner_ = desired_;  // switch at gap
    }
    if (fire && onOutput) onOutput(out, /*fallback=*/false);
}

void CwArbiter::pushClassic(const std::string& text) {
    std::string out;
    bool fire = false;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (owner_ != Source::Classic) return;           // muted -> drop
        out = text; fire = true;
        if (isGap(text) && desired_ != owner_) owner_ = desired_;  // switch at gap
    }
    if (fire && onOutput) onOutput(out, /*fallback=*/true);
}

void CwArbiter::reset() {
    std::lock_guard<std::mutex> lk(mx_);
    owner_ = Source::DeepFist;
    desired_ = Source::DeepFist;
    fadeCount_ = 0;
    solidCount_ = 0;
    seeded_ = false;
}

CwArbiter::Source CwArbiter::owner() const {
    std::lock_guard<std::mutex> lk(mx_);
    return owner_;
}

}  // namespace lyra::dsp

#include "dsp/CwArbiter.h"

namespace lyra::dsp {

CwArbiter::CwArbiter(Config cfg) : cfg_(cfg) {}

bool CwArbiter::isGap(const std::string& text) {
    return text.find(' ') != std::string::npos;
}

void CwArbiter::updateKeying(float ratio) {
    std::lock_guard<std::mutex> lk(mx_);
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
}

CwArbiter::Source CwArbiter::owner() const {
    std::lock_guard<std::mutex> lk(mx_);
    return owner_;
}

}  // namespace lyra::dsp

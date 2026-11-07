// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Rick Langford (N8SDR) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/DeepFistCommit.h"

#include <algorithm>
#include <cmath>

namespace lyra::dsp {

namespace {
constexpr double kAgreeTolS  = 0.09;   // same token within this -> agreement
constexpr double kPastTokenS = 0.06;   // commit point sits past a token's jitter
}  // namespace

std::vector<DeepFistCommit::Emitted>
DeepFistCommit::step(const CtcFrames& fr, double audioEnd, double windowS) {
    std::vector<Emitted> out;
    const double winStart = audioEnd - windowS;
    const double settleTo = audioEnd - cfg_.guardS;
    const int    denom    = std::max(1, fr.T);
    auto timeOf = [&](size_t i) {
        return winStart + (static_cast<double>(fr.frames[i]) / denom) * windowS;
    };

    if (!cfg_.agree) {
        for (size_t i = 0; i < fr.ids.size(); ++i) {
            const double ct = timeOf(i);
            if (ct > committed_ && ct <= settleTo) out.push_back({fr.ids[i], ct});
        }
        committed_ = std::max(committed_, settleTo);
        return out;
    }

    // This decode's tokens after the commit point, clear of the window edge.
    std::vector<std::pair<int, double>> hyp;
    for (size_t i = 0; i < fr.ids.size(); ++i) {
        const double ct = timeOf(i);
        if (ct > committed_ && ct <= audioEnd - cfg_.edgeS) hyp.emplace_back(fr.ids[i], ct);
    }
    // Commit the leading run the previous decode also had; anything older
    // than guardS commits regardless.
    size_t k = 0;
    for (; k < hyp.size(); ++k) {
        bool ok = hyp[k].second <= settleTo;
        if (!ok && k < prev_.size())
            ok = prev_[k].first == hyp[k].first &&
                 std::fabs(prev_[k].second - hyp[k].second) < kAgreeTolS;
        if (!ok) break;
        out.push_back({hyp[k].first, hyp[k].second});
        committed_ = hyp[k].second + kPastTokenS;
    }
    prev_.assign(hyp.begin() + static_cast<std::ptrdiff_t>(k), hyp.end());
    return out;
}

void DeepFistCommit::idle(double audioEnd) {
    committed_ = std::max(committed_, audioEnd - cfg_.guardS);
    prev_.clear();
}

void DeepFistCommit::reset() {
    committed_ = 0.0;
    prev_.clear();
}

}  // namespace lyra::dsp

// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/ScpLocal.h"

#include <algorithm>
#include <cstdio>

namespace lyra::dsp {

namespace {
constexpr long long kRefreshSec = 86400;   // persist a timestamp refresh daily
}

int ScpLocal::load(const std::string& path, int maxAgeDays, int cap) {
    path_ = path;
    cap_  = cap;
    byCall_.clear();

    FILE* f = std::fopen(path.c_str(), "rb");
    if (f) {
        char line[256];
        long long newest = 0;
        while (std::fgets(line, sizeof line, f)) {
            std::string s(line);
            const auto tab = s.find('\t');
            if (tab == std::string::npos || tab == 0) continue;
            const std::string call = s.substr(0, tab);
            long long t = 0;
            try { t = std::stoll(s.substr(tab + 1)); } catch (...) { continue; }
            if (t <= 0) continue;
            byCall_[call] = std::max(byCall_[call], t);
            newest = std::max(newest, t);
        }
        std::fclose(f);

        // Age-out relative to the newest entry: no wall clock -> deterministic
        // in tests, and a rig that sat powered off for a year doesn't wake up
        // to an empty list.
        const long long cutoff = newest - static_cast<long long>(maxAgeDays) * 86400;
        for (auto it = byCall_.begin(); it != byCall_.end();)
            it = (it->second < cutoff) ? byCall_.erase(it) : std::next(it);
        enforceCap();
    }
    return count();
}

bool ScpLocal::note(const std::string& call, long long nowEpochSec) {
    if (call.empty()) return false;
    auto it = byCall_.find(call);
    if (it != byCall_.end() && nowEpochSec - it->second < kRefreshSec)
        return false;                          // same-day repeat: no churn
    byCall_[call] = nowEpochSec;
    enforceCap();
    save();
    return true;
}

std::vector<std::string> ScpLocal::calls() const {
    std::vector<std::string> out;
    out.reserve(byCall_.size());
    for (const auto& [c, t] : byCall_) out.push_back(c);
    return out;                                // map iteration = sorted
}

void ScpLocal::enforceCap() {
    while (static_cast<int>(byCall_.size()) > cap_) {
        auto oldest = byCall_.begin();
        for (auto it = byCall_.begin(); it != byCall_.end(); ++it)
            if (it->second < oldest->second) oldest = it;
        byCall_.erase(oldest);
    }
}

void ScpLocal::save() const {
    if (path_.empty()) return;
    const std::string tmp = path_ + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;                            // read-only dir: degrade silently
    for (const auto& [c, t] : byCall_)
        std::fprintf(f, "%s\t%lld\n", c.c_str(), t);
    std::fclose(f);
    std::remove(path_.c_str());                // Windows rename needs the target gone
    std::rename(tmp.c_str(), path_.c_str());
}

}  // namespace lyra::dsp

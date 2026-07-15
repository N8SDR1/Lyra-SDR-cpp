// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — Phase 3 RBN→SCP loop: the local list of callsigns actually copied AND
// RBN/cluster-confirmed at this station.  Persisted as "CALL<TAB>epoch" lines;
// merged into DeepFistScp at model load (rescorer candidates) and consulted
// live by the CW panel's amber call highlight (WdspEngine::cwCallKnown).
//
// Deliberately tiny: a call->last-heard map with a size cap (oldest evicted),
// an age-out at load, and an atomic-ish tmp+rename rewrite on change.  spec
// §6.4/§6.6 — opt-in feature, capped, aged, local-only.
//
// NOT thread-safe: GUI-thread only (plus a read-only calls() snapshot handed
// to DeepFistScp::addCalls before the decode worker starts).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace lyra::dsp {

class ScpLocal {
public:
    // Read `path` (missing file = empty store, not an error), drop entries
    // older than maxAgeDays (measured against the newest entry, so tests and
    // long-idle rigs behave identically), enforce the cap.  Returns the count.
    int load(const std::string& path, int maxAgeDays = 365, int cap = 10000);

    // Record `call` (upper-case) heard-and-confirmed at unix time `now`.
    // New call, or an existing one >1 day stale, persists to disk and returns
    // true; a same-day repeat is a no-op (no file churn at contest rates).
    bool note(const std::string& call, long long nowEpochSec);

    bool contains(const std::string& call) const {
        return byCall_.find(call) != byCall_.end();
    }
    std::vector<std::string> calls() const;      // sorted (map order)
    int count() const { return static_cast<int>(byCall_.size()); }

private:
    void enforceCap();
    void save() const;

    std::string                     path_;
    int                             cap_ = 10000;
    std::map<std::string, long long> byCall_;    // call -> last-heard epoch
};

}  // namespace lyra::dsp

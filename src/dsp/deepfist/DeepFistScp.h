// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — DeepFist CW decoder: Super Check Partial callsign database.
//
// Minimal C++ port of diddle's ScpDb (src/scp.rs), just the surface the CTC
// lattice rescorer needs: load MASTER.SCP (community callsign list from
// supercheckpartial.com) and return the callsigns within a small edit distance
// of a decoded token, nearest first.  The rescorer then asks the model which of
// those candidates the audio actually supports (see DeepFistRescore).
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace lyra::dsp {

class DeepFistScp {
public:
    // Load callsigns from a MASTER.SCP file (skips !/# header lines, upper-cases,
    // keeps call-shaped tokens, sorts+dedups).  Returns the number loaded, 0 on
    // failure.  Safe to call once at startup.
    int loadFile(const std::string& path);

    bool ready() const { return !calls_.empty(); }
    int  count() const { return static_cast<int>(calls_.size()); }

    // All SCP calls within `maxEdit` of `token`, nearest first, capped at
    // `maxCands`.  Empty for tokens shorter than 3 chars.
    std::vector<std::string> candidates(const std::string& token,
                                        int maxEdit, int maxCands) const;

    // Exact membership: is `call` (upper-case) in the database?  O(log n) —
    // calls_ is sorted.  Used by the CW panel to colour known-real callsigns.
    bool contains(const std::string& call) const {
        return std::binary_search(calls_.begin(), calls_.end(), call);
    }

    // Merge extra calls (upper-case) into the database — Phase 3 scp_local
    // feed.  Sorts + dedups so contains()/candidates() stay correct.  NOT
    // thread-safe: call before the decode worker starts (loadModel path).
    void addCalls(const std::vector<std::string>& extra);

private:
    std::vector<std::string> calls_;    // sorted, upper-case, deduped
};

}  // namespace lyra::dsp

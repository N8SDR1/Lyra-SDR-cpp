// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — Phase 2 harvest core (spec §6.1-6.3): turns arbiter/RBN events into
// trust-tiered capture segments — int16@3200 WAV + JSON sidecar — for the
// DeepFist training pipeline.  Curation into training clips happens OFFLINE in
// the deepfist repo (tools/ already window + label long captures); this class
// only captures faithfully and records the adjudication evidence.
//
// v1 tiers/triggers (SILVER, agreement-GOLD and SCP-margin-GOLD deferred):
//   hard_negative — arbiter DeepFist->Classic switch (a fade the gate lost);
//                   text NOT trusted, captured for channel statistics.
//   gold_rbn      — a decoded call confirmed live on RBN/cluster (the Learn
//                   tap moment); the strongest external label evidence.
//
// Threading: feed*/trigger* are thread-safe (short mutex; no I/O).  All disk
// work happens in pump(), which the owner calls ~1 Hz from its own worker
// thread — never the audio or GUI thread.
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "dsp/deepfist/CwHarvestRing.h"

namespace lyra::dsp {

class CwCaptureHarvester {
public:
    struct Config {
        int       sampleRate  = 3200;
        int       preSec      = 20;    // audio before the trigger
        int       postSec     = 10;    // post-roll for fade captures
        int       debounceSec = 30;    // min gap between segments per tier
        long long capBytes    = 2LL * 1024 * 1024 * 1024;   // retention cap
    };

    CwCaptureHarvester(CwHarvestRing& ring, std::string outDir, Config cfg = {});

    // event intake — any thread, cheap, no I/O
    void feedKeying(float ratio, long long nowSec);
    void feedText(bool fromClassic, const std::string& text);
    void feedWpm(int wpm);                                    // latest RX estimate
    void triggerFade(long long nowSec);                       // hard_negative
    void triggerGoldRbn(const std::string& call, long long nowSec);

    // worker heartbeat (~1 Hz): completes pending post-rolls, enforces the
    // retention cap.  ALL file I/O lives here.
    void pump(long long nowSec);

    int segmentsWritten() const;

private:
    struct Pending {
        std::string tier;
        std::string call;
        long long   triggeredAt = 0;
        int         postSec     = 0;    // 0 = write on next pump
    };

    void writeSegment(const Pending& p, long long nowSec);
    void enforceCap();

    CwHarvestRing&      ring_;
    const std::string   dir_;
    const Config        cfg_;

    mutable std::mutex  mx_;
    std::deque<std::pair<long long, float>> keyTrace_;   // (t, ratio), last 60
    std::string         textClassic_, textDeepFist_;     // last ~200 chars each
    int                 lastWpm_ = 0;                    // latest RX WPM estimate
    std::vector<Pending> pending_;
    long long           lastFadeAt_ = -1000000;          // debounce, PER TIER
    long long           lastGoldAt_ = -1000000;
    int                 written_   = 0;
    int                 seq_       = 0;
};

}  // namespace lyra::dsp

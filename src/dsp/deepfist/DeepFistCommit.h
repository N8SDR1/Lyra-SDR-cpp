// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Rick Langford (N8SDR) - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — DeepFist neural CW decoder: streaming commit.
//
// The live decoder re-decodes a rolling window several times a second; this
// decides which decoded characters are final and may be shown.  Two modes:
//
//   Guard  — the original frame-timed commit: a character is shown once its
//            audio is older than guardS.  Fixed delay; a character whose frame
//            time shifts across the commit point between two decodes can be
//            lost (this is where dropped word spaces came from).
//   Agree  — a character is shown as soon as two consecutive decodes agree on
//            it (same token, same place within 90 ms), never within edgeS of
//            the newest audio, and always once it is older than guardS so a
//            disagreeing pair can't stall the copy.  On the offline bench this
//            halved the error rate and cut the average delay from ~1.4 s to
//            ~1.0 s (scratch/test_cw_neural_bench.cpp).
//
// Pure logic, no threads: the decoder calls step() per decoded window and
// idle() when the keying gate skips one.  Shared with the offline bench so the
// two can't drift apart.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "dsp/deepfist/DeepFistCtc.h"

namespace lyra::dsp {

class DeepFistCommit {
public:
    struct Config {
        bool   agree  = true;
        double guardS = 1.3;    // Guard: the delay.  Agree: the hard cap.
        double edgeS  = 0.30;   // Agree: newest audio never committed
    };

    struct Emitted {
        int    id   = 0;        // token id
        double time = 0.0;      // audio time of the token (s)
    };

    void setConfig(const Config& c) { cfg_ = c; }
    const Config& config() const { return cfg_; }

    // A decoded window ending at audioEnd (s), windowS long.  Returns the
    // tokens that became final, in order.
    std::vector<Emitted> step(const CtcFrames& fr, double audioEnd, double windowS);

    // The keying gate skipped a window ending at audioEnd: nothing to show,
    // move the commit point past the silence.
    void idle(double audioEnd);

    void reset();

private:
    Config cfg_;
    double committed_ = 0.0;
    std::vector<std::pair<int, double>> prev_;   // Agree: last uncommitted tail
};

}  // namespace lyra::dsp

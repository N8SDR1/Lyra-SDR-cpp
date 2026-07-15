// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — DeepFist CW decoder: CTC hypothesis rescorer (callsign correction).
//
// Faithful C++ port of diddle's dsp/rescore.rs (itself a port of DeepFist
// tools/rescore.py) — WSJT-X / JT65 "Deep Search" adapted to CTC.  Text edit
// distance can't tell WP3Z from WP3B (both 1 edit from a garble); this asks the
// MODEL instead: swap each nearby SCP callsign candidate into the decoded label
// sequence, compute the full-sequence CTC negative log-likelihood against the
// live log-prob lattice, and keep the candidate the audio actually supports.
//
// Runs at decode time (that's the only moment the lattice exists), on each 6 s
// window's greedy decode + raw ONNX log-probs.
#pragma once

#include <string>
#include <vector>

#include "dsp/deepfist/DeepFistScp.h"

namespace lyra::dsp {

// One rescoring verdict for a call-shaped span of the decode.
struct CallRescore {
    std::string orig;         // call-shaped text as greedily decoded
    std::string best;         // lattice-preferred candidate (== orig if confirmed)
    float       marginNats = 0.0f;  // gap (nats) to the best DIFFERENT candidate
    bool        confident  = false; // marginNats >= 3.0 — safe to show
};

// Full-sequence CTC negative log-likelihood of `target` (blank = class 0) under
// a row-major [t_frames, classes] log-prob lattice.  Equals PyTorch
// F.ctc_loss(..., reduction="sum") for batch 1.  Returns +inf for impossible
// targets (empty, or longer than the frames allow).  Exposed for testing.
float ctcNll(const float* logProbs, int tFrames, int classes,
             const std::vector<int>& target);

// Rescore the callsigns in one decoded window against its CTC lattice.
// `ids` = collapsed greedy ids, `tokens` = model token table (blank at 0).
// Returns a verdict per call-shaped run (and per call-shaped substring of glued
// runs).  The unmodified greedy sequence always competes, so a correct decode
// is confirmed, not overwritten.
std::vector<CallRescore> rescoreCalls(const float* logProbs, int tFrames, int classes,
                                      const std::vector<int>& ids,
                                      const std::vector<std::string>& tokens,
                                      const DeepFistScp& scp);

}  // namespace lyra::dsp

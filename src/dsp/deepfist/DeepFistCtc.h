// Lyra — DeepFist neural CW decoder: greedy CTC decode.
//
// Faithful port of DeepFist's own MIT-licensed decoder
// (deepfist/model/decode.py, greedy_ctc_decode): argmax per frame, collapse
// repeats, drop the CTC blank (index 0), map ids -> token strings.
//
// This is a from-scratch re-implementation of the standard, published CTC
// greedy-decode algorithm from DeepFist's MIT source + the model's JSON
// sidecar token table.  No AGPL (DeepCW/HamNoise) code is used.
#pragma once

#include <string>
#include <vector>

namespace lyra::dsp {

// logProbs: row-major [T][C] (T time frames, C classes), as produced by the
// DeepFist model output squeezed to a single batch.  `tokens` is the C-entry
// token table from the model sidecar (tokens[0] == "<blank>").  Returns the
// decoded text (token strings concatenated, e.g. "CQ TEST <SK>").
std::string greedyCtcDecode(const float* logProbs, int T, int C,
                            const std::vector<std::string>& tokens);

}  // namespace lyra::dsp

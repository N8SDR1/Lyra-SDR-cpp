#include "dsp/deepfist/DeepFistCtc.h"

namespace lyra::dsp {

namespace {
constexpr int kBlank = 0;   // DeepFist sidecar: ctc.blank_index == 0
}  // namespace

std::string greedyCtcDecode(const float* logProbs, int T, int C,
                            const std::vector<std::string>& tokens) {
    std::string out;
    if (!logProbs || T <= 0 || C <= 0) return out;

    int prev = -1;
    for (int t = 0; t < T; ++t) {
        const float* row = logProbs + static_cast<size_t>(t) * C;
        // argmax over classes for this frame
        int best = 0;
        float bestVal = row[0];
        for (int c = 1; c < C; ++c) {
            if (row[c] > bestVal) { bestVal = row[c]; best = c; }
        }
        // collapse repeats, then drop blanks
        if (best != prev) {
            if (best != kBlank && best < static_cast<int>(tokens.size()))
                out += tokens[static_cast<size_t>(best)];
            prev = best;
        }
    }
    return out;
}

}  // namespace lyra::dsp

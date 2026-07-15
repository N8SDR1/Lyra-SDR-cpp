// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/DeepFistRescore.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lyra::dsp {

namespace {
constexpr float kMinMarginNats = 3.0f;
constexpr int   kMaxEdit       = 2;   // SCP candidate radius
constexpr int   kMaxCands      = 60;  // candidates per span
constexpr int   kSubMin        = 4;   // min embedded-call length in a glued run
constexpr int   kSubMax        = 8;   // max embedded-call length
constexpr int   kMaxRuns       = 3;   // rescored runs per window
constexpr int   kMaxSpansPerRun = 8;
constexpr int   kMaxRunLen     = 32;  // longer runs are junk walls — skip

const float kNegInf = -std::numeric_limits<float>::infinity();
const float kPosInf =  std::numeric_limits<float>::infinity();

float logaddexp(float a, float b) {
    if (a == kNegInf) return b;
    if (b == kNegInf) return a;
    const float hi = a > b ? a : b;
    const float lo = a > b ? b : a;
    return hi + std::log1p(std::exp(lo - hi));
}

// ── callsign shape (mirrors rescore.rs is_call_shape) ──
bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool chunkOk(const std::string& b) {
    if (b.size() < 1 || b.size() > 3) return false;
    for (char c : b) if (!(isUpper(c) || isDigit(c))) return false;
    return true;
}

// [A-Z]{1,2}\d{1,3}[A-Z]{1,4}, full match
bool coreOk(const std::string& b) {
    const int n = static_cast<int>(b.size());
    int i = 0;
    while (i < n && isUpper(b[i])) ++i;
    if (i < 1 || i > 2) return false;
    const int d0 = i;
    while (i < n && isDigit(b[i])) ++i;
    if ((i - d0) < 1 || (i - d0) > 3) return false;
    const int tail = n - i;
    if (tail < 1 || tail > 4) return false;
    for (int k = i; k < n; ++k) if (!isUpper(b[k])) return false;
    return true;
}

bool isCallShape(const std::string& s) {
    if (s.size() < 3) return false;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == '/') { parts.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    parts.push_back(cur);
    if (parts.size() == 1) return coreOk(parts[0]);
    if (parts.size() == 2)
        return (chunkOk(parts[0]) && coreOk(parts[1])) ||
               (coreOk(parts[0]) && chunkOk(parts[1]));
    if (parts.size() == 3)
        return chunkOk(parts[0]) && coreOk(parts[1]) && chunkOk(parts[2]);
    return false;
}

struct Run { int start; std::string text; };

std::vector<Run> findRuns(const std::vector<int>& ids,
                          const std::vector<std::string>& tokens) {
    std::vector<Run> runs;
    bool have = false;
    Run cur{0, {}};
    for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
        const std::string& tok = tokens[static_cast<size_t>(ids[i])];
        const bool runChar = tok.size() == 1 &&
            (isUpper(tok[0]) || isDigit(tok[0]) || tok[0] == '/');
        if (runChar) {
            if (!have) { cur = Run{i, {}}; have = true; }
            cur.text.push_back(tok[0]);
        } else if (have) {
            runs.push_back(cur); have = false;
        }
    }
    if (have) runs.push_back(cur);
    return runs;
}

std::vector<std::pair<int, int>> callSpans(const std::string& text) {
    const int len = static_cast<int>(text.size());
    if (isCallShape(text)) return {{0, len}};
    if (len < kSubMin || len > kMaxRunLen) return {};
    std::vector<std::pair<int, int>> spans;
    for (int l = std::min(kSubMax, len); l >= kSubMin; --l) {
        for (int i = 0; i + l <= len; ++i) {
            if (isCallShape(text.substr(i, l))) {
                spans.emplace_back(i, l);
                if (static_cast<int>(spans.size()) >= kMaxSpansPerRun) return spans;
            }
        }
    }
    return spans;
}

int charId(char c, const std::vector<std::string>& tokens) {
    for (int i = 0; i < static_cast<int>(tokens.size()); ++i)
        if (tokens[static_cast<size_t>(i)].size() == 1 && tokens[static_cast<size_t>(i)][0] == c)
            return i;
    return -1;
}

}  // namespace

float ctcNll(const float* logProbs, int T, int C, const std::vector<int>& target) {
    const int L = static_cast<int>(target.size());
    if (L == 0 || T == 0 || L > T) return kPosInf;
    const int s = 2 * L + 1;
    auto ext = [&](int si) { return (si % 2 == 0) ? 0 : target[si / 2]; };
    auto lp  = [&](int ti, int c) { return logProbs[static_cast<size_t>(ti) * C + c]; };

    std::vector<float> prev(s, kNegInf), cur(s, kNegInf);
    prev[0] = lp(0, 0);
    prev[1] = lp(0, ext(1));
    for (int ti = 1; ti < T; ++ti) {
        for (int si = 0; si < s; ++si) {
            float acc = prev[si];
            if (si >= 1) acc = logaddexp(acc, prev[si - 1]);
            if (si >= 2 && ext(si) != 0 && ext(si) != ext(si - 2))
                acc = logaddexp(acc, prev[si - 2]);
            cur[si] = (acc == kNegInf) ? acc : acc + lp(ti, ext(si));
        }
        std::swap(prev, cur);
    }
    return -logaddexp(prev[s - 1], prev[s - 2]);
}

std::vector<CallRescore> rescoreCalls(const float* logProbs, int T, int C,
                                      const std::vector<int>& ids,
                                      const std::vector<std::string>& tokens,
                                      const DeepFistScp& scp) {
    std::vector<CallRescore> out;
    if (!logProbs || T == 0 || C == 0 || ids.empty() || !scp.ready()) return out;

    for (const Run& run : findRuns(ids, tokens)) {
        if (static_cast<int>(out.size()) >= kMaxRuns) break;
        const auto spans = callSpans(run.text);
        if (spans.empty()) continue;
        const bool wholeRun = spans.size() == 1 &&
                              spans[0].first == 0 &&
                              spans[0].second == static_cast<int>(run.text.size());
        const float baseNll = ctcNll(logProbs, T, C, ids);

        struct Group { float nll; std::string call; std::string spanText; };
        std::vector<Group> groups;

        for (const auto& [off, len] : spans) {
            const std::string sub = run.text.substr(off, len);
            for (const std::string& cand : scp.candidates(sub, kMaxEdit, kMaxCands)) {
                if (cand == sub) continue;
                std::vector<int> candIds;
                candIds.reserve(cand.size());
                bool ok = true;
                for (char c : cand) {
                    const int id = charId(c, tokens);
                    if (id < 0) { ok = false; break; }
                    candIds.push_back(id);
                }
                if (!ok) continue;

                std::vector<int> trial;
                trial.reserve(ids.size() + candIds.size());
                trial.insert(trial.end(), ids.begin(), ids.begin() + (run.start + off));
                trial.insert(trial.end(), candIds.begin(), candIds.end());
                trial.insert(trial.end(), ids.begin() + (run.start + off + len), ids.end());

                const float nll = ctcNll(logProbs, T, C, trial);
                if (!std::isfinite(nll)) continue;

                auto it = std::find_if(groups.begin(), groups.end(),
                                       [&](const Group& g) { return g.call == cand; });
                if (it == groups.end()) groups.push_back({nll, cand, sub});
                else if (nll < it->nll) { it->nll = nll; it->spanText = sub; }
            }
        }
        if (groups.empty()) continue;
        std::sort(groups.begin(), groups.end(),
                  [](const Group& a, const Group& b) { return a.nll < b.nll; });
        const Group& top = groups[0];

        if (baseNll <= top.nll) {
            if (wholeRun) {
                const float margin = top.nll - baseNll;
                out.push_back({run.text, run.text, margin, margin >= kMinMarginNats});
            }
        } else {
            const float runner = groups.size() > 1 ? groups[1].nll : kPosInf;
            const float margin = std::min(runner, baseNll) - top.nll;
            out.push_back({wholeRun ? run.text : top.spanText, top.call,
                           margin, margin >= kMinMarginNats});
        }
    }
    return out;
}

}  // namespace lyra::dsp

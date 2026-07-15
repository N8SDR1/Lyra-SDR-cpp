// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/DeepFistScp.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace lyra::dsp {

namespace {

std::string trimUpper(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    std::string out = s.substr(a, b - a);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// diddle ScpDb::looks_like_call — 3..12 chars, only A-Z/0-9/'/', >=1 letter+digit.
bool looksLikeCall(const std::string& s) {
    const size_t len = s.size();
    if (len < 3 || len > 12) return false;
    bool hasLetter = false, hasDigit = false;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z')      hasLetter = true;
        else if (c >= '0' && c <= '9') hasDigit  = true;
        else if (c == '/')             { /* allowed */ }
        else return false;
    }
    return hasLetter && hasDigit;
}

// Levenshtein with early exit at `cap` (byte-wise; callsigns are ASCII).
int editDistanceCapped(const std::string& a, const std::string& b, int cap) {
    const int la = static_cast<int>(a.size()), lb = static_cast<int>(b.size());
    if (std::abs(la - lb) > cap) return cap + 1;
    std::vector<int> prev(lb + 1), cur(lb + 1);
    for (int j = 0; j <= lb; ++j) prev[j] = j;
    for (int i = 0; i < la; ++i) {
        cur[0] = i + 1;
        int rowMin = cur[0];
        for (int j = 0; j < lb; ++j) {
            const int sub = prev[j] + (a[i] != b[j] ? 1 : 0);
            cur[j + 1] = std::min({sub, prev[j + 1] + 1, cur[j] + 1});
            rowMin = std::min(rowMin, cur[j + 1]);
        }
        if (rowMin > cap) return cap + 1;
        std::swap(prev, cur);
    }
    return prev[lb];
}

}  // namespace

int DeepFistScp::loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;

    std::vector<std::string> calls;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // skip blank / header lines (! or #), matching diddle load_str
        size_t a = 0;
        while (a < line.size() && std::isspace(static_cast<unsigned char>(line[a]))) ++a;
        if (a >= line.size() || line[a] == '!' || line[a] == '#') continue;
        std::string c = trimUpper(line);
        if (looksLikeCall(c)) calls.push_back(std::move(c));
    }
    std::sort(calls.begin(), calls.end());
    calls.erase(std::unique(calls.begin(), calls.end()), calls.end());
    calls_ = std::move(calls);
    return count();
}

std::vector<std::string> DeepFistScp::candidates(const std::string& token,
                                                 int maxEdit, int maxCands) const {
    const std::string t = trimUpper(token);
    if (t.size() < 3 || calls_.empty()) return {};

    std::vector<std::pair<int, const std::string*>> scored;
    for (const std::string& c : calls_) {
        const int d = editDistanceCapped(t, c, maxEdit);
        if (d <= maxEdit) scored.emplace_back(d, &c);
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto& x, const auto& y) {
                  if (x.first != y.first) return x.first < y.first;
                  return *x.second < *y.second;   // stable, deterministic order
              });
    if (static_cast<int>(scored.size()) > maxCands)
        scored.resize(static_cast<size_t>(maxCands));
    std::vector<std::string> out;
    out.reserve(scored.size());
    for (const auto& s : scored) out.push_back(*s.second);
    return out;
}

void DeepFistScp::addCalls(const std::vector<std::string>& extra) {
    if (extra.empty()) return;
    calls_.insert(calls_.end(), extra.begin(), extra.end());
    std::sort(calls_.begin(), calls_.end());
    calls_.erase(std::unique(calls_.begin(), calls_.end()), calls_.end());
}

}  // namespace lyra::dsp

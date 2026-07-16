// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/CwCaptureHarvester.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "dsp/deepfist/CwHarvestIo.h"

namespace fs = std::filesystem;

namespace lyra::dsp {

namespace {
constexpr size_t kTraceMax = 60;     // ~24 s of keying history at 2.5 Hz
constexpr size_t kTextMax  = 200;    // rolling per-engine context chars

void rollAppend(std::string& acc, const std::string& s) {
    acc += s;
    if (acc.size() > kTextMax) acc.erase(0, acc.size() - kTextMax);
}

// Age key for retention ordering.  Segment stems are "<tier>_<epoch>_<seq>"
// (see writeSegment), and the tier prefix itself contains underscores
// ("hard_negative", "gold_rbn"), so the epoch/seq fields must be parsed from
// the END of the stem, not assumed to start after the first '_'.
std::pair<long long, long long> segmentAgeKey(const fs::path& path) {
    const std::string stem = path.stem().string();   // strip .wav/.json
    const size_t lastUs = stem.find_last_of('_');
    const size_t prevUs = (lastUs == std::string::npos)
                              ? std::string::npos
                              : stem.find_last_of('_', lastUs - 1);
    if (lastUs == std::string::npos || prevUs == std::string::npos)
        return {0, 0};   // unrecognized name — treat as oldest
    const long long epoch = std::strtoll(stem.substr(prevUs + 1, lastUs - prevUs - 1).c_str(),
                                         nullptr, 10);
    const long long seq   = std::strtoll(stem.substr(lastUs + 1).c_str(), nullptr, 10);
    return {epoch, seq};
}
}  // namespace

CwCaptureHarvester::CwCaptureHarvester(CwHarvestRing& ring, std::string outDir,
                                       Config cfg)
    : ring_(ring), dir_(std::move(outDir)), cfg_(cfg) {}

void CwCaptureHarvester::feedKeying(float ratio, long long nowSec) {
    std::lock_guard<std::mutex> lk(mx_);
    keyTrace_.push_back({nowSec, ratio});
    while (keyTrace_.size() > kTraceMax) keyTrace_.pop_front();
}

void CwCaptureHarvester::feedText(bool fromClassic, const std::string& text) {
    std::lock_guard<std::mutex> lk(mx_);
    rollAppend(fromClassic ? textClassic_ : textDeepFist_, text);
}

void CwCaptureHarvester::feedWpm(int wpm) {
    std::lock_guard<std::mutex> lk(mx_);
    lastWpm_ = wpm;
}

void CwCaptureHarvester::triggerFade(long long nowSec) {
    std::lock_guard<std::mutex> lk(mx_);
    if (nowSec - lastFadeAt_ < cfg_.debounceSec) return;   // per-tier debounce
    lastFadeAt_ = nowSec;
    pending_.push_back({"hard_negative", "", nowSec, cfg_.postSec});
}

void CwCaptureHarvester::triggerGoldRbn(const std::string& call, long long nowSec) {
    std::lock_guard<std::mutex> lk(mx_);
    if (nowSec - lastGoldAt_ < cfg_.debounceSec) return;   // per-tier debounce
    // Per-call dedupe: the Learn re-scan re-confirms every spotted call still
    // in the transcript on EVERY spot-bank change, so the same call would
    // otherwise mint a near-identical gold segment each debounce window
    // (observed on air: 5x W1AW/3 in 3 min).  One gold per call per
    // goldRepeatSec is plenty — a genuine re-work later still captures.
    auto it = goldCallAt_.find(call);
    if (it != goldCallAt_.end() && nowSec - it->second < cfg_.goldRepeatSec)
        return;
    goldCallAt_[call] = nowSec;
    lastGoldAt_ = nowSec;
    pending_.push_back({"gold_rbn", call, nowSec, 0});
}

void CwCaptureHarvester::pump(long long nowSec) {
    std::vector<Pending> ready;
    {
        std::lock_guard<std::mutex> lk(mx_);
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (nowSec >= it->triggeredAt + it->postSec) {
                ready.push_back(*it);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const Pending& p : ready) writeSegment(p, nowSec);
    if (!ready.empty()) enforceCap();
}

void CwCaptureHarvester::writeSegment(const Pending& p, long long nowSec) {
    // Segment audio: preSec before the trigger + whatever post-roll elapsed.
    const int postGot = static_cast<int>(std::min<long long>(
        nowSec - p.triggeredAt, p.postSec));
    const int nSamp = (cfg_.preSec + postGot) * cfg_.sampleRate;
    const std::vector<float> audio = ring_.snapshot(nSamp);

    std::string base;
    std::deque<std::pair<long long, float>> trace;
    std::string tClassic, tDeep;
    int wpm = 0;
    {
        std::lock_guard<std::mutex> lk(mx_);
        char b[64];
        std::snprintf(b, sizeof b, "%s_%lld_%03d",
                      p.tier.c_str(), p.triggeredAt, seq_++);
        base = b;
        trace = keyTrace_;
        tClassic = textClassic_;
        tDeep = textDeepFist_;
        wpm = lastWpm_;
    }

    const std::string wavPath  = dir_ + "/" + base + ".wav";
    const std::string jsonPath = dir_ + "/" + base + ".json";
    float peak = 0.0f;
    if (!writeWav16(wavPath, audio, cfg_.sampleRate, &peak)) return;

    std::string j = "{";
    j += "\"schema\":1";
    j += ",\"tier\":\"" + jsonEscape(p.tier) + "\"";
    j += ",\"utc\":" + std::to_string(p.triggeredAt);
    j += ",\"trigger_call\":\"" + jsonEscape(p.call) + "\"";
    j += ",\"sample_rate\":" + std::to_string(cfg_.sampleRate);
    j += ",\"pre_sec\":" + std::to_string(cfg_.preSec);
    j += ",\"post_sec\":" + std::to_string(postGot);
    j += ",\"peak\":" + std::to_string(peak);
    j += ",\"wpm\":" + std::to_string(wpm);
    j += ",\"keying_trace\":[";
    bool first = true;
    for (const auto& [t, r] : trace) {
        if (!first) j += ",";
        first = false;
        char b[48];
        std::snprintf(b, sizeof b, "[%lld,%.2f]", t, r);
        j += b;
    }
    j += "]";
    j += ",\"text_classic\":\"" + jsonEscape(tClassic) + "\"";
    j += ",\"text_deepfist\":\"" + jsonEscape(tDeep) + "\"";
    j += "}\n";
    if (!writeTextFile(jsonPath, j)) {
        std::remove(wavPath.c_str());        // no orphan wav without sidecar
        return;
    }

    std::lock_guard<std::mutex> lk(mx_);
    ++written_;
}

void CwCaptureHarvester::enforceCap() {
    // Oldest-first delete until the directory fits the cap.  Eviction order
    // must be tier-independent: plain lexicographic filename order sorts by
    // the tier prefix FIRST (e.g. "gold_rbn_..." < "hard_negative_..."),
    // which would evict the high-trust gold_rbn tier ahead of older
    // hard_negative segments.  Sort by the (epoch, seq) age key parsed from
    // the trailing filename fields instead.
    std::error_code ec;
    struct Entry { std::string path; long long size; long long epoch; long long seq; };
    std::vector<Entry> files;
    long long total = 0;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        const long long sz = static_cast<long long>(fs::file_size(e, ec));
        const auto [epoch, seq] = segmentAgeKey(e.path());
        files.push_back({e.path().string(), sz, epoch, seq});
        total += sz;
    }
    std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) {
        if (a.epoch != b.epoch) return a.epoch < b.epoch;
        return a.seq < b.seq;
    });
    for (const auto& f : files) {
        if (total <= cfg_.capBytes) break;
        if (fs::remove(f.path, ec)) total -= f.size;
    }
}

int CwCaptureHarvester::segmentsWritten() const {
    std::lock_guard<std::mutex> lk(mx_);
    return written_;
}

}  // namespace lyra::dsp

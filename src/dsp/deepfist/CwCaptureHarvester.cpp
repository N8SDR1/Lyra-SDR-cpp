// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/CwCaptureHarvester.h"

#include <algorithm>
#include <cstdio>
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
    // Oldest-first delete until the directory fits the cap.  Names embed the
    // trigger epoch, so lexicographic-by-epoch ordering == age ordering.
    std::error_code ec;
    std::vector<std::pair<std::string, long long>> files;   // path, size
    long long total = 0;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        const long long sz = static_cast<long long>(fs::file_size(e, ec));
        files.push_back({e.path().string(), sz});
        total += sz;
    }
    std::sort(files.begin(), files.end());
    for (const auto& [path, sz] : files) {
        if (total <= cfg_.capBytes) break;
        if (fs::remove(path, ec)) total -= sz;
    }
}

int CwCaptureHarvester::segmentsWritten() const {
    std::lock_guard<std::mutex> lk(mx_);
    return written_;
}

}  // namespace lyra::dsp

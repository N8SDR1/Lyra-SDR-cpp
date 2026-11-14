// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/NeuralCwDecoder.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <string>

#include "dsp/deepfist/DeepFistCtc.h"

namespace lyra::dsp {

namespace {
constexpr int    kSr        = 3200;
constexpr int    kWindow    = 6 * kSr;      // 19200 samples (6 s), zero-padded
constexpr double kWindowS   = 6.0;
constexpr int    kTickMs    = 400;          // re-decode ~2.5x/s (tci_decode tick)
constexpr double kGuardS    = 1.3;          // Guard commit delay / Agree hard cap
constexpr int    kMinSamp   = kSr;          // need >=1 s buffered before decoding
constexpr float  kToneSpanHz = 80.0f;       // tone lock: pitch +/- this

// Defaults below were chosen on the offline bench
// (scratch/test_cw_neural_bench.cpp, synthetic CW): mean character error
// 40% -> 3.5%, average display delay 1.4 s -> 1.0 s.  Each can be put back
// to the original behaviour with an env var for A/B on real signals:
//   LYRA_CW_TONELOCK=loudest  strongest tone anywhere in 400-1200 Hz
//   LYRA_CW_GATE=12           original keying gate (p90/p10)
//   LYRA_CW_MASK=0            no clean-up mask after the conditioner
//   LYRA_CW_COMMIT=guard      fixed 1.3 s commit delay
//
// Keying gate: p90/p10 of the locked tone's envelope.  12 (the DeepFist
// squelch.py value) also rejected keyed CW at 6 dB SNR (amplitude ratio ~9),
// which is why weak and fading signals stopped mid-QSO.  6 still printed no
// false characters on 20 s of pure noise; 4 started to.
float envFloat(const char* name, float dflt) {
    const char* e = std::getenv(name);
    return e ? std::strtof(e, nullptr) : dflt;
}
bool envIs(const char* name, const char* value) {
    const char* e = std::getenv(name);
    if (!e) return false;
    std::string s(e);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s == value;
}
}  // namespace

NeuralCwDecoder::NeuralCwDecoder() {
    ring_.assign(kWindow, 0.0f);
    // Initial blank penalty from LYRA_CW_BLANKPEN (default 0); the panel can
    // override it live via setBlankPenalty().
    if (const char* e = std::getenv("LYRA_CW_BLANKPEN"))
        blankPen_.store(std::strtof(e, nullptr), std::memory_order_relaxed);

    pinTone_   = !envIs("LYRA_CW_TONELOCK", "loudest");
    keyingMin_ = envFloat("LYRA_CW_GATE", 6.0f);
    maskHz_    = envFloat("LYRA_CW_MASK", 150.0f);
    DeepFistCommit::Config cc;
    cc.agree  = !envIs("LYRA_CW_COMMIT", "guard");
    cc.guardS = kGuardS;
    commit_.setConfig(cc);
    model_.setOutputMaskHz(maskHz_);
}

NeuralCwDecoder::~NeuralCwDecoder() {
    stopWorker();
}

bool NeuralCwDecoder::loadModel(const std::string& modelDir,
                                const std::vector<std::string>& extraScpCalls) {
    const bool ok = model_.load(modelDir);
    if (ok) {
        // Optional callsign DB for the lattice rescorer; a no-op if absent.
        scp_.loadFile(modelDir + "/MASTER.SCP");
        scp_.addCalls(extraScpCalls);      // Phase 3: locally-confirmed calls
        startWorker();
    }
    return ok;
}

void NeuralCwDecoder::setSampleRate(double hz) {
    decim_.setInRate(hz);
    reset();
}

void NeuralCwDecoder::reset() {
    std::lock_guard<std::mutex> lk(mx_);
    decim_.reset();
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    total3200_  = 0;
    ++gen_;                     // discard any in-flight worker result; the
                                // worker resets commit_ when it sees the bump
}

void NeuralCwDecoder::process(const float* mono, int nframes) {
    if (!model_.ready() || !mono || nframes <= 0) return;

    tmp_.clear();
    decim_.process(mono, nframes, tmp_);        // -> 3200 Hz
    if (tmp_.empty()) return;

    std::lock_guard<std::mutex> lk(mx_);
    const size_t p = tmp_.size();
    if (p >= ring_.size()) {
        std::copy(tmp_.end() - ring_.size(), tmp_.end(), ring_.begin());
    } else {
        std::move(ring_.begin() + p, ring_.end(), ring_.begin());
        std::copy(tmp_.begin(), tmp_.end(), ring_.end() - p);
    }
    total3200_ += static_cast<long long>(p);
    // The worker polls on its own tick; no need to wake it per audio block.
}

void NeuralCwDecoder::startWorker() {
    if (running_.exchange(true)) return;        // already running
    worker_ = std::thread([this] { workerLoop(); });
}

void NeuralCwDecoder::stopWorker() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void NeuralCwDecoder::workerLoop() {
    std::vector<float> window(kWindow);
    std::vector<float> logits;
    long long lastTotal = -1;           // skip decoding when no new audio arrived
    unsigned  seenGen   = 0;            // last reset generation the commit saw

    while (running_.load()) {
        double audioEnd = 0.0;
        unsigned gen = 0;
        {
            std::unique_lock<std::mutex> lk(mx_);
            cv_.wait_for(lk, std::chrono::milliseconds(kTickMs),
                         [this] { return !running_.load(); });
            if (!running_.load()) break;
            // Idle unless we have >=1 s buffered AND new audio since last decode
            // (when Neural isn't the active engine, no audio is fed -> no work).
            if (total3200_ < kMinSamp || total3200_ == lastTotal) continue;
            lastTotal = total3200_;
            std::copy(ring_.begin(), ring_.end(), window.begin());
            audioEnd = static_cast<double>(total3200_) / kSr;
            gen      = gen_;
        }
        if (gen != seenGen) { commit_.reset(); seenGen = gen; }

        // Lock the station once per window; the gate, speed estimate and the
        // model all use the same tone.
        DeepFistConditioner::ToneSearch search;
        const float pitch = toneHz_.load(std::memory_order_relaxed);
        if (pinTone_ && pitch > 0.0f) {
            search.centerHz    = pitch;
            search.halfSpanHz  = kToneSpanHz;
            search.wholeWindow = true;
        }
        const float tone = model_.lockTone(window.data(), kWindow, search);

        // Keying gate: only decode when the window actually contains keyed CW.
        // A steady carrier / birdie / the CW-AGC artifact tone clears any level
        // test but has a flat envelope, so the model would hallucinate on it.
        // No keying -> emit nothing, just advance the commit boundary so silence
        // doesn't back up.  (Mirrors diddle's presence gate; DeepFist HANDOFF
        // §18.23 "no signal energy -> no characters".)
        const float keyRatio = model_.keyingRatio(window.data(), kWindow, tone);
        if (onKeying) onKeying(keyRatio);
        if (keyRatio < keyingMin_) {
            {
                std::lock_guard<std::mutex> lk(mx_);
                if (gen != gen_) continue;
            }
            commit_.idle(audioEnd);
            // Emit ONE space at the start of a pause so text across the gap
            // doesn't run together (mirrors tci_decode's idle-gap space).
            if (!idleGap_) { idleGap_ = true; if (onText) onText(" "); }
            continue;
        }
        idleGap_ = false;   // keyed signal present again

        // Estimated RX speed (WPM) from the keying envelope; emit on change.
        if (onWpm) {
            const int wpm = model_.keyingWpm(window.data(), kWindow, tone);
            const int d = wpm > lastWpm_ ? wpm - lastWpm_ : lastWpm_ - wpm;
            if (wpm > 0 && d >= 2) { lastWpm_ = wpm; onWpm(wpm); }
        }

        int T = 0, C = 0;
        if (!model_.inferAt(window.data(), kWindow, tone, logits, T, C)) continue;
        const CtcFrames fr = greedyCtcFrames(logits.data(), T, C,
                                             blankPen_.load(std::memory_order_relaxed));

        // CTC-lattice callsign rescoring for this window (worker thread, no lock).
        std::vector<CallRescore> calls;
        if (scp_.ready() && onCalls)
            calls = rescoreCalls(logits.data(), T, C, fr.ids, model_.tokens(), scp_);
        const auto& toks = model_.tokens();

        {
            std::lock_guard<std::mutex> lk(mx_);
            if (gen != gen_) continue;          // reset() happened mid-decode
        }
        std::string emit;
        for (const DeepFistCommit::Emitted& e : commit_.step(fr, audioEnd, kWindowS))
            if (e.id > 0 && e.id < static_cast<int>(toks.size()))
                emit += toks[static_cast<size_t>(e.id)];
        if (!emit.empty() && onText) onText(emit);

        if (onCalls && !calls.empty()) {
            std::vector<CallRescore> confident;
            for (const CallRescore& c : calls)
                if (c.confident) confident.push_back(c);
            if (!confident.empty()) onCalls(confident);
        }
    }
}

}  // namespace lyra::dsp

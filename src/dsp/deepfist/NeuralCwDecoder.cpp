#include "dsp/deepfist/NeuralCwDecoder.h"

#include <algorithm>
#include <chrono>

#include "dsp/deepfist/DeepFistCtc.h"

namespace lyra::dsp {

namespace {
constexpr int    kSr        = 3200;
constexpr int    kWindow    = 6 * kSr;      // 19200 samples (6 s), zero-padded
constexpr double kWindowS   = 6.0;
constexpr int    kTickMs    = 400;          // re-decode ~2.5x/s (tci_decode tick)
constexpr double kGuardS    = 1.3;          // commit delay / latency
constexpr int    kMinSamp   = kSr;          // need >=1 s buffered before decoding
constexpr float  kBlankPen  = 0.0f;         // exp9 is robust; off (matches Rust)
}  // namespace

NeuralCwDecoder::NeuralCwDecoder() {
    ring_.assign(kWindow, 0.0f);
}

NeuralCwDecoder::~NeuralCwDecoder() {
    stopWorker();
}

bool NeuralCwDecoder::loadModel(const std::string& modelDir) {
    const bool ok = model_.load(modelDir);
    if (ok) {
        // Optional callsign DB for the lattice rescorer; a no-op if absent.
        scp_.loadFile(modelDir + "/MASTER.SCP");
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
    committedT_ = 0.0;
    ++gen_;                     // discard any in-flight worker result
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

        int T = 0, C = 0;
        if (!model_.infer(window.data(), kWindow, logits, T, C)) continue;
        const CtcFrames fr = greedyCtcFrames(logits.data(), T, C, kBlankPen);

        // CTC-lattice callsign rescoring for this window (worker thread, no lock).
        std::vector<CallRescore> calls;
        if (scp_.ready() && onCalls)
            calls = rescoreCalls(logits.data(), T, C, fr.ids, model_.tokens(), scp_);

        const double winStart = audioEnd - kWindowS;
        const double settleTo = audioEnd - kGuardS;
        const int    denom    = std::max(1, fr.T);
        const auto&  toks     = model_.tokens();

        std::string emit;
        {
            std::lock_guard<std::mutex> lk(mx_);
            if (gen != gen_) continue;          // reset() happened mid-decode
            for (size_t i = 0; i < fr.ids.size(); ++i) {
                const double ct =
                    winStart + (static_cast<double>(fr.frames[i]) / denom) * kWindowS;
                if (ct > committedT_ && ct <= settleTo) {
                    const int id = fr.ids[i];
                    if (id > 0 && id < static_cast<int>(toks.size()))
                        emit += toks[static_cast<size_t>(id)];
                }
            }
            committedT_ = std::max(committedT_, settleTo);
        }
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

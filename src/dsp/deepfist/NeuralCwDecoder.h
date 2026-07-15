// Lyra — DeepFist neural CW decoder: streaming adapter (second CW engine).
//
// Same shape as the classic fldigi-port decoder (src/dsp/CwDecoder.h) so it
// drops into WdspEngine beside it: setSampleRate(), process(mono,nframes),
// reset(), and a decoded-text callback.
//
// Streaming design mirrors DeepFist's reference live decoder
// (deepfist/scripts/tci_decode.py): a rolling 6 s window at 3200 Hz is
// re-decoded several times per second on a WORKER THREAD (never the audio
// callback), and characters are committed with FRAME-TIMED COMMIT — each
// decoded char carries a frame time, and is emitted exactly once when its audio
// has settled (older than `guard` seconds).  So text STREAMS character-by-
// character into an accumulating transcript (~1.3 s latency), instead of
// dumping/replacing a whole window.  onText therefore delivers INCREMENTAL
// committed text to append (not a full-window snapshot).
//
// Threading: the audio thread calls process()/reset() (which only decimate +
// buffer — cheap); the worker thread runs inference and fires onText.  The
// owner marshals onText to the GUI thread via a queued signal.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dsp/deepfist/DeepFistModel.h"
#include "dsp/deepfist/DeepFistResampler.h"
#include "dsp/deepfist/DeepFistRescore.h"
#include "dsp/deepfist/DeepFistScp.h"

namespace lyra::dsp {

class NeuralCwDecoder {
public:
    NeuralCwDecoder();
    ~NeuralCwDecoder();

    NeuralCwDecoder(const NeuralCwDecoder&)            = delete;
    NeuralCwDecoder& operator=(const NeuralCwDecoder&) = delete;

    // Load the model dir (deepfist.onnx + sidecar); starts the worker on
    // success.  Returns ready().
    bool loadModel(const std::string& modelDir);
    bool ready() const { return model_.ready(); }
    int  scpCount() const { return scp_.count(); }   // 0 = rescorer disabled

    // CTC blank-logit penalty, live-adjustable (audio-thread-safe atomic).
    // Higher = recover more dropped chars on weak audio, more spurious chars on
    // strong signals.  Initial value from LYRA_CW_BLANKPEN (default 0).
    float blankPenalty() const { return blankPen_.load(std::memory_order_relaxed); }
    void  setBlankPenalty(float p) { blankPen_.store(p, std::memory_order_relaxed); }
    const std::string& lastError() const { return model_.lastError(); }

    void setSampleRate(double hz);      // input rate (default 48 kHz)

    // Fires from the worker thread with the newly-committed characters to append
    // to the transcript (incremental, may be empty).
    std::function<void(const std::string& newText)> onText;

    // Fires from the worker thread with CTC-lattice callsign verdicts for the
    // current window (only confident ones), when a MASTER.SCP db is loaded.
    std::function<void(const std::vector<CallRescore>& calls)> onCalls;

    // hot path (audio thread) — decimate + buffer only; no inference here.
    void process(const float* mono, int nframes);
    void reset();

private:
    void startWorker();
    void stopWorker();
    void workerLoop();

    DeepFistModel     model_;
    DeepFistScp       scp_;             // callsign DB for the lattice rescorer
    DeepFistResampler decim_{48000.0, 3200.0};
    std::atomic<float> blankPen_{0.0f}; // CTC blank penalty (live, UI↔worker)

    // Shared rolling window (audio thread writes, worker reads), under mx_.
    std::mutex          mx_;
    std::vector<float>  ring_;          // fixed 6 s @ 3200 Hz, zero-padded
    long long           total3200_ = 0; // cumulative 3200 Hz samples seen
    double              committedT_ = 0.0; // audio time already emitted (s)
    unsigned            gen_ = 0;       // bumped on reset() to discard in-flight
    bool                idleGap_ = false; // worker-only: in a keyed-CW pause
    std::vector<float>  tmp_;           // decimator scratch (audio thread)

    // Worker.
    std::thread             worker_;
    std::atomic<bool>       running_{false};
    std::condition_variable cv_;
};

}  // namespace lyra::dsp

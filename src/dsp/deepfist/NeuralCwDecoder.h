// Lyra — DeepFist neural CW decoder: streaming adapter (second CW engine).
//
// Same shape as the classic fldigi-port decoder (src/dsp/CwDecoder.h) so it
// drops into WdspEngine beside it: setSampleRate(), process(mono,nframes),
// reset(), and a decoded-text callback.  Internally it decimates 48k->3200 Hz,
// keeps a rolling 6 s window, and re-decodes every ~1.5 s through the DeepFist
// ONNX model (C++ mirror of the verified Rust CwNeural in diddle cw_neural.rs).
//
// Unlike the classic engine (which emits characters incrementally), the neural
// engine emits the FULL decoded text of the current 6 s window each time it
// runs — a rolling snapshot.  The owner displays it in "replace" mode.
//
// Threading matches CwDecoder: the audio thread calls process()/reset(); onText
// fires from process() and the owner marshals it to the GUI thread.
#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "dsp/deepfist/DeepFistModel.h"
#include "dsp/deepfist/DeepFistResampler.h"

namespace lyra::dsp {

class NeuralCwDecoder {
public:
    NeuralCwDecoder();

    // Load the model directory (holds deepfist.onnx + deepfist.onnx.json).
    // Returns ready(); on failure the owner should stay on the classic engine.
    bool loadModel(const std::string& modelDir);
    bool ready() const { return model_.ready(); }
    const std::string& lastError() const { return model_.lastError(); }

    void setSampleRate(double hz);      // input rate (default 48 kHz)

    // Fires from process() whenever a window is decoded; text is the FULL
    // current-window decode (may be empty during silence).
    std::function<void(const std::string& windowText)> onText;

    // hot path (audio thread)
    void process(const float* mono, int nframes);
    void reset();

private:
    void tryDecode();

    DeepFistModel     model_;
    DeepFistResampler decim_{48000.0, 3200.0};

    std::deque<float>  buf_;            // rolling window at 3200 Hz
    std::vector<float> tmp_;            // decimator output scratch
    std::vector<float> window_;         // contiguous copy for the model
    size_t             newSinceDecode_ = 0;
};

}  // namespace lyra::dsp

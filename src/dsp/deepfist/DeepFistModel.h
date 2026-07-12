// Lyra — DeepFist neural CW decoder: ONNX model engine.
//
// C++ mirror of the verified Rust reference `CwEngine` (diddle
// src-tauri/src/dsp/cw_neural.rs): owns the ONNX Runtime session + token table
// + spectrogram front-end, and decodes a window of 3200 Hz audio to text.
//
// This is the ONLY DeepFist file that depends on ONNX Runtime; the rest of the
// pipeline (resampler, spectrogram, CTC) is plain C++.  ONNX Runtime is MIT
// (Microsoft); the model is DeepFist (MIT, © Brent Crier).  No AGPL code.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "dsp/deepfist/DeepFistSpectrogram.h"

namespace lyra::dsp {

class DeepFistModel {
public:
    DeepFistModel();
    ~DeepFistModel();

    DeepFistModel(const DeepFistModel&)            = delete;
    DeepFistModel& operator=(const DeepFistModel&) = delete;

    // Load deepfist.onnx (+ deepfist.onnx.json sidecar) from `modelDir`.
    // Returns false on any failure (missing file, bad model); the caller can
    // then report "model not found" and stay on the classic engine.
    bool load(const std::string& modelDir);

    bool ready() const { return ready_; }

    // Decode one window of mono audio already at 3200 Hz.  Returns the decoded
    // text (may be empty).  Safe to call only when ready().
    std::string decode3200(const float* audio, int n);

    const std::string& lastError() const { return lastError_; }

private:
    struct Impl;                        // hides the ONNX Runtime types
    std::unique_ptr<Impl> impl_;

    DeepFistSpectrogram      fe_;
    std::vector<std::string> tokens_;   // 48-entry table from the sidecar
    std::vector<float>       specScratch_;
    bool                     ready_ = false;
    std::string              lastError_;
};

}  // namespace lyra::dsp

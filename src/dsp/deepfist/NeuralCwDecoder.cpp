#include "dsp/deepfist/NeuralCwDecoder.h"

namespace lyra::dsp {

namespace {
constexpr int kSr          = 3200;
constexpr int kWindow      = 6 * kSr;        // 19200 samples (6 s)
constexpr int kDecodeEvery = kSr * 3 / 2;    // ~1.5 s of new audio
constexpr int kMinDecode   = kSr;            // need >=1 s before first decode
}  // namespace

NeuralCwDecoder::NeuralCwDecoder() = default;

bool NeuralCwDecoder::loadModel(const std::string& modelDir) {
    return model_.load(modelDir);
}

void NeuralCwDecoder::setSampleRate(double hz) {
    decim_.setInRate(hz);
    reset();
}

void NeuralCwDecoder::reset() {
    decim_.reset();
    buf_.clear();
    newSinceDecode_ = 0;
}

void NeuralCwDecoder::process(const float* mono, int nframes) {
    if (!model_.ready() || !mono || nframes <= 0) return;

    tmp_.clear();
    decim_.process(mono, nframes, tmp_);      // -> 3200 Hz samples

    for (float s : tmp_) {
        if (static_cast<int>(buf_.size()) == kWindow) buf_.pop_front();
        buf_.push_back(s);
    }
    newSinceDecode_ += tmp_.size();

    if (static_cast<int>(newSinceDecode_) >= kDecodeEvery &&
        static_cast<int>(buf_.size()) >= kMinDecode) {
        newSinceDecode_ = 0;
        tryDecode();
    }
}

void NeuralCwDecoder::tryDecode() {
    window_.assign(buf_.begin(), buf_.end());
    const std::string text = model_.decode3200(window_.data(),
                                               static_cast<int>(window_.size()));
    if (onText) onText(text);
}

}  // namespace lyra::dsp

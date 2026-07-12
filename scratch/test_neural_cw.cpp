// DeepFist neural CW decoder — headless WAV decode verification.
//
// Proves Lyra's C++ port (decimator + spectrogram + ONNX + greedy CTC) matches
// the Python/Rust reference on known clips, BEFORE any UI wiring.
//
//   test_neural_cw <modelDir> <clip.wav> [expected] [<clip.wav> [expected] ...]
//
// For a 3200 Hz WAV the audio is fed straight to the model (matches the Python
// reference decode_3200 with no decimation); for any other rate the C++
// decimator runs first (matches Rust decode_clip).  Exit code = number of
// mismatches against the provided `expected` strings (0 = all pass).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "dsp/deepfist/DeepFistModel.h"
#include "dsp/deepfist/DeepFistResampler.h"

namespace {

// Minimal RIFF/WAVE reader: 16-bit PCM, mono or multi-channel (takes ch 0).
bool readWav(const std::string& path, std::vector<float>& mono, int& rate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) ||
        std::memcmp(b.data() + 8, "WAVE", 4)) {
        std::fprintf(stderr, "%s: not a WAVE file\n", path.c_str()); return false;
    }
    int    channels = 1, bits = 16;
    size_t rateOff = 0;
    size_t i = 12;
    const uint8_t* d = b.data();
    auto u16 = [&](size_t o){ return (uint16_t)(d[o] | (d[o+1] << 8)); };
    auto u32 = [&](size_t o){ return (uint32_t)(d[o] | (d[o+1]<<8) | (d[o+2]<<16) | (d[o+3]<<24)); };
    size_t dataOff = 0, dataLen = 0;
    while (i + 8 <= b.size()) {
        const size_t sz = u32(i + 4);
        if (!std::memcmp(d + i, "fmt ", 4)) {
            channels = u16(i + 8 + 2);
            rate     = (int)u32(i + 8 + 4);
            bits     = u16(i + 8 + 14);
            (void)rateOff;
        } else if (!std::memcmp(d + i, "data", 4)) {
            dataOff = i + 8; dataLen = sz; break;
        }
        i += 8 + sz + (sz & 1);
    }
    if (!dataOff || bits != 16) {
        std::fprintf(stderr, "%s: need 16-bit PCM data chunk\n", path.c_str());
        return false;
    }
    const size_t nSamp = dataLen / 2;
    const int16_t* s = reinterpret_cast<const int16_t*>(d + dataOff);
    mono.clear();
    mono.reserve(nSamp / (channels ? channels : 1));
    for (size_t k = 0; k + channels <= nSamp; k += channels)
        mono.push_back(s[k] / 32768.0f);          // channel 0
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
            "usage: %s <modelDir> <clip.wav> [expected] ...\n", argv[0]);
        return 2;
    }
    const std::string modelDir = argv[1];

    lyra::dsp::DeepFistModel model;
    if (!model.load(modelDir)) {
        std::fprintf(stderr, "model load failed: %s\n", model.lastError().c_str());
        return 2;
    }
    std::printf("model loaded from %s\n\n", modelDir.c_str());

    int mismatches = 0, checked = 0;
    for (int a = 2; a < argc; ++a) {
        const std::string wav = argv[a];
        std::string expected;
        bool haveExpected = false;
        if (a + 1 < argc && std::strstr(argv[a + 1], ".wav") == nullptr) {
            expected = argv[++a]; haveExpected = true;
        }

        std::vector<float> mono; int rate = 0;
        if (!readWav(wav, mono, rate)) { ++mismatches; continue; }

        std::string text;
        if (rate == 3200) {
            text = model.decode3200(mono.data(), (int)mono.size());
        } else {
            lyra::dsp::DeepFistResampler dec(rate, 3200.0);
            std::vector<float> a3200;
            dec.process(mono.data(), (int)mono.size(), a3200);
            text = model.decode3200(a3200.data(), (int)a3200.size());
        }

        std::printf("%-28s @%5d Hz -> \"%s\"\n", wav.c_str(), rate, text.c_str());
        if (haveExpected) {
            ++checked;
            const bool ok = (text == expected);
            if (!ok) ++mismatches;
            std::printf("   expected \"%s\"  [%s]\n",
                        expected.c_str(), ok ? "PASS" : "FAIL");
        }
    }
    std::printf("\n%d checked, %d mismatch(es)\n", checked, mismatches);
    return mismatches;
}

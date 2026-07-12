// DeepFist neural CW decoder — real-time STREAMING harness.
//
// Plays a 48 kHz mono CW WAV through the actual NeuralCwDecoder (worker thread +
// frame-timed commit), in real time, and prints committed characters as they
// stream — exactly the live-app path, minus the radio/GUI.  Lets us iterate on
// the streaming behavior deterministically.
//
//   test_neural_cw_stream <modelDir> <clip48k.wav>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dsp/deepfist/NeuralCwDecoder.h"

namespace {
bool readWav(const std::string& path, std::vector<float>& mono, int& rate) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    if (b.size() < 44) return false;
    const uint8_t* d = b.data();
    auto u16 = [&](size_t o){ return (uint16_t)(d[o] | (d[o+1] << 8)); };
    auto u32 = [&](size_t o){ return (uint32_t)(d[o] | (d[o+1]<<8) | (d[o+2]<<16) | (d[o+3]<<24)); };
    int channels = 1, bits = 16; size_t dataOff = 0, dataLen = 0, i = 12;
    while (i + 8 <= b.size()) {
        const size_t sz = u32(i + 4);
        if (!std::memcmp(d + i, "fmt ", 4)) {
            channels = u16(i + 8 + 2); rate = (int)u32(i + 8 + 4); bits = u16(i + 8 + 14);
        } else if (!std::memcmp(d + i, "data", 4)) { dataOff = i + 8; dataLen = sz; break; }
        i += 8 + sz + (sz & 1);
    }
    if (!dataOff || bits != 16) return false;
    const int16_t* s = reinterpret_cast<const int16_t*>(d + dataOff);
    const size_t nSamp = dataLen / 2;
    mono.clear();
    for (size_t k = 0; k + channels <= nSamp; k += channels) mono.push_back(s[k] / 32768.0f);
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <modelDir> <clip48k.wav>\n", argv[0]); return 2; }
    lyra::dsp::NeuralCwDecoder dec;
    if (!dec.loadModel(argv[1])) {
        std::fprintf(stderr, "model load failed: %s\n", dec.lastError().c_str()); return 2;
    }
    std::vector<float> mono; int rate = 0;
    if (!readWav(argv[2], mono, rate)) { std::fprintf(stderr, "wav read failed\n"); return 2; }
    dec.setSampleRate(rate);

    std::mutex mx; std::string transcript;
    const auto t0 = std::chrono::steady_clock::now();
    dec.onText = [&](const std::string& s) {
        std::lock_guard<std::mutex> lk(mx);
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("[%6.2fs] +\"%s\"\n", t, s.c_str());
        std::fflush(stdout);
        transcript += s;
    };

    std::printf("streaming %s (%.1fs @ %d Hz) in real time...\n\n",
                argv[2], mono.size() / (double)rate, rate);
    const int block = rate / 50;                     // 20 ms blocks
    for (size_t i = 0; i < mono.size(); i += block) {
        const int n = (int)std::min((size_t)block, mono.size() - i);
        dec.process(&mono[i], n);
        std::this_thread::sleep_until(t0 + std::chrono::duration<double>((i + n) / (double)rate));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));  // flush settle latency

    std::lock_guard<std::mutex> lk(mx);
    std::printf("\n=== final transcript ===\n%s\n", transcript.c_str());
    return 0;
}

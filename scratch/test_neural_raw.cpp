// DeepFist raw, DETERMINISTIC decode of a WAV slice — no worker thread, no
// streaming commit.  Resamples 48k->3200, then decodes in tiled 6 s windows and
// prints each window's greedy transcript (spaces preserved) plus the rhythm
// word-gap count.  Purpose: see the model's raw character output + whether it
// emits space tokens, reproducibly, to compare against a reference decoder.
//
//   test_neural_raw <modelDir> <wav> [startSec] [durSec] [winSec] [hopSec]
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "dsp/deepfist/DeepFistCtc.h"
#include "dsp/deepfist/DeepFistModel.h"
#include "dsp/deepfist/DeepFistResampler.h"

using namespace lyra::dsp;

// Greedy decode a window with an explicit blank penalty (decode3200 hardcodes 0).
static std::string decodePen(DeepFistModel& model, const float* a, int n, float pen) {
    std::vector<float> logits; int T = 0, C = 0;
    if (!model.infer(a, n, logits, T, C)) return {};
    CtcFrames fr = greedyCtcFrames(logits.data(), T, C, pen);
    const auto& toks = model.tokens();
    std::string s;
    for (int id : fr.ids)
        if (id > 0 && id < (int)toks.size()) s += toks[(size_t)id];
    return s;
}

static std::vector<float> readWavF32(const char* path, int& sr) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("cannot open %s\n", path); return {}; }
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> b(sz); std::fread(b.data(), 1, sz, f); std::fclose(f);
    int fmt = 1, ch = 1, bits = 16; sr = 48000; size_t dataOff = 0, dataLen = 0;
    for (size_t p = 12; p + 8 <= b.size();) {
        uint32_t cksz; std::memcpy(&cksz, &b[p + 4], 4);
        if (std::memcmp(&b[p], "fmt ", 4) == 0) {
            std::memcpy(&fmt, &b[p + 8], 2); std::memcpy(&ch, &b[p + 10], 2);
            std::memcpy(&sr, &b[p + 12], 4); std::memcpy(&bits, &b[p + 22], 2);
        } else if (std::memcmp(&b[p], "data", 4) == 0) {
            dataOff = p + 8;
            dataLen = (cksz == 0 || dataOff + cksz > b.size()) ? b.size() - dataOff : cksz;
            break;
        }
        p += 8 + cksz + (cksz & 1);
    }
    std::vector<float> out;
    const uint8_t* d = &b[dataOff];
    size_t frames = dataLen / (ch * (bits / 8));
    out.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        if (fmt == 3 && bits == 32) { float v; std::memcpy(&v, d + i * ch * 4, 4); out[i] = v; }
        else if (bits == 16) { int16_t v; std::memcpy(&v, d + i * ch * 2, 2); out[i] = v / 32768.0f; }
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: %s <modelDir> <wav> [startSec] [durSec] [winSec] [hopSec]\n", argv[0]); return 2; }
    int sr = 0;
    std::vector<float> full = readWavF32(argv[2], sr);
    if (full.empty()) { std::printf("wav read failed\n"); return 2; }
    double startSec = argc > 3 ? std::atof(argv[3]) : 0.0;
    double durSec   = argc > 4 ? std::atof(argv[4]) : 0.0;
    double winSec   = argc > 5 ? std::atof(argv[5]) : 6.0;
    double hopSec   = argc > 6 ? std::atof(argv[6]) : 6.0;
    long total = (long)full.size();
    long s0 = startSec < 0 ? total + (long)(startSec * sr) : (long)(startSec * sr);
    if (s0 < 0) s0 = 0; if (s0 > total) s0 = total;
    long len = durSec > 0 ? (long)(durSec * sr) : total - s0;
    if (s0 + len > total) len = total - s0;
    std::printf("file=%.1fs  slice=[%.1f..%.1f]s  win=%.1fs hop=%.1fs\n",
                (double)total / sr, (double)s0 / sr, (double)(s0 + len) / sr, winSec, hopSec);

    DeepFistModel model;
    if (!model.load(argv[1])) { std::printf("model load failed: %s\n", model.lastError().c_str()); return 2; }

    // Resample the whole slice once to 3200 Hz.
    DeepFistResampler decim(sr, 3200.0);
    std::vector<float> a; a.reserve(len / 15 + 16);
    decim.process(full.data() + s0, (int)len, a);
    const int n3200 = (int)a.size();
    const int SR = 3200;
    const int win = (int)(winSec * SR);
    const int hop = (int)(hopSec * SR);
    std::printf("resampled: %d samp @3200 (%.1fs)\n\n", n3200, (double)n3200 / SR);

    // Per-window keying-gate diagnostic — mirrors NeuralCwDecoder's live gate
    // (it skips decode + emits nothing when keyingRatio < 12).  This shows
    // whether the gate is what suppresses live output on this capture.
    std::printf("=== per-window keying gate (live threshold=12.0) + tone ===\n");
    for (int off = 0; off + win <= n3200; off += hop) {
        const float kr = model.keyingRatio(a.data() + off, win);
        const std::string d = decodePen(model, a.data() + off, win, 0.0f);
        std::printf("  win @%5.1fs  keyingRatio=%8.2f  %-12s  decode0=\"%s\"\n",
                    (double)off / SR, kr,
                    kr < 12.0f ? "GATE->SILENT" : "pass", d.c_str());
    }
    std::printf("\n");

    // Blank-penalty sweep: negative suppresses insertions/doubles, positive
    // recovers dropped chars.  Prints the full concatenated transcript per pen.
    const float pens[] = {-1.0f, -0.5f, 0.0f, 1.0f, 2.0f};
    for (float pen : pens) {
        std::string catled;
        for (int off = 0; off < n3200; off += hop) {
            const int m = std::min(win, n3200 - off);
            if (m < SR) break;                          // need >=1 s
            catled += decodePen(model, a.data() + off, m, pen);
            catled += ' ';
        }
        std::printf("--- blankPen=%+.1f ---\n%s\n\n", pen, catled.c_str());
    }
    return 0;
}

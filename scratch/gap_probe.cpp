// Probe the keying-envelope gap distribution so word-gap thresholding is tuned
// against real numbers, not guesses. Prints on-run and off-run lengths (in dot
// units) for the whole file's conditioned window.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "dsp/deepfist/DeepFistConditioner.h"

using namespace lyra::dsp;

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
            // The live capture leaves the data-size field 0 when its header isn't
            // patched on exit — fall back to the rest of the file.
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

// Naive 48k->3200 decimator (avg of 15) just for the probe.
static std::vector<float> down(const std::vector<float>& in) {
    std::vector<float> o; o.reserve(in.size() / 15);
    for (size_t i = 0; i + 15 <= in.size(); i += 15) {
        float s = 0; for (int k = 0; k < 15; ++k) s += in[i + k];
        o.push_back(s / 15.0f);
    }
    return o;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: gap_probe <wav> [startSec] [durSec]\n"); return 1; }
    int sr = 0;
    std::vector<float> a48full = readWavF32(argv[1], sr);
    if (a48full.empty()) return 1;
    // Optional [startSec] [durSec] slice (negative start = seconds from the end).
    double startSec = argc > 2 ? std::atof(argv[2]) : 0.0;
    double durSec   = argc > 3 ? std::atof(argv[3]) : 0.0;
    long total = (long)a48full.size();
    long s0 = startSec < 0 ? total + (long)(startSec * sr) : (long)(startSec * sr);
    if (s0 < 0) s0 = 0; if (s0 > total) s0 = total;
    long len = durSec > 0 ? (long)(durSec * sr) : total - s0;
    if (s0 + len > total) len = total - s0;
    std::printf("file=%.1fs  slice=[%.1f..%.1f]s\n", (double)total / sr,
                (double)s0 / sr, (double)(s0 + len) / sr);
    std::vector<float> a48(a48full.begin() + s0, a48full.begin() + s0 + len);
    std::vector<float> a = down(a48);
    const int n = (int)a.size();
    const int SR = 3200;

    DeepFistConditioner cond;
    const float tone = cond.detectTone(a.data(), n);

    // Recompute the keying envelope the same way wordGapTimes does.
    const float pi = 3.14159265358979323846f;
    const float alpha = 1.0f - std::exp(-2.0f * pi * (45.0f) / SR);
    const float ds = std::sin(-2.0f * pi * tone / SR), dc = std::cos(-2.0f * pi * tone / SR);
    float pr = 1, pj = 0, bI = 0, bQ = 0, cI = 0, cQ = 0;
    std::vector<float> env(n); float peak = 1e-9f;
    for (int k = 0; k < n; ++k) {
        float s = a[k]; float di = s * pr, dq = s * pj;
        bI += alpha * (di - bI); bQ += alpha * (dq - bQ);
        cI += alpha * (bI - cI); cQ += alpha * (bQ - cQ);
        float e = std::sqrt(cI * cI + cQ * cQ); env[k] = e; peak = std::max(peak, e);
        float npr = pr * dc - pj * ds, npj = pr * ds + pj * dc; pr = npr; pj = npj;
    }
    float thr = 0.5f * peak;
    std::vector<int> onRuns, offRuns; int on = 0, off = 0; bool sawOn = false;
    for (int k = 0; k < n; ++k) {
        if (env[k] > thr) { if (off > 0 && sawOn) offRuns.push_back(off); off = 0; ++on; }
        else { if (on > 0) { onRuns.push_back(on); sawOn = true; } on = 0; ++off; }
    }
    if (on > 0) onRuns.push_back(on);
    std::vector<int> son = onRuns; std::sort(son.begin(), son.end());
    int dot20 = son.empty() ? 0 : son[son.size() / 5];
    int dotMed = son.empty() ? 0 : son[son.size() / 2];
    std::printf("tone=%.0f  onRuns=%zu offRuns=%zu  dot(20pct)=%d dot(med)=%d samp  (dot20=%.1fms)\n",
                tone, onRuns.size(), offRuns.size(), dot20, dotMed, dot20 * 1000.0f / SR);
    std::printf("OFF gaps in dot20 units: ");
    std::vector<int> offSorted = offRuns; std::sort(offSorted.begin(), offSorted.end());
    for (int g : offSorted) std::printf("%.1f ", dot20 ? (float)g / dot20 : 0);
    std::printf("\n");

    // Element sequence (ms) for a short slice — read prosign shapes directly.
    // dah ~ 3x dit; BT = dah dit dit dit dah = LONG . . . LONG.
    if (durSec > 0 && durSec <= 15.0) {
        std::printf("\nelement sequence (ON=key-down, off=gap), ms:\n  ");
        bool on = env[0] > thr; int run = 0; int col = 0;
        for (int k = 0; k <= n; ++k) {
            const bool cur = (k < n) && env[k] > thr;
            if (cur == on && k < n) { ++run; continue; }
            const float ms = run * 1000.0f / SR;
            if (on) std::printf("[ON %.0f]", ms);
            else    std::printf(" %.0f ", ms);
            if (++col % 12 == 0) std::printf("\n  ");
            on = cur; run = 1;
        }
        std::printf("\n");
    }
    return 0;
}

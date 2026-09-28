// Unit test for lyra::dsp::RttyDecoder.  Qt-free, pure C++.
// Build + run:  cmake --build build --target test_rtty_decoder
//               build/test_rtty_decoder.exe
//
// Synthesises 48 kHz USB AFSK (mark 2295 / space 2125, 45.45 baud, 1.5 stop)
// and asserts the Baudot decoder prints RY.

#include "dsp/RttyDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using lyra::dsp::RttyDecoder;

namespace {
constexpr double kPi      = 3.14159265358979323846;
constexpr double kSr      = 48000.0;
constexpr double kBaud    = 45.45;
constexpr double kMarkHz  = 2295.0;
constexpr double kSpaceHz = 2125.0;

void addTone(std::vector<float>& s, bool mark, int nsamp, double& ph) {
    const double f = mark ? kMarkHz : kSpaceHz;
    const double w = 2.0 * kPi * f / kSr;
    s.reserve(s.size() + static_cast<size_t>(nsamp));
    for (int i = 0; i < nsamp; ++i) {
        s.push_back(static_cast<float>(0.65 * std::sin(ph)));
        ph += w;
        if (ph > 2.0 * kPi) ph -= 2.0 * kPi;
    }
}

void addBaudot(std::vector<float>& s, unsigned char data, int nbit, int nstop,
               double& ph) {
    addTone(s, false, nbit, ph);  // start = space
    for (int b = 0; b < 5; ++b)
        addTone(s, (data >> b) & 1, nbit, ph);
    addTone(s, true, nstop, ph);  // stop = mark
}
}  // namespace

int main() {
    const int nbit  = static_cast<int>(std::lround(kSr / kBaud));
    const int nstop = static_cast<int>(std::lround(1.5 * kSr / kBaud));

    std::vector<float> sig;
    double ph = 0.0;
    addTone(sig, true, static_cast<int>(kSr * 2.0), ph);  // idle mark
    for (int n = 0; n < 40; ++n) {
        addBaudot(sig, 0x1F, nbit, nstop, ph);  // LTRS
        addBaudot(sig, 0x0A, nbit, nstop, ph);  // R
        addBaudot(sig, 0x15, nbit, nstop, ph);  // Y
    }

    RttyDecoder d;
    d.setSampleRate(kSr);
    d.setCenterHz(2210.0);
    d.setShiftHz(170.0);
    d.setBaud(kBaud);
    d.setReverse(false);
    d.setSquelch(true, 18.0);

    std::string got;
    d.onText = [&](const std::string& t) { got += t; };

    constexpr int kBlock = 256;
    for (size_t i = 0; i < sig.size(); ) {
        const int n = static_cast<int>(
            std::min(sig.size() - i, static_cast<size_t>(kBlock)));
        d.process(sig.data() + i, n);
        i += static_cast<size_t>(n);
    }

    std::printf("decoded (%zu chars): [%s]\n", got.size(), got.c_str());
    if (got.find("RYRY") == std::string::npos) {
        std::printf("FAIL: expected RYRY in decoded text with SQL on\n");
        return 1;
    }

    RttyDecoder noiseDec;
    noiseDec.setSampleRate(kSr);
    noiseDec.setCenterHz(2210.0);
    noiseDec.setShiftHz(170.0);
    noiseDec.setBaud(kBaud);
    noiseDec.setSquelch(true, 18.0);
    std::string noiseGot;
    noiseDec.onText = [&](const std::string& t) { noiseGot += t; };
    std::vector<float> noise(static_cast<size_t>(kSr * 2.0));
    uint32_t rng = 0xC0FFEEu;
    for (float& x : noise) {
        rng = rng * 1664525u + 1013904223u;
        const float u1 = ((rng >> 8) & 0xFFFFu) / 65535.0f;
        rng = rng * 1664525u + 1013904223u;
        const float u2 = ((rng >> 8) & 0xFFFFu) / 65535.0f;
        const float r = (u1 < 1e-6f) ? 1e-6f : u1;
        x = 0.35f * std::sqrt(-2.0f * std::log(r))
            * std::cos(static_cast<float>(2.0 * kPi) * u2);
    }
    for (size_t i = 0; i < noise.size(); ) {
        const int n = static_cast<int>(
            std::min(noise.size() - i, static_cast<size_t>(kBlock)));
        noiseDec.process(noise.data() + i, n);
        i += static_cast<size_t>(n);
    }
    std::printf("noise decoded (%zu chars): [%s]\n", noiseGot.size(), noiseGot.c_str());
    if (noiseGot.size() > 4) {
        std::printf("FAIL: SQL should suppress noise garbage (got %zu chars)\n",
                    noiseGot.size());
        return 1;
    }
    std::printf("PASS\n");
    return 0;
}

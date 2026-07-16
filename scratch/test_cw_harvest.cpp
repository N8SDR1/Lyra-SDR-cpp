// Lyra — Phase 2 harvest support unit tests (ring, wav16, json escape).
// Qt-free.  Build + run:
//   cmake --build build --target test_cw_harvest && build/test_cw_harvest
#include "dsp/deepfist/CwHarvestRing.h"
#include "dsp/deepfist/CwHarvestIo.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace lyra::dsp;

namespace {
int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)
}  // namespace

int main() {
    // 1) Ring: fill less than capacity -> snapshot returns exactly what went in.
    {
        CwHarvestRing r(10);
        const float a[4] = {1, 2, 3, 4};
        r.push(a, 4);
        auto s = r.snapshot(4);
        CHECK(s.size() == 4 && s[0] == 1 && s[3] == 4);
        CHECK(r.totalPushed() == 4);
        // Asking for more than was pushed zero-pads the FRONT (oldest side).
        auto s6 = r.snapshot(6);
        CHECK(s6.size() == 6 && s6[0] == 0 && s6[1] == 0 && s6[2] == 1);
    }

    // 2) Ring: overfill wraps — snapshot returns the newest samples in order.
    {
        CwHarvestRing r(5);
        for (int i = 1; i <= 8; ++i) { const float v = float(i); r.push(&v, 1); }
        auto s = r.snapshot(5);
        CHECK(s.size() == 5 && s[0] == 4 && s[4] == 8);   // 4,5,6,7,8
        auto s3 = r.snapshot(3);
        CHECK(s3.size() == 3 && s3[0] == 6 && s3[2] == 8); // 6,7,8
    }

    // 3) wav16: writes a valid int16 WAV, peak-normalized to 20000, and
    //    reports the original peak.
    {
        std::vector<float> tone(3200);
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.25f * std::sin(2.0 * 3.14159265 * 600.0 * i / 3200.0);
        float peak = 0;
        CHECK(writeWav16("test_cw_harvest_tmp.wav", tone, 3200, &peak));
        CHECK(peak > 0.24f && peak < 0.26f);
        // Read back: RIFF/WAVE magic, fmt 1 (PCM), 16-bit, mono, 3200 Hz,
        // data size = 2*N, max |sample| == 20000 (+-1 for rounding).
        FILE* f = std::fopen("test_cw_harvest_tmp.wav", "rb");
        CHECK(f != nullptr);
        unsigned char h[44];
        CHECK(std::fread(h, 1, 44, f) == 44);
        CHECK(h[0]=='R' && h[1]=='I' && h[2]=='F' && h[3]=='F');
        CHECK(h[8]=='W' && h[9]=='A' && h[10]=='V' && h[11]=='E');
        CHECK(h[20] == 1 && h[21] == 0);                  // PCM
        CHECK(h[22] == 1 && h[23] == 0);                  // mono
        const unsigned sr = h[24] | (h[25]<<8) | (h[26]<<16) | (unsigned(h[27])<<24);
        CHECK(sr == 3200);
        CHECK(h[34] == 16 && h[35] == 0);                 // bits/sample
        short smax = 0, s;
        while (std::fread(&s, 2, 1, f) == 1)
            if (std::abs(s) > smax) smax = short(std::abs(s));
        std::fclose(f);
        CHECK(smax >= 19999 && smax <= 20000);
        std::remove("test_cw_harvest_tmp.wav");
    }

    // 4) wav16: unwritable path fails cleanly (no crash, returns false).
    {
        std::vector<float> z(16, 0.0f);
        float peak = 0;
        CHECK(!writeWav16("no_such_dir_xyz/out.wav", z, 3200, &peak));
    }

    // 5) jsonEscape: quotes, backslashes, control chars.
    {
        CHECK(jsonEscape("plain") == "plain");
        CHECK(jsonEscape("a\"b") == "a\\\"b");
        CHECK(jsonEscape("a\\b") == "a\\\\b");
        CHECK(jsonEscape("a\nb") == "a\\nb");
        CHECK(jsonEscape(std::string(1, char(2))) == "\\u0002");
    }

    // 6) writeTextFile round-trip.
    {
        CHECK(writeTextFile("test_cw_harvest_tmp.json", "{\"k\":1}\n"));
        FILE* f = std::fopen("test_cw_harvest_tmp.json", "rb");
        char buf[32] = {0};
        const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
        std::fclose(f);
        CHECK(std::string(buf, n) == "{\"k\":1}\n");
        std::remove("test_cw_harvest_tmp.json");
    }

    if (g_fail == 0) std::printf("test_cw_harvest: ALL PASS\n");
    else             std::printf("test_cw_harvest: %d CHECK(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}

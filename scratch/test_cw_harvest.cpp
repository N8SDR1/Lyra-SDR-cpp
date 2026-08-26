// Lyra — Phase 2 harvest support unit tests (ring, wav16, json escape).
// Qt-free.  Build + run:
//   cmake --build build --target test_cw_harvest && build/test_cw_harvest
#include "dsp/deepfist/CwHarvestRing.h"
#include "dsp/deepfist/CwHarvestIo.h"
#include "dsp/deepfist/CwCaptureHarvester.h"
#include <filesystem>

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

    // 7) Harvester: gold trigger writes wav+json immediately; fade trigger
    //    waits postSec of pump() time; debounce suppresses a repeat trigger.
    {
        namespace fs = std::filesystem;
        const std::string dir = "test_cw_harvest_seg";
        fs::remove_all(dir); fs::create_directory(dir);

        CwHarvestRing ring(3200 * 60);
        std::vector<float> sec(3200, 0.1f);
        for (int i = 0; i < 30; ++i) ring.push(sec.data(), 3200);  // 30 s audio

        CwCaptureHarvester::Config cfg;
        cfg.preSec = 5; cfg.postSec = 2; cfg.debounceSec = 10;
        CwCaptureHarvester h(ring, dir, cfg);
        h.feedKeying(35.0f, 100); h.feedKeying(4.0f, 101);
        h.feedText(false, "NA2DX 5NN");
        h.feedText(true,  "NA2DX 5NN K");

        h.triggerGoldRbn("NA2DX", 101);
        h.pump(101);
        CHECK(h.segmentsWritten() == 1);                 // gold: immediate

        h.triggerGoldRbn("NA2DX", 102);                  // inside debounce
        h.pump(102);
        CHECK(h.segmentsWritten() == 1);                 // suppressed

        h.triggerFade(105);
        h.pump(105);
        CHECK(h.segmentsWritten() == 1);                 // fade: post-roll pending
        h.pump(106);
        CHECK(h.segmentsWritten() == 1);                 // still pending (105+2>106)
        h.pump(107);
        CHECK(h.segmentsWritten() == 2);                 // post-roll complete

        // Files exist in pairs, and the gold sidecar carries the call + texts.
        int wavs = 0, jsons = 0; std::string goldJson;
        for (auto& e : fs::directory_iterator(dir)) {
            const std::string p = e.path().string();
            if (p.size() > 4 && p.substr(p.size() - 4) == ".wav") ++wavs;
            if (p.size() > 5 && p.substr(p.size() - 5) == ".json") {
                ++jsons;
                if (p.find("gold_rbn") != std::string::npos) {
                    FILE* f = std::fopen(p.c_str(), "rb");
                    char buf[4096] = {0};
                    const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
                    std::fclose(f);
                    goldJson.assign(buf, n);
                }
            }
        }
        CHECK(wavs == 2 && jsons == 2);
        CHECK(goldJson.find("\"tier\":\"gold_rbn\"") != std::string::npos);
        CHECK(goldJson.find("\"trigger_call\":\"NA2DX\"") != std::string::npos);
        CHECK(goldJson.find("NA2DX 5NN K") != std::string::npos);
        CHECK(goldJson.find("\"keying_trace\":[[100,35") != std::string::npos);

        fs::remove_all(dir);
    }

    // 8) Retention: with a tiny capBytes, older segments are deleted so the
    //    directory stays under the cap.
    {
        namespace fs = std::filesystem;
        const std::string dir = "test_cw_harvest_cap";
        fs::remove_all(dir); fs::create_directory(dir);
        CwHarvestRing ring(3200 * 60);
        std::vector<float> sec(3200, 0.1f);
        for (int i = 0; i < 30; ++i) ring.push(sec.data(), 3200);

        CwCaptureHarvester::Config cfg;
        cfg.preSec = 5; cfg.postSec = 0; cfg.debounceSec = 0;
        cfg.goldRepeatSec = 0;            // repeat-K7CO golds on purpose here
        cfg.capBytes = 80000;             // ~2 five-second wav16 segments
        CwCaptureHarvester h(ring, dir, cfg);
        for (int k = 0; k < 4; ++k) { h.triggerGoldRbn("K7CO", 200 + k * 100); h.pump(200 + k * 100); }
        CHECK(h.segmentsWritten() == 4);
        long long bytes = 0;
        for (auto& e : fs::directory_iterator(dir)) bytes += (long long)fs::file_size(e);
        CHECK(bytes <= cfg.capBytes);
        fs::remove_all(dir);
    }

    // 9) Retention across tiers: eviction must be strictly oldest-first by
    //    trigger epoch, NOT by lexicographic filename (which would sort the
    //    "gold_rbn" tier ahead of "hard_negative" and evict the high-trust
    //    segment first even though it's newer).
    {
        namespace fs = std::filesystem;
        const std::string dir = "test_cw_harvest_cap_tiers";
        fs::remove_all(dir); fs::create_directory(dir);
        CwHarvestRing ring(3200 * 60);
        std::vector<float> sec(3200, 0.1f);
        for (int i = 0; i < 30; ++i) ring.push(sec.data(), 3200);

        CwCaptureHarvester::Config cfg;
        cfg.preSec = 5; cfg.postSec = 0; cfg.debounceSec = 0;
        cfg.capBytes = 40000;              // fits ~1 segment pair, not 2
        CwCaptureHarvester h(ring, dir, cfg);

        h.triggerFade(200);                // hard_negative — OLDER
        h.pump(200);
        h.triggerGoldRbn("K7CO", 500);      // gold_rbn — NEWER
        h.pump(500);
        CHECK(h.segmentsWritten() == 2);

        bool hasHardNeg = false, hasGold = false;
        long long bytes = 0;
        for (auto& e : fs::directory_iterator(dir)) {
            bytes += (long long)fs::file_size(e);
            const std::string p = e.path().filename().string();
            if (p.find("hard_negative") != std::string::npos) hasHardNeg = true;
            if (p.find("gold_rbn") != std::string::npos) hasGold = true;
        }
        CHECK(bytes <= cfg.capBytes);
        CHECK(!hasHardNeg);   // older pair evicted regardless of tier name
        CHECK(hasGold);       // newer pair survives despite sorting after
                               // "hard_negative" only in trigger-epoch terms,
                               // NOT lexicographically ("gold_rbn" < "hard_negative")
        fs::remove_all(dir);
    }

    // 10) Per-call gold dedupe: the Learn re-scan re-confirms the same call on
    //     every spot-bank change, so a repeat of the SAME call inside
    //     goldRepeatSec is suppressed even outside the tier debounce; a
    //     DIFFERENT call passes, and the same call passes again after the
    //     window expires.
    {
        namespace fs = std::filesystem;
        const std::string dir = "test_cw_harvest_dedupe";
        fs::remove_all(dir); fs::create_directory(dir);
        CwHarvestRing ring(3200 * 60);
        std::vector<float> sec(3200, 0.1f);
        for (int i = 0; i < 30; ++i) ring.push(sec.data(), 3200);

        CwCaptureHarvester::Config cfg;
        cfg.preSec = 5; cfg.postSec = 0; cfg.debounceSec = 0;
        cfg.goldRepeatSec = 600;
        CwCaptureHarvester h(ring, dir, cfg);

        h.triggerGoldRbn("W1AW/3", 100); h.pump(100);
        CHECK(h.segmentsWritten() == 1);                 // first gold: writes
        h.triggerGoldRbn("W1AW/3", 200); h.pump(200);
        CHECK(h.segmentsWritten() == 1);                 // same call, in window
        h.triggerGoldRbn("K7CO", 300); h.pump(300);
        CHECK(h.segmentsWritten() == 2);                 // different call: writes
        h.triggerGoldRbn("W1AW/3", 800); h.pump(800);
        CHECK(h.segmentsWritten() == 3);                 // window expired: writes
        fs::remove_all(dir);
    }

    if (g_fail == 0) std::printf("test_cw_harvest: ALL PASS\n");
    else             std::printf("test_cw_harvest: %d CHECK(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}

# CW Training Harvest — Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** While the operator works CW, Lyra passively (opt-in) captures trust-tiered audio segments + rich JSON sidecars aimed at DeepFist's fade blind spot — hard-negative fades for synthetic-channel tuning and RBN-verified copy as future GOLD training material.

**Architecture:** A Qt-free `CwCaptureHarvester` core owns a rolling 3200 Hz audio ring (fed from the existing CW tap via a second decimator on the audio thread) and a small thread-safe event intake (owner-change, keying-ratio, RBN-confirm). Triggers snapshot the ring into segments — int16 @ 3200 Hz WAV + JSON sidecar — written by a worker thread with debounce and a total-size retention cap. **Curation into training clips stays in the DeepFist repo** (its `tools/pseudo_label_capture.py` / `align_arrl.py` already window, peak-normalize, and label long captures; a future tool there reads these sidecars to filter by tier). Lyra captures faithfully; Python curates.

**Tech Stack:** C++23, Qt 6 (QML), CMake/MSVC (VS 2022 vcvars64). No new dependencies.

## Global Constraints

- Build environment: **VS 2022 vcvars64**; helper `C:\dev\Lyra-SDR-cpp\build3.ps1` (builds everything — only run when lyra.exe is NOT running; a running exe blocks the link with LNK1104, which is then the accepted "PASS-deferred" gate for per-target builds).
- `src/dsp/` additions are Qt-free: `<cstdio> <cstdint> <mutex> <string> <vector> <deque> <functional> <algorithm>` only. Qt appears solely in `wdsp_engine.*`, `prefs.*`, QML.
- Spec §6.6 privacy: capturing received audio to disk is a real side effect → **opt-in, default OFF** (`Prefs.cwCaptureEnabled`), size-capped, local-only. With the pref off: no thread started, no directory created, no file written, zero audio buffered beyond the pre-existing decoders.
- Audio-thread rule: the hot path may only decimate + `memcpy` into the ring under a short mutex (same pattern `NeuralCwDecoder::process` already uses). No allocation after warm-up, no file I/O, no callbacks.
- WAV format: **int16 PCM @ 3200 Hz, peak-normalized to 20000** (matching `deepfist/tools` convention), captured **pre-conditioner**. The sidecar records the original peak so scale is recoverable. (Deviation from spec §6.2's "float": DeepFist's `wmr_dataset.py` loader divides by 32768 unconditionally — int16 is what the consumer actually eats.)
- Defaults (compile-time, YAGNI): ring 60 s; hard-negative segment = 20 s pre + 10 s post-roll; gold segment = 20 s pre; ≥30 s debounce per tier; retention cap 2 GiB (oldest segments deleted).
- Deferred BY DESIGN (record in sidecar schema, do not build): SILVER tier, both-engines-agreement GOLD trigger, SCP-margin GOLD trigger. v1 triggers are exactly two: arbiter `DeepFist→Classic` owner change (tier `hard_negative` — text NOT trusted, channel stats only) and RBN-confirmed call (tier `gold_rbn`).
- **CMake gotcha (bitten twice — P1 CwArbiter, P3 ScpLocal):** every new `src/dsp/**.cpp` must be added to BOTH the new test target AND the main `lyra` target source list.
- Header style: new `src/dsp/deepfist/CwHarvest*` files carry the two-line SPDX MIT header (deepfist folder); a new `src/dsp/CwCaptureHarvester.*` would be Lyra-core style — but all Phase 2 files live under `src/dsp/deepfist/` (they exist to feed DeepFist training), so ALL new files use the SPDX header:
  `// SPDX-License-Identifier: MIT` + `// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md`.
- **QML escape hazard** (chronic): `CwDecoderPanel.qml` contains literal backslash-u marker escapes. Never retype/move those lines; after any edit `LC_ALL=C grep -c $'\x02\|\x03' src/qml/CwDecoderPanel.qml` → `0` and `grep -c 'u0002' src/qml/CwDecoderPanel.qml` → `≥ 2`.

---

### Task 1: `CwArbiter::onOwnerChange` callback (additive, TDD)

**Files:**
- Modify: `src/dsp/CwArbiter.h`
- Modify: `src/dsp/CwArbiter.cpp`
- Test: `scratch/test_cw_arbiter.cpp` (extend; currently 7 cases)

**Interfaces:**
- Consumes: existing `CwArbiter` internals.
- Produces: `std::function<void(CwArbiter::Source from, CwArbiter::Source to)> CwArbiter::onOwnerChange;` — fires OUTSIDE the lock (like `onOutput`), on the thread that caused the switch: a push thread for gap-aligned switches, `updateKeying`'s caller for the cold-start seed. Never fires when owner is unchanged; `reset()` does not fire it.

- [ ] **Step 1: Extend the test (failing)**

In `scratch/test_cw_arbiter.cpp`, add to the `Sink` struct:

```cpp
    std::vector<std::pair<int,int>> switches;   // (from, to) as ints
```

and in `Sink::attach`, after the `onOutput` assignment:

```cpp
        a.onOwnerChange = [this](CwArbiter::Source f, CwArbiter::Source t) {
            switches.push_back({static_cast<int>(f), static_cast<int>(t)});
        };
```

Add test 8 before the summary printf (Source::DeepFist == 0, Source::Classic == 1 by enum order):

```cpp
    // 8) onOwnerChange fires exactly once per real switch, outside the lock:
    //    seed (solid->no event: owner unchanged from default), fade switch at
    //    the gap (DeepFist->Classic), recovery switch back, and a cold-start
    //    seed INTO Classic fires DeepFist->Classic.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(50.0f);                   // seed solid: owner stays DeepFist
        CHECK(k.switches.empty());               //   -> no event
        a.updateKeying(4.0f); a.pushDeepFist(" "); // fade -> switch at gap
        CHECK(k.switches.size() == 1);
        CHECK(k.switches[0] == std::make_pair(0, 1));   // DeepFist -> Classic
        a.updateKeying(50.0f); a.updateKeying(50.0f); a.updateKeying(50.0f);
        a.pushClassic(" ");                      // recovered -> switch back
        CHECK(k.switches.size() == 2);
        CHECK(k.switches[1] == std::make_pair(1, 0));   // Classic -> DeepFist
    }
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(4.0f);                    // cold-start seed mid-fade
        CHECK(k.switches.size() == 1);           // DeepFist(default) -> Classic
        CHECK(k.switches[0] == std::make_pair(0, 1));
    }
```

Build `test_cw_arbiter` (per-target, vcvars64): expected **compile failure** (`onOwnerChange` unknown).

- [ ] **Step 2: Implement**

`src/dsp/CwArbiter.h` — after the `onOutput` declaration add:

```cpp
    // Fires OUTSIDE the lock whenever display ownership actually changes —
    // gap-aligned switches (from the pushing thread) and the cold-start seed
    // (from updateKeying's caller).  Consumed by the Phase-2 harvester as its
    // capture trigger; leaving it null costs nothing.  reset() does not fire.
    std::function<void(Source from, Source to)> onOwnerChange;
```

`src/dsp/CwArbiter.cpp` — in `updateKeying`, the seed block becomes:

```cpp
    Source from = Source::DeepFist, to = Source::DeepFist;
    bool switched = false;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (!seeded_) {
            // Cold start (spec §5.3): the first ratio sample after reset() seeds
            // ownership directly — fading goes straight to the Classic safety net,
            // anything else to DeepFist — so Auto never enters silent mid-fade
            // waiting for a keying gap that the gated engine may never emit.
            seeded_ = true;
            from = owner_;
            owner_ = desired_ =
                (ratio < cfg_.tLow) ? Source::Classic : Source::DeepFist;
            to = owner_;
            switched = (from != to);
        }
        if (ratio < cfg_.tLow) {
            solidCount_ = 0;
            if (++fadeCount_ >= cfg_.nFall) desired_ = Source::Classic;
        } else if (ratio > cfg_.tHigh) {
            fadeCount_ = 0;
            if (++solidCount_ >= cfg_.nRise) desired_ = Source::DeepFist;
        }
        // Dead-band (tLow..tHigh): hold counts and desired_ — no progress either way.
    }
    if (switched && onOwnerChange) onOwnerChange(from, to);
```

(This replaces the whole body of `updateKeying`; the threshold logic is unchanged, only wrapped so the callback fires after the lock releases.)

In `pushDeepFist`, the locked section gains switch capture:

```cpp
    std::string out;
    bool fire = false, switched = false;
    Source from = Source::DeepFist, to = Source::DeepFist;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (owner_ != Source::DeepFist) return;          // muted -> drop
        out = text; fire = true;
        if (isGap(text) && desired_ != owner_) {          // switch at gap
            from = owner_; owner_ = desired_; to = owner_;
            switched = true;
        }
    }
    if (fire && onOutput) onOutput(out, /*fallback=*/false);
    if (switched && onOwnerChange) onOwnerChange(from, to);
```

`pushClassic` gets the mirrored change (`fallback=true`, owner check `Source::Classic`).

- [ ] **Step 3: Build + run**

`test_cw_arbiter`: expected `ALL PASS` (now 9 CHECK groups / 8 numbered cases). The existing 7 cases must pass unmodified — the callback is additive.

- [ ] **Step 4: Commit**

```bash
git add src/dsp/CwArbiter.h src/dsp/CwArbiter.cpp scratch/test_cw_arbiter.cpp
git commit -m "feat(cw): CwArbiter onOwnerChange callback for the Phase-2 harvester"
```

---

### Task 2: Harvest support units — ring, WAV16 writer, sidecar writer (pure, TDD)

**Files:**
- Create: `src/dsp/deepfist/CwHarvestRing.h` (header-only)
- Create: `src/dsp/deepfist/CwHarvestIo.h`
- Create: `src/dsp/deepfist/CwHarvestIo.cpp`
- Test: `scratch/test_cw_harvest.cpp`
- Modify: `CMakeLists.txt` (new `test_cw_harvest` target after `test_scp_local`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `CwHarvestRing(int capacitySamples)`; `void push(const float* x, int n);` (audio thread, mutex'd); `std::vector<float> snapshot(int lastNSamples) const;` (any thread); `long long totalPushed() const;`
  - `bool writeWav16(const std::string& path, const std::vector<float>& mono, int sampleRate, float* outPeak);` — peak-normalizes to 20000, writes a complete int16 PCM WAV; returns false on I/O failure; reports the pre-normalization peak.
  - `std::string jsonEscape(const std::string& s);` and `bool writeTextFile(const std::string& path, const std::string& body);` — sidecar building blocks (the sidecar itself is assembled by the harvester in Task 3).

- [ ] **Step 1: Write the failing test**

Create `scratch/test_cw_harvest.cpp`:

```cpp
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
```

CMake target (after the `test_scp_local` block):

```cmake
# Phase 2 — harvest support units (ring / wav16 / sidecar io).  Qt-free.
#   cmake --build build --target test_cw_harvest && build/test_cw_harvest
add_executable(test_cw_harvest EXCLUDE_FROM_ALL
    scratch/test_cw_harvest.cpp
    src/dsp/deepfist/CwHarvestRing.h
    src/dsp/deepfist/CwHarvestIo.h
    src/dsp/deepfist/CwHarvestIo.cpp
)
target_include_directories(test_cw_harvest PRIVATE src)
if(MSVC)
    target_compile_options(test_cw_harvest PRIVATE /utf-8 /Zc:preprocessor)
endif()
```

Build: expected **compile failure** (headers missing).

- [ ] **Step 2: Implement**

`src/dsp/deepfist/CwHarvestRing.h` (header-only):

```cpp
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — Phase 2 harvest: rolling mono audio ring at the DeepFist rate
// (3200 Hz).  The audio thread push()es decimated samples under a short mutex
// (same discipline as NeuralCwDecoder's window ring); the harvester worker
// snapshot()s the last N samples when a capture trigger fires.  Snapshots
// zero-pad the oldest side when less audio than requested has been pushed.
#pragma once

#include <algorithm>
#include <mutex>
#include <vector>

namespace lyra::dsp {

class CwHarvestRing {
public:
    explicit CwHarvestRing(int capacitySamples)
        : buf_(static_cast<size_t>(capacitySamples), 0.0f) {}

    // audio thread — memcpy under the lock, no allocation.
    void push(const float* x, int n) {
        if (!x || n <= 0) return;
        std::lock_guard<std::mutex> lk(mx_);
        const size_t cap = buf_.size();
        for (int i = std::max(0, n - static_cast<int>(cap)); i < n; ++i) {
            buf_[head_] = x[i];
            head_ = (head_ + 1) % cap;
        }
        total_ += n;
    }

    // any thread — newest `lastNSamples`, oldest first, zero-padded in front
    // when fewer samples have ever been pushed.
    std::vector<float> snapshot(int lastNSamples) const {
        std::vector<float> out(static_cast<size_t>(std::max(0, lastNSamples)), 0.0f);
        std::lock_guard<std::mutex> lk(mx_);
        const size_t cap = buf_.size();
        const long long have = std::min<long long>(total_, static_cast<long long>(cap));
        const long long take = std::min<long long>(have, lastNSamples);
        // copy the newest `take` samples to the END of out (front stays zero)
        for (long long i = 0; i < take; ++i) {
            const size_t src = (head_ + cap - static_cast<size_t>(take - i)) % cap;
            out[out.size() - static_cast<size_t>(take - i)] = buf_[src];
        }
        return out;
    }

    long long totalPushed() const {
        std::lock_guard<std::mutex> lk(mx_);
        return total_;
    }

private:
    mutable std::mutex mx_;
    std::vector<float> buf_;
    size_t             head_ = 0;
    long long          total_ = 0;
};

}  // namespace lyra::dsp
```

`src/dsp/deepfist/CwHarvestIo.h`:

```cpp
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — Phase 2 harvest: tiny Qt-free file helpers.  writeWav16 emits the
// int16-PCM, peak->20000 clip format DeepFist's curation tools already eat
// (tools/pseudo_label_capture.py convention); jsonEscape/writeTextFile build
// the .json sidecars.
#pragma once

#include <string>
#include <vector>

namespace lyra::dsp {

// Peak-normalize `mono` to 20000 and write a complete 16-bit PCM WAV.
// `outPeak` (optional) receives the pre-normalization |peak| so the original
// scale is recoverable from the sidecar.  Returns false on any I/O failure.
bool writeWav16(const std::string& path, const std::vector<float>& mono,
                int sampleRate, float* outPeak = nullptr);

// Minimal JSON string escaping: backslash, quote, and control chars.
std::string jsonEscape(const std::string& s);

// Write `body` to `path` (binary, whole file).  Returns false on failure.
bool writeTextFile(const std::string& path, const std::string& body);

}  // namespace lyra::dsp
```

`src/dsp/deepfist/CwHarvestIo.cpp`:

```cpp
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/CwHarvestIo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace lyra::dsp {

namespace {
void put32(unsigned char* p, uint32_t v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}
void put16(unsigned char* p, uint16_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
}  // namespace

bool writeWav16(const std::string& path, const std::vector<float>& mono,
                int sampleRate, float* outPeak) {
    float peak = 0.0f;
    for (float v : mono) peak = std::max(peak, std::abs(v));
    if (outPeak) *outPeak = peak;
    const float scale = peak > 0.0f ? 20000.0f / peak : 0.0f;

    const uint32_t dataBytes = static_cast<uint32_t>(mono.size() * 2);
    unsigned char h[44];
    h[0]='R'; h[1]='I'; h[2]='F'; h[3]='F'; put32(h + 4, 36 + dataBytes);
    h[8]='W'; h[9]='A'; h[10]='V'; h[11]='E';
    h[12]='f'; h[13]='m'; h[14]='t'; h[15]=' '; put32(h + 16, 16);
    put16(h + 20, 1);                                   // PCM
    put16(h + 22, 1);                                   // mono
    put32(h + 24, static_cast<uint32_t>(sampleRate));
    put32(h + 28, static_cast<uint32_t>(sampleRate) * 2);
    put16(h + 32, 2);                                   // block align
    put16(h + 34, 16);                                  // bits/sample
    h[36]='d'; h[37]='a'; h[38]='t'; h[39]='a'; put32(h + 40, dataBytes);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(h, 1, 44, f) == 44;
    for (size_t i = 0; ok && i < mono.size(); ++i) {
        const float scaled = mono[i] * scale;
        const int   v = static_cast<int>(std::lround(
                          std::clamp(scaled, -20000.0f, 20000.0f)));
        const int16_t s = static_cast<int16_t>(v);
        ok = std::fwrite(&s, 2, 1, f) == 1;
    }
    return std::fclose(f) == 0 && ok;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    out += b;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

bool writeTextFile(const std::string& path, const std::string& body) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(body.data(), 1, body.size(), f) == body.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace lyra::dsp
```

- [ ] **Step 3: Build + run** — `test_cw_harvest`: expected `ALL PASS`.

- [ ] **Step 4: Commit**

```bash
git add src/dsp/deepfist/CwHarvestRing.h src/dsp/deepfist/CwHarvestIo.h src/dsp/deepfist/CwHarvestIo.cpp scratch/test_cw_harvest.cpp CMakeLists.txt
git commit -m "feat(cw): Phase-2 harvest support — audio ring, wav16 writer, sidecar io"
```

---

### Task 3: `CwCaptureHarvester` core — triggers, debounce, post-roll, retention (pure, TDD)

**Files:**
- Create: `src/dsp/deepfist/CwCaptureHarvester.h`
- Create: `src/dsp/deepfist/CwCaptureHarvester.cpp`
- Test: `scratch/test_cw_harvest.cpp` (extend) + `CMakeLists.txt` (add sources to `test_cw_harvest`)

**Interfaces:**
- Consumes: `CwHarvestRing`, `writeWav16`, `jsonEscape`, `writeTextFile` (Task 2).
- Produces (Task 4 wires these; all `feed*`/`trigger*` are thread-safe):
  - `struct CwCaptureHarvester::Config { int sampleRate=3200; int preSec=20; int postSec=10; int debounceSec=30; long long capBytes=2LL*1024*1024*1024; };`
  - `CwCaptureHarvester(CwHarvestRing& ring, std::string outDir, Config cfg = {});`
  - `void feedKeying(float ratio, long long nowSec);` — keeps a 60-entry rolling trace
  - `void feedText(bool fromClassic, const std::string& text);` — rolling last-200-chars per engine (context for the sidecar)
  - `void triggerFade(long long nowSec);` — tier `hard_negative`, waits `postSec` of post-roll (see `pump`)
  - `void triggerGoldRbn(const std::string& call, long long nowSec);` — tier `gold_rbn`, immediate
  - `void pump(long long nowSec);` — called periodically (~1 Hz) by the owner's worker; completes pending post-rolls (writes the WAV+sidecar) and enforces retention
  - `int segmentsWritten() const;`
  - Segment on disk: `<outDir>/<tier>_<epoch>_<seq>.wav` + same basename `.json`.
- Sidecar schema (v1): `{"schema":1,"tier":"...","utc":<epoch>,"trigger_call":"...", "sample_rate":3200,"pre_sec":20,"post_sec":N,"peak":<float>,"keying_trace":[[t,r],...],"text_classic":"...","text_deepfist":"..."}`

- [ ] **Step 1: Extend the test (failing)**

Append to `scratch/test_cw_harvest.cpp` (before the summary; add `#include "dsp/deepfist/CwCaptureHarvester.h"` and `#include <filesystem>` at the top; use a local temp dir):

```cpp
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
        cfg.capBytes = 80000;             // ~2 five-second wav16 segments
        CwCaptureHarvester h(ring, dir, cfg);
        for (int k = 0; k < 4; ++k) { h.triggerGoldRbn("K7CO", 200 + k * 100); h.pump(200 + k * 100); }
        CHECK(h.segmentsWritten() == 4);
        long long bytes = 0;
        for (auto& e : fs::directory_iterator(dir)) bytes += (long long)fs::file_size(e);
        CHECK(bytes <= cfg.capBytes);
        fs::remove_all(dir);
    }
```

Add to the `test_cw_harvest` target sources in `CMakeLists.txt`:

```cmake
    src/dsp/deepfist/CwCaptureHarvester.h
    src/dsp/deepfist/CwCaptureHarvester.cpp
```

Build: expected **compile failure** (header missing).

- [ ] **Step 2: Implement**

`src/dsp/deepfist/CwCaptureHarvester.h`:

```cpp
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — Phase 2 harvest core (spec §6.1-6.3): turns arbiter/RBN events into
// trust-tiered capture segments — int16@3200 WAV + JSON sidecar — for the
// DeepFist training pipeline.  Curation into training clips happens OFFLINE in
// the deepfist repo (tools/ already window + label long captures); this class
// only captures faithfully and records the adjudication evidence.
//
// v1 tiers/triggers (SILVER, agreement-GOLD and SCP-margin-GOLD deferred):
//   hard_negative — arbiter DeepFist->Classic switch (a fade the gate lost);
//                   text NOT trusted, captured for channel statistics.
//   gold_rbn      — a decoded call confirmed live on RBN/cluster (the Learn
//                   tap moment); the strongest external label evidence.
//
// Threading: feed*/trigger* are thread-safe (short mutex; no I/O).  All disk
// work happens in pump(), which the owner calls ~1 Hz from its own worker
// thread — never the audio or GUI thread.
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "dsp/deepfist/CwHarvestRing.h"

namespace lyra::dsp {

class CwCaptureHarvester {
public:
    struct Config {
        int       sampleRate  = 3200;
        int       preSec      = 20;    // audio before the trigger
        int       postSec     = 10;    // post-roll for fade captures
        int       debounceSec = 30;    // min gap between segments per tier
        long long capBytes    = 2LL * 1024 * 1024 * 1024;   // retention cap
    };

    CwCaptureHarvester(CwHarvestRing& ring, std::string outDir, Config cfg = {});

    // event intake — any thread, cheap, no I/O
    void feedKeying(float ratio, long long nowSec);
    void feedText(bool fromClassic, const std::string& text);
    void feedWpm(int wpm);                                    // latest RX estimate
    void triggerFade(long long nowSec);                       // hard_negative
    void triggerGoldRbn(const std::string& call, long long nowSec);

    // worker heartbeat (~1 Hz): completes pending post-rolls, enforces the
    // retention cap.  ALL file I/O lives here.
    void pump(long long nowSec);

    int segmentsWritten() const;

private:
    struct Pending {
        std::string tier;
        std::string call;
        long long   triggeredAt = 0;
        int         postSec     = 0;    // 0 = write on next pump
    };

    void writeSegment(const Pending& p, long long nowSec);
    void enforceCap();

    CwHarvestRing&      ring_;
    const std::string   dir_;
    const Config        cfg_;

    mutable std::mutex  mx_;
    std::deque<std::pair<long long, float>> keyTrace_;   // (t, ratio), last 60
    std::string         textClassic_, textDeepFist_;     // last ~200 chars each
    int                 lastWpm_ = 0;                    // latest RX WPM estimate
    std::vector<Pending> pending_;
    long long           lastFadeAt_ = -1000000;          // debounce, PER TIER
    long long           lastGoldAt_ = -1000000;
    int                 written_   = 0;
    int                 seq_       = 0;
};

}  // namespace lyra::dsp
```

`src/dsp/deepfist/CwCaptureHarvester.cpp`:

```cpp
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/CwCaptureHarvester.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "dsp/deepfist/CwHarvestIo.h"

namespace fs = std::filesystem;

namespace lyra::dsp {

namespace {
constexpr size_t kTraceMax = 60;     // ~24 s of keying history at 2.5 Hz
constexpr size_t kTextMax  = 200;    // rolling per-engine context chars

void rollAppend(std::string& acc, const std::string& s) {
    acc += s;
    if (acc.size() > kTextMax) acc.erase(0, acc.size() - kTextMax);
}
}  // namespace

CwCaptureHarvester::CwCaptureHarvester(CwHarvestRing& ring, std::string outDir,
                                       Config cfg)
    : ring_(ring), dir_(std::move(outDir)), cfg_(cfg) {}

void CwCaptureHarvester::feedKeying(float ratio, long long nowSec) {
    std::lock_guard<std::mutex> lk(mx_);
    keyTrace_.push_back({nowSec, ratio});
    while (keyTrace_.size() > kTraceMax) keyTrace_.pop_front();
}

void CwCaptureHarvester::feedText(bool fromClassic, const std::string& text) {
    std::lock_guard<std::mutex> lk(mx_);
    rollAppend(fromClassic ? textClassic_ : textDeepFist_, text);
}

void CwCaptureHarvester::feedWpm(int wpm) {
    std::lock_guard<std::mutex> lk(mx_);
    lastWpm_ = wpm;
}

void CwCaptureHarvester::triggerFade(long long nowSec) {
    std::lock_guard<std::mutex> lk(mx_);
    if (nowSec - lastFadeAt_ < cfg_.debounceSec) return;   // per-tier debounce
    lastFadeAt_ = nowSec;
    pending_.push_back({"hard_negative", "", nowSec, cfg_.postSec});
}

void CwCaptureHarvester::triggerGoldRbn(const std::string& call, long long nowSec) {
    std::lock_guard<std::mutex> lk(mx_);
    if (nowSec - lastGoldAt_ < cfg_.debounceSec) return;   // per-tier debounce
    lastGoldAt_ = nowSec;
    pending_.push_back({"gold_rbn", call, nowSec, 0});
}

void CwCaptureHarvester::pump(long long nowSec) {
    std::vector<Pending> ready;
    {
        std::lock_guard<std::mutex> lk(mx_);
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (nowSec >= it->triggeredAt + it->postSec) {
                ready.push_back(*it);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const Pending& p : ready) writeSegment(p, nowSec);
    if (!ready.empty()) enforceCap();
}

void CwCaptureHarvester::writeSegment(const Pending& p, long long nowSec) {
    // Segment audio: preSec before the trigger + whatever post-roll elapsed.
    const int postGot = static_cast<int>(std::min<long long>(
        nowSec - p.triggeredAt, p.postSec));
    const int nSamp = (cfg_.preSec + postGot) * cfg_.sampleRate;
    const std::vector<float> audio = ring_.snapshot(nSamp);

    std::string base;
    std::deque<std::pair<long long, float>> trace;
    std::string tClassic, tDeep;
    int wpm = 0;
    {
        std::lock_guard<std::mutex> lk(mx_);
        char b[64];
        std::snprintf(b, sizeof b, "%s_%lld_%03d",
                      p.tier.c_str(), p.triggeredAt, seq_++);
        base = b;
        trace = keyTrace_;
        tClassic = textClassic_;
        tDeep = textDeepFist_;
        wpm = lastWpm_;
    }

    const std::string wavPath  = dir_ + "/" + base + ".wav";
    const std::string jsonPath = dir_ + "/" + base + ".json";
    float peak = 0.0f;
    if (!writeWav16(wavPath, audio, cfg_.sampleRate, &peak)) return;

    std::string j = "{";
    j += "\"schema\":1";
    j += ",\"tier\":\"" + jsonEscape(p.tier) + "\"";
    j += ",\"utc\":" + std::to_string(p.triggeredAt);
    j += ",\"trigger_call\":\"" + jsonEscape(p.call) + "\"";
    j += ",\"sample_rate\":" + std::to_string(cfg_.sampleRate);
    j += ",\"pre_sec\":" + std::to_string(cfg_.preSec);
    j += ",\"post_sec\":" + std::to_string(postGot);
    j += ",\"peak\":" + std::to_string(peak);
    j += ",\"wpm\":" + std::to_string(wpm);
    j += ",\"keying_trace\":[";
    bool first = true;
    for (const auto& [t, r] : trace) {
        if (!first) j += ",";
        first = false;
        char b[48];
        std::snprintf(b, sizeof b, "[%lld,%.2f]", t, r);
        j += b;
    }
    j += "]";
    j += ",\"text_classic\":\"" + jsonEscape(tClassic) + "\"";
    j += ",\"text_deepfist\":\"" + jsonEscape(tDeep) + "\"";
    j += "}\n";
    if (!writeTextFile(jsonPath, j)) {
        std::remove(wavPath.c_str());        // no orphan wav without sidecar
        return;
    }

    std::lock_guard<std::mutex> lk(mx_);
    ++written_;
}

void CwCaptureHarvester::enforceCap() {
    // Oldest-first delete until the directory fits the cap.  Names embed the
    // trigger epoch, so lexicographic-by-epoch ordering == age ordering.
    std::error_code ec;
    std::vector<std::pair<std::string, long long>> files;   // path, size
    long long total = 0;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        const long long sz = static_cast<long long>(fs::file_size(e, ec));
        files.push_back({e.path().string(), sz});
        total += sz;
    }
    std::sort(files.begin(), files.end());
    for (const auto& [path, sz] : files) {
        if (total <= cfg_.capBytes) break;
        if (fs::remove(path, ec)) total -= sz;
    }
}

int CwCaptureHarvester::segmentsWritten() const {
    std::lock_guard<std::mutex> lk(mx_);
    return written_;
}

}  // namespace lyra::dsp
```

- [ ] **Step 3: Build + run** — `test_cw_harvest`: expected `ALL PASS` (8 cases).

- [ ] **Step 4: Commit**

```bash
git add src/dsp/deepfist/CwCaptureHarvester.h src/dsp/deepfist/CwCaptureHarvester.cpp scratch/test_cw_harvest.cpp CMakeLists.txt
git commit -m "feat(cw): Phase-2 CwCaptureHarvester — tiered capture, post-roll, retention"
```

---

### Task 4: `WdspEngine` + `Prefs` wiring

**Files:**
- Modify: `src/wdsp_engine.h` (includes, members, worker, Q_INVOKABLE status)
- Modify: `src/wdsp_engine.cpp` (feeds, tap, worker, enable/disable)
- Modify: `src/prefs.h`, `src/prefs.cpp` (`cwCaptureEnabled`, default false)
- Modify: `CMakeLists.txt` (**add all three Phase-2 .cpp/.h sets to the `lyra` target** — the twice-bitten gotcha)

**Interfaces:**
- Consumes: everything from Tasks 1-3.
- Produces (Task 5 relies on):
  - `Prefs.cwCaptureEnabled` (bool Q_PROPERTY, default false, persisted, full pattern like `cwLearnCalls`)
  - `Q_INVOKABLE void WdspEngine::setCwCaptureEnabled(bool on);` — creates the output dir, allocates ring+harvester, starts the 1 Hz pump worker on first enable; stops the worker on disable (objects stay, inert)
  - `Q_INVOKABLE int WdspEngine::cwCaptureCount() const;` — segments written this session (0 when never enabled)
  - Capture dir: `QStandardPaths::writableLocation(DocumentsLocation) + "/Lyra/cw_harvest"`

- [ ] **Step 1: Members + lifecycle (`wdsp_engine.h`)**

Includes (next to the ScpLocal one):

```cpp
#include "dsp/deepfist/CwCaptureHarvester.h"  // Phase 2: training harvest
#include "dsp/deepfist/CwHarvestRing.h"
```

Members (next to `cwScpLocal_`; `unique_ptr`s so the OFF default allocates nothing):

```cpp
    // Phase 2 harvest — allocated on first enable (opt-in, default off).
    std::unique_ptr<lyra::dsp::CwHarvestRing>       cwHarvestRing_;
    std::unique_ptr<lyra::dsp::DeepFistResampler>   cwHarvestDecim_;
    std::unique_ptr<lyra::dsp::CwCaptureHarvester>  cwHarvester_;
    std::vector<float>                              cwHarvestTmp_;
    std::atomic<bool>                               cwCaptureOn_{false};
    std::thread                                     cwHarvestWorker_;
    std::atomic<bool>                               cwHarvestRun_{false};
```

(Add `#include <thread>` and `#include <memory>` to the header's std includes if absent.)

Public methods (near `cwNoteConfirmedCall`):

```cpp
    // Phase 2 harvest — opt-in capture of trust-tiered CW segments for
    // DeepFist training (spec §6).  Enabling creates <Documents>/Lyra/
    // cw_harvest, allocates the ring+harvester and starts a 1 Hz pump worker;
    // disabling stops the worker.  GUI thread.
    Q_INVOKABLE void setCwCaptureEnabled(bool on);
    Q_INVOKABLE int  cwCaptureCount() const {
        return cwHarvester_ ? cwHarvester_->segmentsWritten() : 0;
    }
```

- [ ] **Step 2: Implementation (`wdsp_engine.cpp`)**

Add near `ensureCwScpLocal` (uses `QStandardPaths`, `QDir`, `QDateTime` — already included from Phase 3):

```cpp
// Phase 2 — opt-in harvest lifecycle.  First enable allocates the 60 s ring,
// its own 48k->3200 decimator (parallel to the neural engine's, so capture
// works in EVERY engine mode), and the harvester; a 1 Hz worker pumps
// post-rolls + retention.  Disable stops the worker; objects stay for cheap
// re-enable.  GUI thread only.
void WdspEngine::setCwCaptureEnabled(bool on)
{
    if (on == cwCaptureOn_.load(std::memory_order_relaxed)) return;
    if (on) {
        if (!cwHarvester_) {
            const QString dir =
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                + QStringLiteral("/Lyra/cw_harvest");
            QDir().mkpath(dir);
            cwHarvestRing_  = std::make_unique<lyra::dsp::CwHarvestRing>(3200 * 60);
            cwHarvestDecim_ =
                std::make_unique<lyra::dsp::DeepFistResampler>(cfg_.outRate, 3200.0);
            cwHarvester_    = std::make_unique<lyra::dsp::CwCaptureHarvester>(
                *cwHarvestRing_, dir.toStdString());
        }
        cwHarvestRun_.store(true);
        cwHarvestWorker_ = std::thread([this] {
            while (cwHarvestRun_.load()) {
                cwHarvester_->pump(QDateTime::currentSecsSinceEpoch());
                for (int i = 0; i < 10 && cwHarvestRun_.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        cwCaptureOn_.store(true, std::memory_order_relaxed);   // tap feeds from here
    } else {
        cwCaptureOn_.store(false, std::memory_order_relaxed);  // tap stops first
        cwHarvestRun_.store(false);
        if (cwHarvestWorker_.joinable()) cwHarvestWorker_.join();
    }
}
```

(Add `#include <chrono>` if absent. Also join the worker in `WdspEngine`'s destructor: `cwHarvestRun_.store(false); if (cwHarvestWorker_.joinable()) cwHarvestWorker_.join();` next to the existing teardown.)

Feed points — each is a 2-4 line addition to code that already exists:

1. **Audio** — in the CW tap block (after the engine routing, next to the `LYRA_CW_WAV` capture), add:

```cpp
        // Phase 2 harvest: parallel decimate into the capture ring (opt-in).
        if (cwCaptureOn_.load(std::memory_order_relaxed)) {
            cwHarvestTmp_.clear();
            cwHarvestDecim_->process(cwMonoBuf_.data(), nframes, cwHarvestTmp_);
            if (!cwHarvestTmp_.empty())
                cwHarvestRing_->push(cwHarvestTmp_.data(),
                                     static_cast<int>(cwHarvestTmp_.size()));
        }
```

2. **Keying trace** — extend the existing `neuralCw_.onKeying` lambda:

```cpp
    neuralCw_.onKeying = [this](float r) {
        if (cwEngine_.load(std::memory_order_relaxed) == 2)
            cwArbiter_.updateKeying(r);
        if (cwCaptureOn_.load(std::memory_order_relaxed))
            cwHarvester_->feedKeying(r, QDateTime::currentSecsSinceEpoch());
    };
```

3. **Fade trigger** — new wiring in the init section (after `cwArbiter_.onOutput`):

```cpp
    // Phase 2 harvest: a DeepFist->Classic ownership switch IS the fade event.
    cwArbiter_.onOwnerChange = [this](lyra::dsp::CwArbiter::Source from,
                                      lyra::dsp::CwArbiter::Source to) {
        if (from == lyra::dsp::CwArbiter::Source::DeepFist &&
            to   == lyra::dsp::CwArbiter::Source::Classic  &&
            cwCaptureOn_.load(std::memory_order_relaxed))
            cwHarvester_->triggerFade(QDateTime::currentSecsSinceEpoch());
    };
```

4. **Texts** — extend the two existing `onText` lambdas (Classic and Neural) with one guarded line each, placed before their existing emit/route logic:

```cpp
        if (cwCaptureOn_.load(std::memory_order_relaxed))
            cwHarvester_->feedText(/*fromClassic=*/true, s);    // false in neural's
```

5. **WPM** — extend BOTH existing `onWpm` lambdas (Classic and Neural) with one guarded line before their owner-gate logic:

```cpp
        if (cwCaptureOn_.load(std::memory_order_relaxed))
            cwHarvester_->feedWpm(w);        // `wpm` in the neural lambda
```

6. **GOLD trigger** — in `cwNoteConfirmedCall` (the RBN-confirmed moment), after the `note(...)` call:

```cpp
    if (cwCaptureOn_.load(std::memory_order_relaxed))
        cwHarvester_->triggerGoldRbn(call.trimmed().toUpper().toStdString(),
                                     QDateTime::currentSecsSinceEpoch());
```

Thread-safety note for the implementer: every `cwHarvester_->` call above is guarded by `cwCaptureOn_`, and `setCwCaptureEnabled` orders stores so the pointer is fully constructed before the flag flips on, and the flag flips off before the worker joins. The audio-thread feed (`cwHarvestRing_->push`) uses the ring's own short mutex — the same discipline as `NeuralCwDecoder::process`.

- [ ] **Step 3: `Prefs.cwCaptureEnabled`**

Exactly the `cwLearnCalls` pattern from Phase 3 (Q_PROPERTY + accessor + setter + signal + member + key `"cw/capture_enabled"` + load default `false`). Copy that pattern verbatim with the new name; comment:

```cpp
    // Phase 2 — opt-in: capture trust-tiered CW audio segments to Documents/
    // Lyra/cw_harvest for DeepFist training (privacy: records received
    // audio, default off; size-capped).
```

- [ ] **Step 4: CMake — lyra target**

Add to the `lyra` target's DeepFist source block (next to `ScpLocal.cpp`):

```cmake
    src/dsp/deepfist/CwHarvestRing.h
    src/dsp/deepfist/CwHarvestIo.h
    src/dsp/deepfist/CwHarvestIo.cpp
    src/dsp/deepfist/CwCaptureHarvester.h
    src/dsp/deepfist/CwCaptureHarvester.cpp
```

- [ ] **Step 5: Build gate** — lyra target compiles (+ links if lyra.exe isn't running; LNK1104-only = PASS-deferred).

- [ ] **Step 6: Commit**

```bash
git add src/wdsp_engine.h src/wdsp_engine.cpp src/prefs.h src/prefs.cpp CMakeLists.txt
git commit -m "feat(cw): wire the Phase-2 harvester — tap feed, arbiter/RBN triggers, cwCaptureEnabled pref"
```

---

### Task 5: QML "Harvest" toggle + docs

**Files:**
- Modify: `src/qml/CwDecoderPanel.qml` (chip beside Learn; restore pref at startup)
- Modify: `docs/DEEPFIST.md` (harvest section)

**Interfaces:**
- Consumes: `WdspEngine.setCwCaptureEnabled(bool)`, `Prefs.cwCaptureEnabled` (Task 4).

**⚠ Escape hazard applies (see Global Constraints) — run both integrity greps after editing.**

- [ ] **Step 1: Chip**

After the Learn `ChipButton` in the Decode/Clear row:

```qml
            ChipButton {
                label: qsTr("Harvest")
                lit: Prefs.cwCaptureEnabled
                onClicked: {
                    Prefs.cwCaptureEnabled = !Prefs.cwCaptureEnabled
                    WdspEngine.setCwCaptureEnabled(Prefs.cwCaptureEnabled)
                }
            }
```

- [ ] **Step 2: Restore at startup**

In `Component.onCompleted`, after the engine-restore block:

```qml
        // Phase 2: resume harvest capture if the operator left it on.
        if (Prefs.cwCaptureEnabled)
            WdspEngine.setCwCaptureEnabled(true)
```

- [ ] **Step 3: Docs**

Append to `docs/DEEPFIST.md` after the "Learn calls" paragraph:

```markdown
**Harvest** (opt-in, CW panel): captures trust-tiered training segments to
`Documents/Lyra/cw_harvest` — `hard_negative` fades (the arbiter's
DeepFist→Classic switches; audio for channel statistics, text untrusted) and
`gold_rbn` segments (copy whose callsign was RBN-confirmed live). Each segment
is an int16 @ 3200 Hz WAV (peak in the sidecar) + a JSON sidecar (tier,
keying-ratio trace, both engines' rolling text). Size-capped (2 GiB, oldest
deleted). Curation into training clips happens offline in the DeepFist repo's
`tools/`; Lyra only captures. SILVER and agreement/SCP-margin GOLD tiers are
deferred (sidecar `schema:1` reserves them).
```

- [ ] **Step 4: Build + integrity greps + operator check**

Build lyra (or full `build3.ps1` if not running); both QML greps clean. Operator check (deferred if unavailable): toggle **Harvest** on in Auto during QSB → after a fade, `Documents/Lyra/cw_harvest` gains a `hard_negative_*.wav/.json` pair; a confirmed green call adds a `gold_rbn_*` pair; toggling off stops new files.

- [ ] **Step 5: Commit**

```bash
git add src/qml/CwDecoderPanel.qml docs/DEEPFIST.md
git commit -m "feat(cw): Harvest toggle — opt-in capture of tiered CW training segments"
```

---

## Notes / deferrals

- **SILVER tier, both-engines-agreement GOLD, SCP-margin GOLD:** deferred (spec allows staging); the sidecar's `schema` field versions them in later.
- **Channel-stat rollup (spec §6.3 output 2):** lives in the DeepFist repo as an offline script over the sidecars' keying traces — not Lyra code. The sidecar deliberately captures the raw evidence (trace + pre-conditioner audio) so that analysis needs nothing more from Lyra.
- **labels.jsonl / training-clip production:** explicitly NOT Lyra's job — `deepfist/tools` (pseudo_label_capture.py pattern) windows, aligns, peak-normalizes and labels; a small new tool there will filter by sidecar tier. Keeps label policy next to the model.
- **int16 (not float) WAV:** deviation from spec §6.2, driven by the actual consumer (`wmr_dataset.py` divides by 32768 unconditionally); the sidecar's `peak` preserves scale.
- Segment length, debounce, cap are compile-time `Config` fields — prefs only if field use demands.

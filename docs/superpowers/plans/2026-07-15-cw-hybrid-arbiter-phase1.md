# CW Hybrid "Auto" Decoder — Phase 1 (Live Arbiter) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add "Auto" as a third selectable RX CW engine that runs the Classic (fldigi) and DeepFist (neural) decoders together and hands display ownership to whichever is trustworthy, so the pane never goes silent during a fade.

**Architecture:** A new engine-agnostic `CwArbiter` supervises the two existing (unmodified) decoders. It watches DeepFist's `keyingRatio` as a fade detector (asymmetric hysteresis) and forwards only the current owner's committed characters, switching owner only at a keying gap (a space). Fallback (Classic-during-fade) text is source-marked so the panel dims it. `WdspEngine` gains `cwEngine_ == 2` (Auto): the CW tap fans out to both decoders and their callbacks route into the arbiter.

**Tech Stack:** C++23, Qt 6 (QML), CMake/MSVC (VS 2022 vcvars64). No new dependencies.

## Global Constraints

- Build environment: **VS 2022 vcvars64** (a toolset-less VS 2026 shell fails with `type_traits` C1083). Helper: `build3.ps1`.
- The Classic decoder (`CwDecoder`) and DeepFist decoder (`NeuralCwDecoder`) internals MUST NOT change behaviorally — Classic-only (engine 0) and DeepFist-only (engine 1) output stay byte-identical. The only allowed edit to a decoder is additive (a new optional callback).
- `CwArbiter` is engine-agnostic: it may include only `<functional>`, `<mutex>`, `<string>` — no fldigi, ONNX, or Qt headers.
- `CwArbiter` is fed from two threads (Classic pushes on the audio thread, DeepFist on its worker thread) → all public methods take an internal mutex; `onOutput` is invoked outside the lock.
- New pure test target mirrors `test_rescore` / `test_cw_decoder`: `EXCLUDE_FROM_ALL`, `target_include_directories(... PRIVATE src)`, and under MSVC `target_compile_options(... PRIVATE /utf-8 /Zc:preprocessor)`.
- Fade-detector thresholds default to `tLow = 12.0` (== `NeuralCwDecoder`'s `kKeyingMin`), `tHigh = 20.0`, `nFall = 1`, `nRise = 3`. These are `CwArbiter::Config` fields, injectable for tests; final on-air tuning is deferred (spec §10).
- SPDX/header style: `CwArbiter.{h,cpp}` is Lyra core (not DeepFist) — use a plain descriptive header comment matching `src/dsp/CwDecoder.h`, **no** `SPDX-License-Identifier` line (that marker is reserved for the MIT-provenance DeepFist files).

---

### Task 1: `CwArbiter` unit + switch state machine (pure, TDD)

**Files:**
- Create: `src/dsp/CwArbiter.h`
- Create: `src/dsp/CwArbiter.cpp`
- Test: `scratch/test_cw_arbiter.cpp`
- Modify: `CMakeLists.txt` (add `test_cw_arbiter` target after the `test_rescore` block, ~line 1003)

**Interfaces:**
- Consumes: nothing (leaf unit).
- Produces (later tasks rely on these exact signatures):
  - `enum class CwArbiter::Source { DeepFist, Classic };`
  - `struct CwArbiter::Config { float tLow=12.0f; float tHigh=20.0f; int nFall=1; int nRise=3; };`
  - `explicit CwArbiter(Config cfg = {});`
  - `void updateKeying(float ratio);`
  - `void pushDeepFist(const std::string& text);`
  - `void pushClassic(const std::string& text);`
  - `void reset();`
  - `std::function<void(const std::string& text, bool fallback)> onOutput;`
  - `Source owner() const;`

- [ ] **Step 1: Write the failing test**

Create `scratch/test_cw_arbiter.cpp` (framework-free, mirrors `test_rescore` style — plain asserts + a pass/fail count, returns non-zero on failure):

```cpp
// Lyra — CwArbiter unit tests (Auto CW engine ownership/state machine).
// Qt-free, ONNX-free.  Build + run:
//   cmake --build build --target test_cw_arbiter && build/test_cw_arbiter
#include "dsp/CwArbiter.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using lyra::dsp::CwArbiter;

namespace {
int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

// Capture arbiter output as (text, fallback) pairs and the concatenated string.
struct Sink {
    std::vector<std::pair<std::string,bool>> runs;
    std::string text;
    void attach(CwArbiter& a) {
        a.onOutput = [this](const std::string& s, bool fb) {
            runs.push_back({s, fb}); text += s;
        };
    }
    bool anyFallback() const {
        for (auto& r : runs) if (r.second) return true;
        return false;
    }
};
}  // namespace

int main() {
    // 1) Solid signal: DeepFist owns, output is not fallback.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(50.0f);
        a.pushDeepFist("C"); a.pushDeepFist("Q");
        CHECK(a.owner() == CwArbiter::Source::DeepFist);
        CHECK(k.text == "CQ");
        CHECK(!k.anyFallback());
    }

    // 2) Fade: DeepFist gates (one space then silence), we fall back to Classic
    //    and keep producing text — never silent.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(50.0f); a.pushDeepFist("C");     // DeepFist solid, owns
        a.updateKeying(4.0f);                            // fade -> desired=Classic (nFall=1)
        a.pushDeepFist(" ");                             // DeepFist idle space -> switch here
        CHECK(a.owner() == CwArbiter::Source::Classic);
        a.pushDeepFist("X");                             // muted engine -> dropped
        a.pushClassic("W"); a.pushClassic("1");          // Classic drives, fallback
        CHECK(k.text == "C W1");
        CHECK(k.anyFallback());
        CHECK(k.runs.back() == std::make_pair(std::string("1"), true));
    }

    // 3) Recovery hysteresis: one solid tick is NOT enough (nRise=3); three are.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(4.0f); a.pushDeepFist(" ");       // in fade, Classic owns
        CHECK(a.owner() == CwArbiter::Source::Classic);
        a.updateKeying(50.0f);                           // 1 solid
        a.pushClassic(" ");                              // gap, but desired still Classic
        CHECK(a.owner() == CwArbiter::Source::Classic);
        a.updateKeying(50.0f); a.updateKeying(50.0f);    // 3 solid -> desired=DeepFist
        a.pushClassic(" ");                              // gap -> switch back
        CHECK(a.owner() == CwArbiter::Source::DeepFist);
    }

    // 4) Dead-band dither (tLow..tHigh) causes no ownership flip.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(50.0f);                           // DeepFist owns
        for (int i = 0; i < 20; ++i) a.updateKeying(15.0f); // between 12 and 20
        a.pushDeepFist(" ");
        CHECK(a.owner() == CwArbiter::Source::DeepFist);
    }

    // 5) No-dup / no-drop: only the owner's chars appear, exactly once, in order.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(50.0f);
        a.pushDeepFist("A"); a.pushClassic("z");         // z dropped (muted)
        a.updateKeying(4.0f); a.pushDeepFist(" ");       // fade -> switch to Classic
        a.pushClassic("B"); a.pushDeepFist("q");         // q dropped (muted)
        CHECK(k.text == "A B");
    }

    // 6) Switch only at a gap: a pending switch waits for a space.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(4.0f);                            // desired=Classic immediately
        a.pushDeepFist("N"); a.pushDeepFist("R");        // no gap yet -> DeepFist still owns
        CHECK(a.owner() == CwArbiter::Source::DeepFist);
        CHECK(k.text == "NR");
        a.pushDeepFist(" ");                             // gap -> now switch
        CHECK(a.owner() == CwArbiter::Source::Classic);
    }

    if (g_fail == 0) std::printf("test_cw_arbiter: ALL PASS\n");
    else             std::printf("test_cw_arbiter: %d CHECK(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
```

Add the CMake target at the end of `CMakeLists.txt`'s test section (immediately after the `test_rescore` block that ends at ~line 1003):

```cmake
# CW Auto arbiter — engine-agnostic ownership state machine unit test.
# Qt-free, no ONNX Runtime.  Build + run:
#   cmake --build build --target test_cw_arbiter && build/test_cw_arbiter
add_executable(test_cw_arbiter EXCLUDE_FROM_ALL
    scratch/test_cw_arbiter.cpp
    src/dsp/CwArbiter.h
    src/dsp/CwArbiter.cpp
)
target_include_directories(test_cw_arbiter PRIVATE src)
if(MSVC)
    target_compile_options(test_cw_arbiter PRIVATE /utf-8 /Zc:preprocessor)
endif()
```

- [ ] **Step 2: Run test to verify it fails**

Run (from a VS 2022 vcvars64 shell):
```
cmake --build build --target test_cw_arbiter
```
Expected: **FAIL to compile** — `src/dsp/CwArbiter.h` does not exist yet (`Cannot open include file: 'dsp/CwArbiter.h'`).

- [ ] **Step 3: Write minimal implementation**

Create `src/dsp/CwArbiter.h`:

```cpp
// Lyra — CW "Auto" arbiter.
//
// Supervises the two RX CW decoders (Classic fldigi + DeepFist neural) when the
// operator selects the "Auto" engine.  It is deliberately engine-agnostic: it
// sees only committed characters tagged by source and a scalar fade confidence
// (DeepFist's keyingRatio), and hands DISPLAY OWNERSHIP to exactly one engine at
// a time.  DeepFist is preferred (cleaner copy); Classic is the fade safety net.
//
// Only the owner's characters are forwarded (onOutput); the muted engine keeps
// running but its output is dropped.  Ownership flips only at a keying GAP (a
// space), so no character is ever cut mid-symbol and there is no de-duplication
// problem by construction.  Asymmetric hysteresis biases toward "never silent":
// fall back fast (nFall), return to DeepFist only when solidly recovered (nRise).
//
// Threading: fed from two threads (Classic on the audio thread, DeepFist on its
// worker thread) — every method takes mx_; onOutput fires OUTSIDE the lock.
#pragma once

#include <functional>
#include <mutex>
#include <string>

namespace lyra::dsp {

class CwArbiter {
public:
    enum class Source { DeepFist, Classic };

    struct Config {
        float tLow  = 12.0f;   // ratio below this => fading (== NeuralCwDecoder kKeyingMin)
        float tHigh = 20.0f;   // ratio above this => solidly recovered
        int   nFall = 1;       // consecutive sub-tLow ticks to fall back (fast)
        int   nRise = 3;       // consecutive above-tHigh ticks to return (slow)
    };

    explicit CwArbiter(Config cfg = {});

    void updateKeying(float ratio);            // fade-confidence feed (~2.5 Hz)
    void pushDeepFist(const std::string& text);
    void pushClassic(const std::string& text);
    void reset();                              // seeds owner = DeepFist

    std::function<void(const std::string& text, bool fallback)> onOutput;

    Source owner() const;

private:
    static bool isGap(const std::string& text); // a space == safe switch point

    Config             cfg_;
    mutable std::mutex mx_;
    Source             owner_      = Source::DeepFist;
    Source             desired_    = Source::DeepFist;
    int                fadeCount_  = 0;
    int                solidCount_ = 0;
};

}  // namespace lyra::dsp
```

Create `src/dsp/CwArbiter.cpp`:

```cpp
#include "dsp/CwArbiter.h"

namespace lyra::dsp {

CwArbiter::CwArbiter(Config cfg) : cfg_(cfg) {}

bool CwArbiter::isGap(const std::string& text) {
    return text.find(' ') != std::string::npos;
}

void CwArbiter::updateKeying(float ratio) {
    std::lock_guard<std::mutex> lk(mx_);
    if (ratio < cfg_.tLow) {
        solidCount_ = 0;
        if (++fadeCount_ >= cfg_.nFall) desired_ = Source::Classic;
    } else if (ratio > cfg_.tHigh) {
        fadeCount_ = 0;
        if (++solidCount_ >= cfg_.nRise) desired_ = Source::DeepFist;
    }
    // Dead-band (tLow..tHigh): hold counts and desired_ — no progress either way.
}

void CwArbiter::pushDeepFist(const std::string& text) {
    std::string out;
    bool fire = false;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (owner_ != Source::DeepFist) return;          // muted -> drop
        out = text; fire = true;
        if (isGap(text) && desired_ != owner_) owner_ = desired_;  // switch at gap
    }
    if (fire && onOutput) onOutput(out, /*fallback=*/false);
}

void CwArbiter::pushClassic(const std::string& text) {
    std::string out;
    bool fire = false;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (owner_ != Source::Classic) return;           // muted -> drop
        out = text; fire = true;
        if (isGap(text) && desired_ != owner_) owner_ = desired_;  // switch at gap
    }
    if (fire && onOutput) onOutput(out, /*fallback=*/true);
}

void CwArbiter::reset() {
    std::lock_guard<std::mutex> lk(mx_);
    owner_ = Source::DeepFist;
    desired_ = Source::DeepFist;
    fadeCount_ = 0;
    solidCount_ = 0;
}

CwArbiter::Source CwArbiter::owner() const {
    std::lock_guard<std::mutex> lk(mx_);
    return owner_;
}

}  // namespace lyra::dsp
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```
cmake --build build --target test_cw_arbiter && build/test_cw_arbiter
```
Expected: `test_cw_arbiter: ALL PASS` and exit code 0.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/CwArbiter.h src/dsp/CwArbiter.cpp scratch/test_cw_arbiter.cpp CMakeLists.txt
git commit -m "feat(cw): CwArbiter ownership state machine for Auto engine (Phase 1)"
```

---

### Task 2: Surface DeepFist's keying ratio via an `onKeying` callback

**Files:**
- Modify: `src/dsp/deepfist/NeuralCwDecoder.h` (add callback near the other `on*` callbacks, ~line 73)
- Modify: `src/dsp/deepfist/NeuralCwDecoder.cpp` (`workerLoop`, ~line 125)

**Interfaces:**
- Consumes: nothing.
- Produces: `std::function<void(float ratio)> NeuralCwDecoder::onKeying;` — fires once per worker tick (~2.5 Hz) with the freshly computed keying ratio, whether or not the gate opens. Wired into `CwArbiter::updateKeying` in Task 3.

- [ ] **Step 1: Add the callback declaration**

In `src/dsp/deepfist/NeuralCwDecoder.h`, immediately after the `onWpm` declaration (the block ending ~line 73), add:

```cpp
    // Fires from the worker thread once per decode tick with the current keying
    // ratio (p90/p10 of the locked tone's baseband envelope) — the same scalar
    // the gate uses.  Consumed by the Auto-engine arbiter as its fade detector;
    // leaving it null costs nothing.
    std::function<void(float ratio)> onKeying;
```

- [ ] **Step 2: Fire it each tick, before the gate branch**

In `src/dsp/deepfist/NeuralCwDecoder.cpp`, replace the gate test at the top of the keying section (currently line 125):

```cpp
        if (model_.keyingRatio(window.data(), kWindow) < kKeyingMin) {
```

with a version that computes the ratio once, publishes it, then branches:

```cpp
        const float keyRatio = model_.keyingRatio(window.data(), kWindow);
        if (onKeying) onKeying(keyRatio);
        if (keyRatio < kKeyingMin) {
```

(The rest of that `if` block — advancing `committedT_` and emitting the idle space — is unchanged.)

- [ ] **Step 3: Verify it builds and behaves unchanged in engines 0/1**

Run:
```
cmake --build build --target lyra
```
Expected: **PASS** (compiles/links). `onKeying` is null unless wired, so DeepFist-only decoding is behaviorally identical.

Optional live check (requires the model + a clip; skip if unavailable): temporarily set `dec.onKeying = [](float r){ std::printf("kr=%.1f\n", r); };` in `scratch/test_neural_cw_stream.cpp`, build `test_neural_cw_stream`, run it on a clip, and confirm a stream of `kr=` values that dips during a fade. Revert the temporary edit before committing.

- [ ] **Step 4: Commit**

```bash
git add src/dsp/deepfist/NeuralCwDecoder.h src/dsp/deepfist/NeuralCwDecoder.cpp
git commit -m "feat(cw): expose DeepFist keyingRatio via onKeying callback (Phase 1)"
```

---

### Task 3: Wire the Auto engine (`cwEngine_ == 2`) into `WdspEngine`

**Files:**
- Modify: `src/wdsp_engine.h` (include, member, signal, engine-comment; ~lines 43-44, 798, 1233-1235)
- Modify: `src/wdsp_engine.cpp` (callback wiring ~319-339; `setCwDecodeEngine` ~1956-1988; tap routing ~3683-3689)

**Interfaces:**
- Consumes: `CwArbiter` (Task 1); `NeuralCwDecoder::onKeying` (Task 2); existing `cwDecoder_.onText`, `neuralCw_.onText`.
- Produces: `void WdspEngine::cwAutoText(QString text, bool fallback);` — the unified Auto transcript signal (queued to the GUI). `setCwDecodeEngine(2)` selects Auto (lazy-loads the neural model exactly like engine 1).

- [ ] **Step 1: Header — include, member, signal**

In `src/wdsp_engine.h`, after the `NeuralCwDecoder.h` include (line 44) add:

```cpp
#include "dsp/CwArbiter.h"          // Auto-engine ownership arbiter (Phase 1)
```

Change the engine comment + member block (lines 1233-1235) from:

```cpp
    // cwEngine_: 0 = Classic (fldigi), 1 = Neural (DeepFist).
    lyra::dsp::NeuralCwDecoder           neuralCw_;
    std::atomic<int>                     cwEngine_{0};
```

to:

```cpp
    // cwEngine_: 0 = Classic (fldigi), 1 = Neural (DeepFist), 2 = Auto (arbiter).
    lyra::dsp::NeuralCwDecoder           neuralCw_;
    lyra::dsp::CwArbiter                 cwArbiter_;   // Auto: owns display handoff
    std::atomic<int>                     cwEngine_{0};
```

After the `cwNeuralText` signal declaration (line 798) add:

```cpp
    // Auto engine — unified arbiter output; fallback == true when the Classic
    // safety net produced it (panel dims that run).
    void cwAutoText(QString text, bool fallback);
```

- [ ] **Step 2: `.cpp` — route callbacks into the arbiter in Auto mode**

In `src/wdsp_engine.cpp`, change the Classic `onText` lambda (lines 319-322) from:

```cpp
    cwDecoder_.onText = [this](const std::string& s) {
        emit cwDecodedChar(QString::fromUtf8(s.c_str(),
                                             static_cast<int>(s.size())), 1.0);
    };
```

to:

```cpp
    cwDecoder_.onText = [this](const std::string& s) {
        if (cwEngine_.load(std::memory_order_relaxed) == 2) {
            cwArbiter_.pushClassic(s);
            return;
        }
        emit cwDecodedChar(QString::fromUtf8(s.c_str(),
                                             static_cast<int>(s.size())), 1.0);
    };
```

Change the Neural `onText` lambda (lines 331-334) from:

```cpp
    neuralCw_.onText = [this](const std::string& s) {
        emit cwNeuralText(QString::fromUtf8(s.c_str(),
                                            static_cast<int>(s.size())));
    };
```

to:

```cpp
    neuralCw_.onText = [this](const std::string& s) {
        if (cwEngine_.load(std::memory_order_relaxed) == 2) {
            cwArbiter_.pushDeepFist(s);
            return;
        }
        emit cwNeuralText(QString::fromUtf8(s.c_str(),
                                            static_cast<int>(s.size())));
    };
```

Immediately after the Neural `onWpm` lambda (line 339) add the keying feed and the arbiter output wiring:

```cpp
    // Auto engine: DeepFist's keying ratio drives the arbiter's fade detector,
    // and the arbiter's unified output becomes the Auto transcript (queued to
    // the GUI thread; fallback flag dims Classic-during-fade runs in the panel).
    neuralCw_.onKeying = [this](float r) {
        if (cwEngine_.load(std::memory_order_relaxed) == 2)
            cwArbiter_.updateKeying(r);
    };
    cwArbiter_.onOutput = [this](const std::string& s, bool fallback) {
        emit cwAutoText(QString::fromUtf8(s.c_str(),
                                          static_cast<int>(s.size())), fallback);
    };
```

- [ ] **Step 3: `.cpp` — `setCwDecodeEngine` accepts engine 2**

Replace the body of `setCwDecodeEngine` (lines 1956-1988) with:

```cpp
void WdspEngine::setCwDecodeEngine(int engine)
{
    const int cur = cwEngine_.load(std::memory_order_relaxed);
    if (engine == cur) return;

    if (engine == 1 || engine == 2) {           // both need the neural model
        if (!neuralCw_.ready()) {
            // Resolve the model dir: env override, then <exeDir>/models.
            QString dir = qEnvironmentVariable("DEEPFIST_MODEL_DIR");
            if (dir.isEmpty() ||
                !QFileInfo::exists(dir + "/deepfist.onnx")) {
                dir = QCoreApplication::applicationDirPath() + "/models";
            }
            const bool ok = neuralCw_.loadModel(dir.toStdString());
            emit cwNeuralAvailableChanged();
            if (!ok) {
                qWarning("DeepFist: neural CW model not loaded (%s) — staying on "
                         "the classic decoder.  %s",
                         qUtf8Printable(dir), neuralCw_.lastError().c_str());
                return;   // keep current engine
            }
            qInfo("DeepFist: neural CW model loaded from %s (SCP rescorer: %d calls, "
                  "blank_pen %.1f)",
                  qUtf8Printable(dir), neuralCw_.scpCount(), neuralCw_.blankPenalty());
        }
        // Reset every consumer BEFORE storing the new engine, so the audio tap
        // (which only feeds them once cwEngine_ flips) never sees stale state.
        neuralCw_.reset();
        if (engine == 2) {
            cwDecoder_.reset();
            cwArbiter_.reset();
        }
        cwEngine_.store(engine, std::memory_order_relaxed);
    } else {
        cwEngine_.store(0, std::memory_order_relaxed);
        cwDecoder_.reset();
    }
    emit cwDecodeEngineChanged();
}
```

- [ ] **Step 4: `.cpp` — fan the tap out to both decoders in Auto mode**

Replace the tap routing (lines 3683-3689) from:

```cpp
        // Route to the selected engine (only one runs).  Engine is switched to
        // Neural only after its model is fully loaded (see setCwDecodeEngine),
        // so neuralCw_ is safe to call here.
        if (cwEngine_.load(std::memory_order_relaxed) == 1)
            neuralCw_.process(cwMonoBuf_.data(), nframes);
        else
            cwDecoder_.process(cwMonoBuf_.data(), nframes);
```

to:

```cpp
        // Route to the selected engine.  Neural is only reachable after its model
        // is fully loaded (see setCwDecodeEngine), so it is safe to call here.
        // Auto (2) fans out to BOTH; the arbiter picks who drives the display.
        const int eng = cwEngine_.load(std::memory_order_relaxed);
        if (eng == 2) {
            cwDecoder_.process(cwMonoBuf_.data(), nframes);
            neuralCw_.process(cwMonoBuf_.data(), nframes);
        } else if (eng == 1) {
            neuralCw_.process(cwMonoBuf_.data(), nframes);
        } else {
            cwDecoder_.process(cwMonoBuf_.data(), nframes);
        }
```

- [ ] **Step 5: Build and verify engines 0/1 are unchanged**

Run:
```
cmake --build build --target lyra
```
Expected: **PASS**. No behavioral change for engines 0/1 (the `== 2` branches are inert until Auto is selected). Full on-air verification of Auto happens after Task 4.

- [ ] **Step 6: Commit**

```bash
git add src/wdsp_engine.h src/wdsp_engine.cpp
git commit -m "feat(cw): Auto engine (cwEngine_=2) fans out to both decoders via CwArbiter"
```

---

### Task 4: QML — "Auto" selector + subtle source-marked rendering

**Files:**
- Modify: `src/qml/CwDecoderPanel.qml` (Connections ~131-139; `displayHtml` ~73-82; `appendDecoded`/helpers ~41-46; engine selector ~248-267)

**Interfaces:**
- Consumes: `WdspEngine.cwAutoText(text, fallback)` (Task 3); existing `appendDecoded`, `displayHtml`.
- Produces: no downstream consumers (UI leaf).

- [ ] **Step 1: Add the Auto append helper**

In `src/qml/CwDecoderPanel.qml`, after `appendDecoded`/`clearDecoded` (line 46) add:

```qml
    // Auto engine: fallback (Classic-during-fade) runs are wrapped in private
    // control-char markers (U+0002 / U+0003) so displayHtml() can dim them.
    // (Non-empty fallback text only — a bare gap space needs no marking.)
    function appendAuto(s, fallback) {
        if (fallback && s.trim().length > 0)
            appendDecoded("\u0002" + s + "\u0003")
        else
            appendDecoded(s)
    }
```

- [ ] **Step 2: Handle the Auto signal**

In the `Connections { target: WdspEngine ... }` block, after the `onCwNeuralText` handler (line 137) add:

```qml
        // Auto engine: arbiter output; fallback runs are source-marked (dimmed).
        function onCwAutoText(text, fallback) { root.appendAuto(text, fallback) }
```

- [ ] **Step 3: Render the markers as a subtle opacity span**

Replace `displayHtml` (lines 73-82) with a version that converts the fallback markers to an opacity span after escaping + spot-coloring:

```qml
    function displayHtml(t) {
        var rev = root.spotRev   // create a binding dependency on spot updates
        var esc = t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        var html = esc.replace(/[A-Z0-9\/]{3,}/g, function(w) {
            // only bother the spot store with call-shaped tokens (contain a digit)
            if (/[0-9]/.test(w) && Spots.isSpotted(w))
                return '<span style="color:#5fffa8; font-weight:bold">' + w + '</span>'
            return w
        })
        // Auto-engine fallback markers -> subtle dim (lower-confidence cue).
        html = html.replace(/\u0002/g, '<span style="opacity:0.68">')
                   .replace(/\u0003/g, '</span>')
        return html
    }
```

(The control-char markers survive HTML escaping and never match the `[A-Z0-9/]` spot regex, so RBN highlighting still works inside a fallback run. A marker pair split by `appendDecoded`'s 6000-char trim degrades to a self-closing span at block end — harmless.)

- [ ] **Step 4: Add the "Auto" engine chip**

In the engine selector `RowLayout`, after the DeepFist `ChipButton` (closes at line 261) add:

```qml
            ChipButton {
                label: qsTr("Auto")
                lit: root.cwEngine === 2
                onClicked: { WdspEngine.cwDecodeEngine = 2
                             // Persist only if it actually engaged (model loaded).
                             if (WdspEngine.cwDecodeEngine === 2)
                                 Prefs.cwDecodeEngine = 2 }
            }
```

Update the "model not found" warning label (line 263) so it also shows for Auto — change:

```qml
                visible: root.cwEngine === 1 && !WdspEngine.cwNeuralAvailable
```

to:

```qml
                visible: (root.cwEngine === 1 || root.cwEngine === 2) && !WdspEngine.cwNeuralAvailable
```

- [ ] **Step 5: Build, then verify on-air**

Run:
```
cmake --build build --target lyra
```
Expected: **PASS**.

Manual verification (RX only; talks to the HL2, no TX at idle):
1. Launch `build/lyra.exe`, enter a CW mode (CWU/CWL), enable the decoder.
2. Selector shows **Classic | DeepFist | Auto**; each is selectable and `lit` tracks selection; the transcript clears on each switch.
3. In **Auto** on a solid signal, copy reads at full brightness (DeepFist driving).
4. On a fading signal, copy keeps flowing (never blank) and the fallback stretch renders subtly dimmed (~68% opacity), then returns to full brightness when the signal recovers.
5. An RBN-spotted call still highlights green whether it landed in a primary or fallback run.

- [ ] **Step 6: Commit**

```bash
git add src/qml/CwDecoderPanel.qml
git commit -m "feat(cw): Auto engine selector + subtle source-marked fallback text"
```

---

## Golden regression (optional, fixture-gated)

The spec (§7) names `cw_bt_debug.wav` — the real off-air fade that clamped the gate during the original "DeepFist shows nothing" debug — as the acceptance fixture. It is a private capture, not committed. If it is available locally, extend `scratch/test_neural_cw_stream.cpp` (or a new `test_cw_auto_stream`) to drive both decoders + a `CwArbiter` through that clip and assert: DeepFist drives the clean spans, Classic backfills the fade, output is never empty across the fade, and `fallback` toggles at the fade edges. This depends on the model + WAV being present, so it stays out of the always-on target set — the Task 1 unit tests are the hard gate.

## Notes / deferrals

- **Prefs range:** `cwDecodeEngine` is already an `int`; value `2` persists with no schema change. `Component.onCompleted` currently restores only engine `1` (line 119) — leave restore-of-Auto out of Phase 1 unless desired; add an `=== 2` restore branch mirroring the `=== 1` one if you want Auto to survive a restart.
- Phases 2 (harvest) and 3 (RBN→SCP loop) are separate plans per the spec.

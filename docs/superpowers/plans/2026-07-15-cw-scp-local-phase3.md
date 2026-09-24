# CW RBN→SCP Local Loop — Phase 3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** RBN-confirmed callsigns heard at this station accumulate in a local `scp_local.txt` that merges into the SCP candidate space — the rescorer and the amber call highlight get smarter from the operator's own copy.

**Architecture:** A new Qt-free `ScpLocal` store (call → last-heard epoch; capped, age-out, atomic rewrite) owned by `WdspEngine`. The QML decode pane (GUI thread, where both the text stream and `Spots.isSpotted` already live) taps completed words and notes RBN-confirmed ones via one Q_INVOKABLE. At neural-model load, the local list merges into `DeepFistScp` so the CTC rescorer's candidates include locally-heard calls; the amber highlight (`cwCallKnown`) ORs the live in-memory set immediately.

**Tech Stack:** C++23, Qt 6 (QML), CMake/MSVC (VS 2022 vcvars64). No new dependencies.

## Global Constraints

- Build environment: **VS 2022 vcvars64** (toolset-less VS 2026 fails with `type_traits` C1083). Helper: `C:\dev\Lyra-SDR-cpp\build3.ps1` (PowerShell, builds everything). Per-target from Bash fails on quoting for some shells — the helper is the reliable gate.
- `ScpLocal` is Qt-free: only `<cstdio>`, `<map>`, `<string>`, `<vector>`, `<algorithm>` — path comes in as a `std::string` parameter; no Qt headers in `src/dsp/`.
- Spec §6.6 privacy: `scp_local.txt` is a record of received activity → the feature is **opt-in, default OFF** (`Prefs.cwLearnCalls`, default `false`).
- Defaults (spec §6.4 "size-capped and age-out'd"): `cap = 10000` calls, `maxAgeDays = 365`. Not operator-tunable in this phase (YAGNI).
- Thread rule: `DeepFistScp::addCalls` and `ScpLocal` are **not** thread-safe — `addCalls` runs only inside `loadModel` before `startWorker()`; `ScpLocal` is touched only from the GUI thread (Q_INVOKABLEs + `setCwDecodeEngine`).
- Existing decode behavior must not change when `cwLearnCalls` is off (the default): no file is created, no lookups added to the render path beyond what exists.
- **Escape-mangling hazard (learned in Phase 1):** `src/qml/CwDecoderPanel.qml` contains literal 6-char ``/`` escape sequences (the Auto dim markers). Tooling (Edit-tool JSON, perl) silently corrupts typed `\uXXXX` sequences. NEVER retype or move those lines; anchor edits away from them; the new word-tap code uses **`charCodeAt` numeric comparisons (2/3)** specifically so no new escape literals are needed. After any QML edit run: `LC_ALL=C grep -c $'\x02\|\x03' src/qml/CwDecoderPanel.qml` → must print `0`.
- Header style: `ScpLocal.{h,cpp}` lives in `src/dsp/deepfist/` → carries the same two-line SPDX header as its siblings: `// SPDX-License-Identifier: MIT` + `// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md`.

---

### Task 1: `ScpLocal` store (pure, TDD)

**Files:**
- Create: `src/dsp/deepfist/ScpLocal.h`
- Create: `src/dsp/deepfist/ScpLocal.cpp`
- Test: `scratch/test_scp_local.cpp`
- Modify: `CMakeLists.txt` (add `test_scp_local` target after the `test_cw_arbiter` block)

**Interfaces:**
- Consumes: nothing (leaf unit).
- Produces (later tasks rely on these exact signatures):
  - `int ScpLocal::load(const std::string& path, int maxAgeDays = 365, int cap = 10000);` — returns count kept
  - `bool ScpLocal::note(const std::string& call, long long nowEpochSec);` — records/refreshes; saves the file when it changed; returns whether it changed
  - `bool ScpLocal::contains(const std::string& call) const;`
  - `std::vector<std::string> ScpLocal::calls() const;` — sorted, for the SCP merge
  - `int ScpLocal::count() const;`

- [ ] **Step 1: Write the failing test**

Create `scratch/test_scp_local.cpp` (framework-free, mirrors `test_cw_arbiter` style):

```cpp
// Lyra — ScpLocal unit tests (RBN-confirmed local callsign list, Phase 3).
// Qt-free.  Build + run:
//   cmake --build build --target test_scp_local && build/test_scp_local
#include "dsp/deepfist/ScpLocal.h"

#include <cstdio>
#include <string>

using lyra::dsp::ScpLocal;

namespace {
int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

const char* kPath = "test_scp_local_tmp.txt";
const long long kDay = 86400;
}  // namespace

int main() {
    std::remove(kPath);
    const long long now = 1800000000;   // fixed epoch: deterministic tests

    // 1) Fresh store: empty, note() adds, contains() sees it, count grows.
    {
        ScpLocal db;
        CHECK(db.load(kPath) == 0);
        CHECK(!db.contains("NA2DX"));
        CHECK(db.note("NA2DX", now));           // new -> changed
        CHECK(db.contains("NA2DX"));
        CHECK(db.count() == 1);
        CHECK(!db.note("NA2DX", now));          // same call same day -> no churn
        db.note("K7CO", now);
        CHECK(db.count() == 2);
    }

    // 2) Persistence round-trip: a second instance reads what the first wrote.
    {
        ScpLocal db;
        CHECK(db.load(kPath) == 2);
        CHECK(db.contains("NA2DX"));
        CHECK(db.contains("K7CO"));
        // calls() is sorted for the SCP merge.
        auto v = db.calls();
        CHECK(v.size() == 2 && v[0] == "K7CO" && v[1] == "NA2DX");
    }

    // 3) Timestamp refresh: noting an existing call >1 day later persists the
    //    newer time (protects it from age-out), and reports changed.
    {
        ScpLocal db;
        db.load(kPath);
        CHECK(db.note("K7CO", now + 2 * kDay));  // refresh -> changed
    }

    // 4) Age-out on load: entries older than maxAgeDays are dropped.
    {
        ScpLocal db;
        // NA2DX stamped `now`, K7CO stamped now+2d.  Load "1 day" after K7CO's
        // stamp with maxAgeDays=1: NA2DX (3 days old) ages out, K7CO survives.
        // load() measures age against the NEWEST entry (no wall clock in tests).
        CHECK(db.load(kPath, /*maxAgeDays=*/1) == 1);
        CHECK(db.contains("K7CO"));
        CHECK(!db.contains("NA2DX"));
    }

    // 5) Cap eviction: oldest timestamps evicted first.
    {
        std::remove(kPath);
        ScpLocal db;
        db.load(kPath, 365, /*cap=*/3);
        db.note("A1AA", now + 1);
        db.note("B2BB", now + 2);
        db.note("C3CC", now + 3);
        db.note("D4DD", now + 4);               // overflows cap=3 -> evict A1AA
        CHECK(db.count() == 3);
        CHECK(!db.contains("A1AA"));
        CHECK(db.contains("D4DD"));
    }

    // 6) Garbage tolerance: malformed lines are skipped, not fatal.
    {
        std::remove(kPath);
        FILE* f = std::fopen(kPath, "w");
        std::fputs("NA2DX\t1800000000\n"
                   "not a valid line\n"
                   "\n"
                   "K7CO\t1800000001\n", f);
        std::fclose(f);
        ScpLocal db;
        CHECK(db.load(kPath) == 2);
    }

    std::remove(kPath);
    if (g_fail == 0) std::printf("test_scp_local: ALL PASS\n");
    else             std::printf("test_scp_local: %d CHECK(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
```

Add the CMake target immediately after the `test_cw_arbiter` block:

```cmake
# Phase 3 — RBN-confirmed local callsign list unit test.  Qt-free.
#   cmake --build build --target test_scp_local && build/test_scp_local
add_executable(test_scp_local EXCLUDE_FROM_ALL
    scratch/test_scp_local.cpp
    src/dsp/deepfist/ScpLocal.h
    src/dsp/deepfist/ScpLocal.cpp
)
target_include_directories(test_scp_local PRIVATE src)
if(MSVC)
    target_compile_options(test_scp_local PRIVATE /utf-8 /Zc:preprocessor)
endif()
```

- [ ] **Step 2: Run test to verify it fails**

Build via the helper (PowerShell): `& C:\dev\Lyra-SDR-cpp\build3.ps1` will fail on the missing files — OR build just the target from a vcvars64 shell. Expected: **FAIL to compile** (`ScpLocal.h` not found).

- [ ] **Step 3: Write minimal implementation**

Create `src/dsp/deepfist/ScpLocal.h`:

```cpp
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md
//
// Lyra — Phase 3 RBN→SCP loop: the local list of callsigns actually copied AND
// RBN/cluster-confirmed at this station.  Persisted as "CALL<TAB>epoch" lines;
// merged into DeepFistScp at model load (rescorer candidates) and consulted
// live by the CW panel's amber call highlight (WdspEngine::cwCallKnown).
//
// Deliberately tiny: a call->last-heard map with a size cap (oldest evicted),
// an age-out at load, and an atomic-ish tmp+rename rewrite on change.  spec
// §6.4/§6.6 — opt-in feature, capped, aged, local-only.
//
// NOT thread-safe: GUI-thread only (plus a read-only calls() snapshot handed
// to DeepFistScp::addCalls before the decode worker starts).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace lyra::dsp {

class ScpLocal {
public:
    // Read `path` (missing file = empty store, not an error), drop entries
    // older than maxAgeDays (measured against the newest entry, so tests and
    // long-idle rigs behave identically), enforce the cap.  Returns the count.
    int load(const std::string& path, int maxAgeDays = 365, int cap = 10000);

    // Record `call` (upper-case) heard-and-confirmed at unix time `now`.
    // New call, or an existing one >1 day stale, persists to disk and returns
    // true; a same-day repeat is a no-op (no file churn at contest rates).
    bool note(const std::string& call, long long nowEpochSec);

    bool contains(const std::string& call) const {
        return byCall_.find(call) != byCall_.end();
    }
    std::vector<std::string> calls() const;      // sorted (map order)
    int count() const { return static_cast<int>(byCall_.size()); }

private:
    void enforceCap();
    void save() const;

    std::string                     path_;
    int                             cap_ = 10000;
    std::map<std::string, long long> byCall_;    // call -> last-heard epoch
};

}  // namespace lyra::dsp
```

Create `src/dsp/deepfist/ScpLocal.cpp`:

```cpp
// SPDX-License-Identifier: MIT
// Copyright (C) 2026 Brent Crier (N9BC) - part of Lyra (GPLv3+) per NOTICE.md

#include "dsp/deepfist/ScpLocal.h"

#include <algorithm>
#include <cstdio>

namespace lyra::dsp {

namespace {
constexpr long long kRefreshSec = 86400;   // persist a timestamp refresh daily
}

int ScpLocal::load(const std::string& path, int maxAgeDays, int cap) {
    path_ = path;
    cap_  = cap;
    byCall_.clear();

    FILE* f = std::fopen(path.c_str(), "rb");
    if (f) {
        char line[256];
        long long newest = 0;
        while (std::fgets(line, sizeof line, f)) {
            std::string s(line);
            const auto tab = s.find('\t');
            if (tab == std::string::npos || tab == 0) continue;
            const std::string call = s.substr(0, tab);
            long long t = 0;
            try { t = std::stoll(s.substr(tab + 1)); } catch (...) { continue; }
            if (t <= 0) continue;
            byCall_[call] = std::max(byCall_[call], t);
            newest = std::max(newest, t);
        }
        std::fclose(f);

        // Age-out relative to the newest entry: no wall clock -> deterministic
        // in tests, and a rig that sat powered off for a year doesn't wake up
        // to an empty list.
        const long long cutoff = newest - static_cast<long long>(maxAgeDays) * 86400;
        for (auto it = byCall_.begin(); it != byCall_.end();)
            it = (it->second < cutoff) ? byCall_.erase(it) : std::next(it);
        enforceCap();
    }
    return count();
}

bool ScpLocal::note(const std::string& call, long long nowEpochSec) {
    if (call.empty()) return false;
    auto it = byCall_.find(call);
    if (it != byCall_.end() && nowEpochSec - it->second < kRefreshSec)
        return false;                          // same-day repeat: no churn
    byCall_[call] = nowEpochSec;
    enforceCap();
    save();
    return true;
}

std::vector<std::string> ScpLocal::calls() const {
    std::vector<std::string> out;
    out.reserve(byCall_.size());
    for (const auto& [c, t] : byCall_) out.push_back(c);
    return out;                                // map iteration = sorted
}

void ScpLocal::enforceCap() {
    while (static_cast<int>(byCall_.size()) > cap_) {
        auto oldest = byCall_.begin();
        for (auto it = byCall_.begin(); it != byCall_.end(); ++it)
            if (it->second < oldest->second) oldest = it;
        byCall_.erase(oldest);
    }
}

void ScpLocal::save() const {
    if (path_.empty()) return;
    const std::string tmp = path_ + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;                            // read-only dir: degrade silently
    for (const auto& [c, t] : byCall_)
        std::fprintf(f, "%s\t%lld\n", c.c_str(), t);
    std::fclose(f);
    std::remove(path_.c_str());                // Windows rename needs the target gone
    std::rename(tmp.c_str(), path_.c_str());
}

}  // namespace lyra::dsp
```

- [ ] **Step 4: Run test to verify it passes**

From a vcvars64 environment build `test_scp_local`, run `build/test_scp_local.exe`. Expected: `test_scp_local: ALL PASS`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/deepfist/ScpLocal.h src/dsp/deepfist/ScpLocal.cpp scratch/test_scp_local.cpp CMakeLists.txt
git commit -m "feat(cw): ScpLocal store for RBN-confirmed local callsigns (Phase 3)"
```

---

### Task 2: SCP merge — `DeepFistScp::addCalls` + `NeuralCwDecoder::loadModel` extra-calls parameter

**Files:**
- Modify: `src/dsp/deepfist/DeepFistScp.h` (declare `addCalls` after `contains`)
- Modify: `src/dsp/deepfist/DeepFistScp.cpp` (implement)
- Modify: `src/dsp/deepfist/NeuralCwDecoder.h` (`loadModel` signature)
- Modify: `src/dsp/deepfist/NeuralCwDecoder.cpp` (merge before `startWorker`)
- Test: `scratch/test_scp_local.cpp` (append merge case) + `CMakeLists.txt` (add DeepFistScp sources to the `test_scp_local` target)

**Interfaces:**
- Consumes: `ScpLocal::calls()` (Task 1).
- Produces:
  - `void DeepFistScp::addCalls(const std::vector<std::string>& extra);` — merge, sort, dedup; NOT thread-safe, call before the worker starts
  - `bool NeuralCwDecoder::loadModel(const std::string& modelDir, const std::vector<std::string>& extraScpCalls = {});` — same behavior as today when the vector is empty

- [ ] **Step 1: Extend the test (failing)**

In `scratch/test_scp_local.cpp`, add `#include "dsp/deepfist/DeepFistScp.h"` under the existing include, and this case before the summary printf:

```cpp
    // 7) DeepFistScp::addCalls merges local calls into the candidate space:
    //    contains() sees them, candidates() can return them, dedup holds.
    {
        lyra::dsp::DeepFistScp scp;               // empty (no MASTER.SCP here)
        CHECK(!scp.contains("NA2DX"));
        scp.addCalls({"NA2DX", "K7CO", "NA2DX"}); // dup collapses
        CHECK(scp.count() == 2);
        CHECK(scp.contains("NA2DX"));
        auto cands = scp.candidates("NA2DX", 1, 5);
        bool found = false;
        for (const auto& c : cands) if (c == "NA2DX") found = true;
        CHECK(found);
    }
```

In `CMakeLists.txt`, add to the `test_scp_local` target's sources:

```cmake
    src/dsp/deepfist/DeepFistScp.h
    src/dsp/deepfist/DeepFistScp.cpp
```

Build the target: expected **compile failure** — `addCalls` does not exist.

- [ ] **Step 2: Implement `addCalls`**

`src/dsp/deepfist/DeepFistScp.h`, after the `contains` method:

```cpp
    // Merge extra calls (upper-case) into the database — Phase 3 scp_local
    // feed.  Sorts + dedups so contains()/candidates() stay correct.  NOT
    // thread-safe: call before the decode worker starts (loadModel path).
    void addCalls(const std::vector<std::string>& extra);
```

`src/dsp/deepfist/DeepFistScp.cpp` (with the other method bodies):

```cpp
void DeepFistScp::addCalls(const std::vector<std::string>& extra) {
    if (extra.empty()) return;
    calls_.insert(calls_.end(), extra.begin(), extra.end());
    std::sort(calls_.begin(), calls_.end());
    calls_.erase(std::unique(calls_.begin(), calls_.end()), calls_.end());
}
```

(If the .cpp lacks `<algorithm>`, the header now provides it via DeepFistScp.h's include; verify it compiles.)

- [ ] **Step 3: Thread the extra-calls parameter through `loadModel`**

`src/dsp/deepfist/NeuralCwDecoder.h` — change the declaration and its comment:

```cpp
    // Load the model dir (deepfist.onnx + sidecar); starts the worker on
    // success.  extraScpCalls (Phase 3 scp_local) merge into the callsign DB
    // before the worker starts.  Returns ready().
    bool loadModel(const std::string& modelDir,
                   const std::vector<std::string>& extraScpCalls = {});
```

`src/dsp/deepfist/NeuralCwDecoder.cpp` — the body becomes:

```cpp
bool NeuralCwDecoder::loadModel(const std::string& modelDir,
                                const std::vector<std::string>& extraScpCalls) {
    const bool ok = model_.load(modelDir);
    if (ok) {
        // Optional callsign DB for the lattice rescorer; a no-op if absent.
        scp_.loadFile(modelDir + "/MASTER.SCP");
        scp_.addCalls(extraScpCalls);      // Phase 3: locally-confirmed calls
        startWorker();
    }
    return ok;
}
```

- [ ] **Step 4: Build + run the test**

Build `test_scp_local` (now includes DeepFistScp) and run: expected `ALL PASS` (7 cases). Then build the full app via `build3.ps1` (the default-argument change must not break the existing `loadModel(dir)` call in wdsp_engine.cpp): expected **BUILD OK**.

- [ ] **Step 5: Commit**

```bash
git add src/dsp/deepfist/DeepFistScp.h src/dsp/deepfist/DeepFistScp.cpp src/dsp/deepfist/NeuralCwDecoder.h src/dsp/deepfist/NeuralCwDecoder.cpp scratch/test_scp_local.cpp CMakeLists.txt
git commit -m "feat(cw): merge scp_local calls into the SCP candidate space at model load"
```

---

### Task 3: `WdspEngine` ownership + `Prefs.cwLearnCalls` (+ engine-pref clamp bugfix)

**Files:**
- Modify: `src/wdsp_engine.h` (include, members, Q_INVOKABLE, `cwCallKnown` OR)
- Modify: `src/wdsp_engine.cpp` (ensureScpLocal, note, loadModel call site)
- Modify: `src/prefs.h`, `src/prefs.cpp` (new bool pref + **clamp fix**)

**Interfaces:**
- Consumes: `ScpLocal` (Task 1); `NeuralCwDecoder::loadModel(dir, extraCalls)` (Task 2).
- Produces (Task 4 relies on):
  - `Q_INVOKABLE void WdspEngine::cwNoteConfirmedCall(const QString& call);` — GUI thread; persists an RBN-confirmed heard call
  - `WdspEngine::cwCallKnown` now also true for locally-noted calls (immediate, same session)
  - `Prefs.cwLearnCalls` (bool Q_PROPERTY, default false, persisted)

- [ ] **Step 1: `wdsp_engine.h`**

Add the include next to the CwArbiter one:

```cpp
#include "dsp/deepfist/ScpLocal.h"  // Phase 3: RBN-confirmed local call list
```

Add members next to `cwArbiter_`:

```cpp
    lyra::dsp::ScpLocal                  cwScpLocal_;       // Phase 3 local calls
    bool                                 cwScpLocalLoaded_ = false;
```

Add a private method declaration (near the other private helpers):

```cpp
    void ensureCwScpLocal();    // lazy load of scp_local.txt (GUI thread)
```

Replace the `cwCallKnown` body:

```cpp
    // CW panel — is this decoded token a KNOWN-REAL callsign?  Exact
    // MASTER.SCP membership (loaded with the neural model), OR'd with the
    // Phase-3 local list of RBN-confirmed calls heard at this station (live —
    // a call noted this session ambers immediately; the rescorer picks it up
    // at the next model load).
    Q_INVOKABLE bool cwCallKnown(const QString& call) {
        const std::string u = call.trimmed().toUpper().toStdString();
        if (neuralCw_.scpKnows(u)) return true;
        ensureCwScpLocal();
        return cwScpLocal_.contains(u);
    }

    // CW panel — remember an RBN/cluster-confirmed call copied off the air
    // (Prefs.cwLearnCalls gate lives in QML; this always records).  GUI
    // thread only.
    Q_INVOKABLE void cwNoteConfirmedCall(const QString& call);
```

(Note: `cwCallKnown` loses its `const` — it lazy-loads. Q_INVOKABLE does not require const.)

- [ ] **Step 2: `wdsp_engine.cpp`**

Add near the other includes if not present: `#include <QStandardPaths>` and `#include <QDir>` and `#include <QDateTime>` (check first — the file may already include them).

Add the two method bodies (place near `setCwDecodeEngine`):

```cpp
// Phase 3 — lazy-load the local RBN-confirmed call list from AppData (created
// on first note).  GUI thread only; cheap after the first call.
void WdspEngine::ensureCwScpLocal()
{
    if (cwScpLocalLoaded_) return;
    cwScpLocalLoaded_ = true;
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    cwScpLocal_.load((dir + QStringLiteral("/scp_local.txt")).toStdString());
}

void WdspEngine::cwNoteConfirmedCall(const QString& call)
{
    ensureCwScpLocal();
    cwScpLocal_.note(call.trimmed().toUpper().toStdString(),
                     QDateTime::currentSecsSinceEpoch());
}
```

In `setCwDecodeEngine`, change the model-load call to pass the local calls:

```cpp
            ensureCwScpLocal();
            const bool ok = neuralCw_.loadModel(dir.toStdString(),
                                                cwScpLocal_.calls());
```

(The surrounding lazy-load block is otherwise unchanged.)

- [ ] **Step 3: `Prefs` — new bool + clamp bugfix**

`src/prefs.h` — add alongside the other cwDecode Q_PROPERTYs (~line 148):

```cpp
    // Phase 3 — opt-in: remember RBN-confirmed calls copied off the air in a
    // local SCP list (privacy: a record of received activity, default off).
    Q_PROPERTY(bool cwLearnCalls READ cwLearnCalls WRITE setCwLearnCalls
               NOTIFY cwLearnCallsChanged)
```

Accessors (near `cwDecodeTracking`):

```cpp
    bool    cwLearnCalls() const { return cwLearnCalls_; }
    void    setCwLearnCalls(bool on);
```

Signal: `void cwLearnCallsChanged();` — member: `bool cwLearnCalls_;`

`src/prefs.cpp`:
- Key constant next to the other kCwDec* keys: `constexpr auto kCwLearnCalls = "cw/learn_calls";`
- Load (next to the other cwDecode loads): `cwLearnCalls_ = s.value(kCwLearnCalls, false).toBool();`
- **BUGFIX (Phase 1 escape)** — line ~198 currently clamps the persisted engine to 0..1, silently turning a saved Auto (2) into DeepFist (1) on restart:

```cpp
    cwDecodeEngine_        = std::clamp(s.value(kCwDecEngine, 0).toInt(), 0, 1);
```

becomes

```cpp
    cwDecodeEngine_        = std::clamp(s.value(kCwDecEngine, 0).toInt(), 0, 2);
```

Also check `Prefs::setCwDecodeEngine` in prefs.cpp for the same 0..1 clamp and widen it to 0..2 if present.
- Setter body (next to `setCwDecodeTracking`):

```cpp
void Prefs::setCwLearnCalls(bool on) {
    if (on != cwLearnCalls_) {
        cwLearnCalls_ = on;
        QSettings().setValue(kCwLearnCalls, on);
        emit cwLearnCallsChanged();
    }
}
```

- [ ] **Step 4: Build**

`& C:\dev\Lyra-SDR-cpp\build3.ps1` — expected **BUILD OK** (stop lyra.exe first if running: the link fails on a running exe).

- [ ] **Step 5: Commit**

```bash
git add src/wdsp_engine.h src/wdsp_engine.cpp src/prefs.h src/prefs.cpp
git commit -m "feat(cw): local confirmed-call store in WdspEngine + cwLearnCalls pref; fix engine-pref clamp to allow Auto"
```

---

### Task 4: QML word tap + "Learn" toggle + docs

**Files:**
- Modify: `src/qml/CwDecoderPanel.qml` (word tap in `appendDecoded`; Learn chip in the Decode/Clear row)
- Modify: `docs/DEEPFIST.md` (short Phase-3 paragraph)

**Interfaces:**
- Consumes: `WdspEngine.cwNoteConfirmedCall(call)`, `Prefs.cwLearnCalls` (Task 3); existing `Spots.isSpotted`.
- Produces: UI leaf.

**⚠ Escape hazard:** this file contains literal ``/`` sequences near `appendAuto` and in `displayHtml`. Do not retype or move those lines. The new code below deliberately uses `charCodeAt` numeric checks so no escape literals are introduced. After editing, verify: `LC_ALL=C grep -c $'\x02\|\x03' src/qml/CwDecoderPanel.qml` prints `0`.

- [ ] **Step 1: Word tap**

Add above `appendDecoded` (i.e., just before the existing `function appendDecoded(s) {` line):

```qml
    // Phase 3 "Learn calls": watch the decode stream for completed words and
    // remember the RBN/cluster-confirmed ones in the local SCP list — the
    // amber highlight knows them immediately, the DeepFist rescorer from the
    // next model load.  Opt-in (Prefs.cwLearnCalls, default off).
    property string pendingWord: ""
    function tapWord(s) {
        for (var i = 0; i < s.length; ++i) {
            var cc = s.charCodeAt(i)
            if (cc === 2 || cc === 3) continue      // Auto dim markers: invisible
            var c = s.charAt(i)
            if ((c >= "A" && c <= "Z") || (c >= "0" && c <= "9") || c === "/") {
                pendingWord += c
            } else if (pendingWord.length > 0) {
                var w = pendingWord
                pendingWord = ""
                if (Prefs.cwLearnCalls && w.length >= 3 && /[0-9]/.test(w)
                        && Spots.isSpotted(w))
                    WdspEngine.cwNoteConfirmedCall(w)
            }
        }
    }
```

Then add one line at the top of `appendDecoded` (the single choke point — Classic, DeepFist, Auto, and the seam glyph all flow through it):

```qml
    function appendDecoded(s) {
        tapWord(s)
        var t = root.decodedText + s
        if (t.length > 6000) t = t.slice(-4500)
        root.decodedText = t
    }
```

- [ ] **Step 2: Learn chip**

In the `// ── Decode on/off + Clear ──` row, after the Clear `ChipButton` (which ends `onClicked: root.clearDecoded()` + closing brace), add:

```qml
            ChipButton {
                label: qsTr("Learn")
                lit: Prefs.cwLearnCalls
                onClicked: Prefs.cwLearnCalls = !Prefs.cwLearnCalls
            }
```

- [ ] **Step 3: Docs**

In `docs/DEEPFIST.md`, at the end of the "Front-end recipe" section (after the Sensitivity paragraph), add:

```markdown
**Learn calls** (opt-in, CW panel): while enabled, decoded callsigns that are
simultaneously confirmed by an RBN/cluster spot are remembered in a local
`scp_local.txt` (AppData; capped, aged out after a year). Those calls join the
known-real (amber) highlight immediately and merge into the callsign
rescorer's SCP candidate space at the next model load. The shipped
`MASTER.SCP` stays untouched — refreshing it is still a plain file swap.
```

- [ ] **Step 4: Build, verify markers intact, quick functional check**

1. `LC_ALL=C grep -c $'\x02\|\x03' src/qml/CwDecoderPanel.qml` → `0`.
2. Stop lyra.exe if running, `& C:\dev\Lyra-SDR-cpp\build3.ps1` → **BUILD OK**.
3. Operator check (on air, deferred if unavailable): toggle **Learn** on, copy an RBN-spotted (green) call, then check `%APPDATA%/<app>/scp_local.txt` gained a `CALL<TAB>epoch` line. With Learn off (default): file untouched/absent.

- [ ] **Step 5: Commit**

```bash
git add src/qml/CwDecoderPanel.qml docs/DEEPFIST.md
git commit -m "feat(cw): Learn-calls toggle — RBN-confirmed copy feeds the local SCP list"
```

---

## Notes / deferrals

- Cap/age constants are compile-time defaults (spec YAGNI); expose prefs only if field use demands.
- The rescorer sees locally-learned calls at the **next model load** (engine switch or restart), never mid-worker — this is the deliberate no-locking design.
- `scp_local.txt` lives in AppData, not `models/` — it survives a MASTER.SCP artifact swap (spec §6.4) and installer upgrades.
- Restore-of-Auto on restart remains deferred (Phase 1 decision); the Task 3 clamp fix only stops the pref from silently degrading 2→1.

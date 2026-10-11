# VFO Styles — Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Tuning panel's hard-wired amber VFO readout with a style framework and ship six selectable styles (Amber LED, Lyra (Mac) — default, Classic LCD, Nixie, Widget, Aluminum).

**Architecture:** `FreqDisplay` stops painting and becomes a transparent tuning core that hit-tests against digit rects reported by the active style. A shared `VfoBox.qml` (used for VFO A and VFO B) loads the style named by `Prefs.vfoStyle` through the `VfoStyles.js` registry, lays FreqDisplay, the typed-entry field, the shared Step/Mode row and the TX pip over rects the style reports, and scales the style uniformly to fit. Styles are pure drawing files with no access to Stream/Prefs, so a contract test can load every one of them headless.

**Tech Stack:** C++23, Qt 6.11 (Quick, Qml, QuickControls2, QtQuick.Effects), QML, CMake + Ninja, Homebrew Qt on macOS arm64.

**Spec:** `docs/superpowers/specs/2026-10-10-vfo-styles-design.md`

## Global Constraints

- Build: `cmake --build build -j` (configured: Ninja, `RelWithDebInfo`, `CMAKE_PREFIX_PATH=/opt/homebrew/opt/qt`). Run: `build/lyra.app/Contents/MacOS/lyra`.
- Work in `/Users/bcrier/Documents/dev/Lyra` on branch `macos-port` only. Never touch `/Users/bcrier/Documents/dev/Lyra-meter`.
- **No `QtQuick.Shapes`** anywhere (broke the shared scene graph / waterfall before — `PanadapterPanel.qml`).
- `Canvas` only for static decoration painted once; `MultiEffect` allowed, verified against the waterfall.
- Do not set a global Qt Quick Controls style.
- Styles must not reference `Stream`, `Prefs`, `WdspEngine` or `panel` — VfoBox feeds them.
- TX state must always read as red (`#ff4136`-family) in every style.
- `MeterModel` is not touched in Phase 1.
- Commits only after the operator approves; conventional messages that say why; trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push.
- Downloading font files needs the operator's explicit OK first.
- Report changed vs. verified separately; never claim a check that wasn't run.

## Spec refinements (decided while planning — applied to the spec in this plan's commit)

1. Files live flat in `src/qml/` with a `Vfo` prefix (matches the existing flat `Lyra*` / `Meter*` layout and avoids QML-module subdirectory quirks), not `src/qml/vfo/`.
2. `VfoBox` takes `isB: bool` instead of `role: "A"|"Bsplit"|"Bsub"` — the existing VFO B box already serves SPLIT and SUB from one code path (`displayBHz`).
3. Styles do **not** instantiate `VfoStepMode` / `VfoTxPip`; they report `stepModeRect`, `txPipRect`, `tuneRect` and a `theme`, and `VfoBox` places the shared controls. This keeps styles free of Stream/Prefs so the contract test can load them headless, and lets the test prove no control sits under FreqDisplay.
4. VFO B shows its Mode picker in SUB (RX2's own mode), exactly as today; hidden in SPLIT.
5. Fonts live in `src/assets/fonts/` (beside the existing `src/assets/logo/`) and are registered from `main.cpp` with `QFontDatabase::addApplicationFont`.
6. `Prefs.vfoStyle` defaults to `""`; the registry maps `""`/unknown to its `defaultId`, so Prefs never needs to know style ids.

## Review Focus

1. **Typed entry on every style** — double-click a VFO, type `14.2`, Enter → tunes; Esc → cancels; click elsewhere → commits. Pinned by manual steps in Task 9 and Task 16.
2. **Keyboard tuning focus** — after clicking a digit, ←/→ move the selection and ↑/↓ tune that VFO once per press (the global ↑/↓ nudge in `mainwindow.cpp` must not also fire). Pinned in Task 9.
3. **Floating or saved-layout Tuning dock** — changing style while the dock is floating must not resize the main window or crash; a restart restores the saved layout, not a style-driven height. Pinned in Task 8.
4. **Narrow panel** — at 760–900 px wide, both VFOs + logo: styles shrink, nothing overlaps the digits. Pinned in Task 9 and Task 16.
5. **Frequency extremes** — 0 Hz, 136 kHz (leading zeros), 2.147 GHz (all ten digits): every style still reports 10 rects inside its tune area and always shows places 0–5. Pinned by the contract test (Task 5).

---

### Task 0: Baseline screenshots (before any change)

**Files:** none (scratchpad only)

- [ ] **Step 1: Write the window-finder script** to the session scratchpad as `winlist.swift`:

```swift
import CoreGraphics
let opts = CGWindowListOption(arrayLiteral: .optionAll)
if let list = CGWindowListCopyWindowInfo(opts, kCGNullWindowID) as? [[String: Any]] {
  for w in list {
    let owner = w[kCGWindowOwnerName as String] as? String ?? ""
    if owner.lowercased() == "lyra" {
      let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
      print(w[kCGWindowNumber as String] ?? 0, w[kCGWindowName as String] ?? "", b["Width"] ?? 0, b["Height"] ?? 0)
    }
  }
}
```

- [ ] **Step 2: Build and run the current tree**, then capture the window:

```bash
cmake --build build -j && (build/lyra.app/Contents/MacOS/lyra > "$SCRATCH/lyra.log" 2>&1 &)
swift "$SCRATCH/winlist.swift"            # note the id of the main "Lyra — v…" window
screencapture -x -o -l <id> "$SCRATCH/baseline.png"
```

- [ ] **Step 3: Ask the operator to turn SPLIT on, capture `baseline-split.png`; SUB on, capture `baseline-sub.png`.** Crop the VFO row of each (`sips -c <h> <w> --cropOffset <y> <x>`) for later side-by-side comparison. Keep these until Task 9 passes.

---

### Task 1: Digit hit-test helper

**Files:**
- Create: `src/vfo_digit_hit.h`
- Create: `scratch/test_vfo_digit_hit.cpp`
- Modify: `CMakeLists.txt` (new test target after the `test_wheel_steps` block, ~line 1349)

**Interfaces:**
- Produces: `std::vector<QRectF> lyra::ui::digitRectsFromVariant(const QVariantList &l, int nDigits)` and `int lyra::ui::digitPlaceAt(const std::vector<QRectF> &rects, const QPointF &p)` (−1 = no digit).

- [ ] **Step 1: Write the failing test** `scratch/test_vfo_digit_hit.cpp`:

```cpp
// Offline test for lyra::ui::digitPlaceAt / digitRectsFromVariant
// (src/vfo_digit_hit.h) — how a click or wheel position on a VFO style maps
// to a digit place, given the per-digit rects the style reports
// (index = place: 0 = Hz units … 9 = 1 GHz).
//   build: cmake --build build --target test_vfo_digit_hit
//   run:   build/test_vfo_digit_hit
#include "vfo_digit_hit.h"

#include <QString>
#include <cstdio>

using lyra::ui::digitPlaceAt;
using lyra::ui::digitRectsFromVariant;

static int g_failures = 0;

static void check(const char *name, int got, int want)
{
    const bool ok = got == want;
    std::printf("  %-60s got=%3d want=%3d  %s\n", name, got, want,
                ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

int main()
{
    std::printf("test_vfo_digit_hit\n");

    // Ten 20x30 cells left→right; the leftmost cell is place 9.
    QVariantList row;
    for (int place = 0; place < 10; ++place)
        row.append(QRectF((9 - place) * 20.0, 0.0, 20.0, 30.0));
    const auto rects = digitRectsFromVariant(row, 10);
    check("ten slots",                         int(rects.size()), 10);
    check("leftmost cell -> place 9",          digitPlaceAt(rects, {5, 10}), 9);
    check("rightmost cell -> place 0",         digitPlaceAt(rects, {195, 10}), 0);
    check("fourth cell -> place 6",            digitPlaceAt(rects, {65, 10}), 6);
    check("below the row -> none",             digitPlaceAt(rects, {65, 40}), -1);
    check("left of the row -> none",           digitPlaceAt(rects, {-1, 10}), -1);

    // A style that reports fewer rects: the missing places are never hit.
    QVariantList shortList;
    shortList.append(QRectF(0, 0, 10, 10));
    const auto s = digitRectsFromVariant(shortList, 10);
    check("short list still ten slots",        int(s.size()), 10);
    check("short list place 0 hit",            digitPlaceAt(s, {5, 5}), 0);
    check("short list missing place not hit",  digitPlaceAt(s, {50, 5}), -1);

    // Junk entries and empty rects (a hidden digit) are ignored.
    QVariantList junk;
    junk.append(QStringLiteral("nope"));
    junk.append(QRectF(0, 0, 0, 0));
    junk.append(QRectF(0, 0, 10, 10));
    const auto j = digitRectsFromVariant(junk, 10);
    check("junk and empty skipped, place 2 hit", digitPlaceAt(j, {5, 5}), 2);

    // A list longer than ten: extras ignored.
    QVariantList longList = row;
    longList.append(QRectF(500, 0, 20, 30));
    check("eleventh rect ignored", digitPlaceAt(digitRectsFromVariant(longList, 10), {505, 5}), -1);

    check("no rects -> none", digitPlaceAt(digitRectsFromVariant({}, 10), {5, 5}), -1);

    std::printf(g_failures ? "FAILED: %d\n" : "ALL PASS\n", g_failures);
    return g_failures ? 1 : 0;
}
```

- [ ] **Step 2: Add the test target** to `CMakeLists.txt` directly after the `test_wheel_steps` block:

```cmake
# VFO style digit hit-testing (src/vfo_digit_hit.h) — which digit place a
# click / wheel lands on, from the rects the active VFO style reports.
# Qt Core only.  Build + run:  cmake --build build --target test_vfo_digit_hit
add_executable(test_vfo_digit_hit EXCLUDE_FROM_ALL
    scratch/test_vfo_digit_hit.cpp
    src/vfo_digit_hit.h
)
target_include_directories(test_vfo_digit_hit PRIVATE src)
target_link_libraries(test_vfo_digit_hit PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(test_vfo_digit_hit PRIVATE /utf-8 /Zc:preprocessor)
endif()
```

- [ ] **Step 3: Run it to verify it fails**

Run: `cmake --build build --target test_vfo_digit_hit`
Expected: FAIL — compile/configure error, `vfo_digit_hit.h` not found.

- [ ] **Step 4: Write `src/vfo_digit_hit.h`**

```cpp
// Lyra — VFO digit hit-testing (FreqDisplay mouse / wheel).
//
// The active VFO style reports one rect per digit place (index = place:
// 0 = Hz units … 9 = 1 GHz) in FreqDisplay's coordinates.  An empty rect —
// or a missing / non-rect entry — means that digit isn't shown and can't be
// clicked.  Pure functions so scratch/test_vfo_digit_hit.cpp can pin them.

#pragma once

#include <QPointF>
#include <QRectF>
#include <QVariant>
#include <QVariantList>

#include <vector>

namespace lyra::ui {

inline std::vector<QRectF> digitRectsFromVariant(const QVariantList &list,
                                                 int nDigits)
{
    std::vector<QRectF> out(static_cast<size_t>(nDigits));
    for (int i = 0; i < nDigits && i < list.size(); ++i) {
        const QVariant &v = list.at(i);
        if (v.canConvert<QRectF>())
            out[static_cast<size_t>(i)] = v.toRectF();
    }
    return out;
}

inline int digitPlaceAt(const std::vector<QRectF> &rects, const QPointF &p)
{
    for (size_t i = 0; i < rects.size(); ++i) {
        if (!rects[i].isEmpty() && rects[i].contains(p))
            return static_cast<int>(i);
    }
    return -1;
}

}  // namespace lyra::ui
```

- [ ] **Step 5: Run it to verify it passes**

Run: `cmake --build build --target test_vfo_digit_hit && build/test_vfo_digit_hit`
Expected: every line `PASS`, last line `ALL PASS`, exit 0. If "junk" fails because `QVariant(QString).canConvert<QRectF>()` is true on this Qt, change the guard to `v.typeId() == QMetaType::QRectF` and re-run.

- [ ] **Step 6: No commit yet** — the framework lands as one commit at Task 9.

---

### Task 2: FreqDisplay becomes the input-only tuning core

**Files:**
- Modify: `src/freqdisplay.h` (whole class)
- Modify: `src/freqdisplay.cpp` (drop `paint()`, colours, font includes; new rect/selection plumbing)

**Interfaces:**
- Consumes: `digitRectsFromVariant`, `digitPlaceAt` (Task 1).
- Produces (QML type `FreqDisplay`, URI `LyraUI 1.0`, registration in `main.cpp` unchanged): properties `freqHz` (rw), `externalStepHz` (rw), `digitRects : QVariantList` (rw, index = place, FreqDisplay coords), `selectedDigit : int` (read-only, notify); signals `freqEdited(int hz)`, `editRequested()`; `Q_INVOKABLE int parseFreqInput(QString)`.

- [ ] **Step 1: Rewrite the header comment and class in `src/freqdisplay.h`**

Replace the file's top comment with:

```cpp
// Lyra — VFO tuning core (RX1 / VFO B readout input).
//
// Started as a C++ port of old Lyra's led_freq.py and used to paint the
// amber readout itself.  Painting now belongs to the selectable VFO styles
// (src/qml/VfoStyle*.qml); this item is laid transparently over the style's
// tune area by VfoBox.qml and keeps ALL the tuning behaviour so every style
// tunes identically: click a digit to select its place, wheel over a digit
// tunes THAT place (10^place), wheel elsewhere in the area tunes by the Step
// combo's value (externalStepHz), ←/→ move the selection, ↑/↓ nudge it, and
// double-click requests typed entry (the host pops a field and calls
// setFreqHz with parseFreqInput's result).
//
// The style reports where its digits are (digitRects, index = place); the
// style draws the selection from selectedDigit.  freqHz mirrors the
// operator's tuned frequency; user tuning emits freqEdited(hz), which the
// host wires to Stream.  setFreqHz() does NOT emit freqEdited, so there's no
// set→edit→set feedback loop.
```

Replace the includes and class body:

```cpp
#pragma once

#include <QQuickItem>
#include <QRectF>
#include <QVariantList>

#include <vector>

namespace lyra::ui {

class FreqDisplay : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int freqHz READ freqHz WRITE setFreqHz NOTIFY freqHzChanged)
    // Mouse-wheel step (Hz) used when the cursor isn't over a specific
    // digit — driven by the panel's Step combo.  0 = per-digit only.
    Q_PROPERTY(int externalStepHz READ externalStepHz WRITE setExternalStepHz
               NOTIFY externalStepHzChanged)
    // Per-digit hit rects from the active VFO style, index = place
    // (0 = Hz units … 9 = 1 GHz), in this item's coordinates.  An empty or
    // missing rect = that digit isn't shown and can't be clicked.
    Q_PROPERTY(QVariantList digitRects READ digitRects WRITE setDigitRects
               NOTIFY digitRectsChanged)
    // Selected place, drawn by the style; click / wheel / ←→ change it.
    Q_PROPERTY(int selectedDigit READ selectedDigit NOTIFY selectedDigitChanged)

public:
    explicit FreqDisplay(QQuickItem *parent = nullptr);

    int  freqHz() const { return freqHz_; }
    void setFreqHz(int hz);              // external set (no freqEdited)
    int  externalStepHz() const { return externalStepHz_; }
    void setExternalStepHz(int hz);
    QVariantList digitRects() const { return digitRectsVar_; }
    void setDigitRects(const QVariantList &rects);
    int  selectedDigit() const { return selected_; }

    // Parse a free-form frequency entry (MHz decimal / Hz with
    // separators / bare) into Hz, or -1 if invalid.  Mirrors old Lyra's
    // parse_freq_input.  Q_INVOKABLE so the QML typed-entry field uses it.
    Q_INVOKABLE int parseFreqInput(const QString &text) const;

signals:
    void freqHzChanged();
    void externalStepHzChanged();
    void digitRectsChanged();
    void selectedDigitChanged();
    void freqEdited(int hz);     // user tuned (wheel / keys) — wire to Stream
    void editRequested();        // double-click — host shows a typed-entry field

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void changeFreq(int deltaHz);        // tune + emit freqEdited
    void setSelected(int place);

    static constexpr int kNDigits = 10;
    // Display/RF ceiling (VFO LED + wheel).  HF native is ~55 MHz; Xvtr
    // RF (2 m / 70 cm / 23 cm) sits well above that.  Cap is signed-int
    // max so freqHz_ / Q_PROPERTY(int) stay valid (~2.147 GHz).
    static constexpr int kMaxHz   = 2'147'483'647;

    int  freqHz_         = 7'074'000;
    int  selected_       = 3;            // default = 1 kHz place
    int  externalStepHz_ = 0;
    int  wheelAccum_     = 0;            // partial wheel delta (wheel_steps.h)
    QVariantList        digitRectsVar_;  // as set from QML (change detection)
    std::vector<QRectF> rects_;          // parsed, index = place (vfo_digit_hit.h)
};

} // namespace lyra::ui
```

- [ ] **Step 2: Update `src/freqdisplay.cpp`**

1. Top comment: `// Lyra — VFO tuning core.  See freqdisplay.h.`
2. Includes: remove `<QColor>`, `<QFont>`, `<QFontMetricsF>`, `<QPainter>`, `<QPen>`; add `#include "vfo_digit_hit.h"`.
3. Delete the `kBg`, `kAmber`, `kAmberDim`, `kSelect`, `kFaint` constants (keep `placeStep`).
4. Constructor:

```cpp
FreqDisplay::FreqDisplay(QQuickItem *parent) : QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setFlag(ItemAcceptsInputMethod, false);
}
```

5. In `setFreqHz` and `changeFreq`, delete the `update();` lines.
6. Delete `FreqDisplay::paint` entirely.
7. Add after `setExternalStepHz`:

```cpp
void FreqDisplay::setDigitRects(const QVariantList &rects) {
    if (rects == digitRectsVar_) {
        return;
    }
    digitRectsVar_ = rects;
    rects_ = digitRectsFromVariant(rects, kNDigits);
    emit digitRectsChanged();
}

void FreqDisplay::setSelected(int place) {
    if (place == selected_) {
        return;
    }
    selected_ = place;
    emit selectedDigitChanged();
}
```

8. Replace `mousePressEvent`'s body after the button check:

```cpp
    const int place = digitPlaceAt(rects_, e->position());
    if (place >= 0) {
        setSelected(place);
    }
    forceActiveFocus();
    e->accept();
```

9. In `wheelEvent`, replace the `for (const auto &[idx, rect] : digitRects_)` loop with:

```cpp
    const int hover = digitPlaceAt(rects_, e->position());
    if (hover >= 0) {
        setSelected(hover);
    }
```

   and change the following `int hover = -1;` declaration line away (the line above now declares it).
10. In `keyPressEvent`, Left/Right become:

```cpp
    case Qt::Key_Left:  setSelected(std::min(kNDigits - 1, selected_ + 1)); break;
    case Qt::Key_Right: setSelected(std::max(0, selected_ - 1)); break;
```

- [ ] **Step 3: Build the app**

Run: `cmake --build build -j 2>&1 | grep -E "error|warning: " ; echo exit ${pipestatus[1]}`
Expected: exit 0, no errors. (The VFO shows no digits until Task 7 — that is expected; do not run the app for verification yet.)

- [ ] **Step 4: Re-run the helper and wheel tests**

Run: `cmake --build build --target test_vfo_digit_hit test_wheel_steps && build/test_vfo_digit_hit && build/test_wheel_steps`
Expected: both `ALL PASS` (wheel_steps prints its own summary; exit 0).

---

### Task 3: `Prefs.vfoStyle`

**Files:**
- Modify: `src/prefs.h` (property, accessors, signal, member)
- Modify: `src/prefs.cpp` (key constant, load, setter)

**Interfaces:**
- Produces: `Prefs.vfoStyle : QString` (rw, notify `vfoStyleChanged()`), QSettings key `ui/vfo_style`, default `""`.

- [ ] **Step 1: `src/prefs.h`** — next to the `tooltipsEnabled` Q_PROPERTY add:

```cpp
    // Tuning-panel VFO style id (src/qml/VfoStyles.js), shared by VFO A and
    // B.  Empty = the registry default; an unknown id also falls back to it.
    Q_PROPERTY(QString vfoStyle READ vfoStyle WRITE setVfoStyle
               NOTIFY vfoStyleChanged)
```

   With the other accessors (next to `tooltipsEnabled()`):

```cpp
    QString vfoStyle() const { return vfoStyle_; }
    void    setVfoStyle(const QString &id);
```

   Signal (next to `tooltipsEnabledChanged();`): `void vfoStyleChanged();`
   Member (next to `tooltipsEnabled_`): `QString vfoStyle_;`

- [ ] **Step 2: `src/prefs.cpp`** — after `constexpr auto kTooltipsEnabled = "ui/tooltips_enabled";` add `constexpr auto kVfoStyle = "ui/vfo_style";`. After the `tooltipsEnabled_ = s.value(...)` load line add `vfoStyle_ = s.value(kVfoStyle, QString()).toString();`. After `Prefs::setTooltipsEnabled` add:

```cpp
void Prefs::setVfoStyle(const QString &id) {
    const QString v = id.trimmed();
    if (v != vfoStyle_) {
        vfoStyle_ = v;
        QSettings().setValue(kVfoStyle, v);
        emit vfoStyleChanged();
    }
}
```

- [ ] **Step 3: Build** — `cmake --build build -j`, expected exit 0.

---

### Task 4: Themeable `LyraComboBox` (defaults unchanged)

**Files:**
- Modify: `src/qml/LyraComboBox.qml:14-52`

**Interfaces:**
- Produces: `fieldColor`, `fieldBorderColor`, `textColor`, `accentColor` (all `color`) on `LyraComboBox`; defaults equal today's literals so every existing combo is unchanged.

- [ ] **Step 1: Add the properties** after `property int textRightPad: 20`:

```qml
    // Optional theme (VFO styles colour their Step/Mode pickers).  Defaults
    // are the standard Lyra look, so every other combo is unchanged.
    property color fieldColor: "#161e28"
    property color fieldBorderColor: "#2a3a4a"
    property color textColor: "#f2f8fc"
    property color accentColor: "#50d0ff"
```

- [ ] **Step 2: Use them** — `contentItem` color `control.enabled ? control.textColor : "#5a6670"`; `indicator` color `control.enabled ? control.accentColor : "#5a6670"`; `background` color `control.enabled ? control.fieldColor : "#12171d"` and border `control.activeFocus ? control.accentColor : control.hovered ? "#3a5a6a" : control.fieldBorderColor`.

- [ ] **Step 3: Build** — `cmake --build build -j`, expected exit 0. Visual check happens in Task 9 (every other panel's combos must look exactly as before).

---

### Task 5: Style contract test (written before the styles)

**Files:**
- Create: `scratch/test_vfo_styles.cpp`
- Modify: `CMakeLists.txt` (target after `test_vfo_digit_hit`)

**Interfaces:**
- Consumes: the style contract (inputs `freqHz, selectedDigit, vfoState, roleText, vfoLetter, modeText, stepHz, showMode`; outputs `naturalWidth, naturalHeight, tuneRect, digitRects, stepModeRect, txPipRect, theme{label, field, fieldBorder, text, accent, pipIdle, pipIdleBorder, pipIdleText, compact}`) and the registry (`VfoStyles.styles`, `defaultId`, `resolve(id)`).

- [ ] **Step 1: Write `scratch/test_vfo_styles.cpp`**

```cpp
// Contract test for the Tuning-panel VFO styles (src/qml/VfoStyle*.qml).
// Loads the registry (src/qml/VfoStyles.js) and instantiates EVERY
// registered style straight from the source tree, headless, then checks
// what VfoBox.qml relies on:
//   - it loads with no QML errors or warnings;
//   - naturalWidth/Height > 0;
//   - tuneRect / stepModeRect / txPipRect lie inside the style; the Step/Mode
//     row does NOT overlap tuneRect (FreqDisplay sits over tuneRect and would
//     swallow its clicks), and the TX pip covers no digit;
//   - exactly 10 digitRects, all inside tuneRect, places 0-5 (kHz + Hz,
//     always shown) non-empty;
//   - theme carries every key VfoStepMode / VfoTxPip read;
// at 0 Hz, 136 kHz, 14.168 MHz and 2.147 GHz.  Also: the registry resolves
// an empty or unknown id to its default, and the default is registered.
//   build: cmake --build build --target test_vfo_styles
//   run:   build/test_vfo_styles
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRectF>
#include <QUrl>
#include <QVariant>

#include <cstdio>
#include <memory>

#ifndef LYRA_QML_DIR
#error "LYRA_QML_DIR must point at src/qml"
#endif

static int g_failures = 0;

static void check(const QString &name, bool ok)
{
    std::printf("  %-76s %s\n", qPrintable(name), ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

static QVariant plain(const QVariant &v)
{
    return v.canConvert<QJSValue>() ? v.value<QJSValue>().toVariant() : v;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    std::printf("test_vfo_styles\n");

    const QString dir = QStringLiteral(LYRA_QML_DIR);
    QQmlEngine engine;
    QList<QQmlError> warnings;
    QObject::connect(&engine, &QQmlEngine::warnings,
                     [&](const QList<QQmlError> &w) { warnings += w; });

    // ── Registry ──
    QQmlComponent reg(&engine);
    reg.setData(R"(import QtQml
import "VfoStyles.js" as VfoStyles
QtObject {
    property var styles: VfoStyles.styles
    property string defaultId: VfoStyles.defaultId
    property string unknownId: VfoStyles.resolve("no-such-style").id
    property string emptyId: VfoStyles.resolve("").id
})", QUrl::fromLocalFile(dir + QStringLiteral("/registry_probe.qml")));
    std::unique_ptr<QObject> r(reg.create());
    check(QStringLiteral("registry loads"), r != nullptr);
    if (!r) {
        std::printf("%s\n", qPrintable(reg.errorString()));
        return 1;
    }
    const QString defId = r->property("defaultId").toString();
    check(QStringLiteral("unknown id resolves to the default"),
          r->property("unknownId").toString() == defId);
    check(QStringLiteral("empty id resolves to the default"),
          r->property("emptyId").toString() == defId);
    const QVariantList styles = plain(r->property("styles")).toList();
    check(QStringLiteral("registry is not empty"), !styles.isEmpty());
    bool defaultListed = false;
    for (const QVariant &sv : styles)
        defaultListed |= sv.toMap().value(QStringLiteral("id")).toString() == defId;
    check(QStringLiteral("default id is a registered style"), defaultListed);

    // ── Every style, several frequencies ──
    const int freqs[] = {0, 136'000, 14'168'000, 2'147'483'647};
    const char *themeKeys[] = {"label", "field", "fieldBorder", "text", "accent",
                               "pipIdle", "pipIdleBorder", "pipIdleText", "compact"};

    for (const QVariant &sv : styles) {
        const QVariantMap e = sv.toMap();
        const QString id = e.value(QStringLiteral("id")).toString();
        const QString file = e.value(QStringLiteral("file")).toString();
        for (int hz : freqs) {
            const QString tag = QStringLiteral("%1 @ %2 Hz: ").arg(id).arg(hz);
            warnings.clear();
            QQmlComponent comp(&engine, QUrl::fromLocalFile(dir + QLatin1Char('/') + file));
            std::unique_ptr<QObject> st(comp.createWithInitialProperties({
                {QStringLiteral("freqHz"), hz},
                {QStringLiteral("selectedDigit"), 3},
                {QStringLiteral("vfoState"), QStringLiteral("rx")},
                {QStringLiteral("roleText"), QStringLiteral("RX")},
                {QStringLiteral("vfoLetter"), QStringLiteral("A")},
                {QStringLiteral("modeText"), QStringLiteral("USB")},
                {QStringLiteral("stepHz"), 100},
                {QStringLiteral("showMode"), true}}));
            check(tag + QStringLiteral("loads"), st != nullptr);
            if (!st) {
                std::printf("%s\n", qPrintable(comp.errorString()));
                continue;
            }
            QCoreApplication::processEvents();
            check(tag + QStringLiteral("no QML warnings"), warnings.isEmpty());
            for (const QQmlError &w : warnings)
                std::printf("      %s\n", qPrintable(w.toString()));

            const double nw = st->property("naturalWidth").toDouble();
            const double nh = st->property("naturalHeight").toDouble();
            check(tag + QStringLiteral("natural size > 0"), nw > 0 && nh > 0);
            const QRectF bounds(0, 0, nw, nh);
            const QRectF tune = st->property("tuneRect").toRectF();
            const QRectF sm = st->property("stepModeRect").toRectF();
            const QRectF pip = st->property("txPipRect").toRectF();
            check(tag + QStringLiteral("tuneRect inside"), !tune.isEmpty() && bounds.contains(tune));
            check(tag + QStringLiteral("stepModeRect inside"), !sm.isEmpty() && bounds.contains(sm));
            check(tag + QStringLiteral("txPipRect inside"), !pip.isEmpty() && bounds.contains(pip));
            check(tag + QStringLiteral("stepModeRect clear of tuneRect"), !sm.intersects(tune));
            const QVariantList rects = plain(st->property("digitRects")).toList();
            // The pip is stacked above FreqDisplay in VfoBox, so it may sit in
            // a corner of tuneRect (Amber's does) — but never on a digit.
            bool pipClear = true;
            for (const QVariant &rv : rects)
                if (pip.intersects(rv.toRectF())) pipClear = false;
            check(tag + QStringLiteral("txPipRect clear of every digit"), pipClear);
            check(tag + QStringLiteral("exactly 10 digit rects"), rects.size() == 10);
            bool inside = true;
            bool lowShown = true;
            for (int p = 0; p < rects.size(); ++p) {
                const QRectF rr = rects.at(p).toRectF();
                if (rr.isEmpty()) {
                    if (p <= 5) lowShown = false;
                    continue;
                }
                if (!tune.contains(rr)) inside = false;
            }
            check(tag + QStringLiteral("digit rects inside tuneRect"), inside);
            check(tag + QStringLiteral("places 0-5 always shown"), lowShown);

            const QVariantMap theme = plain(st->property("theme")).toMap();
            bool keys = true;
            for (const char *k : themeKeys)
                keys &= theme.contains(QLatin1String(k));
            check(tag + QStringLiteral("theme has every key"), keys);
        }
    }

    std::printf(g_failures ? "FAILED: %d\n" : "ALL PASS\n", g_failures);
    return g_failures ? 1 : 0;
}
```

- [ ] **Step 2: Add the target** after `test_vfo_digit_hit` in `CMakeLists.txt`:

```cmake
# VFO style contract test — loads src/qml/VfoStyles.js and every registered
# VfoStyle*.qml from the source tree (headless, offscreen) and checks the
# geometry/theme contract VfoBox.qml relies on.
# Build + run:  cmake --build build --target test_vfo_styles
add_executable(test_vfo_styles EXCLUDE_FROM_ALL
    scratch/test_vfo_styles.cpp
)
target_compile_definitions(test_vfo_styles PRIVATE
    LYRA_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/qml")
target_link_libraries(test_vfo_styles PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
if(MSVC)
    target_compile_options(test_vfo_styles PRIVATE /utf-8 /Zc:preprocessor)
endif()
```

- [ ] **Step 3: Run it to verify it fails**

Run: `cmake --build build --target test_vfo_styles && build/test_vfo_styles`
Expected: builds; `registry loads ... FAIL` (VfoStyles.js does not exist yet), exit 1.

---

### Task 6: Shared VFO QML — digit row, registry, Amber style, Step/Mode, TX pip, VfoBox

**Files:**
- Create: `src/qml/VfoDigitRow.qml`, `src/qml/VfoStyles.js`, `src/qml/VfoStyleAmber.qml`, `src/qml/VfoStepMode.qml`, `src/qml/VfoTxPip.qml`, `src/qml/VfoBox.qml`
- Modify: `CMakeLists.txt` `qt_add_qml_module` `QML_FILES` (after `src/qml/LyraButton.qml`)

**Interfaces:**
- Consumes: `FreqDisplay` (Task 2), `Prefs.vfoStyle` (Task 3), `LyraComboBox` theme props (Task 4); from the TuningPanel root (`panel`): `centerHz`, `displayBHz`, `commitBCarrier(hz)`, `assignTx(toB)`, `vfoStepHzList`, `vfoStepLabels`, `vfoStepIndex(hz)`, `modeList`.
- Produces: QML types `VfoBox { required panel; required isB; readonly naturalWidth; readonly naturalHeight }`, `VfoDigitRow`, `VfoStepMode`, `VfoTxPip`; JS `VfoStyles.styles`, `.defaultId`, `.byId(id)`, `.resolve(id)`.

- [ ] **Step 1: `src/qml/VfoDigitRow.qml`**

```qml
// Lyra — one row of VFO digits (MMMM.kkk.hhh) for the VFO styles.
//
// Lays the ten digits and two separators out at EXPLICIT x positions (no Row
// positioner, so the geometry is known without a window — the style contract
// test relies on that) and reports each digit's cell as `rects` (index =
// place: 0 = Hz units … 9 = 1 GHz) for FreqDisplay hit-testing.  Draws an
// optional ghost glyph behind each digit, dims leading MHz zeros, and
// underlines the selected place.  Styles choose the font and colours.
import QtQuick

Item {
    id: row
    property string digitString: "0000000000"
    property font digitFont
    property color digitColor: "white"
    property color dimColor: "gray"          // leading MHz zeros
    property string ghostText: ""            // e.g. "8" for an unlit-segment ghost
    property color ghostColor: "transparent"
    property string sepText: "."
    property color sepColor: digitColor
    property int selectedDigit: -1
    property color selectColor: "#00e5ff"
    property real selectThickness: 3
    property real selectGap: 3               // underline centre, below the baseline
    property bool centerGlyphs: false        // centre proportional glyphs in their cell
    property int renderType: Text.QtRendering

    FontMetrics { id: fm; font: row.digitFont }
    readonly property real digitW: fm.advanceWidth("0")
    readonly property real sepW: fm.advanceWidth(sepText)
    readonly property real cellH: fm.height
    readonly property real baseline: fm.ascent

    // x of character cell i (0 = leftmost = place 9).
    function cellX(i) {
        return i * digitW + (i > 3 ? sepW : 0) + (i > 6 ? sepW : 0)
    }
    // Leading-zero suppression on the MHz digits (places 9..6).
    function leadingZero(i) {
        if (9 - i < 6) return false
        for (var j = 0; j <= i; ++j)
            if (digitString.charAt(j) !== "0") return false
        return true
    }

    readonly property var rects: {
        var a = []
        for (var place = 0; place < 10; ++place)
            a.push(Qt.rect(cellX(9 - place), 0, digitW, cellH))
        return a
    }
    // x centres of the MHz / kHz / Hz groups, for unit labels.
    readonly property var groupCenters: [cellX(0) + 2 * digitW,
                                         cellX(4) + 1.5 * digitW,
                                         cellX(7) + 1.5 * digitW]

    implicitWidth: 10 * digitW + 2 * sepW
    implicitHeight: cellH

    Repeater {
        model: 10
        Item {
            required property int index
            x: row.cellX(index)
            width: row.digitW
            height: row.cellH
            Text {
                visible: row.ghostText !== ""
                width: parent.width
                horizontalAlignment: row.centerGlyphs ? Text.AlignHCenter : Text.AlignLeft
                text: row.ghostText
                font: row.digitFont
                color: row.ghostColor
                renderType: row.renderType
            }
            Text {
                width: parent.width
                horizontalAlignment: row.centerGlyphs ? Text.AlignHCenter : Text.AlignLeft
                text: row.digitString.charAt(index)
                font: row.digitFont
                color: row.leadingZero(index) ? row.dimColor : row.digitColor
                renderType: row.renderType
            }
            Rectangle {
                visible: row.selectedDigit === 9 - index
                x: 1
                width: row.digitW - 2
                height: row.selectThickness
                y: row.baseline + row.selectGap - height / 2
                color: row.selectColor
            }
        }
    }
    Repeater {
        model: [3, 6]
        Text {
            required property int modelData
            x: row.cellX(modelData) + row.digitW
            text: row.sepText
            font: row.digitFont
            color: row.sepColor
            renderType: row.renderType
        }
    }
}
```

- [ ] **Step 2: `src/qml/VfoStyles.js`**

```js
.pragma library
// Lyra — VFO style registry (Tuning panel).  One entry per style:
//   id    — saved in Prefs.vfoStyle; NEVER rename an existing id
//   name  — shown in the right-click "VFO style" menu
//   group — Modern / Classic / Ham shack (menu headings, Phase 2+)
//   file  — the style's QML file, next to this one
// Only implemented styles are listed.  resolve() maps an empty or unknown id
// (first run, or a removed style) to defaultId.
// scratch/test_vfo_styles.cpp loads every entry and checks the contract.

var defaultId = "amber"

var styles = [
    { id: "amber", name: "Amber LED", group: "Classic", file: "VfoStyleAmber.qml" }
]

function byId(id) {
    for (var i = 0; i < styles.length; ++i)
        if (styles[i].id === id) return styles[i]
    return null
}

function resolve(id) {
    return byId(id) || byId(defaultId)
}
```

- [ ] **Step 3: `src/qml/VfoStyleAmber.qml`** — the original look, same 360×108 box and geometry as the old ColumnLayout (margins 8/8/4/6, digit area 64 high, 10 px spacing, 24 px Step row):

```qml
// Lyra VFO style — Amber LED (the original look).
//
// A port of FreqDisplay's old QPainter readout: amber digits on near-black
// with a ghost "8" behind each (unlit-segment look), dimmed leading MHz
// zeros, cyan selected-digit underline and MHz/kHz/Hz labels, in the
// original 360×108 bordered box.  Style contract: see VfoBox.qml.
import QtQuick

Item {
    id: st
    // ── Contract inputs (set by VfoBox) ──
    property int freqHz: 0
    property int selectedDigit: -1
    property string vfoState: "rx"          // "rx" | "tx" | "armed"
    property string roleText: "RX"
    property string vfoLetter: "A"
    property string modeText: ""
    property int stepHz: 0
    property bool showMode: true
    // ── Contract outputs ──
    readonly property real naturalWidth: 360
    readonly property real naturalHeight: 108
    readonly property rect tuneRect: Qt.rect(8, 4, 344, 64)
    readonly property rect stepModeRect: Qt.rect(8, 78, 344, 24)
    readonly property rect txPipRect: Qt.rect(327, 5, 28, 15)
    readonly property var digitRects: digits.rects.map(function (r) {
        return Qt.rect(tuneRect.x + digits.x + r.x, tuneRect.y + digits.y + r.y,
                       r.width, r.height)
    })
    readonly property var theme: ({
        label: "#cccccc", field: "#161e28", fieldBorder: "#2a3a4a",
        text: "#f2f8fc", accent: "#50d0ff",
        pipIdle: "#2a323c", pipIdleBorder: "#5a6570", pipIdleText: "#7a8490",
        compact: false
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    // First installed family, like the old QFont::setFamilies fallback list.
    function pickFamily(list) {
        var have = Qt.fontFamilies()
        for (var i = 0; i < list.length; ++i)
            if (have.indexOf(list[i]) >= 0) return list[i]
        return list[list.length - 1]
    }
    readonly property string digitFamily:
        pickFamily(["Share Tech Mono", "Consolas", "Courier New"])

    width: naturalWidth
    height: naturalHeight

    Rectangle {
        anchors.fill: parent
        color: "#0a0e12"
        radius: 6
        border.width: 2
        border.color: st.vfoState === "tx" ? "#ff4136"
                    : st.vfoState === "armed" ? "#3a4250" : "#34c759"
    }

    Item {
        id: area
        x: st.tuneRect.x
        y: st.tuneRect.y
        width: st.tuneRect.width
        height: st.tuneRect.height

        Rectangle { anchors.fill: parent; color: "#040406" }

        VfoDigitRow {
            id: digits
            digitString: st.digitString
            digitFont: Qt.font({ family: st.digitFamily,
                                 pixelSize: Math.max(24, Math.floor(area.height * 0.64)),
                                 weight: Font.Bold })
            digitColor: "#ffab47"
            dimColor: "#181006"
            ghostText: "8"
            ghostColor: "#181006"
            sepColor: "#ffab47"
            selectedDigit: st.selectedDigit
            selectColor: "#00e5ff"
            renderType: Text.NativeRendering
            x: (area.width - implicitWidth) / 2
            y: (area.height - cellH) / 2
        }

        FontMetrics {
            id: unitFm
            font.family: "Consolas"
            font.pixelSize: Math.max(11, Math.floor(area.height * 0.19))
            font.bold: true
        }
        Repeater {
            model: ["MHz", "kHz", "Hz"]
            Text {
                required property int index
                required property string modelData
                text: modelData
                font: unitFm.font
                color: "#b9cadc"
                renderType: Text.NativeRendering
                x: digits.x + digits.groupCenters[index] - implicitWidth / 2
                y: area.height - 4 - unitFm.ascent
            }
        }
    }

    Text {   // amber role tag, upper-left — over the readout, as before
        x: 8
        y: 4
        text: st.roleText
        color: "#efb340"
        font.bold: true
        font.pixelSize: 12
    }
}
```

- [ ] **Step 4: `src/qml/VfoStepMode.qml`**

```qml
// Lyra — a VFO's Step (and Mode) pickers, shared by every VFO style.
//
// Holds ALL the Step/Mode logic — per-mode step memory, and RX2's own step
// and mode on VFO B — so a style only decides where this row sits (its
// stepModeRect) and its colours (its theme).  VFO B shows Mode only in SUB;
// in SPLIT it transmits on VFO A's mode.  theme.compact hides the "Step" /
// "Mode" captions and narrows the pickers for small styles.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: sm
    required property var panel      // TuningPanel root (step / mode lists)
    required property bool isB
    property bool showMode: true
    property var theme
    readonly property bool compact: theme.compact === true
    readonly property int stepHz: panel.vfoStepHzList[stepBox.currentIndex]
    spacing: 6

    Item { Layout.fillWidth: true }
    Label {
        visible: !sm.compact
        text: qsTr("Step")
        color: sm.theme.label
        font.bold: true
    }
    LyraComboBox {
        id: stepBox
        Layout.preferredWidth: sm.compact ? 84 : 92
        model: sm.panel.vfoStepLabels
        currentIndex: sm.panel.vfoStepIndex(sm.isB
            ? Prefs.vfoStepHzRx2(Prefs.modeRx2)
            : Prefs.vfoStepHz(Prefs.mode))
        onActivated: {
            var hz = sm.panel.vfoStepHzList[currentIndex]
            if (sm.isB) Prefs.setVfoStepHzRx2(Prefs.modeRx2, hz)
            else        Prefs.setVfoStepHz(Prefs.mode, hz)
        }
        fieldColor: sm.theme.field
        fieldBorderColor: sm.theme.fieldBorder
        textColor: sm.theme.text
        accentColor: sm.theme.accent
    }
    Label {
        visible: sm.showMode && !sm.compact
        text: qsTr("Mode")
        color: sm.theme.label
        font.bold: true
    }
    LyraComboBox {
        visible: sm.showMode
        Layout.preferredWidth: sm.compact ? 70 : 78
        model: sm.panel.modeList
        currentIndex: Math.max(0, sm.panel.modeList.indexOf(sm.isB ? Prefs.modeRx2 : Prefs.mode))
        onActivated: {
            if (sm.isB) Prefs.modeRx2 = sm.panel.modeList[currentIndex]
            else        Prefs.mode    = sm.panel.modeList[currentIndex]
        }
        fieldColor: sm.theme.field
        fieldBorderColor: sm.theme.fieldBorder
        textColor: sm.theme.text
        accentColor: sm.theme.accent
    }
    Item { Layout.fillWidth: true }
}
```

- [ ] **Step 5: `src/qml/VfoTxPip.qml`**

```qml
// Lyra — the always-on TX assignment pip on a VFO (locked pile-up model):
// red = will transmit on this VFO (bright while keyed), idle = click to
// assign TX here.  Red is fixed for every style so TX always reads as TX;
// the style themes only the idle look.
import QtQuick
import QtQuick.Controls

Rectangle {
    id: pip
    required property bool active     // TX is assigned to this VFO
    required property bool keyed      // transmitting now
    required property string tipText
    property var theme
    signal assign()

    radius: 3
    color: active ? (keyed ? "#ff4136" : "#8a1f1a") : theme.pipIdle
    border.width: 1
    border.color: active ? "#ff6b63" : theme.pipIdleBorder

    Label {
        anchors.centerIn: parent
        text: qsTr("TX")
        color: pip.active ? "#ffffff" : pip.theme.pipIdleText
        font.pixelSize: 9
        font.bold: true
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: pip.assign()
        ToolTip.visible: containsMouse && Prefs.tooltipsEnabled
        ToolTip.delay: 400
        ToolTip.text: pip.tipText
    }
}
```

- [ ] **Step 6: `src/qml/VfoBox.qml`**

```qml
// Lyra — one VFO (A, or B = SPLIT TX / SUB RX2) in the Tuning panel, drawn
// by the selected VFO style.
//
// The style (VfoStyle*.qml, picked by Prefs.vfoStyle through VfoStyles.js)
// only DRAWS.  Everything that must behave the same in every style lives
// here: the FreqDisplay tuning core over the style's tuneRect, the typed-
// entry field, the shared Step/Mode row and TX pip at the rects the style
// reports, the focus click, and the right-click "VFO style" menu.
//
// Style contract — a style's root item provides:
//   inputs : freqHz, selectedDigit, vfoState ("rx"|"tx"|"armed"), roleText,
//            vfoLetter, modeText, stepHz, showMode
//   outputs: naturalWidth/Height (design size), tuneRect, digitRects (10,
//            index = place, empty = hidden), stepModeRect, txPipRect — all in
//            the style's own coordinates — and theme { label, field,
//            fieldBorder, text, accent, pipIdle, pipIdleBorder, pipIdleText,
//            compact }.
// Styles must not touch Stream / Prefs.  scratch/test_vfo_styles.cpp checks
// every registered style against this contract.
//
// The style is drawn at its natural size and scaled uniformly — never up —
// to fit this box, so a large style on a small panel shrinks, never clips.
import QtQuick
import QtQuick.Controls
import QtQml
import LyraUI
import "VfoStyles.js" as VfoStyles

Item {
    id: box
    required property var panel      // TuningPanel root
    required property bool isB       // false = VFO A, true = VFO B

    readonly property var styleEntry: VfoStyles.resolve(Prefs.vfoStyle)
    readonly property var style: loader.item
    readonly property real naturalWidth: style ? style.naturalWidth : 360
    readonly property real naturalHeight: style ? style.naturalHeight : 108
    readonly property real fitScale:
        Math.min(1, width / naturalWidth, height / naturalHeight)

    readonly property rect noRect: Qt.rect(0, 0, 0, 0)
    readonly property rect tuneRect: style ? style.tuneRect : noRect
    readonly property rect stepModeRect: style ? style.stepModeRect : noRect
    readonly property rect txPipRect: style ? style.txPipRect : noRect
    readonly property var theme: style ? style.theme : ({
        label: "#cccccc", field: "#161e28", fieldBorder: "#2a3a4a",
        text: "#f2f8fc", accent: "#50d0ff",
        pipIdle: "#2a323c", pipIdleBorder: "#5a6570", pipIdleText: "#7a8490",
        compact: false })

    // VFO A shows the RX1 carrier (DDS + CW marker offset); VFO B shows
    // TuningPanel.displayBHz (SPLIT carrier / SUB RX2 / parked B).
    readonly property int freqHz: isB ? panel.displayBHz
                                      : panel.centerHz + WdspEngine.markerOffsetHz
    // TX is assigned here: A in simplex, B in SPLIT.
    readonly property bool txHere: isB ? Stream.splitEnabled : !Stream.splitEnabled
    readonly property string vfoState: isB
        ? ((Stream.splitEnabled && Stream.txDisplayActive) ? "tx"
           : (Stream.subEnabled && Stream.focusedRx === 2) ? "rx" : "armed")
        : ((Stream.txDisplayActive && !Stream.splitEnabled) ? "tx"
           : (Stream.subEnabled && Stream.focusedRx !== 1) ? "armed" : "rx")
    readonly property string roleText: isB
        ? (Stream.splitEnabled ? qsTr("TX") : qsTr("RX"))
        : ((Stream.txDisplayActive && !Stream.splitEnabled) ? qsTr("TX") : qsTr("RX"))

    function commitHz(hz) {
        if (isB) panel.commitBCarrier(hz)
        else     Stream.setRx1FreqHz(hz - WdspEngine.markerOffsetHz)
    }

    implicitWidth: naturalWidth
    implicitHeight: naturalHeight

    Item {
        id: stage
        width: box.naturalWidth
        height: box.naturalHeight
        transformOrigin: Item.TopLeft
        scale: box.fitScale
        x: Math.round((box.width - width * scale) / 2)
        y: Math.round((box.height - height * scale) / 2)

        Loader {
            id: loader
            source: Qt.resolvedUrl(box.styleEntry.file)
            onLoaded: {
                item.freqHz        = Qt.binding(function () { return box.freqHz })
                item.selectedDigit = Qt.binding(function () { return fd.selectedDigit })
                item.vfoState      = Qt.binding(function () { return box.vfoState })
                item.roleText      = Qt.binding(function () { return box.roleText })
                item.vfoLetter     = box.isB ? "B" : "A"
                item.modeText      = Qt.binding(function () {
                    return (box.isB && Stream.subEnabled) ? Prefs.modeRx2 : Prefs.mode })
                item.stepHz        = Qt.binding(function () { return stepMode.stepHz })
                item.showMode      = Qt.binding(function () { return stepMode.showMode })
            }
        }

        FreqDisplay {
            id: fd
            x: box.tuneRect.x
            y: box.tuneRect.y
            width: box.tuneRect.width
            height: box.tuneRect.height
            freqHz: box.freqHz
            externalStepHz: stepMode.stepHz
            digitRects: box.style
                ? box.style.digitRects.map(function (d) {
                      return Qt.rect(d.x - box.tuneRect.x, d.y - box.tuneRect.y,
                                     d.width, d.height) })
                : []
            onFreqEdited: (hz) => box.commitHz(hz)
            onEditRequested: {
                editField.text = (fd.freqHz / 1.0e6).toFixed(6)
                editField.visible = true
                editField.selectAll()
                editField.forceActiveFocus()
            }
        }

        TextField {
            id: editField
            x: fd.x
            y: fd.y
            width: fd.width
            height: fd.height
            visible: false
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            font.family: "Consolas"
            font.pixelSize: 22
            font.bold: true
            color: "#ffb000"
            background: Rectangle {
                color: "#000000"
                border.color: "#00d8ff"
                border.width: 2
                radius: 3
            }
            function commit() {
                if (!visible) return
                var hz = fd.parseFreqInput(text)   // entered carrier
                visible = false
                if (hz >= 0) box.commitHz(hz)
            }
            onAccepted: commit()
            onActiveFocusChanged: if (!activeFocus) commit()
            Keys.onEscapePressed: visible = false
        }

        VfoStepMode {
            id: stepMode
            panel: box.panel
            isB: box.isB
            showMode: box.isB ? Stream.subEnabled : true
            theme: box.theme
            x: box.stepModeRect.x
            y: box.stepModeRect.y
            width: box.stepModeRect.width
            height: box.stepModeRect.height
        }

        VfoTxPip {
            active: box.txHere
            keyed: Stream.txDisplayActive
            theme: box.theme
            tipText: box.txHere
                ? (box.isB ? qsTr("Transmit on VFO B") : qsTr("Transmit on VFO A"))
                : (box.isB ? qsTr("Click to transmit on VFO B (enter SPLIT)")
                           : qsTr("Click to transmit on VFO A (leave SPLIT)"))
            onAssign: box.panel.assignTx(box.isB)
            x: box.txPipRect.x
            y: box.txPipRect.y
            width: box.txPipRect.width
            height: box.txPipRect.height
        }
    }

    // Any left press on the VFO focuses its receiver, then passes through.
    MouseArea {
        anchors.fill: parent
        z: 3
        acceptedButtons: Qt.LeftButton
        onPressed: (mouse) => {
            Stream.setFocusedRx(box.isB ? 2 : 1)
            mouse.accepted = false
        }
    }

    // Right-click → VFO style menu (both VFOs share the choice).
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: styleMenu.popup()
    }
    Menu {
        id: styleMenu
        MenuItem { text: qsTr("VFO style"); enabled: false }
        MenuSeparator {}
    }
    Instantiator {
        model: VfoStyles.styles
        delegate: MenuItem {
            required property var modelData
            text: modelData.name
            checkable: true
            checked: box.styleEntry.id === modelData.id
            onTriggered: Prefs.vfoStyle = modelData.id
        }
        onObjectAdded: (index, object) => styleMenu.insertItem(index + 2, object)
        onObjectRemoved: (index, object) => styleMenu.removeItem(object)
    }
}
```

- [ ] **Step 7: Register the files** — in `CMakeLists.txt` `qt_add_qml_module(lyra … QML_FILES`, after `src/qml/LyraButton.qml` add:

```cmake
        src/qml/VfoBox.qml
        src/qml/VfoDigitRow.qml
        src/qml/VfoStepMode.qml
        src/qml/VfoTxPip.qml
        src/qml/VfoStyles.js
        src/qml/VfoStyleAmber.qml
```

- [ ] **Step 8: Run the contract test to verify it passes**

Run: `cmake --build build --target test_vfo_styles && build/test_vfo_styles`
Expected: every line `PASS` for `amber` at all four frequencies, `ALL PASS`, exit 0. Fix the style, not the test, if a line fails.

- [ ] **Step 9: Build the app** — `cmake --build build -j`, expected exit 0 (qmlcachegen compiles the new files; a QML syntax error fails here).

---

### Task 7: Tuning panel uses VfoBox

**Files:**
- Modify: `src/qml/TuningPanel.qml` (VFO-A column ~343-351, the `vfoA` Rectangle ~351-520, `vfoBSlot` ~659-667, the `vfoB` Rectangle ~669-827)

**Interfaces:**
- Consumes: `VfoBox` (Task 6).
- Produces: ids `vfoBoxA`, `vfoBoxB` (nothing outside TuningPanel references the removed ids `led`, `ledB`, `stepCombo*`, `modeCombo*`, `editField*`, `vfoA`, `vfoB`, `txPip*` — verified by grep during planning).

- [ ] **Step 1: VFO-A column width follows the style.** Replace

```qml
                Layout.preferredWidth: 360
                Layout.maximumWidth: 360
                // A real floor, so the readout can't be crushed to nothing when
                // the row is squeezed — 300px still seats the LED digits and the
                // Step/Mode combos beneath them.
                Layout.minimumWidth: 300
```

with

```qml
                Layout.preferredWidth: vfoBoxA.naturalWidth
                Layout.maximumWidth: vfoBoxA.naturalWidth
                // A real floor, so the readout can't be crushed to nothing when
                // the row is squeezed — 5/6 of the style's width (300 px for the
                // 360 px Amber box); below its natural width the style scales.
                Layout.minimumWidth: Math.round(vfoBoxA.naturalWidth * 5 / 6)
```

- [ ] **Step 2: Replace the whole `vfoA` Rectangle** — from the line `              Rectangle {` whose next line is `                id: vfoA` down to and including its closing `              }` (the line directly above the `// Zero-beat needle strip` comment) — with:

```qml
              VfoBox {
                id: vfoBoxA
                panel: root
                isB: false
                Layout.fillWidth: true
                Layout.preferredHeight: naturalHeight
                Layout.maximumHeight: naturalHeight
              }
```

- [ ] **Step 3: VFO-B slot width follows the style.** In `vfoBSlot`, replace

```qml
                Layout.preferredWidth: reserve ? 360 : 0
                Layout.maximumWidth: reserve ? 360 : 0
                Layout.minimumWidth: (Stream.splitEnabled || Stream.subEnabled) ? 300 : 0
```

with

```qml
                Layout.preferredWidth: reserve ? vfoBoxB.naturalWidth : 0
                Layout.maximumWidth: reserve ? vfoBoxB.naturalWidth : 0
                Layout.minimumWidth: (Stream.splitEnabled || Stream.subEnabled)
                                     ? Math.round(vfoBoxB.naturalWidth * 5 / 6) : 0
```

   and in the comment above it change `But it is 360px` to `But it is a whole VFO wide`.

- [ ] **Step 4: Replace the whole `vfoB` Rectangle** — from `                Rectangle {` whose next line is `                    id: vfoB` down to and including its closing `                }` (the last child of `vfoBSlot`) — with:

```qml
                VfoBox {
                    id: vfoBoxB
                    panel: root
                    isB: true
                    visible: vfoBSlot.reserve
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: Math.min(naturalHeight, parent.height)
                }
```

- [ ] **Step 5: Build** — `cmake --build build -j`, expected exit 0.

- [ ] **Step 6: Run and read the log**

```bash
pkill -x lyra; (build/lyra.app/Contents/MacOS/lyra > "$SCRATCH/lyra.log" 2>&1 &); sleep 10
grep -E "qrc:.*(Error|Warning|ReferenceError|TypeError)|is not a type|Cannot assign" "$SCRATCH/lyra.log"
```

Expected: only the known Qt-internal macOS ScrollView binding-loop lines (present before this work); nothing from `Vfo*.qml` or `TuningPanel.qml`.

---

### Task 8: Tuning dock follows the style's height

**Files:**
- Modify: `src/mainwindow.h` (slot + flag)
- Modify: `src/mainwindow.cpp` (`finishQuickSource` after the minimum-height block ~1349; new slot)

**Interfaces:**
- Consumes: `Prefs::vfoStyleChanged` (Task 3); TuningPanel root `lyraMinHeight` (+ its QML-generated `lyraMinHeightChanged()` notify).
- Produces: `void MainWindow::syncTuningDockHeight()` (private slot).

- [ ] **Step 1: `src/mainwindow.h`** — beside the existing `syncCollapsibleDock()` slot declaration add `void syncTuningDockHeight();`, and beside the other bool members add:

```cpp
    // Set when the VFO style changes; the next Tuning lyraMinHeight change
    // resizes the dock to the new style (grow OR shrink).  Cleared after use
    // or after 1 s, so start-up and other content changes never resize it.
    bool tuningResizePending_ = false;
```

- [ ] **Step 2: `src/mainwindow.cpp`** — confirm `#include <QTimer>` is present (`grep -n "include <QTimer>" src/mainwindow.cpp`; add it with the other Qt includes if not). At the end of `finishQuickSource`, after the `if (root && !collapsible) { … setMinimumHeight(h); }` block, add:

```cpp
    // Tuning: the VFO style decides the panel's height.  Keep the minimum
    // live (a taller style, or the zero-beat strip appearing, must never be
    // clipped), and when the operator picks a new style resize the dock to
    // it — taller or shorter.  Only on a style change, so a saved layout is
    // never overridden at start-up.
    if (root && qmlFile == QLatin1String("TuningPanel.qml")) {
        connect(root, SIGNAL(lyraMinHeightChanged()),
                this, SLOT(syncTuningDockHeight()));
        connect(prefs_, &Prefs::vfoStyleChanged, this, [this]() {
            tuningResizePending_ = true;
            QTimer::singleShot(1000, this, [this]() { tuningResizePending_ = false; });
        });
    }
```

   and add the slot (near `syncCollapsibleDock`):

```cpp
void MainWindow::syncTuningDockHeight() {
    QDockWidget *d = docks_.value(QStringLiteral("tuning"));
    auto *qw = d ? d->findChild<QQuickWidget *>() : nullptr;
    QObject *root = qw ? qw->rootObject() : nullptr;
    if (!root) return;
    const int h = root->property("lyraMinHeight").toInt();
    if (h <= 0) return;
    qw->setMinimumHeight(h);
    if (tuningResizePending_ && !d->isFloating()) {
        tuningResizePending_ = false;
        const int chrome = d->height() - qw->height();   // title bar + frame
        resizeDocks({d}, {h + chrome}, Qt::Vertical);
    }
}
```

   (If `Prefs` is namespaced in this file, use the same spelling `prefs_`'s declaration uses, e.g. `lyra::ui::Prefs::vfoStyleChanged`.)

- [ ] **Step 3: Build** — `cmake --build build -j`, expected exit 0.

- [ ] **Step 4: Floating-dock check (Review Focus 3)** — run Lyra, float the Tuning dock (double-click its title), right-click a VFO → pick Amber LED again (only style so far; it still emits `vfoStyleChanged` if the stored value was `""`). Expected: no crash, main window size unchanged. Re-dock it. Quit and relaunch: the dock comes back at the size it was saved with.

---

### Task 9: Framework verification and commit 1

**Files:** none new.

- [ ] **Step 1: Automated** — `cmake --build build --target test_vfo_digit_hit test_vfo_styles test_wheel_steps && build/test_vfo_digit_hit && build/test_vfo_styles && build/test_wheel_steps` → all `ALL PASS`.

- [ ] **Step 2: Visual parity** — relaunch, capture the window, crop the VFO row and compare side by side with `baseline*.png` (simplex, SPLIT, SUB). Expected: same box size, border colours, digit positions, ghost 8s, units, Step/Mode row, TX pip. Small anti-aliasing differences (QPainter → QML Text) are acceptable; any layout shift is not.

- [ ] **Step 3: Behaviour (Review Focus 1, 2, 4)** — on VFO A and on VFO B (SPLIT, then SUB):
  - click a digit → cyan underline moves there; wheel over a digit tunes that place; wheel over empty readout area tunes by Step;
  - click a digit, then ←/→ move the underline, ↑/↓ tune exactly one step per press;
  - double-click → entry field; `14.2` + Enter tunes to 14.200.000; Esc cancels; clicking elsewhere commits;
  - Step/Mode pickers change step and mode as before (B: RX2 step; Mode visible only in SUB);
  - TX pip: click B's pip → SPLIT on; click A's → SPLIT off; tooltips as before;
  - right-click either VFO → menu "VFO style" with **Amber LED** checked;
  - drag the Tuning dock narrow (~800 px): VFOs shrink, the logo never covers the digits.

- [ ] **Step 4: Other panels unchanged** — glance at Audio, Filters, Display panels: their combos (LyraComboBox) look exactly as before.

- [ ] **Step 5: Quit check** — quit and relaunch three times; no crash, no hang.

- [ ] **Step 6: Report to the operator** (changed vs. verified, with crops) and **wait for approval**. Then commit:

```bash
git add src/vfo_digit_hit.h scratch/test_vfo_digit_hit.cpp scratch/test_vfo_styles.cpp \
        src/freqdisplay.h src/freqdisplay.cpp src/prefs.h src/prefs.cpp \
        src/qml/LyraComboBox.qml src/qml/VfoBox.qml src/qml/VfoDigitRow.qml \
        src/qml/VfoStepMode.qml src/qml/VfoTxPip.qml src/qml/VfoStyles.js \
        src/qml/VfoStyleAmber.qml src/qml/TuningPanel.qml \
        src/mainwindow.h src/mainwindow.cpp CMakeLists.txt
git diff --cached --stat
git commit -F - <<'EOF'
refactor(vfo): split VFO drawing into selectable styles

The VFO readout was painted inside FreqDisplay and the A/B boxes were two
copies of the same QML, so a new look meant forking both.  FreqDisplay is
now only the tuning core (hit-testing digit rects a style reports); a
shared VfoBox hosts the core, typed entry, Step/Mode and TX pip, and loads
the style named by Prefs.vfoStyle.  Amber LED reproduces today's look, so
this commit changes nothing visible; the contract test keeps every future
style honest.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

---

### Task 10: Bundled fonts

**Files:**
- Create: `src/assets/fonts/ShareTechMono-Regular.ttf`, `NixieOne-Regular.ttf`, `Inter-Variable.ttf`, `DSEG7Classic-Bold.ttf`, and licence texts `OFL-ShareTechMono.txt`, `OFL-NixieOne.txt`, `OFL-Inter.txt`, `LICENSE-DSEG.txt`
- Modify: `CMakeLists.txt` (`qt_add_qml_module` `RESOURCES`), `src/main.cpp` (register after `QApplication app(...)`, ~line 478), `scratch/test_vfo_styles.cpp` (font check), `CMakeLists.txt` test target (font dir define)

**Interfaces:**
- Produces: application font families `Share Tech Mono`, `Nixie One`, `Inter`, `DSEG7 Classic` available to all QML.

- [ ] **Step 1: Ask the operator to approve downloading** these files (name, source, approx. size), then download into a fresh scratch folder:

| File | Source |
|---|---|
| ShareTechMono-Regular.ttf + OFL.txt | `https://raw.githubusercontent.com/google/fonts/main/ofl/sharetechmono/` |
| NixieOne-Regular.ttf + OFL.txt | `https://raw.githubusercontent.com/google/fonts/main/ofl/nixieone/` |
| `Inter[opsz,wght].ttf` + OFL.txt | `https://raw.githubusercontent.com/google/fonts/main/ofl/inter/` |
| DSEG7Classic-Bold.ttf + licence | `https://github.com/keshikan/DSEG/releases/download/v0.46/fonts-DSEG_v046.zip` (`DSEG7-Classic/`) |

```bash
set -euo pipefail
F="$SCRATCH/fonts-dl"; mkdir -p "$F"
curl -fsSL -o "$F/ShareTechMono-Regular.ttf" https://raw.githubusercontent.com/google/fonts/main/ofl/sharetechmono/ShareTechMono-Regular.ttf
curl -fsSL -o "$F/OFL-ShareTechMono.txt"     https://raw.githubusercontent.com/google/fonts/main/ofl/sharetechmono/OFL.txt
curl -fsSL -o "$F/NixieOne-Regular.ttf"      https://raw.githubusercontent.com/google/fonts/main/ofl/nixieone/NixieOne-Regular.ttf
curl -fsSL -o "$F/OFL-NixieOne.txt"          https://raw.githubusercontent.com/google/fonts/main/ofl/nixieone/OFL.txt
curl -fsSL -o "$F/Inter-Variable.ttf"        "https://raw.githubusercontent.com/google/fonts/main/ofl/inter/Inter%5Bopsz,wght%5D.ttf"
curl -fsSL -o "$F/OFL-Inter.txt"             https://raw.githubusercontent.com/google/fonts/main/ofl/inter/OFL.txt
curl -fsSL -o "$F/dseg.zip"                  https://github.com/keshikan/DSEG/releases/download/v0.46/fonts-DSEG_v046.zip
unzip -l "$F/dseg.zip" | grep -iE "DSEG7Classic-Bold.ttf|licen" 
```

  Extract `DSEG7Classic-Bold.ttf` and the licence file named in the listing. Verify: `file "$F"/*.ttf` says TrueType for each; each licence text contains "SIL Open Font License". If a URL 404s, find the current path on the same host, tell the operator, and do not substitute a different font without asking.

- [ ] **Step 2: Copy into `src/assets/fonts/`** with the names in **Files** above.

- [ ] **Step 3: Resources** — in `qt_add_qml_module(lyra … RESOURCES` add:

```cmake
        src/assets/fonts/ShareTechMono-Regular.ttf
        src/assets/fonts/NixieOne-Regular.ttf
        src/assets/fonts/Inter-Variable.ttf
        src/assets/fonts/DSEG7Classic-Bold.ttf
```

- [ ] **Step 4: Register in `src/main.cpp`** right after `QApplication app(argc, argv);` (add `#include <QFontDatabase>`):

```cpp
    // VFO-style fonts (src/assets/fonts, all SIL OFL — licences alongside).
    // Bundled so a style looks the same on every machine; registered before
    // any QML loads.
    for (const char *f : {"ShareTechMono-Regular.ttf", "NixieOne-Regular.ttf",
                          "Inter-Variable.ttf", "DSEG7Classic-Bold.ttf"}) {
        const QString path = QStringLiteral(":/qt/qml/Lyra/src/assets/fonts/")
                             + QLatin1String(f);
        if (QFontDatabase::addApplicationFont(path) < 0)
            qWarning("[fonts] failed to load %s", f);
    }
```

- [ ] **Step 5: Font check in the contract test** — add `target_compile_definitions(test_vfo_styles PRIVATE LYRA_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/assets/fonts")` and, in `main()` right after `QGuiApplication app`, register every `*.ttf` in `LYRA_FONT_DIR` and check the families:

```cpp
#ifdef LYRA_FONT_DIR
    {
        QDir fontDir(QStringLiteral(LYRA_FONT_DIR));
        for (const QString &f : fontDir.entryList({QStringLiteral("*.ttf")}, QDir::Files))
            check(QStringLiteral("font loads: ") + f,
                  QFontDatabase::addApplicationFont(fontDir.filePath(f)) >= 0);
        const QStringList fams = QFontDatabase::families();
        for (const char *fam : {"Share Tech Mono", "Nixie One", "Inter", "DSEG7 Classic"})
            check(QStringLiteral("font family present: ") + QLatin1String(fam),
                  fams.contains(QLatin1String(fam)));
    }
#endif
```

   If a family check fails, print `QFontDatabase::applicationFontFamilies(id)` for that file, use the real family name in the test and in the style that needs it, and note it in the report.

- [ ] **Step 6: Verify** — `cmake --build build -j && cmake --build build --target test_vfo_styles && build/test_vfo_styles` → `ALL PASS`; launch Lyra and `grep "\[fonts\]" "$SCRATCH/lyra.log"` → nothing. Note for the operator: Amber LED now renders in Share Tech Mono (its intended font — it previously fell back to Courier New), so it will look slightly different from the Task 0 baseline.

---

### Task 11: Lyra (Mac) style — the new default

**Files:**
- Create: `src/qml/VfoStyleLyraMac.qml`
- Modify: `src/qml/VfoStyles.js` (entry + `defaultId`), `CMakeLists.txt` `QML_FILES`

**Interfaces:**
- Consumes: `VfoDigitRow`, style contract.

- [ ] **Step 1: Register it first so the test fails** — in `VfoStyles.js` set `var defaultId = "lyraMac"` and make `styles`:

```js
var styles = [
    { id: "lyraMac", name: "Lyra (Mac)", group: "Modern",  file: "VfoStyleLyraMac.qml" },
    { id: "amber",   name: "Amber LED",  group: "Classic", file: "VfoStyleAmber.qml" }
]
```

  Run `build/test_vfo_styles` → expect `lyraMac … loads FAIL` (file missing).

- [ ] **Step 2: Write `src/qml/VfoStyleLyraMac.qml`**

```qml
// Lyra VFO style — Lyra (Mac).  The default.
//
// A raised glass panel in the Lyra palette: blue-slate gradient body, soft
// top highlight and bottom shade for depth, a thin rim in the RX/TX/armed
// colour; thin system-font digits (SF on macOS, Inter elsewhere) with a
// faint cyan halo and cyan decimal points; the Lyra constellation (Vega and
// the parallelogram) etched top-right.  Style contract: see VfoBox.qml.
import QtQuick
import QtQuick.Effects

Item {
    id: st
    property int freqHz: 0
    property int selectedDigit: -1
    property string vfoState: "rx"
    property string roleText: "RX"
    property string vfoLetter: "A"
    property string modeText: ""
    property int stepHz: 0
    property bool showMode: true

    readonly property real naturalWidth: 380
    readonly property real naturalHeight: 140
    readonly property rect tuneRect: Qt.rect(10, 26, 360, 64)
    readonly property rect stepModeRect: Qt.rect(10, 106, 322, 26)
    readonly property rect txPipRect: Qt.rect(342, 111, 28, 16)
    readonly property var digitRects: digits.rects.map(function (r) {
        return Qt.rect(tuneRect.x + digits.x + r.x, tuneRect.y + digits.y + r.y,
                       r.width, r.height)
    })
    readonly property var theme: ({
        label: "#c5d6e6", field: "#1b2a3a", fieldBorder: "#3a5a78",
        text: "#eaf4ff", accent: "#00e5ff",
        pipIdle: "#22303e", pipIdleBorder: "#4a6078", pipIdleText: "#8aa0b4",
        compact: false
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    readonly property bool isMac: Qt.platform.os === "osx" || Qt.platform.os === "macos"
    readonly property string uiFamily: isMac ? Qt.application.font.family : "Inter"
    readonly property color stateColor: vfoState === "tx" ? "#ff4136"
                                      : vfoState === "armed" ? "#3a4250" : "#34c759"

    width: naturalWidth
    height: naturalHeight

    Rectangle {   // body
        anchors.fill: parent
        radius: 14
        gradient: Gradient {
            GradientStop { position: 0.0;  color: "#1d3346" }
            GradientStop { position: 0.55; color: "#0f1a26" }
            GradientStop { position: 1.0;  color: "#0a1119" }
        }
        border.width: 2
        border.color: Qt.rgba(st.stateColor.r, st.stateColor.g, st.stateColor.b, 0.75)
    }
    Rectangle {   // top glass highlight
        x: 8; y: 5
        width: parent.width - 16
        height: 40
        radius: 10
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.10) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }
    Rectangle {   // bottom inner shade — depth
        x: 2
        y: parent.height - 24
        width: parent.width - 4
        height: 22
        radius: 12
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.0) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.35) }
        }
    }

    Canvas {   // the Lyra constellation — static, painted once per size
        x: parent.width - 66
        y: 4
        width: 56
        height: 53
        opacity: 0.5
        onPaint: {
            var c = getContext("2d")
            c.reset()
            c.scale(0.75, 0.75)
            // Vega, ε, ζ, δ, γ (Sulafat), β (Sheliak) …
            var stars = [[12, 8, 3.4], [22, 4, 1.3], [26, 18, 1.6], [36, 30, 1.6],
                         [58, 36, 1.5], [62, 62, 2.0], [40, 58, 2.0]]
            var lines = [[0, 2], [0, 1], [2, 3], [3, 4], [4, 5], [5, 6], [6, 3]]
            c.strokeStyle = "rgba(0,229,255,0.55)"
            c.lineWidth = 1
            c.beginPath()
            for (var i = 0; i < lines.length; ++i) {
                var a = stars[lines[i][0]], b = stars[lines[i][1]]
                c.moveTo(a[0], a[1])
                c.lineTo(b[0], b[1])
            }
            c.stroke()
            c.fillStyle = "#cfefff"
            for (var j = 0; j < stars.length; ++j) {
                c.beginPath()
                c.arc(stars[j][0], stars[j][1], stars[j][2], 0, 2 * Math.PI)
                c.fill()
            }
        }
    }

    Text {   // role tag
        x: 16; y: 8
        text: "● " + st.roleText
        color: st.vfoState === "tx" ? "#ff6b5e" : "#00e5ff"
        font.family: st.uiFamily
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 1
    }

    Item {
        id: area
        x: st.tuneRect.x
        y: st.tuneRect.y
        width: st.tuneRect.width
        height: st.tuneRect.height

        VfoDigitRow {
            id: digits
            visible: false               // drawn through the halo effect below
            digitString: st.digitString
            digitFont: Qt.font({ family: st.uiFamily, pixelSize: 48, weight: Font.Light })
            digitColor: "#f2f8ff"
            dimColor: Qt.rgba(242 / 255, 248 / 255, 1, 0.13)
            sepColor: "#00e5ff"
            selectedDigit: st.selectedDigit
            selectColor: "#00e5ff"
            selectThickness: 2
            centerGlyphs: true
            x: (area.width - implicitWidth) / 2
            y: (area.height - cellH) / 2
        }
        MultiEffect {
            source: digits
            anchors.fill: digits
            shadowEnabled: true
            shadowColor: "#00e5ff"
            shadowOpacity: 0.45
            shadowBlur: 0.6
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0
            blurMax: 16
        }
    }

    Repeater {   // MHz / kHz / Hz, under the readout
        model: ["MHz", "kHz", "Hz"]
        Text {
            required property int index
            required property string modelData
            text: modelData
            color: "#7fb3d0"
            font.family: st.uiFamily
            font.pixelSize: 11
            font.weight: Font.Medium
            font.letterSpacing: 1
            x: area.x + digits.x + digits.groupCenters[index] - implicitWidth / 2
            y: st.tuneRect.y + st.tuneRect.height
        }
    }
}
```

- [ ] **Step 3: Register** — add `src/qml/VfoStyleLyraMac.qml` to `QML_FILES` after `VfoStyleAmber.qml`.

- [ ] **Step 4: Test** — `cmake --build build --target test_vfo_styles && build/test_vfo_styles` → `ALL PASS`. Then `cmake --build build -j`.

- [ ] **Step 5: Look at it** — relaunch (stored style `""` → now Lyra (Mac)); capture and crop the VFO row; check digits are centred, the constellation sits top-right behind the Hz digits faintly, the halo is subtle, the rim is green on RX. Check the panadapter and waterfall render cleanly (no patchy waterfall — the Shapes failure mode) with this style active.

---

### Task 12: Classic LCD style

**Files:**
- Create: `src/qml/VfoStyleClassicLcd.qml`
- Modify: `src/qml/VfoStyles.js`, `CMakeLists.txt` `QML_FILES`

- [ ] **Step 1: Register** — append to `styles`: `{ id: "classicLcd", name: "Classic LCD", group: "Classic", file: "VfoStyleClassicLcd.qml" }`. Run the contract test → expect `classicLcd … loads FAIL`.

- [ ] **Step 2: Write `src/qml/VfoStyleClassicLcd.qml`**

```qml
// Lyra VFO style — Classic LCD.
//
// Backlit grey-green glass set in a dark bezel, black 7-segment digits
// (DSEG7 Classic) with faint unlit segments — the older Yaesu/Kenwood look.
// The bezel rim carries the RX/TX/armed colour.  Style contract: VfoBox.qml.
import QtQuick

Item {
    id: st
    property int freqHz: 0
    property int selectedDigit: -1
    property string vfoState: "rx"
    property string roleText: "RX"
    property string vfoLetter: "A"
    property string modeText: ""
    property int stepHz: 0
    property bool showMode: true

    readonly property real naturalWidth: 360
    readonly property real naturalHeight: 118
    readonly property rect tuneRect: Qt.rect(14, 28, 332, 40)
    readonly property rect stepModeRect: Qt.rect(8, 88, 344, 24)
    readonly property rect txPipRect: Qt.rect(316, 12, 28, 14)
    readonly property var digitRects: digits.rects.map(function (r) {
        return Qt.rect(tuneRect.x + digits.x + r.x, tuneRect.y + digits.y + r.y,
                       r.width, r.height)
    })
    readonly property var theme: ({
        label: "#c9ccd0", field: "#3a3e43", fieldBorder: "#555a60",
        text: "#e8eaec", accent: "#b9c79f",
        pipIdle: "#3a3e43", pipIdleBorder: "#5a5e63", pipIdleText: "#9a9ea3",
        compact: false
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    readonly property color ink: "#1f2618"
    readonly property color unlit: Qt.rgba(31 / 255, 38 / 255, 24 / 255, 0.10)

    width: naturalWidth
    height: naturalHeight

    Rectangle {   // bezel
        anchors.fill: parent
        radius: 8
        color: "#2a2d30"
        border.width: 2
        border.color: st.vfoState === "tx" ? "#ff4136"
                    : st.vfoState === "armed" ? "#3a4250" : "#34c759"
    }
    Rectangle {   // backlit glass
        x: 8; y: 8
        width: parent.width - 16
        height: 74
        radius: 4
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#b9c79f" }
            GradientStop { position: 0.6; color: "#a3b387" }
            GradientStop { position: 1.0; color: "#95a679" }
        }
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.4)
    }
    Rectangle {   // glass inner shadow (top)
        x: 9; y: 9
        width: parent.width - 18
        height: 8
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.25) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
        }
    }

    Text {
        x: 16; y: 12
        text: "VFO-" + st.vfoLetter + "   " + st.roleText
        color: "#2c3622"
        font.family: "Arial"
        font.pixelSize: 10
        font.bold: true
    }

    Item {
        id: area
        x: st.tuneRect.x
        y: st.tuneRect.y
        width: st.tuneRect.width
        height: st.tuneRect.height

        VfoDigitRow {
            id: digits
            digitString: st.digitString
            digitFont: Qt.font({ family: "DSEG7 Classic", pixelSize: 30, weight: Font.Bold })
            digitColor: st.ink
            dimColor: st.unlit
            ghostText: "8"
            ghostColor: st.unlit
            sepColor: st.ink
            selectedDigit: st.selectedDigit
            selectColor: "#2c3622"
            selectThickness: 2
            x: (area.width - implicitWidth) / 2
            y: (area.height - cellH) / 2
        }
    }

    Repeater {
        model: ["MHz", "kHz", "Hz"]
        Text {
            required property int index
            required property string modelData
            text: modelData
            color: "#2c3622"
            font.family: "Arial"
            font.pixelSize: 9
            x: area.x + digits.x + digits.groupCenters[index] - implicitWidth / 2
            y: st.tuneRect.y + st.tuneRect.height
        }
    }
}
```

- [ ] **Step 3: Register** in `QML_FILES`; **Test** → `ALL PASS`; build; **look**: digits in 7-segment, faint ghost 8s, decimal points sit at the bottom-right of the 4th and 7th digits. If DSEG's `.` renders mid-cell, set `sepText: ""` and draw the two points as small `Rectangle`s at `digits.cellX(4)` / `cellX(7)` minus 4 px, baseline-aligned — re-run the test.

---

### Task 13: Nixie style

**Files:**
- Create: `src/qml/VfoStyleNixie.qml`
- Modify: `src/qml/VfoStyles.js`, `CMakeLists.txt` `QML_FILES`

- [ ] **Step 1: Register** — append `{ id: "nixie", name: "Nixie", group: "Classic", file: "VfoStyleNixie.qml" }`; test → `nixie … loads FAIL`.

- [ ] **Step 2: Write `src/qml/VfoStyleNixie.qml`**

```qml
// Lyra VFO style — Nixie.
//
// Each digit in its own glass tube with a warm orange glow; unlit leading
// tubes stay dark.  Serif brass labels on a fine-mesh background; the rim
// carries the RX/TX/armed colour.  Style contract: see VfoBox.qml.
import QtQuick
import QtQuick.Effects

Item {
    id: st
    property int freqHz: 0
    property int selectedDigit: -1
    property string vfoState: "rx"
    property string roleText: "RX"
    property string vfoLetter: "A"
    property string modeText: ""
    property int stepHz: 0
    property bool showMode: true

    readonly property real naturalWidth: 420
    readonly property real naturalHeight: 134
    readonly property rect tuneRect: Qt.rect(30, 26, 360, 70)
    readonly property rect stepModeRect: Qt.rect(14, 104, 392, 24)
    readonly property rect txPipRect: Qt.rect(378, 6, 28, 15)
    readonly property var theme: ({
        label: "#d9b48a", field: "#21180f", fieldBorder: "#4a3522",
        text: "#f0d6b4", accent: "#ff8a2b",
        pipIdle: "#21180f", pipIdleBorder: "#4a3522", pipIdleText: "#8a6a4a",
        compact: false
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    readonly property real tubeW: 30
    readonly property real tubeH: 64
    readonly property real tubeY: 28
    readonly property real x0: Math.round((naturalWidth - (9 * 33 + 30 + 22)) / 2)
    function tubeX(i) { return x0 + i * 33 + (i > 3 ? 11 : 0) + (i > 6 ? 11 : 0) }
    function leadingZero(i) {
        if (9 - i < 6) return false
        for (var j = 0; j <= i; ++j)
            if (digitString.charAt(j) !== "0") return false
        return true
    }
    readonly property var digitRects: {
        var a = []
        for (var place = 0; place < 10; ++place)
            a.push(Qt.rect(tubeX(9 - place), tubeY, tubeW, tubeH))
        return a
    }

    width: naturalWidth
    height: naturalHeight

    Rectangle {   // case
        anchors.fill: parent
        radius: 8
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1a1410" }
            GradientStop { position: 1.0; color: "#0b0806" }
        }
        border.width: 2
        border.color: st.vfoState === "tx" ? "#ff4136"
                    : st.vfoState === "armed" ? "#3a4250" : "#34c759"
    }
    Canvas {   // fine mesh — static
        anchors.fill: parent
        anchors.margins: 3
        onPaint: {
            var c = getContext("2d")
            c.reset()
            c.fillStyle = "rgba(255,255,255,0.04)"
            for (var y = 2; y < height; y += 5)
                for (var x = 2; x < width; x += 5)
                    c.fillRect(x, y, 1, 1)
        }
    }

    Repeater {   // tube glass
        model: 10
        Rectangle {
            required property int index
            x: st.tubeX(index)
            y: st.tubeY
            width: st.tubeW
            height: st.tubeH
            topLeftRadius: 15
            topRightRadius: 15
            bottomLeftRadius: 6
            bottomRightRadius: 6
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.07) }
                GradientStop { position: 0.4; color: Qt.rgba(1, 1, 1, 0.0) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.04) }
            }
            border.width: 1
            border.color: Qt.rgba(1, 200 / 255, 150 / 255, 0.18)
        }
    }

    Item {   // lit cathodes — drawn through the glow below
        id: glyphs
        anchors.fill: parent
        visible: false
        Repeater {
            model: 10
            Text {
                required property int index
                x: st.tubeX(index)
                y: st.tubeY
                width: st.tubeW
                height: st.tubeH
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: st.digitString.charAt(index)
                font.family: "Nixie One"
                font.pixelSize: 44
                color: st.leadingZero(index) ? Qt.rgba(1, 138 / 255, 43 / 255, 0.07) : "#ff8a2b"
            }
        }
        Repeater {
            model: [3, 6]
            Text {
                required property int modelData
                x: st.tubeX(modelData) + st.tubeW + 1
                y: st.tubeY + st.tubeH - 22
                text: "."
                font.family: "Nixie One"
                font.pixelSize: 22
                color: "#ff8a2b"
            }
        }
    }
    MultiEffect {
        source: glyphs
        anchors.fill: glyphs
        shadowEnabled: true
        shadowColor: "#ff6a00"
        shadowOpacity: 0.9
        shadowBlur: 0.7
        shadowHorizontalOffset: 0
        shadowVerticalOffset: 0
        blurMax: 24
    }

    Rectangle {   // selected-digit underline
        visible: st.selectedDigit >= 0
        x: st.tubeX(9 - st.selectedDigit) + 3
        y: st.tubeY + st.tubeH + 2
        width: st.tubeW - 6
        height: 2
        color: "#ffb066"
    }

    Repeater {
        model: [{ t: "MHz", a: 0, b: 3 }, { t: "kHz", a: 4, b: 6 }, { t: "Hz", a: 7, b: 9 }]
        Text {
            required property var modelData
            text: modelData.t
            color: "#b08a63"
            font.family: "Georgia"
            font.pixelSize: 11
            font.letterSpacing: 1
            x: (st.tubeX(modelData.a) + st.tubeX(modelData.b) + st.tubeW) / 2 - implicitWidth / 2
            y: st.tubeY + st.tubeH + 6
        }
    }

    Text {
        x: 14; y: 6
        text: st.roleText
        color: st.vfoState === "tx" ? "#ff5a3a" : "#d9a066"
        font.family: "Georgia"
        font.pixelSize: 12
        font.bold: true
    }
}
```

- [ ] **Step 3: Register** in `QML_FILES`; **Test** → `ALL PASS`; build; **look**: glow is warm but digits remain crisp, leading tubes dark, underline under the selected tube, units below. Waterfall still clean with Nixie active.

---

### Task 14: Widget style (square, stacked)

**Files:**
- Create: `src/qml/VfoStyleWidget.qml`
- Modify: `src/qml/VfoStyles.js`, `CMakeLists.txt` `QML_FILES`

- [ ] **Step 1: Register** — append `{ id: "widget", name: "Widget", group: "Modern", file: "VfoStyleWidget.qml" }`; test → `widget … loads FAIL`.

- [ ] **Step 2: Write `src/qml/VfoStyleWidget.qml`**

```qml
// Lyra VFO style — Widget.
//
// A macOS/iOS-widget tile: frosted rounded square with the frequency
// stacked — MHz small (no leading zeros), kHz huge, Hz in cyan.  Hidden MHz
// places report empty rects (can't be clicked).  Compact Step/Mode.
// Style contract: see VfoBox.qml.
import QtQuick

Item {
    id: st
    property int freqHz: 0
    property int selectedDigit: -1
    property string vfoState: "rx"
    property string roleText: "RX"
    property string vfoLetter: "A"
    property string modeText: ""
    property int stepHz: 0
    property bool showMode: true

    readonly property real naturalWidth: 230
    readonly property real naturalHeight: 230
    readonly property rect tuneRect: Qt.rect(10, 36, 210, 148)
    readonly property rect stepModeRect: Qt.rect(14, 192, 202, 26)
    readonly property rect txPipRect: Qt.rect(188, 13, 28, 15)
    readonly property var theme: ({
        label: "#cfe6ff", field: "#1d3248", fieldBorder: "#3f6a90",
        text: "#eaf4ff", accent: "#00e5ff",
        pipIdle: "#1d3248", pipIdleBorder: "#4a7090", pipIdleText: "#8fb0cc",
        compact: true
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    readonly property bool isMac: Qt.platform.os === "osx" || Qt.platform.os === "macos"
    readonly property string uiFamily: isMac ? Qt.application.font.family : "Inter"
    readonly property color stateColor: vfoState === "tx" ? "#ff4136"
                                      : vfoState === "armed" ? "#3a4250" : "#34c759"

    readonly property string mhzText: String(Math.floor(freqHz / 1000000))  // "0" below 1 MHz
    FontMetrics { id: fmM; font: mhz.font }
    FontMetrics { id: fmK; font: khz.font }
    FontMetrics { id: fmH; font: hzDigits.font }
    readonly property real mW: fmM.advanceWidth("0")
    readonly property real kW: fmK.advanceWidth("0")
    readonly property real hW: fmH.advanceWidth("0")

    readonly property var digitRects: {
        var a = []
        for (var p = 0; p < 3; ++p)        // Hz: places 0..2, left→right = 2,1,0
            a.push(Qt.rect(hzDigits.x + (2 - p) * hW, hzDigits.y, hW, fmH.height))
        for (var q = 0; q < 3; ++q)        // kHz: places 3..5
            a.push(Qt.rect(khz.x + (2 - q) * kW, khz.y, kW, fmK.height))
        for (var k = 0; k < 4; ++k) {      // MHz: places 6..9, only shown digits
            if (k < mhzText.length)
                a.push(Qt.rect(mhz.x + (mhzText.length - 1 - k) * mW, mhz.y, mW, fmM.height))
            else
                a.push(Qt.rect(0, 0, 0, 0))
        }
        return a
    }

    width: naturalWidth
    height: naturalHeight

    Rectangle {
        anchors.fill: parent
        radius: 28
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#284664" }
            GradientStop { position: 1.0; color: "#0e1824" }
        }
        border.width: 2
        border.color: Qt.rgba(st.stateColor.r, st.stateColor.g, st.stateColor.b, 0.8)
    }
    Rectangle {   // top sheen
        x: 10; y: 4
        width: parent.width - 20
        height: 50
        radius: 22
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.12) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }

    Text {
        x: 20; y: 14
        text: qsTr("VFO ") + st.vfoLetter
        color: "#8fd0ff"
        font.family: st.uiFamily
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }
    Text {
        x: st.txPipRect.x - 8 - implicitWidth
        y: 14
        text: "● " + st.roleText
        color: st.vfoState === "tx" ? "#ff6b5e" : "#8fd0ff"
        font.family: st.uiFamily
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }

    Text {   // MHz
        id: mhz
        x: 20; y: 40
        text: st.mhzText + "."
        color: "#cfe6ff"
        font.family: st.uiFamily
        font.pixelSize: 30
    }
    Row {    // kHz — one centred cell per digit (proportional figures)
        id: khz
        property font font: Qt.font({ family: st.uiFamily, pixelSize: 86,
                                      weight: Font.ExtraLight })
        x: 16; y: 66
        Repeater {
            model: 3
            Text {
                required property int index
                width: st.kW
                horizontalAlignment: Text.AlignHCenter
                text: st.digitString.charAt(4 + index)
                color: "#ffffff"
                font: khz.font
            }
        }
    }
    Text {
        x: 20; y: 154
        text: "."
        color: "#00e5ff"
        font: hzDigits.font
    }
    Row {    // Hz digits
        id: hzDigits
        property font font: Qt.font({ family: st.uiFamily, pixelSize: 18 })
        x: 20 + fmH.advanceWidth(".")
        y: 154
        Repeater {
            model: 3
            Text {
                required property int index
                width: st.hW
                horizontalAlignment: Text.AlignHCenter
                text: st.digitString.charAt(7 + index)
                color: "#00e5ff"
                font: hzDigits.font
            }
        }
    }
    Text {
        x: hzDigits.x + 3 * st.hW + 4
        y: 154
        text: "Hz"
        color: "#00e5ff"
        font: hzDigits.font
    }

    Rectangle {   // selected-digit underline (on whichever row holds it)
        readonly property rect r: st.selectedDigit >= 0
                                  ? st.digitRects[st.selectedDigit] : Qt.rect(0, 0, 0, 0)
        visible: r.width > 0
        x: r.x + 2
        y: r.y + r.height - 2
        width: Math.max(0, r.width - 4)
        height: 2
        color: "#00e5ff"
    }
}
```

  Note: `khz` and `hzDigits` are `Row`s, but `digitRects` only uses their `x`/`y` (set explicitly) and the FontMetrics cell width — never positioner output — so the headless test geometry is exact.

- [ ] **Step 3: Register** in `QML_FILES`; **Test** → `ALL PASS` (the 0 Hz / 2.147 GHz cases exercise the hidden-MHz-place rule); build; **look**: switching to Widget makes the Tuning dock taller (Task 8) and back to Lyra (Mac) shorter; stacked digits read clearly; compact Step/Mode fit; clicking each kHz/Hz digit selects it.

---

### Task 15: Aluminum style (light)

**Files:**
- Create: `src/qml/VfoStyleAluminum.qml`
- Modify: `src/qml/VfoStyles.js`, `CMakeLists.txt` `QML_FILES`

- [ ] **Step 1: Register** — append `{ id: "aluminum", name: "Aluminum", group: "Modern", file: "VfoStyleAluminum.qml" }`; test → `aluminum … loads FAIL`.

- [ ] **Step 2: Write `src/qml/VfoStyleAluminum.qml`**

```qml
// Lyra VFO style — Aluminum.
//
// The light-mode partner of Lyra (Mac): brushed-silver body, white inset
// well, dark thin digits, blue decimal points and selection, an RX/TX pill
// in the state colour, compact pill pickers.  Style contract: VfoBox.qml.
import QtQuick

Item {
    id: st
    property int freqHz: 0
    property int selectedDigit: -1
    property string vfoState: "rx"
    property string roleText: "RX"
    property string vfoLetter: "A"
    property string modeText: ""
    property int stepHz: 0
    property bool showMode: true

    readonly property real naturalWidth: 380
    readonly property real naturalHeight: 128
    readonly property rect tuneRect: Qt.rect(14, 12, 352, 64)
    readonly property rect stepModeRect: Qt.rect(64, 87, 264, 26)
    readonly property rect txPipRect: Qt.rect(338, 92, 28, 16)
    readonly property var digitRects: digits.rects.map(function (r) {
        return Qt.rect(tuneRect.x + digits.x + r.x, tuneRect.y + digits.y + r.y,
                       r.width, r.height)
    })
    readonly property var theme: ({
        label: "#3a3a3c", field: "#ffffff", fieldBorder: "#c4c8ce",
        text: "#1d1d1f", accent: "#0a84ff",
        pipIdle: "#ffffff", pipIdleBorder: "#ffb3ae", pipIdleText: "#ff3b30",
        compact: true
    })

    readonly property string digitString: ("0000000000" + freqHz).slice(-10)
    readonly property bool isMac: Qt.platform.os === "osx" || Qt.platform.os === "macos"
    readonly property string uiFamily: isMac ? Qt.application.font.family : "Inter"
    readonly property color stateColor: vfoState === "tx" ? "#ff3b30"
                                      : vfoState === "armed" ? "#8e8e93" : "#34c759"

    width: naturalWidth
    height: naturalHeight

    Rectangle {   // body
        anchors.fill: parent
        radius: 18
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#f4f5f7" }
            GradientStop { position: 1.0; color: "#d9dce1" }
        }
        border.width: 2
        border.color: st.stateColor
    }
    Rectangle {   // top edge highlight
        x: 14; y: 2
        width: parent.width - 28
        height: 1
        color: Qt.rgba(1, 1, 1, 0.9)
    }
    Rectangle {   // inset well
        x: st.tuneRect.x; y: st.tuneRect.y
        width: st.tuneRect.width; height: st.tuneRect.height
        radius: 12
        color: "#fbfbfc"
        border.width: 1
        border.color: "#c9cdd3"
    }
    Rectangle {   // well inner shadow
        x: st.tuneRect.x + 1; y: st.tuneRect.y + 1
        width: st.tuneRect.width - 2
        height: 7
        radius: 11
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.10) }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.0) }
        }
    }

    Item {
        id: area
        x: st.tuneRect.x
        y: st.tuneRect.y
        width: st.tuneRect.width
        height: st.tuneRect.height
        VfoDigitRow {
            id: digits
            digitString: st.digitString
            digitFont: Qt.font({ family: st.uiFamily, pixelSize: 44, weight: Font.Light })
            digitColor: "#1d1d1f"
            dimColor: "#c4c7cc"
            sepColor: "#0a84ff"
            selectedDigit: st.selectedDigit
            selectColor: "#0a84ff"
            selectThickness: 2
            centerGlyphs: true
            x: (area.width - implicitWidth) / 2
            y: (area.height - cellH) / 2
        }
    }

    Rectangle {   // RX / TX pill
        x: 14; y: 88
        width: 44; height: 24
        radius: 12
        color: st.stateColor
        Text {
            anchors.centerIn: parent
            text: st.roleText
            color: "#ffffff"
            font.family: st.uiFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
    }
}
```

- [ ] **Step 3: Register** in `QML_FILES`; **Test** → `ALL PASS`; build; **look**: readable dark digits on white, blue points, white pill pickers with dark text, RX pill green; double-click entry field (dark, cyan border) is readable over the light well.

---

### Task 16: Phase 1 verification and commit 2

- [ ] **Step 1: Automated** — `cmake --build build --target test_vfo_digit_hit test_vfo_styles test_wheel_steps && build/test_vfo_digit_hit && build/test_vfo_styles && build/test_wheel_steps` → all `ALL PASS`; `cmake --build build -j` → exit 0.

- [ ] **Step 2: Each style, live** — for Lyra (Mac), Amber LED, Classic LCD, Nixie, Widget, Aluminum: right-click → select; capture + crop VFO A, and VFO B in SPLIT and in SUB; check RX/armed rims; click/wheel/←→/↑↓ on a digit; double-click entry (Enter / Esc / click-away); Step/Mode pickers; TX pip; narrow panel (~800 px) — scales, no overlap; dock height follows the style (Widget tallest).

- [ ] **Step 3: Persistence** — pick Nixie, quit, relaunch → Nixie is still selected. The fallback for a missing or unknown id is covered by the contract test (`resolve("")`, `resolve("no-such-style")`). If the operator never picked a style in commit 1's build, the first launch of this build shows Lyra (Mac).

- [ ] **Step 4: Rendering** — with Lyra (Mac) and Nixie active, watch the panadapter + waterfall for 30 s: no patchy/blocky waterfall, no flicker. Log: no `qrc:` errors from `Vfo*.qml`; `[fonts]` silent.

- [ ] **Step 5: Quit check** — quit/relaunch three times; no crash.

- [ ] **Step 6: Operator TX check** — ask the operator to key into a dummy load on Lyra (Mac) and on Aluminum: rims/pills turn red, TX pip bright red.

- [ ] **Step 7: Report** (changed vs. verified, crops of all six) and **wait for approval**, then:

```bash
git add src/assets/fonts src/main.cpp src/qml/VfoStyles.js src/qml/VfoStyleLyraMac.qml \
        src/qml/VfoStyleClassicLcd.qml src/qml/VfoStyleNixie.qml src/qml/VfoStyleWidget.qml \
        src/qml/VfoStyleAluminum.qml scratch/test_vfo_styles.cpp CMakeLists.txt
git diff --cached --stat
git commit -F - <<'EOF'
feat(vfo): add Lyra (Mac), Classic LCD, Nixie, Widget and Aluminum styles

Operator asked for a new VFO look with selectable presets.  Lyra (Mac)
becomes the default; the others cover classic (LCD, Nixie), compact
(Widget) and light (Aluminum) tastes.  Fonts are bundled (SIL OFL,
licences alongside) so each style looks the same on every machine.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
```

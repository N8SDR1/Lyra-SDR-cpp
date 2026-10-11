# Selectable VFO Styles — Tuning Panel

**Status:** Phase 1 implemented (framework, Lyra (Mac), Amber LED, Classic LCD, Nixie, VFD, Widget); Phases 2–3 pending
**Branch context:** `macos-port` (Qt 6.11 / macOS arm64; must stay buildable on Linux/Windows)
**Author:** Brent Crier (N9BC)

## 1. Summary

Replace the single amber-LED VFO readout with a set of operator-selectable
**VFO styles** (presets). Each style restyles the *whole* VFO box — digits,
background, border, RX/TX tags, unit labels and the Step/Mode controls — for
both VFO A and VFO B together. Styles may have entirely different shapes and
sizes (wide strip, round gauge, square tile); the Tuning panel resizes to fit
the chosen style. The style is picked from a right-click menu on either VFO
and persists across restarts. **Lyra (Mac)** is the default.

Twenty styles are approved, delivered in three phases. This spec fully
defines the framework and Phase 1, and scopes Phases 2–3.

## 2. Goals and non-goals

**Goals**
- Every style tunes identically: digit click-select, wheel (incl. the macOS
  one-notch fix in `wheel_steps.h`), arrow keys, double-click typed entry.
- A style can never break tuning, Step/Mode, or TX assignment — those
  behaviours live in shared code, styles only draw and position.
- Adding a style = one new QML file + one registry line.
- The panel never clips a style; on small screens the style scales down.
- One-time refactor removes the duplicated VFO A / VFO B box code.

**Non-goals (YAGNI)**
- No per-VFO style choice (A and B always match).
- No user-editable/custom styles or colour pickers.
- No changes to tuning maths, Stream/Prefs frequency wiring, SPLIT/SUB logic,
  the zero-beat strip, the logo, or the button row.
- No animation, live S-meter, spectrum, map, or band-plan data in Phase 1.

## 3. Approved style list

| # | Style | Phase | Notes |
|---|---|---|---|
| 1 | Amber LED | 1 | Port of today's look (ghost 8s at the darkened 24,16,6) |
| 2 | **Lyra (Mac)** — default | 1 | Glassy raised panel, top highlight, thin SF digits with cyan halo, cyan decimal points, Lyra constellation top-right |
| 5 | Classic LCD | 1 | Grey-green backlit glass in dark bezel, DSEG7 digits, faint unlit segments |
| 6 | Nixie | 1 | One glass tube per digit, warm orange glow, unlit tubes dark |
| 17 | Widget | 1 | Square ~230×230 tile, stacked MHz / big kHz / cyan Hz |
| 10 | Aluminum | 1 | Light silver body, white inset well, dark digits, blue points, pill controls |
| 7 | Boatanchor dial | 2 | Cream slide-rule dial scrolls with tuning, red needle, readout below, Step/Mode as knobs |
| 8 | Split-flap | 2 | Solari flip cards that flip on change |
| 16 | Odometer | 2 | Rolling drums, brass window, walnut case, red Hz drums |
| 12 | Aurora | 2 | Night sky, aurora glow, constellation, gradient digits |
| 14 | Blueprint | 2 | Grid paper, outlined drafted digits, dimension arrows, title block |
| 18 | Dot-matrix | 2 | Red 5×7 LED dots, unlit dots faint |
| 20 | Telegraph | 2 | Brass plate on wood, Morse paper tape of the frequency |
| 19 | QSL card | 2 | Callsign/grid from Prefs, QSO box, band stamp + postmark, "73" |
| 3 | Futuristic | 3 | HUD frame, grid, scanlines, glowing cyan, signal bar (needs S-meter) |
| 9 | VFD | 3 | Teal 14-seg, side status, bar-graph S-meter |
| 13 | Gauge | 3 | Round, kHz centre, S-meter arc, tick ring rotates with tuning |
| 11 | Phosphor CRT | 3 | Green CRT, scanlines, mini passband spectrum trace |
| 22 | Grayline map | 3 | World map with day/night terminator, own-grid dot |
| 23 | Band plan | 3 | Segment ribbon + marker/passband, licence-class check (US plan first) |

Dropped during review: Modern flat (#4), Synthwave (#15), Bench counter (#21),
Logbook (#24). Mockups were browser HTML; implemented styles will be close,
not pixel-identical.

## 4. Architecture

### 4.1 Current state
`src/qml/TuningPanel.qml` holds two near-duplicate ~150-line VFO boxes
(`vfoA`; `vfoB` = SPLIT TX VFO and SUB/RX2 VFO). Each contains the bordered
box, amber RX/TX role tag, clickable TX pip, a `FreqDisplay`, a typed-entry
`TextField`, and Step (+ Mode on A) `LyraComboBox`es. `FreqDisplay`
(`src/freqdisplay.{h,cpp}`, a `QQuickPaintedItem`) both paints the digits and
handles input. No fonts are bundled; "Share Tech Mono" silently falls back.

### 4.2 Components

**`FreqDisplay` → input/tuning core (C++, no painting).**
- Keeps: `freqHz`, `externalStepHz`, `freqEdited`, `editRequested`,
  `parseFreqInput`, wheel/key/click/double-click handling, `kNDigits = 10`,
  `kMaxHz`.
- Drops `paint()`. Becomes a transparent `QQuickItem` laid over the style.
- New property `digitRects` (`QVariantList` of `QRectF`, index = place
  0..9, coordinates in the FreqDisplay's own space). Hit-testing for click and
  wheel uses these instead of the rects it used to compute while painting.
- New property `selectedDigit` (int, -1..9, notify) exposed so styles can draw
  the selection; selection changes still originate in FreqDisplay.
- New read-only `digitString` (10-char zero-padded string) for styles.
- Class name and QML type name `FreqDisplay` are kept — `mainwindow.cpp`
  (`qobject_cast<lyra::ui::FreqDisplay*>` in the global key filter) relies on it.

**`src/qml/vfo/VfoBox.qml` — shared host, used twice.**
- Property `role`: `"A"`, `"Bsplit"`, `"Bsub"` — drives which Stream
  frequency/setters, which border-colour rule and which role tag text apply
  (logic moved verbatim from the two current boxes).
- Loads the selected style with a `Loader` from the registry.
- Overlays `FreqDisplay` (filling the style), binds
  `digitRects ← style.digitRects` (mapped into FreqDisplay space).
- Owns the typed-entry `TextField`, positioned over the union of the style's
  digit rects, neutral dark styling with cyan border so it is readable on
  every style (incl. Aluminum).
- Owns the focus `MouseArea` (left press → `Stream.setFocusedRx`) and the
  right-click **VFO style** `Menu`.

**Style files — `src/qml/vfo/VfoStyle<Name>.qml`.**
Each root item exposes this contract:

| Direction | Property | Meaning |
|---|---|---|
| in | `freqHz`, `digitString` | frequency to show |
| in | `selectedDigit` | place 0..9 or -1 |
| in | `state` | `"rx"`, `"tx"`, `"armed"` (styles choose colours) |
| in | `roleText` | "RX"/"TX" tag text |
| in | `vfoLetter` | "A"/"B" |
| in | `mode`, `stepHz`, `bandName` | display info |
| in | `showMode` | false on VFO B (shares A's mode) |
| out | `naturalWidth`, `naturalHeight` | design size at scale 1 |
| out | `digitRects` | 10 rects, index = place |

Styles render at their natural size inside a container the host scales
uniformly (§5). Styles instantiate two shared controls and position/theme them:
- **`VfoStepMode.qml`** — the real Step and Mode `LyraComboBox`es with all
  existing logic (`Prefs.vfoStepHz`, `Prefs.mode`, B's own step list). Styles
  pass colours/size only.
- **`VfoTxPip.qml`** — the click-to-assign-TX pip with existing logic
  (`root.assignTx`), themed by the style.

**Registry — `src/qml/vfo/VfoStyles.js`.** Ordered list of
`{ id, name, group, file }`. Only implemented styles are listed. Helper
`resolve(id)` returns the entry or the `lyraMac` entry if unknown.

**Preference — `Prefs.vfoStyle`** (`QString`, notify, QSettings key
`ui/vfo_style`, matching the existing `ui/…` snake_case keys). Default `"lyraMac"`. String ids so adding/reordering never
reinterprets a saved value. Shared by both VFOs.

**Fonts — `resources/fonts/`**, compiled into the binary as Qt resources and
loaded with `FontLoader` (no system install):
Share Tech Mono, DSEG7 Classic, Nixie One, Inter — all SIL OFL 1.1; licence
texts committed alongside. Lyra (Mac) uses the macOS system font (SF) and
falls back to Inter elsewhere (SF cannot be redistributed). Later phases add
fonts the same way (one licence check each).

### 4.3 Rendering rules (all styles)
- **No `QtQuick.Shapes`** (documented regression: broke the shared
  scene-graph path, waterfall went patchy — `PanadapterPanel.qml`).
- Use `Rectangle`, gradients, `Image`, `Text`, `Repeater`. `Canvas` only for
  static decoration painted once (e.g. constellation), never per frame.
- `QtQuick.Effects` `MultiEffect` (glow/blur) allowed where a design needs it;
  each use is verified against the waterfall (§7).
- Frequency changes must not trigger full-style repaints beyond the changed
  text items.

## 5. Sizing

- The scaled container size is `naturalSize × s`, where
  `s = min(1, availW / naturalWidth, availH / naturalHeight)`. The style is
  never clipped and never scaled up.
- VFO column preferred width = active style's `naturalWidth` (replaces the
  fixed 360 px); minimum width keeps a floor so a squeezed row scales the
  style rather than collapsing it. Existing narrow rules stay (logo hides
  < 760 px; empty VFO-B slot yields first).
- VFO row height = max(style height, logo 132). `lyraMinHeight` already
  derives from content, so it follows the style.
- **`mainwindow.cpp` change (Tuning dock only):** today the dock's minimum
  height is read once at load. For the Tuning dock: (a) on every
  `lyraMinHeightChanged`, re-apply `setMinimumHeight` (so content that grows —
  a taller style, the zero-beat strip — is never clipped); (b) only on
  `Prefs::vfoStyleChanged`, call
  `resizeDocks({tuningDock}, {newHeight}, Qt::Vertical)` so the dock grows
  *or shrinks* to the new style. Resizing only on a style change keeps it from
  fighting the saved window layout at start-up. Other docks unchanged.
- If the window cannot grant the height, the style scales down to the height
  it gets (formula above).

## 6. Selection and persistence

- Right-click anywhere on either VFO → `Menu` titled "VFO style", one
  checkable item per registry entry, check on current. Selecting sets
  `Prefs.vfoStyle`; both VFOs reload immediately; QSettings saved at once.
- Right-click is unused on the VFO today; pattern matches the existing
  SPLIT/SUB right-click menus. Left-click/wheel/keys/double-click unchanged.
- Group headings (Modern / Classic / Ham shack) are added once the list is
  long (Phase 2+); Phase 1 menu is flat.
- First run after upgrade → `lyraMac`. Unknown/removed id → `lyraMac`.

## 7. Testing

**Automated** (`scratch/` test executables, existing CMake pattern):
1. *Digit hit-test* — FreqDisplay with given `digitRects`: clicks/wheel map to
   the right place; gaps map to none (wheel then uses `externalStepHz`);
   key/wheel step sizes identical to before.
2. *Style contract* — load every registry entry in a `QQmlEngine`; each must
   instantiate without warnings, report positive natural size and exactly 10
   digit rects inside its bounds. Unknown id resolves to `lyraMac`.
3. Existing `test_wheel_steps` still passes.

**Live (screenshots of the Lyra window):** each style on VFO A and on VFO B in
SPLIT and SUB; RX/TX/armed borders; typed entry; right-click menu; narrow
panel; persistence across restart; zero QML warnings in the log; panadapter +
waterfall render cleanly with glow styles active; several quit/relaunch cycles
(shutdown-crash fix still holds). Operator: key a dummy load on two styles.

## 8. Delivery

**Phase 1** (this spec), two commits, each approved by the operator:
1. *Framework* — FreqDisplay split, `VfoBox`, shared controls, registry,
   `Prefs.vfoStyle`, dock resize, fonts, **Amber LED only**. Must be visually
   identical to today (safe checkpoint).
2. *Styles* — Lyra (Mac) (becomes default), Classic LCD, Nixie, Widget,
   Aluminum.

**Phase 2** — the eight animated/decorative styles; menu group headings.
QSL card reads `Prefs.callsign` / `Prefs.gridSquare`.

**Phase 3** — data-driven styles. Live level from the `Meter` context object
(`MeterModel`), **read-only** — its property names are frozen by agreement
with the parallel `meter-panel` work. Phosphor CRT needs a small passband
spectrum feed; Grayline needs a bundled public-domain Natural Earth outline
and a solar-position calc (reuse `SolarService` daylight maths); Band plan
needs band-plan data + a licence-class setting (US first). Each gets its own
short design note before implementation.

## 9. Coordination with parallel work

A Cursor session works on branch `meter-panel` in worktree
`../Lyra-meter` (new meter styles). Agreed boundaries: it does not edit
`freqdisplay.*`, `TuningPanel.qml`, `src/qml/vfo/`, `prefs.*`, `main.cpp`,
`mainwindow.cpp`, the `Lyra*` controls, or fonts/resources; it only appends to
`CMakeLists.txt` `QML_FILES`. Expected merge overlap: `CMakeLists.txt`
`QML_FILES` list only.

## 10. Risks

| Risk | Mitigation |
|---|---|
| Refactor regresses tuning/focus/typed entry | Framework commit ships Amber only and must be visually and behaviourally identical; hit-test unit test |
| MultiEffect disturbs waterfall rendering (like Shapes did) | Verify per glow style; fall back to layered Text glow if any artefact |
| Dock resize fights saved window layout | `resizeDocks` only on `vfoStyleChanged`, never at start-up or on other min-height changes; verify with a saved layout |
| Font licences | OFL-only, licence files committed, SF never bundled |
| Large styles on small screens | Uniform scale-down, never clip |

## 11. Revisions from implementation planning

Decided while writing `docs/superpowers/plans/2026-10-10-vfo-styles-phase1.md`.
Where these conflict with the sections above, these win.

1. **Flat file layout.** Files go in `src/qml/` with a `Vfo` prefix (`VfoBox.qml`,
   `VfoDigitRow.qml`, `VfoStepMode.qml`, `VfoTxPip.qml`, `VfoStyles.js`,
   `VfoStyle<Name>.qml`). This matches the existing flat `Lyra*` layout. There is
   no `src/qml/vfo/` directory.
2. **`VfoBox` takes `isB: bool`, not a role string.** The existing VFO B box
   already handles both SPLIT and SUB through one code path (`displayBHz`).
   In SUB, VFO B shows its Mode picker (RX2's own mode); in SPLIT it is hidden.
3. **Styles only draw.** A style does not instantiate the Step/Mode row or
   the TX pip. It reports where they go and how they look:
   - `tuneRect`, `stepModeRect`, `txPipRect`
   - `theme { label, field, fieldBorder, text, accent, pipIdle, pipIdleBorder, pipIdleText, compact }`

   `VfoBox` then places the shared controls. Inputs are `freqHz`,
   `selectedDigit`, `vfoState` (`rx`/`tx`/`armed`), `roleText`, `vfoLetter`,
   `modeText`, `stepHz` and `showMode`. Styles never touch Stream or Prefs, so
   the contract test loads them headless. The test also checks that the
   Step/Mode row does not overlap `tuneRect` and that the TX pip covers no
   digit.
4. **Fonts location.** Fonts live in `src/assets/fonts/` and are registered in
   `main.cpp` with `QFontDatabase::addApplicationFont`.
5. **`Prefs.vfoStyle` defaults to `""`.** The registry resolves `""` or an
   unknown id to its `defaultId`. The default is `amber` in the framework
   commit and `lyraMac` once that style ships.
6. **`LyraComboBox` theme properties.** It gains optional `fieldColor`,
   `fieldBorderColor`, `textColor` and `accentColor`. Their defaults equal
   today's colours, so every other combo stays unchanged.
7. **Post-implementation notes (Phase 1).**
   - `digitString` was dropped from the style contract in favour of
     `digitRects`: styles report one rect per digit place and `VfoBox` hit-tests
     against those, so proportional and stacked layouts need no per-style
     digit arithmetic.
   - The bundled fonts landed in the styles commit, not the framework commit.
   - Aluminum was removed. A saved `aluminum` id migrates to `lyraMac` on load
     (`src/vfo_style_migrate.h`).
   - VFD (`VfoStyleVfd.qml`, stroked tube segments — no DSEG font) was added to the Classic group.

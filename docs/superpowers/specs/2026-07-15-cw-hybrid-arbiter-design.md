# CW Hybrid "Auto" Decoder — Arbiter + Training Harvest

**Status:** Design approved (2026-07-15) — pending implementation plan
**Branch context:** builds on `feat/deepfist-cw` (DeepFist neural CW engine, PR #7)
**Author:** Brent Crier (N9BC)

## 1. Summary

Add a third, selectable RX CW engine — **Auto** — beside the existing
**Classic** (fldigi-port) and **DeepFist** (neural) decoders. Auto is a
*reliability* engine: it runs both existing decoders unmodified and a thin
supervisor (`CwArbiter`) hands display ownership to whichever one is
trustworthy right now, so the operator is **never left staring at a silent
pane during a fade**. Displayed copy is subtly source-marked so fallback text
carries an honest lower-confidence cue.

A second, separable layer (`CwCaptureHarvester`) rides on the arbiter's
decisions to passively build a **training corpus** aimed at DeepFist's known
blind spot (fading signals), using only externally-corroborated labels — and
feeds RBN-confirmed callsigns back into the rescorer's Super Check Partial
candidate space.

## 2. Goals and non-goals

**Goals**
- Never go silent while a copyable signal is present. Always show *something*
  readable.
- Keep Classic and DeepFist fully selectable and byte-for-byte unchanged; Auto
  is additive and opt-in.
- Present fallback copy honestly (subtle dim = lower confidence), not disguised
  as solid copy.
- Turn live operation into a passive, trustworthy training-data source targeting
  the real fade distribution.
- Grow the callsign rescorer's coverage from calls actually heard on-air.

**Non-goals (YAGNI)**
- Not maximizing copy quality by blending characters (that is confidence-fusion,
  explicitly rejected — reintroduces alignment/dup risk).
- Not modifying either decoder's internals.
- Not auto-training on unverified text (fade labels are never trusted as text).
- No cloud upload, no automatic model retraining loop — harvest writes local
  files an operator later feeds to a training run by hand.

## 3. Background — why fusion is the right tool

From the "DeepFist shows nothing" investigation, the two engines fail in
*opposite* places:

- **Classic** rides fades — its mature adaptive filter/AGC keeps producing
  characters deep into QSB — but hallucinates on noise and garbles fast clean
  copy more than the neural net.
- **DeepFist** gives cleaner copy on solid signals and does not hallucinate on
  dead air (its keying gate), but that same gate **clamps shut on fades**: the
  key-up floor rises, `keyingRatio` (p90/p10 of the locked tone's baseband
  envelope) collapses, and the streamer emits nothing — silence exactly where
  Classic is still reading.

Complementary failure modes are the ideal precondition for an arbiter, and the
gate scalar that *causes* the silence is a ready-made fade detector.

## 4. Architecture overview

Two layers, cleanly separable — Layer 1 ships and delivers the reliability goal
on its own; Layer 2 is the training payoff on top.

```
CW mono tap ──┬──▶ Classic decoder  ──┐ (committed chars, tagged CLASSIC)
              │                        ├──▶ CwArbiter ──▶ cwAutoText (source-tagged)
              └──▶ NeuralCwDecoder ────┘        │              │
                     │ keyingRatio ─────────────┘              ▼
                     │                              CwDecoderPanel.qml (subtle dim on fallback)
                     ▼
              (Layer 2) CwCaptureHarvester ◀── arbiter events + audio ring buffer
                     ├──▶ GOLD real-data corpus (runs/real_*)
                     ├──▶ hard-negative channel-stat rollup (synth tuning)
                     └──▶ scp_local.txt ──▶ DeepFistScp merge
```

The critical boundary: **`CwArbiter` is engine-agnostic.** It sees only tagged
characters and a fade-confidence scalar — no fldigi, no ONNX. That keeps both
engines swappable and makes the arbiter unit-testable with synthetic streams.

## 5. Layer 1 — Live Arbiter

### 5.1 Engine fan-out

`wdsp_engine.cpp` gains `cwEngine_ = 2` (Auto). In Auto the CW mono tap is fanned
out to **both** the Classic decoder and `NeuralCwDecoder` simultaneously; each
decodes independently exactly as today. Classic-only (`0`) and DeepFist-only
(`1`) paths are untouched.

### 5.2 `CwArbiter` unit

New `src/dsp/CwArbiter.{h,cpp}`, no fldigi/ONNX knowledge. Inputs:
- committed characters from each engine, tagged with **source** + **timing**;
- DeepFist's live `keyingRatio` (already computed at `NeuralCwDecoder.cpp:125`,
  gate threshold `kKeyingMin = 12`).

Output: a single unified character stream, each character tagged
`DEEPFIST` (primary) or `CLASSIC` (fallback), delivered on a new `cwAutoText`
signal (+ per-run source flag). The arbiter consumes each engine's
*already-committed* characters (not internal state) — so it stays a thin
supervisor and inherits each engine's commit latency (fine for display).

### 5.3 Switch state machine

Two states — only one engine owns the display at a time:
- **`DEEPFIST` (preferred)** — default whenever the signal is solid (cleaner copy).
- **`CLASSIC` (safety net)** — drives during fades; always running underneath, so
  instantly available.

Inputs watched:
- **`keyingRatio`** — the fade detector (the exact scalar that collapses in QSB).
- **Keying gaps** — inter-character/word silence, detected as "no new committed
  char for > one element-space at the current WPM." Switches happen *only* at a gap.

Transitions — asymmetric (Schmitt) hysteresis, biased toward reliability:

```
             ratio < T_low  for T_fall (short)   ← reliability: bail out fast
  DEEPFIST ─────────────────────────────────────▶ CLASSIC
           ◀─────────────────────────────────────
             ratio > T_high for T_rise (long)    ← clean copy: return only when solidly back
     (both transitions fire ONLY on a keying gap)
```

Three stacked anti-flap layers:
1. **Two thresholds** (`T_high > T_low`) — dead-band prevents edge oscillation.
2. **Time debounce** — `T_fall` short (honor "never silent"), `T_rise` long
   (don't yank back until convincingly recovered). Asymmetric on purpose.
3. **Gap-aligned switching** — a "ready" decision waits for the next gap to take
   effect.

**No de-duplication by construction:** we switch only during silence, only one
engine commits at a time. At the gap both engines have committed everything up to
it; the new owner starts at the *next* character. The muted engine keeps decoding
(stays warm — AGC/filter/rolling window keep tracking) but its output is dropped.
No overlap → no dup, no drop, no alignment math.

**Cold start / mode entry:** seed state from the current `keyingRatio` — solid →
`DEEPFIST`, else `CLASSIC`. Never start silent.

Thresholds (`T_low`, `T_high`, `T_fall`, `T_rise`) are tunable constants with
sensible defaults derived from `kKeyingMin`; exposed for test injection.

### 5.4 Source-marking to the panel

Each emitted character carries a one-bit source flag. `cwAutoText` delivers text
as short runs tagged by source; `CwDecoderPanel.qml` renders them in the existing
RichText decode pane:
- **Primary (DeepFist)** → full-strength foreground (as today).
- **Fallback (Classic-during-fade)** → same color at **~65–70% opacity** — a
  faint dim, not a hue change — via a `<span style="opacity:…">` run. No new widget.

Layers cleanly with existing behavior: the RBN-confirmed callsign keeps its
bright-green highlight on top of whichever engine produced it, and the decode-pane
auto-scroll fix (`Connections` on `contentItem.onContentHeightChanged`) already
handles RichText height settling, so mixed-opacity runs scroll correctly. The
dim level is a single attribute, trivially A/B-adjustable.

### 5.5 Prefs / signals

- `cwDecodeEngine` extended to accept `2` (Auto).
- New `cwAutoText` signal (text + per-run source flags) from `WdspEngine`.
- Arbiter thresholds live as constants (defaults from `kKeyingMin`), overridable
  for tests.

## 6. Layer 2 — Training Harvest

A separate unit riding on the arbiter's events — the arbiter stays pure and
ships without it. `CwCaptureHarvester` subscribes to arbiter events plus a
rolling audio ring buffer and writes a labeled corpus to disk on a worker thread
(never stalls DSP). **Off by default** — capturing received audio to disk is a
real side effect; explicit opt-in pref (`cwCaptureEnabled`) with a size cap and
retention policy.

### 6.1 Capture events and trust tiers

| Event | Source | Trust tier | Training use |
|---|---|---|---|
| Arbiter `DEEPFIST→CLASSIC` (gate clamped on real signal) | the fade | **hard-negative** (text NOT trusted) | measure channel — QSB depth/rate, SNR, key-up floor rise → tune **synthetic** augmentation |
| Both engines agree, OR RBN-confirmed call, OR high-margin SCP snap | verified | **GOLD** | append to **real-data corpus** (`runs/real_*`, `--wmr` set) |
| Single engine, high CTC/gate confidence, unconfirmed | one engine | **SILVER** | staged for optional human spot-check; never auto-trained |

The tiering is the safety story: **fade audio is captured for its channel
statistics, never its text** — that is exactly where labels are least
trustworthy. Only externally-corroborated segments become labeled data.

### 6.2 On-disk format

One segment = a WAV (**3200 Hz float** — DeepFist's native rate, captured
*pre-conditioner* so the front-end stays re-runnable) + a JSON sidecar:
timestamp, WPM estimate, the `keyingRatio` trace, both engines' text with source
tags, trust tier, adjudication result (which of RBN / SCP / agreement fired), and
measured channel stats. The shape maps onto the label+clip form DeepFist's
`--wmr` pipeline already consumes, so a harvested GOLD folder drops into a
training run directly.

### 6.3 Two outputs

1. **GOLD → real labeled corpus** — grows `runs/real_all_train` with verified
   real-world clips, concentrated on this station's actual conditions.
2. **Hard-negatives → synth-tuning stats** — a rollup of QSB depth/rate/SNR at
   the moments DeepFist clamped, feeding the synthetic channel model's config for
   the next run (exp28), so it learns realistic fades with *clean synthetic
   labels*.

### 6.4 RBN → SCP loop

When a callsign is RBN-confirmed during operation it is a verified-real call, fed
back to strengthen the rescorer:
- The immutable shipped `MASTER.SCP` stays untouched (refreshable artifact). The
  harvester maintains a separate **`scp_local.txt`** of RBN-confirmed calls heard
  at this station.
- `DeepFistScp::load` merges both — shipped master + local list — dedup'd, so the
  rescorer's candidate space grows with real calls copied here.
- **Closes the loop:** better SCP coverage → stronger SCP-margin trust gate →
  more segments qualify as GOLD → better harvest. The local list is dedup'd
  against the master, size-capped, and age-out'd so it cannot grow unbounded or
  drift stale.
- Refreshing the master stays a no-code artifact swap; `scp_local.txt` survives
  the swap (separate file).

### 6.5 Code boundaries / prefs

- `src/dsp/CwCaptureHarvester.{h,cpp}` — event + audio subscriber, async disk
  writer, trust adjudicator.
- Small `ScpLocal` addition to the SCP loader for the merge.
- Prefs: `cwCaptureEnabled` (default off), retention/size-cap settings.
- The arbiter needs **zero** knowledge of any of this — it only emits events the
  harvester listens to.

### 6.6 Privacy / resource

`scp_local.txt` and the corpus are a local record of received activity. Off by
default; opt-in, size-capped, retention-bounded. No network egress.

## 7. Testing

Approach A's boundaries make almost everything testable without radio/audio/model.

**Arbiter (`test_cw_arbiter`, deterministic, no I/O)** — drive `CwArbiter` with
synthetic tagged character streams + a scripted `keyingRatio` curve; assert:
- ownership timeline `solid→DEEPFIST`, `fade→CLASSIC`, `recover→DEEPFIST`;
- hysteresis — dither inside `T_low…T_high` yields **zero** flips;
- asymmetric debounce — fallback within `T_fall`, return waits `T_rise`;
- gap-aligned — every switch on a gap, never mid-character;
- **no-dup / no-drop invariant** — concatenated owned runs reproduce input exactly;
- **never-silent** — continuous signal with a mid-fade yields continuous output;
- cold start seeds correctly.

**Golden regression (fixture already exists)** — `cw_bt_debug.wav` (a real off-air
fade that clamps the gate, from the original debug) replayed through the full Auto
path: assert DeepFist drives clean spans, Classic backfills the fade, output never
goes silent, source flags flip at fade edges. The motivating failure is the
acceptance test.

**Mode-isolation regression** — Classic-only and DeepFist-only output byte-identical
to today on a reference clip.

**Harvest** — trust adjudicator: known inputs → asserted tier (pure logic).
Corpus round-trip: write→read, schema matches `--wmr` loader (a GOLD folder must
load in a real deepfist training run — integration check). `ScpLocal` merge:
dedup, cap, age-out, candidate space includes a local-only call.

**Perf sanity (note, not gate)** — Auto runs both engines; both already run fine
individually; confirm CPU headroom, no hard assertion.

## 8. Build phases

- **Phase 1 — Arbiter (ships the reliability goal):** fan-out, `CwArbiter` +
  state machine, `cwAutoText` + source-marking in QML, `test_cw_arbiter`, golden
  regression, mode-isolation regression.
- **Phase 2 — Harvest:** `CwCaptureHarvester`, audio ring buffer, trust
  adjudicator, corpus format, GOLD + hard-negative outputs, adjudicator/round-trip
  tests, opt-in prefs.
- **Phase 3 — RBN→SCP loop:** `scp_local.txt`, `ScpLocal` merge, merge tests.

Each phase is an independent implementation plan; Phase 1 is fully useful alone.

## 9. New / touched files

**New**
- `src/dsp/CwArbiter.{h,cpp}` — engine-agnostic supervisor + state machine.
- `src/dsp/CwCaptureHarvester.{h,cpp}` — Phase 2.
- `scratch/test_cw_arbiter.cpp` — arbiter unit tests (CMake target).

**Touched**
- `src/wdsp_engine.{h,cpp}` — `cwEngine_ = 2` fan-out; `cwAutoText` signal.
- `src/qml/CwDecoderPanel.qml` — Auto toggle option; source-marked rendering.
- `src/prefs.{h,cpp}` — `cwDecodeEngine=2`; `cwCaptureEnabled` + retention (Phase 2).
- `src/dsp/deepfist/DeepFistScp.{h,cpp}` — `ScpLocal` merge (Phase 3).
- `docs/DEEPFIST.md` — document Auto mode + harvest.

## 10. Open questions / deferred

- Exact default thresholds (`T_low`, `T_high`, `T_fall`, `T_rise`) — seed from
  `kKeyingMin`, tune on-air against `cw_bt_debug.wav` and live fades.
- SILVER tier is staged only (captured, flagged) — no human-review UI in scope
  now.
- Auto-retraining is out of scope — harvest produces files; training stays a
  manual, reviewed run.

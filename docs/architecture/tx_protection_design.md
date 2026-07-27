# TX Protection design — #169 SWR auto-cut + #170 max-power / over-power

Status: **COMPLETE — #169 (Cut + Fold + guards + latch + PROT lamp) shipped &
bench-confirmed; #170a drive cap shipped; #170b over-power trip DEFERRED by
operator.** Drafted 2026 while operator benched #172, built 2026-06-19 on
operator greenlight.

**Phase 1 shipped (builds clean):** SWR protection lives ON `HL2Stream`
alongside the host TX-safety timer + ATT-on-TX (same QSettings persistence,
same `Stream.*` Q_PROPERTY exposure, same mox-edge self-wire — NOT the
standalone QObject the pre-build summary sketched; consolidating onto the
stream matches the established TX-safety pattern and satisfies the off-GUI-loop
requirement automatically). A 50 ms repeating `swrEvalTimer_` armed on the
key-down edge / cancelled on key-up runs `evalSwrProtect()`: guard A key-down
blanking (`swrBlankMs_` ~200), guard B fwd/rev power floors (`swrFwdFloorW_`
1.0 / `swrRevFloorW_` 0.3 provisional-W — NaN-safe), the calibration-free
SWR ratio `(1+ρ)/(1−ρ)` with `ρ=√(rev/fwd)`, guard C sustained dwell
(`swrDwellMs_` ~200), then `tripSwrProtect()` = guard D latch + CUT via the
`requestMox(false)` funnel. Latch clears only on the next key-down (manual
re-arm). Operator surface: `swrProtectEnabled` (default ON) / `swrProtectLimit`
(5.0, clamp 1.5..10) / `swrProtectDuringTune` (default ON) + read-only
`swrProtectTripped` / `swrProtectReason`; advanced blank/dwell/floor knobs are
QSettings-only (`tx/swr*`) for no-rebuild bench tuning. UI: TxPanel "PROT" lamp
(gray=disabled / green=armed / red=tripped-with-reason) + Settings → TX "SWR
protection" group (enable + limit spin + during-tune). `swrProtectCut` signal
emitted on trip (no toast consumer yet — matches the `txTimeoutFired` sibling
precedent; the `safetyLog` line + lamp cover it).

**Phase 1b shipped (builds clean):** the action is now an operator option —
`swrProtectAction` 0 = **Cut** (default) | 1 = **Fold**. Fold (`foldSwrProtect()`)
captures the operator's drive at the first step, then steps the APPLIED drive
×0.5 toward `foldMinDrivePct` (default 10 %) via the new
`applyDriveLevelNoPersist()` (no QSettings write — TxPanel drive readout tracks
the back-off live), giving each reduced level a fresh dwell; if it reaches the
fold floor and SWR is still over the limit it escalates to a hard Cut. Never
auto-recovers — the operator's stored drive is restored on the next key-down
(`armSwrProtect` fold-restore). UI: Settings → TX "SWR protection" group gains
an action combo (Cut / Fold back power) + a fold-floor % spin; the PROT lamp
reason shows `SWR x.x:1 → fold N%` while folded. Persisted `tx/swrProtectAction`
+ `tx/foldMinDrivePct`.

**Phase 1/1b bench-confirmed (operator, 2026-06-19):** Cut + Fold both fire on
a deliberately-bad SWR; the PROT lamp overflow that initially spilled over the
ATT lamp was fixed (compact `SWR x.x:1` latched label, width-clamped + elided;
fold level shown live on the Drive % readout, full detail in tooltip + log).

**Phase 2 #170a shipped (builds clean):** Max TX drive cap on `HL2Stream`. A
hard ceiling (`maxDrivePct`, default 100 = no cap) enforced as a clamp in
`setTxDriveLevel` — verified to be the single chokepoint every drive write
funnels through (operator slider, TUN drive main.cpp:907/914/934, BandMemory
restore bandmemory.cpp:178, TCI DRIVE tci_server.cpp:1549). Also clamped at the
open re-push (line ~922) + the fold no-persist path + the ctor startup load.
`maxDriveRaw()` = header-safe integer ceiling (pct→0..255). `setMaxDrivePct`
re-clamps the live drive DOWN on lowering (never raises). Pure preventive clamp,
zero false-trigger risk, no telemetry/cal needed. UI: Settings → TX "TX power
limit (drive cap)" group → "Max TX drive %". Persisted `tx/maxDrivePct`.

**Phase 3 #170b — DEFERRED (operator decision 2026-06-19): "it should be fine
as it now is."** The always-safe drive cap (#170a) covers the real need, exactly
as the §7 framing predicted ("most operators will run (a) only"). The over-power
trip stays a documented future item — NOT built. If revived: cal-gated, default
OFF, must read the #45 calibrated fwd watts (never raw `fwdPowerW()`), refuse to
arm on bands without 3-point cal, trip at a ~1.15×P_max margin, reuse the §5
guards (it would slot in beside `evalSwrProtect` as a second rule object on the
same eval tick).

**#169 + #170 are COMPLETE for this arc.** SWR protection (Cut + Fold) +
drive cap shipped, #169 bench-confirmed; #170a awaiting a quick operator
cap-can't-be-exceeded bench check.

**Operator decisions (locked):** (1) action is an **operator option: Cut
(default) | Fold back** — fold mechanics in §6/§7; (2) **manual re-key** re-arm,
no auto-rearm; (3) **drive cap (#170a) approved**; (4) **SWR-during-tune is an
operator option** (checkbox); (5) **5:1 default threshold**. Build staging:
**Cut first (Phase 1)**, then **Fold option (Phase 1b)** — the §5 guards are
identical for both actions, so guard tuning carries over.
Scope: the smartest false-trigger-resistant way to do high-SWR cut (#169) and
max-power limiting (#170) on the HL2/HL2+ (QRP, ~5 W). Grounded in Lyra's
actual telemetry + MOX surfaces and the Thetis reference. Bench-tune the
constants; the *structure* is the point.

---

## 0. The operator's ask, restated

> "Don't FALSE-trigger, but don't hold back so much that it should have
> triggered."

That balance is won almost entirely by **what we gate the trigger on** and
**how long we require the condition to persist** — not by the threshold value
itself. The threshold (5:1) is the easy part; the gates below are the design.

---

## 1. Physics framing (why this is gateable without being twitchy)

- **Damage scales with reflected POWER, not SWR ratio.** A 10:1 SWR at 0.3 W
  forward reflects ~0.2 W — harmless. The same 10:1 at 5 W reflects ~3.4 W —
  that's the case worth unkeying for. **So the real danger metric is reflected
  watts, and SWR ratio is the "is the match bad" qualifier.** Gating on
  reflected power is what lets us be aggressive on a real fault and silent on a
  harmless low-power blip.
- **SWR is a RATIO ⇒ it is calibration-free.** `ρ = √(rev/fwd)` divides out the
  raw→watts scale, so #169 works correctly even though Lyra's absolute power
  cal (#45) is still "provisional" in `fwdPowerW()`/`revPowerW()`
  (`hl2_stream.cpp:1321-1338`). This is the single biggest reliability win for
  #169 — **do NOT make the SWR trip depend on calibrated watts.**
- **Absolute over-power (#170b) is NOT ratio-based** — it needs trustworthy
  calibrated watts. That cal-dependency is exactly why #170b is the riskier,
  opt-in half and #170a (a pure drive clamp) is the always-safe primary.
- **QRP changes the reference numbers.** Thetis gates its SWR check behind
  `fwd > 5 W` — on a 5 W-max HL2 that gate would *never* open, i.e. a verbatim
  port would silently never protect. We must scale the gates to QRP (gate on
  reflected-watts + a ~1 W forward floor, not 5 W).

---

## 2. Reference (Thetis 2.10.3.13) — what to copy, what to diverge from

Extracted from `Console/console.cs` (agent read, file:line in the session log):

| Aspect | Thetis | Lyra decision |
|---|---|---|
| SWR formula | `(1+ρ)/(1−ρ)`, `ρ=√(rev/fwd)` | **Same** — already in `metermodel.cpp:549`. Parity. |
| Default threshold | 2.0:1, operator-settable | **5:1 default** (operator's call). 2:1 foldback is a *fine* protection for a 100 W+ rig; on QRP a 5:1 *cut* is a "something is genuinely wrong" safety, which matches the task's "auto-cut" wording. |
| Action | **Foldback** drive (`SWRProtect = limit/(swr+1)`) | **Hard unkey + latch** (the task says "cut"). Foldback is the better long-term behaviour but needs a drive-foldback hook; a clean unkey is simpler, safer for QRP, and re-keyable. (Foldback offered as a future mode — §7.) |
| Debounce | 4 consecutive 1 ms polls = 4 ms | **Dwell in real time** (~200 ms), because Lyra samples at 50 ms, not 1 ms (§3). |
| Min-power gate | `fwd > 5 W` | **reflected-watts floor + ~1 W fwd floor** (QRP-scaled, §5). |
| Open-antenna | fwd>10 W & fwd−rev<1 W ⇒ immediate unkey | **Fast path** kept but QRP-scaled (§6). |
| Poll + smoothing | dedicated thread, 1 ms; IIR α=0.90 on fwd/rev before the check | Lyra: reuse/clone the 50 ms tick on the **stream side**; light IIR on fwd/rev before the protection math (§4). |
| Max-power cap | **none for HL2** — operator + ATT slider only | **#170 is Lyra-native** (operator wants it for a low-drive amp). |

Net: SWR *math* is reference-faithful; the *action* (cut vs foldback) and the
*gates* (QRP-scaled, reflected-power-aware) are deliberate, justified
divergences.

---

## 3. Lyra surfaces we build on (verified)

- **Telemetry:** `HL2Stream::fwdPowerW()` / `revPowerW()` — raw provisional
  watts from EP6 slots 0x08 C3:C4 / 0x10 C1:C2; return `NaN` if `prn==nullptr`.
  ⚠ fwd and rev arrive on **different EP6 rotation slots**, so they refresh at
  the telemetry sub-rate and are **not simultaneous** — the evaluator must pair
  "latest fwd" with "latest rev", and the dwell must span at least one full
  fwd+rev refresh cycle (confirm that cadence at bench).
- **SWR math + low-power guard already exist** in `metermodel.cpp`
  (`kSwrGuardW = 0.5 W`, `ρ`, clamp). Reuse the formula; the protection floor
  will be higher than the *display* guard.
- **Sample cadence:** `MeterModel::tick()` is a **50 ms QTimer**
  (`metermodel.cpp:208`). 20 Hz is plenty for protection (a 200 ms dwell = 4
  ticks). The protection evaluator should run at the same 50 ms cadence but
  **on the stream side** (§8), not piggy-backed on the GUI meter.
- **Unkey funnel:** `HL2Stream::requestMox(false)` is the SINGLE unkey path;
  the FSM keyup TR-delay chain runs through it. Cross-thread callers use the
  `Q_INVOKABLE requestMoxFromHwPtt/Tci` wrappers via QueuedConnection. A
  protection trip is "just another PTT source" — add `PttSource::Protect` (or
  reuse Manual) and unkey through the same funnel so the normal downslew/
  TR-delay safety chain runs (no special-case wire path).
- **Precedent:** there's already a **host-side TX safety timeout**
  (`hl2_stream.h:867`, "TX-0c-pa-debug host-side safety timeout"). SWR/
  over-power trips are the same *class* of host-side TX guard and should live
  next to it, sharing the latch + re-arm + toast plumbing.
- **Drive setter for #170a:** `HL2Stream::setTxDriveLevel(0..255)`
  (per-band, BandMemory-saved). The cap clamps here.
- **Per-band cal (#45):** lives on the PWR-meter path (rated-max scaling), NOT
  in `fwdPowerW()`. #170b must read the *calibrated* watts, not raw.

---

## 4. The shared evaluator (one mechanism, two rules)

Both #169 and #170b are "watch a TX telemetry value, qualify it, trip MOX."
Build **one** `TxProtect` evaluator rather than two parallel timers:

```
TxProtect (lives on HL2Stream / stream thread affinity)
  - 50 ms tick (own QTimer, or hook the existing telemetry tick)
  - reads latest fwdPowerW(), revPowerW()  (+ calibrated fwd watts for #170b)
  - light IIR (α≈0.3 per 50 ms ≈ 150 ms) on fwd/rev BEFORE evaluating
  - state: armed?, keydownMs, dwellCounters[rule], latched?, latchReason
  - on MOX rise:  reset all counters, stamp keydownMs, clear latch
  - on each tick while MOX active:
        if (now - keydownMs) < KEYDOWN_BLANK_MS: return          // §5 guard A
        evaluate SWR rule (#169) and over-power rule (#170b)
        a rule "fires" only after its dwell is satisfied
        first rule to fire → trip()
  - trip(): requestMox(false) [via funnel], latched=true,
            emit txProtectTripped(reason, measuredValue) → toast
  - re-arm: latch clears on the NEXT operator keydown (manual). No auto-retry.
```

One evaluator = consistent guards, one place to reason about false-trips, and
#170b is added as a second rule object, not a second subsystem.

---

## 5. The four false-trigger guards (this is the heart of it)

These four, combined, are what give "tight but not twitchy":

**A. Keydown blanking (~150–250 ms after MOX rise).**
On keydown the T/R relay settles, drive ramps, and the first fwd/rev telemetry
pair is stale/garbage. Ignore protection entirely for the blank window. This
kills the single biggest false-trip source. (Confirm the window covers the
relay-settle + first-telemetry-refresh at bench.)

**B. Reflected-power floor + forward-power floor (the physical gate).**
Only *consider* a trip when **reflected watts ≥ R_min** AND **fwd watts ≥
F_min**. Suggested starting points for a 5 W rig: `R_min ≈ 0.25–0.5 W`,
`F_min ≈ 1.0 W`. Below these, ρ is noisy AND the energy is harmless — exactly
the readings we must NOT trip on. This is the QRP-correct replacement for
Thetis's flat `fwd > 5 W` gate. (Both floors are cal-light: the floors only
need to be *roughly* right; the SWR ratio decision is cal-free.)

**C. Dwell / persistence (~150–300 ms continuous).**
Require the full trip condition true for N consecutive ticks (e.g. 4–6 × 50 ms)
spanning ≥1 fwd+rev refresh cycle. Skips a single bad sample / a momentary
hand-near-antenna / a tuner stepping. A genuine no-antenna keyup holds high
reflected power continuously → trips in ~0.2–0.4 s. Fast enough to protect,
slow enough to ignore transients.

**D. Latch + manual re-arm (no auto-retry into a fault).**
After a trip, stay unkeyed and require a conscious operator re-key. Auto-rearm
after N seconds is a footgun (keys back into the same dead antenna). Toast
names the cause + the measured value ("TX cut: SWR 7.3:1").

> The "not holding back too much" side is guards B+C tuned *short* (200 ms
> dwell, low floors); the "no false trips" side is guards A+B+C together. They
> trade against each other through 3 numbers (blank, floors, dwell) — all
> bench-tunable, all in Prefs so they need no rebuild to tune.

---

## 6. #169 — SWR auto-cut spec

**Trigger (all must hold, sustained for the dwell):**
```
past keydown-blank
AND fwd_iir   ≥ F_min            (~1.0 W)
AND rev_iir   ≥ R_min            (~0.25–0.5 W)   ← the real danger gate
AND swr       ≥ swrLimit         (operator, default 5.0)
```
**Action:** `requestMox(false)` + latch + toast.

**Open-antenna fast path (optional, QRP-scaled):** if `swr ≥ ~10` (ρ→1) AND
`fwd_iir ≥ F_min` AND `rev_iir ≥ R_min`, shorten the dwell to ~2 ticks
(~100 ms) — the worst case (no antenna / dead short) shouldn't wait the full
dwell. Keep it gated on the reflected floor so it can't fire on low power.

**Enable:** master on/off (default ON — it's a safety). **Disable-on-TUN**
option (Thetis has `disable_swr_on_tune`) — but for *finding* a bad antenna you
usually WANT it live during tune, so default that to "protect during tune too";
make it a checkbox.

**Action is an operator option (locked): Cut (default) | Fold back.**
Settings → TX: "On high SWR → [Cut TX] | [Fold back]". Default **Cut** — the
task says "cut," it's simplest, and on QRP a sustained 5:1 with real reflected
power means no/bad antenna or a bad connection where the clean safe action is
drop MOX. **Fold** is the nicer ATU/marginal-antenna behaviour for operators
who want to stay on the air at reduced power.

**Foldback mechanics (when Fold is selected) — monotone step-down to a
reflected-power target.** Deliberately NOT Thetis's continuous
`limit/(swr+1)` scalar and NOT a servo loop (both hunt/oscillate against the
operator's drive slider). Actuator = `setTxDriveLevel` (reducing only):
1. On the dwell-confirmed condition, **halve effective drive (×0.5)** per
   confirmation.
2. Re-measure; keep halving **until reflected watts < the danger floor**
   (`protectRevFloorW`, ~0.3 W) — fold exactly as hard as the reflection
   demands, no more.
3. **Floor at `foldMinDrivePct` (~10 %)**: if reflected is still over the floor
   at min drive → **escalate to a full Cut** (graceful degradation; never sit
   keyed into a dead load at min drive).
4. **Hold (latched-fold), never auto-recover upward** → no oscillation; the
   operator's slider stays the ceiling, protection caps it.
5. **Manual re-key clears the fold** (same re-arm as Cut).
Answers "how hard" = halve per confirmation (safe in ~1–2 steps); "to how
much" = down to whatever makes reflected power safe, floored at ~10 %, then
cut. (Future: a target-reflected-power servo with damping if step-down proves
too coarse — not the first cut.)

---

## 7. #170 — max-power-out limit

Two distinct mechanisms; ship **(a) first, always-on-capable; (b) opt-in.**

**(a) Drive cap — the primary, always-safe mechanism (cal-free).**
A hard ceiling on TX drive so a low-drive amp's input can't be exceeded.
Implement as a clamp in the `setTxDriveLevel` path: effective drive =
`min(operatorDrive, maxDrivePct)`. Pure preventive clamp — **zero false-trigger
risk** (it never trips; it just won't go above the ceiling). UI: "Max TX drive
%" in Settings → TX. This is what actually protects the amp; it needs no
telemetry and no cal.
- Interaction: TUN drive (#74) must also respect the cap (clamp both the
  operator drive and the tune drive). BandMemory restore must re-clamp.

**(b) Over-power trip — opt-in backstop (cal-dependent, default OFF).**
"Unkey if measured fwd watts > P_max." This is the telemetry trip and shares
the §5 guards (blank + dwell + latch). **Caveats that make it opt-in:**
- It MUST read the **calibrated** fwd watts (the #45 PWR-meter path), not the
  raw `fwdPowerW()`. A trip on uncalibrated watts = false trips from cal error.
- Default OFF, with a **margin** above the rated limit (e.g. trip at
  `1.15 × P_max`) so cal slop + PEP overshoot don't nuisance-trip.
- Gate it on "cal present/trusted for this band" — if the band isn't
  3-point-calibrated, the trip should refuse to arm (and say so), not guess.
- Because (a) already prevents over-drive, (b) is a *backstop* for the case
  where the amp's safe input ≠ a fixed drive % (e.g. band-dependent gain). Most
  operators will run (a) only.

**Foldback (future, ties both):** a "reduce drive instead of cut" mode for #169
+ a soft power-limit for #170 would reuse `setTxDriveLevel` as the actuator.
Park as a follow-on; not in the first cut.

---

## 8. Where it runs — OFF the GUI thread

The protection evaluator + trip should live on the **stream side**
(HL2Stream / its thread), next to the existing host TX-timeout — NOT on the
GUI-thread meter tick. Rationale: a safety unkey must not depend on a
responsive GUI event loop (the project's hard-won "don't couple the wire path
to UI/paint" lesson). The evaluator reads `fwdPowerW()/revPowerW()` (already
stream-side) and calls `requestMox(false)` in the stream's own thread (the
funnel + FSM live there). Thresholds come from Prefs as plain atomics/values
read each tick (no cross-thread call on the hot path). The `txProtectTripped`
signal marshals to the GUI for the toast.

(Native C++/Qt has no GIL, so a stall is far less likely than the Python-Lyra
case — but "safety guard independent of the GUI loop" is still the correct
posture, and it co-locates with the TX-timeout that already works this way.)

---

## 9. Prefs + UI surface

New Prefs (persisted `tx/protect_*`), all bench-tunable without rebuild:
- `swrProtectEnabled` (bool, default true)
- `swrProtectLimit` (double, default 5.0, range ~2.0–10.0)
- `swrProtectAction` (enum Cut | Fold, default **Cut**)        ← #169 option
- `foldMinDrivePct` (int, default 10) — fold floor; below it, escalate to Cut
- `swrProtectDuringTune` (bool, default true)  ← operator option (checkbox)
- `maxDrivePct` (int, default 100 = no cap; range 1–100)   ← #170a
- `overPowerTripEnabled` (bool, default false)             ← #170b
- `overPowerTripW` (double; default = rig rated max; armed only w/ cal)
- (internal/advanced, maybe hidden): `protectKeydownBlankMs` (200),
  `protectDwellMs` (200), `protectFwdFloorW` (1.0), `protectRevFloorW` (0.3),
  `protectOpenAntSwr` (10.0)

UI: a **"TX Protection"** QGroupBox in Settings → TX:
- SWR protection: [✓ enable]  Cut above [5.0]:1  [✓ also during tune]
- Max TX drive: [100] %
- [☐ Over-power trip] above [W] (greyed + "needs band PWR calibration" until
  the band is calibrated)
- (Advanced/collapsible: blank/dwell/floor knobs for the tinkerer.)

Plus a **TxPanel lamp** (mirrors the ATT-on-TX lamp #166): a "PROT" indicator
that flashes red + shows the reason on a trip, so the operator immediately sees
*why* TX dropped.

---

## 10. Bench / test plan

- **Dummy load (matched, ~1:1):** must NEVER trip at any drive. (Baseline.)
- **No antenna / open coax:** should trip within ~0.4 s once fwd ≥ floor.
- **Deliberate mismatch (e.g. a mistuned ATU / known bad load):** trip at/above
  the set ratio, not below.
- **Keydown into a bad load:** the trip should fire from the *steady* high-SWR,
  NOT instantly on the keydown transient (proves guard A).
- **Low-power high-ratio (drive ~5–10 %, mismatched):** should NOT trip
  (proves guard B — reflected power below floor).
- **Rapid re-key / SSB voice peaks / TUN:** no nuisance trips (proves C).
- **#170a:** set max drive 30 %; operator slider + TUN both clamp; BandMemory
  restore respects it.
- **#170b:** with cal, set trip just above a known power; confirm it trips +
  latches + the lamp shows the reason; with NO cal, confirm it refuses to arm.
- Confirm the **fwd/rev telemetry refresh cadence** at bench (sets the real
  dwell floor) — `LYRA`-style debug print of fwd/rev timestamps during a keyed
  carrier.

---

## 11. Operator answers (RESOLVED 2026)

1. **Cut vs fold** — **operator OPTION, default Cut, Fold available** (§6 fold
   mechanics: ×0.5 step-down to reflected-power target, 10 % floor, escalate to
   Cut). RESOLVED.
2. **Re-arm** — **manual re-key only** (no auto-rearm). RESOLVED.
3. **#170a drive cap** — **approved** as the primary mechanism. #170b
   over-power trip stays opt-in / cal-gated. RESOLVED.
4. **SWR-during-tune** — **operator option (checkbox)**, default protect during
   tune. RESOLVED.
5. **Threshold** — **5:1 default** confirmed. Other starting numbers (~1 W fwd
   floor / ~0.3 W rev floor / 200 ms blank / 200 ms dwell) stand as bench-tune
   guesses. RESOLVED.

All five locked → no design blockers. Ready to build on greenlight (Phase 1 =
SWR Cut + guards; Phase 1b = Fold option; Phase 2 = #170a drive cap; Phase 3 =
#170b over-power trip).
```

---

### Build order when greenlit
1. `TxProtect` evaluator skeleton on the stream side + Prefs + the 4 guards,
   SWR rule only (#169), latch + toast + lamp. Bench the dummy-load /
   no-antenna / low-power matrix.
2. #170a drive cap (clamp in `setTxDriveLevel`, TUN + BandMemory aware) + UI.
3. #170b over-power trip as the 2nd rule (cal-gated, opt-in).
4. (future) foldback mode for #169 + soft power-limit.

---

## ATT-on-TX — which implementation is live (resolved 2026-07-21)

Operator-flagged during a UI pass: **there are two ATT-on-TX implementations
in tree, and only one of them runs.** Recording the answer here because the
v0.3 PureSignal work has to hook the *live* one, and picking the wrong one
would produce a PS-A auto-attenuator that silently never reaches the wire.

**LIVE (authoritative):** inline in the MOX FSM on `HL2Stream` —
`fsmAdvance()` (`src/hl2_stream.cpp`) saves the operator's value into
`savedTxStepAttn_` and forces `setTxStepAttnDb(attOnTxEnabled_ ? attOnTxDb_ : 0)`
on the keydown edge, before the `mox_delay` window; the restore happens at the
keyup and keydown-cancel sites (three `setTxStepAttnDb(savedTxStepAttn_)` call
sites). Operator surface `attOnTxEnabled` / `attOnTxDb` (Q_PROPERTY + QSettings
`tx/attOnTx*`), Settings → TX group, TxPanel ATT lamp. This is the path that is
bench-validated — it is what drops the RX front end on key-down.

**DEAD:** `src/tx/AttOnTxPolicy.{h,cpp}`. Compiled (it is in `CMakeLists.txt`)
but **never constructed and never called** — the only references to the name
outside its own two files are `CMakeLists.txt` and two comments in
`hl2_stream.cpp` citing it as a style "precedent". Its own header still says
"Wire-inert at this stage: nothing constructs AttOnTxPolicy yet."

**Why the standalone class lost:** the same architectural decision recorded at
the top of this document for SWR protection — TX-safety features live ON
`HL2Stream` (same QSettings persistence, same `Stream.*` Q_PROPERTY exposure,
same mox-edge self-wire), "NOT the standalone QObject the pre-build summary
sketched," because consolidating onto the stream matches the established
TX-safety pattern and satisfies the off-GUI-loop requirement automatically.
`AttOnTxPolicy` is that superseded sketch, left behind when ATT-on-TX was built
the consolidated way.

**Action owed (per the no-patching rule — pick one, delete the other, do not
shim):** delete `src/tx/AttOnTxPolicy.{h,cpp}` and its two `CMakeLists.txt`
lines, first migrating the genuinely useful content out of its header into the
FSM call sites — specifically its docstring on the keyup no-op rationale
(off-MOX `compose_case_11` reads `rx_step_attn`, so a leftover `tx_step_attn`
has no RX-time effect) and its enumeration of what is deferred. Not done at the
time of writing: the operator was mid-way through a different thread and this
is a separate change with its own bench.

### What PureSignal must hook

`tx_step_attn` is the **shared actuator** — the same register carries the
operator's ATT-on-TX value and the PS-A auto-attenuator's dynamic value. Keep
`HL2Stream::setTxStepAttnDb()` as the single writer and have PS-A swap the
*source*, never the encoding (`set_tx_step_attn_db` owns the HL2 `31 - x`
inversion; no inline attenuator arithmetic anywhere else).

Deferred pieces that land with PS-A, none of which exist today:
* the PS-A auto-attenuator FSM dynamic value (the keyup restore is the hook
  point — today it is a plain restore of `savedTxStepAttn_`);
* per-band TX-att memory (the reference's per-band step-attenuator lookup);
* the reference's PS-conditional clauses (force-31 when PS-A is off / when
  drive or tune rises with PS-A on) — today it is a single global value.

Note for anyone reading a Thetis comparison: on the reference the displayed ATT
value **moves** while PureSignal runs, because PS-A is driving it. In Lyra it is
static, because `src/ps/` (`PsFsm`, `PsCalcThread`, `IqcLifecycle`) are empty
skeletons and nothing ever writes `puresignal_run`. A front-panel ATT readout
therefore shows a constant until PS lands — the existing TxPanel lamp already
reads `attOnTxDb` live and will show the movement for free once it does.

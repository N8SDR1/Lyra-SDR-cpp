# PureSignal — HL2-first, staged, bench-gated (Lyra-native)

Status: **DRAFT / planning** (2026-09-08). No code yet. HL2 first (documented
feedback path, operator has the PS hardware mod); P2/Brick feedback routing
ports on top afterward.

## 0. Method (the proven P2 approach)

- **Study the reference, call the engine, write the glue native.** The
  PureSignal *math* is WDSP and already lives in the bundled `wdsp.dll` — we
  call its exports (`SetPSControl`, `SetPSRunCal`, `SetPSMox`, `SetPSMoxDelay`,
  `SetPSFeedbackRate`, `SetPSHWPeak`, `GetPSInfo`, `GetTXAiqcValues`,
  `SetTXAiqcStart/Swap/End/Values`, `pscc`, `psccF`) exactly as the reference
  sequences them. **No re-port of `calcc.c`/`iqc.c` source is needed** — the
  DLL is byte-identical to the reference's and exports all of it (verified
  2026-09-08). `src/wire/wdspcalls.cpp` already resolves the PS symbols.
- **Two behavioral references — study, don't copy.** The reference C# console's
  `PSForm.cs` (state machine `timer1code`, auto-attenuator `timer2code`,
  `NeedToRecalibrate_HL2`) shows the sequencing/thresholds; **deskHPSDR
  (`D:/sdrprojects/deskhpsdr`) is the likely-easier reference** because it is
  **C** calling the same WDSP — its `pscc`/`psccF`/`SetTXAiqc*`/`SetPSControl`
  call order and the HL2 feedback-DDC handling map far more directly to Lyra's
  C++ than parsing the C# form. Prefer deskHPSDR for the *call sequencing +
  feedback routing*, cross-check thresholds/UX-states against `PSForm.cs`. Both
  are studied only; the FSM, lifecycle, and UI are **written Lyra-native** — no
  character-for-character copy, no reference names in shipped
  code/comments/commits (provenance lives here + in docs).
- **Every stage is RF-inert first, then bench-gated on real HL2 hardware
  before the next stage.** Smallest revertable step → operator bench → next.
  One careful design + one HL2 bench beats N convergence rounds.
- **WDSP upgrade is NOT a prerequisite.** The current DLL does standard
  PureSignal. WDSP 2.00 / PS 3.0 (NURBS) is a newer *algorithm* only — a
  decision gate inside P-3/P-4, taken only if the newer curve fit is wanted
  after basic PS works. Do not bump the DLL blind (re-validates the whole
  RX/TX chain against a new ABI for no confirmed gain).

## 1. Where we actually are

- **Confirmed:** all PS DLL exports present; `wdspcalls.cpp` binds the PS
  symbols; `src/ps/` skeleton exists (`PsFsm`, `IqcLifecycle`, `PsCalcThread`,
  `CalcCffi`, `IqcCffi`) as **stubs — not implemented, not wired into the
  build**; `src/wdsp/TxaCffi.h` covers the TXA iqc side.
- **HL2 TX** (SSB/CW/FM) is on-air and bench-passed; the TX chain the PS
  feedback loop wraps around is live and stable.
- **Not done:** the PS protocol surface (puresignal_run flags + the MOX+PS
  feedback-DDC reroute), the PS DSP wiring, the auto-attenuator, the PS UI,
  and coefficient persistence.

## 2. HL2 feedback facts to verify against the C++ wire layer

Ground these against `src/wire/` (`FrameComposer`, `NetworkProto1`,
`Ep6RecvThread`, `RadioNet`) + the HL2 gateware + `PSForm.cs`/`radio.cs`
**before** wiring — do not assume the Python-era notes hold 1:1 in this tree:

- `nddc = 4` (HL2 default). Enable PS = `puresignal_run` in frame 11 C2 bit 6
  **and** frame 16 C2 bit 6; the frame-0 C4 duplex bit is already set.
- **HL2 MOX+PS reroutes DDC0/DDC1 to the TX freq via `cntrl1=4`** (PA-coupler
  ADC → DDC0, DDC1 sync-paired at TX freq). **DDC2/DDC3 are gateware-disabled**
  during HL2 PS+TX — feedback is read from host channels **0 and 2**, not the
  DDC2/DDC3 twist. RX2's band is not received while PS runs (RX2 "paused").
- PS feedback sample rate on HL2 = **rx1_rate** (not the ANAN 192 kHz ps_rate).
- Auto-attenuator uses the HL2 TX step attenuator range **-28..+31 dB**;
  recalibrate trigger ≈ `FeedbackLevel > 181 || (FeedbackLevel <= 128 &&
  cur_att > -28)`.
- **Operator self-attestation** that the HL2 PureSignal hardware mod is
  installed — gate all PS controls behind it, default OFF.

## 3. Staged plan (each stage ends at a bench gate → commit)

### P-0 — Ground-truth + binding audit (no RF, no behavior change)
- Diff the PS exports actually bound in `wdspcalls.cpp` against the full
  lifecycle set; add any missing bindings.
- Read `PSForm.cs` `timer1code` (PS FSM), `timer2code` (auto-att),
  `NeedToRecalibrate_HL2`, and `radio.cs` PS channel setup. Map the HL2
  feedback DDC reroute against Lyra-P2's real wire layer.
- **Output:** a routing + binding dossier in this doc. Nothing ships.

### P-1 — Protocol surface: PS flags + feedback-DDC reroute (RF-inert)
- Emit `puresignal_run` (frame 11/16 C2 bit 6) behind an operator opt-in
  (default OFF, attestation-gated).
- Wire the MOX+PS DDC0/DDC1 → TX-freq reroute (`cntrl1=4`) so feedback IQ
  lands on host channels 0/2 when keyed. **PS correction stays OFF** (iqc
  bypassed) — this stage only proves feedback samples arrive.
- **Bench gate (dummy load):** with PS enabled + keyed, feedback IQ is
  present on the expected channels; with PS OFF, RX1/RX2/TX are byte-for-byte
  unchanged (no regression).

### P-2 — PS DSP wiring (RF-inert correction path)
- Implement the `src/ps/` skeleton Lyra-native and wire it into the build:
  `PsCalcThread` (semaphore-driven `calc()` on its own thread — matches the
  §5 threading model), `IqcLifecycle` (RUN/BEGIN/SWAP/END/DONE), `CalcCffi`/
  `IqcCffi` thin wrappers over the DLL exports.
- Plumb `SetPSControl` / `SetPSMox` / `SetPSMoxDelay` / `SetPSRunCal` /
  `SetPSFeedbackRate` on the MOX edges; poll `GetPSInfo` for state.
- **Correction still OFF** (`SetTXAiqcRun` off): validate the calc thread runs
  and `GetPSInfo` advances through states without touching the transmitted
  signal.
- **Bench gate:** PS state machine advances on key-up/down; zero effect on the
  air; no thread races on stop/restart (reuse the §15.21-class teardown care).

### P-3 — PS live + auto-attenuator + calibration (first real correction)
- Turn on iqc correction; port the auto-attenuator FSM (FeedbackLevel
  thresholds, HL2 -28..+31) Lyra-native.
- Coefficient persistence (per the app data dir).
- **HARD gate — dummy load FIRST:** the feedback loop closes, `GetPSInfo`
  reaches the correcting state, IMD visibly improves on a spectrum check, and
  there is **no runaway** (inert-first design + a hard drive/att ceiling).
  Only after a clean dummy-load pass → antenna.
- Requires the operator's HL2 PS hardware mod (attestation).

### P-4 — UI + polish + release
- A PS panel with **Lyra's own graphics and layout — operator-directed, NOT
  modeled on the reference's appearance.** `PSForm.cs` informs *what data /
  states* the panel must surface (PS state, single-shot calibrate,
  auto-attenuator + feedback-level readout, restore) — the *look* is designed
  fresh, and the operator specifies the graphics/layout direction when this
  stage starts (as with the glassy panadapter redesign). Only behavior is
  referenced; nothing about the visual design carries over.
- Settings attestation checkbox; help/wiki notes.
- Consolidate + release.

## 4. WDSP 2.00 decision gate (inside P-3/P-4)
Only revisit if PS 3.0 (NURBS) is wanted after standard PS is bench-stable.
Standard PureSignal ships on the current DLL. A DLL bump re-validates the
entire RX/TX chain against a new ABI — not worth it until there's a confirmed
algorithm reason.

## 5. Risk register
- **Feedback-loop runaway** if mis-scaled → inert-first (P-1/P-2), hard
  drive/att ceiling, dummy-load-first hard gate (P-3).
- **Wrong feedback DDC routing** → bench-verify feedback IQ presence at P-1
  before any DSP; the P1/HL2 routing is the documented cross-check.
- **RX2 pauses during PS** (DDC1 goes to TX freq) — surface it in the UI, not
  a silent dead RX2.
- **Operator-empirical overrides agent inference** — on any conflict at a
  bench gate, re-open and bisect; do not defend the design.

## 6. After HL2: P2 / Brick PureSignal
The P2/Brick feedback routing differs (see `p2_tx_puresignal_plan.md` T2/T3).
Do it **after** HL2 PS is bench-stable — the HL2 loop is the reference the P2
port is validated against.

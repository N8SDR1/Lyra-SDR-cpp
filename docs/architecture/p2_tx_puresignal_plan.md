# P2 TX + PureSignal — catch-up plan (solo, Lyra-native)

Status: PLAN (2026-09-05). Owner: N8SDR. Supersedes the "wait on Jerry"
posture — the P2 collaborator (KD4YAL) has had no activity since
2026-08-02, so the remaining P2 TX + PureSignal work is taken in-house.

## 0. Method (unchanged, proven)

Same discipline that shipped RX, first-RF, the S-meter, AGC-T:

- **Study** deskHPSDR (`D:/sdrprojects/deskhpsdr`) + Thetis
  (`D:\sdrprojects\OpenHPSDR-Thetis-2.10.3.13`) + the HL2/HL2+ gateware
  RTL (`Y:\Claude local\_hl2src`, the ak4951v4 variant) as ground truth.
- **Port the DSP math from WDSP directly, with attribution** (NOTICE) —
  WDSP is GPL v3+, lyra-cpp bundles it, so this is the sanctioned path.
  The TXA chain, `calcc`, `iqc`, the coefficient builder are WDSP.
- **Write the protocol/wire/UI/lifecycle glue Lyra-native** — no verbatim
  Thetis/deskHPSDR code, no reference names in shipped code/comments/
  commits. Provenance lives only here + memory.
- **Verify-first, then code. Stage it. Bench-gate every stage.** Operator
  hardware data outranks any agent/audit inference — on conflict, re-open.
- **Multi-agent review is a code-review tool, not a truth oracle.** One
  careful design + one operator HL2/Brick bench beats N convergence rounds.

## 1. Where we actually are (not "from scratch")

Already shipped / present:

- **P1/HL2 TX**: SSB + CW + FM, on-air, first-RF bench-passed (v0.4.1+).
  TXA chain via bundled WDSP; MOX/PTT FSM; ATT-on-TX; TX power model;
  TX protection; voice-keyer/VOX/clip-player scaffolding (`src/tx/`).
- **P2 TX RF-inert foundation** (`jerry/g2-p2-tx`, needs integration):
  bounded DUC-IQ transport, TX pacing, safe DUC setup, WDSP-TX-IQ →
  bounded P2 FIFO, fail-closed G2 P2 TX bench path, guarded two-tone
  test. `P2RxBridge` exposes `txProducerSeam`, `ducFifoSamples`,
  `txDriveLimitPercent`, `activateTxProducerSeam`.
- **PureSignal scaffolding** (`src/ps/`): `CalcCffi`, `IqcCffi`,
  `IqcLifecycle`, `PsCalcThread` — WDSP calcc/iqc cffi + lifecycle
  skeleton, not yet wired live.
- **TX design docs**: `tx1_ssb_design.md`, `cw_tx_design.md`,
  `fm_tx_design.md`, `tx_power_model_design.md`, `tx_protection_design.md`,
  `tx_audio_path_reference.md`, `STAGE_7_TX_WIRE_DESIGN.md`,
  `tx_research.md`.

The remaining work is therefore: **(A)** finish P2 TX to real RF on
G2/Saturn/Brick, **(B)** bring PureSignal live, **(C)** decide the WDSP
2.00 upgrade, **(D)** consolidate branches.

## 2. Licensing posture (locked)

lyra-cpp bundles WDSP (GPL v3+) → the project is copyleft-bound. The port
is legally clean provided: WDSP math is ported with attribution; all
protocol/UI/lifecycle is Lyra-native (no Thetis/deskHPSDR verbatim). If a
closed/commercial posture is ever wanted, that is a separate decision made
BEFORE more WDSP surface lands — flag, do not assume.

## 3. Staged plan

Each stage = grounded design → (light) review → implement → **operator
bench gate** → commit. No stage starts before the prior stage's bench
gate passes. RF-producing stages are HARD-gated (dummy load, then amp).

### T0 — Integrate `jerry/g2-p2-tx` (RF-inert) — ✅ ALREADY DONE
- Verified 2026-09-05: `lyra-p2` was **branched from** `jerry/g2-p2-tx`
  (`675f552` "graft first-class BrickSDR model onto g2-p2-tx"). merge-base
  == Jerry's tip `3df5219`; `lyra-p2..jerry/g2-p2-tx` is EMPTY. So Jerry's
  entire RF-inert P2 TX foundation is already the base of the Brick build —
  nothing to merge, no conflicts.
- Present + RF-inert on `lyra-p2`: `P2RxBridge` TX-producer-seam
  (`activateTxProducerSeam`), bounded DUC-IQ FIFO (`ducFifoSamples`), TX
  pacing, `txDriveLimitPercent`, fail-closed G2 P2 TX bench path, guarded
  two-tone test; `src/ps/` PS cffi scaffolding.
- ⇒ Proceed straight to T1.

### T1 — P2 TX SSB to real RF (G2 / Saturn / Brick)
- Complete the DUC-IQ → P2 wire path so keying produces a real carrier:
  MOX edge → TX-freq (RIT-free) → PA-enable → nonzero TX I/Q, mirroring
  the P1 first-RF sequence but over P2 (study deskHPSDR P2 TX + the
  gateware; Lyra-native).
- Reuse the P1 MOX/PTT FSM + ATT-on-TX + TX-power model; add the P2
  wire specifics only.
- HARD gate: bare rig, **dummy load, no amp**, TUN + SSB, watch PA
  current, then the kill-mid-TX PA-bias-drop safety test.

### T2 — PureSignal, RF-inert wiring
- Wire `src/ps/` (calcc + iqc cffi) into the TXA path: predistortion
  application (`iqc`) in the TX chain, calibration thread (`calcc`)
  fed by the feedback DDC. Port the WDSP math faithfully (attribution);
  write the PS lifecycle FSM + auto-attenuator + PSForm-equivalent UI
  Lyra-native.
- Feedback-DDC routing over P2: study deskHPSDR (it does ANAN-P2 PS) +
  the gateware; the P1 HL2 feedback routing is the cross-check.
- Keep it **inert** first (coefficients computed, not applied) so the
  math can be validated against a captured TX sample offline.

### T3 — PureSignal live + bench calibration
- Enable predistortion application; run the calibration loop on-air
  (operator's HL2 PS hardware mod).
- HARD gate: dummy load first (feedback loop closes, IMD improves,
  no runaway), then real antenna + amp, IMD/ALC watched.

### T4 — Consolidate + release
- Collapse the branch tangle (`feature/brick-p2-rx`, `lyra-p2`,
  `jerry/g2-p2-tx`, `main`) once T1–T3 are bench-stable; resolve the
  `HardwareCatalog.cpp` / `P2RxBridge.cpp` conflicts (operator picks the
  Brick hardware-profile winner).
- Doc + version bump + release ritual.

## 4. WDSP 2.00 decision gate (inside T2)

Memory (`reference_wdsp_version`): WDSP 2.00 ships PS 3.0 (NURBS); lyra
bundles ~1.x; prior guidance was "hold the upgrade until PureSignal." PS
is now, so evaluate at T2:

- **Upside**: better PS predistortion (NURBS), possibly simpler cal.
- **Cost**: bundled-DLL swap + cdef/API reconciliation + **re-validate
  every RX/TX path that touches WDSP** (RX chain, AGC, filters, TX chain).
- **Rule**: treat as a gated sub-decision, NOT a blind swap. Prototype
  the 2.00 PS path on a branch, A/B the calibration result vs 1.x on the
  dummy load, keep 1.x as the fallback until 2.00 clears the same RX/TX
  regression suite.

## 5. Risk register

- **On-air safety is the dominant risk** (PA, splatter, stuck carrier,
  hot-switching an amp). Every RF stage is dummy-load-first + the
  kill-mid-TX PA-bias-drop gate before antenna/amp.
- **PS feedback loop** can run away if mis-scaled — inert-first (T2)
  before live (T3); watch ALC/IMD.
- **Brick S-meter cal** (separate track) should be trimmed before trusting
  any absolute TX/PS power readout that leans on it.
- **Branch drift** — the longer T1–T3 run on parallel branches, the worse
  T4 gets; keep integrating to the P2 line as stages pass.

## 6. What this buys

Removes the collaborator dependency that stalled the project ~5 weeks.
The method needs no second person; Jerry's async responses were the
bottleneck, not the missing code. Jerry's RF-inert P2 TX groundwork +
the existing PS scaffolding are a real head start — this is a finish, not
a rewrite.

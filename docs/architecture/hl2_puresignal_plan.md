# PureSignal — HL2-first, staged, bench-gated (Lyra-native)

Status: **DRAFT / planning v2 (2026-09-08).** No code yet. Rewritten
after a 5-pass read-only reference study (deskHPSDR C source + WDSP
1.29 vs 2.00 diff + Lyra-P2 current-state audit). The v1 draft assumed
an iqc-coefficient-lifecycle model that the reference **disproved** —
see §0. HL2 first (documented feedback path, operator has the PS
hardware mod); P2/Brick feedback routing ports on top afterward, with
three axes baked in now (§5).

Provenance for every factual claim below is the reference dossier in
§8 (file:line into deskHPSDR / WDSP / Lyra-P2). No reference names or
citations go into shipped code/comments/commits — they live here only.

---

## 0. Method (corrected by the reference study)

- **Drive PureSignal through WDSP's high-level PS surface ONLY. Do NOT
  manage iqc.** The reference (deskHPSDR) drives all of PS through
  `pscc` + `SetPSControl` + `SetPSMox` + `SetPS{MoxDelay,LoopDelay,
  TXDelay,HWPeak,FeedbackRate}` + `GetPSInfo`/`GetPSMaxTX`. It **never**
  calls `SetTXAiqc*`, never touches iqc coefficient buffers, never uses
  `psccF`. WDSP applies the predistortion **internally** as a side
  effect of `pscc`/`SetPSControl`. The WDSP 1.29→2.00 diff confirms the
  `SetTXAiqc*` functions are internal-only (called only inside
  `calcc.c`) and were dropped from the public header.
  **CONSEQUENCE:** the v1 iqc scaffolding is deleted, not filled —
  `src/ps/IqcCffi.h`, `src/ps/IqcLifecycle.{h,cpp}`, `src/wdsp/
  TxaCffi.h`, and any plan to bind `SetTXAiqc*`. The calcc-drive
  binding table already resolved in `wdspcalls.cpp` is the entire
  WDSP surface PS needs.
- **Study the C reference, write the glue native.** deskHPSDR
  (`D:/sdrprojects/deskhpsdr/src`, primarily `transmitter.c` /
  `ps_menu.c` / `old_protocol.c` / `new_protocol.c`) is the primary
  reference — it is C calling the same WDSP, and its call sequencing +
  HL2 feedback routing map directly to Lyra-P2's C++ wire layer.
  Cross-check thresholds/UX against PSForm.cs where useful. Study only;
  FSM/lifecycle/UI are written Lyra-native, no reference names in
  shipped code.
- **Every stage is RF-inert first, then bench-gated on real HL2
  hardware before the next.** Smallest revertable step → operator
  bench → next.
- **WDSP 2.0 is a near-zero-change future upgrade — build PS FIRST on
  the current DLL.** The diff (§4) shows every PS call a host actually
  makes is byte-identical across 1.29↔2.00 (incl. `OpenChannel` — no
  ABI break) or strictly additive. Building PS now on the bundled ~1.x
  DLL and upgrading later is a *delete-six-calls-plus-add-one* edit;
  going 2.0-first would force absorbing the NURBS rewrite with no
  on-air baseline to bisect against. **PS-first is materially safer.**

---

## 1. Where we actually are (reconciled current state)

- **Bundled DLL is fully capable.** `_native/wdsp.dll` (5,552,640 B)
  exports the full calcc-drive set (`pscc`, `psccF`, `SetPS*`,
  `GetPSInfo`, `GetPSMaxTX`, `SetPSTXDelay`, …) **and** the iqc side
  (`SetTXAiqcStart/Swap/End/Values`, `GetTXAiqcValues`) — export scan
  confirmed. NOTE the bundled DLL is ~1.x-era: it has the 2.0-**removed**
  knobs (`SetPSPtol/PinMode/MapMode/Stabilize/IntsAndSpi`, `psccF`) and
  **lacks** the 2.0-only `SetPSDeadlockMinFrac`. There is **no**
  `SetTXAiqcRun` export (the stub comments name it wrongly) — the real
  iqc lifecycle is Start/Swap/End/Values, and we don't use it anyway.
- **Calcc-drive bindings: already resolved, never called.** `src/wire/
  wdspcalls.{h,cpp}` binds `pscc`/`SetPS*`/`GetPSInfo` via the
  GetProcAddress table. Zero call sites today. This is the table PS
  drives.
- **`src/ps/` is 100% stub** (`CalcCffi.h`, `IqcCffi.h`,
  `IqcLifecycle`, `PsCalcThread`, `PsFsm`): empty shells compiled into
  the binary, referenced by nothing. `PsCalcThread` + `PsFsm` get
  written; `IqcCffi.h` + `IqcLifecycle` + `wdsp/TxaCffi.h` get deleted
  (§0).
- **HL2 TX is on-air and bench-passed** (v0.22.0). The ATT-on-TX
  actuator PS needs (`31 − attenuation` on the wire) is **already
  shipped** as the single-writer TX-att encoding — PS drives the
  existing setter, no new wire encoding.
- **Not done:** the calcc consumer loop, the PS FSM + auto-attenuator,
  the HL2 `puresignal_run` wire bit on the nddc=4 path, the EP6
  feedback-DDC reroute, the MOX-edge PS arming, the PS panel, and
  coefficient persistence (`PSSaveCorr`/`PSRestoreCorr`).

---

## 2. HL2 feedback facts — byte-precise (verified against the C++ wire layer)

From `old_protocol.c` (P1/HL2), reconciled to Lyra-P2 `FrameComposer` /
`Ep6RecvThread`:

- **PS run flag = one bit:** round-robin `command==4` (C0=0x14), byte
  C2 bit 6 (0x40). (A secondary C0=0x24 C2-bit6 exists but is
  ANAN-scoped; HL2 sync is achieved by the DDC freq writes, not that
  bit.) Lyra's composer currently wires the PS guard only on the dead
  nddc=2 path — the nddc=4 HL2 path must emit it.
- **PS raises the DDC count 2→4** purely via `how_many_receivers()`
  (C0=0x00 C4 nrx field = `(4-1)<<3 = 0x18`); duplex bit (C4 bit2,
  0x04) mandatory and already set.
- **Feedback reroute = tune DDC2/DDC3 to the TX/DUC freq** — and the
  host ALWAYS does this (`tx[0].frequency` into both, every cycle),
  PS-on or PS-off. **No explicit coupler/ADC-mux bit on HL2** —
  confirmed against the HL2+ ak4951v4 gateware RTL: the second mixer
  (built expressly "for PureSignal support") selects
  `(tx_on & pure_signal) ? tx_data_dac : adc` and feeds **receivers 1
  and 3**, while the first mixer feeds **receivers 0 and 2** from the
  ADC. Nothing is gateware-disabled and no DDC is co-tuned by the
  gateware.
- **⚠ DDC ROLES (corrected 2026-09-11 — earlier drafts had these
  swapped, and a later "DDC0/DDC1 + `cntrl1=4`" model was wrong
  outright; see §8):** during MOX+PS —
  **DDC2 = ADC / PA-coupler @ TX freq = the FEEDBACK** → `pscc` **`rx`**
  argument; **DDC3 = DAC loopback @ TX freq = the REFERENCE** →
  `pscc` **`tx`** argument. (In `calc()` the `tx` array is the
  reference envelope and `rx` is the PA feedback.) DDC0/DDC1 stay at
  the RX1/RX2 VFOs and carry garbage for PS purposes — RX is stopped
  at MOX anyway, so do NOT route them and do NOT display them as RX.
- **EP6 dispatch during MOX+PS:** the calc is fed from router
  **source 1** = the existing `twist(DDC2, DDC3)` pair. In Lyra this
  is the `Ep6RecvThread` **case-4** block (`DDC0→src0`, `DDC1→src2`,
  `DDC2/3 twist→src1`) — **already the correct routing; attach a
  `pscc` sink to source 1 rather than re-routing anything.**
- **Feedback rate on HL2 = the current RX rate** (48/96/192/384k) on
  the wire — all DDCs share RX1's rate. NOTE the reference still tells
  the calc `SetPSFeedbackRate(192000)` regardless (it only scales
  internal delay/timeout constants, not the correction math). PS
  quality wants 192k — steer the operator there when armed.
- **Auto-attenuator (HL2, the MI0BOT/Thetis reference):** recal trigger
  `FeedbackLevel > 181 || (FeedbackLevel <= 128 && att > −28)`
  (`info[4]`); delta = `round(20·log10(FeedbackLevel / 152.293))` with
  the HL2 clamps; attenuator range **−28..+31 dB**; `SetPSControl`
  reset before / restore after. HL2 hardware-peak scale =
  `SetPSHWPeak(0.233)`. The wire att is `31 − attenuation` — the
  single-writer encoding Lyra already ships.
- **`GetPSInfo` FSM value:** `info[15]` = control state, enum
  `LRESET=0…LTURNON=9`, **byte-identical across WDSP 1.29/2.00**. Also
  read: `info[4]` level, `info[5]` cal counter (new-cal edge),
  `info[14]` correcting flag.
- **Operator self-attestation** gates all PS controls, default OFF.

---

## 3. Build map (HL2) — what exists vs what to write

**Already present (reuse, don't rebuild):**
- calcc-drive binding table (`wdspcalls.cpp`) — resolved.
- ATT-on-TX `31−att` single-writer wire encoding — shipped.
- MOX FSM edges `HL2Stream::fsmKeydownPostMox()` / `fsmKeyupPostSpace()`
  — clean PS-arm attach points.
- TX-freq→DDC2/DDC3 mirroring (`set_tx_freq`).
- TXA channel 1 (`create_xmtr`) running the transmit chain; pre-iqc
  siphon tap in place. **No iqc attach needed** (§0).

**To write (the PS-functional work):**
1. **`PsCalcThread`** — two 1024-sample feedback buffers (TX-reference +
   RX-feedback) + `pscc(txch, 1024, txref, rxfb)` per full block. CW
   excluded (WDSP not used for CW TX).
2. **`PsFsm`** — Lyra-native port of deskHPSDR's model: `SetPSControl`
   reset→resume(automode) enable, one-shot via `mancal`; `SetPSMox` on
   the MOX edges; parameter push (`SetPSHWPeak`, `SetPSMoxDelay`,
   `SetPSTXDelay`, `SetPSLoopDelay`, `SetPSHWPeak(0.233)` — **skip
   `SetPSDeadlockMinFrac`**, it's 2.0-only/absent); `GetPSInfo[15]`
   poll for UI; the auto-attenuator (recal `FB>181 || (FB<=128 &&
   att>−28)`, range −28..+31) driving the existing att setter.
3. **EP6 feedback-DDC tap** — attach the calc consumer to
   `Ep6RecvThread` case-4 **source 1** (the existing `twist(DDC2,DDC3)`)
   under a `(mox && ps_armed)` predicate; DDC2→`pscc rx`, DDC3→`pscc
   tx`. **Keep the existing routing** — no re-route, the twist is
   already correct.
4. **HL2 `puresignal_run` wire bit** — emit C0=0x14 C2 bit6 on the
   nddc=4 path.
5. **MOX-edge PS arming** — `SetPSMox(1)`+start calc thread on keydown,
   `SetPSMox(0)`+stop on keyup (mirror the reference edge order).
6. **PS panel** (operator-directed graphics/layout) + attestation +
   coefficient persistence (`PSSaveCorr`/`PSRestoreCorr`).
7. **iqc: nothing** — deleted from scope; delete the iqc stubs.

---

## 4. WDSP 2.0 — resolved: build PS first, upgrade is trivial

Diff of `deskhpsdr/wdsp-1.29` vs `wdsp-2.00`:
- **Every PS host call is IDENTICAL across versions** (`pscc`,
  `SetPSControl`, `SetPSMox`, `SetPSRunCal`, `SetPS{MoxDelay,LoopDelay,
  TXDelay,HWPeak,FeedbackRate}`, `GetPSInfo`, `GetPSMaxTX`,
  `PSSaveCorr/RestoreCorr`) **including `OpenChannel`** — no ABI break,
  no whole-chain re-validation.
- **`GetPSInfo[15]` state enum is byte-identical** — the FSM contract
  is stable.
- **Removed in 2.00 (do NOT call, even though the ~1.x DLL binds
  them):** `psccF`, `SetPSPtol`, `SetPSPinMode`, `SetPSMapMode`,
  `SetPSStabilize`, `SetPSIntsAndSpi`. Use the `double* pscc` path only.
- **New in 2.00 (optional):** `SetPSDeadlockMinFrac` — add at upgrade,
  PS runs without it.
- The NURBS/PS-3.0 rewrite is entirely behind the DLL boundary
  (private structs, internal `SetTXAiqc*`) — no host touches it.

**Upgrade path = delete-six-calls-plus-add-one + re-trim the wdspcalls
table** (drop the six removed symbols so GetProcAddress doesn't null
them on the 2.0 DLL). Confirm the 2.0 DLL's export table via dumpbin
before the swap (the plan's original §4 gate). **Ordering: PS-first.**

---

## 5. P2 / Brick — three axes to bake in NOW, one hardware gate

deskHPSDR's P2 PS (`new_protocol.c`) confirms the HL2-first invariants
carry over (feedback DDC at TX freq, one-flag run enable, coupler-ADC
select, feedback as an extra DDC host-channel). Lyra's P2 session
already exposes every hook (all 10 DDCs freq/ADC/rate-addressable; PS
run bit is a reserved seam at `P2Session.cpp:265`; `iqFrameReceived`
carries the DDC index). **PS plugs in additively** — provided the
HL2-first design honors three P2 axes from the start:

1. **Feedback rate is an independent `ps_rate` (fixed 192k on P2), NOT
   `rx1_rate`.** Make feedback rate its own axis in the PS abstraction.
2. **Feedback topology is an explicit choice** — P2 uses a two-DDC
   (RX-feedback + TX-DAC-loopback) pair synced via the `[1363]=0x02`
   interleave word, which Lyra's `buildDdcSpecificPacket` leaves zero.
   Decide single- vs two-DDC PS feedback now.
3. **Replace `P2RxBridge`'s `if (ddc != 0) return`** (single-DDC
   consumer) with a DDC→consumer dispatch table (RX2 needs this too).

**Hardware gate (bench, same class as the HL2 mod):** deskHPSDR has
**zero Brick PS precedent**, and Lyra's Brick path uses a fixed
captured TX front-end constant with **no ADC-mux / PA-coupler control
surface**. Whether the BrickSDR2 can present a PA-coupler sample to a
feedback DDC is **UNVERIFIED** and must be answered on hardware before
any Brick-specific PS work. HL2 PS is fully designable now regardless.

---

## 6. Staged plan (each stage ends at a bench gate → commit)

### P-0 — Ground-truth + scope-correction (no RF, no behavior change)
- Delete the iqc stubs (`src/ps/IqcCffi.h`, `IqcLifecycle.{h,cpp}`,
  `src/wdsp/TxaCffi.h`) and their CMake entries; fix the stub comments
  naming the non-existent `SetTXAiqcRun`.
- Confirm the wdspcalls table binds the calcc-drive set we call (it
  does) and note the six 2.0-removed symbols to avoid CALLING.
- Write the routing + binding dossier (this doc + §8) as the map.
- **Output:** corrected scope, deleted dead scaffolding. Nothing ships.

### P-1 — Protocol surface: PS flags + feedback-DDC reroute (RF-inert)
- Emit `puresignal_run` (C0=0x14 C2 bit6) on the nddc=4 HL2 path, behind
  operator opt-in (default OFF, attestation-gated).
- Make `Ep6RecvThread` case-4 `(mox, ps_armed)`-aware so feedback IQ
  lands on the calc consumer when keyed. **Correction stays OFF** (no
  calc yet) — this stage only proves feedback samples arrive.
- **Bench gate (dummy load):** with PS enabled + keyed, feedback IQ is
  present on the expected channels; with PS OFF, RX/TX byte-for-byte
  unchanged.

### P-2 — Calc consumer + FSM wiring (RF-inert correction path)
- Write `PsCalcThread` (the `pscc` feed) and `PsFsm` (SetPSControl /
  SetPSMox / param push / GetPSInfo poll), arm/disarm from the MOX
  edges. **Do not enable correction yet** beyond what WDSP does
  internally on `pscc` — validate the calc thread runs and `GetPSInfo`
  advances through states with the radio into a dummy load.
- **Bench gate:** PS state machine advances on key-up/down; no thread
  races on stop/restart; RX/TX otherwise unchanged with PS off.

### P-3 — PS live + auto-attenuator + calibration (first real correction)
- Port the auto-attenuator (recal `info[4]>181 || (info[4]<=128 &&
  att>−28)`, range −28..+31) driving the existing att setter;
  coefficient persistence.
- **HARD gate — dummy load FIRST:** `GetPSInfo` reaches the correcting
  state, IMD visibly improves, no runaway. Only after a clean
  dummy-load pass → antenna. Requires the operator's HL2 PS mod.

### P-4 — UI + polish + release
- Lyra-native PS panel (operator-directed graphics/layout — PSForm.cs
  informs *what data/states* to surface, not the look), attestation
  checkbox, help notes. Consolidate + release.

---

## 7. Risk register
- **Feedback-loop runaway** if mis-scaled → inert-first (P-1/P-2),
  dummy-load-first hard gate (P-3), the existing att ceiling.
- **Wrong feedback DDC routing** → bench-verify feedback IQ presence at
  P-1 before any calc.
- **RX pauses during PS** (DDC0/DDC1 at TX freq) — surface in the UI,
  not a silent dead RX.
- **Calling a 2.0-removed knob** (psccF/SetPSPtol/…) → build to the
  minimal identical surface; never call the six.
- **Operator-empirical overrides agent inference** — on any bench
  conflict, re-open and bisect; do not defend the design. (This plan is
  itself the product of reading the reference first, per the locked
  methodology.)

---

## 8. Reference dossier (provenance — file:line, kept out of shipped code)

**deskHPSDR PS engine** (`transmitter.c`, `ps_menu.c`, `radio.c`):
- Drives PS via `pscc`/`SetPS*`/`GetPSInfo` only; **never** `SetTXAiqc*`
  or `psccF` (grep = 0 host hits). iqc applied internally by WDSP.
- Feed: `pscc(txid, 1024, txref_buf, rxfb_buf)` per full block
  (`transmitter.c:1922`); two host feedback RXs at `radio.c:1299-1305`,
  HL2 rate = active RX rate.
- Enable: `usleep(100000)` → `tx_ps_resume` (`SetPSControl(id,0,0,1,0)`
  automode) → `tx_ps_setparams` (`transmitter.c:2253-2325`).
- Params: `SetPSHWPeak`/`SetPSMoxDelay`(0.2)/`SetPSTXDelay`(150ns)/
  `SetPSLoopDelay`(0)/`SetPSDeadlockMinFrac`(2.0-only)
  (`transmitter.c:2347-2360`).
- MOX order: keydown `SetPSMox(1)`→`tx_on` (`radio.c:2003-2008`); keyup
  `SetPSMox(0)`→`tx_off` (`radio.c:2027-2030`).
- Auto-att: `ps_calibration_timer` (`ps_menu.c:154-245`), window
  140–165, HL2 −29..+31, only armed during two-tone.
- `GetPSInfo` states: `info[15]` LRESET=0…LTURNON=9; `info[4]` level,
  `info[5]` cal counter, `info[14]` correcting.

**deskHPSDR HL2 wire (`old_protocol.c`):**
- PS bit: `command==4` C0=0x14 C2 |= 0x40 (`:2652-2654`).
- nrx 2→4: `how_many_receivers()` (`:1513-1517`); C4 nrx field
  (`:2443`); duplex C4=0x04 (`:2436`).
- Reroute: `channel_freq()` → `vfonum=-1` (TX freq) for feedback DDCs
  during MOX+PS (`:1438-1443`); no HL2 ADC-mux bit (mux write no-op,
  `:2731-2737`).
- Dispatch: DDC2→rx, DDC3→tx → `tx_add_ps_iq_samples`
  (`:1856-1872`); normal RX suppressed in simplex TX (`:1889-1898`).
- HL2 TX att `31 − attenuation` (`:2671`, `:2745`).

**WDSP 1.29↔2.00 diff** (`wdsp-1.29` / `wdsp-2.00`):
- Host PS calls IDENTICAL incl. `OpenChannel`; `GetPSInfo[15]` enum
  identical (`calcc.c:465-476` / `1190-1201`).
- Removed in 2.00: `psccF`, `SetPSPtol`, `SetPSPinMode`, `SetPSMapMode`,
  `SetPSStabilize`, `SetPSIntsAndSpi`. New: `SetPSDeadlockMinFrac`
  (`calcc.c:1559`). `SetTXAiqc*`/`GetTXAiqcValues` radically changed but
  host-uncalled. Verdict: PS-first, delete-6-add-1 upgrade.

**deskHPSDR P2 + Lyra P2 layer** (`new_protocol.c` / `P2Session.cpp` /
`P2RxBridge.cpp`):
- P2 run flag = `ALEX_PS_BIT` in HP Alex0/1 (`:1276-1279`); feedback on
  DDC0/DDC1 synced via `[1363]=0x02`, **fixed 192k ps_rate**
  (`:1852-1870`); coupler via `PS_RX_FEEDBACK->alex_antenna`
  (`:1468-1469`). No Brick device in deskHPSDR.
- Lyra P2: all 10 DDCs addressable (`P2Session.cpp:271`); PS bit
  reserved off (`:265`); interleave sync word left zero (`:242`);
  `P2RxBridge` consumes DDC0 only (`:64`); Brick uses fixed TX
  front-end constant (`:303-310`) — no coupler ADC surface.

**Lyra-P2 current state:**
- calcc-drive bound `wdspcalls.{h,cpp}` (unused); `src/ps/` all stubs;
  iqc side unbound + to-be-deleted; EP6 case-4 reroute hook
  `Ep6RecvThread.cpp:801-813`; MOX edges `hl2_stream.cpp:3228`/`:3386`;
  TXA ch1 `CMaster.cpp:201`; bundled DLL exports verified (has ~1.x
  knobs, lacks `SetPSDeadlockMinFrac`, no `SetTXAiqcRun`).

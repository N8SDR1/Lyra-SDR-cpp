# VAC2 — second virtual audio cable (#103) design

**Status:** V2-0 through V2-3 **shipped** — two IVAC slots, Settings VAC1 +
VAC2, RX2 tee (`xvacOUT(1)`), Mic source **PC Soundcard (VAC2)** (`micpc2`).
VAC2 RX is silent when SUB is off (cable stays open). One modulator: TCI
wins; else explicit `micpc` / `micpc2`; else auto-digital (VAC1 preferred
if both). Profile `vac2*` fields are **V2-4** (not in this pass).
WDSP 2.10 + PureSignal are **not** in this step.
**Scope:** #103. A second, fully-independent full-duplex VAC (RX-out **and**
TX-in), mirroring VAC1, that carries **RX2's** audio to/from its own PC device
pair — exactly Thetis's VAC2 (bound to the second receiver).
Operator: full-duplex like VAC1, VAC2 = RX2's cable.

## Dependency
Thetis's VAC2 is the **second receiver's** audio cable (`cmaster.cs:924,
941-944` — `VAC2Enabled` ties VAC2 to `RX2 = WDSP.id(2,0)`). Lyra **SUB/RX2 is
shipped** (WDSP ch2, DDC1). VAC2 Settings + RX2 tee + VAC2-as-TX are live.
**Remaining:** V2-4 Profile `vac2*` fields; V2-5 Brent/Timmy enable/disable
crash-surface re-bench on **both** cables.

## HL2 PureSignal × VAC2 (when PS lands)
On HL2, MOX+PS reroutes DDC1 to TX freq, so RX2 is not VFO B. **VAC2 RX must
pause** in that state — stop `xvacOUT(1, …)` while `(mox && ps_armed)` on HL2;
restore on un-key. VAC1 RX may need the same pause (DDC0 is also PS feedback
on HL2) — confirm against the PS plan when wiring. Digital modes still **turn
PS off** (lock in `dsp_options_design.md`); that is PS-bring-up, not VAC2.
WDSP 2.10: re-check IVAC/rmatch ABI on DLL swap.

## Ground truth (mapped 2026-06-19)
- **Engine `wire/Ivac` is already 2-VAC** — `pvac[MAX_EXT_VACS]`; every
  function takes `int id` (`create_ivac`/`destroy_ivac`/`xvacIN`/`xvacOUT`/
  `Start`/`StopAudioIVAC`/all `SetIVAC*`). VAC2 = the same calls with `id=1`.
  **No engine change needed.**
- **All Lyra scaffolding is single-VAC** (`kVac1Id=0`, `vac1*`): the
  `wdsp_engine` device layer (`rebuildVac1`/`teardownVac1`/`vac1Enabled_`/
  names/gains/combine/host-API/`vac1Active_`/`vacMtx_`/`vacMox_`/
  `vacRxScaled_`/`muteWillMuteVac_`), the RX→VAC tee in `dispatchAudioFrame`
  (`xvacOUT(kVac1Id,…)`), the TX pump `vacInboundCb` (VAC1-only,
  `xvacIN(kVac1Id,…)`), the Settings VAC1 group (`vac1/*` keys), and the 4
  Profile fields.
- **TX-audio is single-source** (`applyTxAudioSource`): mic source ∈
  {codec EP6 / tci / vac}; exactly one armed. The radio has ONE modulator,
  so only one VAC ever feeds TX at a time — VAC2 adds a *selectable* source,
  not a second simultaneous TX path.

## The care item (not the UI — the concurrency)
The whole v0.3.x crash arc (Brent/Timmy, field-closed 2026-06-18) lived on
`vacMtx_` + `vac1Active_` + teardown ordering (UAF in the mix tee + the
inbound pump). **A second VAC doubles that surface.** So the guards go
**per-VAC**, the teardown ordering is preserved per-VAC, and VAC2 gets the
same Brent/Timmy enable/disable/device-swap/profile-flip bench gate. This is
the real work + risk; it is NOT a copy-paste.

## Design (no-patching: parameterize, don't twin)
1. **Per-VAC state struct, `vac_[MAX_EXT_VACS]`.** Move the single-VAC Lyra
   fields into `struct VacState { bool enabled, autoDigital, combineInput;
   QString outName, inName, hostApiName; double rxGainDb, txGainDb;
   std::atomic<bool> active_; std::mutex mtx_;
   std::atomic<bool> muteWillMuteVac_; std::vector<double> rxScaled_; }`.
   Shared (not per-VAC): `vacMox_`, `vacMonSilence_`, `vacMonStereo_`,
   `vacEnvApplied_`. Public QML/Settings stay `vac1*` wrappers until V2-2.
   Existing VAC1 call sites use `vac_[0]` / `kVac1Id`. `g_aamixOutboundSelf`
   stays one self-ptr (the statics already route by `id`).
2. **Device layer `rebuildVac(int id)` / `teardownVac(int id)`** (replace
   `rebuildVac1`/`teardownVac1`); each reconciles its own VacState +
   `Start`/`StopAudioIVAC(id)` + `create`/`destroy_ivac(id)` with the
   per-VAC guard. Two full-duplex PortAudio streams (one device pair each).
3. **RX→VAC tee:** VAC1 tees RX1 audio (today's `dispatchAudioFrame`); **VAC2
   tees RX2 audio** off RX2 Phase 2's output (#97). Each VAC has its own
   `rxScaled_` (gain + per-VAC mute-will-mute) → `xvacOUT(id,…)`. (The tee is
   per-receiver, not a single loop over one RX — VAC1↔RX1, VAC2↔RX2, matching
   Thetis.)
4. **TX ownership:** one `txSourceVacId_` (0 or 1) set by mic source. The
   inbound pump `vacInboundCb` reads it → gates under `vac_[that].mtx_/
   active_` → `xvacIN(txSourceVacId_,…)`. `use_vac_audio` stays a single
   flag; `txSourceVacId_` selects which VAC feeds the modulator. TCI-vs-VAC
   arbitration unchanged.
5. **Mic source** gains **"PC Soundcard (VAC2)"** → arms `use_vac_audio` +
   `txSourceVacId_=1`. ("PC Soundcard (VAC1)" → `txSourceVacId_=0`.)
6. **`setVacMox`** applies to BOTH active VACs (RX muted on TX for each).
   TX-monitor (MON) stays id-0-only per the reference — moot until the
   deferred Stage-5 monitor tap; mon stays 0 on both now.
7. **Settings UI:** a second VAC2 group mirroring VAC1 (Enable / auto-digital
   / Driver / Output / RX gain / Input / TX gain / Combine / Mute-will-mute),
   `vac2/*` QSettings keys.
8. **Profile:** mirror the 4 fields → `vac2Enabled/vac2AutoDigital/
   vac2RxGainDb/vac2TxGainDb` (capture/apply/sameValues/JSON; devices stay
   global, like VAC1).

## Build order
- **V2-0 … V2-3** — **shipped** (engine slots, Settings, RX2 tee, `micpc2`).
- **V2-4** — Profile `vac2*` fields.
- **V2-5** — USER_GUIDE polish + Brent/Timmy crash-surface re-bench
  (enable/disable + device-swap on BOTH VACs + profile flips).

## Thetis grounding (verified 2026-06-19 — "follow Thetis")
- `MAX_EXT_VACS = 16` (`ivac.h:34`) — engine handles far more than 2; VAC2 = id 1.
- **VAC2 (ivac id 1) is a fully symmetric full-duplex VAC in Thetis** —
  `audio.cs` drives the complete id-1 setter set: `SetIVACmox(1)`,
  `SetIVACmon(1)`, `SetIVACvox(1)`, `SetIVACcombine(1)` (VAC2CombineInput),
  `EnableVAC2`, per-VAC latency, `SetIVACbypass(1)`, `SetIVACRBReset(1)`. So
  "full-duplex like VAC1" = Thetis's VAC2 exactly; mirror the id-1 setters.
- **MON/monitor is NOT id-0-only** — `cmaster.c:574-575` sets the TX-monitor
  rate/size on **id 1** (`vacOUT1`). Corrects my earlier "monitor id-0-only"
  note. Moot until the deferred Stage-5 monitor tap, but when it lands the
  monitor reference is id 1, not id 0.
- **Thetis VAC2 RX feed is RX2-bound** (`cmaster.cs:924,941-944`
  `VAC2Enabled` → anti-VOX source `RX2 = WDSP.id(2,0)`). **Lyra follows
  Thetis: VAC2 carries RX2's audio** → the RX→VAC2 tee feeds off RX2 Phase 2's
  output (#97), NOT RX1. This is the dependency that defers #103 behind RX2.
  (A "VAC1 uses RX2" option also exists in Thetis; out of scope — VAC1 stays
  RX1, VAC2 = RX2.)
- **TX arbitration:** one modulator → one TX-audio source at a time. Mic
  source picks the VAC (`txSourceVacId_`); both VACs RX simultaneously. This
  is the Lyra-faithful realization of Thetis routing one app's audio to TXA
  (Thetis's full two-VAC-to-TX PIPE is more elaborate; not needed for the
  single-source-at-a-time operator model).

## Out of scope
"VAC1 uses RX2" source-swap option (Thetis has it; VAC1 stays RX1 here);
TX-monitor/MON on VAC2 (Stage-5 monitor-tap dependent; reference id is 1);
true mono-capture mode (combine-ON covers it, per VAC1).

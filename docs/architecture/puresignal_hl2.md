# PureSignal on Hermes Lite 2 / HL2+ (Lyra-P2)

Authoritative host-side plan for the first PureSignal slice. **Hardware:
HL2 / HL2+ with the PureSignal coupler mod, dummy load only.** Brick P2 is
documented here and scaffolded in protocol (P7); it is **not** first RF.

WDSP is **not** bumped for this work. The bundled `wdsp.dll` already exports
`pscc` / `SetPS*` / `GetPSInfo`. Host work is protocol + DDC remux + FSM /
auto-att + UI.

## Locked routing (HL2)

**Thetis-faithful, not DeskHPSDR’s HL2 idle picture.**

| Item | Value |
|---|---|
| Attestation | Operator checkbox, **default OFF**. No wire PS until checked. |
| `puresignal_run` | C2 bit 6 on P1 frames 11 (`0x14`) and 16 (`0x20`) when armed **and** attested. Stays 1 in RX so the radio is primed; mux stays off. |
| ADC mux | `P1_adc_cntrl = 4` (`cntrl1=4`) **only** when `mox && ps_armed && attestation && family==Hl2`. Frame 4 C1/C2. Routes the PA coupler into DDC0; DDC1 is sync-paired at TX frequency. |
| Feedback streams | **DDC0 = coupler (rx into `pscc`)**, **DDC1 = TX replica (tx into `pscc`)** at the RX1 wire rate. |
| Idle DDC map | RX-only: DDC0→RX1, DDC1→RX2, twist(DDC2,DDC3) unused. Capability field `psDdcFirst=2` describes **idle** DeskHPSDR numbering (RX3/RX4), **not** the live Thetis MOX+PS mux. |
| SUB | Pause dual-RX **without** calling `setSubEnabled` (that setter persists). Transient `subPausedForPs`. |
| Captured-profile | Bypass Wiener apply on `(mox && ps_armed)` without clearing the operator `applyEnabled_` flag. |
| sip1 | TX panadapter (`TXASetSipDisplay`). **Not** the calcc feed. |
| Host `SetTXAiqc*` | **Do not call.** DeskHPSDR never does; WDSP 2.0 dropped the public iqc wrappers. `pscc` drives the engine. |
| Auto-att writer | **HL2:** `setTxStepAttnDb` (−28…+31). **Brick/Hermes P2:** ADC0 coupler pad (0…31). Last pad **persists across PTT** (Thetis `ATTOnTX` / DeskHPSDR `ps_tx_att`). Seed is first bind only, not every MOX. Recal only when `info[5]` (cal attempts) **changes** and FB is outside 129–181. Delta `round(20·log10(FB/152.293))`; FB>256 uses a coarse +10/+15 step. Target ~152. FB is coupler ADC counts, not watts. |

`ddc_map(mox, ps_armed, rx2, family)` is the state-product helper. Live HL2
row: `adcCntrl1=4`, `feedPsccFromDdc0Ddc1`, `pauseSub`, `bypassCapturedProfile`.

If dummy-load PS has **no coupler energy after `cntrl1=4`**: capture Thetis C&C
on the **same** HL2+. Do **not** silently switch to DeskHPSDR HL2 `nrx=4`
RX3/RX4 without a new operator lock.

**FB vs drive (N8SDR HL2+, dummy, ATT-on-TX −31, 2026-09-26):** coupler
samples can be in `pscc` (`in` ~48k, D0 not −120) while **FB stays 0** at
**2–3 W**. That is envelope vs min LNA, not a dead mux. ~5–8 W on this
unit brings D0 up (~−18 dBFS) and FB into the ~130–160 band (138 measured;
Thetis 139 on the same radio). Do not treat low-watt FB=0 as a routing bug.

## DeskHPSDR host dataflow (keep)

`tx_add_ps_iq_samples` → `pscc(channel, size, tx, rx)` from **radio IQ pairs**.
Lyra: EP6 thread feeds `PsCalcThread` 128-sample blocks → `pscc(1, n, tx=DDC1, rx=DDC0)`
(TXA channel id 1). WDSP owns the calc thread.

## Brick PureSignal (P7 later — DeskHPSDR is first-class)

The old line “No Brick device in deskHPSDR” is **false**. DeskHPSDR **does**
run PureSignal on Brick.

- MAC `02:B2` / `02:B3` → `HERMES_MODE_BRICK`, `filter_board = ALEX`
  (`radio.c` ~2824–2829; `HERMES_MODE_BRICK = 2` in `radio.h`).
- `PS_TX_FEEDBACK = RECEIVERS` (2), `PS_RX_FEEDBACK = RECEIVERS+1` (3) are
  **host receiver object indices**, not wire DDC numbers. On Hermes/Brick
  P2, wire feedback is **DDC0 + DDC1**. Restore path calls `tx_ps_onoff`
  (`radio.c` ~1848–1850). `ps_menu.c` has **no** Brick exclusion.
- P2 Hermes-class (including Brick2): while `xmit && transmitter->puresignal`,
  **DDC0 and DDC1 frequency words lock to the DUC/TX freq**
  (`new_protocol.c` ~1580–1591). Run flag **`ALEX_PS_BIT` (bit 18,
  `0x00040000`)** on Alex0 while keyed and Alex1 whenever PS is on
  (`new_protocol.c` ~1744–1747, `alex.h`).
- `brick_ddc0_fix` still writes DDC0 = DUC during TX **even with PS off**
  (`new_protocol.c` ~1592–1599) so cross-band TX with RX2 as TX VFO still
  produces RF. Keep that overlay; PS must not fight it — when PS is live,
  lock **both** DDC0 and DDC1 to TX.
- Brick3 / Angelia extras (`p2_diversity_brick3_mode_active`,
  `p2_angelia_ddc0_map`) **must not** override the PS TX DDC0/DDC1 lock
  (`new_protocol.c` ~1548–1554).

**Thetis fallback policy (locked):**

- Brick2: treat as **Hermes-class P2**, never 7000/8000 / Saturn. Testers
  (Timmy + others, Ramdor/Ritchie Thetis) confirm: Brick2SDR + **rig type
  HERMES** + PureSignal works. That is the same wire as DeskHPSDR
  `HERMES_MODE_BRICK` (DDC0+DDC1, receive-specific sync, TX replica on
  `n_adc`). Do **not** pick ANAN-7000/8000 in that Thetis — that is a
  different DDC/ADC map.
- Brick2 host path: DeskHPSDR + that Thetis **HERMES** config are the
  same class. Thetis P2 **ANAN** mux is **second** only if a dummy-load
  capture on *that* Brick shows Hermes-class DDC/Alex words do not
  engage the coupler.
- Brick3 (catalog ≈ ANAN-100D / Angelia): read DeskHPSDR `NEW_DEVICE_ANGELIA`
  first (still DDC0+DDC1 at TX freq when PS is on). If DeskHPSDR and Thetis
  **disagree** on ADC/coupler mux, **Thetis wins for the mux**; DeskHPSDR
  still wins for `pscc` / never calling host `SetTXAiqc*`.
- Do **not** invent a Brick-specific coupler register.

P7 implements the DeskHPSDR P2 bits (gated, default off). First RF stays HL2.

**Host `pscc` on Brick (required — RF alone is not enough):** P1 feeds
`PsCalcThread` from EP6. P2 IQ never hit that path, so the dock showed
`in=0` / `FB=0` even with more power than an HL2 can make.

Hermes-class live PS TX (Brick dummy 2026-09-26: 7.7 W, DDC0=DDC1=0 pkt/s
with two independent streams + ADC0 att 31):

- DDC-specific: **enable DDC0 only**, **sync word [1363]=0x02**, DDC0
  ADC=0 (coupler), DDC1 ADC=`n_adc` (TX replica — do not clamp to 0),
  both slots 192 kHz.
- One combined IQ UDP on 1035: even samples = coupler, odd = TX replica.
- ADC0 att during PS is the live RX/feedback att, **not** ATT-on-TX 31.
- DDC2/DDC3 unused. DDC1 is not routed to RX2 audio while keyed.

## Safety

Dummy load. TX timeout + PA disarm unchanged. ATT-on-TX stays on. Gate 3
kill-test **PASS 2026-09-26** (Palstar dropped; relaunch no auto-key; stream
did not autostart — operator Start). No antenna / no amp until you choose
that next step.

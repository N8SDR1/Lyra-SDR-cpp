# PureSignal HL2 dummy-load gates (P6)

**No antenna. Dummy load only.** Attestation stays **unchecked** until the
coupler is physically present. Do not treat a green compile as RF clearance.

Authoritative mux: `docs/architecture/puresignal_hl2.md` (Thetis `cntrl1=4`,
DDC0+DDC1). If coupler energy is missing after that mux, capture **Thetis**
C&C on the same HL2+ — do not silently switch DeskHPSDR `nrx=4` RX3/RX4.

## Gate 0 — attestation off (no RF required)

1. Fresh settings: `tx/psAttestation` unset/false.
2. Arm checkbox disabled; ON in the PureSignal dock disabled.
3. Frames 11/16 C2 bit 6 stay 0 (`puresignal_run`).
4. Frame 4 `P1_adc_cntrl` stays 0 (not 4).
5. Key TUN: no `pscc` feed, SUB still follows the operator SUB button.

## Gate 1 — arm on RX, mux still off

1. Check attestation (Settings → TX).
2. Arm PS (dock ON or Settings).
3. RX: `puresignal_run=1`; `P1_adc_cntrl` still 0.
4. Captured-profile apply still runs (bypass is MOX+PS only).

## Gate 2 — TUN + PS on dummy (coupler present)

1. Dummy load, PA opt-in as for first RF. **Do not expect FB at 2–3 W.**
2. ATT-on-TX on (RX ADC not pegged). Leave it on.
3. Arm PS, enable 2-tone or TUN, key MOX.
4. Expect: `P1_adc_cntrl=4`; DDC0+DDC1 at TX freq; `PsCalcThread` running
   (`in` > 0 on the dock); panadapter is the blinded TX view, not a
   cleanliness meter.
5. Raise drive until dock **FB** leaves 0 (N8SDR HL2+, ATT-on-TX −31,
   2026-09-26): ~2–3 W → D0 ≈ −49 dBFS, **FB 0**; ~8 W → D0 ≈ −18 dBFS,
   **FB ~138**, `cal` counting, **correcting**. Same neighborhood as
   Thetis on this unit (feedbk 139 @ ~5 W tune). Target band ~130–160.
6. Auto-att only via `set_tx_step_attn_db` (same writer as ATT-on-TX).
7. Unkey: mux returns to 0; SUB pause flag clears; captured bypass clears.

## Gate 3 — kill-test (still dummy)

`taskkill /F` / Task Manager End task the Lyra process **mid-TUN** with PS
armed (not window-close). Observable: Palstar (and/or banner PA) must drop
within a few seconds. Must not auto-key on relaunch. Stream need not
auto-Start — operator Start (or Stop then Start) is OK.

**PASS 2026-09-26 (N8SDR HL2+, dummy, TUN, PS armed):** kill mid-tune →
Palstar RF dropped; relaunch **no TX**; HL2 did not autostart (Start is
operator); Stop then Start recovered clean, still no RF until keyed.

## Fail → capture, do not guess

- No coupler after `cntrl1=4`: Wireshark/Thetis C&C on **this** unit.
- FB stuck 0 with mux 4: first check drive (2–3 W + ATT-on-TX −31 is
  often silent FB with `in` still high). Then D0 vs D1 on the dock.
  Empty D0 (`in` 0 or D0 ≈ −120) → EP6/`pscc` path. D0 energy + FB 0 at
  low watts → raise RF, not a mux swap. Confirm DDC0/DDC1 not DDC2/DDC3.
- Relay chatter / RX pops: not a PS mux bug; parked audio-path work.

Brick P2 (`ALEX_PS_BIT`, DDC lock) is **not** this gate list. P7 after Gate 2
is real on HL2.

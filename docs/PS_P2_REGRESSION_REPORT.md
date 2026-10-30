# PureSignal on Protocol 2 — Brick2 regression report

**Prepared for:** Cursor (and Rick) for a review / rebuttal session
**Date:** 2026-10-30
**Repo state reviewed:** `feature/wdsp-2.10` @ `9be343a` (v0.25.32), plus tags v0.25.0 → v0.25.31
**References used:** deskHPSDR `src/new_protocol.c`, `src/transmitter.c`, `src/radio.c`
(`Y:\Claude local\_deskhpsdr\`); Thetis 2.10.3.13 `ChannelMaster/network.c`
**No source code was changed for this report.**

---

## 1. Summary

Greg (VK3KV, Brick2, MAC `00:1C:C0:A2:22:5C`, firmware `codeVersion=106`) reports:

- PureSignal **worked earlier in Lyra** and then stopped.
- PureSignal **works in deskHPSDR and Thetis** on the same radio.

His Amp View screenshot (v0.25.31 era):

```
FB 0   ATT 0 dB   st 4   cal 0   idle   D0 -3 dB   D1 -18 dB
GetPk 0.000   SetPk 0.290   dg.cnt 0
```

**Primary finding:** commit `ac3ad57` (2026-10-09, co-authored by Cursor, first shipped in
**v0.25.16**) removed the Protocol-2 "TX IQ stays at unity gain" rule and made the TX IQ
scale with the Drive slider on every P2 radio. The radio's TX-DAC replica (the PureSignal
reference on DDC1) shrinks with that IQ, so below full drive it never reaches SetPk.
PureSignal then can never finish collecting, FB stays 0, and the auto-attenuator walks the
feedback pad down to 0 dB until the coupler ADC is near clipping. That matches Greg's
screen exactly.

The removed code carried an explicit warning that it protected PureSignal:

```cpp
// P2: analog drive [345] is the watts lever. Keep IQ gain at unity so
// the cap/PA-gain byte is not fighting a second scaler (and so a later
// PureSignal tap can own this stage without a dual-path fight).
if (p2Path)
    lyra::wire::SetTXFixedGain(0, 1.0, 1.0);
else
    lyra::wire::SetTXFixedGain(0, rv, rv);
```

It was replaced with `lyra::wire::SetTXFixedGain(0, rv, rv);` for all paths
(`src/hl2_stream.cpp`, now ~line 2887). The commit message itself says PS did **not** work
after the change ("Dummy-load bench still shows FB ~22 and a linear Amp View — coupler FB
did not rise"), yet it was released to every P2 radio, Brick2 included.

The later "fixes" (`f0941ad`, `96d0be4`, `94e5056`, the `SetPSDeadlockMinFrac` change)
treated the symptoms (FB 0, "D1 dead") and misdiagnosed the cause. The claim that Brick2's
D1 is "dead" is **contradicted by Greg's own screenshot** (D1 = −18 dB, alive but low).

---

## 2. How the failure chain works

1. Operator transmits at partial drive (Greg's band memory: Tune drive 38 %, TX drive 99 %
   on some bands; the PS 2-tone uses whatever drive is set).
2. `radioVolumeFor_()` gives `rv = drive% × PAgain/100 / 100` on P2.
3. `SetTXFixedGain(0, rv, rv)` scales the TX IQ by `rv` **and** the HP drive byte `[345]`
   also carries `255·rv`. That is drive applied twice on any radio that honours `[345]`.
4. The radio's TX-DAC replica (DDC1, `adc = n_adc` in the DDC config) follows the IQ
   amplitude. At full-scale IQ the replica peaks at ≈ 0.2899 (deskHPSDR's documented
   P2 value, `transmitter.c:1364`, `hpsdrsim.c:36`).
5. At `rv ≈ 0.4` the replica peaks ≈ 0.12 → **−18 dB**, versus SetPk 0.290 (−10.8 dB).
   Greg's D1 = −18 dB corresponds to drive ≈ 40 % (Tune 38 % gives −19 dB).
6. WDSP `calcc` sorts samples into amplitude buckets from 0 to SetPk and waits for all of
   them. The top ~55 % of the buckets never fill → it stays in COLLECT (`st 4`), `cal 0`,
   `dg.cnt 0`, `GetPk 0.000`.
7. `GetPSInfo[4]` (FB) stays 0 because nothing has been calculated.
8. `PsAutoAtt.h`: `fb < 25 → −15 dB` step, repeated every ~1 s until `ATT = 0`
   (`autoAttNeedsStep(fb<140 && att>min)`). This is why Greg shows **ATT 0 dB**.
9. With no pad, the coupler feedback on ADC0 sits at **D0 −3 dB** (near clipping). Even if
   collection started, a clipped feedback cannot produce a valid correction.
10. The per-band pad is persisted (`rig/.../ps/txAtt/<band>`), so the walked-down 0 dB is
    saved and comes back next session. Greg's Oct 28 snapshot had `20m=24`; his Oct 30
    snapshot has every band at 0.

deskHPSDR and Thetis keep the TX IQ at unity on P2 and set power only with the drive byte
(deskHPSDR `radio.c:2523` `do_scale = 0` for every radio except Penelope/HL2). Their
replica therefore reaches SetPk at any drive, which is why PS works there on the same unit.

---

## 3. Every change in the PS path between "worked" and "broken"

Compared tag-to-tag (v0.25.14 / v0.25.15 vs v0.25.31). Items are ranked by likelihood of
breaking Brick2 PS.

### 3.1 HIGH — P2 TX IQ now scales with Drive (`ac3ad57`, v0.25.16)
See sections 1–2. **Prime suspect.** Also changes Brick2 power behaviour: drive is now
applied twice (IQ × byte), so output vs. slider became roughly squared on radios that
honour `[345]`. Motivation in the commit was Brick3 sitting at ~0.9 W regardless of drive —
a Brick3-specific problem fixed for every P2 radio.

**Fix direction to argue:** restore unity IQ on P2 (at least while PS is armed, and at
least for Brick2/Hermes-class). If Brick3 truly ignores `[345]`, gate the IQ scaling to
that profile only, and still pin unity while PS is armed so the replica reaches SetPk.

### 3.2 MEDIUM — Auto-attenuator walks the pad to 0 when FB is 0
`src/ps/PsAutoAtt.h` + `PsFsm::tickAutoAtt`. FB = 0 is treated as "feedback too weak",
so the pad drops 15 dB per step to 0. But FB = 0 here means "no calculation yet", not
"weak coupler". The result is a clipped coupler (D0 −3 dB) and a persisted 0 dB pad.
The header comment ("A muted coupler (FB=0, pad 31) must still step ATT down") was written
for a different case and now makes the failure worse.

**Fix direction:** do not step the pad down on FB = 0 when D0 is already hot (e.g. D0 above
−10 dBFS); never persist a pad learned while FB was 0.

### 3.3 MEDIUM — HP byte 4 bit 7 ("PureSignal") removed (`ac3ad57`, v0.25.16)
Up to v0.25.15 Lyra set `HP[4] |= 0x80` while PS was armed. `ac3ad57` removed it to match
deskHPSDR. Neither deskHPSDR (`new_protocol.c:1456`) nor Thetis (`network.c:910-912`,
bit documented but never set) sets it, and both work on Greg's radio, so this is probably
not the cause. Listed because it changed in the same release as the regression.

### 3.4 MEDIUM — PS feedback attenuation moved between ADC bytes (`ac3ad57`, v0.25.16)
HP packet: before, PS att went through `overlayAdcAttByte` (ADC0 → `[1443]`). After,
`overlayHpAdcAttByte` writes PS att to ADC1 `[1442]` and, with PA keyed + ATT-on-TX on
(Greg has `attOnTxEnabled=true`), ADC0 `[1443]` gets the ATT-on-TX value (31). The
DUC-specific packet still puts PS att on ADC0 `[59]`. Brick2 has one ADC (ADC0 = the
coupler). This now matches deskHPSDR (`new_protocol.c:2093-2099`, `2222-2234`), so it is
probably fine on firmware that reads DUC-specific `[58/59]` during TX — but which byte the
Brick2 gateware honours during TX has not been verified.

### 3.5 MEDIUM — Host DUC "replica" fallback (`94e5056`, v0.25.32 — not shipped yet)
Built on the false "D1 dead on Brick2" premise. It also has two defects of its own:
- **Wrong rate:** `copyHostTxForCoupler` takes every 2nd host sample (2:1). Each
  interleaved pair is one instant of two DDCs both at 192 kHz, and the host DUC stream is
  also 192 kHz, so the mapping must be 1:1. As written the reference runs at 2× speed.
- **No time alignment:** each call copies the newest tail of the host buffer instead of
  consuming samples in order with a fixed delay for the radio's DUC FIFO and pipeline.
  TX and feedback are not sample-aligned, which PureSignal requires.
- **Diagnostic masking:** when the fallback feeds pscc, Amp View's D1 shows the host copy,
  so a genuinely dead radio replica would look alive.
- If it ever reached "correcting", it would apply predistortion fitted to mismatched data
  (splatter risk on air).

**Recommendation:** do not ship it in v0.25.32. The `txSq` gate would use the radio replica
anyway when present, but the fallback should be removed or disabled.

### 3.6 LOW — `SetPSDeadlockMinFrac(0.02)` band-aid (`PsFsm::pushArm`)
The comment cites Greg's D1 −18 dB vs SetPk 0.29. Lowering the deadlock fraction hides
the symptom (the top bucket not filling) instead of fixing the replica level.

### 3.7 LOW — `f0941ad` "unstick P2 collect when FB stays 0" and `96d0be4` three-tick auto-att
Both target FB = 0, the downstream symptom. With the replica below SetPk they cannot help;
`96d0be4`'s reset-on-each-step also restarts collection repeatedly while the pad walks.

### 3.8 LOW — DDC-config `+3` byte written as 24 (`94e5056`, v0.25.32 — not shipped yet)
`writeDdcSlot` now writes `pkt[off+3] = 24` for every DDC slot, in RX and TX. In the P2
DDC-specific layout `+3` is a CIC field, not sample size (`+5`). deskHPSDR writes `[26]`
(DDC1 `+3`) only during PS, which looks like a desk quirk. Most firmware ignores it, but
writing it on every DDC in RX is an unverified change to the receive config.

### 3.9 LATENT (not Greg's case) — `iqSampleScale()` during PS at 48 kHz
`P2Session::iqSampleScale()` returns 0.0355 (−29 dB) when the operator's DDC0 rate is
48 kHz on one-ADC radios. During PS the radio is forced to 192 kHz, but the scale still
follows the stored operator rate, so both feedback and replica arrive 29 dB low → PS never
collects. Greg runs 192 kHz, so it does not apply to him, but any Brick2 user at 48 kHz
would hit it. Fix: use scale 1.0 whenever PS is live (the PS samples are 192 kHz).

### 3.10 INFO — things checked and found matching deskHPSDR
- DDC config during PS: DDC0 ADC 0, DDC1 ADC = `n_adc` (1 on Brick2 = TX-DAC), 192 kHz,
  24-bit, enable DDC0 only, sync `[1363] = 0x02` (`new_protocol.c:2342-2361`).
- Interleave order: even = feedback (DDC0), odd = TX replica (DDC1) — same as deskHPSDR's
  `tx_add_ps_iq_samples(tx = sample1, rx = sample0)` (`new_protocol.c:3379-3396`).
- DDC0 and DDC1 frequency set to the TX frequency while keyed (HP `[9]`, `[13]`).
- ALEX PS bit 18 (`[1429]`, `[1433]`).
- PS config re-sent on key-down (`applyTxControlNow` → `sendDdcSpecificToRadio`).
- Brick2 profile: `adcCount = 1`, not Angelia-slots (unchanged since v0.25.14).
- SetPk default 0.2899 = deskHPSDR's P2 value. `SetPSFeedbackRate` 192000.
- Two-tone magnitude 0.49999 × 2 = deskHPSDR (`transmitter.c:2976`).
- `drive_iscal` is Penelope/HL2 only in deskHPSDR — Cursor was right that it does not
  apply to Brick.

---

## 4. Points to raise with Cursor directly

1. Why was the unity-IQ pin removed for every P2 radio when the motivation was Brick3,
   and why was its "PureSignal" warning comment deleted?
2. The commit message says PS still failed after the change. Why was it shipped in v0.25.16?
3. The "D1 dead on Brick2" conclusion conflicts with Greg's screenshot (D1 −18 dB). What
   data was it based on?
4. `94e5056` substitutes host IQ for the radio replica at a 2:1 ratio with no alignment.
   What evidence was there that this could produce a valid calibration?
5. The auto-attenuator steps the pad down on FB = 0 regardless of D0 level. Should FB = 0
   with D0 near clipping ever lower the pad?

---

## 5. Suggested verification (no guessing)

**On Greg's radio (no code change):**
1. Set the PS pad back to ~24 dB on 20 m.
2. Key the PS 2-tone at **100 % drive**. Watch D1. If D1 rises to about −11 dB and
   FB / cal start moving, section 3.1 is confirmed.
3. Optional A/B: install **v0.25.15** (last release before `ac3ad57`) and try PS at the same
   drive, then **v0.25.16**.

**On Rick's bench:**
- Brick2 on the dev build, PS 2-tone at 40 % and at 100 % drive; record D1 at each. If D1
  drops ~8 dB at 40 %, 3.1 is confirmed independently of Greg.
- Same test in deskHPSDR: D1 should stay put across drive levels.

**Proposed fix order (after verification):**
1. Restore unity TX IQ on P2 for Brick2 (and whenever PS is armed); keep drive on `[345]`.
2. Stop the auto-attenuator from stepping down on FB = 0 when D0 is hot; do not persist
   pads learned with FB = 0.
3. Drop the host-tap fallback (`94e5056`) before v0.25.32 is released.
4. Fix `iqSampleScale()` to return 1.0 while PS is live.
5. Revisit the `+3` DDC byte and the `SetPSDeadlockMinFrac` band-aid once PS is collecting.

---

## 6. Key commits

| Commit | Date | Release | What |
|---|---|---|---|
| `ac3ad57` | 2026-10-09 | v0.25.16 | **IQ scales with drive on P2**, HP bit 7 removed, HP ATT bytes remapped (Cursor co-author) |
| `f0941ad` | 2026-10-26 | v0.25.29 | "unstick P2 collect when FB stays 0" |
| `96d0be4` | 2026-10-27 | v0.25.31 | three-tick auto-att (pad, reset, resume) |
| `94e5056` | 2026-10-28 | v0.25.32 (unreleased) | host DUC tap fallback, `+3` DDC byte |

---

## 7. Resolution (2026-10-30)

Implemented on `feature/wdsp-2.10` after the review session, per the agreed
contract (which supersedes §5's "fix order" where they differ):

| Commit | What |
|---|---|
| `e95b135` | §3.1 — P2 TX IQ at unity on every P2 radio (no PS-state branch); watts on HP `[345]` only. P1/HL2 path unchanged. Pure helper `src/tx/TxDriveSplit.h` + `scratch/test_tx_drive_split.cpp`. One-shot per-rig reset of learned watts-cap ceilings (`p2/psUnityIqCapResetV1`). |
| `a278e9b` | §3.5 + §3.8 — `94e5056` reverted: host DUC tap removed, pscc fed from the radio even/odd pair only; the `+3` DDC byte write is gone. |
| `12cd393` | §3.9 — `iqSampleScale()` returns 1.0 while PureSignal is live. |

**Deliberately not changed:** §3.2 auto-attenuator (no D0-hot guard — math,
thresholds, three-tick sequence and per-band pad persistence stay as shipped);
§3.3 HP `[4]` bit 7 stays absent; §3.6 `SetPSDeadlockMinFrac` call line left
(the symbol is not exported by the bundled `wdsp.dll`, so the call is inert and
the effective value is the compiled-in 0.06); SetPk; DDC routing.

Operator-facing notes: `docs/releases/UNRELEASED-ps-p2-unity-iq.md` (includes the
drive-level warning for amplifier users).

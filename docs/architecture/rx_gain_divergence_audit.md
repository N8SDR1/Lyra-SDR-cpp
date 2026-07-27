# RX front-end gain / attenuator — divergence audit vs the reference

Status: **round 2 complete — two independent adversarial verification passes
done. READ THE ROUND-2 SECTION AT THE BOTTOM BEFORE ACTING ON ANY ROW.**
Round 2 overturned one row outright (37), softened two severities (30, 39),
found a third copy of a default the table only had two of (29), and found
six things the table missed — one of which is directly PureSignal-relevant.
It also refuted the headline conclusion that was drawn from row 5.

This is an enumeration, not a plan. Nothing here has been fixed. Rows are
facts with file:line on both sides; where a claim could not be established
by reading, it is in the UNVERIFIED list rather than in the table.

Why this exists: over the TX bring-up and Auto-LNA arc, the operator
repeatedly asked "is this the same as the reference?", and each time a
divergence surfaced — a ceiling, a recovery cadence, a branch, a claimed
latch — that had been introduced here and not carried over from anywhere.
Fixing those one at a time as they were noticed guaranteed the next one
would surface during PureSignal, where `tx_step_attn` is the *shared*
actuator and a divergence is a calibration fault rather than a deaf
receiver. So: enumerate the whole surface once, then decide.

## Axis identity (verified on both sides, not inferred)

The reference writes the wire value as `31 - att`; Lyra writes it as
`gain + 12`. Equating the two wire expressions:

```
gain + 12 = 31 - att        =>        att = 19 - gain
```

Both land at the same byte — `networkproto1.c:1102` / `FrameComposer.cpp:582`
as `(x & 0x3F) | 0x40`. The RTL (`hl2_rtl_ad9866.v:134`) with bit 6 set
passes `cmd_data[5:0]` straight to the AD9866 PGA: 0 = −12 dB … 60 = +48 dB
(`hl2_wiki_Protocol.md:137`).

Mixed-axis comparisons — a limit or default written in one axis and used in
the other — are the specific failure class hunted in area 3 below.

## Divergence table

Areas: 1 = overload detection · 2 = auto-attenuator control law ·
3 = axis / wire mapping · 4 = ATT-on-TX · 5 = invented or missing.

| # | Area | Reference does (file:line) | Lyra does (file:line) | Same/Different | Severity |
|---|---|---|---|---|---|
| 1 | 1 · overload bit decode | HL2 read loop: `prn->adc[0].adc_overload = ControlBytesIn[1] & 0x01` — `networkproto1.c:502` | `p->adc[0].adc_overload = cc[1] & 0x01` — `wire/Ep6RecvThread.cpp:962` | Same | — |
| 2 | 1 · accumulate vs overwrite | **Generic** loop OR-accumulates, tagged `[2.10.3.13]MW0LGE` — `networkproto1.c:338`, `:356-358`. **HL2** loop plain-assigns (fix NOT applied) — `networkproto1.c:502`, `:520-522` | Mirrors the HL2 plain assign at `Ep6RecvThread.cpp:962`, then adds an **invented** sticky latch `adc_overload_seen` — `wire/RadioNet.h:303`, set at `Ep6RecvThread.cpp:963-965`, consume-and-clear at `hl2_stream.cpp:760-762` | Different | MAJOR |
| 3 | 1 · reference self-divergence | OR-accumulate present in the generic loop `networkproto1.c:338`, absent from the HL2 loop `networkproto1.c:502` — same file, two behaviours for one field | Copies the HL2 (unfixed) line at `Ep6RecvThread.cpp:962` and compensates out-of-band at `hl2_stream.cpp:760-762` | Different | MAJOR |
| 4 | 1 · consumer read/clear | `getAndResetADC_Overload()` packs all 3 ADCs into a bitmask and zeroes all 3 — `netInterface.c:128-142`; called `console.cs:21461` | Reads/clears only its own atomic; `prn->adc[0].adc_overload` is written by the wire thread and **never cleared by anyone** — `Ep6RecvThread.cpp:962` vs `hl2_stream.cpp:760-762` | Different | MAJOR |
| 5 | 1 · poll cadence | `await Task.Delay(100)` per iteration, `checkOverloadsAndSync()` every iteration — `console.cs:21902-21921`. The comment claiming ~400 ms at `console.cs:21502` is **stale** | 400 ms timer — `hl2_stream.h:2093`, wired `hl2_stream.cpp:209-211` | Different (4×) | MAJOR |
| 6 | 1 · counter / hysteresis | `_adc_overload_level[i]++` cap 5, `--` floor 0, act/red at `> 3` — `console.cs:21496-21510` | Identical arithmetic — `hl2_stream.cpp:763-764`, `:792` | Same numbers, 4× longer in wall-clock (see #5) | MAJOR |
| 7 | 1 · ADCs tracked | 3 ADCs — `console.cs:21325-21327`, loop `console.cs:21491` | ADC0 only — `hl2_stream.cpp:760-761` | Different | DELIBERATE — RTL `hl2_rtl_control.v:472-475` emits one clip bit |
| 8 | 1 · response slot carrying the bit | Slot `0x00` C1 bit 0 — `networkproto1.c:502`; also decodes slot `0x20` — `networkproto1.c:520-522` | Slot `0x00` C1 bit 0 — `Ep6RecvThread.cpp:962`; also decodes `0x20` — `Ep6RecvThread.cpp:1010-1012` | Same | RTL: `hl2_rtl_control.v:472-475` emits only addr 0-3 → the `0x20` branch is dead on HL2 in **both** |
| 9 | 1 · arm-time clear | Clears once before the poll loop starts — `console.cs:21893` | Resets `overloadLevel_` at open (`hl2_stream.cpp:962`); leaves any pre-existing `adc_overload_seen` latch set | Different | MINOR |
| 10 | 1 · magnitude-at-overload telemetry | `getAndResetADCmaxMagnitudeAtOverload()` / `getADCmaxMagnitude()` — `netInterface.c:145-165` | No counterpart; field absent from `wire/RadioNet.h:289-303` | Missing in Lyra | MINOR |
| 11 | 1 · bad-ADC error path | `_check_for_bad_adc` + MessageBox + permanent disable — `console.cs:21464-21473` | No counterpart — `hl2_stream.cpp:739-762` | Missing in Lyra | MINOR |
| 12 | 2 · trigger condition | `_adc_overloaded[0] && _adc_overload_level[0] > 3` **and** `nRX1ADCinUse == 0` — `console.cs:21684` | `ov && overloadLevel_ > 3`, no ADC-in-use test — `hl2_stream.cpp:792` | Different (test absent) | MINOR — HL2 has one ADC |
| 13 | 2 · back-off step + limit | `if (RX1AttenuatorData < (31-3)) RX1AttenuatorData += 3;` — `console.cs:21696-21697` | `if (g > kAutoLnaBackoffMinDb /* -9 */) applyLnaGainNoPersist(g-3)` — `hl2_stream.cpp:812-813`, const `hl2_stream.h:2112` | Same (att<28 ⇔ g>−9) | — |
| 14 | 2 · creep step + limit | `if (RX1AttenuatorData > -28) RX1AttenuatorData--;` — `console.cs:21751-21752` | `if (g < kAutoLnaCreepMaxDb /* 47 */) applyLnaGainNoPersist(g+1)` — `hl2_stream.cpp:832-833`, const `hl2_stream.h:2113` | Same (att>−28 ⇔ g<47) | — |
| 15 | 2 · hold-clock reset placement | Outside the step guard — `console.cs:21699`, `:21754` | Outside the step guard — `hl2_stream.cpp:820`, `:835` | Same | — |
| 16 | 2 · **band-change handling** | `_band_change` set on band select (`console.cs:43776`) and on auto-enable (`console.cs:21360`), cleared on first overload (`console.cs:21694`), **OR-ed into the creep condition** (`console.cs:21748-21749`) → after a band change it creeps 1 dB **per poll (100 ms)**, ignoring both the undo flag and the hold timer | No counterpart in `hl2_stream.cpp:739-836`; band change only re-applies the stored *manual* LNA — `bandmemory.cpp:164-166` | Different (absent) | MAJOR |
| 17 | 2 · **what happens on TX** | Entire RX auto-att block is inside `if (!_mox)` — `console.cs:21641` | `onAutoLnaTick()` has no MOX gate; only a `running_` check — `hl2_stream.cpp:740-742` → gain is roamed from TX-coupling clips and lands on the wire at unkey | Different | MAJOR — step attenuator is the shared PS actuator |
| 18 | 2 · auto-ATT on TX | `_auto_attTX_when_not_in_ps`: on MOX + sustained ADC0 overload force `ATTOnTX=true`, raise `TxAttenData` by `_adc_step_shift[0]`, push history, unwind on unkey; **suppressed while `psform.AutoCalEnabled`** — `console.cs:21582-21637` | No counterpart | Missing in Lyra | MAJOR |
| 19 | 2 · step-shift accumulator | `_adc_step_shift[i]` 0..31, +1 on overload / −1 otherwise — `console.cs:21327`, `:21568-21580` | No counterpart | Missing | MINOR for HL2-RX (ref's HL2 RX branch uses a fixed 3, `console.cs:21697`); load-bearing only for #18 |
| 20 | 2 · auto-enable side effects | Sets `_band_change = true` only — `console.cs:21354-21363` | Zeroes `overloadLevel_`, restarts the hold clock — `hl2_stream.cpp:696-698` | Different | MINOR |
| 21 | 2 · auto-disable side effects | Clears the (HL2-always-empty) history stack; the attenuator stays where auto left it — `console.cs:21643-21650` | Restores the persisted manual set point — `hl2_stream.cpp:700-707` | Different — invented here | MAJOR |
| 22 | 2 · per-band persistence of auto steps | Setter persists **every** write incl. auto steps — `console.cs:21697` → setter → `console.cs:11113` | Auto path deliberately bypasses persistence (`applyLnaGainNoPersist`, `hl2_stream.cpp:674-687`); only manual sets persist — `hl2_stream.cpp:668-670`, `bandmemory.cpp:31-40` | Different | MAJOR |
| 23 | 2 · defaults (auto / undo / hold) | `false` / `false` / `5 s` — `console.cs:21333`, `:21335`, `:21337` | `true` / `true` / `4 s` — `hl2_stream.cpp:304-307` | Different | DELIBERATE — comment at `hl2_stream.cpp:300-303` cites the operator's station export |
| 24 | 2 · operator indication | `A-ATT`/`S-ATT` label + infoBar warning **suppressed** when HL2 && AutoAttRX1 — `console.cs:21344-21349`, `:21520-21524` | Red lamp shown unconditionally — `qml/AudioPanel.qml:136-146` | Different | MINOR |
| 25 | 3 · RX axis → wire | `SetADC1StepAttenData(31 - _rx1_attenuator_data)` — `console.cs:11075` → `netInterface.c:873-875` → `networkproto1.c:1102` | `set_rx_step_attn_db(db + 12, 0)` — `hl2_stream.cpp:666`, `:684`, `:1159-1161` → `FrameComposer.cpp:228-230` → `:582` | Same bytes over the overlapping range | — |
| 26 | 3 · RX operator range | Two contradicting reference limits: `Maximum=31 / Minimum=-28` at model init (`console.cs:2111-2113`) vs `Maximum=32 / Minimum=-28` inside the setter (`console.cs:11043-11044`) | −12…+48 gain = att +31…−29 — `hl2_stream.h:740-741`, `qml/AudioPanel.qml:91` | Different — Lyra reaches att −29 (wire 60), one step beyond every reference limit | MINOR |
| 27 | 3 · att = 32 edge (reference side) | Setter validates to 32 (`console.cs:11043`) → `SetADC1StepAttenData(31-32 = -1)` → masked `& 0x3F` at `networkproto1.c:1102` → wire **63** | Cannot be expressed; gain axis clamped `hl2_stream.h:740-741` | Different | **RTL wins, contradicts the reference**: `hl2_rtl_ad9866.v:134` passes 63 through as a PGA code — the reference's *most-attenuating* UI position commands *maximum* gain. CRITICAL (reference-side) |
| 28 | 3 · auto-limit derivation | `att < 28` / `att > -28` — `console.cs:21696`, `:21751` | Documented and applied on the gain axis — `hl2_stream.h:2100-2113`, used `hl2_stream.cpp:812`, `:832` | Same — no mixed-axis comparison here | — |
| 29 | 3 · **RX default set point** | `_rx1_attenuator_data = 0` — `console.cs:11022` → att 0 → wire 31 → **+19 dB** | Literal `31` consumed on the **gain** axis — `hl2_stream.cpp:297`, again `hl2_stream.cpp:704` → wire 43 → **+31 dB** | Different — `31` is the reference's wire/att-domain constant, used here as gain dB | MAJOR |
| 30 | 3+4 · **ATT-on-TX "off" state** | `ATTOnTX = false` → `SetTxAttenData(0)` **un-inverted** — `console.cs:19173` (also `:10670`) → `tx_step_attn = 0` → `networkproto1.c:1100` → C4 `0x40` → PGA code 0 = **−12 dB (max attenuation)** | `attOnTxEnabled_ == false` → `setTxStepAttnDb(0)` — `hl2_stream.cpp:3006` → `:2388` → `FrameComposer.cpp:204-221` applies `31-0 = 31` → C4 `0x5F` → PGA code 31 = **+19 dB** | Different — **opposite wire byte for the same operator state** | CRITICAL |
| 31 | 3+4 · ATT-on-TX "on" encoding | `SetTxAttenData(31 - txatt)` — `console.cs:19163-19165`, `:10657-10658` | `set_tx_step_attn_db()` applies `31 - db` — `FrameComposer.cpp:204-221` | Same | — |
| 32 | 3 · TX operator range | HL2: `udTXStepAttData.Minimum = -28` — `console.cs:2115`; Maximum 31 — `console.Designer.cs:3966-3970` ⇒ **−28…31** | `0…31` — `hl2_stream.cpp:585-587`, `:2381`, `settingsdialog.cpp:6507`; tooltip asserts "Range 0..31 matches the reference's spin exactly" — `settingsdialog.cpp:6514-6516` | Different — negative TX att (TX-time LNA *gain*, wire up to 59) unreachable here, and the tooltip claim is false for HL2 | MAJOR |
| 33 | 3 · cross-control clamp (reference side) | `validateTXStepAttData` tests against **RX1's** minimum then assigns **TX's** minimum — `console.cs:10982-10983` | Single-axis clamp, no cross-control compare — `hl2_stream.cpp:585-587` | Different | MINOR (benign on HL2: both minima are −28) |
| 34 | 3 · S-meter / display offset axis | `RXPreampOffset` returns `+_rx1_attenuator_data` (att-sense) — `console.cs:21109` | `raw + calDb_ - lnaGainDb()` (gain-sense) — `metermodel.cpp:1131-1135` | Same up to a constant 19 dB, absorbed by `calDb_` / `RXCalibrationOffset` | MINOR |
| 35 | 3 · adc[1]/adc[2] RX step-att | Init 0, never written on HL2 (only `SetADC1StepAttenData` used — `console.cs:11075`); init `netInterface.c:1656` | Init 0 — `wire/RadioNet.cpp:107`; never written | Same | — |
| 36 | 4 · register carrying key-down forcing | Case 11 / addr `0x0a`, C4 MOX-gated: `XmitBit ? tx_step_attn : rx_step_attn`, `|0x40` — `networkproto1.c:1099-1102` | Byte-identical — `FrameComposer.cpp:577-583` | Same | RTL: with `en_tx_gain=0` (`0x0e[15]` never set), `hl2_rtl_ad9866.v:144` still applies the 0x0a value during `tx_en` — the live TX actuator on both |
| 37 | 4 · when the forcing is written | On property/band change only, not per key — `console.cs:19163-19171`; band reload `console.cs:17396` | On every key-down — `hl2_stream.cpp:3006`; restored on key-up `:3247` and on cancelled key-down `:3044` | Different mechanism | MAJOR |
| 38 | 4 · per-band ATT-on-TX value | Per-band `getTXstepAttenuatorForBand(_tx_band)` — `console.cs:19163`, reloaded on band change `console.cs:17396` | Single global QSettings value — `hl2_stream.cpp:585-587`, `hl2_stream.h:2252` | Different | MAJOR |
| 39 | 4 · QSK/CW coupling | QSK on ⇒ save + force `ATTOnTX=true`, `SetupForm.ATTOnTX=31`; QSK off ⇒ restore — `console.cs:13069-13090` | No CW/QSK coupling anywhere in `hl2_stream.cpp:3600-3643` | Missing | MAJOR — but **RTL partly moots it**: `hl2_rtl_ad9866.v:150-152` forces PGA gain to 0 whenever `cw_on & ~en_tx_gain`, which neither host models |
| 40 | 4 · create-time default | `prn->adc[i].tx_step_attn = 31` — `netInterface.c:1657` | `prn->adc[i].tx_step_attn = 31` — `wire/RadioNet.cpp:108` | Same | — |
| 41 | 5 · invented | — (no such field: `netInterface.c:1656-1658` enumerates the whole `adc[]` init) | `std::atomic<int> adc_overload_seen` — `wire/RadioNet.h:303` | Invented | MAJOR — the mechanism behind #2/#4 |
| 42 | 5 · invented | — (no equivalent diagnostic, `console.cs:21455-21515`) | `LYRA_LNA_DEBUG` per-2 s poll/level/gain log — `hl2_stream.cpp:777-789` | Invented | MINOR |
| 43 | 5 · invented | — (reference clears at poll start, `console.cs:21893`) | Lamp force-cleared on stream close — `hl2_stream.cpp:1440` | Invented | MINOR |
| 44 | 5 · dead code | — | `tx/AttOnTxPolicy.{h,cpp}` compiled (`CMakeLists.txt:247-248`) but **never instantiated** — only comment references (`hl2_stream.cpp:1151`, `:3602`); live path is `hl2_stream.cpp:3006`/`:3247` | Dead | DELIBERATE (header says wire-inert, `tx/AttOnTxPolicy.h:44`) |
| 45 | 5 · missing | Preamp-mode fallback ladder when step-att is disabled — `console.cs:21718-21740`; gate `_rx1_step_att_enabled` — `console.cs:10988-10999` | No enable gate, no preamp ladder — `hl2_stream.cpp:797-836` | Missing | DELIBERATE — HL2 has no separate preamp |
| 46 | 5 · missing | RX2 auto-att block incl. RX1↔RX2 shared-ADC linking — `console.cs:21786+`, `:11125-11131` | No RX2 | Missing | DELIBERATE — RX1-only today |
| 47 | 5 · missing | Amp-overload power back-off in the same poll — `console.cs:21443-21452` | No counterpart — `hl2_stream.cpp:739-836` | Missing | MINOR (adjacent surface) |
| 48 | 5 · missing | Historic-reading stack with per-band pruning — `console.cs:21545-21560`, `:21661-21663` | No counterpart | Missing | DELIBERATE — the reference's HL2 branch (`console.cs:21692-21700`) never pushes to it |

## Where the RTL contradicts an implementation (RTL wins)

**Row 27.** `console.cs:11043` permits att = 32, which reaches the wire as
`0x3F`. `hl2_rtl_ad9866.v:134` (bit 6 set) hands `cmd_data[5:0]` straight to
the PGA, so the reference's maximum-attenuation UI position commands the
*highest* gain code. Lyra cannot reach it.

**Clip-bit meaning.** `hl2_rtl_control.v:465` clears `clip_cnt` on **every**
response, `:478-479` saturates it, `:472` reports `(&clip_cnt)`.
`resp_addr` advances each response (`:466`) so slot 0 is 1-in-4.
`hl2_wiki_Protocol.md:217` confirms the bit position.

*Corrected in round 2 — the unit was wrong and the last sentence was false.*
`clip_cnt` increments once per **`clk_ctrl` cycle** (2.5 MHz,
`hermeslite_core.v:927`) in which the synchronised `rxclip` level is high —
not once per clip. `rxclip` (`hl2_rtl_ad9866.v:232-241`) is a latch set by a
full-scale sample and cleared by `rxclrstatus`, which `control.v:355`
toggles every control clock. So the flag means **≥3 distinct ~400 ns windows
containing at least one full-scale sample, since the last response frame** —
i.e. a single loud peak lasting >1.2 µs sets it. The flag is *easy* to set.

The original last sentence ("Lyra … still treats it as one") was **false and
contradicted row 2**: Lyra latches (`Ep6RecvThread.cpp:963-965`) and does a
consume-and-clear `exchange` (`hl2_stream.cpp:760-762`). The reference's HL2
loop plain-assigns, so it is the *reference* that samples instantaneously.
That false sentence was the load-bearing premise of the "4× cadence explains
the missed back-off" conclusion — see Round 2 finding C.

**Row 39.** `hl2_rtl_ad9866.v:150-152` forces PGA gain to 0 during `cw_on`
when `en_tx_gain` is clear. Neither host sets `0x0e[15]` (C3 bit 7 is always
0: `networkproto1.c:1019` / `FrameComposer.cpp:381` mask `0x1F`), so this
hardware behaviour is live and unmodelled on both sides.

## UNVERIFIED — needs bench or capture

1. Response-frame rate, hence how many slot-0 frames land inside a 400 ms
   window vs the reference's 100 ms. `resp_rqst` generation was not traced
   out of `hl2_rtl_control.v`. **This gates the #5/#6 conclusion.**
2. Actual dB-per-code of the AD9866 PGA. Taken from
   `hl2_wiki_Protocol.md:137`, not measured.
3. Whether `cmd_resp_rqst` slot-stealing (`hl2_rtl_control.v:468-470`)
   measurably starves slot 0 under heavy command traffic.
4. Whether the change-gate + `CmdHighPriority()` in `SetADC1StepAttenData`
   (`netInterface.c:871-878`) has any P1 effect or is P2-only. Lyra's
   `set_rx_step_attn_db` (`FrameComposer.cpp:228-230`) writes
   unconditionally with no command trigger.
5. Real-world consequence of row 30 (ATT-on-TX "off" = +19 dB here vs
   −12 dB on the reference). The sign is proven by reading; the audible and
   PS impact is not.
6. Whether `autoLnaTimer_` is started/stopped anywhere that would
   incidentally gate it around MOX. Only the `open()`/`close()` sites
   (`hl2_stream.cpp:966`, `:1439`) and the tick's `running_` guard
   (`hl2_stream.cpp:740`) were read.

---

# Round 2 — independent adversarial verification

Two senior agents, each told to try to break the table rather than confirm
it, each required to open both cited sites per row. Rows 1-24 + the clip-bit
paragraph to one; rows 25-48 + the axis identity to the other.

## Rows overturned or re-graded

**Row 37 — WRONG. Withdraw it.** The reference **does** write the ATT-on-TX
forcing per key. `chkMOX_CheckedChanged2` keydown `console.cs:30293-30327`:
with `m_bATTonTX` true it computes `getTXstepAttenuatorForBand(_tx_band)`
(`:30308`), applies a CW/PS force (`:30309-30312`), then assigns
`SetupForm.ATTOnTX` (`:30314`) → `SetTxAttenData(31 - x)`; with it false it
calls `SetTxAttenData(0)` directly (`:30325`). Keyup `:30388-30414` is
symmetric. That is the *same shape* as our `hl2_stream.cpp:3006`/`:3247`.
The row cited only the property setter and missed the MOX handler entirely.
Not "Different mechanism / MAJOR" — broadly the same mechanism.

**Row 30 — CONFIRMED end-to-end and stronger (three reference sites, not
one: `:19173`, `:10670`, and both MOX edges `:30325`/`:30411`), but
SEVERITY ARGUED DOWN to MAJOR.** `SetTxAttenData` stores raw
(`netInterface.c:1028-1040`) → C4 `0x40` → PGA code 0 = −12 dB; ours applies
`31-0=31` → C4 `0x5F` → code 31 = +19 dB. A 31 dB swing on the PS actuator,
real. **But our defaults mask it completely**: `kDefaultAttOnTxEnabled=true`
+ `kAttOnTxDb=31` → `31-31=0` → C4 `0x40` — **byte-identical to the
reference**. The divergence is only reachable when the operator unticks the
box whose only purpose is protection, and the RTL moots it entirely in CW
(`hl2_rtl_ad9866.v:150-152` forces PGA 0 during `cw_on`). Still fix before
PureSignal; not a live fault at defaults.

**Row 39 — severity DOWN to MINOR.** `cw_on` is the gateware keyer's *active*
state (`hl2_rtl_radio.v:1053`/`:1073`), not a config bit, so the RTL forces
PGA code 0 on every CW key-down on **both** hosts regardless of any host
value. The reference's QSK force-to-31 is redundant on this hardware.

**Row 29 — fact CONFIRMED, attribution WITHDRAWN, and a third site found.**
Reference default → +19 dB, ours → +31 dB: real, ships 12 dB hotter. But the
claim that our `31` *is* the reference's wire-domain constant is a provenance
hypothesis, not readable fact — `hl2_stream.h:739` documents an independent
origin ("old Lyra software-capped at +31 on an 'above this is IMD' call").
Report the divergence, drop the causal story. **Third copy of the literal:
`hl2_stream.h:1778` `std::atomic<int> lnaGainDb_{31}`**, alongside
`hl2_stream.cpp:297` and `:704` — change one and the others silently
disagree.

**Row 27 — over-claim softened.** att=32 → `(-1 & 0x3F)|0x40` = `0x7F` → PGA
code **63**. The wiki defines the range as 0…60 (`hl2_wiki_Protocol.md:137`);
codes 61-63 are undocumented. "Commands maximum gain" is directionally right;
any specific dB figure is not establishable from these trees.

**Row 46 — INCOMPLETE.** The reference's RX2 auto-att block is gated on
`radioHasRx2Att` (`console.cs:21781-21784`), which does **not** list
HERMESLITE. Dead on HL2 *in the reference too* — grade it like row 8, not
"Lyra has no RX2".

**Rows 6, 12, 22, 35, 44 — INCOMPLETE.** See the additions below.

**Citation drift:** rows 7, 11, 12, 18, 19, 24, 32, 38, 47, 48 sit 1-5 lines
off. Individually harmless; collectively it means the table was assembled
from a scroll position rather than re-read.

## New rows (missed entirely by round 1)

| # | Area | Reference does (file:line) | Lyra does (file:line) | Severity |
|---|---|---|---|---|
| 49 | 4 · **per-key CW / PS-off force-to-31** | `if ((!chkFWCATUBypass.Checked && _forceATTwhenPSAoff) \|\| CurrentDSPMode == CWL \|\| CWU) txAtt = 31;` on **every** keydown — `console.cs:30309-30312` | Static `attOnTxDb_`, no CW branch, no PS-A gating — `hl2_stream.cpp:3006` | **MAJOR — directly PureSignal-relevant.** This is the "Force ATT on Tx to 31 when PS-A off" behaviour, which the operator's own station export has ticked |
| 50 | 4 · ATT-on-TX off-switch interlock | `if (!value && _auto_attTX_when_not_in_ps) return;` — the operator **cannot** disable ATT-on-TX while the PS auto-attenuator has it engaged — `console.cs:19154` | No interlock; a mid-PS toggle is accepted and applied live — `hl2_stream.cpp:3604-3613`, `:3610` | MAJOR (PS) |
| ~~51~~ | ~~1 · reference lamp/act split~~ | **WITHDRAWN — round 3. THERE IS NO SPLIT.** `:21502` sits *inside* `if (_adc_overloaded[i])` (opened `:21496`, closed `:21503`), so it reduces to `_adc_overloaded[i] && level > 3` — algebraically identical to `:21685`. Round 1 and round 2 both read the line without its braces | Our lamp (`hl2_stream.cpp:792`) implements the reference's red rung **exactly** | NOT A DIVERGENCE |
| 51a | 1 · **missing yellow tier** | Three-rung ladder: silent / **yellow** `sWarning` on `level > 0` (`console.cs:21511-21513`) / **red** on `overloaded && level > 3`. Yellow persists ~5 polls while the counter unwinds | Only the red rung exists — no intermediate "overload seen recently" state | MINOR |
| 51b | 1 · **warning suppressed on our exact config** | `if (Model != HERMESLITE \|\| AutoAttRX1 == false) infoBar.Warning(...)` — on **HL2 with auto-att on, NO overload indication is painted at all**; the preamp label reads `"A-ATT"` instead (`console.cs:21342-21348`, `:21522-21524`) | Red lamp shown unconditionally — `qml/AudioPanel.qml:136-146` | MAJOR — a faithful port **removes** our lamp in the default configuration |
| 51c | 1 · confirmation-window intent | `ReleaseNotes.txt:119`: *"auto rx step attenuation will only happen when there has been at least 400ms of adc overload and it moves from a yellow to red warning"* — 4 cycles × 100 ms = **400 ms cumulative**. The `:21502` comment garbles this into "3 cycles… around 400ms" per-cycle | 4 cycles × 400 ms = **1.6 s** — 4× the documented design window | MAJOR — this is the real cadence divergence, and it now has a **documented target** |
| 52 | 4 · TX-att ADC fan-out | `SetTxAttenData` writes `tx_step_attn` to **all** `MAX_ADC` — `netInterface.c:1034-1037` | `adc[0]` only — `FrameComposer.cpp:221`; `adc[1]/[2]` stay at the create-time 31 forever | MINOR (inert on HL2 — only `adc[0]` is composed) |
| 53 | 4 · RX per-band snapshot on key | Snapshots RX1's per-band step-att on the MOX edge — `console.cs:30313` | No RX-side per-band save/restore across a key cycle | MINOR |
| 54 | 5 · dead class ≠ live path | — | `AttOnTxPolicy`'s keyup hook is a deliberate **no-op** (`tx/AttOnTxPolicy.cpp:81-88`) while the live path **restores** (`hl2_stream.cpp:3247`) — resurrecting the dead class would change behaviour | MINOR (see row 44) |

Additions to existing rows: **row 4** — `hl2_stream.cpp:963-964` carries a
**false comment** asserting Auto-LNA reads `adc_overload` directly; it reads
`adc_overload_seen`. **Row 12** — the reference's trigger is a two-branch OR
with outer gates `_auto_att_rx1 && radioHasRx1Att` (`:21680`) and
`_rx1_step_att_enabled` (`:21690`). **Row 22** — persistence is `!_mox`-gated
(`console.cs:11112`), not unconditional. **Row 35** — `RadioNet.cpp:107` is a
comment; the real init is the in-class default `RadioNet.h:287`.

## UNVERIFIED items — resolved or re-stated

**#1 RESOLVED, and it does NOT gate rows 5/6.** `resp_rqst` is a toggle
(`usopenhpsdr1.v:409`) edge-converted by `sync_pulse` (`hermeslite_core.v:
939-943`) into a one-cycle pulse in the control domain — so **`resp_addr`
does not free-run and the slot model holds.** Rate: ~2526 responses/s at
48 kHz nddc=4 ⇒ **~632 slot-0 frames/s, ~1.58 ms apart** ⇒ ~253 per 400 ms
window, ~63 per 100 ms. Neither host is starved. *Residual:* `sync_pulse.v`
is not present in either tree; one-pulse-per-edge is inferred from usage,
not read.

**#2 STILL UNVERIFIED and load-bearing.** The wiki gives only two endpoints
(code 0 = −12 dB, 60 = +48 dB). **1 dB/code is inferred, not proven**, and
every dB figure in rows 27/29/30/32 and the axis identity depends on it.

**#3 RESOLVED and worse than stated.** `resp_cnt` and `resp_addr` are
phase-locked (`control.v:464`, `:466`, `:467`), so a command response can
only ever displace slots **0 and 2** — the clip bit's slot. Sustained
C0-bit-7 traffic starves it **100%**, not partially. Not currently fired:
our composers never set C0 bit 7.

**#4 SUBSTANTIALLY RESOLVED — it is Protocol-2 machinery.**
`CmdHighPriority()` (`network.c:900`, "port 1027"), `CmdGeneral()` (`:808`,
port 1024), `CmdTx()` (`:1168`, port 1026) are the P2 command sockets, and
the call is gated on `prn->sendHighPriority`. On a P1/HL2 session all C&C
rides the EP2 round-robin, so the setter's only P1-relevant effect is the
field store — our unconditional write is correct for P1.

**#5 partially resolved:** the sign is proven three times over, our
default-ON makes the wire byte identical to the reference, and CW is fully
mooted by the RTL. Remaining scope is SSB/AM/FM/digital with protection
explicitly disabled. Audible/measured impact still needs the bench.

**#6 RESOLVED, and the note's own citations were wrong.** `autoLnaTimer_`
has exactly four sites: `setInterval` `hl2_stream.cpp:209`, `connect`
`:210`, `start()` **`:1259`**, `stop()` **`:1438`** (the note cited `:966`,
a comment, and `:1439`, which is `hwPttTimer_`). **There is no MOX-related
gating anywhere**, which makes row 17 strictly worse, not better.

## Finding C — the headline conclusion drawn from row 5 is REFUTED

The cadence facts hold (reference 100 ms at `console.cs:21921`,
`checkOverloadsAndSync()` every iteration; the 1-in-5 divisor applies to
`checkSeqErrors()` only), but the conclusion drawn from them does not.

Both hosts read the same bit from the same ~632 slot-0 frames/s. The
difference is what happens *between* polls:

- **Reference (HL2 loop):** plain assignment ⇒ a **point sample of 1 frame
  out of ~63** per window. Sees a clip with probability ≈ *p*.
- **Ours:** the latch catches **any** of ~253 frames. Probability
  ≈ 1 − (1−*p*)²⁵³.

Same `>3` hysteresis ⇒ reference needs ≈ *p*⁴; ours needs
≈ (1 − (1−*p*)²⁵³)⁴. For any *p* above ~1% our per-poll term is essentially
1. **Our detector is dramatically MORE sensitive than the reference's — only
4× slower.** The latch (row 41) more than offsets the cadence.

The residual regime where the reference acts and we don't is narrow: a
clipping episode of roughly **0.4-1.6 s**, dense enough that four consecutive
100 ms point samples all land on set frames. That is a real latency
divergence worth closing — it is *not* "a signal that would never trip our
detector."

**Ranked source-provable candidates for gain roaming to the ceiling**, per
the verifier: (1) **row 17** — no MOX gate, so the loop runs during transmit,
TX coupling clips ADC0, and the result lands on the wire at unkey; (2) **row
21** and `bandmemory.cpp:164-166` — both snap gain to a stored value the auto
loop must then re-walk, neither of which the reference does; (3) row 16 is a
*reference-favourable* divergence (it creeps **faster** after band change),
so it cannot explain a stuck ceiling.

---

# Round 3 — the three "reference-side bugs", investigated (2026-07-22)

Operator asked whether items 1 and 2 existed in earlier releases, and put a
third agent on item 3. All three answers changed the picture.

**Bug 3 — DOES NOT EXIST.** See rows 51 / 51a / 51b / 51c above. Round 1
and round 2 both misread brace scope. The replacement findings (missing
yellow tier, warning suppressed on HL2+auto-att, the documented 400 ms
window) are worth more than the "bug" was.

**Bug 1 — DELIBERATE, and MI0BOT-specific.** MI0BOT release notes,
v2.10.3.8 (11 May 2023): *"Increased receiver Attenuate to full range, 32dB
max attenuation."* `ramdor-Thetis` has **no HERMESLITE branch** in the
step-att setter (`Maximum` is 61 or 31 only), confirming the widening came
with the HL2 fork. Open sliver: "32 dB" most likely means *32 steps (0-31)*,
which would make `Maximum = 32` an off-by-one producing the out-of-range
wire code 63. Needs `console.cs` from a current tag to settle; unreachable
on our axis either way.

**Bug 2 — REAL, STILL LIVE UPSTREAM, and it is a merge gap.** Fetched
`networkproto1.c` from MI0BOT **master** (past 2.10.3.14 and
2.10.3.15-beta1): the generic loop OR-accumulates, `MetisReadThreadMainLoop
_HL2` still plain-assigns. `ramdor-Thetis` has **no HL2 loops at all**
(no `MetisReadThreadMainLoop_HL2`, no `WriteMainLoop_HL2`), so on ramdor HL2
runs the generic loop and **gets the OR-accumulate**.

History: MW0LGE fixed the accumulate in official Thetis 2.10.3.13
(`ReleaseNotes.txt:130` — *"issues handling the highprio network packet
(1025) meant that ADC overloads could be missed. This is now resolved."*);
MI0BOT's separately-authored HL2 loops never received it.

**Consequence for "do as the reference does":** the phrase has two different
answers here. *Official Thetis* and the HL2 branch's own sibling loop
OR-accumulate — which is exactly what our `adc_overload_seen` latch does, so
the latch is **not an invention**, it is the same fix arrived at
independently. Only the MI0BOT HL2 loop point-samples.

### DECISION A — operator-locked 2026-07-22: follow OFFICIAL Thetis

**OR-accumulate. Keep the latch. One code path, no per-rig branch.**

Rationale, in the operator's order:
1. ANAN family support is the direction of travel (Jerry's work) and is the
   larger install base; the ANAN path runs the generic loop = accumulate.
2. Accumulate is the **upstream-maintained** semantics — MW0LGE wrote the
   fix and tags it `[2.10.3.13]MW0LGE`.
3. It is **strictly more informative**: it cannot miss a clip the
   point-sample catches, only the reverse.
4. A per-rig branch would encode a **merge accident** as if it were a
   hardware distinction, and every future reader would go looking for the
   hardware reason. If a genuine per-rig difference ever appears it belongs
   in the capability struct (§6.7), not scattered branches.

**Honest qualification (an earlier overstatement, corrected):** PureSignal
does NOT require accumulate. The operator runs MI0BOT on HL2, so PS-A's TX
branch (`console.cs:21593`) has been reading the *point-sampled* flag on
that bench and works. The case for official is convergence and maintenance,
not a PS defect.

**Recorded deviation:** this is a deliberate, documented divergence from the
HL2 tree the operator actually runs. A/B against installed Thetis on HL2
will show our detector catching clips theirs misses. That is expected and
correct — not a bug to chase.

---

# Implementation status (2026-07-22)

Operator directive: do exactly as the reference does, end to end, no
invented behaviour. Decisions A/B/C taken, then the RX rows worked in
order. Every commit built clean.

| row | what | commit |
|---|---|---|
| A | overload accumulate — follow OFFICIAL Thetis, keep the latch, one code path | `5b0e3c0` (doc) |
| 5, 6 | poll at 100 ms → the documented 400 ms confirmation window | `e296c59` |
| 51a/b/c, C | three-rung ladder (silent / amber / red) + A-ATT state cue replacing the alarm while Auto owns the front end | `b5f990a` |
| — | LNA + Vol sliders trimmed so row 1 fits the wider cue | `31023b2` |
| 17 | no gain writes while transmitting (`!_mox` gate on the ACTION, poll+counter+tier stay live) | `aa5112f` |
| 21, 22 | Auto owns the band's gain: steps persist through the same path a manual set uses; disable leaves the gain where Auto left it | `d6603f9` |
| 29 | default gain +19 dB (reference att 0), was 31 — the reference's WIRE value used as gain | `7120d85` |
| 16, 20 | `_band_change` fast acquire after band change / auto-enable; enable no longer clears the counter or hold clock | `ae24326` |
| 4 | false comment naming the wrong field | this commit |

**Operator-visible behaviour changes to expect at the bench:**
1. Auto-LNA **overwrites the stored per-band LNA value** as it works.
   Turn Auto off → gain stays where Auto put it. Change band → you get
   what Auto last settled on there, not a remembered manual number.
2. With Auto ON there is **no overload lamp** — an `A-ATT` cue in its
   place. With Auto OFF you get the full ladder, including the new
   amber "clipped recently, settling" rung.
3. Landing on a new band (or switching Auto on) now **climbs fast** to
   the overload edge instead of crawling 1 dB per hold interval.
4. Confirmation is 400 ms, not 1.6 s — Auto reacts ~4× sooner.

**Still open on this surface** (TX / ATT-on-TX cluster — the
PureSignal-relevant one): rows 30, 32, 37, 38, 49, 50, plus the minor
12 / 52 / 53 and the dead `AttOnTxPolicy` (44 / 54). Row 27 (att=32 →
undocumented PGA code 63) is unreachable on our axis and needs a
current-tag `console.cs` to settle. UNVERIFIED #2 (1 dB/code) still
gates every dB figure here.

---

# Bench results (2026-07-22, N8SDR HL2+, 40 m)

First captures with a working log path. Everything below is measured,
not inferred.

**The loop works.** The clip flag arrives (`ovPolls` 0-9 of 20). On a
real signal back-off ran **48 -> 27 dB in 656 ms** (seven 3 dB steps,
one per poll while the counter held above 3). The band-change fast
acquire climbed ~12 dB in ~1.2 s after a retune, then found the edge
and backed off. Band restores and manual sets are now labelled.

**A 96 s stretch parked at 47 dB had `ovPolls 0` throughout** — nothing
was clipping. Sitting at the ceiling with no overload is correct, and
is a different situation from the original "pinned at +48" report: the
moment a genuine overload appeared, the loop shed 20 dB in under a
second.

**The "creep runs at half rate" anomaly was an ERROR OF MINE, not a
defect.** I measured ~2.06 s/dB in the first capture and compared it
against a hold value of 1 s read from the registry *after* the run.
The second capture's own log shows `hold 2 s` for its first two windows
and `hold 1 s` thereafter — the operator had changed it in between. The
step counters settle it: **hold 1 -> `up 2` per window; hold 2 -> `up 1`**,
i.e. ~1.09 s/dB and ~2.19 s/dB. The arithmetic was correct all along.
Recorded because the instinct to go hunting for a phantom bug was
strong, and only measuring stopped it.

**The one real finding is a limit cycle at short hold values.** The
whole second capture is this shape repeating every ~5-7 s:

```
28 -> 30 -> 32 -> 34    creep at ~2 dB/s (hold 1 s)
34 -> 31 -> 28          back-off, 2 steps
```

It never settles. This is faithful — the reference sawtooths the same
way at a 1 s hold — but it is not a useful setting on a live band. The
creep reaches the overload edge in ~3 s, gets knocked down ~6 dB, and
starts over. Reference default hold is **5 s** (`console.cs:21337`);
the operator's own Thetis uses **4 s**. At 4 s the approach is four
times slower, so the loop should sit just under the clip point rather
than pumping.

## Third capture — hold 4 s, with transmit and band changes

**Hold 4 s settles the limit cycle.** `up` alternates 1 / 0 per window
= 1 dB per ~4.4 s. The excursion went from 28→34→28 every 5-7 s (hold
1) to 27→32 every ~15-20 s, with long flat stretches. Tracking rather
than hunting.

**Row 17 (MOX gate) CONFIRMED on hardware.** Key-down 08:29:32.739 →
key-up 08:29:41.730:

```
08:29:33.486  up 1  gain 31  elapsed 1966
08:29:35.673  up 0  gain 31  elapsed 4153   past the 4000 ms hold
08:29:37.861  up 0  gain 31  elapsed 6341   still no creep
08:29:40.048  up 0  gain 31  elapsed 8529   still no creep
```

Gain frozen for the whole transmission AND `elapsed` climbing past the
hold threshold three times without a creep — the gate is returning
before the creep branch, not merely coinciding with an idle loop.
Second key (08:31:25-30) identical at 47 dB, elapsed 5905.

**Band restore visible and correct.** `set 47 dB (manual / band
restore)` → six back-offs in 550 ms (`dn 6`) → settled 29. The stored
per-band value was too hot for that band; corrected in half a second.

**The original "pinned at +48" is explained.** On 20 m the loop crept
32→47 and parked 90 s with `ovPolls 0` throughout — nothing clipping.
Climbing to the travel limit on a quiet band is exactly what the
reference does (creep to att −28 when clear). It only read as a fault
because the loop had no voice in the log.

**Known, faithful, worth remembering:** the overload counter runs
during transmit (reference does the same — `checkOverloadsAndSync` is
ungated, only the RX *action* sits inside `if (!_mox)`). So TX-coupling
clips accumulate while keyed and can act on the first poll after
unkey — observed once here, 32→29 immediately post-keyup. ATT-on-TX was
engaged, so this is likely the T/R transition rather than steady
coupling. The creep walks it back.

---

# TX / ATT-on-TX cluster (2026-07-22)

**Done:** row 30 (`6299d4a`) and row 32's false tooltip (same commit).
Row 37 needs no work — the round-2 verification established the
reference DOES write per key, same shape we do; the original row was
wrong.

**Rows 38, 49, 50 are PureSignal-coupled and deliberately DEFERRED to
the PS work.** Not skipped — blocked, for a reason worth writing down.

The reference's keydown force (`console.cs:30308-30314`) is:

```csharp
int txAtt = getTXstepAttenuatorForBand(_tx_band);
if ((!chkFWCATUBypass.Checked && _forceATTwhenPSAoff) ||
    (mode == CWL || mode == CWU)) txAtt = 31;
SetupForm.ATTOnRX1 = getRX1stepAttenuatorForBand(rx1_band);
SetupForm.ATTOnTX  = txAtt;
```

Both gates on the first disjunct depend on state Lyra does not have:
a PureSignal auto-cal flag and an ATU-bypass setting. Row 50's
interlock (`console.cs:19154` — the operator cannot switch ATT-on-TX
off while the PS auto-attenuator holds it engaged) is inert without PS
by definition. Row 38's per-band TX attenuator only earns its keep once
the value can legitimately differ per band, which is what row 49
introduces.

Building them now means inventing two PS-shaped settings and guessing
their semantics. That is the failure mode this whole audit exists to
stop. They land with PureSignal, against real PS state, with the
mechanism above already captured so the work does not start cold.

The one piece that is NOT PS-dependent — CW forcing 31 — is currently
moot: our ATT-on-TX applies 31 on every keydown regardless of mode, so
CW already gets the reference's forced value. It stops being moot the
moment row 49 introduces per-band values below 31.

**Also still open, unrelated to PS:** row 32's actual range (we expose
0..31, reference allows −28..31 on this hardware — the negative half is
TX-time gain); rows 52 / 53 (minor); the dead `AttOnTxPolicy` class
(44 / 54) still compiled and never constructed.

## Standing rules for anyone working this list

- Cite both sides. A claim with a citation on only one side is a hypothesis.
- Do not infer semantics from a function's name — open it. The claim that
  `getAndResetADC_Overload()` was a latch was made twice without reading it;
  the HL2 read loop plain-assigns (`networkproto1.c:502`) and the fix at
  `:338` was never backported.
- Where the RTL disagrees with either host, the RTL wins.
- The step attenuator is the actuator PureSignal will drive. Divergences on
  that register are calibration faults, not preferences.

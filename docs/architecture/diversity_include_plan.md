# Native Diversity — include plan for Lyra

**Status:** RESEARCH + PLAN ONLY. No feature code until operator go-ahead.  
**Date:** 2026-10-13 (amended after red-team consensus + Thetis confirm + polar/WDSP lock)  
**Sources:** Thetis 2.10.3.13 (`D:\sdrprojects\OpenHPSDR-Thetis-2.10.3.13\`) and DeskHPSDR DL1BZ (`D:\sdrprojects\deskhpsdr\src\`). First-pass reads plus four senior red-teams ([Thetis red-team](6cdfac59-85e7-403a-9ea5-95771dad9ca0), [Thetis confirm](6ea4e2ef-aa40-40b0-a986-9f645d45b6fc)). WDSP files are `wdsp/div.c` / `div.h` (there is no `divext.c`).  
**Operator locks (2026-10-10 / this session):** diversity is **not** SUB, **not** MAP, **not** the Settings ADC combo. Second RF is the **RX2 jack (Alex Ext1)**, **not ANT2**. Diversity is **inactive while PureSignal is live / transmitting**. Single-ADC boxes cannot do it. Audit items E/F (non-blocking `SetChannelState`, infinite `WaitForMultipleObjects`) stay **parked**. **UI sequence (2026-10-13):** working mix first (dB/phase); **polar with Lyra flare later** — do not start polar until DIV is on the air. **WDSP (2026-10-13 check):** latest published engine is **2.10** (2026-09-04); Lyra already ships it — **no bump before DIV**.

This document is the include plan. Implement later in staged commits after operator go-ahead.

**Red-team lock (2026-10-13):** four senior agents converged on the wire, DSP, PS, TX, and DDC-mapping facts below. The first draft’s “two UDP ports / pair DDC0+DDC1 streams” assumption is **REFUTED**. Amendments from the Thetis verifier (CAT names, ATT-link RX-only, P1 PS mask wording, HL2 C4-bit-7 wording, intentional TX-drop divergence) are folded here. No production `src/` change in this pass.

---

## 0. WDSP version gate (checked 2026-10-13 — before DIV code)

**Do not confuse Thetis 2.10.3.13 with WDSP 2.10.** Thetis’s *application* version is the protocol/UI reference. WDSP is NR0V’s library (`GetWDSPVersion()` returns version × 100).

| Engine | Date | Notes |
|---|---|---|
| 1.20–1.29 | 2022–2026-01 | Last 1.x = 1.29 (2026-01-21). `divEXT` already existed here. |
| **2.00** | 2026-07-01 | Major: PS 3.0 NURBS, free-curve EQ/CFC, PHROT optimizer, broadcast FM, RX decimator. |
| **2.10** | 2026-09-04 | NNR Standard/Premium, PS + filter-gen tuning, auto-generated `wdsp.h`. **Latest published notes.** |

**Check (2026-10-13):**

- TAPR `OpenHPSDR-wdsp` README release notes **stop at 2.10**. Latest master commits: `b02d5bac` “Release Version 2.10” (2026-09-09) then `6c8aad86` README-only (2026-09-09). **No 2.11 / 2.20 tag or notes.**
- GitHub Releases page is empty — version truth is the README, not a Releases artifact.
- DL1BZ mirror (`git.bzsax.de/dl1bz/OpenHPSDR-wdsp`) newest branch is `wdsp 2.10` (2026-09-09). DeskHPSDR discussion #207 (2026-09-05): 2.10 is current; *next* version after 2.10 will drop the 1.x backport — that next version is **not published**.
- Apache Labs ANAN Core also cites WDSP 2.10 as current (Oct 2026).
- **Lyra already ships WDSP 2.10** (`wdsp.dll`, `GetWDSPVersion` → 210; USER_GUIDE / wiki Roadmap v0.25.8). `wdsp_native.h` already has optional NNR binds. `divEXT` is unchanged in 2.10.

**Decision:** adopt-newer-if-true is the right rule. It is **not true today**. Do **not** bump WDSP as a DIV prerequisite. Re-check TAPR README + `GetWDSPVersion` **the day D1 starts**; if a 2.11+ note exists then, bump first (PS 3.x / NNR / `wdsp.h` regen — own commit, dummy-load, not mixed into D2+D3).

---

## 1. What it is

Diversity is a **host-side coherent mix of two converters at the same carrier**.

```
ADC0 ── DDC0 ── I0/Q0 ─┐
                       ├─ rotate/scale path 1 by (gain, phase) ─► sum ─► RX1 WDSP
ADC1 ── DDC1 ── I1/Q1 ─┘     (same Hz, same sample rate, hardware-synced)
         ▲
         └── on P2, both paths arrive interleaved on the SINGLE DDC0 UDP stream
```

The operator hears **one** receiver (RX1). Phase and gain steer the second path so the pair adds or nulls. That is the whole feature.

| Nearby surface | Why it is not diversity |
|---|---|
| **SUB / RX2** | Second VFO. May sit on a second ADC on Brick3 / 100D, but it is a **different frequency** and a second demod. |
| **MAP / ANT / RX chips** | Front-end jack (TRX / Ext1 / XVTR). Ext1 silk on Brick3 **is** the RX2 jack — the **port**, not the combiner. |
| **Settings ADC combo** | Today Lyra can pick which ADC RX1 listens on; it still programs **every DDC to that same ADC**. Diversity needs **two ADCs at once**. |

---

## 2. Hardware matrix (Lyra capabilities)

`RadioCapabilities::diversityCapable` already exists and is **unread**.

| Family / model | `adcCount` | Flag today | Diversity? |
|---|---|---|---|
| HL2 / HL2+ | 1 | `false` | **No.** Do not set `P1_en_diversity` for a feature. Thetis still **emits** P1 C4 bit 7 when its (removed) diversity flag is on (`networkproto1.c:638–640`, `:967–969`) but **strips the diversity menu** on HL2 (`console.cs:2120–2125`). DeskHPSDR gates on `n_adc < 2`. Framing: one ADC, no second converter — not “HL2 has no C4 bit.” |
| BrickSDR2 / ANAN-10 / 10E / Hermes | 1 | `false` | **No.** |
| Brick3SDR / ANAN-100D / 200D | 2 | `true` (AnanP2) | **Yes — first target.** Same v1 overlay (DDC0/DDC1 + `[1363]=0x02` + enable `0x01`). The Brick3 extra-DDC checkbox (`0x0F`) is **Angelia/Brick3-only**, default off, hidden on 100D. |
| ANAN-7000 / 8000 / G2 / Saturn | 2 | `true` when that profile is AnanP2 | **Yes**, after those models are on-air. 7000/8000 stay locked until OrionMkII BPF is in scope. |
| ANAN P1 | — | stub | **Out of v1.** Lyra P1 TX is HL2-only. `P1_en_diversity` (frame-0 C4 bit 7) stays **0**. |

**Capability rule:** UI, setters, and wire composers all read `diversityCapable && adcCount > 1`. Never a model-name string check in `src/` (existing hardware-abstraction discipline).

---

## 3. What the two references actually do

### 3.1 DSP mix

| | Thetis | DeskHPSDR |
|---|---|---|
| Engine | WDSP `divEXT` (`wdsp/div.c`) via ChannelMaster `sync.c` `InboundBlock` case 0 | **Host C mix only** — `rx_add_div_iq_samples`. `div.c` is in the WDSP tree and **never called** from `src/` |
| Math | `out += Irot[i]·I − Qrot[i]·Q` / `Irot[i]·Q + Qrot[i]·I` | `receiver.c` `rx_add_div_iq_samples` (`:1637–1645`): `I = i0 + (div_cos·i1 − div_sin·q1)`, **`Q = q0 + (div_sin·i1 + div_cos·q1)`**. Same rotation, RX1 ref fixed. Confirm-pass T3 **CLOSED** from source. |
| Off | `run=0` → memcpy stream 0 | Diversity action not selected; RX1 is ADC0 only |
| Output modes | 0 = RX1, 1 = RX2, 2 = mix. Combiner **output count equals `nr`** (`div.c:67–95`) | Combined on RX1; RX2 is ADC1 **monitor** |

**Lyra choice:** bind and call **WDSP `divEXT`** (Thetis path). Lyra is already WDSP-native (`wdsp_native` already binds `SetEXTNOB*`; `divEXT` is the sibling). DeskHPSDR’s host mix is the **math we must match**, not the preferred insertion layer. A tiny host-side rotate-and-sum is a **bench fallback** only if `xdivEXT` is awkward at the P2 IQ boundary.

**Combiner init order (Thetis landmine — D1):** `create_div` leaves `Irot`/`Qrot` **zeros** (`wdsp/div.c`). If `SetEXTDIVRun(1)` fires first, the mix is silence. Before run: `SetEXTDIVOutput(0, 2)` and `SetEXTDIVRotate` with **ref = 1+j0**, path1 = `amp·e^{jθ}` (defaults 0 dB / 0° → 1+j0). Do **not** import Thetis per-band phase tables 1:1 (ref-swap adds π; their linear `udR` is a different UI axis). Clamp gain to **−27 … +27 dB** (`diversity_menu.c` `set_diversity_gain`).

**IQ boundary (LOCKED — first-draft “two UDP ports” is REFUTED):** Protocol 2 hardware, when DDC1 is hardware-synced (`[1363]=0x02`), delivers **interleaved IQ for DDC0 and DDC1 on the single DDC0 UDP stream (port 1035)**. Frame is 1444 bytes, 238 × 6-byte slots: `I0, Q0, I1, Q1` (119 pairs). DeskHPSDR decodes this in `process_div_iq_data` (`new_protocol.c` ~3250–3306). Lyra already implements the **identical** interleave for PureSignal via `P2Session::ingestPsccInterleaved` (`P2Session.cpp` ~797–813). Diversity ingest is that same splitter, routed to the combiner instead of PSCC. **Do not** invent a second UDP consumer or pair two independent DDC ports on the host.

Rotate coefficients (DeskHPSDR, operator-friendly dB; identical to `wdsp/div.c:108–180`):

```
amp   = 10^(gain_dB / 20)
θ     = phase_deg · π/180
Irot0 = 1,  Qrot0 = 0          // RX1 / ADC0 reference
Irot1 = amp·cos θ, Qrot1 = amp·sin θ
Iout  = I0 + (Irot1·I1 − Qrot1·Q1)
Qout  = Q0 + (Irot1·Q1 + Qrot1·I1)
      = Q0 + (div_sin·I1 + div_cos·Q1)   // DeskHPSDR names
```

Thetis UI stores **linear** `udR1`/`udR2` 0…5 (GainMulti up to 10). Lyra should **not** copy that. Use DeskHPSDR **dB + degrees**. Convert at the setter. Defaults: **0 dB, 0°**.

Suggested first-cut ranges (DeskHPSDR):

| | Coarse | Fine |
|---|---|---|
| Gain | −25 … +25 dB, 0.5 | −2 … +2 dB, 0.05 |
| Phase | −180 … +180°, 2 | −5 … +5°, 0.1 |

Clamp gain to about **−27 … +27 dB**. Phase wrap ±180°.

### 3.2 Protocol 2 wire (DeskHPSDR is the byte-level source of truth)

Lyra already composes `receive_specific` in `P2Session::buildDdcSpecificPacket()`. Diversity is a **new overlay** on that packet + High Priority DDC words. Today `[1363]=0x02` is used **only for live PS**.

When `diversity_armed && !xmit && !ps_live` (DeskHPSDR `new_protocol.c` ~1498–1526, ~2335–2368):

| Field | Value |
|---|---|
| DDC0 ADC | 0 |
| DDC1 ADC | 1 (`n_adc > 1`) |
| Both rates | RX1 sample rate |
| Both HP frequency words | **RX1** (same phase word) |
| `[1363]` sync | **`0x02`** — DDC1 locked to DDC0; **this is what puts I1/Q1 onto the DDC0 UDP stream** |
| Enable `[7]` (non-Brick3 extra mode) | **`0x01`** — DDC0 only; DDC1 is enabled **by sync**, not by bit 1. **Never write `0x03` / `0x0003`.** |
| Enable `[7]` Brick3 extra-DDC mode | **`0x0F`** — DDC0..3; DDC2/DDC3 also get RX1 freq + ADC0/ADC1 (STM32 band-decoder workaround). Opt-in, default **off**, Angelia/Brick3 only. |
| Dither / random | Copy **RX1** onto **both** ADC0 and ADC1 |
| Alex BPF (7000-class) | ADC1 BPF follows **RX1** freq |
| TX | `ALEX1_ANAN7000_RX_GNDonTX` — ground RX2 path on TX (Lyra already has RX2-jack isolate on TX as of v0.25.18) |

**Trap:** do not set enable `0x03` (DDC0+DDC1 bits) as a “safer” default. Stock path is `0x01` + sync `0x02`. Brick3 optional mode is `0x0F` behind an operator checkbox, default **off**.

**Angelia / Brick3 slot remapping (load-bearing for Lyra):**

Today (`P2Session.cpp` ~571–586, `angeliaRxDdcSlots`):

- RX1 = **DDC2 / ADC0**
- RX2 = **DDC3 / ADC1**
- DDC0/DDC1 HP words are written so the STM32 sees the carriers

DeskHPSDR **diversity replaces that**: the live pair becomes **DDC0 + DDC1** on the **DDC0 UDP interleave**. The host must consume that interleaved stream (not DDC2/DDC3 UDP) while DIV is on and not transmitting. That is why diversity and live PS fight: **PS also uses DDC0/DDC1 + `[1363]=0x02`**.

**Atomic overlay + consumer (LOCKED):** switching the packet without switching `P2RxBridge` (today `rx1DdcIndex() == 2` on Angelia) **mutes RX1**. Switching the consumer without the packet leaves the bridge listening to a port that is no longer the live pair. Wire overlay and host IQ consumer **must flip in the same commit**. First-draft phases D2 then D3 as separate wire-then-bridge steps are **rejected**.

**De-interleave is DDC0 / port 1035 only** (confirm T7): consume IQ from that **one** interleaved stream. Do not also pull DDC1 / 1036 as a second diversity path.

**DeskHPSDR copies onto both streams while DIV RX** (`process_div_iq_data` `:3293–3304` + receive_specific / HP): **RX1 dither and random** on ADC0 **and** ADC1; **RX1 IQ-gain** (`p2_iq_sample_gain(receiver[0])`) on **both** I0/Q0 and I1/Q1; **RX2 sample rate forced = RX1**. Lyra must do the same or the pair is not a coherent mix.

**Do not inherit Thetis DiversityForm RX2 auto-enable.** Opening / enabling DIV must not turn SUB on. v1 Option A is the opposite: DIV on → SUB off.

### 3.3 Protocol 1 (record only — not v1)

DeskHPSDR / Thetis: C4 `|= 0x80` when diversity RX is active (“locks VFOs”). DeskHPSDR also forces two receivers on the wire, ADC0→RX1 / ADC1→RX2, and mixes in the EP6 parser. Lyra already emits `P1_en_diversity << 7` in `FrameComposer.cpp` and the global stays **0**. Leave it.

HL2: Thetis **does emit** C4 bit 7 on the HL2 write path when diversity is conceptually on, but **removes the diversity menu** because there is no second ADC (`console.cs:2120–2125`). Lyra keeps `P1_en_diversity = 0` and shows no DIV control.

### 3.4 PureSignal / TX interlock

**Priority (LOCKED, first-wins):**

1. **PS live** — existing PS overlay wins. Drop DIV overlay. Combiner off. **Restore DDC2 as the RX1 consumer** (Angelia) and give **port 1035 / DDC0 UDP to PS** (`ingestPsccInterleaved`). Leaving the DIV consumer on 1035 while PS is live **mutes RX1 and starves PS**.
2. **TX (MOX / TUN / keyed)** — drop DIV overlay. Combiner off. RX2-jack isolate stays as shipped (v0.25.18).
3. **DIV armed + RX** — apply DIV overlay + interleaved ingest + combiner.
4. **Else** — today’s normal RX map (Angelia DDC2/DDC3, Hermes DDC0).

| | Thetis | DeskHPSDR | **Lyra (locked)** |
|---|---|---|---|
| DIV flag while PS-A on | Stays on (`_diversity2` only written by its setter, `console.cs:10776–10800`); `UpdateDDCs` remaps the sync pair to the coupler | No `set_diversity(0)`; **action table drops DIV whenever `xmit\|ptt`** | **Operator “armed” stays true; overlay dropped** whenever PS is live or we are transmitting |
| P1 MOX+PS wire | 5-DDC: `(rx_adc_ctrl1 & 0xf3) \| 0x08` (`console.cs:8297–8307`). HL2: config 6, `cntrl1=4` (`:8469–8488`) | `RXACTION_PS` on DDC0; same sync byte 1363 | Existing PS overlay in `buildDdcSpecificPacket()` **wins** |
| P2 MOX+DIV+PS (`tot=7`) | `GetDDC` assigns **no** sync pair — diversity dropped (`console.cs:8582–8836`) | Same as xmit drop | Same: PS packet, not DIV |
| **Plain TX (MOX, no PS)** | Thetis **keeps** the diversity layout (P1 `:8308–8317`; P2 `tot=6` keeps the sync pair) | Drops DIV on any `xmit\|ptt` | **Drop DIV overlay on all TX.** This is an **intentional, safer divergence** from Thetis: no weighting while RX is muted, no ATT-on-TX × DIV interaction, no stale phase into the PS state. Recorded so a later audit does not “fix” it back to Thetis. |
| Audio | On 2-DDC models, RX2 audio is dropped from the mix when DIV on (`console.cs:28223`, `:28255–28310`) | Combined audio is RX1 only | RX1 only |

Charter (a): Thetis never feeds PS feedback through the diversity combiner. Copying Thetis’s *effective* PS path (drop the DIV pair under MOX+PS) is required. Leaving the DIV overlay up under PS would either mix PA-coupler IQ with the antenna or steal DDC0/1 from PS — both corrupt calibration.

Charter (b): “PS/TX kills the DIV overlay” matches Thetis on **MOX+PS** and is **stricter than Thetis on plain TX**. Keep the stricter rule; label it a deliberate departure.

On PS-off / unkey, restore the prior DIV layout if the operator left DIV armed.

### 3.5 Antenna

Neither tree has a dedicated “diversity antenna” picker. Second path = **second ADC**. On Brick3 / 100D that is the **RX2 jack (Ext1)**. ANT2 is a **TX** antenna select — do not route diversity through it. MAP / Ext1 remains the operator’s way to confirm the jack; DIV does not steal ANT2.

**DeskHPSDR ant-menu tension (UNVERIFIED on the bench):** MAC `02:B2` / `02:B3` sets `HERMES_MODE_BRICK`, which **hides Ext1/Ext2/Xvtr** and forces Ant1 (`ant_menu.c`). That fights a Brick3 operator who must use the RX2 jack. Lyra already labels Ext1 as the RX2 jack — keep that; do **not** copy the Brick-mode hide.

**Related, not diversity:** DeskHPSDR `p2_angelia_ddc0_map` mirrors RX freqs into DDC0/DDC1 in **non-diversity** RX so the STM32 can track band. It is **skipped** when diversity is on. Do not confuse that helper with the DIV remap.

### 3.6 ATT, VFO lock, leftover Thetis knobs, CAT

| Surface | Thetis | DeskHPSDR | Lyra v1 |
|---|---|---|---|
| ADC1 attenuator | `DiversityAttLink` (`console.cs:11122–11150`, `:19468–19478`). Link runs on **receive only** — gated on `!_mox` and `EXTDIVOutput==2`. **Do not claim ATT-on-TX hits both ADCs** (UNVERIFIED / contradicted by the `!_mox` gate). | ADC1 att **forced = ADC0** while DIV RX (P1 + P2) | **Link ADC1 ← ADC0 while DIV armed and RX.** Single writer (`set_tx_step_attn` / RX att path). No extra checkbox in v1. No ATT-link during TX (overlay is already dropped). |
| VFO lock | UI `VFO Sync` on DiversityForm is **optional** and only sets `console.VFOSync`. P1 C4 bit7 is a **separate** gateware “locks VFOs” flag | P1 C4 bit7 when DIV RX; P2 just writes both HP words = RX1 | P2: both freq words = RX1 (already §3.2). Do **not** add a VFO Sync checkbox. P1 bit stays 0. |
| Listen-to-one-ADC | `SetEXTDIVOutput` 0 / 1 / 2 (ADC0 / ADC1 / mix) | Always mix into RX1; RX2 = ADC1 monitor | v1: `output=2` only (`nr=2`). 0/1 later if a tester wants “listen to one converter.” |
| Polar / spacing / λ | DiversityForm geometry → `UpdateDiversity` | None | **Out of v1**. |
| CAT / TCI | Diversity CAT is **ZZDB** (RX ref), **ZZDC** (RX2 gain), **ZZDD** (phase), **ZZDE** (enable), **ZZDF** (form), **ZZDG** (RX gain), **ZZDH** (RX source) (`CATCommands.cs`). ZZDA / ZZDP / ZZDR are **display** commands, not diversity. **No TCI** diversity. | MIDI/actions only | **Out of v1.** Do not invent TCI DIV. When CAT is added later, use ZZDB–ZZDH, not ZZDA/ZZDP/ZZDR. |
| Form close | Disposing DiversityForm **clears** `Diversity2` | Menu does not kill state | DIV is radio state. Closing a panel must **not** disable the combiner. |
| MOX vs DIV flag | **Does not** set `Diversity2=false` on MOX; PS remaps DDCs under a still-true flag | Action table / HP / RS all `&& !xmit` | **Our lock:** drop the **overlay** on TX/PS; keep operator “armed” so RX restores (§3.4). |

---

## 4. Lyra gaps (as of v0.25.18)

| Layer | Today | Needed |
|---|---|---|
| Capability | `diversityCapable` true on AnanP2, **never read** | Gate every DIV setter / UI / composer |
| WDSP | `SetEXTNOB*` only; **no `divEXT`** | `create_divEXT` / `destroy` / `flush` / `SetEXTDIVRun` / `Nr` / `Output` / `Rotate` / `xdivEXT` |
| ChannelMaster | `sync.h` **unported** (`CMaster.h`) | Do **not** port Thetis `sync.c` wholesale. Split interleaved DDC0 UDP (reuse `ingestPsccInterleaved`) and call `xdivEXT` (or host mix) |
| P2 composer | DDC2/DDC3 = RX1/RX2 on Angelia; `[1363]` **PS-only** | Diversity overlay: DDC0/1 ADC+rate, sync, enable `0x01` (or `0x0F`), HP both = RX1 |
| P2 ingest | `ingestPsccInterleaved` exists for **PS only** | Same interleave → combiner when DIV overlay is live |
| Settings ADC | One ADC for all programmed DDCs | DIV overrides: ADC0 + ADC1; combo disabled or ignored while DIV on |
| SUB | Independent VFO; dual pane on 2-ADC | Policy — see §6 |
| UI | None | Dock or Tuning-adjacent DIV panel; **hidden until D4** (not a disabled “not built” button in D0) |
| Persistence | None | Per-band gain/phase + master enable (Thetis has per-band; DeskHPSDR is global) |

---

## 5. Recommended architecture (Lyra-native)

```
                    ┌─ diversityCapable && adcCount>1? ─┐
                    │  no → feature gone                 │
                    └──────────────┬─────────────────────┘
                                   │ yes
                    ┌──────────────┴──────────────┐
                    │  1. PS live  → PS overlay   │
                    │  2. TX       → drop DIV     │
                    │  3. DIV armed+RX → overlay  │
                    │  4. else     → normal map   │
                    └──────────────┬──────────────┘
           P2 HP: DDC0 freq = DDC1 freq = RX1
           receive_specific: ADC0/ADC1, same rate,
           [1363]=0x02, enable 0x01 (or 0x0F Brick3 extra)
                               │
           DDC0 UDP 1444 B ── ingest (I0,Q0,I1,Q1) ── xdivEXT(id=0) ── RX1 WDSP
           (same frame shape as ingestPsccInterleaved)
           SUB pane: off (v1 Option A)
```

**Combiner instance:** one `divEXT` id `0`, `nr=2`, `output=2` (mix). After `create_divEXT`: **Output + Rotate first**, then `SetEXTDIVRun(0, 1)` only in the armed-and-RX window. On TX/PS: `SetEXTDIVRun(0, 0)` and stop interleaved DIV ingest (PS ingest may still run; RX1 must leave the DDC0 interleave — §3.4).

**Do not** port ChannelMaster router `{MOX, DIV, PS}` tables as C#. Encode the same state product in `P2Session` the way we already do for PS (`isPureSignalLive()`). Thetis `tot = MOX + (Div<<1) + (PS<<2)` (`console.cs` `UpdateDDCs` 8214–8576) is the **state-product idea**, not a table to copy.

---

## 6. SUB vs DIV (v1 lock: Option A)

Two-ADC boxes can already run **independent-band SUB** (DDC2 ADC0 + DDC3 ADC1). Diversity wants **both ADCs at RX1’s frequency**. They cannot both be true at once on two converters.

| Option | Behavior | Recommendation |
|---|---|---|
| **A — exclusive (v1)** | Turning DIV on turns SUB off (toast). Turning SUB on turns DIV off. | **Locked for v1.** Matches “DIV is not SUB.” Smallest state space. **This is a Lyra choice, not Thetis P2 parity.** Thetis keeps an independent RX2 on **DDC3 / port 1038** while the sync pair is on DDC0/DDC1. We deliberately do not. |
| **B — DeskHPSDR monitor** | DIV on: RX1 = mix; SUB pane = ADC1 only, **same** freq (no second VFO). DeskHPSDR also feeds RX2 from I1/Q1 when rates match (`process_div_iq_data` `:3302–3304`). | Nice later; do not block v1. |
| **C — Thetis 5-DDC** | Keep an independent RX2 DDC while mixing a sync pair. | Only if a 5-DDC Saturn/Orion profile is live **and** testers ask. Not Brick3. Note: Thetis 5-DDC diversity has a chained-assignment inconsistency (`console.cs:8253–8262`) — do not copy that line. |

---

## 7. UI — sequence lock (operator 2026-10-13)

**Get to a working mix first. Polar comes after that point.**

| When | What the operator sees | Why |
|---|---|---|
| **v1 (D4)** | Master **DIV** + gain (dB) + phase (deg). Coarse/fine steppers or sliders; right-click typed entry. DeskHPSDR axes, not Thetis linear `udR`. | Prove the wire, the combiner, and a real null. A compass cannot debug a silent RX1. |
| **Later (D8)** | **Polar with Lyra flare** — not a Thetis DiversityForm clone. Same two numbers (gain, phase) on a polar plane: drag the vector, read dB/deg, Lyra palette / glass / stepper idiom. Optional null helper once the mix is trusted. | Operator yes (2026-10-13): “polar with Lyra flare — but first we need to get to that point.” |

v1 rules:

- Visible only when `diversityCapable && adcCount > 1`.
- **Hidden until D4** (capability gate + panel land together). Do not ship a disabled “not built” DIV button in D0 — that is inert UI.
- Master **DIV** lit button (same idiom as SUB / PS).
- Tooltip: “Two converters, same frequency. Second RF is the RX2 jack (Ext1), not ANT2. Off while transmitting or PureSignal is live.”
- Disabled + explanation when PS armed or TX active (no inert-looking live control).
- **Do not** ship Thetis polar compass in v1. Do **not** start D8 until D2+D3+D5 bench is operator-clean (audible peak/null, PS/TX interlock).
- Brick3 extra-DDC checkbox: **Settings → Radio → Advanced**, default off, tooltip “older Brick3 / Angelia gateware that needs DDC2+DDC3 enabled for the STM32.” **Hide unless Angelia-class profile** (not on 100D).

D8 polar (when we get there):

- One state model: polar **is** dB + phase, not a third axis. Drag updates the same `diversity/gain_db` + `diversity/phase_deg` the steppers already own.
- Lyra-native look (glass, accent, steppers stay). Study Thetis DiversityForm for the *gesture*, do not copy the chrome.
- Still capability-gated; still hidden on HL2; still dead while TX / PS live.

Persistence (suggested keys):

- `diversity/enabled` (bool, default false)
- `diversity/gain_db` (double, 0)
- `diversity/phase_deg` (double, 0)
- `diversity/brick3_extra_ddc` (bool, default false)
- Optional v1.1: per-band gain/phase like Thetis `diversity_gain_{band}`

---

## 8. Phased include (after go-ahead)

Each phase is one revertable commit + tests. Dummy load only. No antenna until the PS interlock and RX2-jack TX isolate are re-verified with DIV armed.

| Phase | Goal | Gate |
|---|---|---|
| **D0** | Doc-only / capability comment. **No UI.** `diversityCapable` stays unread until D4. Zero wire change. | Spec is this file. HL2/Brick2 unchanged. |
| **D1** | Bind `divEXT` in `wdsp_native`. Unit-test rotate math vs DeskHPSDR / `div.c` formula (synthetic IQ). | Green tests; no radio. |
| **D2+D3 (ONE commit)** | P2 overlay **and** host consumer together: `buildDdcSpecificPacket` + HP freq lock + **interleaved DDC0 ingest** (clone/reuse `ingestPsccInterleaved`) + `xdivEXT` → RX1. Angelia: `rx1DdcIndex` / bridge slot **must** follow the overlay (DDC0 interleave, not DDC2). Composer tests: enable `0x01`, `[1363]=0x02`, ADC0/1, both freqs = RX1, **never** mask `0x03`; PS overlay still wins when live. | Packet bytes vs DeskHPSDR capture. Dummy: DIV off = today’s RX1; DIV on = audio present; gain +25 vs −25 audible; 0° vs 180° peak vs null on a local carrier. **Silent RX1 after this commit = FAIL** (forgot the consumer switch). |
| **D4** | DIV panel + persistence + Option A vs SUB. First time the operator sees a DIV button. | UI: HL2 has no button; Brick3 has it. SUB and DIV cannot latch together. |
| **D5** | TX / PS: drop DIV wire overlay, `SetEXTDIVRun(0)`, restore on RX if still armed. Priority table §3.4. | Key TUN with PS on: receive_specific is the **PS** packet, not DIV; DDC0 UDP goes to PS ingest, not the combiner. RX2 jack stays isolated on TX (v0.25.18). |
| **D6** | Optional Brick3 `0x0F` + DDC2/DDC3 HP mirror. | **Only if D2+D3 fail** on a specific Brick3 image that DeskHPSDR needs the checkbox for. Not in the v1 happy path. |
| **D7** | Per-band memory + (optional) ADC1 monitor pane. | After v1 is operator-clean. |
| **D8** | Polar UI with **Lyra flare** (same gain/phase state as D4). | **After** D2+D3+D5 dummy-load peak/null + PS/TX interlock. Not a v1 blocker. |

**Out of v1:** P1 ANAN diversity, Thetis polar *clone*, `sync.c` port, 7000/8000 until that hardware is in scope, CAT ZZDB–ZZDH, TCI DIV, items E/F. Polar-with-Lyra-flare is **wanted**, just not first.

---

## 9. Bench plan (Brick3SDR / 100D)

1. Dummy load, DIV off — RX1 unchanged (null vs current build).
2. DIV on, 0 dB / 0° — still hears the band; panadapter is RX1 mixed.
3. Second antenna or RX2 jack terminated vs open — gain/phase should move the null.
4. Local carrier or noise source into **only** Ext1 — DIV at 0° should bring it into RX1; SUB-off must not be required to “see” it if the mix is live.
5. Sweep phase 0 → 180 on a stable carrier — peak then null (or the reverse).
6. Enable PS + TUN — DIV overlay **gone** on the wire; PS feedback still calibrates; DDC0 UDP is PS interleave, not combiner.
7. Unkey — DIV overlay returns if still armed; no stuck DDC2/DDC3 silence (Angelia remap).
8. Rapid SUB ↔ DIV — exclusive latch, no dual-armed state.
9. HL2 / Brick2 — no DIV control, `P1_en_diversity` still 0, C4 still `0x1C`.
10. Plain TX (no PS) — DIV overlay **off** (Lyra divergence); unkey restores if armed.

---

## 10. Traps (do not rediscover)

1. **HL2 C4 bit 7** is not a free “diversity bit.” Leave `P1_en_diversity = 0`. Thetis emits the bit on HL2 but removes the menu; we do neither the menu nor the bit.
2. **Enable mask `0x01` + sync `0x02`**, not `0x03`, unless Brick3 extra mode (`0x0F`).
3. **`[1363]=0x02` is shared with PS.** One overlay at a time; PS wins. Same byte also **creates the interleaved DDC0 UDP payload**.
4. **IQ is interleaved on DDC0 UDP**, not two host streams. Pairing DDC0+DDC1 ports will miss I1/Q1 (they are not on port 1036 in this mode). Reuse `ingestPsccInterleaved`.
5. **Angelia RX lives on DDC2/DDC3 today.** DIV moves the live pair to DDC0/DDC1 **and** the host must ingest DDC0 interleave. Forgetting either half = silent RX1 with a “working” packet. Overlay + consumer are **one commit**.
6. **Settings ADC** must not stomp ADC1 back onto ADC0 while DIV is on.
7. **Gain is dB in the UI**, linear rotate in WDSP. Do not feed dB into `SetEXTDIVRotate`.
8. **Thetis does not drop DIV on plain TX.** We do (operator lock + safer Brick3). Do not “fix” this back to Thetis.
9. **Do not port `sync.c` / ChannelMaster router** as a block. Lyra’s P2 path is session + bridge, not `InboundBlock`. Thetis inbound cases: 0 = diversity, 1 = PS, 2 = sync, 3 = RX0 only (`ChannelMaster/sync.c:35`, `:49–65`).
10. **DeskHPSDR never calls `divEXT`.** Binding WDSP is still the right Lyra path.
11. **ANT2 is TX.** Second RF = RX2 / Ext1.
12. **Items E/F stay parked** — do not fold “non-blocking channel stop” or wait-infinite into this feature.
13. **ATT link is RX-only** while combining. DeskHPSDR forces ADC1=ADC0 on DIV RX; Thetis `DiversityAttLink` is gated `!_mox`. We link on RX; we do **not** claim ATT-on-TX hits both ADCs.
14. **Do not copy `HERMES_MODE_BRICK` Ext1-hide** onto Brick3. Second RF stays the RX2 jack.
15. **`p2_angelia_ddc0_map` is not diversity.** Non-DIV STM32 mirror; skip it when DIV owns DDC0/DDC1.
16. **Thetis 5-DDC `P1_DDCConfig` cascade** (`DDCEnable = DDC0` assigned into config) looks wrong vs Hermes `P1_DDCConfig = 5`. P1 is out of v1 — do not copy that line later (`console.cs:8253–8262`).
17. **Closing the DIV panel ≠ off.** Radio state stays armed until the operator or TX/PS overlay rules say otherwise.
18. **Hide DIV until D4.** No disabled placeholder button.
19. **100D uses the same `0x01` overlay as Brick3.** Extra-DDC `0x0F` is Angelia-only and optional (D6).
20. **`create_div` zeros the rotate vectors.** `SetEXTDIVOutput(0,2)` + `SetEXTDIVRotate(ref 1+j0)` **before** `SetEXTDIVRun(1)`, or RX1 is silent.
21. **Do not import Thetis per-band phase / ref-swap.** Their tables include a π jump when the reference ADC is swapped. Lyra v1 is RX1-ref-fixed (DeskHPSDR). Fresh 0 dB / 0° per band if we persist later.
22. **PS restore must flip the consumer.** Overlay drop alone is not enough: RX1 back to DDC2 (Angelia), 1035 to PS. Reverse on PS-off if DIV still armed.
23. **Copy RX1 dither/random + IQ gain onto both ADCs; lock RX2 rate = RX1** while DIV RX (DeskHPSDR).
24. **No DiversityForm-style RX2 auto-enable.** DIV does not turn SUB on.
25. **Option A ≠ Thetis P2.** Thetis can keep RX2 on DDC3/1038; we latch exclusive. Do not “fix” that to Thetis.

---

## 11. File touch list (when coding starts)

| File | Role |
|---|---|
| `src/wdsp_native.h/.cpp` | `divEXT` binds |
| `src/wdsp_engine.cpp` | thin `DivExt` wrapper (create/run/rotate/process) |
| `src/wire/P2Session.cpp` | HP freq lock + `buildDdcSpecificPacket` overlay; reuse `ingestPsccInterleaved` (or sibling) for DIV |
| `src/wire/P2RxBridge.cpp` | Interleaved pair → combiner → RX1; DIV vs SUB latch; Angelia `rx1DdcIndex` follows overlay |
| `src/rig/RadioCapabilities.*` | already flagged; start **reading** `diversityCapable` at D4 |
| New `src/qml/DiversityPanel.qml` (or Tuning row) | D4 controls |
| `tests/wire/` packet tests + `tests/dsp/` rotate tests + interleave-split tests |
| `docs/architecture/p2_rx_design.md` | replace “parked” stub with a pointer here |
| Wiki Roadmap Diversity section | already listed as next; update when D4 ships (first visible control) |

---

## 12. Decision checklist for Rick (before D1)

- [x] **v1 = Option A** (DIV exclusive with SUB) — locked in this amendment.
- [x] **UI sequence** — v1 = dB/phase (DeskHPSDR axes). **Polar later, Lyra flare** (D8). Do not start polar until the mix works.
- [x] **WDSP** — latest published is **2.10** (2026-09-04); Lyra already on it. No bump before DIV. Re-check TAPR the day D1 starts.
- [ ] **First hardware = Brick3SDR** (then 100D), dummy load only?
- [ ] **Brick3 extra-DDC checkbox** default off, only if that image needs it? (D6, not happy-path)
- [x] **PS/TX always kills the DIV overlay** — locked; plain-TX drop is a **deliberate** Thetis divergence.

When the remaining boxes are ticked, start at **D0** (doc/capability comment only, no UI, no wire).

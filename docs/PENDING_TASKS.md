# Lyra-cpp — Pending Tasks

> **Historical snapshot, frozen 2026-06-03.** Do not treat the checklists
> below as current — CW, VOX, the voice keyer, TX profiles, and Brick P2
> have all moved on. Live status: [Feature Status](wiki/Feature-Status.md),
> [Roadmap](wiki/Roadmap.md), [REMAINING_WORK.md](REMAINING_WORK.md)
> (also frozen; see its banner). Tree version is **v0.24.0**.

**Snapshot:** 2026-06-03 EOD
**Status:** 28 pending · 0 in-progress · 71 completed (since project start)
**Author:** Rick Langford (N8SDR)

---

## Summary

After the §15.27–§15.30 / #108 TX-side cleanup arc closed out, the pending
work breaks into seven natural groups. Three of those groups are
**large multi-week arcs** (RX2 dual-receiver, TX advanced chain, TX
modulators), and four are **small single-slice items** (mic inputs,
peripherals, UI, cleanup).

**Recommended next step:** `#49 TX Profile Manager` + `#55 chip panel`
(bundled, ~1 day). Captures the recent ALC / Leveler / waterfall tuning
work, lays the schema that the entire TX advanced chain (#50 / #51 /
#52 / #88 / #92) slots into as those features land. See *Recommended
order* at the end of this document.

---

## What ships today (for reference)

So you know what the pending list is filling in *around*:

### TX

| Form | Status |
|---|---|
| SSB (USB / LSB) | ✅ Live — full WDSP TXA chain |
| TUN (tune carrier) | ✅ Live — output-side single-tone gen, drive %-governed |
| Digital via TCI | ✅ Live — MSHV / JTDX / WSJT-X stream into TXA chain |
| **AM / SAM** | ✅ Live — WDSP-native DSB-with-carrier; operator AM Carrier Level (#93) |
| **DSB** | ✅ Live — suppressed-carrier double-sideband |
| **FM** | ✅ Live (basic) — WDSP FM modulator, CTCSS silenced; deviation / pre-emphasis / operator CTCSS knobs are the remaining #107 refinement |
| **CW** | ❌ Not built (#105) |

### Mic inputs

| Source | Status |
|---|---|
| HL2/HL2+ codec mic (`Hl2Ep6MicSource`) | ✅ Live |
| TCI inbound (`TciMicSource`) | ✅ Live |
| **VAC1** (PC audio in) | ❌ Not built |
| **VAC2** (second PC audio in) | ❌ Not built |
| **HL2 codec Line In** (analog) | ❌ Not built |

### RX

| Capability | Status |
|---|---|
| RX1 — full WDSP RXA chain, all modes, all DSP | ✅ Live |
| **RX2 / SUB receiver** | ❌ Plumbing seamed only — feature is 0 % built |

### PTT sources

| Source | Status |
|---|---|
| MOX (button) | ✅ Live |
| TUN | ✅ Live |
| HW-PTT (foot switch) | ✅ Live (opt-in, default OFF per ff5f128) |
| TCI (`trx:1` from client) | ✅ Live |
| **VOX** | ❌ Not built |
| **CW key / CWX** | ❌ Not built (covered by #105) |

---

## Pending — Group 1: TX Modulators (v0.2.2 arc)

Basic AM / DSB / SAM / FM transmit now lands via the WDSP TXA modulator
(per-mode `SetTXAMode` + sign-coded bandpass). CW and the FM operator-knob
refinement remain.

| # | Item | Effort | Notes |
|---|---|---|---|
| **#106** | ✅ DONE — AM / DSB / SAM TX (WDSP-native DSB-with-carrier) | — | Shipped 2026-06-17. Per-mode mode map + double-sideband symmetric ±(BW/2) bandpass (matches RX `computePassband`). Partners with #93. |
| **#93** | ✅ DONE — AM Carrier Level spinner (Settings → TX) | — | Shipped 2026-06-17. UI is % of standard carrier power; coeff = √(pct/100)·0.5, default 100 = standard AM (25 % of PEP). Reference-faithful mapping. |
| **#107** | FM TX **refinement** — operator deviation + pre-emphasis position-1 + CTCSS sub-tones | ~2-3 days | Basic FM transmit is already live (CTCSS silenced at 100 Hz default). This is the operator-knob layer. |
| **#105** | CW TX modulator — internal keyer + CWX + sidetone + CW state bits packed into HL2 TX I-LSBs | ~1 week | TxChannel `Mode` enum extends to CWU/CWL. Adds CW key as new PttSource. Per CLAUDE.md §3.8 — HL2 has 4 CW state bits (cwx_ptt / dot / dash / cwx) packed into TX I-sample LSBs. |

---

## Pending — Group 2: Mic Inputs

Three input paths the design ledger has locked but never built. `tx_dsp_worker.h`
already reserves the `MicSource::MicPc1` / `MicPc2` enum slots.

| # | Item | Effort | Notes |
|---|---|---|---|
| **#102** | **VAC1 TX-input bridge** — PC Soundcard mic + digital-mode audio routing | ~1-2 weeks | Full spec locked in `tx1_ssb_design.md §5.4.1`. Settings → Audio → VAC1 tab with 12 controls (Enable, Driver MME/WASAPI/WDM-KS/DirectSound/ASIO, Input, Output, Buffer Size, Sample Rate, Mono/Stereo, Gain RX, Gain TX, Direct I/Q→Output, Buffer Latency). Plus auto-enable-for-digital toggle + VAC-override group. Uses WDSP `rmatchV` cffi. New `vac1_bridge.{h,cpp}`. |
| **#103** | VAC2 second-channel bridge | bundled with #102 | Same architecture, second instance. Operator can route audio to logger separately from digital-mode app. |
| **#104** | HL2 codec Line In (analog input via I2C2) | ~½ day | Mostly UX — toggle in Settings → TX → Mic source picker + gain slider. Underlying protocol bits already plumbed in `stream.py`. |

---

## Pending — Group 3: RX2 / Dual-Receiver Arc (the big missing piece)

Operator-locked design "Option A Hybrid focused-RX + EESDR active-VFO
enhancements" per `docs/refs/dual_rx/README.md` + the Phase-3 UI
candidates PDF in the same folder. Plumbing seams exist (nddc=4, frame
0x06 reserved, WdspEngine channel-parameterized) but feature is 0 % built.

| # | Phase | What ships | Effort |
|---|---|---|---|
| **#96** | Phase 1 | DDC1 wire dispatch + 2nd WdspEngine RxChannel + RX2 freq writer | ~1-2 days |
| **#97** | Phase 2 | Stereo split audio routing (RX1 hard-L + RX2 hard-R via `SetRXAPanelPan`, AAmixer-style combiner) | ~2-3 days |
| **#98** | Phase 3 | UI focused-RX model (2nd VFO LED + orange focus border + SUB button + Ctrl+1/Ctrl+2 + middle-click focus swap + per-RX vol/mute) | ~3-4 days |
| **#99** | Phase 4 | SPLIT + per-VFO RED/gray TX indicators + A→B/B→A/SWAP + right-click SPLIT per-mode shift-offset | ~3-4 days |
| **#100** | Phase 5 | Per-band RX2 freq memory + QSettings persistence + TCI channel 1 dispatch + tooltips | ~2 days |
| **#101** | RIT/XIT | Extend RX1-only RIT to per-RX RIT offsets, SPLIT-aware XIT | ~1 day after #98 |

**Arc total:** ~2-3 weeks of focused work. Each phase independently
revertable per the locked staged-rollout methodology. Phase 1 is pure
plumbing (no operator-visible win until Phase 3 ships the UI).

---

## Pending — Group 4: TX Advanced Chain

The big v0.2.1+ feature arc — operator-locked chain per
`tx1_ssb_design.md §"POST-COMPONENT-8 TX ADVANCED-CHAIN ARCHITECTURE"`:

```
Mic → [WDSP EQ]──→ [5-band Combinator]──→ [Plate Reverb]──→ ALC/Limiter → TX BW LPF → I/Q → EP2
       toggle       toggle                  toggle              always-on      always-on
```

| # | Item | Effort | Notes |
|---|---|---|---|
| **#49** | **TX Profile Manager** — full chain bundle (Mode + BW + lock + mic + ALC + Leveler + PHROT + EQ + Combinator + Plate + mute-on-TX) | ~1 day | **Recommended next.** Schema mostly derivable from reference's TXProfile XSD. Save / Load / Delete / Set-as-Default. Manual select only — NO auto-detect by call. QSettings JSON. Unblocks #50 / #51 / #52 / #88 / #92 (they add their fields as they land). |
| **#55** | Profile Manager UI — RX/TX quick-preset chips for one-click recall | bundled with #49 | Operator-named chip panel for fast profile switching. |
| **#50** | TX 8-band parametric EQ (EESDR3-style) | ~1 day | Topology locked per `tx1_ssb_design.md §11`. Big log-freq curve + per-band Hz/dB/Q tiles + drag-the-marker editing + master gain. Replaces the older "5-band X-Air placeholder" — supersedes that doc. |
| **#52** | TX Plate Reverb (DSP2024P-faithful Schroeder-Moorer) | ~4-5 days | Native C++23. W5UDX + N8SDR presets locked in design ledger. Ships before the always-on ALC limiter in the chain. |
| **#51** | TX 5-band Combinator — Linkwitz-Riley IIR 24 dB/oct crossovers + 5 parallel compressors + summation + WDSP ALC at output (always-on limiter) | ~1 week | NOT linear-phase FIR. NOT AVX-512. Reaper FX3 "Dual Combinator" reference set landed 2026-06-01 — defaults can be locked. **Includes Pre-EQ vs post-EQ comp ordering toggle.** |
| **#88** | TX Speech Enhancement Suite — Formant boost + Sibilance enhance + DX cut-through + De-esser + Auto-AGC (5 toggleable pre-EQ stages) | ~3-4 days | All sit before EQ in chain. Each toggleable, default OFF. |
| **#59** | RX 8-band parametric EQ (mirrors #50) | ~1 day | Same topology as #50, RX side. |
| **#92** | CESSB Overshoot Control toggle — `SetTXAosctrlRun` | ~2 hr | Single WDSP setter, single checkbox in Settings → TX. |

---

## Pending — Group 5: TX Peripherals

| # | Item | Effort | Notes |
|---|---|---|---|
| **#89** | TX Voice Keyer / Message Memory | ~3-4 days | Contest CQ-loop staple. Recorder + playback queue feeding TxChannel via TciMicSource sibling. |
| **#90** | Hot-mic monitor / SSB sidetone + Separate monitor output device (bundled) | ~2-3 days | Tap post-TXA pre-EP2, route to operator-selectable monitor output device. Useless apart — bundle. |
| **#91** | VOX (voice-operated TX) — PttSource on the FSM + threshold + open/close hang + anti-VOX | ~2-3 days | New PttSource subscriber on PttStateMachine. No new DSP — RMS-threshold crossings of the mic ring. |

---

## Pending — Group 6: TX UI / Misc

| # | Item | Effort | Notes |
|---|---|---|---|
| **#94** | External TX Inhibit — operator-facing controls for relay-protect / external SDR/scope hardware | ~½ day | Settings → TX section with "Update with TX Inhibit state" + "Reversed logic" toggles. Hooks to external hardware input. |
| **#95** | Tune mode selector — 3-way radio (Use Drive Slider / Use Tune Slider / Use Fixed Drive + fixed-drive value) | ~½ day | Completes the partial #74 surface (which shipped binary "use separate tune drive on/off"). Adds the third "Fixed Drive" mode with its own spinner. |

---

## Pending — Group 7: Cleanup

| # | Item | Effort | Notes |
|---|---|---|---|
| **#73** | Cleanup pre-existing "Thetis" mentions in shipped code (§15.26 no-attribution rule) | ~1 hr | Pure hygiene sweep. Provenance only in `docs/` and design ledger. |
| **#76** | Make AGC threshold operator-tunable (Settings → DSP) | ~2 hr | Was deferred when #81 (SetRXAAGCFixed loudness fix) landed. Quick RX-side win. |

---

## Recommended order

The natural sequence that maximises operator-visible wins per hour and
minimises rework. Each phase listed is its own ship-able commit; reorder
freely if priorities shift.

### Immediate (today / next session)

1. **#49 TX Profile Manager + #55 chips** (~1 day)
   *Captures everything you just tuned in §15.27–§15.30. Lays the schema
   the rest of the TX advanced chain slots into.*

### TX Advanced Chain — interleave at your pace

2. **#50 TX EQ** (~1 day) — Topology locked, slots into #49's schema.
3. **#52 Plate Reverb** (~4-5 days) — Presets already locked, no
   screenshot wait.
4. **#51 Combinator** (~1 week) — Reaper FX3 reference set in hand.
5. **#88 Speech Enhancement Suite** (~3-4 days) — 5 toggleable pre-EQ
   stages.
6. **#59 RX EQ** (~1 day) — Mirror of #50.
7. **#92 CESSB** (~2 hr) — Tiny add, single checkbox.

### TX Peripherals — fold in as wanted

8. **#90 Hot-mic monitor + separate output** (~2-3 days)
9. **#91 VOX** (~2-3 days)
10. **#89 Voice keyer** (~3-4 days)
11. **#95 3-way Tune mode** (~½ day)
12. **#94 External TX Inhibit** (~½ day)

### v0.2.2 TX Modulator bundle — when you want CW/AM/FM

13. **#105 CW + #106 AM + #93 + #107 FM** (~3 weeks bundled)

### Mic Inputs — when digital-mode operator surface needs to grow beyond TCI

14. **#102 VAC1 + #103 VAC2** (~1-2 weeks bundled, full spec locked)
15. **#104 HL2 Line In** (~½ day)

### RX2 / Dual-RX Arc — focused multi-week push, schedule when SUB matters more than another TX feature

16. **#96 Phase 1** (~1-2 days, plumbing only)
17. **#97 Phase 2** (~2-3 days, stereo split)
18. **#98 Phase 3** (~3-4 days, UI focused-RX model — **first operator-visible win**)
19. **#99 Phase 4** (~3-4 days, SPLIT + TX indicators)
20. **#100 Phase 5** (~2 days, persistence + TCI dispatch)
21. **#101 RIT/XIT extension** (~1 day after #98)

### Cleanup — anytime

22. **#73 Thetis-mention sweep** (~1 hr)
23. **#76 AGC threshold tunable** (~2 hr)

---

## Provenance

This list consolidates three previously-drifted sources:

1. The live **task tracker** (Tasks #26–#108, managed in Claude session
   state)
2. `docs/architecture/tx1_ssb_design.md` design ledger (parked items,
   §9.5 reserved slots, §13.5 "what this leaves" callouts)
3. `docs/refs/dual_rx/README.md` + the Phase-3 UI candidates PDF
   (operator-locked dual-RX design that had zero task coverage)

The 28-item list above is now self-consistent across all three sources;
the documentation drift that produced "two or more lists" is closed.

When you complete a task, update the live tracker — and update this
document's snapshot date + counts so the printed copy stays useful.

---

*Generated 2026-06-03 EOD as a printable snapshot of the live task
tracker. For the most current state, ask Claude for `TaskList`.*

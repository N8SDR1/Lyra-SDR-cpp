# P2 TX: native 192 kHz TXA output + CFIR — design & plan

**Status:** design locked, pre-code (2026-09-06).
**Scope:** Protocol-2 (Brick/ANAN/Saturn) transmit path only. The
Protocol-1 (HL2/HL2+) transmit path — including its PureSignal —
is **out of scope and must stay byte-identical** (see the
non-regression contract below).
**Why now (operator decision 2026-09-06):** fix the TX foundation
before adding more rigs or more TX features on top of it, because
this refactor is a hard prerequisite for working PureSignal and for
clean wide SSB/ESSB.

---

## 1. The problem, stated precisely

There are **three** distinct sample rates in the TX chain. The gap
is at exactly one of them.

| stage | reference (Thetis / deskHPSDR, P2) | Lyra now | status |
|---|---|---|---|
| RX DDC / IQ rate (operator-selected) | 96k / 192k | 96k / 192k | ✅ not involved |
| TXA **input** (modulator) rate | 48k | 48k | ✅ correct, reference does the same |
| TXA internal **dsp** rate | 96k | 96k | ✅ correct |
| TXA **output** rate | **192k** | **48k**, then an *external* 48→192k `create_resample` | ❌ the defect |
| CFIR (CIC-droop pre-correction) | **ON** (P2) | **off** | ❌ consequence of the above |

The "48k" is the modulator input, not a receive rate and not a
mistake — the reference is 48k-in too. The defect is only that
Lyra keeps the TXA **output** at 48k (the P1/HL2 rate) and bolts on
a hand-rolled resampler, where the reference lets WDSP output the
DUC rate (192k) natively and runs CFIR to pre-correct the radio's
DUC CIC interpolator droop.

**Why it matters (both invisible on a centered tune tone, which is
why the bench looked fine):**
1. **Wide SSB/ESSB HF rolloff.** With no CIC-droop pre-correction,
   the radio's DUC CIC rolls off the high end of a wide passband.
   A single centered TUN tone shows nothing; real off-center voice
   content does.
2. **PureSignal correctness.** PS calibrates the TXA output and
   predistorts against it. If the wire IQ is externally resampled +
   CIC-uncompensated *downstream* of the calibration tap, PS applies
   its correction to a signal that no longer matches what it
   measured. The 48k+external-resample+CFIR-off path structurally
   breaks the PS tap assumption. This is the load-bearing reason to
   fix it before PS work.

The earlier CFIR-off change (which gave centered-tone power parity
~14 W) was the right call *for the wrong architecture*: at 48k-out
the CFIR passband is designed for the 192k CIC path, so it was
attenuating (~−13 dB). The correct answer is to move the TXA output
to 192k where CFIR is valid — not to keep the external path with
CFIR disabled.

---

## 2. Reference architecture (Thetis 2.10.3.13 / deskHPSDR, verified)

- TX channel opened **in=48k, dsp=96k, out=192k**.
  - `SetXmtrChannelOutrate`/`OpenChannel` output = 192000 for P2.
  - WDSP's own `rsmpin` (48→96k) and `rsmpout` (96→192k) do the rate
    work internally; **no external resampler** between WDSP and the
    wire (Thetis `network.c:1237` reads the 192k TXA output buffer
    directly).
- **CFIR ON for P2, OFF for P1**, toggled by protocol:
  - Thetis `audio.cs`: P1 → `SampleRateTX=48000; SetTXACFIRRun(false)`;
    P2 → `SampleRateTX=192000; SetTXACFIRRun(true)`.
  - CFIR is created against the hardware DUC CIC: differential delay
    **DD=1**, interpolation factor **R=640**, integrator-comb
    **Pairs=5**, operating at dsp_rate (96k) with the CIC input rate
    = out_rate (192k). `setOutRate_cfir` re-points it on rate change.
- Amplitude model: **full-scale 24-bit IQ + drive byte @HP[345]** is
  the power knob. (Thetis additionally scales the target by a
  per-band `GainByBand` PA-cal; Lyra sends flat full drive — a future
  power-cal item, same class as the known P1 GainByBand gap, **not**
  part of this refactor.)
- DUC-IQ wire order: **I then Q** (both references; deskHPSDR
  validated on a live Saturn G2). See §6.

---

## 3. The mechanism is already in-tree (the key insight)

Lyra's ported ChannelMaster already carries the reference-native
runtime rate switch: **`SetXmtrChannelOutrate(int xmtr_id, int rate,
int state)`** (`src/wire/CMaster.cpp:718`). In one thread-safe call
(inside `pcm->update[in_id]` critical section) it:

- sets `pcm->xmtr[xmtr_id].ch_outrate` / `ch_outsize`,
- calls `SetOutputSamplerate(chid(in_id,0), rate)` — reconfigures the
  WDSP channel output rate and its internal `rsmpout`,
- reconfigures the txgain, EER, and **ILV interleaver** (`pSetILVInsize`)
  — the ILV is exactly the block whose `Outbound()` call is swapped to
  the P2 DUC producer (`p2TxCmasterOutbound`).

So switching the shared TXA channel between 48k (P1) and 192k (P2)
output is a supported, reference-native operation — not a re-open,
not a rewrite. Required WDSP bindings already exist in
`wdspcalls.h`: `SetOutputSamplerate` (:67), `SetTXACFIRRun` (:218),
`SetChannelState` (:61).

The output buffers `pcm->xmtr[i].out[j]` are pre-allocated at
`getbuffsize(pcm->cmMAXTxOutRate)` (`CMaster.cpp:182`) so a runtime
rate raise needs no realloc — **provided `cmMAXTxOutRate ≥ 192000`**
(verify; almost certainly true since P2 needs it).

---

## 4. Design

The shared TXA channel `chid(1,0)` is created for P1 at 48k out
(`create_xmtr`, `CMaster.cpp:201-214`, out = `ch_outrate`). The P2
DUC producer seam (`P2TxCmaster.cpp`) is the only place that touches
that channel for P2. The fix lives entirely in that seam:

**On P2 TX transport activate (`activateP2TxCmasterProducer`):**
1. `SetXmtrChannelOutrate(0, 192000, <active state>)` — raise the
   shared TXA output to the DUC rate.
2. `SetTXACFIRRun(chid(1,0), 1)` — enable CIC-droop pre-correction
   (now valid at 192k out).
3. **Remove** the external `create_resample(48000→192000)` — the
   outbound callback pushes WDSP's native 192k IQ straight to the DUC
   FIFO. (`p2TxCmasterOutbound` no longer resamples; `resampler`
   state and the `xresample` call are deleted from the seam.)

**On P2 TX transport deactivate (`deactivateP2TxCmasterProducer`):**
1. `SetTXACFIRRun(chid(1,0), 0)` — restore P1 default.
2. `SetXmtrChannelOutrate(0, 48000, <inactive state>)` — restore the
   shared channel to the P1/HL2 48k output.

This is the exact P1↔P2 pattern the reference uses, applied at the
one seam. When no P2 session exists, none of this runs and the
channel stays at its create-time 48k — so P1 is provably untouched.

**Peak/energy diagnostic (`p2TxCmasterLastPeak`) stays** — after the
change it reads the native 192k IQ that hits the wire, which is now
the honest DUC-IQ level. Verify the centered-tone peak is still ~full
(≈1.0) with CFIR on at 192k; if it is not, the shortfall is in the
gain/energy path, **not** CFIR (the earlier −13 dB was the 48k-out
CFIR-mismatch, which this removes).

---

## 5. P1 / HL2 non-regression contract (hard requirement)

The operator constraint: **HL2 (P1) is correct; do not touch it, and
do not disturb the P1 PureSignal path that works as Lyra is currently
written.**

Guarantees this design must hold, verified by bench gate:
- The shared TXA channel is created at 48k out for P1 exactly as
  today. P2 code only *raises* it on activate and *restores* it to
  48k on deactivate.
- With no P2 session open, zero P2-seam code executes → P1 TX and P1
  PS are byte-identical to current `lyra-p2` HEAD.
- After a P2 TX session opens and closes, the shared channel is back
  at 48k + CFIR off → a subsequent P1 TX/PS sees the create-time
  state. **Gate: P1 TX + P1 PS regression-null after a P2 open/close
  cycle** (open a P2 session, key/unkey, close, then exercise the P1
  path — must behave exactly as before).
- Any P2↔P1 rig switch must land the channel back at 48k before the
  P1 path uses it. `SetXmtrChannelOutrate` is critical-section-guarded,
  so the restore is safe against a concurrent P1 open.

---

## 6. I/Q wire order — RESOLVED (Q-then-I is correct for the Brick)

**Outcome (operator bench 2026-09-06): the current Q-then-I DUC-IQ
order is CORRECT for the Brick. Do NOT flip it. S3 is dropped.**

Background: Lyra packs **Q then I** (`P2TxPackets.cpp:86-88`) on the
strength of a KD4YAL-fork comment ("Saturn's InDUCIQ swaps the
components"), while both deskHPSDR (live Saturn G2) and Thetis
(`network.c:1252-1259`) pack **I then Q**. The audit flagged this as
possibly a wrong-sideband/mirror bug.

Resolution, empirically, on the operator's confirmed-correct RX:
1. A separate bug was found first — the **postgen two-tone was not
   mode-signed** (`main.cpp` `.setTwoTone` sent fixed `+700/+1900` in
   every mode), so it sat on the upper side regardless of USB/LSB and
   could not be used as a sideband indicator. Fixed to sign the tones
   per mode exactly like the TUN tone and the bandpass switch
   (USB-side `+`, LSB-side `−`).
2. With the two-tone honest, the bench showed **USB → tones above the
   carrier, LSB → tones below** — landing on the *same side as the
   confirmed-correct RX passband box in each mode*. That means the TX
   sideband is correct end-to-end, so the Q-then-I wire order is
   correct for this hardware. The deskHPSDR/Thetis I-then-Q difference
   is compensated elsewhere in the chain (TXA sign convention and/or
   the radio's DUC handedness) — a genuine Saturn/Brick property, not
   a Lyra bug.

No change to `P2TxPackets.cpp`. The audit's "WRONG-leaning Q/I order"
item is closed in favour of the current code.

---

## 7. Staged plan (each stage bench-gated, smallest revertable step)

- **S1 — Verify preconditions (read-only).** Confirm in the bundled
  WDSP: (a) `create_cfir` in TXA uses the ANAN CIC params
  (DD=1/R=640/Pairs=5) and `SetOutputSamplerate` re-points CFIR's
  rate; (b) `cmMAXTxOutRate ≥ 192000` so `out[]` buffers already fit
  192k; (c) `SetXmtrChannelOutrate` is safe to call while the P2 zero-
  IQ RX-state stream is running. If the bundled WDSP lacks the ANAN
  CFIR params, escalate (may need the WDSP-side params confirmed
  before CFIR-on is meaningful).
- **S2 — 192k TXA output + CFIR on, external resampler removed.
  ✅ DONE + BENCH-PASSED on the Brick (2026-09-06).** Implemented in
  `main.cpp` (cmMAXTxOutRate 48k→192k), `CMaster.cpp`
  (`SetXmtrDucOutrate` narrow reconfigure), `P2TxCmaster.cpp` (seam
  raises to 192k + CFIR on, feeds native IQ, restores 48k + CFIR off
  on deactivate). Operator bench: **correct power on the Palstar with
  TUN**, `iq=0.90` (the ~1.0→0.90 dip is expected: WDSP rsmpout ~0.98
  gain + CFIR headroom — reference-faithful). NOTE the "DUC FIFO 0
  samples" telemetry is normal — it has always read 0 even at full
  power (the writer drains the FIFO to the wire as fast as it fills);
  it is NOT a fill indicator. **Remaining gate: P1/HL2 non-regression
  after a P2 open/close cycle — deferred (HL2 disconnected at bench
  time).**
- **S3 — DROPPED.** The I/Q order was verified CORRECT as-is (§6);
  no change. (The two-tone mode-sign bug found during this step was
  fixed separately — `main.cpp` `.setTwoTone` now signs per mode.)
- **S4 — Wide-SSB verification.** With mic (Stage-2 modulation) or the
  two-tone, confirm the passband no longer rolls off at the high end
  vs the pre-refactor capture (needs the second-SDR spectrum view).
- **P1 regression gate (runs after S2 and again after S3/S4):** P1 TX
  + P1 PS null after a P2 open/close cycle (§5).

Nothing commits until the operator has bench-confirmed at least S2 +
S3. Nothing pushes without an explicit ask.

---

## 8. Open questions / risks

- **Bundled WDSP CFIR params.** The audit read Thetis's WDSP source
  (DD=1/R=640/Pairs=5). Lyra bundles a WDSP DLL (~1.x). If it is built
  from the same NR0V source, CFIR is created with the right ANAN CIC
  params and only the run flag + rate need toggling. **Confirm at S1.**
  If the bundled DLL's CFIR params differ, CFIR-on may not correctly
  match the Brick/Saturn DUC — a WDSP-side item that could pull the
  WDSP 2.00 upgrade forward.
- **Buffer/FIFO sizing at native 192k.** `out[]` is pre-allocated at
  max rate (fine if `cmMAXTxOutRate ≥ 192000`); the P2TxFifo already
  targets 192k. Confirm block sizes line up without the external
  resampler's 256-vs-240 absorption reasoning.
- **Live reconfigure while streaming zero-IQ in RX.** Lyra streams
  zero-IQ on 1029 continuously in RX (a divergence from the reference,
  which streams only while keyed). Raising the output rate on the
  shared channel while that RX-state stream runs must be clean —
  `SetXmtrChannelOutrate` is critical-section-guarded; verify at S1/S2
  there is no glitch on the RX→TX arm.

---

## 9. Sequence around this refactor (operator-locked 2026-09-06)

1. **This refactor** (192k TXA out + CFIR on, I/Q → I-then-Q) — the
   PS prerequisite; works on **current** WDSP (1.x API).
2. **PureSignal** on current WDSP (the wire IQ now == calibrated TXA
   output at 192k, so the PS tap is valid).
3. **WDSP 2.00 upgrade** (PS 3.0 NURBS) — a *quality* follow-on and a
   separate DLL-swap risk surface; **not** a blocker for PS working,
   so it is decoupled from steps 1–2, not gated ahead of them.

More rigs and additional TX features wait behind steps 1–2.

---

## 10. Provenance note

deskHPSDR (`D:/sdrprojects/deskhpsdr`) and Thetis
(`D:/sdrprojects/OpenHPSDR-Thetis-2.10.3.13`) were studied for the
correct architecture and byte layouts; shipped Lyra code stays
Lyra-native with no reference names in code/comments/commits. WDSP
ports remain the sole attributed code. All file:line references here
are audit/provenance material for this document only.

# P2 TX Stage 2 — front-panel mic → modulator (SSB/AM/FM)

**Status:** reference-locked, implementation started 2026-09-06.
**Goal:** get the Brick/ANAN **front-panel mic** to actually modulate
(SSB/AM/FM), not just TUN/two-tone. TUN/two-tone work today because they
are **postgen** (injected at the WDSP *output*, after the modulator);
real modulation needs audio at the modulator *input*, and Lyra currently
feeds the modulator **zeros** (the `P2TxPump` placeholder).

---

## 1. Reference mechanism — Thetis + deskHPSDR (corroborated, high confidence)

Both references implement the identical P2 front-panel-mic → modulator
path. The rig digitizes its own front-panel mic and streams the samples
to the host; the host modulates in WDSP and sends TX I/Q back.

| aspect | value (both references agree) |
|---|---|
| **mic wire stream** | its **own dedicated UDP stream, radio source port 1026** (base+1) — NOT multiplexed with RX-IQ (1035+) or status (1025) |
| **packet** | **132 bytes** = 4-byte big-endian sequence + **64 samples** (128-byte payload) |
| **format** | 16-bit **big-endian**, mono, **48 kHz** (already the DSP rate → NO host decimation on P2; the decimation code is P1/HL2/USB only) |
| **scaling** | int16 → **≈ ×1/32768** → double in [-1,1). (Thetis places the 16-bit value in the top 16 bits of a 32-bit word then ÷2³¹ = the same net thing.) |
| **route** | pack **{I = mic, Q = 0}** → `Inbound(inid(1,0), 64, double*)` → CMB ring → cmaster pump → **`fexchange0(chid, mic_buf, iq_out)`** (the SSB/AM/FM modulator) → `xilv`/Outbound → 24-bit I/Q → port 1029 |
| **source default** | **radio front-panel mic is the DEFAULT/base**; PC-soundcard/VAC/TCI/ASIO only *override* it (Thetis precedence: TCI > VAC > ASIO > radio-mic) |
| **feed timing** | **continuous** — mic is fed to WDSP even in RX (it drains, doesn't transmit); **RF is gated by MOX**, not by the wire feed. Pre-fill ~1024 zeros on RX→TX, flush ~240 on TX→RX (deskHPSDR). |
| **mic gain** | WDSP **TXA panel gain** (`SetTXAPanelGain1 = 10^(dB/20)`), NOT a raw multiply. Hardware mic-boost/line/XLR/bias are separate C&C register bits to the radio. |
| **P2 CFIR** | `SetTXACFIRRun(1)` — already shipped in the 192k/CFIR refactor (`fb12c31`). ✓ |
| **CW / EER** | CW **bypasses** the mic path (keyer/sidetone + PTT bits); EER **post-processes** the modulator output, doesn't replace the mic input. |
| **Brick specific** | **none** — deskHPSDR has zero Brick branches in the mic path; a Brick behaves as a plain Hermes/ANAN. |

Reference anchors (provenance only — shipped code stays Lyra-native):
deskHPSDR `new_protocol.c` recv 2288/2339 → ring 2433 → `mic_line_thread`
2374 → `process_mic_data` 2900; pack `{I,Q}` `transmitter.c:1888`; modulate
`fexchange0` `transmitter.c:1556`. Thetis `network.c` recv 480/521 → decode
`case 1` 748-760 → `Inbound(inid(1,0))` **759**; modulate `cmaster.c:389`;
source overrides `cmasio.c:120`/`pipe.c:217-231`/`ivac.c:129`; gain
`audio.cs:216-244`.

---

## 2. Lyra state — the gap (verified)

Lyra's TX rack + modulator path is **already the reference architecture**:
`feedP2TxCmasterInput → Inbound(inid(1,0)) → cmaster pump → xcmaster →
fexchange0 → xilv → Outbound → port 1029`. Lyra even already has the
**VAC/TCI override layer** in `xcmaster` (`use_vac_audio`/`use_tci_audio`,
CMaster.cpp:426-439) that matches the reference source precedence.

**The one missing piece:** Lyra never receives the radio's mic stream.
`P2Session::onReadyRead` (P2Session.cpp:727-747) dispatches by sender port
and handles only status (1025) + RX-IQ (1035+); line 747 says *"Phase D
adds: 1026 mic, wideband."* So the modulator is fed the **`P2TxPump` zero
placeholder** instead of real mic audio → no SSB/AM/FM RF.

---

## 3. Staged plan (smallest revertable step → bench → next)

- **S2a — RECEIVE + DECODE + DIAGNOSTIC (zero TX risk). ✅ IMPLEMENTED
  2026-09-06 (built clean, unshipped).** `P2Session::onReadyRead` now has
  a `senderPort == kPortMicFromSdr (1026)` + `size == kMicPktLen (132)`
  case → `parseMic()` decodes 4-byte BE seq + 64×int16-BE ×1/32768,
  tracks packet rate + peak + seq gaps, and emits one `logLine` per
  second: `P2 mic: <N> pkt/s  peak=<x.xxx>  seqErr=<n>`. Does NOT feed the
  modulator yet (P2TxPump still owns the zero-feed). **Bench:** confirm
  the Brick streams mic on 1026 at ~750 pkt/s and the peak tracks your
  voice. If NO `P2 mic:` line ever appears, the Brick isn't streaming mic
  on 1026 (or a firewall blocks it) → that's the finding before S2b.
- **S2b — WIRE INTO THE MODULATOR.** Feed the decoded mic `{I=mic, Q=0}`
  into `feedP2TxCmasterInput(iq, 64)` on arrival, and retire/gate the
  `P2TxPump` zero-feed (the mic stream becomes the continuous 48 kHz
  input; the CMB ring absorbs its jitter). RF stays MOX-gated. Mic gain =
  the existing Mic slider (WDSP `SetTXAPanelGain1`). **Bench:** real SSB
  into the dummy, watch it on the panadapter + a second receiver. Confirm
  USB/LSB sideband correct (this is also the definitive sideband test the
  two-tone couldn't give). The VAC/TCI override stays the opt-in
  alternative (reference precedence).
- **S2c — edges:** RX→TX pre-fill / TX→RX flush if bench shows a click;
  gap-fill zeros if the mic stream stalls (network jitter) so the DUC
  FIFO can't underrun.

Deferred/unchanged: CW keying (own path), EER (post-modulator), hardware
mic-boost/line/XLR C&C bits (separate from host DSP gain).

# P2 TX Stage 2 — front-panel mic → modulator (SSB/AM/FM)

**Status:** S2a + S2b **shipped** (2026-09). Front-panel mic on UDP 1026
is decoded and FIFO-fed into the modulator; `P2TxPump` is the 48 kHz
cadence clock, not a zero-only placeholder.
**Goal (original):** get the Brick **front-panel mic** to actually modulate
(SSB/AM/FM), not just TUN/two-tone. TUN/two-tone stay **postgen** (injected
at the WDSP *output*); real voice uses the modulator *input*.

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

**Was the gap; closed in S2a/S2b:** `P2Session::onReadyRead` now dispatches
mic on sender port **1026** (`parseMic`) into a FIFO; `feedTxProducer`
drains `{I=mic, Q=0}` on the pump clock into `feedP2TxCmasterInput`. VAC/TCI
overrides still win when selected. Residual: **S2c** (RX↔TX pre-fill / flush
/ gap-fill) if bench shows a click or a stalled mic stream.

---

## 3. Staged plan (smallest revertable step → bench → next)

- **S2a — RECEIVE + DECODE + DIAGNOSTIC. ✅ SHIPPED.** `onReadyRead`
  dispatches sender port **1026** / 132-byte packets → `parseMic()`
  (BE seq + 64×int16 ×1/32768) with pkt/s, peak, and seq-gap logs. S2b
  then FIFO-feeds that decode into the modulator.
- **S2b — WIRE INTO THE MODULATOR. ✅ SHIPPED.** Decoded mic `{I=mic, Q=0}`
  is FIFO-fed; `P2TxPump` ticks the 48 kHz drain into
  `feedP2TxCmasterInput`. RF stays MOX-gated. Mic gain = the existing Mic
  slider (WDSP `SetTXAPanelGain1`). VAC/TCI override stays the opt-in
  alternative (reference precedence).
- **S2c — edges (open if bench shows it):** RX→TX pre-fill / TX→RX flush if
  a click appears; gap-fill zeros if the mic stream stalls so the DUC FIFO
  cannot underrun.

Deferred/unchanged: CW keying (own path), EER (post-modulator), hardware
mic-boost/line/XLR C&C bits (separate from host DSP gain).

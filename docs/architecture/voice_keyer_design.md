# Voice Keyer + RX Recorder + Playback‑OTA — Design

**Task:** #89 (TX Voice Keyer / Message Memory) + the operator‑floated RX
record → save / playback‑OTA feature.  **Build 1** of the release roadmap
(Voice Keyer + RX Recorder + Bandscope → EXE/push; then RX2; then
PureSignal).

**Status:** design locked 2026‑07‑02 (operator‑confirmed shape).  Not yet
implemented.

---

## 1. Goal — one engine, three features

Build **one native record/playback engine** that delivers all three:

1. **Voice keyer** — record short voice messages (CQ, exchange, "standing
   by"), store a labelled bank, play any into the TX on a click / F‑key.
2. **RX recorder** — capture received audio to a saved clip.
3. **Playback‑OTA** — transmit any saved clip on the air.

This mirrors the reference exactly: Thetis does all of this in **one class,
`clsAudioRecordPlayback`** (`Console/clsAudioRecordPlayback.cs`), driven from
`Console/audio.cs` + the ChannelMaster wave run‑flags
(`cmaster.CMSetSRXWaveRecordRun`).  Build once → all three features fall out.

---

## 2. Reference study — how Thetis does it (and why PureSignal survives)

Studied `clsAudioRecordPlayback.cs`, `audio.cs`, and the Setup → Audio →
**Recording** tab.  Key facts:

- **Record‑source matrix** (`AudioRecordTxSource` / `AudioRecordRxSource`):
  - *When TX'ing* → **MIC Audio** (the voice keyer) or TX Output IQ.
  - *When RX'ing* → **RX Audio** (the RX recorder) or RX Input IQ.
- **MIC Audio is recorded PRE‑processed** (`recTXPreProc = TxSource ==
  MicAudio`) — the tap is at the **mic input, before the TX DSP**.
- **`MoxOnPlayback = true`** → on playback it **asserts MOX and feeds the
  samples back in at the mic‑input point**, so the clip flows through the
  **entire TX DSP chain** (EQ → Comp → CFC → PhaseRot → Leveler → ALC →
  modulator), identical to live mic audio.
- **Per‑stage playback bypass** ("During playback temporarily"): Disable TX
  EQ / COMP / CFC / Phase Rotation — **Leveler left ON by default**.
- **`Enable MON in MOX`** — the operator monitors the clip as it transmits.
- Record post‑processed by default with an optional "Disable RX EQ during
  recording"; **TX Gain (dB)**; WAV format (32‑bit float default / 48 k /
  dither / record channel); storage folder + free‑space guard; optional MP3;
  global stop keybind.

### 2.1 The PureSignal answer (the decisive finding)

Because playback injects at the **mic input (pre‑DSP) with MOX**, PureSignal
sees the played clip as **completely ordinary TX audio** — no special case,
no post‑DSP splice.  **That is why PS works with the Thetis voice keyer.**

⇒ **Lyra injects at the same mic‑input funnel** and PS is satisfied for free.
Every processing‑stage bypass happens *inside* the TX chain, **ahead of the
PS tap**, so PS always sees normal TX I/Q regardless of which stages are on.
Reference‑faithful = PS‑proven.

---

## 3. The injection point (Lyra seams)

Lyra's live mic audio path (already shipped):

```
Hl2Ep6MicSource::Consumer  (48 kHz mono, {I=mic, Q=0} doubles, [-1,+1))
   → TX‑audio input funnel  → TxDspWorker
   → WDSP TXA fexchange0     (EQ · Comp · Combinator · PHROT · Leveler ·
                              ALC · bandpass/mode · modulator)
   → EP2 I/Q                 (PS taps post‑gain, downstream of all the above)
```

`src/mic_source.h` (`Hl2Ep6MicSource`, `Consumer = void(int n, const double*
iq_pairs)` @ 48 kHz), the selected mic source (HL2 codec / TCI / PC / VAC per
`Prefs.micSource`) feeds this funnel; `TxDspWorker` drains it into the WDSP
TXA chain.

**The ClipPlayer becomes a TX‑audio source at that funnel** — while a clip
plays it *is* the active TX source (the live mic is suppressed for the
duration), producing 48 kHz mono samples in the same `{I=sample, Q=0}` shape.
Nothing downstream changes: the clip runs the full modulator chain, so PS,
the meters, and the wire path all behave exactly as for live mic.

**Keying** funnels through the existing FSM via a new `PttSource::Keyer`
(sibling of `Vox` in `hl2_stream.h`): `requestMox(true, PttSource::Keyer)` →
`MoxEdgeFade` keydown → stream clip → on last sample `requestMox(false,
PttSource::Keyer)` → keyup.  Inherits TR‑sequencing, ATT‑on‑TX, TX‑timeout,
mute‑on‑TX for free — same as VOX.  `Keyer` owns only its own key (never
overrides a Manual / HwPtt key; a manual key or Stop aborts the clip
gracefully).

---

## 4. The record/playback engine

New subsystem (working name `src/tx/ClipRecorderPlayer.{h,cpp}` — a native
equivalent of `clsAudioRecordPlayback`).  Responsibilities:

- **Record** from a selectable tap → WAV.
- **Playback** a WAV → the mic‑input funnel, with keying + bypass + gain.
- Own the **clip bank** (labelled, persisted metadata).
- Enforce the **safety rules** (§6).

Threading: playback is a producer feeding the TX funnel at the 48 kHz EP2
cadence (reuse the existing TX‑audio ring discipline, not a new wire clock).
Record is a consumer tap writing to a WAV writer on a worker thread.

### 4.1 Record path

Operator picks the source (mirrors the Thetis matrix):

| Mode | Tap | Notes |
|---|---|---|
| **Voice‑keyer clip** (TX / MIC) | mic input, **pre‑processed** | so playback re‑processes through the *current* TX profile — the message always matches your live sound |
| **RX clip — post** (default) | RX audio **after** RX DSP | exactly what you heard (NR / notch / EQ applied) |
| **RX clip — pre** | RX audio **before** RX DSP | rawer demod audio, for archival / analysis |

*(TX Output IQ / RX Input IQ record modes from Thetis are advanced —
deferred; not in Build 1.)*

Record has an **input gain** so clips capture at a good level.

### 4.2 Playback path — the operator's locked decisions

- **Bypass TX DSP — single on/off toggle** (simpler than Thetis's five
  per‑stage boxes):
  - **OFF** → clip runs through **EQ · Comp · Combinator · PHROT** — sounds
    like your normal processed TX (a CQ recorded with your mic).
  - **ON** → clip plays **flat, as recorded** (a pre‑produced clip, or an RX
    recording re‑transmitted "as heard").
  - Default: **OFF for voice‑keyer clips** (process → matches your voice),
    **ON for RX clips played OTA** (re‑broadcast as recorded).
- **Two playback actions per clip:**
  - **Transmit (OTA)** → asserts MOX, goes out.
  - **Review** → local playback to the monitor/speakers, **no MOX**.
- **OTA playback gain** — a level trim applied **before the modulator** so
  you set how hot the clip drives TX, independent of live mic gain.
  **Global** slider in the panel + optional **per‑clip trim** (so a quiet CQ
  and a loud recording each sit right without re‑riding the master).
- **MON‑in‑MOX** — monitor the clip as it transmits (reuses the #90 monitor
  tap).

---

## 5. Safety — "bypass = flat tone, never flat safety"

The single most important guarantee: **"Bypass TX DSP" only skips the *taste*
stages (EQ / Comp / Combinator / PHROT).  It cannot skip the modulator** —
and the **ALC limiter + the TX bandpass/mode live *inside* WDSP TXA**, which
is the modulator itself, so they are **always on** no matter what.  This
matches Thetis exactly (note "Disable Leveler" is left *unchecked* in the
reference — the level control stays on).  Consequences:

- A fully "bypassed" clip still **cannot overdrive** (ALC) or **transmit
  out‑of‑band** (bandpass).
- The **Leveler stays on by default** even when the taste stages are
  bypassed, for a controlled OTA level.

Plus the standard TX interlocks, inherited via the FSM / `PttSource::Keyer`:

- **IDs + honours the TX timeout** on OTA playback (re‑broadcast rules — a
  long clip past the timeout auto‑unkeys).
- **Never overrides a manual / foot‑switch key**; a manual key or **Stop**
  aborts the clip immediately (own‑key discipline, like VOX).
- **Voice modes only** (SSB / AM / FM) — never in CW or the digital modes.
- **A light UI note on OTA replay of RX clips** re: ID / re‑transmit
  responsibility (operator's call — they're the licensed op).
- **PS‑safe by construction** — injection at the mic input; all bypasses are
  upstream of the PS tap.

---

## 6. Storage & format

- **WAV**, **48 kHz** (the TX/mic path rate), format per the Thetis options
  (default **32‑bit IEEE float**; 16/24/32‑bit PCM offered; optional dither).
- **Clip bank** = a folder of WAVs + per‑clip metadata (label, type =
  voice/RX, per‑clip gain trim, bypass default, created‑date) in QSettings
  or a sidecar — same idea as the CW macro bank (#176) and tuner memory.
- **Storage location:** default `%APPDATA%\Lyra\clips\` (or an operator
  "Music"/custom folder like Thetis); **free‑space guard** (stop recording
  if < N % free).  Optional MP3‑alongside deferred.
- Per‑machine / not exported in a shared `.lyra` profile (clips are local
  media, like the companion‑app binding #193 posture).

---

## 7. UI — two surfaces (the CW pattern)

Identical mental model to CW: **Settings tab = how it behaves; floating panel
= how you operate it.**

### 7.1 Settings → Audio → Recording tab (config)

Mirrors the Thetis Recording tab, mapped to Lyra's stages:

| Thetis | Lyra |
|---|---|
| Disable TX EQ / COMP / CFC / Phase Rotation | one **Bypass TX DSP** default (EQ #50 / Speech‑comp #88 / Combinator #51 / PHROT #109) |
| Disable Leveler | stays on (safety) |
| Playback causes MOX / MON in MOX | `PttSource::Keyer` + MON tap (#90) |
| When TX'ing record MIC Audio | voice‑keyer record (pre‑proc) |
| When RX'ing record RX Audio + Disable RX EQ | RX record **pre/post** tap |
| Bit rate / Sample rate / Dither / Record chan | WAV format group |
| TX Gain / gains | playback + input gain |
| Storage / free‑space / Quick+Recordings folders | storage group |
| Global Stop keybind | global stop keybind |

Set‑once config; width‑capped spin boxes (operator standing pref).

### 7.2 Floating "Voice Keyer" panel (operate)

Pops from a **header chip** (like the **CW console / CW Dec** chips), laid
out like the **CW macro chips** — labelled, editable rows:

```
┌── Voice Keyer ──────────────────────────────┐
│  [● REC]   Bypass DSP ☐    Gain ──●── +3 dB  │
│  ────────────────────────────────────────────│
│  F1 │ CQ Contest       │ ▶ OTA │ ▶ Review │✎ │
│  F2 │ QRZ standing by  │ ▶ OTA │ ▶ Review │✎ │
│  F3 │ 73 & thanks      │ ▶ OTA │ ▶ Review │✎ │
│  R1 │ ⟲ last RX clip   │ ▶ OTA │ ▶ Review │✎ │
│               [■ STOP]    ▓▓▓▓░░ 0:03 / 0:07  │
└──────────────────────────────────────────────┘
```

- **Labelled clip rows** (rename via ✎, like CW macros), each with **▶ OTA**
  (transmit), **▶ Review** (local), and a per‑row select.
- **REC** captures a new clip (source per the Settings tab).
- **F‑keys** fire clips via `MainWindow::keyPressEvent` (C++, not QML
  Shortcut — same as the CW macros #176).
- **Gain slider** + **playing progress bar** + a big **STOP**.
- Voice‑keyer and RX‑recorded clips share the list (an "R1" row is a recorded
  RX clip you can replay OTA).

---

## 8. Staged build plan (build the injector once)

1. **Stage A — the injector core.**  `ClipRecorderPlayer` skeleton + the
   mic‑input funnel producer hook + `PttSource::Keyer` on the FSM + the
   keydown/stream/keyup keying (reuse `MoxEdgeFade`, TR‑seq, timeout).  A
   WAV player feeding the funnel with MOX.  **Unit test:** synthetic clip →
   funnel capture, key/keyup ordering, own‑key/abort discipline.
2. **Stage B — voice keyer.**  Record MIC pre‑proc → WAV; the clip bank +
   the floating panel (labelled rows, F‑keys, OTA/Review, STOP, progress);
   the single **Bypass TX DSP** toggle wired to skip EQ/Comp/Combinator/PHROT
   while keeping Leveler/ALC/bandpass; playback + per‑clip gain.
3. **Stage C — RX recorder + playback‑OTA.**  RX‑audio **pre/post** tap →
   WAV (reuses the RX audio tap / #90 monitor tap); RX clips join the bank;
   OTA replay defaults bypass‑ON (as recorded); the light ID/re‑transmit UI
   note.
4. **Stage D — Settings → Audio → Recording tab** + persistence + USER_GUIDE.
5. **Bench gate** (operator, HL2, dummy load): record a CQ → play OTA →
   external‑meter/monitor check; Review (no MOX) works; Bypass on/off audibly
   differs; RX record → replay OTA as‑recorded; Stop/timeout/own‑key aborts
   all behave.

Each stage is independently testable; the injector (Stage A) is the shared
core that Stages B and C both reuse — exactly the "build once" plan.

---

## 8.1 Parked — TX rack setup without RF (later add-in)

Operator ask 2026-10-06. **Not scheduled.** Wiki: Roadmap 💡 “TX rack setup
without RF.”

Gap: **Review** is raw WAV (no rack). **MON TX** (#90) is post-rack **into**
`fexchange0` — before ALC and TX bandpass — and only while keyed. There is
no dry-run that hears EQ/Combinator/Plate **and** 3 / 4 / 6 kHz TX width
without RF.

If built later:

- Hard no-MOX / no TX I/Q / PA off.
- Loop a 20–30 s pre-rack clip through the same mic funnel as live TX.
- Listen **after** WDSP TXA bp0 so bandwidth A/B is real; current #90 tap
  will never show 3 kHz vs 6 kHz.

---

## 9. Provenance

Reference: openHPSDR **Thetis** `Console/clsAudioRecordPlayback.cs` +
`Console/audio.cs` + the ChannelMaster wave run‑flags, studied for
architecture (mic‑input injection, MoxOnPlayback, per‑stage bypass, the
Recording‑tab option set).  Implemented Lyra‑native.  PS‑safety rationale
per §2.1.  See `THETIS_DIRECT_PORT_PLAN.md` for the provenance/attribution
posture (GPL v3+, TX baseline ported from Thetis ChannelMaster).

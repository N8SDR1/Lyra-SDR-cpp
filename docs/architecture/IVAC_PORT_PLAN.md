# IVAC / VAC Audio-Sources Port Plan (#158 → #102/#103)

Status: **VAC1 SHIPPED (v0.2.4, 2026-06-14).** RX decode + TX on-air both
bench-passed on operator HL2+; released to GitHub. Decisions signed off
2026-06-14 (incl. PureSignal verified unaffected — `calcc.c`/`iqc.c`
reference no audio device; PS feedback is a rate-configured IQ channel
`SetPSFeedbackRate` calcc.c:1065).

▶ **EOD 2026-06-14 — VAC1 RX+TX complete + released. READ THIS FIRST.**

Stages 0–4 + the Stage-6 core (VAC1 Settings panel) are DONE and
operator-bench-confirmed. `main` == `tx-rebuild` == HEAD `e957f00`
(pushed). Tag **`v0.2.4`** + GitHub release (installer + notes) published.

- **Stage 0/1/2/3 DONE** (rmatchV ABI; `wire/Ivac.{h,cpp}` engine;
  `src/ivac_audio.{h,cpp}` Qt I/O; VAC-out RX→PC bench-passed — FT8
  decode via `Line 1`). Stage-3 bench fixed two bugs: zero-length rmatchV
  rings (set SetIVAC*Latency 120 ms after create_ivac) + 2-input mixer
  starvation (feed stream-2 silence each block).
- **Stage 4 DONE — BENCH-PASSED 2026-06-14** (VAC-in PC→TX). MSHV out →
  `Line 2` → VAC1 input → `xvacIN` → TXA → HL2 → RF. On air: worked
  **KF2OG**, N8SDR spotted coast-to-coast on PSKReporter. TCI does PTT/
  freq while audio rides VAC. `use_vac_audio` override in xcmaster case 1
  (before the TCI override); mic_size = getbuffsize(48000) = 64.
  **THE GOTCHA (now documented):** VAC TX = silent/no-power unless
  **Settings → TX → Mic source = "PC Soundcard (VAC1)"** — the mic-source
  selector is the single global switch; `micSource=tci` makes the TCI
  server claim TX-audio ownership and pull from an empty TCI ring →
  memset 0. `applyTxAudioSource` (main.cpp:644) keeps exactly one of
  {codec mic, tci, vac} armed.
- **Profile carries VAC1 + VAC2 (DONE).** Schema 6 stores
  `vac1Enabled/vac1AutoDigital/vac1RxGainDb/vac1TxGainDb` plus the VAC1
  latency/size keys, and the matching `vac2*` set. Applied BEFORE
  `setMicSource` so `micpc` / `micpc2` have a live inbound callback.
  Devices stay GLOBAL station setup. Pre-v6 profiles omit `vac2*` and
  keep VAC2 off.
- **Auto-start-on-launch** Hardware opt-out (default ON) — gates the
  launch auto-connect. Confirmed working.
- **Docs/help updated** (USER_GUIDE.md): new "Digital modes over VAC"
  section, mic-source VAC1, Profiles-stores-VAC, Leveler **not
  recommended for digital modes** callout, auto-start. Shipped in the
  v0.2.4 installer (rebuilt + release asset clobbered).

**NEXT (remaining IVAC work, no longer release-blocking):**
- **Stage 5 — TX-monitor real audio + raw IQ.** Today the VAC-out mixer
  stream-2 (TX monitor) is fed SILENCE; Stage 5 feeds the real TX-monitor
  audio + the raw-IQ (`iq_type`) path. `SetIVACmox`/`SetIVACmon`.
  - **#90 monitor (design locked 2026-06-16; build parked for next session
    — see SESSION_LOG + memory `project-lyra-cpp-monitor`):** the #90 Audio-
    panel MON button + Monitor-volume slider shares ONE tap with this stage.
    The tap = `pcm->in[stream]` AFTER the rack / BEFORE `fexchange0` in
    `xcmaster` case 1 (processed "what you sound like" mic audio) → SPSC
    ring. **Route 1 (HL2 jack, builds first):** drain the ring in
    `WdspEngine::dispatchAudioFrame` and, when MOX up + MON on, emit the
    monitor audio (scaled by the new Monitor-vol) in place of the muted RX
    audio via the existing `OutBound(0)` path (dispatchAudioFrame stays LIVE
    during TX — RX is only gain-zeroed by `txMuted_ && autoMuteOnTx_`).
    **Route 2 (separate PC device, = THIS stage):** feed the same tap into
    `xvacOUT(kVac1Id, stream 2)` (replacing the `vacMonSilence_` block at
    wdsp_engine.cpp:3140) + `SetIVACmon`. Build the tap once, feed both.
- **Stage 7 — VAC2 (#103).** Second `Ivac` instance (id 1) + second
  device pair + selector token `micpc2` (already in `Prefs::micSourceTokens`).
- Stage-6 polish: per-mode buffer/latency surface (#159), SwapIQ/Use-RX2,
  combine-input toggle if MSHV mono-routing ever needs it.

— historical Stage-3/4 as-built detail follows —

▶ **RESUME (Stage 3 — VAC-out LIVE, BENCH GATE):**

Stage 3 as-built (in `wdsp_engine.{h,cpp}`):
  • `WdspEngine` owns VAC1 = the IVAC engine instance (`wire/Ivac`,
    `kVac1Id=0`) + its Qt device layer (`lyra::ipc::IvacAudio`).
  • TAP POINT = `dispatchAudioFrame(audio, nframes)` — the RX audio is
    teed from the `audio` param (POST-RXA: after AGC/NR/demod/AF-panel-
    gain, BEFORE the operator monitor volume/mute/balance) into
    `xvacOUT(kVac1Id, 1, audio)` → the IVAC AAMix input 0 → `rmatchOUT`.
    Feeding pre-monitor-volume audio is deliberate (a digital app needs
    steady level regardless of the operator's speaker volume — the
    reference keeps VAC and AF-gain independent).
  • `IvacAudio::pullRenderInt16` (Stage 2) drains `rmatchOUT` → the
    `QAudioSink` on the chosen PC output device.
  • `create_ivac(0, …)` is built with `audio_rate = outRate (48 k)`,
    `audio_size = outSize_`, `vac_rate = 48 k`, `vac_size = 512`
    (`LYRA_VAC1_VAC_SIZE` override).  `rmatchOUT` drift-corrects the two
    independent crystals even at equal nominal rate.
  • Lifecycle: `rebuildVac1()` at the end of `openRx1()` (idempotent — a
    sample-rate reopen rebuilds at the new `audio_size`/`audio_rate`);
    `teardownVac1()` at the top of `closeRx1()`.
  • Hot-path safety: `vac1Active_` (atomic) gates the tee; the `xvacOUT`
    runs under `vacMtx_`; teardown clears the flag under the lock, stops
    `IvacAudio` (joins the sink), then `destroy_ivac` — so the ring can
    never be freed mid-`xvacOUT` or mid-render.
  • Bench enable (no Settings UI until Stage 6):
      set LYRA_VAC1_OUT=<device index OR description substring>
      (optional) set LYRA_VAC1_VAC_SIZE=<frames 64..8192, default 512>
    e.g. `set LYRA_VAC1_OUT=CABLE Input` (VB-Audio Cable) → point the
    digital app's INPUT at `CABLE Output`.  An unset/no-match var = VAC1
    off (logs `[vac1] … matched no PC output device`).

**Bench gate (operator):** with a virtual cable installed, set
`LYRA_VAC1_OUT` to its playback endpoint, launch Lyra, tune a signal.
Point WSJT-X/MSHV/etc. input at the cable's recording endpoint.  PASS =
the digital app decodes clean radio audio with NO drift / dropouts over
minutes, AND the operator's normal monitor (HL2 jack or PC) is
unaffected, AND muting/changing monitor volume does NOT change what the
digital app receives.  Watch the log for `[vac1] VAC-out LIVE …`.  THEN
Stage 4 (VAC-in LIVE → mic-source selector, mutual-exclusion vs TCI).

**MSHV / FT8 RX-decode bench recipe (operator, 2026-06-14):**
  1. Install VB-Audio Virtual Cable → it adds `CABLE Input` (a PLAYBACK
     device) + `CABLE Output` (a RECORDING device).  Lyra sends RX audio
     TO `CABLE Input`; MSHV listens FROM `CABLE Output`.
  2. Launch: `set LYRA_VAC1_OUT=CABLE Input` then run `build\lyra.exe`.
     Log should show `[vac1] enabled → PC output device [N] CABLE Input`,
     and after a signal is tuned `[vac1] VAC-out LIVE …`.
  3. Lyra: connect HL2, tune an FT8 watering hole (e.g. 14.074 MHz),
     mode USB or DIGU, RX filter ~200–3000 Hz (wide enough for the whole
     FT8 sub-band).
  4. MSHV: Soundcard Input = `CABLE Output`, 48000 Hz; mode FT8; dial =
     Lyra's dial (14.074); PC clock time-synced (<~1 s).
  5. **LEVEL = AF GAIN, not Volume.**  The VAC tap is post-RXA (after AF
     panel gain + AGC) but BEFORE the monitor Volume/mute — so Volume and
     Mute do NOT change MSHV's input, and AF Gain IS the VAC level knob.
     Ride AF Gain to land MSHV's input meter ~30–50 dB (green, not red).
  6. PASS: MSHV waterfall shows the sub-band; FT8 decodes every 15 s; no
     dropouts over minutes.  If choppy: `set LYRA_VAC1_VAC_SIZE=1024`
     (or 2048) and relaunch.  No device match in the log → use a numeric
     index or the exact description substring the log printed.
  (Honest note: coupling the VAC level to AF Gain is a Stage-3
  simplification; a dedicated `vac_rx_scale` independent of AF Gain is a
  Stage 5/6 refinement if the coupling proves annoying.)

Stage 2 as-built (DONE):
  • `src/ivac_audio.{h,cpp}` (`lyra::ipc::IvacAudio`), OUT of `wire/`.
  • VAC-out: a render `QIODevice` (`IvacRenderDevice`) the `QAudioSink`
    pulls — `readData()` drains `rmatchOUT` via `xrmatchOUT` in `vac_size`
    chunks → double×32767 clamp → int16 staging; underrun pads silence
    (rmatchV pads its own ring).
  • VAC-in: `QAudioSource` (pull-mode `readyRead`) → int16/32768→double
    accumulate to `vac_size` → `xrmatchIN`.  Int16/stereo/`vac_rate`;
    mono-only capture devices are a documented Stage-2 limitation.
  • `IVAC ivacGet(int id)` accessor added to `wire/Ivac.h` (returns null
    before `create_ivac` / after `destroy_ivac`).
  • Device enum via `QMediaDevices::audioOutputs()/audioInputs()`.
  • `start(-1,-1)` = no-device setup path (caches sizes, opens nothing) —
    used by the unit test to arm the converters without hardware.
  • `test_ivac_audio` (CMake target, EXCLUDE_FROM_ALL): create_ivac →
    push capture into rmatchIN → drive xvacOUT → drain via
    pullRenderInt16 → diags sane → destroy = ALL PASS.
Reference studied: Thetis 2.10.3.13 `ChannelMaster/ivac.c` (907) + `ivac.h`
(133) + `Console/ivac.cs` (195) + `wdsp/rmatch.c` + `ChannelMaster/pipe.c`
+ `cmaster.c`, cross-referenced against the lyra-cpp ported `wire/` layer
and the existing Qt `QAudioSink` RX-out path.

This is the **host-audio foundation** (#158): the IVAC engine that bridges
PC audio devices to the radio's TX/RX chains. VAC1 (#102) and VAC2 (#103)
are two instances of it. HL2 Line-In (#104) is a separate codec path, not
covered here.

---

## 1. What IVAC does (two independent data flows)

**VAC OUTPUT — radio RX → PC** (so WSJT-X/MSHV/etc. *hear* the radio):
```
RX audio  ─┐
TX monitor ─┴► AAMix (2-in, synchronised)  ──► rmatchOUT ring (RX rate → VAC rate, drift-corrected)
                                                   └─► QAudioSink → PC input device
```
(raw-IQ mode: RX-IQ bypasses the mixer straight into rmatchOUT, + optional Q-swap.)

**VAC INPUT — PC → radio TX** (so the PC app *keys/feeds* the radio):
```
PC output device → QAudioSource ──► rmatchIN ring (VAC rate → mic rate, drift-corrected)
                                        └─► xvacIN → combine(L+R) + preamp → TX mic seam
```

The two directions are fully independent; each has its **own** rmatch ring
doing its own two-clock drift correction. That's why two separate Qt
devices (source + sink) is clean — we don't need one full-duplex stream.

---

## 2. Engine = the reference; device I/O + plumbing = Lyra-native

| ivac.c responsibility | Lyra-cpp approach |
|---|---|
| PortAudio device I/O (`StartAudioIVAC`, `CallbackIVAC`) | **Qt Multimedia.** VAC-out `QAudioSink` + QIODevice mirroring the existing `AudioRing` (`wdsp_engine.cpp:184-267`); VAC-in `QAudioSource` pushing into rmatchIN. Device enumeration/format/buffer-depth pattern already exists (`wdsp_engine.cpp:1303-1355`, `:2354-2403`). |
| Adaptive resampler `rmatchIN`/`rmatchOUT` | **WDSP DLL via `wdspcalls`** — `create_rmatchV` / `xrmatchIN` / `xrmatchOUT` / `destroy_rmatchV` / `forceRMatchVar` / `setRMatch*` / `getRMatchDiags`. NOT plain `RESAMPLE` (no drift loop). NOT a C re-port. **Gated on Stage 0 link-check.** |
| Async mixer (`create_aamix`/`xMixAudio`/`xvac_out`) | **Reuse ported `wire/AAMix`** (Stage B). VAC-out = a 2-input AAMix instance (RX audio + TX-monitor). |
| IN/OUT rings | These ARE the rmatchV-internal rings (sized from latency). No new ring class; do NOT reuse `CmBuffs` (different role). |
| VAC-in → TX seam (`xvacIN`) | **Mirror the TCI TX-in pattern** (`CMaster.cpp:413-418` drain + `:475-478` register), joining the SAME mic-source selector with mutual-exclusion vs TCI (`use_tci_audio`). |
| VAC-out tap (`xvacOUT`) | Hang off **`WdspEngine`'s RX audio/IQ path** (lyra has no `pipe.c`; RX dispatch is `WdspEngine::feedIq` — `CMaster.cpp:377-381`). TX-monitor tap off the TX path. |
| `SetIVAC*` setters/state | Port verbatim as a small `lyra::wire::Ivac` class, instance-parameterised by id (VAC1=0, VAC2=1), like `pvac[id]`. |

---

## 3. Staged build order (each stage builds clean + is revertible; bench-gate the audio-producing ones)

- **Stage 0 — rmatchV link-check + `wdspcalls` ABI expose. ✅ DONE.**
  `dumpbin /exports _native/wdsp.dll` confirmed all 20 needed symbols are
  exported (`create_rmatchV`/`destroy_rmatchV`/`xrmatchIN`/`xrmatchOUT`/
  `forceRMatchVar`/`getRMatchDiags`/`resetRMatchDiags` + 13 `setRMatch*`;
  `create_varsampV`/`xvarsampV` present underneath). Added the 20 cdefs +
  X-macro resolves to `wire/wdspcalls.{h,cpp}`; handle is opaque `void*`
  (no struct port). Build clean. **GATE CLEARED — no `rmatch.c`/
  `varsamp.c` re-port needed.**
- **Stage 1 — `wire/Ivac.{h,cpp}` engine, wire-INERT. ✅ DONE (`52adff3`).**
  Ported the `ivac` struct + `create_ivac`/`destroy_ivac` + the two rmatchV
  rings + the AAMix instance + the full `SetIVAC*` surface; `pvac[]` bank
  file-static; PortAudio device fields + `GetIVACControlFlag` deferred.
  `scratch/test_ivac.cpp` (CMake `test_ivac` target): construct → xvacIN →
  xvacOUT(audio) → drain rmatchOUT → getIVACdiags → destroy = ALL PASS.
  Fixed a heap-corruption bug from initial authoring (struct `malloc0`'d
  but `free`'d — `malloc0` is `_aligned_malloc`; restored reference
  `calloc`/`free`).  Full `lyra.exe` builds clean (engine wire-inert).
- **Stage 2 — Qt device I/O. ✅ DONE.** `src/ivac_audio.{h,cpp}`
  (`lyra::ipc::IvacAudio`): VAC-out render `QIODevice`+`QAudioSink`
  (drains `rmatchOUT`), VAC-in `QAudioSource` (feeds `rmatchIN`),
  Int16↔double + mono-accumulate-to-`vac_size`, device enum via
  `QMediaDevices`, `start(outIdx,inIdx)`/`stop()`, `ivacGet(id)`
  accessor on `wire/Ivac.h`.  `scratch/test_ivac_audio.cpp` (CMake
  `test_ivac_audio`) exercises the converters against a live engine
  with NO devices (`start(-1,-1)`) = ALL PASS; full `lyra.exe` links
  clean (still wire-inert — no RX/TX tap yet).
- **Stage 3 — VAC-out LIVE (RX → PC). ✅ DONE — BENCH-PASSED 2026-06-14**
  (MSHV FT8 decode, monitor Volume −60 dB, RX gain 0 dB).  `WdspEngine`
  tees post-RXA RX audio (`dispatchAudioFrame`'s `audio` param) →
  `xvacOUT(0,1,…)` + a silence block on `xvacOUT(0,2,…)` to keep the
  2-input mixer synced → IVAC AAMix → `rmatchOUT` → `IvacAudio`
  `QAudioSink`.  `create_ivac` + `SetIVAC{Out,In}Latency(120ms)` +
  `SetIVACrxscale` (Gain RX dB) on `openRx1`/`closeRx1`; `vacMtx_`+
  `vac1Active_` race-safety.  Operator-facing via the **VAC1 Settings
  panel** (Stage 6 core, landed early).  (TX-monitor real audio is
  Stage 5; VAC-in is Stage 4.)
- **Stage 4 — VAC-in LIVE (PC → TX). ✅ DONE — BENCH-PASSED 2026-06-14**
  (MSHV FT8 TX on air, worked KF2OG, spotted coast-to-coast; shipped
  v0.2.4).  THE gotcha confirmed on the bench: TX is silent until mic
  source = "PC Soundcard (VAC1)" (TCI wins as the global source otherwise).
  CMaster VAC quartet (`InboundVacTxAudio`/`use_vac_audio`/
  `SendpInboundVacTxAudio`/`SetTXVacAudio`) + the `xcmaster` `asioIN`-seam
  override (before the TCI override); VAC-in bridge `vacInboundCb → xvacIN`
  (null-guarded so picking VAC mic with VAC1 off can't crash on TX);
  `rebuildVac1` opens the input `QAudioSource` + `mic_size=getbuffsize(48000)
  =64` (the §7.3 fix — TX insize, NOT RX-out 256) + VAC TX gain `vac_preamp`
  (+3 dB); `IvacAudio` capture → `rmatchIN`.  Operator surface: VAC1
  **Input device** + **TX gain** in Settings → Audio, and **"PC Soundcard
  (VAC1)"** enabled in the Settings → TX mic-source selector (mutually
  exclusive with TCI / codec mic via the main.cpp gate).  `lyra.exe` links
  clean; RX/TX byte-identical until the PC mic source is selected.
  **Bench gate (TX, dummy load):** MSHV TX → Line 2 → Lyra VAC-in → HL2
  transmits cleanly; selecting VAC disables TCI and vice-versa.
- **Stage 5 — MOX / monitor routing.** `SetIVACmox`/`SetIVACmon` →
  AAMix mute/monitor; wire to the PTT FSM (mute RX into VAC on TX, route
  TX-monitor). + raw-IQ output mode (#IQ) + swapIQ.
- **Stage 6 — Settings UI (VAC1).** Settings → Audio: VAC1 enable, in/out
  device pickers, rate/latency, IQ mode, preamp; add **"PC Soundcard
  (VAC1)"** to the mic-source selector.
  **Reference dialog spec (from `Y:\hold\screenshots\VAC1 and VAC2
  important settings.jpg`, operator's working Thetis — mirror this):**
  Driver (host API) + Input + Output device pickers + Enable VAC1;
  Buffer Size **2048**; Sample Rate **48000**; Mono/**Stereo**; **Gain (dB)
  RX 0 / TX +3** (the operator-facing VAC gains — RX = `vac_rx_scale`
  via SetIVACrxscale; TX = the VAC input gain, `vac_preamp` /
  SetIVACpreamp, default +3 dB); Direct I/O "Output to VAC" (raw-IQ,
  Stage 5) + SwapIQ + Use RX2; Buffer Latency (ms) RingBuffer In/Out
  **120/120** + PortAudio In/Out 120/120 (Manual); Auto-Enable "for
  Digital modes, disable for all others"; "Allow PTT/SPACE/MOX to
  override/bypass VAC for Phone"; "VOX uses MIC instead of VAC"; "Mute
  will mute VAC"; "Combine VAC Input Channels"; VAC Monitor diagnostics
  (Overflows / Underflows / Var Ratio / RingBuffer / RingBuff% for both
  To-VAC(Out) and From-VAC(In) — these are the `getIVACdiags` values).
  Stage-3/4 bench env hooks (`LYRA_VAC1_OUT`, `LYRA_VAC1_RX_GAIN_DB`,
  `LYRA_VAC1_VAC_SIZE`) are the stand-ins for these controls until the
  panel lands.
- **Stage 7 — VAC2 (#103).** Second `Ivac` instance + second device pair +
  `txvac` selector (which VAC drives TX). The engine is already
  id-parameterised, so this is mostly UI + wiring.

---

## 4. Locked decisions (pending sign-off)

1. **Qt Multimedia, not PortAudio.** Matches the existing RX `QAudioSink`
   path; no new native dependency; two-device clocks handled per-ring.
2. **rmatchV via the bundled DLL** (Stage 0 confirms) — not a C re-port,
   not plain RESAMPLE.
3. **Reuse the ported AAMix** for the VAC-out 2-input mixer.
4. **VAC-in joins the mic-source selector** (mutually exclusive with TCI),
   not a parallel TX feed.
5. **Foundation-first**: #158 engine + Qt I/O, then VAC1 (#102) end-to-end,
   then VAC2 (#103). HL2 Line-In (#104) separate.

---

## 5. Risks / open items

- **R0 (gate):** rmatchV not exposed in `wdspcalls` yet — Stage 0 must
  confirm the DLL exports before the Qt approach is viable as-planned.
- **Two-device clock model** vs Thetis's single full-duplex PA stream:
  `CallbackIVAC`'s `vac_size == frameCount` assumption won't hold; size is
  negotiated per Qt device. Each rmatch ring already absorbs the drift.
- **Format/rate:** Thetis forces `paFloat64`; Qt negotiates Int16
  (`wdsp_engine.cpp:1319`). Bridge converts int16↔double; rmatch in/out
  rates configured from the negotiated device rate, not a fixed 48k.
- **Mono→stereo** mic dup (`CallbackIVAC:212-248`) must be replicated.
- **PTT-override / `use_tci_audio` mutual-exclusion** with the existing TCI
  source must be respected at the selector (Task #33), not bypassed.
- **IQ-output mode** needs a separate RX-IQ tap off `feedIq` (not the audio
  tap).

---

## 6. Key reference anchors

- Thetis: `ChannelMaster/ivac.c` (`create_resamps:33`, `CallbackIVAC:196`,
  `xvacIN:129`, `xvacOUT:145`, `xvac_out:165`, `StartAudioIVAC:265`),
  `ivac.h:36-96`, `Console/ivac.cs:53-190`, `wdsp/rmatch.c` (rmatchV ABI),
  `pipe.c:181-235` (the xvacIN/xvacOUT call sites), `cmaster.c:479-575`
  (size/rate setters).
- lyra-cpp: `wdsp_engine.cpp` (AudioRing/QAudioSink 184-267, startAudio
  1303-1355, device enum 2354-2403), `wire/CMaster.cpp` (DEFERRED stubs
  537-538/552-556, TCI-TX seam 413-418/475-478, RX-dispatch note 377-381),
  `wire/AAMix.{h,cpp}`, `wire/wdspcalls.h` (RESAMPLE surface 85-90 — to be
  extended with rmatchV), `mic_source.h`, `tci/TciTxBridge.h`.

---

## 7. Stage 4 design — VAC-in LIVE (PC → TX).  DRAFT 2026-06-14.

**Goal:** a digital app on the PC (WSJT-X / MSHV / JTDX) transmits its
audio out a virtual cable → Lyra captures it → it becomes the TX mic
source → the HL2 transmits it.  VAC-in is selectable, MUTUALLY EXCLUSIVE
with the TCI TX-audio source and the HL2/EP6 mic (locked decision §4.4).
**Bench gate** like Stage 3 — needs the radio + a digital app.

The capture half is ALREADY built: `IvacAudio` (Stage 2) opens a
`QAudioSource` on the chosen PC INPUT device and pushes int16→double into
`xrmatchIN(rmatchIN, …)` via `pushCaptureInt16`.  Stage 3 only opened the
output (sink); Stage 4 opens BOTH (`start(outIdx, inIdx)`).  The new work
is the TX-side consumer + the selector.

### 7.1 The consumer seam — un-defer `asioIN` (the reference's PC-mic line)

`xcmaster` case 1 (the transmitter, `wire/CMaster.cpp:409-436`) builds the
TX mic buffer `pcm->in[stream]` then runs `fexchange0`.  Today:
  • default — the EP6/codec mic is already in `pcm->in[stream]` (the
    `Hl2Ep6MicSource` → CMB ring → cm_main pump path);
  • `use_tci_audio` set → OVERRIDE via `InboundTCITxAudio(insize,
    pcm->in[stream])` (CMaster.cpp:413-419).
The reference's PC-mic input is the **DEFERRED `asioIN(pcm->in[stream])`
line at CMaster.cpp:411-412** ("cmasio unported — no Lyra ASIO").  Lyra's
VAC-in IS that PC mic.  So Stage 4 un-defers it as a Lyra-native
realization, modeled VERBATIM on the proven TCI override quartet:

New CMaster surface (mirror the TCI four — already in-tree + bench-proven):
  • `pcm->InboundVacTxAudio` fn ptr  (mirror `InboundTCITxAudio`)
  • `SendpInboundVacTxAudio(void(*)(int,double*))`  (mirror
    `SendpInboundTCITxAudio`, CMaster.cpp:474-478)
  • `pcm->xmtr[tx].use_vac_audio` interlocked flag  (mirror
    `use_tci_audio`)
  • `SetTXVacAudio(int txid, int active)`  (mirror `SetTXTCIAudio`,
    CMaster.cpp:490-494)

xcmaster case 1 — insert the VAC override at the `asioIN` seam, BEFORE the
TCI override (so the precedence matches the reference's
`asioIN` → `use_tci_audio` order; the selector keeps them exclusive
anyway):
```c
// at CMaster.cpp:411-412 (replacing the DEFERRED asioIN comment):
if (_InterlockedAnd(&pcm->xmtr[tx].use_vac_audio, 1)) {
    if (pcm->InboundVacTxAudio)
        (*pcm->InboundVacTxAudio)(pcm->xcm_insize[stream], pcm->in[stream]);
    else
        memset(pcm->in[stream], 0, pcm->xcm_insize[stream] * sizeof(complex));
}
// existing use_tci_audio block stays at :413-419
```

### 7.2 The VAC inbound bridge — `xvacIN`

A small bridge (mirror `tci/TciTxBridge.h`'s `inboundCb`) registered via
`SendpInboundVacTxAudio`.  Its body is one call:
```c
void vacInboundCb(int nsamples, double* buff) {
    lyra::wire::xvacIN(kVac1Id, buff, /*bypass*/0);
}
```
`xvacIN` (Ivac.cpp:156-170) drains `rmatchIN` into `buff` (the
`xrmatchOUT(rmatchIN, buff)` consumer side), then does `combinebuff`
(L+R, if `vac_combine_input`) + `scalebuff` by `vac_preamp` — exactly the
reference mic path.  Runs on the cm_main TX pump thread; the rmatchV ring
decouples it from the Qt capture thread (the two-clock correction).

### 7.3 ⚠ The size/rate contract — `mic_size` MUST equal the TX insize

`vacInboundCb` is handed `pcm->xcm_insize[stream]` (the TX channel input
block) and `xvacIN` fills exactly `a->mic_size` complex samples → **the
IVAC `create_ivac` `mic_size` must equal the TX channel's `xcm_insize`**,
and `mic_rate` must equal the TX channel input rate (48 kHz).  Stage 3's
`rebuildVac1()` currently passes `mic_size = outSize_` (the RX-out block)
— that is fine while VAC-in is unused, but Stage 4 MUST set `mic_size` to
the TX insize.  **Resolve at implementation:** read the live TX channel
input size (the `create_xmtr_hl2` / `TxChannel` open `in_size`, = the
`pcm->xcm_insize` for the TX stream) and pass it as `create_ivac`'s
`mic_size`.  (RX-out `audio_size` and TX-in `mic_size` are independent
fields of the SAME create_ivac call — get both right.)  rmatchIN was
created `(mic_size, vac_size, mic_rate, vac_rate, …)` at Ivac.cpp:64-class
— so a wrong `mic_size` mis-sizes the ring.

### 7.4 The selector — extend `applyTciTxSource` (main.cpp:630-642)

The mic source is `prefs->micSource()` (string; today "tci" vs the
EP6/codec default).  The gate lambda at **main.cpp:638-639** does
`SetTXTCIAudio(0, tci?1:0)` on `micSourceChanged`.  Stage 4 extends it to
a 3-way mutually-exclusive selector — add a `"vac1"` value:
```cpp
const QString src = prefs->micSource();
const bool tci  = (src == "tci");
const bool vac1 = (src == "vac1");
lyra::wire::SetTXTCIAudio(0, tci  ? 1 : 0);
lyra::wire::SetTXVacAudio(0, vac1 ? 1 : 0);     // NEW
// VAC1 capture device follows the selection:
if (vac1) engine->enableVac1Input(<pc input device>);   // start QAudioSource
else      engine->disableVac1Input();                    // stop capture only
```
Neither flag set ⇒ the EP6/codec mic flows (default).  Exactly one of
{EP6, TCI, VAC1} is ever the live source — satisfies §4.4 + Risk
"`use_tci_audio` mutual-exclusion (Task #33)".

### 7.5 IvacAudio / WdspEngine surface for the input side

`IvacAudio::start(outIdx, inIdx)` already opens the capture
`QAudioSource` when `inIdx >= 0` (Stage 2).  Stage 4 adds to `WdspEngine`
(beside the Stage-3 VAC-out members):
  • `enableVac1Input(int inDev)` / `disableVac1Input()` — set the input
    device + re-`start()` the SAME `IvacAudio` with both out/in indices
    (VAC-out and VAC-in share one engine instance + one `IvacAudio`).
  • `vac1InDev_` + bench env `LYRA_VAC1_IN` (mirror `LYRA_VAC1_OUT`).
  • `rebuildVac1()` passes `mic_size`/`mic_rate` per §7.3 and
    `start(vac1OutDev_, vac1InDev_)`.
  • Register `vacInboundCb` via `SendpInboundVacTxAudio` once at TX setup
    (where `SendpInboundTCITxAudio` is registered — main.cpp ~313).
Note VAC-in and VAC-out are INDEPENDENT (own rmatch rings); either can be
active alone.  A digital-mode op typically wants BOTH (hear + transmit).

### 7.6 Stage 4 implementation checklist (each step builds clean)

1. CMaster: add the VAC quartet (`InboundVacTxAudio` field +
   `SendpInboundVacTxAudio` + `use_vac_audio` field + `SetTXVacAudio`),
   verbatim-mirroring the TCI quartet.  Decl in `CMaster.h`.
2. CMaster: xcmaster case 1 — add the `use_vac_audio` override at the
   `asioIN` seam (§7.1).  RX/TX-quiescent regression: with `use_vac_audio`
   never set, byte-identical to today.
3. `VacTxBridge` (or fold into `IvacAudio`): `vacInboundCb` → `xvacIN`.
   Register via `SendpInboundVacTxAudio` at TX setup.
4. `WdspEngine`: `mic_size`/`mic_rate` reconciliation in `rebuildVac1`
   (§7.3) + `enableVac1Input`/`disableVac1Input` + `LYRA_VAC1_IN`.
5. main.cpp: extend `applyTciTxSource` to the 3-way selector (§7.4);
   `prefs` gains `"vac1"` as a `micSource` value.
6. **Bench gate:** WSJT-X TX → cable → Lyra VAC-in → HL2 TX is clean; and
   selecting VAC1 disables TCI/EP6 (and vice-versa) — verify exclusivity.
   (Settings UI for the source + device pickers is Stage 6; bench via
   `LYRA_VAC1_IN` + setting `micSource=vac1`.)

### 7.7 Open items for Stage 4

- **mic_size source of truth** (§7.3) — confirm the exact TX
  `xcm_insize`/`TxChannel` in_size at implementation; do NOT assume it
  equals RX `outSize_`.
- **Capture device rate** — `IvacAudio` opens the source at `vac_rate`
  (48 k); if a cable only offers another rate, `isFormatSupported` fails
  → log + VAC-in off (same posture as the output side).  rmatchIN handles
  the radio-vs-cable drift; a fixed 48 k both ends is the simple case.
- **PTT interaction** — VAC-in only matters during TX; capture can run
  continuously (the ring just fills/drains).  Whether to gate capture on
  MOX is a Stage 5 (MOX/monitor routing) refinement, not Stage 4.
- **vac_combine_input / vac_preamp** — default unity / no-combine for the
  bench; expose with the Stage 6 UI.

---

## 8. DEVICE-LAYER REBUILD — PortAudio duplex (replaces the Qt two-stream substitution)

**Why (deep dive 2026-06-15, operator-flagged):** the engine port (`wire/Ivac.cpp`
rings/mixer/`xvacIN`/`xvacOUT`) is faithful, but the DEVICE layer is NOT.  The
reference runs the VAC as **ONE full-duplex PortAudio stream** —
`Pa_OpenStream(&Stream,&inParam,&outParam,vac_rate,vac_size,0,CallbackIVAC,id)` —
and `CallbackIVAC` does BOTH directions every interrupt on ONE clock
(`xrmatchIN(rmatchIN,in)` mic→TX + `xrmatchOUT(rmatchOUT,out)` RX→VAC).  Lyra
substituted **two independent Qt streams** (`QAudioSink`+`QAudioSource`, two PC
clocks) and never wired MOX/MON.  That mismatch is the root of: feedback/dead-Mute
(no MOX gating + RX→device continuous, mute-independent), the **crash** (rmatchV
rings driven by two unsynchronized clocks/threads vs one duplex callback), and the
all-or-nothing enable.  Fix = do the device layer the reference way.

**Contained scope (confirmed by recon):** `wire/Ivac.h` already carries every PA
field except `PaStream*`/`inParam`/`outParam`; the `Set*DEVindex`/`hostAPI`/
`PALatency`/`Exclusive`/`mox`/`mon` setters are already ported; **no resampler
work** (`create_resamps` only builds the rmatchV rings, which exist).  Data flow
that STAYS unchanged: RX tee (`dispatchAudioFrame`→`xvacOUT(1)`→AAMix→`xvac_out`→
`rmatchOUT`) and the TX bridge (`vacInboundCb`→`xvacIN` drains `rmatchIN`→TX pump).
Only the device PULL/PUSH moves from the two Qt streams to the one PA callback.

**Sourcing decision (DL-0) — LOCKED 2026-06-15 ("do it like the reference"):**
**vendor PortAudio 19.7.0 SOURCE** (the SAME version the reference bundles in its
`lib/portaudio-19.7.0/`) into `lyra-cpp/third_party/portaudio` + `add_subdirectory`
with `PA_BUILD_SHARED_LIBS=OFF` (→ static `portaudio` target, no DLL to ship) +
`PA_BUILD_TESTS=OFF`/`PA_BUILD_EXAMPLES=OFF`; `target_link_libraries(lyra PRIVATE
PortAudio)`.  PortAudio is MIT — add the notice to NOTICE.md/CREDITS.md (NOT the
no-attribution WDSP-style rule; MIT requires the copyright notice retained).
Windows host APIs confirmed present (wasapi/wdmks/dsound/wmme).  This is exactly
the reference's posture (it vendors the PA source + links it; `ivac.h` →
`#include "portaudio.h"`).  The historical option list is retained below for the
record.

Historical option list:
  (a) vendor PortAudio SOURCE in `third_party/portaudio` + `add_subdirectory`,
      static link (offline, reproducible, no DLL to ship) — RECOMMENDED;
  (b) prebuilt `portaudio_x64.dll` bundled in `_native/` like the WDSP DLLs;
  (c) FetchContent from GitHub at configure (needs network).
Host APIs needed: WASAPI (+ WDM-KS / DirectSound / MME) on Windows.

**Stages (each builds clean + revertible; DL-2 & DL-4 are the bench-gated ones):**
- **DL-0 — PortAudio in the build.** Per the sourcing decision; expose headers +
  link; scratch smoke (`Pa_Initialize`/`Pa_GetVersionText`/`Pa_Terminate`).  No
  behavior change.
- **DL-1 — Un-defer the device-I/O port (verbatim).** Add `PaStream* Stream;
  PaStreamParameters inParam, outParam;` to the `ivac` struct; port `CallbackIVAC`
  + `StartAudioIVAC` + `StopAudioIVAC` verbatim (ivac.c:196-371) incl. the mono→
  stereo dupe + WASAPI-exclusive setup + `swapIQout`.  `Pa_Initialize` once at app
  init (create_rnet-adjacent), `Pa_Terminate` at shutdown.  NOT called by
  wdsp_engine yet (IvacAudio still live) → no behavior change.  Scratch test:
  open+close a duplex stream on a chosen device.
- **DL-2 — Switch wdsp_engine to PortAudio.** `rebuildVac1`: set host_api/in/out
  dev indices + pa latency + exclusive from the operator's choice, then
  `StartAudioIVAC`; `teardownVac1`: `StopAudioIVAC`.  Drop the `IvacAudio` member.
  RX tee + `xvacIN` bridge unchanged.  **BENCH-GATED.**
- **DL-3 — Device enumeration + Settings pickers via PortAudio.** Replace the
  QMediaDevices `vac1{Output,Input}Devices()` with PA host-API+device enumeration;
  Settings combos list PA devices grouped by host API (Thetis-style) + a host-API
  picker; persist host_api + dev (by name, resolve at start).  Keep "(none)".
- **DL-4 — Wire MOX/MON.** `SetIVACmox` off the PTT FSM MOX edge (RX muted out of
  VAC during TX, reference behavior); `SetIVACmon` from a monitor toggle (default
  off).  Remove the `vacMonSilence_` starvation hack (mixer "what" flags handle it).
  **BENCH-GATED.**
- **DL-5 — Retire `src/ivac_audio.{h,cpp}`** (two-stream layer) from the build.
  Mono/channel adaptation now comes from `CallbackIVAC`.
- **DL-6 — Operator HL2 bench + release.** VAC TX (DAW/mic→TX) clean, no feedback,
  Mute correct (RX muted on TX), no crash; RX→VAC decode intact; stop/start +
  toggle stable; Brent + Timmy confirm.  Then cut the release (v0.3.0 or v0.2.6).

**Keep the shipped v0.2.5 as-is** while this is built on the branch; interim fixes
(relabel `88bdab1` / combine `b57bd09` / output-"(none)" `a01d308`) remain until
DL supersedes them.  No-attribution rule applies (RF/first-principles terms in
shipped code/comments/commits; provenance only here).

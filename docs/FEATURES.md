# Lyra (C++23 / Qt 6 rebuild) — Feature Catalog

**Status:** LOCKED 2026-05-20. All 7 open questions resolved.
Ready for Step 2 sequencing (HL2/HL2+ wire path: open the
radio, receive IQ on a dedicated RX thread, parse samples).

This is the consolidated feature list for the C++23 rebuild — drawn
from three sources:

1. **Old Lyra Python** (`Y:\Claude local\SDRProject\lyra\` and
   `CLAUDE.md`) — features that actually SHIPPED on the operator's
   real HL2+ through v0.1.1 GA, plus features that were operator-
   LOCKED scope but not yet built (the v0.2 / v0.3 / v0.4 plans).
2. **Thetis SDR feature list** (operator-supplied 2026-05-20) — the
   reference Windows app that drives the same radio family.
3. **Explicit operator directives from this conversation** — your
   asks for Combinator instead of CFC, P1+P2 protocol support,
   movable panels, glossy theme, noise capture, RTA, 5-band
   parametric EQ.

**How to use this doc:** scan each item. Mark up with one of:

| Tag | Meaning |
|---|---|
| **YES** | Want this, port from old Lyra / build new |
| **NO**  | Don't want it; drop from scope |
| **CHANGE: ...** | Want it but built / behaved differently than described |
| **OPEN:** | Question I'm asking you |

When you're done marking, I lock the catalog and we sequence
implementation in small empirical steps the way Step 1 was done.

Status legend used below:

* 🟢 **PORT** — Shipped in old Lyra Python; behavior is bench-verified;
  C++ implementation is a port with reference behavior.
* 🟡 **LOCKED** — Locked scope in old Lyra docs (§15.x of `CLAUDE.md`)
  but never built before the project was abandoned.  Specs exist.
* 🔵 **NEW** — From this conversation OR Thetis-equivalent NOT in old
  Lyra; needs design before build.
* ❓ **OPEN** — Question to operator.

---

## 0. Architecture (LOCKED — already settled in this conversation)

| Layer | Choice |
|---|---|
| Language | C++23 |
| UI | Qt 6 Quick / QML |
| Graphics | Qt RHI → D3D12 / Vulkan / Metal / OpenGL fallback (cross-API; Thetis is D3D12-only) |
| DSP | WDSP linked directly (native C, GPL v3+) |
| FFT | FFTW3 |
| Wire I/O | Native UDP + `std::jthread` on dedicated OS threads — no GIL, no Python anywhere |
| Build | CMake + Ninja |
| Platforms | Windows now; macOS + Linux are free with Qt RHI when we want them |
| License | GPL v3+ |
| Project name | **Lyra** (logo + dark theme carry over, glossier per your ask) |

---

## 1. Hardware Support Matrix (operator-locked from old Lyra §6.7 + this convo)

| Radio | Protocol | Status |
|---|---|---|
| Hermes Lite 2 (HL2) | HPSDR Protocol 1 | 🟢 PORT — primary target |
| Hermes Lite 2 Plus (HL2+) | HPSDR Protocol 1 | 🟢 PORT — operator's daily-driver |
| Apache Labs ANAN-G2 / G2-1K | HPSDR Protocol 2 | 🟡 P2 profile live; TX dummy-load arm (not on-air validated) |
| Apache Labs ANAN-10 / 10E / 100 / 100B / 100D / 200D | HPSDR Protocol 2 | 🟡 Classic Alex HPF (deskHPSDR edges); TX dummy-load arm; pick marketed model in Settings |
| Apache Labs ANAN-7000DLE / 8000 | HPSDR Protocol 2 | 🟡 LOCKED — OrionMkII BPF / PA not in this pass |
| Apache Labs ANAN still on Protocol 1 FPGA | HPSDR Protocol 1 | 🟡 LOCKED — refuse HL2 TX path; use P2 firmware if the box can (most capable 10/100/200-class units already have) |
| **Brick SDR** | HPSDR Protocol 2 (ANAN-class, pending Timmy confirm) | 🟡 LOCKED |

**Operator answer 2026-05-20:** Brick is **ANAN-class** — Timmy
ran ANAN in Thetis, will confirm exact model. Drops into the
HPSDR P2 / ANAN-G2 code path (option 3 from the original three
choices — covered by the existing ANAN scope, no separate
vendor protocol module needed).

---

## 2. Reception (RX DSP chain)

All 🟢 PORT unless noted. Old Lyra runs WDSP linked via cffi; rebuild
links WDSP directly into the C++ binary.

### 2.1 Modes
🟢 USB, LSB, AM, FM, CWU, CWL, DSB, SAM, DIGU, DIGL, DRM, SPEC

### 2.2 Dual RX
🟢 True dual receiver — RX1 (DDC0) + RX2 (DDC1 / host ch 2) independent
   freq / mode / filter / AGC / NR / etc. Same-ADC radios (HL2 P1,
   BrickSDR2 P2): analog filter follows RX1; cross-band SUB is weaker.
🟢 Stereo-split audio routing — RX1 hard-left, RX2 hard-right by
   default; balance + per-RX volume + per-RX mute always visible.
🟢 SUB toggle = primary RX2 enable.
🟢 Focus model — click any VFO LED to focus; focused RX is what the
   MODE+FILTER panel + DSP panel + panadapter follow.
   Ctrl+1 / Ctrl+2 hotkeys to focus RX1 / RX2.
🟢 A→B / B→A / SWAP buttons.
🟢 SPLIT — TX uses VFO B's freq.

### 2.3 Noise reduction
🟢 NR Mode 1-4 picker (Wiener+SPP / Wiener simple / MMSE-LSA /
   Trained adaptive) — replaces the legacy NR1 vs NR2 backend choice.
🟢 AEPF (Anti-Musical Post-Filter) — on/off toggle, audible diff.
🟢 NPE (Noise Power Estimator) method picker — OSMS / MCRA / etc.
   (Lyra-only knob — Thetis hides this in registry.)
🟢 ANF (Auto-Notch Filter) — profile picker + μ slider.
🟢 LMS adaptive line enhancer — independent toggle + μ slider.
🟢 NB (Noise Blanker) — Off / Light / Medium / Heavy / Custom.

### 2.4 Captured noise profile (your "noise capture")
🟢 **Operator-curated IQ-domain captured profiles.** Tap raw IQ
   pre-WDSP at the operator's current rate, accumulate per-bin
   magnitudes, save as profile. Apply runs Wiener-from-profile
   spectral subtraction on raw IQ before WDSP's RXA chain. Operator
   reports ~6-12 dB noise floor drop depending on band conditions.
🟢 Per-profile rate-locked (192k / 96k / 48k separate profiles —
   profile baseband bin structure is rate-specific).
🟢 Profile manager dialog: list, load, save, delete, switch.
🟢 Right-click "Switch profile" submenu (single-click reload).
🟢 Gain smoothing slider (γ, default 0.6 ≈ 10 ms time constant).
🟢 FFT size picker (1024 / 2048 / 4096).
🟢 **NO auto-select.** Operator picks profile manually. Locked
   directive from old Lyra §9.5: "captured profiles are operator-
   curated by design; algorithmic auto-select overrides operator
   ears."

### 2.5 Squelch
🟢 All-mode squelch via WDSP SSQL (SSB/CW/DIG/SPEC), FMSQ (FM), AMSQ
   (AM/SAM/DSB). One slider, mode-aware routing.

### 2.6 Notches
🟢 Manual notches — right-click on spectrum to add/remove.
🟢 BW + tune + master-run all wired.

### 2.7 AGC
🟢 Profiles: Off / Fast / Med / Slow / Long / Auto / Custom.
🟢 Auto profile: threshold tracker re-calibrates ~18 dB above
   rolling noise floor every 1 sec.
🟢 Threshold + Slope + Decay + Hang time exposed in Settings.
🟢 AGC gain readout (live, ~6 Hz updates).
🟢 Per-band AGC settings memory.

### 2.8 Other RX DSP
🟢 BIN (Binaural / Hilbert phase split) — works on HL2 jack + PC
   Soundcard.
🟢 APF (Audio Peak Filter) — CW peaking, mode-gated to CWU/CWL,
   tracks CW pitch.
🟢 Variable per-mode RX BW — operator-adjustable, per-mode memory.
🟢 CW pitch operator-adjustable — affects passband + VFO marker.

### 2.9 RX Audio output
🟢 HL2 audio jack (EP2 → onboard codec) — default for HL2 / HL2+.
🟢 PC Soundcard output (Qt Multimedia or native WASAPI) — fallback
   + the only option for ANAN (no onboard codec).
🟢 Host API picker: Auto / WASAPI shared / WASAPI exclusive /
   WDM-KS / DirectSound / MME / ASIO.
🟢 Device list grouped by host API (so the same physical device
   shows once per available API, not interleaved).
🟢 VAC compatibility (operator routes Lyra audio to WSJT-X /
   FLDigi / DM780 via Virtual Audio Cable).

---

## 3. Transmission (TX DSP chain) — operator-locked §15.19

All 🟡 LOCKED — these are operator-locked scope from old Lyra
§15.19 but were never built (the project hit first-RF on
2026-05-17 then was abandoned). Carrying the spec forward verbatim
because you locked it.

### 3.1 Modulators
🟢 SSB (USB/LSB) — WDSP TXA chain.
🟢 AM / SAM — WDSP-native DSB-with-carrier; operator AM Carrier Level
   (% of standard, default 100; #93).
🟢 DSB — suppressed-carrier double-sideband.
🟢 FM (basic) — WDSP FM modulator, CTCSS silenced. Operator deviation /
   pre-emphasis / CTCSS knobs are the remaining refinement (#107).
🟡 CW with internal keyer + sidetone (HL2 CW state bits per §3.8
   of old Lyra docs) — #105, not built.

### 3.2 Dynamics — load-bearing
🟡 ALC (xwcpagc on TXA.c line 579) — 1 ms attack / 10 ms decay /
   −3 dBFS thresh per Thetis radio.cs. **Always on** (no operator
   opt-out — splatter protection, not a taste knob).
🟡 Leveler (WDSP wcpagc mode 5 cffi) — shared binding with RX AGC.
🟡 Auto-AGC on mic input (toggle + user gain). Default OFF.
🟡 Noise gate (WDSP amsq block, operator-tunable threshold / hang /
   muted-gain).
🟡 Pre-EQ vs post-EQ compressor ordering toggle. Default post-EQ.

### 3.3 Speech processing — Lyra-native (operator-curated)
**Replaces Thetis CFC with Combinator + extras per your ask.**

🟡 **Combinator** — **5-band** multiband compressor (X-Air style,
   operator-confirmed 2026-05-20). Linkwitz-Riley crossovers,
   operator-adjustable defaults (will dial in from operator's
   X-Air bench screenshots when we get to that stage). Per-band
   threshold / ratio / attack / release / makeup. Optional
   top-band harmonic exciter ("polish").
🟡 **Tube Plating effect** — soft tube-saturation (tanh waveshaper
   with even-order harmonic emphasis) + top-end exciter. Slider for
   amount, default OFF.
🟡 **Formant boost** (toggle) — boost ~700 / 1200 / 2500 Hz.
🟡 **Sibilance / consonant emphasis** (toggle) — high-shelf boost
   ~5-8 kHz, operator-tunable.
🟡 **De-esser** (toggle) — targeted HF compressor for sibilance
   overload. Default OFF.
🟡 **DX cut-through** processor (toggle) — aggressive compressor +
   EQ profile bundle for pile-up readability.

### 3.4 Parametric EQ
🟡 **5-band PARAMETRIC EQ for both RX and TX** (your ask; old spec
   said "3 or 5-band" — locking to 5). Native C++ IIR cascade,
   operator-tunable freq / gain / Q / type (bell/shelf) per band.
   EQ band range extends to **8 kHz** so upper-band "shine" lands
   above standard SSB cutoff (ESSB-friendly).
🟡 Replaces the WDSP 10-band graphic EQ (not used in rebuild).

### 3.5 RTA (Real-Time Audio analyzer)
🟡 **RTA RX + RTA TX** (your ask) — audio-domain FFT widgets render
   live spectrum with EQ overlay preview. TX RTA shows full 0-12 kHz
   so ESSB ops see actual passband (not truncated 0-4 kHz Thetis
   view).

### 3.6 ESSB-friendly
🟡 TX BW operator-adjustable up to **8 kHz** (HL2 wire supports it).
🟡 50 Hz low-cutoff allowed (with warning <100 Hz due to mains
   harmonics).
🟡 Per-band TX BW memory.

### 3.7 PureSignal — IMD pre-distortion linearizer
🟡 v0.3 scope from old Lyra — port WDSP calcc.c + iqc.c + xbuilder
   + delay.c.
🟡 New PSDialog UI modeled on Thetis PSForm.cs.
🟡 Auto-attenuator state machine (HL2-specific bounds: -28 to +31 dB).
🟡 Coefficient persistence per band.
🟡 Operator self-attestation checkbox: "I have the PureSignal
   hardware mod installed" (HL2 only — ANAN G2 has it stock).

### 3.8 PTT / MOX state machine
🟢 Single-state FSM with sources: SW_MOX, HW_PTT, TUN, CW_KEY, VOX,
   CAT_TCI. Sources are OR-funneled — at least one active ⇒ MOX_TX.
🟢 SW MOX button (TUNING panel or TX panel).
🟢 TUN button — low-power tune carrier (needs tune-carrier generator
   commit — was deferred in old Lyra Phase 3).
🟡 Hardware PTT input (EP6 ControlBytesIn[0] & 0x1). Operator opt-in
   default OFF — known per-unit ptt_in-at-rest issue on N8SDR's HL2+
   (foot switch wanted; needs per-unit semantics verification +
   edge-robust debounce).
🟡 CW key (internal keyer + paddle input + CWX).
🟡 VOX (voice-operated TX) with anti-VOX, threshold dBFS, open/close
   hang times.
🟢 CAT_TCI source (TCI client drives PTT).

### 3.9 TR sequencing (Thetis-faithful, operator's working values
from his Thetis DB export):
🟢 RX delay: 5 ms
🟢 MOX delay: 15 ms
🟢 RF delay: 50 ms (hot-switch protection — operator drives 1 kW
   linear)
🟢 PTT-out / RX-settle: 20 ms
🟢 Space-MOX (CW): 13 ms
🟢 Key-up (CW): 10 ms
All operator-adjustable in Settings → TX.

### 3.10 TX power control
🟢 TX Drive 0-100% → HL2 drive level (frame-10 C1, 16 coarse steps).
🟢 Replaces step-attenuator-as-power-knob.
🟢 PA enable toggle (HL2 frame-10 C2 bit3 = Apollo PA enable).
🟢 Mic gain.

### 3.11 ATT on TX (RX-ADC protection)
🟢 ATT-on-TX during TX with operator-adjustable value (default 31).
🟢 Per-band save / restore (Thetis-style m_bATTonTX).
🟢 Force-31 floor when PA off OR PS off (PureSignal prereq).

### 3.12 Voice keyer / message memory
🟡 Recorder + playback queue feeding TX chain. (Your old Lyra spec.)

### 3.13 Operator-curated TX profile picker
🟡 Profile = dict of all relevant TX state (EQ + comp + leveler +
   mic gain + combinator + tube plating + speech enhancements + BW).
🟡 Setting a profile applies all values atomically.
🟡 Save / Load / Delete buttons. **NO auto-detect by call.**

### 3.14 Hot-mic monitor / SSB sidetone
🟡 Tap post-TXA pre-EP2, route to operator-selectable monitor output.
🟡 Independent device picker (separate from main output device).

### 3.15 Safety
🟢 TX safety timeout — host-side QTimer, 1-20 min configurable,
   default 10, bypass checkbox for long AM ragchews / slow CW.
🟢 Fires `radio.set_mox(False)` + toast on expiry.
🟡 VFO lock (planned in Thetis-style watchdog set).
🟡 Out-of-band TX protection.

### 3.16 QSK
🟡 Fast TX-to-RX and RX-to-TX transitions (CW operators).

---

## 4. Spectrum / Panadapter / Waterfall

🟢 High-resolution panadapter — real-time FFT spectrum.
🟢 Waterfall — rolling history with operator-adjustable speed.
🟢 Zoom slider with **live preview during drag**.
🟢 Peak Hold (Off / Live / timed / Hold) + Decay (Fast/Med/Slow) +
   Clear button.
🟢 Spectrum trace fill master toggle + custom fill color picker.
🟢 Waterfall collapse toggle (operator can hide waterfall to
   maximize panadapter).
🟢 Per-band waterfall min/max persistence.
🟢 Per-band spectrum bounds memory.
🟢 Auto-scale spectrum + waterfall.
🟢 FFT smoothing.
🟢 Variable FFT sizes.

### 4.1 Click-to-tune
🟢 Plain click → literal tune.
🟢 Click + drag → drag-to-pan (rate-limited).
🟢 Shift+click → snap to nearest spectrum peak (reticle preview on
   hover).
🔵 Middle-click → swap focused VFO (proposed in old Lyra §6.2 but
   not built; carrying forward).

### 4.2 Markers / overlays
🟢 Tuning marker (orange) — sits at the operator's actual carrier
   freq even for CW (CW pitch offset visualized properly).
🟢 Passband rectangle (cyan during RX).
🔵 **Red-on-air rule (operator-locked §15.9 but not built):** every
   red UI element = "transmitting RIGHT NOW." VFO LED, passband
   rectangle, SPLIT TX marker, status bar accent — all turn red on
   PTT.
🟢 EiBi shortwave broadcaster overlay (auto-detect, freq labels).
🟢 NCDXF beacon markers + tuning (auto-follow).
🟢 TCI spot markers (from SDRLogger+, cluster, RBN, Skimmer).

### 4.3 Notches
🟢 Right-click → add manual notch on spectrum.
🟢 Visual indicator for active notches.

### 4.4 Tuning quantization
🟢 Exact / 100 Hz quantization toggle.

### 4.5 BUG carry-forward — operator-confirmed 2026-05-20
🟢 **§15.22 fix locked: panadapter drag tunes on X-axis ONLY.**
   Only horizontal drag delta produces frequency change. Vertical
   mouse movement during a drag is a no-op (vertical is reserved
   for zoom via wheel). Default behaviour in the C++ rebuild — not
   an operator option, not a setting. Eliminates the "twitchy"
   feel + overshoot the old Lyra had where any vertical drift
   during a horizontal drag added unwanted frequency change.

---

## 5. UI / Interaction

### 5.1 Movable panels (your explicit ask)
🔵 **Operator-arrangeable panel layout.** Drag / dock / undock.
   Save layout per operator. Multiple saved layouts (operator
   switches between "operating" / "tuning" / "contest" layouts).
   Better than old Lyra's QDockWidget — QML SplitView + custom dock
   container gives smoother drag + higher-DPI handling.

### 5.2 Theme + logo (your explicit ask)
🔵 Dark theme carries from old Lyra. Lyra lyre logo carries.
🔵 **A bit more glossy** — QML supports gradients, soft shadows,
   subtle glass/blur effects. I'll mockup in QML when we get to UI;
   you approve.

### 5.3 Frequency display (VFO LED)
🟢 Carrier-frequency convention — LED shows tuned signal's carrier
   (post-CW-pitch offset, matches every other major HF SDR).
🟢 Two TX indicators per VFO LED (red active, gray inactive).
   SPLIT auto-moves red → VFO B; click gray to manually swap.

### 5.4 Mode + Filter panel
🟢 Single panel acts on focused RX.
🟢 Per-RX shows status badges (mode, filter, AGC) on its VFO area.

### 5.5 DSP + Audio panel
🟢 Top row: `[−] value [+]` StepperReadout widgets (Vol RX1 / Vol RX2 /
   AF Gain — NOT sliders; operator's preference per §15.17).
🟢 Click+hold ramp, Shift+click 5 dB, right-click typed entry,
   wheel-tune.

### 5.6 Tuning panel
🟢 Independent **SUB** (HL2 P1 and BrickSDR2 RX2, same ADC) and **SPLIT** (TX on VFO B) —
   not a tri-state cycle. Gray TX pips, right-click SPLIT per-mode
   1/5/10 kHz shift, TUNE A/B pan tooltip, middle-click focus swap.
🟡 RIT toggle + ±9999 Hz offset, 1 Hz click / 10 Hz Shift+click /
   right-click typed entry, ±9999 Hz range. Persists across
   sessions, RX1 only (per-RX deferred). Already shipped in v0.1.1.
🟡 XIT toggle (TX-mirror RIT). Renders but inert until TX in v0.2 —
   then ~2 hr enable on top of RIT plumbing.
🟢 GEN 1/2/3 customizable favorites.
🟢 TIME button (HF time-station cycle: WWV/CHU/BPM/RWM).

### 5.7 Memory bank
🟢 20-slot Memory bank with CSV import/export.
🟢 Per-slot save: freq, mode, filter, AGC, NR, BW.

### 5.8 Per-band memory
🟢 Frequency, mode, filter, AGC settings, NR settings, RX BW, AGC
   threshold, spectrum bounds, waterfall bounds — all per-band.

### 5.9 Bands strip
🟢 Amateur band quick-pick buttons (160 / 80 / 60 / 40 / 30 / 20 /
   17 / 15 / 12 / 10 / 6 / 2 m).
🟢 BC/Other band shortcuts.

### 5.10 Help / docs
🟢 F1 in-app user guide.
🟢 Per-feature tooltips.
🟢 Help dialog with searchable topics.

---

## 6. Meters (your explicit ask)

🟢 **Lit-Arc style** (classic analog needle look, calibrated).
🟢 **LED-bar style**.
🟢 **Multi-meter panel** (operator toggles which to show).
🟢 dBm + S-units + dBFS readouts.
🟢 Sources:
   - **S-meter** (RX, with peak-hold smoothing ~500 ms)
   - **PWR** (TX forward power, calibrated)
   - **SWR** (TX reverse vs forward)
   - **ALC** (TX peak limiter)
   - **MIC** (mic input level dBFS)
   - **COMP** (compressor gain reduction — Combinator)
   - **PA Volts / PA Current** (HL2 telemetry — operator's working
     values: ~12.3 V, ~1.8 A at full TUN)
   - **Temp** (HL2 board temperature)
🟢 Source-swap on MOX edge (S-meter row → ALC during TX, etc.).
🟢 Click meter face to cycle which source it shows.

---

## 7. Connectivity

### 7.1 TCI server (Transceiver Control Interface)
🟢 TCI server advertising `channel_count:2` (RX1 + RX2).
🟢 `vfo:0,1` = SPLIT VFO B; `vfo:1,0` / `dds:1` = RX2 SUB;
   `rx_enable:1` = SUB (do not auto-enable SUB on RX2 freq set).
🟢 `rx_channel_sensors:0,0` RX1 S-meter; `:1,0` RX2 (floor if SUB off).
🟢 Outbound: RX2 freq / mode as channel 1 updates.
🟢 PTT via TCI (CAT_TCI source on FSM).
🟢 Spot routing in (SDRLogger+, cluster, RBN, Skimmer).
🟢 spot_activated outbound.

### 7.2 CAT
🟡 **Virtual COM port CAT — Kenwood TS-2000 emulation** (operator-
   confirmed 2026-05-20: Kenwood is the more reliable / widely-
   supported choice). For non-TCI loggers / WSJT-X / FLDigi /
   N1MM / DXLab.
🟡 **BCD FTDI cable option (Yaesu-style band-data output)** —
   port from old Lyra. Same hardware path the Yaesu rigs use for
   external linear amplifier band-switching (FT-991A / FT-DX
   family); operator-confirmed needed. Optional dep on Windows
   FTDI driver (the `ftd2xx` package from the old Lyra install
   guide).
🟡 Foot-switch via PTT line on a CAT serial port (alternative to
   EP6 HW-PTT-in).

### 7.3 VAC (Virtual Audio Cable)
🟢 Dual VAC channels (RX audio out + TX audio in for digital
   modes).
🟢 Documented routing recipe for WSJT-X / FLDigi.

### 7.4 Network / discovery
🟢 Multi-NIC discovery (Step 1 already shipped this — works on your
   bench).
🟢 Operator-override target IP for unicast (skip broadcast on
   complex networks).
🟢 Operator-override bind IP for specific NIC.

### 7.5 Quick recording + Voice message playback (operator-confirmed 2026-05-20)
🔵 **Quick-record RX audio / mic input to WAV file.** One-click
   record button + save dialog. Audio-domain capture (not IQ).
   Useful for logging or signal sharing.
🔵 **Record-and-play-over-air ("voice keyer plus")** — operator
   ask: pre-record a clip (contest CQ-CQ-CQ, club net intro,
   "this is N8SDR testing", etc.) and key the radio with it
   on a button press. Distinct from §3.12 voice keyer in that
   the operator can record the clip live in Lyra (vs loading
   a pre-existing WAV) and trigger arbitrary clips, not just
   one. Implementation = numpy WAV recorder + a clip library
   + a TX-chain injector that feeds the recorded buffer into
   TxChannel instead of (or mixed with) live mic.

---

## 8. Diagnostics

🟢 HL2 telemetry banner: T (temp °C), V (supply V), PA (current A).
🟢 ADC pk / rms display (overload indicator).
🟢 AGC threshold + gain readout.
🟢 AUTO LNA messages.
🟢 Audio stream error indicators.
🟢 Diagnostic overlay 3-state toggle (Full / Minimal / Off).
🟢 Network Discovery Probe dialog (operator can see exactly which
   interfaces were tried, what packets went out, what came back).
🔵 **NEW:** wire-cadence probe (was added late in old Python
   project) — optional for cross-checking timing on real hardware.

---

## 9. Audio routing (operator's actual setup)

🟢 HL2 audio jack (EP2 → AK4951 codec) as primary output.
🟢 PC Soundcard fallback / for ANAN.
🟢 Mic input source selector (HL2+ AK4951 mic via EP6 / PC
   soundcard mic input).
🟢 Output device picker grouped by host API.
🟢 VAC routing for digital modes.

### 9.1 Auto-mute-on-TX rules (deferred from old Lyra §15.14)
🟡 Mute RX1 on VFO B TX (toggle).
🟡 Mute RX2 on VFO A TX (toggle).
🟡 Mute RX2 on TX (toggle) — Thetis default.

---

## 10. Settings / Preferences

🟢 All operator-tunable values persisted (per-band, per-RX, per-profile).
🟢 Settings dialog with tabs: Radio / Network/TCI / Hardware /
   DSP / Noise / Audio / TX / Visuals / Keyer / Bands / Propagation /
   Weather.
🔵 **Settings export / import as a single file** (operator-confirmed
   2026-05-20). Operator asked "what is best now for exporting
   and saving / transfer? is JSON still the best choice?"

   **Recommendation (locked unless you say otherwise): JSON.**
   Reasons:
   * Human-readable + diff-able (operator can open in Notepad to
     spot-check before importing on another machine).
   * Native in Qt (`QJsonDocument`) — zero extra dependency, no
     parser bugs.
   * What every other modern SDR (Thetis, ExpertSDR3, SparkSDR)
     and modern Windows app standardised on. Operators moving
     between Lyra + other apps see a familiar format.
   * Future-proof: JSON Schema versioning lets us add fields
     without breaking older exports.

   Considered + rejected: INI (no nesting — bad fit for
   per-band/per-RX/per-profile dict-of-dicts); XML (verbose,
   no human wins over JSON, 2026-era anachronism); SQLite (binary,
   can't email a config to a tester); YAML (extra dep, parser
   ambiguity — JSON wins for config files).

   Format: single `lyra-settings.json` file with sections for
   Radio / TCI / Hardware / DSP / Noise / Audio / TX / Visuals /
   Keyer / Bands / Propagation / Weather / Profiles (TX +
   noise-capture). Version field at top for schema migration.
   Settings dialog: **Export...** + **Import...** buttons at the
   bottom.

---

## 11. Propagation panel
🟢 HamQSL solar XML (SFI, K, A, X-ray, etc.).
🟢 Auto-refresh.
🟢 Band-condition color coding.
🟢 NCDXF beacon list with follow mode (auto-tune to next beacon at
   schedule).

---

## 12. Weather + WX alerts (operator-confirmed 2026-05-20 — PORT)

### 12.1 Toolbar WX indicator (`lyra/ui/wx_indicator.py` in old Lyra)
🟢 Compact 3-badge header widget. Each badge auto-hides when its
   tier is "none" — zero pixels on quiet days.

   * **⚡ Lightning** — closest strike distance + bearing.
     Tiers: Far (yellow, >25 mi) / Mid (orange, <25 mi) /
     Close (red, <10 mi). Operator-tunable thresholds.
   * **💨 Wind** — sustained / gust speed.
     Tiers: Elevated (yellow) / High (orange) / Extreme (red).
     Operator-tunable thresholds.
   * **⚠ Severe** — NWS active alert (single glyph + headline
     tooltip).

   Distance unit (mi/km) + wind unit (mph/kph/kt) operator-selectable.

### 12.2 Weather panel — full conditions
🟢 Live local conditions widget (temperature, pressure, humidity,
   wind, precipitation, etc.).
🟢 Auto-refresh.

### 12.3 Data source — operator-confirmed 2026-05-20: ALL FOUR

🟢 **Ambient WS-2000** (operator's local station) — same source
   the old Lyra `wx-dashboard` aggregator used. Local-accurate
   readings (temp, pressure, humidity, wind, rain, lightning).
🟢 **NWS API** (api.weather.gov) — official severe-weather
   alerts + government forecast. Free, no API key.
🟢 **OpenWeatherMap** — secondary public source for coverage
   redundancy + global stations (when traveling / portable).
   Free tier sufficient.
🟢 **Ecowitt** — operator has Ecowitt code to provide. Same
   station class as Ambient (often the same hardware rebranded);
   gives operators on Ecowitt-branded gear the local-accurate
   path too.

Aggregator pattern (port from old Lyra `wx/aggregator.py`): all
four sources feed a single WxState struct; operator picks per-
field priority in Settings → Weather (e.g. "Ambient for local
temp/wind/lightning, NWS for severe alerts, OpenWeatherMap for
forecast, Ecowitt for redundancy"). Stale-source detection +
fallback. One unified panel + toolbar indicator regardless of
which source is live for which field.

---

## 13. Logo / branding
🔵 Lyra logo carries from old Lyra (lyre stylized).
🔵 GPL v3+ license headers per file (NOTICE.md carries).
🔵 Author: Rick Langford (N8SDR) — same as Python project.

---

## 14. Explicit divergences from Thetis (your asks, locked)

| Thetis | Lyra C++ rebuild |
|---|---|
| **CFC audio tools** | **Combinator** (X-Air-style 4 or 5-band) + Tube Plating + Formant boost + Sibilance + DX cut-through + De-esser |
| WDSP 10-band graphic EQ | 5-band parametric EQ (RX + TX) |
| DirectX 12-only graphics | Qt RHI (D3D12 + Vulkan + Metal + OpenGL — Lyra runs cross-platform for free) |
| Hidden NR knobs (NPE in registry) | NPE picker exposed in DSP+Audio panel |
| Thetis "containers" layout system | QML movable / dockable panels with save-layout-per-operator |
| No captured noise profile | IQ-domain captured noise profile (operator-curated) |
| 4 kHz TX RTA display | 0-12 kHz TX RTA (ESSB-friendly) |

---

## 15. Items confirmed by operator 2026-05-20 (moved from OPEN → PORT)

🟢 **TIME button** — HF time-station cycler (WWV / CHU / BPM / RWM).
   ✅ KEEP.
🟢 **EiBi shortwave broadcaster overlay** — visual labels for SW
   broadcaster freqs on panadapter. ✅ KEEP.
🟢 **GEN 1/2/3** — customizable favorite-freq quick buttons.
   ✅ KEEP.
🟢 **NCDXF beacon rotation / auto-follow** — auto-tune to next
   beacon on its schedule. ✅ KEEP.
🟢 **Weather panel + WX toolbar indicator (Lightning / Wind / Severe)**
   — see §12 above. ✅ KEEP. Data source: Ambient + NWS +
   OpenWeatherMap + Ecowitt (operator has Ecowitt code) per
   §12.3.

### Carry-over reminders from old Lyra (operator-confirmed
2026-05-20 — already in §4 above; calling out so they're not lost):

🟢 **100 Hz / Exact tuning quantization toggle** — already in §4.4.
   Operator-tunable between literal-Hz and 100 Hz snap.
🟢 **Peak Hold options** (Off / Live / timed / Hold) + Decay
   (Fast/Med/Slow) + Clear button — already in §4. Port the
   exact v0.0.9.7 UX.
🟢 **Panadapter / waterfall rate options** — already in §4
   ("Waterfall — rolling history with operator-adjustable speed"
   + "Variable FFT sizes" + "FFT smoothing"). Port the live-
   preview-during-drag zoom slider behaviour (v0.0.9.7 polish).

**Operator standing directive:** "should look and act like Lyra
only without the issues we had." All v0.0.9.x polish that
shipped clean carries over — no behaviour regressions vs the
Python build. New behaviour only where it directly addresses a
known Python-era issue (the §15.22 Y-axis drag bug, the audio
chop, the relay chatter, etc.).

---

## 16. Open Questions Summary — ALL RESOLVED 2026-05-20

All seven open questions from the previous draft are answered.
Full text in the referenced section.

1. **Brick SDR — ANAN-class** (HPSDR P2; exact model pending
   Timmy confirm). See §1.
2. **Combinator — 5-band** (X-Air screenshot when we get there).
   See §3.3.
3. **CAT — Kenwood TS-2000 emulation** + **BCD FTDI cable
   (Yaesu-style band-data output)** for external linear
   amplifier band-switching. See §7.2.
4. **Quick-record RX audio — YES**, plus **record-and-play-over-
   air clip library** (contest CQ-CQ, club net intro, etc.).
   See §7.5.
5. **Settings export/import — JSON locked** (human-readable,
   Qt-native, future-proofed with schema versioning, what
   modern apps standardised on). See §10.
6. **WX data — ALL FOUR sources** (Ambient WS-2000 + NWS +
   OpenWeatherMap + Ecowitt). Operator-prioritised per field
   in Settings; aggregator pattern from old Lyra. See §12.3.
7. **§15.22 panadapter Y-axis drag — fixed in rebuild**,
   only X-axis drag tunes (no setting, default behaviour).
   See §4.5.

**Catalog LOCKED 2026-05-20.** Ready for Step 2 sequencing.
Next: operator picks the implementation order (Step 2
HL2/HL2+ wire path — START packet + EP6 receive thread + IQ
parsing — is the natural next step; everything else stacks
on top of a working radio).

---

## 17. NOT in scope (operator NO'd previously in old Lyra)

These are documented operator-NOs from old Lyra — listed here so the
rebuild doesn't accidentally drift into them:

* **Speaker-selective audio attenuator** (§9.8 WITHDRAWN 2026-05-10).
* **Cyclostationary powerline noise modeling** (§9.5 NOT PURSUED).
* **Auto-select captured noise profile** (§9.5 — operator picks
  manually).
* **Loudness-war / aggressive multi-stage psychoacoustic TX
  processors** (§15.19).
* **TX audio history / replay buffer** (§15.19).
* **Auto-detect EQ profile by call signature** (§15.19).
* **Per-mode auto-switch TX profiles** (§15.19 — operator picks
  manually).

---

## 18. Implementation discipline (locked methodology, no exceptions)

* Smallest revertable empirical step → operator HL2 bench → next.
* No multi-agent convergence rounds. One design read, one
  implementation, one bench.
* Read reference (Thetis source, old Lyra Python, HL2 protocol
  docs) BEFORE writing code, not after.
* When you say "doesn't sound right" or "didn't start clean" — that's
  the truth, not a data point to argue with.
* Operator-empirical OUTRANKS inference. Always.

---

*End of catalog. Mark up freely. Add items I missed. Strike
items you don't want.*

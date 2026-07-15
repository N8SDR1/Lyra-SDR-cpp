# Lyra — C++23 / Qt 6 Rebuild

**🌐 [Website & download →](https://n8sdr1.github.io/Lyra-SDR-cpp/)** · [Releases](https://github.com/N8SDR1/Lyra-SDR-cpp/releases) · [Discord](https://discord.gg/BwjsQvjcSc)

Native rebuild of [Lyra](../lyra) (the Python+Qt6 desktop SDR transceiver
for the Hermes Lite 2 / 2+).  The Python tree is preserved in `../lyra/`
as the protocol research + doc archive reference; this is a clean ground-
up rewrite using the architecture the project should have started with.

> **Acknowledgments at a glance:** Lyra-cpp incorporates code ported
> from two GPL-compatible open-source projects:
> **[WDSP](https://github.com/TAPR/OpenHPSDR-wdsp)** (Dr. Warren Pratt,
> NR0V) for DSP algorithms, and the **[MI0BOT openHPSDR-Thetis
> fork](https://github.com/mi0bot/OpenHPSDR-Thetis)** (Reid MI0BOT/GI8TME,
> building on Richie Samphire ramdor's upstream maintained at
> https://github.com/ramdor/Thetis) for the TX baseline ChannelMaster
> architecture.  Both upstream projects are GPL v3+; per-file copyright
> headers are preserved in every ported file.  See [NOTICE.md](NOTICE.md)
> and [CREDITS.md](CREDITS.md) for full attribution.

> ## ⚠️ Platform support — Windows only (for now)
>
> **Lyra builds and runs on Windows only at this time.** Linux and macOS
> support is **planned but not yet implemented** — the source will *not* build
> or run cleanly on those platforms today. The bundled DSP engine ships as
> Windows binaries (`wdsp.dll` + FFTW/RNNoise/SpecBleach DLLs) and the wire
> layer still uses Windows-specific socket/timer code, so a Linux/macOS build
> will fail. **Please don't try to build or run Lyra on Linux or macOS yet** —
> the errors you'll hit are expected, not bugs to report. There is also no
> sound-device selection on those platforms because the audio path isn't wired
> for them. The native Windows installer (see the
> [Releases](https://github.com/N8SDR1/Lyra-SDR-cpp/releases) page) is the
> only supported way to run Lyra right now.
>
> Linux and macOS will come **after** RX/TX is stable on Windows — the porting
> plan is captured in [docs/CROSS_PLATFORM.md](docs/CROSS_PLATFORM.md).

## Architecture

| Layer            | Choice                                                                                              |
|------------------|-----------------------------------------------------------------------------------------------------|
| Language         | C++23                                                                                               |
| UI framework     | Qt 6 (Qt Quick / QML) — Lyra-native                                                                 |
| Graphics         | Qt RHI — Direct3D 12 / Vulkan / OpenGL fallback on Windows (Metal/macOS auto-selects once the port lands) |
| RX DSP           | WDSP DSP engine (Warren Pratt NR0V, GPL v3+) — loaded natively                                      |
| RX architecture  | Lyra-native (no Python, no GIL) + Lyra-native flair (NPE modes, AEPF, captured-profile IQ-domain NR, per-band bounds memory, EiBi overlay, NCDXF beacon follow, ...) |
| **TX baseline**  | **Ported from [openHPSDR Thetis (MI0BOT fork)](https://github.com/mi0bot/OpenHPSDR-Thetis) ChannelMaster (cmaster, aamix, ilv, supporting modules) — GPL v3+, attributed per file** |
| **TX DSP additions** | **Lyra-native** (Combinator multiband compressor, Plate Reverb, parametric EQ, formant boost, sibilance enhance, DX cut-through, de-esser, auto-AGC — sit BEFORE the ported TXA chain) |
| FFT              | FFTW3 (per-host wisdom-cached on first launch)                                                      |
| Wire I/O         | Native UDP (`QUdpSocket`) on dedicated OS threads — no GIL                                          |
| Threading        | `std::jthread` + Qt thread pools, OS-priority + MMCSS later                                         |
| Build            | CMake 3.21+                                                                                         |
| Compiler         | MSVC v143 (VS 2022/2026), **Windows only today** — Linux clang/gcc + macOS Apple Clang are planned, not yet buildable (see [docs/CROSS_PLATFORM.md](docs/CROSS_PLATFORM.md)) |
| License          | GPL v3+ (matches both upstream projects)                                                            |

**No Python. No GIL. No cffi-on-the-wire-path. No in-process bottleneck.**

## Features (v0.12.3)

A full receive **and transmit** SDR transceiver for the Hermes Lite 2 / 2+,
native C++ end to end.  Lyra transmits every voice mode (SSB / AM / DSB /
SAM / FM) plus CW and digital via TCI, and ships a complete native TX audio
processing rack.  (Still on the roadmap: dual receiver / RX2 and PureSignal
— see below.)

* **Radio** — HPSDR Protocol 1 discovery (multi-NIC, dual limited +
  subnet-directed broadcast) + **Add by IP** unicast probe for fixed-IP /
  cross-subnet radios; live RX off the HL2/HL2+ on dedicated OS threads;
  multi-radio list (double-click to Open, connected radio marked);
  auto-connect to the last radio.  The installer adds the firewall rules so
  it connects without admin rights.
* **DSP** — WDSP RX chain: USB/LSB/CW/AM/FM/DIG modes, per-mode filters,
  AGC, NR, auto-notch, manual notches, squelch, an 8-band parametric RX EQ,
  a signature captured-noise reducer (NR-C), and centre-tune (CTUN) lock.
  Audio out the HL2 jack (AK4951) or PC sound card.
* **Frequency calibration** — a guided **Freq Cal** instrument (and a
  Settings tab) that measures your radio's oscillator error against a time
  station (WWV / WWVH / CHU) and nudges every RX/TX frequency so the dial
  reads true.  Pick a station, watch the error null to zero, Apply.
* **Transmit** — every voice mode: SSB, AM, DSB, SAM, FM (proper carrier +
  both sidebands on AM/SAM, suppressed-carrier DSB), plus digital via TCI /
  virtual audio cable.  TX power / drive, separate tune drive, AM carrier
  level, mic gain + boost, always-on ALC + operator Leveler.
* **CW** — internal iambic keyer (paddle / straight key on the HL2 KEY jack)
  plus a keyboard send line and a contest-grade **macro bank**: named
  click / F-key (F1–F12) memories, defaults + a "My macros" group you build
  out, `{tokens}` (contact + your own personal tokens) with a click-to-insert
  palette, and a Repeat caller.  QSK / semi / manual break-in, adjustable
  sidetone.  The meter shows forward power and the VFO goes red on-air
  while you send CW — console keyer *and* a paddle / key into the radio.
  An **external keyer / Winkeyer** works too (its KEY line into the HL2 jack,
  Iambic off), plus an optional **serial CW-key input** for a key wired to a
  COM-port pin.
* **VOX (voice-operated transmit)** — key up just by talking, with its own
  Settings tab: adjustable threshold, open-delay + hang times, and
  **anti-VOX** so your own receiver audio can't trip it.  Two live bar
  meters (mic level + anti-VOX) make it easy to set; voice-modes only, never
  overrides a manual/foot-switch key, and an instant kill.  PureSignal-safe
  (read-only taps, keying decision only).
* **Native TX DSP rack** — a studio audio chain built into the radio, ahead
  of the modulator: an 8-band parametric EQ with a draggable curve + live
  RTA, a multi-stage Speech section (noise gate, auto-AGC, de-esser), a
  5-band Combinator multiband compressor, and a Plate reverb for ESSB air.
* **TX profiles & metering** — save/recall the whole transmit chain as named
  profiles; multimeter (PO / SWR / MIC / COMP / ALC / PA current); hot-mic
  monitor (HL2 jack, a PC device, or over TCI).  A profile can optionally
  **launch its companion digital-mode app** (VarAC / MSHV / WSJT-X) when you
  pick it — a per-PC binding that's never part of the shared profile.
* **Per-band TX power (PA Gain tab)** — a per-band **PA Gain** table that
  calibrates what your drive % means in real watts on each band (measure
  each band into a dummy load and nudge its number), plus an **auto-tuning
  watts-output cap** to protect a low-drive amp: key TUN on a band and Lyra
  walks the power up *from below* and parks that band on the highest drive
  step that stays *under* your cap (live green/red "Cap tuned" marks), then
  holds it in SSB without chasing voice peaks.  Never overshoots — safe for
  an SS amp.  The on-screen watts are calibrated per band so the meter, the
  cap, and your external watt-meter all agree.  PureSignal-safe.
* **TX safety** — ATT-on-TX RX-front-end protection, TR-sequencing for amp
  hot-switch safety, an operator TX time-out, and a hard External TX Inhibit
  lockout (for sharing the antenna/bench with sensitive gear).
* **Waterfall callsign ID** — an optional courtesy ID that paints your
  callsign as a readable image in the SSB passband (USB/LSB), armed from the
  header — a visual hello on the band (not a replacement for legal ID).  It's
  now **locked to amateur bands** for your region — it can't be armed or
  fired outside a ham allocation (no rastering your call into a broadcast or
  CB segment).
* **Tuner memory (manual-ATU reminder)** — a built-in tuning-memory panel
  for manual antenna tuners: three named antennas, each with a
  frequency-keyed Input / Output / Inductor table, live colour-coded SWR,
  and a nearest-point view that brackets your dial (the saved settings just
  above and below where you're tuned) so you start a re-tune from the right
  place instead of from scratch — no third-party app, no paper sheets.
* **Panadapter + waterfall** — Vulkan/RHI scene-graph spectrum with
  glassy fill/glow, peak-hold markers, noise-floor line, palettes; click/
  drag/wheel tuning; draggable RX passband; collapsible waterfall.
* **Session recorder** — capture a whole listening session to a timestamped
  folder: the receive audio to WAV plus timed panadapter/waterfall snapshots,
  driven from a header **Recorder** chip that turns into a live REC timer.
  A low-priority offline **Convert to MP4** (uses ffmpeg if present) turns a
  session into one video — the snapshots play as a slideshow synced to the
  audio — with a Settings → Recording tab for the folder, snapshot rate,
  auto-split, storage cap, and a Convert / Open / Rename / Delete sessions
  list.  Recording ham radio you receive for personal use is normal; get the
  other operators' OK before publishing.
* **Dockable UI** — drag a panel by its title bar and a cyan zone previews
  where it lands: snap to an edge, split a neighbour, tab behind it, or
  float it free.  Cyan-on-hover resize separators; **four named layout
  slots** + a factory default; **Lock panels** freezes move *and* resize
  and survives a restart.  Dropped a panel in the wrong spot?  **View →
  Layouts → Undo layout change** walks the last one or two moves back where
  they came from.
* **Backup & Restore** — a dedicated **Settings → Backup & Restore** tab:
  export your whole config to a `.lyra` file, or **selectively restore** just
  the section you broke (Radio / Audio / TX / CW / Display / Layout / … — tick
  what to bring back).  Lyra also keeps **automatic dated snapshots** every so
  many launches (adjustable), stored on disk so they **survive an
  uninstall/reinstall** — a fresh install can restore your old setup from the
  snapshot folder.  Machine-specific keys (IP, graphics backend) never travel.
  Its **Maintenance & recovery** tools also rebuild the one-time FFT
  optimization (after a CPU change) and, when all else fails, **reset every
  setting to factory defaults** — a safety snapshot is taken first so it's
  undoable.
* **Crash-safe graphics** — Lyra picks the best graphics backend for your GPU
  (Vulkan / Direct3D / OpenGL, at the bottom of Settings → Visuals).  If a
  launch ever dies during start-up (a flaky GPU driver), the **next** launch
  automatically drops to a safer backend so you're never stuck editing the
  registry to get back in.
* **Band switching + per-band memory** — three rows (Ham / BC / Gen),
  returns to each band's last frequency, mode, and panadapter/waterfall dB
  ranges; optional 11m/CB band with all-40-channel panadapter markers
  (channel numbers + the fun nicknames — Super Bowl, Emergency, Highway…).
* **GEN / TIME / Memory** — three general-coverage slots, an HF
  time-station cycle (WWV/WWVH/CHU/BPM/RWM/…, ordered by your callsign),
  and a 20-slot frequency memory bank with CSV import/export.
* **EiBi shortwave overlay** — the EiBi broadcast schedule on the
  panadapter, on-air-now aware (cyan = live, grey = scheduled), click to
  tune in AM; in-app download or manual CSV import.
* **Band-plan overlay** — per-region (US / IARU R1 / R3) sub-band
  segments, band-edge warnings, digital + NCDXF beacon markers with
  click-to-tune and live beacon-station tooltips; out-of-band advisory.
* **TCI server** — TCI v1.9/2.0 (Expert Electronics) over WebSocket:
  drive frequency / mode / filter / volume from SDRLogger+, Log4OM,
  N1MM, etc.; a header indicator shows connected-client count; optional
  audio + IQ binary streaming and Thetis-style emulation/option toggles.
* **Kenwood CAT + serial PTT** — control Lyra from any logger / digital app
  that speaks **Kenwood TS-480 / TS-2000 CAT** (frequency / mode / PTT), over
  a **COM port** *or* **TCP** (no com0com needed for TCP); two independent,
  labelled CAT instances run side-by-side.  Plus a **serial-PTT input** so a
  digital app (WSJT-X, VarAC, fldigi) keys Lyra over an RTS/DTR line.  All
  under **Settings → CAT / Serial**, which now auto-lists **virtual COM
  ports** (com0com, VSPManager) the way the reference does.
* **DX-cluster spots** — TCI cluster spots painted on the panadapter
  (country-tagged, CW-pitch-aware placement), with show/max/lifetime
  controls, own-callsign highlight + color, a toast when you're spotted
  (on/off + re-notify cooldown), and a Clear button.
* **Solar / propagation panel** — HamQSL SFI/A/K + 10-band day/night
  heat-map, plus NCDXF beacon auto-follow.
* **Weather alerts** — lightning / wind / severe-storm badges from
  Blitzortung / NWS / Ambient / Ecowitt.
* **Quality-of-life** — header clocks with NTP drift check, GitHub
  update notifications, in-app diagnostic log viewer (no console window),
  operator/station identity + band-plan region in Settings.

Every feature landed gated by an operator HL2 bench test.

## Download & run (Windows)

Just want to operate? You don't need to build anything.

1. Go to the **[Releases page](https://github.com/N8SDR1/Lyra-SDR-cpp/releases)**.
2. Download the latest **`Lyra-Setup-X.Y.Z.exe`** installer and run it.
3. Launch Lyra. First start does a one-time FFT optimization (cached per
   machine), then auto-discovers your HL2 / HL2+ on the network.

Requires Windows 10 (1809+) or 11, 64-bit — see **System requirements**
below. The build-from-source instructions further down are only needed if
you want to develop or build it yourself.

## System requirements

Lyra is a native **64-bit Windows** app. Its panadapter and waterfall render
on the GPU, so graphics capability matters more here than in a typical desktop
app — but it runs comfortably on modest hardware, and integrated graphics are
fine.

| Component | Minimum | Recommended | Maxed+ |
|---|---|---|---|
| **OS** | Windows 10 64-bit, **v1809 (build 17763)**+; or Windows 11 | Windows 11 / Win10 22H2 | Windows 11, current |
| **CPU — Intel** | 4th-gen Core (Haswell) or newer, e.g. i3-4000 | Core i5 / i7, 10th-gen+ | Core i7 / i9, 12th-gen+ |
| **CPU — AMD** | Any Ryzen, or AVX-capable FX/A-series | Ryzen 5 / 7, 3000-series+ | Ryzen 7 / 9, 5000-series+ |
| **Cores** | Dual (quad preferred) | Quad+ with AVX2 | High-core-count, AVX2 |
| **RAM** | 4 GB (8 GB comfortable) | 16 GB | 32 GB |
| **GPU** | DirectX 11 / OpenGL 3.3 (integrated OK) | **Vulkan 1.1-capable** | **Vulkan 1.3-capable** dedicated, ample VRAM |
| &nbsp;&nbsp;NVIDIA | GeForce GT 700-series+ | GTX 10-series / RTX+ | RTX 30/40-series |
| &nbsp;&nbsp;AMD | Radeon HD 7000 (GCN)+, incl. iGPU | RX 400/500 / Vega / RX 5000–9000 | RX 6000/7000/9000 (RDNA 2/3/4) |
| &nbsp;&nbsp;Intel | HD 4000 (Ivy Bridge)+ / UHD / Iris | UHD 620 / Iris Xe / Arc | Arc A-series+ |
| **Storage** | ~300 MB free (SSD recommended) | SSD, ~500 MB free | NVMe Gen4 SSD |
| **Network** | 100 Mbit **wired** to the radio | Gigabit, dedicated NIC ideal | Dedicated 1/2.5 GbE, wired to the radio |
| **Display** | 1600 × 900 | 1920 × 1080 | Dual/triple; 1440p or 4K |

**About the display minimum.** Lyra's front panel is a set of dockable panels,
and they need real estate. At **1600 × 900** you get the panels you need to
operate — **Panadapter + waterfall, Tuning, Band, Filters, Audio, Meter and
TX** — laid out with everything reachable. That's what Lyra ships arranged for
at that size, and it's a genuinely usable radio.

The other panels — **Solar / Propagation, Display, and Profiles** — are closed
by default at that size and available any time from the **View** menu, but
opening them will want more screen. **Solar / Propagation** in particular is a
wide strip and needs about 780 px of width on its own. At **1920 × 1080** you
can have everything open at once with room to spare.

Height is what actually binds: below about 900 px there isn't room for the
panadapter *and* the operating panels without something being cut off, so 1366 ×
768 is not enough — even for the minimal set.

Lyra draws the spectrum and waterfall on the GPU. It works on any DirectX 11 /
OpenGL-class card, but the **Vulkan** path is smoother, lower-CPU, and
vendor-neutral (NVIDIA, AMD, and Intel alike) — that's why the higher tiers
call for a Vulkan-capable GPU. The extra headroom in **Maxed+** isn't about
*whether* it runs (Recommended is plenty) — it's about running everything at
once (dual RX, the full TX rack, spots, high-refresh waterfalls on a big
screen) with margin to spare.

Two things that matter on any tier: a **wired** connection to the Hermes Lite
is the single biggest factor in glitch-free audio (Wi-Fi in the radio path is
the top cause of pops/dropouts), and the one-time first-run FFT optimization
finishes quicker on a faster CPU.

## Getting help & reporting bugs

Lyra is in active development — testers and bug reports are welcome.
See **[KNOWN_ISSUES.md](KNOWN_ISSUES.md)** first for current rough edges and
what hasn't been built yet (so you don't report a planned feature as a bug).

* **Discord:** <https://discord.gg/BwjsQvjcSc> — questions, feature ideas,
  and quick help.
* **Bug reports:** open a
  **[GitHub issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)** (or post
  in Discord). To make a report actionable, please include:
  * Lyra version (Help → About, or the installer filename),
  * Windows version, and whether your radio is an **HL2 or HL2+**,
  * what you did and what happened (steps to reproduce),
  * the in-app **diagnostic log** (in Lyra, open the diagnostic log viewer
    and copy the relevant lines — no console window needed).

## Prerequisites (Windows)

* **Visual Studio 2022 or 2026 Community** with the "Desktop development
  with C++" workload + **MSVC v143** toolchain + Windows 11 SDK + CMake.
* **Qt 6.11.1** installed at `C:\Qt\6.11.1\` with the **MSVC 2022 64-bit**
  binding (add via Qt's `MaintenanceTool.exe` if the curated installer
  only pulled MinGW).

## Build (Windows command-line)

From an **x64 Native Tools Command Prompt for VS 2026**:

```bat
cd Y:\Claude local\SDRProject\lyra-cpp
cmake -B build -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
cmake --build build
build\lyra.exe
```

Or **Visual Studio 2026**: File -> Open -> CMake -> point at `CMakeLists.txt`
in this directory.  VS auto-configures, then F5 to build + run.

Or **Qt Creator**: File -> Open File or Project -> pick `CMakeLists.txt`,
let it configure, hit the green Run button.

## License

GPL v3 or later — matches WDSP, openHPSDR Thetis, and the parent
[Lyra](../lyra) Python project.  See [LICENSE](LICENSE) for the full
license text, [NOTICE.md](NOTICE.md) for upstream-project attribution
(WDSP + Thetis), and [CREDITS.md](CREDITS.md) for the full
acknowledgments and contributor list.

### Why this matters

Two open-source projects make Lyra-cpp possible without years of
ground-up implementation:

- **[WDSP](https://github.com/TAPR/OpenHPSDR-wdsp)** — Dr. Warren
  Pratt NR0V's DSP engine.  RX audio chain (NR, AGC, ANF, LMS, NB,
  AEPF, bandpass, demod, panel pan), TX audio chain (ALC, leveler,
  compressor, CFC, PHROT), and the planned PureSignal port (calcc +
  iqc).  Lyra-cpp links the bundled WDSP DLL and calls into
  it from native C++; the bundled DLL itself carries WDSP's GPL v3+
  license and copyright.

- **[openHPSDR Thetis (MI0BOT fork)](https://github.com/mi0bot/OpenHPSDR-Thetis)**
  — the HL2-focused fork of Richie Samphire (ramdor)'s
  [upstream Thetis](https://github.com/ramdor/Thetis), maintained by
  Reid (MI0BOT / GI8TME) with the HL2/HL2+ specific work that makes
  the Hermes Lite 2 family first-class.  Lyra-cpp's TX baseline ports
  the Thetis ChannelMaster layer (cmaster.c + aamix.c + ilv.c +
  supporting modules) with full per-file GPL attribution.  This
  brings tested, debugged, multi-radio (HL2/HL2+/ANAN/Orion/...) TX
  dispatch into Lyra-cpp; Lyra-native TX DSP enhancements layer on
  top.

The Python Lyra (`../lyra/`) and Lyra-cpp are both N8SDR's projects;
Lyra-cpp is the C++23 rebuild that makes hard-realtime audio + wire
I/O practical on Windows / macOS / Linux without Python's GIL.

Cross-platform support (macOS / Linux) is planned once the transmit
feature set settles; Qt 6 + modern C++ stdlib handles most of the cross-platform
lifting, with platform-shim modules for Win32-specific items (MMCSS
thread priority, WSAEventSelect → epoll/kqueue, etc.).

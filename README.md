# Lyra

**v0.25.42 Aladfar** — native **C++23 / Qt 6** SDR transceiver for HPSDR radios.

**Windows 10 / 11 x64 only** for the published installer. Dual RX1/RX2 panadapters and waterfalls, independent **SUB / RX2**, **Diversity** (dual-ADC phase/gain combine), **PureSignal** + Amp View, **WDSP 2.11**, built-in **CW decode**, native CW TX/sidetone, **APF** / **BIN**, and a studio TX rack. **ASIO for VAC is in test** (opt-in, default off).

**🌐 [Website & download →](https://n8sdr1.github.io/Lyra-SDR-cpp/)** ·
[Latest installer](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest) ·
[Discord](https://discord.gg/BwjsQvjcSc) ·
[Facebook](https://www.facebook.com/groups/1994335921523916) ·
[Wiki](https://github.com/N8SDR1/Lyra-SDR-cpp/wiki)

Download **`Lyra-Setup-0.25.42.exe`** from
[GitHub Releases](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest)
or the [landing page](https://n8sdr1.github.io/Lyra-SDR-cpp/).
Install over Sulafat; the app identity stays **Lyra-cpp** (settings stay).
**v0.25.41** is the previous public build. **v0.25.22** remains the long
return point. If you come from ≤0.25.31, **turn Drive down and walk it up
before your first TX on a Protocol 2 radio**.

GPL v3 or later. Attribution: [NOTICE.md](NOTICE.md) · [CREDITS.md](CREDITS.md).

---

## What it is (v0.25.42)

A single-window, dockable Qt 6 console — not Thetis tear-off skins. GPU
panadapters (Vulkan / Direct3D 11; OpenGL fallback) are heavier on a weak
iGPU. HPSDR I/Q and audio stay native C++ (no Python, no GIL).

| | Shipped in Aladfar |
|---|---|
| **OS** | **Windows 10 (1809+) / Windows 11, 64-bit** installer only |
| **Protocol 1** | Hermes Lite 2 / 2+ (Open). Brick / ANAN on P1 **Discover but do not Open** |
| **Protocol 2** | BrickSDR2, Brick3SDR (beta), ANAN-10/10E/100-series/200D, 7000DLE / 8000DLE / Orion MkII / Anvelina-Pro3 — pick the marketed model in Settings → Hardware |
| **G2 / Saturn** | Protocol 2 **wired**; TX is **dummy-load arm** until on-air validated — not battle-tested like Thetis |
| **RX display** | Dual panadapters + waterfalls for RX1 and RX2 on two-converter radios; overlay + same-ADC SUB on one-converter boxes |
| **SUB / RX2** | Independent of **SPLIT**. Second DDC; own pane on Brick3 / 100D / 200D / 7000 / 8000 |
| **Diversity** | Dual-ADC **phase/gain** mix on one frequency (**DIV** chip). RX2 jack / Ext1, not ANT2. Not a second VFO. Drops while keyed or while PureSignal is live |
| **PureSignal** | HL2 / HL2+ coupler mod; Brick2 ADC0 pad; Brick3 rear **PS** jack; Amp View (Snap / Hold). Attestation default off |
| **DSP** | WDSP 2.11 receive (NR 1–4 **or** NNR, with CW models in CW modes, ANF, LMS, NB, AGC, squelch) plus IQ-domain captured-noise **NR-C** |
| **TX rack** | 8-band EQ, Combinator, plate reverb, speech, VOX, profiles, ALC + Leveler |
| **CW** | Native TX + sidetone + keyer/macros; on-screen **Classic** (fldigi) and **DeepFist** (N9BC) decode; **APF** peaking; **BIN** binaural |
| **VAC / ASIO** | VAC1 / VAC2 for digital apps. **ASIO for VAC is in test** — Settings → Audio → Enable ASIO devices for VAC, restart; default **off**. Not Thetis **CMASIO**. Not a WING used as the radio mixer |
| **Remote** | **Lyra Remote** is a **LAN** browser desk (separate download). Not Thetis-style WAN remote. Do not port-forward TCI to the internet |

Full checklist: **[Feature Status](https://github.com/N8SDR1/Lyra-SDR-cpp/wiki/Feature-Status)** ·
operator manual: **[User Guide](https://github.com/N8SDR1/Lyra-SDR-cpp/wiki/User-Guide)** ·
this release: [`docs/releases/v0.25.42.md`](docs/releases/v0.25.42.md).

### Radios at a glance

| Radio | Open | Notes |
|---|---|---|
| Hermes Lite 2 / 2+ | ✅ P1 | SUB overlay (one ADC). PS with coupler mod. HL2+ codec jack |
| BrickSDR2 | ✅ P2 | SUB overlay (one ADC). PS on ADC0 pad |
| Brick3SDR | ✅ P2 (beta) | Dual pan + **Diversity**. PS rear jack. MAP ANT/RX |
| ANAN-10 / 10E | 🚧 P2 | Brick2-class TX. One ADC |
| ANAN-100 / 100B | 🚧 P2 | Brick3-class TX. One ADC |
| ANAN-100D / 200D | 🚧 P2 | Dual pan + Diversity |
| ANAN-7000DLE / 8000DLE / Orion MkII / Anvelina-Pro3 | ✅ P2 | Brick3-class TX (Enable PA + MOX + Drive). Dummy load on first TX |
| ANAN-G2 / G2-1K | 🚧 P2 | Saturn BPF. **Dummy-load arm** — wired, not on-air mature |
| Brick / ANAN still on **Protocol 1** FPGA | ❌ | Listed in Discover; **Open refused**. Flash P2 |

---

## Not yet / not this

Honest gaps — do not expect these in the published Windows installer:

- **Linux / macOS published builds** (macOS is a separate tester branch; Linux is planned)
- **Protocol 1 Open on ANAN or Brick** (HL2 TX layout would be wrong)
- **G2 / G2-1K on-air maturity** comparable to Thetis
- **WAN / internet remote** like Thetis remote
- **Multi-monitor Thetis tear-off skins** (Lyra docks and floats inside one Qt window)
- **CMASIO** and a **Behringer WING / X32 as the radio mixer** (48-channel ChannelMaster routing)

---

## Features (operator surface)

- **Radio** — P1 + P2 discovery (multi-NIC) + **Add by IP**; last-radio auto-connect; installer firewall rules.
- **Dual receive** — **SUB** on HL2, Brick, and P2 ANAN. Two-converter boxes get a **second panadapter/waterfall**. **SPLIT** TX on VFO B is independent of SUB.
- **Diversity** — two converters, same frequency, operator phase (−180…+180°) and gain (−27…+27 dB). Confirmed on Brick3; same path on 100D / 200D.
- **DSP** — USB/LSB/CW/AM/FM/DIG/SPEC, per-mode filters, AGC, NR or NNR, ANF, LMS, NB, squelch, 8-band RX EQ, **NR-C**, CTUN, RIT/XIT, **APF**, **BIN**.
- **Transmit** — SSB / AM / DSB / SAM / FM / CW; TCI + VAC digital; drive, tune drive, mic, ALC, Leveler; per-band PA Gain / watts cap.
- **PureSignal** — compact dock + **Amp View**; auto-att persists across PTT / per TX band.
- **CW** — iambic / straight / serial / keyboard / macros / QSK; sidetone; Classic + DeepFist decode (pitch lock in 0.25.38).
- **TX rack** — EQ + RTA, Combinator, plate reverb, speech, voice keyer, VOX, named profiles.
- **UI** — dockable panels, four layout slots, lock, GPU panafall (HiDPI ribbon in 0.25.39). Weak iGPU: prefer a safer backend in Settings → Visuals.
- **Station** — TCI, Kenwood CAT, serial PTT, USB-BCD, OC / Filters-BCD (ANAN/Brick3 lamps in 0.25.40), Xvtr (eight slots), tuner memory, Freq Cal, spots, EiBi, solar, recorder, backup/restore.
- **Lyra Remote** — LAN browser desk ([separate repo](https://github.com/N8SDR1/Lyra-Remote/releases/latest)). **Lyra to Lyra** is a laptop desk on the shack session (beta).

Every public feature is gated by bench on real hardware. Brick3 remains **beta testing**.

---

## ASIO for VAC (test stage)

Default **off**. Qt speaker **Out** and the HL2 jack are unchanged.

1. Settings → Audio → **Enable ASIO devices for VAC**, then **restart**.
2. VAC Driver = **ASIO**. Use the **same** driver for Input and Output (full duplex, 48 kHz).
3. Close any other program that owns that ASIO driver first.

If Lyra did not finish starting while loading ASIO, Enable is forced off.
This is **not** Thetis CMASIO and **not** parking RX/TX on a WING bus.
Steinberg ASIO SDK is included under its **GPLv3 option** — see [NOTICE.md](NOTICE.md).

---

## Download & run (Windows)

1. [Releases](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest) → **`Lyra-Setup-0.25.42.exe`**.
2. Run the installer, launch Lyra, click **▶ Start**, pick the radio.
3. First start runs a one-time FFT optimization (cached per machine).

Requires Windows 10 64-bit **v1809** (build 17763)+ or Windows 11.
**1600 × 900** is the operating minimum; **1920 × 1080** holds every panel.
Wired Ethernet to the radio. GPU panadapters: DirectX 11 / OpenGL 3.3 minimum;
Vulkan preferred. See [PC Requirements](https://github.com/N8SDR1/Lyra-SDR-cpp/wiki/PC-Requirements).

Build-from-source is only for developers (below).

---

## Getting help

See **[KNOWN_ISSUES.md](KNOWN_ISSUES.md)** before filing a duplicate.

- **Discord:** <https://discord.gg/BwjsQvjcSc>
- **Facebook:** <https://www.facebook.com/groups/1994335921523916>
- **Issues:** [github.com/N8SDR1/Lyra-SDR-cpp/issues](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)

Include Lyra version (Help → About), Windows version, radio model (HL2 / Brick / ANAN),
steps, and `%APPDATA%\N8SDR\Lyra-cpp\logs\lyra-log.txt`.

---

## Architecture (developers)

| Layer | Choice |
|---|---|
| Language | C++23 |
| UI | Qt 6 (Qt Quick / QML), dockable single window |
| Graphics | Qt RHI — Vulkan / Direct3D / OpenGL on Windows |
| RX DSP | WDSP 2.11 (Warren Pratt NR0V, GPL v3+), bundled DLL |
| TX baseline | Port of openHPSDR Thetis (MI0BOT fork) ChannelMaster — GPL v3+, per-file headers |
| TX extras | Lyra-native rack ahead of TXA (EQ, Combinator, reverb, speech) |
| Wire | Native UDP on OS threads |
| Build | CMake 3.21+, MSVC v143 (VS 2022/2026) |
| License | GPL v3+ |

Lyra-cpp is a ground-up native rewrite of the earlier Python Lyra. WDSP and
the ChannelMaster TX baseline are attributed in [NOTICE.md](NOTICE.md).

### Prerequisites

Visual Studio 2022 or 2026 Community (Desktop C++ / MSVC v143 / Windows 11 SDK / CMake)
and **Qt 6.11.1** MSVC 2022 64-bit.

From an **x64 Native Tools** prompt, in this repository:

```bat
cmake -B build -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
cmake --build build
build\lyra.exe
```

Linux and macOS source will not produce a supported public binary today.
Port notes: [docs/CROSS_PLATFORM.md](docs/CROSS_PLATFORM.md).

---

## License

GPL v3 or later — [LICENSE](LICENSE). Upstream: WDSP, openHPSDR Thetis
ChannelMaster, fldigi CW receive, Steinberg ASIO SDK (GPLv3 option).
See [NOTICE.md](NOTICE.md) and [CREDITS.md](CREDITS.md).

**Rick Langford (N8SDR)** — author. **Brent Crier (N9BC)** — co-contributor
(DeepFist). Testers and upstream authors are listed in CREDITS.

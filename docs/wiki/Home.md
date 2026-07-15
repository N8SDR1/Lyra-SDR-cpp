<div align="center">

<img src="https://n8sdr1.github.io/Lyra-SDR-cpp/logo.png" width="120" alt="Lyra logo">

# Lyra — Hermes Lite 2 / 2+ SDR Transceiver

**A native C++23 / Qt 6 SDR transceiver for the Hermes Lite 2 / 2+ — full receive *and* transmit, a GPU-accelerated panadapter, and a studio-grade TX audio rack. No Python, no GIL, nothing in the signal path but native code.**

[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%2F%2011-0078D6?logo=windows&logoColor=white)](PC-Requirements)
[![Latest release](https://img.shields.io/github/v/release/N8SDR1/Lyra-SDR-cpp?label=latest&color=2f81f7)](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/N8SDR1/Lyra-SDR-cpp/total?color=success)](https://github.com/N8SDR1/Lyra-SDR-cpp/releases)
![Built with](https://img.shields.io/badge/C%2B%2B23-Qt%206%20%C2%B7%20Vulkan-41CD52?logo=qt&logoColor=white)
![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)

<a href="https://n8sdr1.github.io/Lyra-SDR-cpp/"><img src="https://n8sdr1.github.io/Lyra-SDR-cpp/app-hero.jpg" width="920" alt="Lyra running live — panadapter, waterfall, tuning cluster, CW console and meters"></a>

<sub>Lyra running live — panadapter, waterfall, tuning cluster, CW console and meters. **[See the full tour →](https://n8sdr1.github.io/Lyra-SDR-cpp/)**</sub>

<sub>Shot on a <b>2560&nbsp;×&nbsp;1440</b> display with every panel open — you'll want a big screen to reproduce it. Lyra needs <b>1600&nbsp;×&nbsp;900</b> for the panels you operate with, and <b>1920&nbsp;×&nbsp;1080</b> to have them all open at once. <a href="PC-Requirements">Full PC requirements →</a></sub>

</div>

> ### ⬇️ [**Download the latest installer →**](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest)
> Grab **`Lyra-Setup-X.Y.Z.exe`**, run it, click **▶ Start**, pick your radio.
> The installer adds the Windows Firewall rules so Lyra connects **without
> administrator rights**.

**New here? Start with one of these:**
🚀 **[Quick Start](Quick-Start)**  ·  🎙️ **[First Voice Setup](First-Voice-Setup)**  ·  💻 **[PC Requirements](PC-Requirements)**  ·  📻 **[Supported Radios](Supported-Radios)**

---

## What Lyra is

A modern, native replacement for a classic HPSDR console — built from the
ground up in C++23 with a GPU-rendered spectrum and a full transmit chain that
would normally mean wiring an outboard studio rack in front of the radio.
Everything runs on the desktop, on the GPU where it helps, and over a plain
wired Ethernet link to the radio.

## Radio support at a glance

| Radio | Status |
|---|---|
| **Hermes Lite 2** | ✅ Supported |
| **Hermes Lite 2+ (AK4951)** | ✅ Supported |
| ANAN family (HPSDR Protocol 2) | 🗺️ Planned |
| Other HPSDR Protocol-1 boards | 🗺️ Planned *(needs a tester with the hardware)* |

Full detail on the **[Supported Radios](Supported-Radios)** page.

## Operating system

| OS | Status |
|---|---|
| **Windows 10 (64-bit, v1809+) / Windows 11** | ✅ Supported today |
| **Linux** | 🗺️ On the roadmap — a future version |
| **macOS** | 🗺️ On the roadmap — a future version |

## At a glance — what works now

✅ Full **receive** (all modes, the complete WDSP noise/filter toolkit, RX EQ,
captured-noise reduction, CTUN, RIT) · ✅ Full **transmit** — SSB / AM / SAM /
DSB / FM / CW plus digital over TCI & VAC · ✅ a **native TX audio rack**
(8-band EQ, multiband Combinator, plate reverb, speech processing, voice
keyer, VOX, TX profiles) · ✅ **CW** send + on-screen **CW decode** · ✅
**DX spots**, **frequency calibration**, **tuner memory**, **CAT / TCI /
VAC**, a **session recorder**, backup & restore, and a **dockable** Vulkan UI.

See the full checklist on **[Feature Status](Feature-Status)**.

## A studio rack, inside the radio

Lyra ships the kind of transmit audio chain you'd normally wire up as outboard
gear — all native, all in front of the WDSP transmitter:

| 8-band parametric EQ | 5-band Combinator | Speech processing |
|:---:|:---:|:---:|
| [![TX EQ](https://n8sdr1.github.io/Lyra-SDR-cpp/feat-eq.jpg)](https://n8sdr1.github.io/Lyra-SDR-cpp/) | [![TX Combinator](https://n8sdr1.github.io/Lyra-SDR-cpp/feat-combinator.jpg)](https://n8sdr1.github.io/Lyra-SDR-cpp/) | [![TX Speech rack](https://n8sdr1.github.io/Lyra-SDR-cpp/feat-speech.jpg)](https://n8sdr1.github.io/Lyra-SDR-cpp/) |
| Draggable response curve over a live analyzer | Multiband compression, X-Air-style | Noise gate, auto-AGC, de-esser, DX cut-through |

<sub>Screens from the [feature tour](https://n8sdr1.github.io/Lyra-SDR-cpp/). Plus plate reverb, voice keyer, VOX, and save/recall TX profiles — see **[Feature Status](Feature-Status)**.</sub>

## On the roadmap

🗺️ **RX2** dual receiver · 🗺️ **PureSignal** adaptive predistortion · 🗺️
**HPSDR Protocol 2 + ANAN** · 🗺️ **Linux, then macOS** · plus a 2-tone test
generator and a second virtual-audio channel (VAC2).

Details, ordering, and status on the **[Roadmap](Roadmap)**.

---

## Documentation

- 🚀 **[Quick Start](Quick-Start)** — box on the desk to first signal in five minutes.
- 🎙️ **[First Voice Setup](First-Voice-Setup)** — your first phone contact (SSB / AM / FM).
- 💻 **[PC Requirements](PC-Requirements)** — CPU / GPU / video spec, in detail.
- 📖 **[User Guide](User-Guide)** — the full operator's manual (also built into the app: **Help → User Guide**).
- 🔗 **[SDRLogger+ Combo](SDRLogger-Plus-Combo)** — the two-way link with the companion logger.
- ❓ **[FAQ &amp; Troubleshooting](FAQ-and-Troubleshooting)**

## Companion app — SDRLogger+

**[SDRLogger+](https://n8sdr1.github.io/SDRLoggerPlus/)** is Lyra's sister
application — a modern contest/DX logger by the same developer. Connect it to
Lyra over TCI and flip on the **[Combo link](SDRLogger-Plus-Combo)** and the two
work as one: copy a call once in Lyra's CW console and it lands in the log with a
callbook lookup, the operator's name comes back to your `{NAME}` macro, RST-Rcvd
auto-fills from Lyra's calibrated S-meter, and a `{LOG}` macro logs the QSO as you
send 73. → **[Get SDRLogger+](https://n8sdr1.github.io/SDRLoggerPlus/)**

## Links

- **[Releases](https://github.com/N8SDR1/Lyra-SDR-cpp/releases)** — installers + release notes
- **[Report a bug / request a feature](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)**
- **[README](https://github.com/N8SDR1/Lyra-SDR-cpp/blob/main/README.md)** — features + build from source
- **License:** GPL-3.0-or-later (WDSP-compatible) — see [NOTICE](https://github.com/N8SDR1/Lyra-SDR-cpp/blob/main/NOTICE.md)

---

*Lyra is built by Rick Langford (N8SDR) for the Hermes Lite 2 community. 73.*

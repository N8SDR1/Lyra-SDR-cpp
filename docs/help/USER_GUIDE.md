# Lyra — User Guide

A friendly, plain-language guide to operating Lyra. Written for hams,
not programmers — if you can click a menu, you can use this.

> **About this guide.** It grows feature-by-feature as Lyra is built.
> Each section describes what a control does, where to find it, and the
> sensible defaults — so nothing ships undocumented. This file is also
> the source for Lyra's in-app **Help** section.
>
> **Maintainers:** when a user-facing feature lands, add or update its
> section here in the same change. A feature isn't "done" until it's in
> this guide.

---

> **Tip — the "?" badge.** Every panel has a small cyan **?** at the
> far-right of its title bar (the panel's name is on the far left). Click
> it for a tiny menu: **Help guide** jumps this guide to that panel's
> section, **Settings…** opens the matching Settings tab. Quick way to
> learn or adjust any panel.

## Contents

- [Start here — Quick Basics](#start-here--quick-basics)
- [Why "Lyra"?](#why-lyra)
- [System requirements](#system-requirements)
- [Getting started](#getting-started)
- [The header (top toolbar)](#the-header-top-toolbar)
- [The status bar (bottom — HL2 telemetry)](#the-status-bar-bottom--hl2-telemetry)
- [Getting around the window](#getting-around-the-window)
- [The panadapter (spectrum display)](#the-panadapter-spectrum-display)
- [Tuning panel](#tuning-panel)
- [Filters panel](#filters-panel)
- [Band panel](#band-panel)
- [Audio panel](#audio-panel)
- [Setting up audio output](#setting-up-audio-output)
- [Setting up your mic input](#setting-up-your-mic-input)
- [First voice setup (SSB / AM / FM)](#first-voice-setup-ssb--am--fm)
- [Display panel](#display-panel)
- [Meter panel](#meter-panel)
- [TX panel](#tx-panel)
- [TX DSP rack (EQ + Speech + Combinator + Plating)](#tx-dsp-rack-eq--speech--combinator--plating)
- [Voice keyer & recording](#voice-keyer--recording)
- [Session recorder (RX audio + snapshots + MP4)](#session-recorder-rx-audio--snapshots--mp4)
  - [Settings → Recording](#settings--recording)
  - [Making an MP4 (the Convert button)](#making-an-mp4-the-convert-button)
  - [Is it legal to record?](#is-it-legal-to-record)
- [CW operating (paddle, keyboard, TCI)](#cw-operating-paddle-keyboard-tci)
  - [Reading CW — the RX decoder](#reading-cw--the-rx-decoder)
- [Tuner (manual ATU memory)](#tuner-manual-atu-memory)
- [Frequency calibration (WWV / time station)](#frequency-calibration-wwv--time-station)
- [Profiles (TX/RX chain presets)](#profiles-txrx-chain-presets)
- [Auto-starting apps at launch](#auto-starting-apps-at-launch)
- [Solar / Propagation panel](#solar--propagation-panel)
- [Weather alerts](#weather-alerts)
- [Updates](#updates)
- [Backing up & sharing your settings](#backing-up--sharing-your-settings)
- [Operating with an external amplifier (hot-switch protection)](#operating-with-an-external-amplifier-hot-switch-protection)
  - [SWR protection & TX power limiting](#swr-protection--tx-power-limiting)
- [Settings → Hardware](#settings--hardware)
  - [Operator / Station](#operator--station)
  - [Band plan (Region)](#band-plan-region)
  - [Diagnostics (debug log)](#diagnostics-debug-log)
  - [Getting help / reporting a bug](#getting-help--reporting-a-bug)
  - [Radio](#radio)
  - [Transmit (PA enable + safety timeout + hardware PTT)](#transmit-pa-enable--safety-timeout--hardware-ptt)
- [Settings → Filters / BCD](#settings--filters--bcd)
  - [Filter board — OC Control (J16 pins)](#filter-board--oc-control-j16-pins)
  - [Transmit pin action + external-PA gating](#transmit-pin-action--external-pa-gating)
  - [USB-BCD (linear-amp band switching)](#usb-bcd-linear-amp-band-switching)
- [Settings → Audio](#settings--audio)
  - [Virtual Audio Cable (VAC1)](#virtual-audio-cable-vac1)
- [Settings → DSP (filter type)](#settings--dsp-filter-type)
- [Settings → TX (Mic + ALC + Leveler + TR sequencing + cos² fade)](#settings--tx-mic--alc--leveler-tr-sequencing--cos-fade)
- [Settings → VOX (voice-operated transmit)](#settings--vox-voice-operated-transmit)
- [Settings → PA Gain (per-band power calibration + watts cap)](#settings--pa-gain)
- [Settings → Network (TCI)](#settings--network-tci)
  - [SDRLogger+ Combo link](#sdrlogger-combo-link)
  - [CW keying over TCI](#cw-keying-over-tci)
- [Settings → CAT / Serial (rig control over COM / TCP)](#settings--cat--serial-rig-control-over-com--tcp)
  - [Digital modes over TCI](#digital-modes-over-tci-ft8--ft4--msk144--q65--etc)
  - [Digital modes over VAC](#digital-modes-over-vac-virtual-audio-cable)
  - [DX-cluster spots](#dx-cluster-spots)
- [Settings → Visuals](#settings--visuals)
  - [Trace color](#trace-color)
  - [Spectrum fill](#spectrum-fill)
  - [Peak markers](#peak-markers)
  - [Noise-floor line](#noise-floor-line)
  - [Trace smoothing](#trace-smoothing)
  - [Peak glow](#peak-glow)
  - [Glass sheen](#glass-sheen)
  - [Tooltips](#tooltips)
  - [Gridline brightness](#gridline-brightness)
  - [Frame rate](#frame-rate)
  - [dB range (floor / ceiling)](#db-range-floor--ceiling)
  - [Waterfall dB range (RX and TX)](#waterfall-db-range-rx-and-tx)
  - [Watermark](#watermark)
  - [Meteors](#meteors)
  - [Graphics backend](#graphics-backend)
- [Settings → Weather](#settings--weather)
- [Credits and References](#credits-and-references)

---

## Start here — Quick Basics

Brand new to Lyra? This is the five-minute on-ramp — do these in order and
you'll be listening, and talking, fast. Every step is covered in more depth
in the sections that follow.

**Currently supported:** Lyra is a native **Windows** app — Windows 10
(64-bit, v1809+) or Windows 11, a DirectX 11 / OpenGL 3.3 GPU (built-in
graphics are fine), and a **wired Ethernet** link to a Hermes Lite 2 / 2+.
(See [System requirements](#system-requirements) for the full picture.)

1. **Wire it up 🔌** — connect the radio to your PC with an **Ethernet cable**
   (wired, not Wi-Fi) and power it on.
2. **Launch & connect 📡** — open Lyra; it lists any radio it finds.
   **Double-click your radio** to open it (or use **Add by IP** for a fixed
   address). *First launch spends a few minutes tuning DSP to your CPU — a
   one-time step; let it finish.*
3. **Start the radio ▶️** — click **▶ Start** in the top header.
4. **Hear a signal 🔊** — **Settings → Audio → Out**: pick **"HL2 audio jack
   (AK4951)"** (headphones in the radio) or your **PC speakers/headset**, turn
   the **Volume** up, then **click the panadapter/waterfall** to tune and set
   the mode (USB above 10 MHz, LSB below, or AM/FM).
5. **Talk 🎙️** — see [First voice setup (SSB / AM / FM)](#first-voice-setup-ssb--am--fm).

**If something's off:** no radio found → check the **wired** cable + power;
no audio → check the **Out** device and **Volume**; can't transmit → see
[First voice setup](#first-voice-setup-ssb--am--fm).

---

## Why "Lyra"?

The openHPSDR naming tradition leans Greek. The radio hardware itself is
**Hermes** (the messenger god). Thetis — the venerable reference PC
client — is named after the sea-nymph mother of Achilles. New projects in
this ecosystem have historically reached into the same mythology for
names that share the family.

**Lyra** continues that tradition:

- **Apollo's lyre** was the instrument that turned invisible vibrations
  in the air into music — which is essentially what an SDR does: it takes
  vibrations in the electromagnetic field, digitizes them, and turns them
  back into something you can hear.
- The **constellation Lyra** contains **Vega** — a star with a long
  history of amateur-radio significance (Vega is the zero point of the
  astronomical magnitude scale, and was the first star ever photographed).
  That's the lyre + Vega you'll find as the faint watermark on the
  panadapter.
- It's short, pronounceable in every language the HPSDR community uses,
  and stays well clear of any existing SDR software or hardware product
  name.

Lyra is built by **Rick Langford (N8SDR)**. This version is a ground-up
native **C++23 / Qt 6** rebuild (Qt Quick + Vulkan/RHI) of the original
Python Lyra — same spirit, no Python and no GIL anywhere. Licensed
**GPL v3 or later**, to align with the wider openHPSDR / WDSP ecosystem.

73 and good DX.

---

## System requirements

Lyra is a native **64-bit Windows** application. Its panadapter and waterfall
render on the GPU, so graphics capability matters more here than in a typical
desktop app — but Lyra runs comfortably on modest hardware, and integrated
graphics are fine.

| Component | Minimum | Recommended | Maxed+ |
|---|---|---|---|
| **Operating system** | Windows 10 64-bit, **v1809 (build 17763)** or newer; or Windows 11. (32-bit not supported.) | Windows 11, or Windows 10 22H2 | Windows 11, current release |
| **CPU (Intel)** | 4th-gen Core (Haswell, ~2013) or newer — e.g. Core i3-4000 series | Core i5 / i7, 10th-gen or newer | Core i7 / i9, 12th-gen or newer |
| **CPU (AMD)** | Any Ryzen, or an AVX-capable FX/A-series | Ryzen 5 / 7, 3000-series or newer | Ryzen 7 / 9, 5000-series or newer |
| **Cores** | Dual-core (quad preferred) | Quad-core+ with AVX2 | High-core-count with AVX2 |
| **Memory** | 4 GB (8 GB comfortable) | 16 GB | 32 GB |
| **Graphics** | DirectX 11 / OpenGL 3.3-capable (integrated OK) | **Vulkan 1.1-capable** | **Vulkan 1.3-capable** dedicated GPU with ample VRAM |
| &nbsp;&nbsp;• NVIDIA | GeForce GT 700-series or newer | GeForce GTX 10-series / RTX or newer | GeForce RTX 30/40-series |
| &nbsp;&nbsp;• AMD | Radeon HD 7000 (GCN) or newer, incl. integrated | Radeon RX 400/500 / Vega / RX 5000–9000 (RDNA) | Radeon RX 6000/7000/9000 (RDNA 2/3/4) |
| &nbsp;&nbsp;• Intel | HD Graphics 4000 (Ivy Bridge) or newer / UHD / Iris | UHD 620 / Iris Xe / Arc | Arc A-series or newer |
| **Storage** | ~300 MB free (SSD recommended) | SSD, ~500 MB free | NVMe Gen4 SSD |
| **Network** | 100 Mbit **wired** Ethernet to the radio | Gigabit Ethernet, ideally a dedicated NIC | Dedicated Gigabit / 2.5 GbE NIC, wired straight to the radio |
| **Display** | 1366 × 768 | 1920 × 1080 | Dual / triple monitor; 1440p or 4K |

**About the graphics tiers.** Lyra draws the spectrum and waterfall on the GPU.
It works on any DirectX 11 / OpenGL-class card (the Minimum tier), but the
**Vulkan** path is smoother, uses less CPU, and is vendor-neutral — it runs
equally well on NVIDIA, AMD, and Intel. That's why Recommended and Maxed+ call
for a Vulkan-capable GPU: it's what lets high-FPS waterfalls scale cleanly
across large or multiple monitors.

**What the higher tiers actually buy you.** Lyra runs fine on the Recommended
spec — the extra CPU/GPU headroom in **Maxed+** isn't about *whether* it runs,
it's about running *everything at once* without the machine breaking a sweat:
dual receivers, the full TX speech-processing rack, spots, high-refresh
spectrum and waterfall on a big screen, and comfortable margin for the heavier
DSP features on the roadmap.

**Two things that matter on any tier:**

- On first launch Lyra tunes its FFT math to your CPU and caches it — a
  one-time step that takes a few minutes (a faster CPU finishes sooner). You
  can rebuild it later from **Settings → Backup & Restore** after a CPU change.
- A **wired** connection to the Hermes Lite is the single biggest factor in
  glitch-free audio. Wi-Fi in the radio path is the most common cause of pops
  and dropouts.

---

## Getting started

### 1. Know your hardware

- **HL2** (plain) — a stock Hermes Lite 2 board. Received audio is
  decoded on the PC and played through whatever output you pick in
  **Settings → Audio**.
- **HL2+** — the HL2 base **plus** the AK4951 audio add-in board (and the
  updated HL2+ gateware). Adds on-board audio routing and a microphone
  input for future transmit. The HL2's own headphone jack is the default,
  lowest-latency audio path.

### 2. Network

The HL2 is a Layer-2 Ethernet device speaking HPSDR Protocol 1 over UDP
port 1024. It has to be reachable on your local network:

- **Direct Ethernet** PC ↔ HL2 — simplest, no switch needed.
- **Same LAN** through any gigabit switch — also fine.
- **Across routers** — not supported (discovery is broadcast-only).

The first time you run Lyra, allow it through **Windows Firewall** so it
can talk to the radio (inbound UDP 1024 for `lyra.exe`).

### 3. First contact

Connecting to the radio lives in **Settings → Hardware** — open Settings
(**Ctrl+,** or **File → Settings…**), pick the **Hardware** tab, and find
the **Radio** section. Or just use **▶ Start** on the toolbar:

1. Power on the HL2 / HL2+ and make sure it's on the same network.
2. Click **▶ Start** on the toolbar (or **Discover** → **Open** in
   Settings → Hardware → Radio). Lyra also auto-connects to the last radio
   you used, so it may already be running.
3. You should see a live spectrum and hear the band noise floor. **■ Stop**
   disconnects.

### 4. First-launch note (one-time)

The very first time Lyra runs (and after a settings reset), it tunes its
signal-processing math for your CPU — a one-time "FFT optimization". A
setup window appears with the Lyra logo, a few tips, and a progress bar;
it takes a few minutes once, then Lyra opens **automatically** and every
launch after is fast. **Please let it finish and don't close it** — the
main window opens on its own when it's done. (You can force a fresh
optimization later from **Settings → Backup & Restore → "Rebuild FFT
optimization on next start"**, e.g. after a CPU change.)

### 5. Tune and listen

- **Tuning panel** — set the **RX1** frequency on the LED readout, and pick
  the **mode** (USB/LSB/CW/…) and **step** right under the VFO; see
  [Tuning panel](#tuning-panel).
- **Filters panel** — set the **RX/TX filter bandwidth** and sample rate;
  see [Filters panel](#filters-panel).
- **Band panel** — jump between bands.
- **Audio panel** — the **DSP + AUDIO** strip: **Vol / MUTE**, the **AGC**
  cycle, and **Noise Reduction** (NR on/off + Mode 1–4 + AEPF + NPE).
  Choose the output
  device in **Settings → Audio**.
- **Panadapter** — click/drag/wheel to tune; drag the right edge to set
  the signal-strength scale. See [The panadapter](#the-panadapter-spectrum-display).

### Finding your version

The version is shown in the **title bar** and under **Help → About
Lyra…**. Include it when reporting a problem so it can be matched to the
exact build.

---

## The header (top toolbar)

The strip across the top, between the menu bar and the panels:

- **▶ Start / ■ Stop** — connect to the radio and start streaming, or stop
  it. On Start, Lyra connects to your saved radio (or scans and opens the
  first one found). The button is **green when stopped** (a click starts you)
  and **red when running** (a click stops you).
- **Connection status** — **green "Connected to …"** while streaming, **red
  "Disconnected"** when not, and **amber "Connecting…/Scanning…"** during a
  connect attempt.
- **DSP / Options chips** — labelled groups of toggle chips: **TX DSP:**
  (the mic-rack panels — EQ / Speech / Combinator / Plating), **RX DSP:**
  (RX EQ), and **Options:** — **CTUN** (centre-tune lock), **Tuner** (the
  manual-ATU [tuning memory](#tuner-manual-atu-memory)), **CW** / **CW Dec**
  (the CW console and the RX decoder), and **WF-ID** (arm the waterfall
  callsign ID). A lit chip means that panel is open, or that toggle is on.
- **● TCI** — the TCI-server indicator, just after the connection status.
  Green **● TCI: N** when one or more programs (logger, cluster, etc.) are
  connected, showing the client count; amber **● TCI** when the server is
  enabled but idle. Hidden when TCI is off. See
  [Settings → Network (TCI)](#settings--network-tci).
- **★ Spotted!** — appears just before the clocks when *your own callsign*
  is spotted on the DX cluster (if the toast/badge option is on). See
  [DX-cluster spots](#dx-cluster-spots).
- **⬆ vX.Y.Z** (only when present) — an update is available; click it to
  open the release page. See [Updates](#updates).
- **Clocks** — a **local** clock (amber) and a **UTC / Zulu** clock (cyan,
  ending in *Z*), centered in the header. They read straight off your PC
  clock. **Right-click either clock** to *Check clock drift now…* — Lyra
  asks a network time server how far off your PC clock is and tells you
  (under 1 sec = fine). This matters for the **NCDXF beacons**: their
  rotation is locked to UTC on 10-second slots, so a PC clock that's off by
  a few seconds makes the beacon markers / Follow track the *wrong* station.
  If your clock is off, a **⚠** appears on the UTC clock; use the same
  right-click menu's *Sync time (w32tm /resync)* to fix it (Windows).
- **Weather badges** — ⚡ lightning, 💨 wind, ⚠ severe — appear toward the
  right edge when an alert is active and **flash** on the most serious
  tier. Hidden when all-clear. See [Weather alerts](#weather-alerts).

---

## The status bar (bottom — HL2 telemetry)

The thin strip across the bottom of the main window. While a stream
is connected, it shows live **HL2 telemetry** read from the EP6
status rotation:

```
HL2:  T 24.7°C    V 12.3 V    PA 0.00 A
```

- **T** — board temperature in °C, sampled every ~400 ms.
- **V** — HL2 supply voltage in volts (a normal HL2 / HL2+ reads
  **12.3 – 13.0 V**; well below ~11 V suggests the supply is sagging
  under load).
- **PA** — combined PA drain current in amps. The key telemetry
  during transmit:
  - **≈ 0 A** at rest (PA bias disabled, gateware idle).
  - **~0.2 A** at idle bias (PA Enable ON, MOX keyed, Drive = 0 %).
  - **~1.8 A** at full output on a bare HL2+ on the dummy load.
  - **Goes BOLD RED at ≥ 50 mA** — visual confirmation that the
    gateware is producing RF. If you see red and your wattmeter
    reads zero, something in the chain (relay, BPF, antenna) is
    eating the RF.

`n/a` in any field means the gateware hasn't delivered that slot
yet (briefly at startup) or the telemetry decode is mid-rotation.
A persistent `n/a` is a stream / gateware issue worth investigating.

> The PA-current readout is the **single most useful TX bench
> instrument** Lyra exposes. It's the gateware's own measurement,
> independent of any external watt-meter, and tells you whether
> the PA is actually biased + drawing current the way it should
> be for the drive you commanded.

---

## Getting around the window

Lyra's window is built from **dockable panels** — like a workbench you
arrange to taste. Lyra opens **full screen (maximized)** by default — the
way it's meant to be run — in a curated default layout (panadapter on top,
control panels in a row beneath).

- **Move a panel — drag it by its title bar.** As you drag, a **cyan
  highlight** previews where it will land:
  - against an **edge** of the window → docks along that edge;
  - over the **left/right/top/bottom third of another panel** → splits
    that panel and shares the space;
  - over the **centre of another panel** → stacks as a **tab** behind it;
  - **out on its own** (away from any panel) → floats as a free window.
  Drop when the highlight shows the spot you want. Double-click a title
  bar to toggle floating.
- **Resize panels:** drag the **separators** between them — they're the
  thin bars that brighten **cyan** when you hover. Drag the divider
  between the **panadapter and waterfall** the same way; the waterfall
  stays clean even squashed into a thin strip, and its position is
  remembered with your layout.
- **Show / hide panels:** the **View** menu lists every panel; tick or
  untick to show or hide it.
- **Lock the layout:** **View → Lock panels** (or **Ctrl+L**) freezes
  everything — panels can't be moved, floated, closed, **or resized** —
  so you can't disturb your arrangement by accident during operating.
  The move cursor disappears on locked title bars as a reminder.

**Undo a bad drag.** Dropped a panel somewhere you didn't mean to?
**View → Layouts → Undo layout change** puts it back where it came from.
It remembers the last few arrangements, so you can step back a couple of
times. (It starts fresh each launch and only tracks panel *moves* — it
won't re-open a panel you closed; use the **View** menu for that.)

**Named layouts.** **View → Layouts** gives you **four save-able slots**
plus the built-in **Lyra default** — five arrangements you can recall in
one click:

- **Save to a slot** — snapshots the current arrangement (panels *and*
  the spectrum/waterfall divider) into that slot; name it whatever you
  like.
- **Recall a slot** — snaps the window back to that arrangement.
- **Lyra default** — returns to the built-in factory arrangement. (Use
  this if a layout ever gets scrambled.)

Recalling a layout restores only your main panels — it won't pop open the
TX/RX DSP-rack or CW tool windows you reach from the header chips.

Your window size, layout, and divider position are remembered between
sessions (and survive a lock + restart). To move a layout to another PC
or keep a backup, see
[Backing up & sharing your settings](#backing-up--sharing-your-settings).

**Share a layout with someone.** **Settings → Backup & Restore → Share a
layout** has **Export layout…** / **Import layout…** buttons that save just
your panel arrangement to a small **`.lyralayout`** file. Post that file
(and a screenshot) on Discord or email it, and the other person imports it
in one click — their panels rearrange to match. Only the *arrangement*
travels: no settings, no radio address, and no window size, so a shared
layout drops cleanly onto any screen. (Importing applies it live; use
**Save current layout to** a slot afterward if you want to keep it.)

**Opening Settings:** **File → Settings…**, or press **Ctrl+,**.

---

## The panadapter (spectrum display)

The panadapter is the live picture of the radio spectrum — signals show
up as peaks rising out of the noise floor. Lyra draws it with a glassy,
glowing look that takes advantage of your graphics card.

**Tuning on the panadapter:**

- **Click** anywhere to tune RX1 there. Where it lands depends on the
  **Exact / 100 Hz** setting and the **Panafall step** on the
  [Display panel](#display-panel): in **Exact** the click snaps to the
  Panafall-step grid (1 Hz = truly exact), in **100 Hz** it rounds to the
  nearest 100 Hz.
- **Click + drag** left/right to pan across the band.
- **Mouse wheel** steps the frequency by the **Panafall step** (set on the
  Display panel). **Ctrl + wheel** zooms instead.
- A small **frequency readout** follows your cursor (toggle in
  Settings → Visuals).
- These gestures work on the **waterfall** below, too — **click** it to tune,
  **wheel** to step, **Ctrl + wheel** to zoom. It shares the spectrum's
  frequencies, so a click lands at the same spot either way.

**The RX filter passband** is shown as a translucent box over the tuned
signal. **Drag either edge** of the box to widen or narrow the receive
bandwidth — the **RX BW** readout in the Filters panel updates to
match (and is remembered per mode). The orange center line marks your
tuned carrier (offset into the passband on CW, where the tone sits).

**The dB scale (signal-strength scale) down each side.** The numbers on
the left and right edges show signal strength in dB. Adjust by **dragging
on the right-hand edge**:

| Where you drag (right edge) | What it does |
|---|---|
| **Top third** | Raises / lowers the **ceiling** (top of the scale) |
| **Bottom third** | Raises / lowers the **floor** (bottom of the scale) |
| **Middle third** | **Pans** the whole scale up or down together |

Dragging **up** always raises that edge. (Exact numbers live in
**Settings → Visuals → dB range**.)

**The frequency scale** runs along the bottom in MHz, centered on your
receive frequency. **Zoom** in/out from the [Display panel](#display-panel).

**Overlays you can turn on:** [peak markers](#peak-markers) (held signal
peaks) and a [noise-floor line](#noise-floor-line) — both configured in
Settings → Visuals, with quick controls on the Display panel; the
[EiBi shortwave schedule](#shortwave-broadcasters-eibi); and
[DX-cluster spots](#dx-cluster-spots) (from TCI, SpotHole or a DX-cluster
node).

---

## Tuning panel

Sets the **RX1 receive frequency** on a big amber **LED-style readout**
(megahertz · kilohertz · hertz), the way old Lyra showed it.

- **Click a digit** to select it (a cyan underline marks it), then **roll
  the mouse wheel** over the display to tune that digit's place up/down;
  the **arrow keys** also nudge it.
- **Roll the wheel** anywhere on the display to tune by the current
  **Step** (see below).
- **Double-click** the display to type an exact frequency.
The frequency, **Step**, and **Mode** form one bordered **VFO cluster**.
The border is **green while receiving** and turns **red on transmit**
(MOX/TUN); an amber **RX/TX tag** in the top-left corner marks the role
(it flips RX→TX on key). The Lyra logo sits centred to its right, with
**VFO B** to the right of that (it appears when SPLIT is on).

- **Step** — under the VFO, the wheel tune step: **1 Hz / 10 Hz / 100 Hz
  / 1 kHz / 5 kHz / 10 kHz** (default **1 kHz**).
- **Mode** — under the VFO: **LSB, USB, CWL, CWU, DSB, AM, FM, DIGU,
  DIGL**. (Mode lives with the VFO it applies to — moved here from the
  old Mode + Filter panel.)
- **CW Pitch** — on the centred row beneath the VFO, only in CW modes
  (CWU/CWL): your preferred sidetone / beat-note pitch, **200–1500 Hz**.
  The receive filter centers on this pitch and the tuned-carrier marker
  offsets to match, so a signal you zero-beat lands at your chosen tone.

**SPLIT — receive on VFO A, transmit on VFO B** (same band). The action
row beneath the VFOs has:

- **SPLIT** — toggles split on/off. When on, **VFO B** appears (the TX
  VFO — tune it like VFO A) and transmit moves to it; VFO A keeps
  receiving. On key, VFO B's border + tag go **red** while VFO A stays
  **green**. VFO B transmits in the **same mode** as VFO A.
- **1→2 / 2→1 / ⇄** — copy VFO A → B, copy B → A, or swap them.

**In FM**, the raw SPLIT button is replaced by a friendlier repeater
front-end (FM repeaters are the common split case):

- **Dev** — FM deviation, right there in the row (same control as
  Settings → TX → FM). Changing it auto-sizes the RX bandwidth to the
  matching FM channel width (±2.5 kHz → 12 kHz, ±5 kHz → 16 kHz).
- **Emph** — FM pre-emphasis, **Comm** (6 dB/oct voice) / **Off** (flat,
  for data). Same control as Settings → TX → FM; leave it on Comm for voice.
- **RPT** — the FM split toggle. Turn it on, then pick **Offset** direction
  (**−** / **+**) and amount (100 kHz / 500 kHz / 600 kHz / 1 MHz / 5 MHz)
  → VFO B = VFO A ± offset, split armed. As you tune VFO A, VFO B **tracks**
  the offset (duplex). **CTCSS** is a lit button → enable it and pick the
  **Tone** for tone-protected repeaters.

On the panadapter (centred on your RX, VFO A) the **RX** carrier is a
solid orange line at centre and the **TX (VFO B)** freq is a solid
**lime** line — **red while transmitting** — so you can see exactly where
you'll transmit before you key. (Split is the same mechanism FM repeater
offsets use; PureSignal tracks VFO B automatically.)

To jump between bands, use the **Band panel**; to change the filter
width, the **Filters panel**.

**RIT / XIT — incremental tuning.** Two lit toggle buttons on the action
row, each revealing a small **± offset** spin (±9.99 kHz, 10 Hz steps)
when engaged:

- **RIT** (Receiver Incremental Tuning) shifts **only the receiver** by
  the offset — chase a station that's drifted or zero-beat a CW signal
  without moving your VFO. The panadapter re-centres on where you're
  actually listening, and a pale-blue **DIAL** marker shows where your
  VFO still reads. Your transmit frequency is unaffected.
- **XIT** (Transmitter Incremental Tuning) shifts **only the transmit**
  frequency by the offset — RX stays put. In simplex a lime TX marker
  (red on key) shows where you'll transmit. PureSignal tracks the
  shifted TX automatically.

Both persist across sessions. The **± arrows** step 1 Hz, the **wheel**
steps 10 Hz (Shift = 100 Hz), and the **0** button clears the offset (or
just toggle off). Tip: RIT/XIT are session-style fine offsets — for a fixed
transmit split (a repeater, or working a DX pile-up up/down), use
**SPLIT** (or the FM **RPT** button) instead.

### Zero-beat markers — dead-tune a carrier

A tiny Kenwood-style **± needle** that appears under the frequency readout
and shows how far the strongest carrier in the passband sits from where
you're dialed — so you can **dead-tune ("zero-beat")** onto a CW, AM, SAM,
or FM signal **by eye** instead of by ear.

- **Turn it on** in **Settings → Visuals → Show zero-beat markers** (off by
  default). It then shows **only** in the carrier modes where it makes
  sense — **CWU, CWL, AM, SAM, FM** — and stays hidden in SSB / digital.
- **Read it** — the needle rides left/right of a centre detent over a
  **±500 Hz** span, with the offset printed beside it. Tune until the needle
  centres and the readout snaps to a green **● 0 Hz** — you're zero-beat.
  The colour cues how close you are: **amber** (far) → **cyan** (within
  ~45 Hz) → **green lock** (within 8 Hz). A grey **—** means no carrier is
  strong enough to trust yet (it briefly holds the last lock through CW
  keying gaps, then greys out).
- It sits in the gap just above the SPLIT button, so switching into a CW
  mode with it enabled never grows the Tuning panel nor nudges the
  frequency readout — the layout stays put.

**Also handy for the CW decoder.** In CW the needle reads the carrier's
offset from your **CW Pitch**, so centring it lands the beat note exactly
on the pitch the [RX CW decoder](#reading-cw--the-rx-decoder) listens at.
The decoder has **no AFC**, so a needle-centred signal is the quickest way
to put a station right on frequency for clean copy.

### CTUN — centre-tune lock

The **CTUN** chip (top toolbar, in the **Options** group) freezes the
panadapter / waterfall on its current
centre so you can tune the VFO **within** the displayed span without the
waterfall scrolling. Click it (it lights **green**) and the band stays put
while the dial marker slides to where you're listening — ideal for watching a
fixed slice of spectrum (a pile-up, a net, an FT8 window) and clicking
signals across it. Tune past the edge of the span and it re-centres on the
new dial automatically. Click again to release and return to normal
scroll-tuning. RX1; works in any receive mode.

---

## Filters panel

Sets the sample rate and how wide the RX/TX filters are. (The **mode**
picker moved to the Tuning panel, under the VFO.)

- **Rate** — the IQ sample rate / panadapter span: **96 k / 192 k /
  384 k**. Higher shows more spectrum at once (wider waterfall) for a bit
  more CPU/network.
- **RX BW** — the receive filter bandwidth. The list is **per-mode**
  (sensible presets for SSB, CW, AM, FM, digital, with SSB running up to
  10 kHz and AM up to 12 kHz for ESSB-style wide audio), and Lyra
  remembers the width you last used for each mode. If you **drag a
  passband edge** on the panadapter to a width that isn't a preset, the
  combo shows it as **"(custom)"** at the top of the list so the readout
  always matches what you're actually hearing; pick a preset to snap back.
- **🔗 (Lock)** — links RX and TX bandwidths so changes to either side
  mirror the other for the current mode. Click to toggle. Toggling it
  ON pulls the RX bandwidth into TX. With the lock OFF, RX and TX BW
  are independent per-mode. (Disabled in **FM** — there's no separate
  TX width to lock to; see TX BW below.)
- **TX BW** — the transmit filter bandwidth. Per-mode just like RX BW.
  For **SSB / digital** it's the high edge: the TX passband runs from
  the **Filter Low edge** (Settings → Audio, shared with RX) up to this
  high edge — e.g. Filter Low = 70 Hz and TX BW = 4 kHz gives a
  70-4000 Hz single sideband. For the **double-sideband modes (AM,
  DSB, SAM)** the value is the **total occupied width**: the TX
  filter is symmetric around the carrier at **±(TX BW / 2)**, so AM at
  TX BW = 6 kHz transmits ±3 kHz = 6 kHz total and lands exactly inside
  the on-screen filter markers (matching RX). Sign-codes correctly per
  mode (USB positive, LSB negative-and-swapped, double-sideband
  symmetric) inside the WDSP TXA chain. In **FM** the TX BW is **not
  editable** — it shows the auto-derived occupied width
  `2 × (deviation + 3 kHz)` (e.g. **"16 k (auto)"** at ±5 kHz), which
  updates live as you change the FM Deviation. The RX BW presets in FM
  are FM channel widths (8 / 10 / 12 / 16 kHz) and follow the deviation
  too (see the FM section).

SSB and digital filters open at the **Filter Low edge** (Settings → Audio
→ Filter Low edge, shared RX+TX, default 100 Hz); CW filters are
centered on your **CW Pitch**; AM/DSB/FM are symmetric around the
carrier (the Filter Low edge doesn't apply to those modes).

---

## Band panel

Quick band switching, in three rows:

- **Ham** — the HF/6m amateur bands (**160m … 6m**). Click one and Lyra
  returns RX1 to **the last frequency you were on in that band** (the
  band's default the first time). The button for the band you're on lights
  up (red-glow), following the frequency however you tune.
  An optional **11m** button (the **Citizens Band**, 26.965–27.405 MHz AM)
  appears at the end of this row, right after 6m, when you enable it in
  **Settings → Hardware → Band panel**. When you're tuned on it, the
  panadapter marks all **40 CB channels** (Ch1…Ch40) across the top —
  **click a channel marker to jump to it** in AM.
- **BC** — the shortwave **broadcast** meter bands (**120m … 13m**, all
  AM). Like the Ham bands, BC and the 11m band **remember their last
  frequency and mode** and return to it when you click them (band default
  the first time). The active band lights the same way.
- **Gen** — the GEN1/2/3 general-coverage slots (below).

**GEN1 / GEN2 / GEN3** (to the right of the band buttons) are
**general-coverage slots** for listening outside the ham bands —
shortwave, MW, time stations, utility. Each works like a band button but
for any frequency: click it to jump to that slot's last freq + mode, and
while it's the active slot (highlighted) **tuning around updates it**, so
it always remembers where you left off. Defaults are WWV 10 MHz, WWV
15 MHz, and 1 MHz (MW), all AM. **Right-click** a GEN button to reset it
to its default. Clicking an amateur band button leaves general-coverage.

**TIME — HF time stations.** Between GEN3 and Mem, the **TIME** button
hops through the world's shortwave time-and-frequency stations — handy for
checking propagation or setting your clock by ear:

- **Left-click TIME** to tune the next station/frequency. It cycles through
  the whole list — WWV (2.5/5/10/15/20/25 MHz), WWVH, CHU, BPM, RWM, and
  others — advancing one step each click, and picks up where you left off
  next time. The mode is set for you (AM, or **USB for CHU**).
- **Right-click TIME** for the full list grouped by station — pick any
  station and frequency directly, or **Reset cycle to first entry**.
- The stations are ordered to put the most useful ones first based on your
  callsign (a US operator leads with WWV, etc. — set your callsign in
  **Settings → Hardware**).

**Mem — frequency memory bank.** At the end of the GEN row, the **Mem**
button holds up to **20 saved frequencies**, each remembering its name,
frequency, mode, and (optionally) RX filter bandwidth:

- **Left-click Mem** to open the recall list — pick a preset and Lyra
  tunes straight to it (mode first, then frequency).
- **Right-click Mem** to **Save current frequency** (the VFO is stored with
  an auto-name like "14.074 USB") or open **Manage presets…**.
- Full editing lives in **Settings → Bands → Memory**: a table where you
  can rename, retype the frequency/mode/bandwidth, add notes, delete, clear
  all, and **import/export CSV** (columns: Name, Freq_Hz, Mode, RX_BW_Hz,
  Notes, Offset_Hz, CTCSS_Hz) to back up or share your list.
- **Repeaters:** set **Offset** (the TX shift in kHz — e.g. −100 for 10 m,
  −1000 = −1 MHz for 6 m) and **CTCSS** (access tone in Hz) on a row, store
  the **output** frequency in Freq, and Mode = FM. Recall it and Lyra tunes
  to the output, **arms SPLIT** to the input (output + offset), and sends
  the tone — one click onto the repeater. Blank Offset/CTCSS = a simplex
  preset (recall clears any split + tone). "Store current" while you're set
  up on a repeater captures its offset + tone too.

Pick the **mode** under the VFO (Tuning panel) and the **filter width**
in the **Filters** panel.

**Per-band memory.** Lyra remembers each band's settings independently and
restores them when you move to that band: the **last frequency** (recalled
by the band buttons), the **demod mode**, the **panadapter dB min/max**
(reference-level range), and the **waterfall dB min/max**. Set 40m to LSB with a tight waterfall range and 20m to a wider
one, and each comes back the way you left it as you hop between them — no
need to re-adjust every time. (Auto-scale on/off is global; RX filter
bandwidth is remembered per *mode*, as before.) Memory is saved live as you
adjust, and persists across sessions.

---

## Audio panel

The **DSP + AUDIO** panel — what you hear and how it's cleaned up. It's
laid out in old Lyra's three-row arrangement:

**Row 1 — Levels**
- **Vol** — output volume, shown in dB beside the slider (−∞ when fully
  down). The mouse wheel nudges it in fine steps.
- **MUTE** — silences or restores the audio without disturbing the Vol
  slider; the button reads **MUTED** while engaged. (Lyra starts
  **unmuted**.)
- **LNA** — RF input gain on the HL2's AD9866 PGA (−12…+31 dB; slider or
  mouse-wheel). Higher = more sensitivity; back off on strong bands to
  avoid ADC overload. The S-meter compensates for it automatically, so
  changing LNA doesn't shift the signal reading.
- **AF** — audio makeup gain (0…+40 dB), applied **before** Vol. Use it to
  set a comfortable working level for your headphones/speakers once, then
  ride **Vol** on top of it for moment-to-moment changes. The value shows
  in dB beside the slider.
- **Bal** — stereo balance: pans the audio left/right. Centre = both
  channels equal; the slider snaps to dead-centre near the middle so it's
  easy to recentre.
- **MON TX · Monitor** — *hear yourself transmit.* With **MON TX** on, while
  you're keyed up Lyra plays your own **post-rack** TX audio (Speech → EQ →
  Combinator → Plating) in place of the auto-muted receiver. The **Monitor**
  slider rides its level (dB beside it). It's your voice **through the DSP rack
  but before** the radio's corrective ALC and TX bandpass — so it shows what
  your *processing* is doing, not the exact transmitted envelope. MON TX does
  nothing on receive, and in **DIGU / DIGL / CW** (rack bypassed / no mic
  audio) there's nothing to monitor. The monitor follows wherever your RX audio
  goes — the **HL2 jack or selected output device** (the **Out** picker), and,
  when enabled, a **VAC** PC device **and** your **TCI** clients — all from the
  single MON TX toggle (matching the reference's monitor routing).
- **Auto · Out** — **Auto** rides the LNA on the ADC-overload edge (see
  **LNA** above). **Out** is a live output-device quick switch: click it for a
  pop-up list of the **HL2 audio jack** + your **PC output devices** (the
  current one marked), and pick one to move RX audio — and the monitor — there
  live. The full audio setup (host API, exclusive mode, VAC) still lives in
  **[Settings → Audio](#settings--audio)**.

**Row 2 — DSP toggles + AGC**
- A row of effect buttons **NB · BIN · NR · ANF · LMS · SQ · APF · NF**.
  **NR** (Noise Reduction), **ANF** (Auto Notch — nulls carriers/
  heterodynes automatically), **LMS** (Line Enhancer — lifts CW/tones out
  of the noise), **NF** (manual Notch Filter), **SQ** (all-mode squelch),
  **NB** (Noise Blanker), **APF** (Audio Peak Filter), and **BIN**
  (Binaural) are all live. NR and LMS are *complementary* — NR subtracts
  broadband noise, LMS predicts the periodic signal, so both can run
  together on weak CW.
- **BIN — binaural pseudo-stereo:** widens the soundstage on **headphones**
  by sending the signal to one ear and a 90°-phase-shifted copy to the
  other, so weak CW/SSB seems to lift out of the noise and voices gain a
  sense of space. A **depth** slider appears in the bottom row while BIN
  is on (0 = mono → 100 = full); loudness stays constant as you sweep it.
  (Headphones only — on speakers the effect collapses.)
- **APF — CW peaking:** a narrow peak parked on your CW pitch that lifts
  a CW tone out of the surrounding hiss. It engages only in **CWU/CWL**
  (no effect in SSB/AM/FM) and re-centres automatically when you change
  the CW pitch. A **gain** slider appears in the bottom row while APF is
  on — pick how hard it lifts (3–18 dB in 3 dB steps; default 12). Stacks
  well with LMS for weak-signal CW.
- **NB — noise blanker:** suppresses *impulse* noise (ignition, power-line
  arcing, lightning crashes) by gating it out of the raw IQ before demod.
  A **strength** slider appears in the bottom row while NB is on — higher
  = more aggressive blanking; back it off if it starts chewing CW/SSB
  transients. (For a steady carrier-type interferer use a manual notch or
  ANF instead — NB is for clicks/pops, not tones.)
- **SQ — all-mode squelch:** mutes the audio between transmissions and
  unmutes when a signal opens it. It routes automatically by mode — a
  voice-presence detector (SSQL) on SSB/CW/DIG, the FM noise squelch on
  FM, and the carrier squelch on AM/SAM/DSB. A **threshold** slider
  appears in the bottom row while SQ is on: higher = tighter (only
  stronger signals open it); the usual sweet spot is ~10–30.
- **NF — manual notches** (right-click the panadapter to use):
  - **Right-click empty spectrum** drops a notch at that frequency
    (200 Hz wide) and turns NF on.
  - **Drag** a red band to slide it onto the offending carrier.
  - **Mouse-wheel** over a band: down = wider, up = narrower.
  - **Right-click** a band to remove it.

  Each notch **visibly cuts the spectrum trace and the waterfall** in
  that region — the trace drops to the noise floor and the waterfall
  shows a dark stripe across the notch width, so you can see exactly
  what's being removed instead of guessing. The notches are deep and
  sharp by design — Lyra uses WDSP's NBP filter in a deep
  rectangular-window, 2048-tap configuration, so a carrier drops into the
  noise rather than just dipping. The **NF** button toggles all notches
  at once without losing their positions; the count shows beside the
  button. ANF (the auto-notch) likewise runs deep adaptive settings — a
  marked step up from the softer notching in the Python build.
- **AGC** — click anywhere on the AGC cell (it highlights on hover) to
  cycle **Off → Fast → Med → Slow**. Med is the everyday default; Fast for
  fast-changing signals, Slow for steady ones, Off for fixed gain (digital
  modes / measurement). The three modes are genuinely distinct — Fast
  recovers in ~50 ms with no hang, Med in ~250 ms, Slow holds ~1 s then
  decays over ~500 ms (industry-standard time constants). The cell shows the AGC
  **threshold** and the **live gain action** (dB) beside the mode.

**Row 3 — Noise Reduction character**
- **NR Mode (1–4)** — picks the WDSP denoiser's gain function:
  **1** Wiener + speech-presence, **2** plain Wiener (edgier), **3**
  MMSE-LSA (the smoothest, default), **4** trained-adaptive (most
  aggressive). Turn **NR** on in Row 2, then sweep modes to find the best
  sound for the band.
- **AEPF** — anti-musical-noise smoother. On (default) engages *both* of
  WDSP's cleanup stages — artifact elimination **and** the post-filter
  that stock WDSP leaves off by default — so the "musical twinkle" is
  knocked down hard while MMSE-LSA (Mode 3) keeps the voice natural
  rather than robotic.
  Turn it off to hear raw EMNR on already-quiet bands. If you still want
  the most natural voice, stay on **Mode 3** (Mode 4 trades smoothness for
  aggression and brings musical noise back).
- **NPE** — how the denoiser tracks the noise floor: **OSMS** (smooth,
  best for steady atmospheric hiss) or **MCRA** (faster-tracking, better
  for changing/intermittent QRM).
- **LMS strength** — appears in this row only while **LMS** is on: 0 is
  subtle, 50 is the WDSP-class default, 100 is full prediction (more taps,
  harder pull). Most useful digging weak CW out of band hiss.

Surfacing NR Mode + AEPF + NPE as separate knobs is one of Lyra's
differentiators — most SDR apps hide them. All three persist across
restarts.

### Captured noise profile (NR-C) — Lyra's signature noise reduction

This is the one that's genuinely different. Instead of a generic denoiser,
NR-C learns **your** band's actual noise spectrum and subtracts it — in the
IQ domain, *before* WDSP's chain — so it's mode-independent and doesn't fight
the AGC. Used well it drops the noise floor several dB and the signal pretty
much sits where it was.

**The workflow (left to right on Row 3):**

1. Tune to a **quiet, signal-free** spot on the band you're working — NR-C
   subtracts whatever it captures, so don't capture on top of a signal.
2. Pick a **capture length** (3 / 5 / 10 s) next to the **📷 Cap** button.
3. Press **📷 Cap**. The button shows a percentage while it averages the
   noise; press it again to cancel.
4. Press **Save**, give the profile a name (e.g. `40m-night`, `20m-ESSB`) in
   the box that pops up. It's now in the picker.
5. Flip **NR-C** on. The captured noise is subtracted from RX. The panadapter
   shows the cleaned spectrum too, so you can watch the floor drop.
6. Fine-tune with the **⚙** button (appears next to NR-C while it's on):
   - **Strength** (1–5×) — how hard to subtract. Higher = more cut.
   - **Floor** (−3 to −30 dB) — the deepest any bin is allowed to drop.
     More negative = more aggressive.
   - **Smoothing** (0–95%) — steadies the gain so it doesn't "twinkle."
     Raise it if deeper Strength/Floor starts to sound watery.

**FFT size** (2048 / 4096 / 8192) sets the resolution — 8192 resolves wide
ESSB noise finest (a touch more latency), 2048 is lowest latency. Changing it
clears the current profile (a profile is tied to the size it was captured at).

**Profiles are files you own.** Each saved profile is a small `.lnp` file in
`%LOCALAPPDATA%\N8SDR\Lyra-cpp\profiles\` — browsable, backup-able, and
shareable between rigs (drop a friend's `.lnp` in there and it shows up in
your picker). Profiles are tagged with their capture rate + FFT size and
won't load against a mismatched rate (you'll get a recapture hint).

**Manage them in Settings → Noise:** the full list with rate/size/date, plus
**Rename**, multi-select **Delete**, and **Open folder**.

**Auto-load on startup (Settings → Noise).** Pick a saved profile to load
automatically each launch, so NR-C comes up ready instead of an empty field.
If you left NR-C **on** last session it re-enables too — unless the chosen
profile no longer matches your sample rate, in which case it stays off with a
recapture hint. **CTUN** is remembered the same way (it comes back on if it was
on). Set the picker to **None**, or turn the toggles off before closing, for a
clean start.

Notes: NR-C adds about one FFT window of RX latency (≈21 ms at 4096) *only
while it's on*; off, the path is unchanged. It's independent of the WDSP
**NR** denoiser — you can run either, both, or neither.

---

## Setting up audio output

*RX listening — the HL2 jack vs a PC device, plus feeding digital apps.*

Lyra can send received audio out two ways — pick the one that matches how
your station is wired:

- **HL2 audio jack** — the headphone/speaker jack **on the Hermes Lite 2**
  itself (its onboard codec, the AK4951 on HL2+). *Recommended for most
  operators.*
- **PC sound device** — your computer's speakers, headset, or USB audio
  interface (or a virtual cable to another program).

You can switch any time; Lyra remembers your choice.

**Which one:**

- Headphones/speakers straight into the HL2 → **HL2 audio jack**.
- Listen on the PC's speakers / a USB headset → **PC sound device**.
- Feed RX audio to WSJT-X / FLDigi / a recorder → **PC sound device via
  VAC**, or **TCI** if the app speaks it (see *Digital modes* below).

The HL2 jack is the easy button: the radio and its codec share one clock,
so there's no drift to correct — glitch-free audio, lowest latency, zero
setup. The PC path bridges two clocks (the HL2's and your sound card's),
which Lyra handles with an adaptive resampler, but it's a little more to
get right.

### Where the controls are

On the **Audio panel**, top "LEVELS" row: **Out** is the output-device
quick-switch — click it for a drop-down of every output (the HL2 jack is
always first; the active one is marked). **Vol** is listening volume,
**AF** is pre-volume makeup gain (0…+40 dB, for when everything is too
quiet even at full Vol), **MUTE** silences without touching Vol, and
**Bal** is left/right balance. **Settings → Audio** has the same chooser
plus the Virtual Audio Cable (VAC1) options.

### HL2 audio jack (onboard codec / AK4951)

1. Plug headphones or powered speakers into the **PHONES jack on the
   Hermes Lite 2**.
2. Click **Out** on the Audio panel and choose **HL2 audio jack** (the
   first entry). *(Or Settings → Audio → output device.)*
3. Bring **Vol** up; if it's still quiet, add a few dB on **AF**.

No device matching, no sample-rate fiddling — this is the default and the
path we recommend unless you specifically need PC audio.

### PC sound device

1. Click **Out** (or open **Settings → Audio**).
2. Pick your PC output — speakers, USB headset, or interface.
3. Set **Vol** to taste.

Lyra resamples between the HL2 and your sound card automatically. If your
device name appears more than once (different Windows audio backends), just
try one; pick another only if you hit dropouts.

### Digital modes — getting RX audio to another program

A digital program (WSJT-X, JTDX, MSHV, FLDigi…) needs to *hear* the
receiver. Lyra offers **two routes** — pick the one your program supports:

- **TCI** — if your program speaks TCI (e.g. **MSHV**, **SDRLogger+**), it
  connects over the network and the RX audio rides that link directly. No
  virtual audio cable needed, and the same link carries transmit audio back
  the other way.
- **VAC** — for programs that expect a **sound-card input** (e.g. WSJT-X,
  JTDX classic, FLDigi). Lyra plays RX audio into a virtual audio cable and
  the program records from it.

*Which?* If the app has a "TCI" radio/transport option, use TCI — least
setup. If it only offers sound-card in/out, use VAC.

**TCI route:** Settings → Network — enable Lyra's **TCI server**, note the
port. In the program, choose **TCI** and point it at Lyra's address + port.
RX audio now streams over TCI (transmit goes back the same way — set the
mic source to **TCI**; see *Setting up your mic input*).

**VAC route:** install a virtual audio cable on Windows first (e.g.
VB-CABLE) — it appears as both a playback and a recording device.

1. **Settings → Audio → Virtual Audio Cable (VAC1)**.
2. Tick **Enable VAC1 (RX→PC and PC→TX)**.
3. **Driver** — the audio backend for the cable (default usually works).
4. **Output device** — the cable's **input side** (e.g. "CABLE Input"):
   where Lyra sends RX audio.
5. Set **RX gain** so the program's level meter sits in its happy range.
6. In the program, set its **audio input** to the cable's **output side**
   (e.g. "CABLE Output").

There's an option to auto-enable VAC1 when you switch to a digital mode.
VAC1 also carries **PC → TX** (covered in *Setting up your mic input*).

**Latency (for fast ARQ modes like VarAC).** Two controls tune how much
buffering the cable carries:

- **Buffer size** — the audio block (smaller = lower latency, more CPU, less
  dropout margin). 2048 is the safe default for SSB/voice.
- **Latency** — the ring‑buffer depth in each direction.

For a quick‑turnaround digital mode (VarAC and other ARQ modes), a smaller
buffer + lower latency shaves the TX↔RX turnaround. Keep the latency above
roughly **2× the buffer‑block time** or the ring underflows. Use the live
**Monitor** line below the controls — it shows ring fill % plus
overflow/underflow per direction; drop the latency until the over/underflow
counters just start to climb, then back off one step.

Both values are **saved with the profile** (schema v5), so a tight VarAC
profile and a fat, safe SSB profile each keep their own latency posture —
switching profiles switches the whole VAC setup with them.

### Hearing your own transmit

On the Audio panel, **MON TX** monitors your own processed TX audio (set
the level with the **Monitor** slider) so you can hear exactly what you're
sending; it routes to the HL2 jack. For CW, **CW MON** sets the gateware
sidetone level (separate from MON TX).

### If something's wrong

- **No audio:** check **MUTE** is off and **Vol** is up; confirm the right
  device under **Out** (the marked entry is active); on the HL2 jack make
  sure the plug is in the *radio's* jack.
- **Too quiet at full Vol:** add gain on **AF**.
- **Clicks/dropouts on a PC device:** that's the two-clock bridge under
  load — try a different backend of the same device under **Out**, close
  heavy audio apps, or switch to the HL2 jack (which avoids it entirely).
- **One side only:** centre the **Bal** slider.
- **A digital app can't hear the radio:** *TCI* — confirm the TCI server is
  on and the app is set to TCI on the right port; *VAC* — Lyra's Output
  device must be the cable's *input* side and the app's input the cable's
  *output* side (a matched pair); nudge **RX gain**.

---

## Setting up your mic input

*TX audio source — the radio's codec mic, a PC mic, or digital-mode audio.*

Lyra lets you choose what drives the transmit chain, at **Settings → TX →
Mic source**:

- **Mic In** — a microphone in the **Hermes Lite 2's mic jack** (its
  onboard codec, AK4951 on HL2+). *The default for voice.*
- **PC Soundcard (VAC1)** — audio from your **PC** (a USB/headset mic, or a
  program) via a virtual audio cable.
- **TCI (digital modes)** — audio streamed from a digital-mode program
  (MSHV, JTDX, WSJT-X, FLDigi…) over Lyra's TCI link; the mic is bypassed.

*(VAC2 appears greyed out — planned for a later version.)* Only
one source is live at a time — whatever's selected goes on the air.

**Which one:**

- Voice with a mic on the radio → **Mic In**.
- Voice with a PC/USB mic or headset → **PC Soundcard (VAC1)**.
- FT8 / FT4 / RTTY / digital → **TCI (digital modes)**.

**Watch the ALC meter while setting any source.** A little ALC action on
peaks is good; ALC slamming hard the whole time means too much input — back
the gain off.

### Mic In (radio's codec mic, AK4951)

1. Plug your mic into the **MIC jack on the Hermes Lite 2**.
2. **Settings → TX → Mic source → Mic In**.
3. Key into a dummy load, speak at your normal level, and raise **Mic
   Gain** until the **ALC** meter just flicks on voice peaks.
4. If your mic is genuinely weak, tick **Mic Boost (+20 dB hardware, codec
   mic only)** — the HL2 codec's analog preamp — then fine-trim with Mic
   Gain. *(Mic Boost only affects the codec mic; it does nothing for PC or
   TCI sources.)*

### PC Soundcard (VAC1) — a PC/USB mic

Use this when your microphone is on the computer. It rides the same
virtual-cable bridge as digital modes (install one, e.g. VB-CABLE, if
you're routing from another program; for a plain PC/USB mic, point VAC1's
input straight at the mic).

1. **Settings → TX → Mic source → PC Soundcard (VAC1)**.
2. **Settings → Audio → Virtual Audio Cable (VAC1)**: tick **Enable**,
   set **Input device** to your PC mic (or the cable's output side if
   feeding from another program), and set **TX gain** so the ALC sits
   right when you talk. **Combine input** mixes a two-channel source to
   mono if it comes through one-sided.
3. Key up and check the ALC as above.

### TCI (digital modes)

For FT8/FT4/RTTY/etc., the digital program sends transmit audio to Lyra
over TCI — no microphone involved.

1. **Settings → TX → Mic source → TCI (digital modes)**.
2. **Settings → Network** — make sure the TCI server is enabled.
3. In the program (MSHV / JTDX / WSJT-X / FLDigi…), select **TCI** and
   point it at Lyra's address + TCI port.
4. Set levels in the **program** so Lyra's ALC shows only light action —
   digital wants clean, just-below-ALC drive. If it's hot, Lyra also has a
   TCI input-gain trim to tame an over-driven client.

### Setting levels (any source)

Aim for voice peaks (or digital drive) to push the **ALC** into a little
action, not pinned. Too hot (ALC slammed) = distortion/splatter — reduce
the gain (Mic Gain, VAC1 TX gain, or the digital app's level). Too cold (no
ALC, low power) = raise it. For voice, start on a **dummy load** and use
**MON TX** to hear your own processed audio while you dial it in.

### If something's wrong

- **No transmit audio:** confirm **Mic source** matches what you're using
  (a PC mic won't work if the source is still *Mic In*), you're keyed, and
  PA + Drive are set for real RF.
- **Mic In weak even at high Mic Gain:** tick **Mic Boost (+20 dB)**, then
  trim with Mic Gain.
- **Distorted, ALC pinned:** back off the gain for the active source.
- **PC mic silent:** source must be **PC Soundcard (VAC1)** *and* VAC1
  enabled with the right **Input device**; enable **Combine input** if a
  stereo source is one-sided.
- **Digital app keys but nothing goes out:** source must be **TCI**, the
  app set to TCI on the right port, and the app's output level up.

---

## First voice setup (SSB / AM / FM)

New to the radio and just want to **talk**? This is the short, no-jargon
path to your first voice contact. It covers the phone modes — **SSB**
(USB/LSB), **AM**, and **FM**. The audio wiring is the **same for all
three** — you just pick the mode you're operating. (Digital modes like FT8
use a different path — see *Digital modes over TCI / VAC*.)

**Start with one question — where are your microphone and headphones/speaker
plugged in?**

- Into the **radio** (it has a MIC jack and a PHONES jack on the box) →
  **Path A**.
- Into your **computer** (USB headset, a PC mic, PC speakers) → **Path B**.

Follow only your path.

### Path A — mic & headphones plugged into the radio  ✅ simplest

This needs an **HL2+ / AK4951** — the version with audio jacks on the box.
Nothing touches Windows sound settings.

1. Plug your headphones or speaker into the radio's **PHONES** jack.
2. Plug your microphone into the radio's **MIC** jack.
3. **Settings → Audio → Out = "HL2 audio jack (AK4951)"**.
4. **Settings → TX → Mic source = "Mic In"**. If people say you're quiet,
   tick **Mic Boost (+20 dB)**.
5. Pick your mode and frequency — SSB (**USB** above 10 MHz, **LSB** below),
   or **AM** / **FM** as the band or net calls for.
6. Hold your mic **PTT** (or a foot switch, or click **MOX**) and talk —
   watch the **MIC** meter move as you speak. That means it's hearing you.

### Path B — mic & headphones plugged into the computer

Works on any HL2 or HL2+.

**Hearing the radio (RX):**

1. **Settings → Audio → Out =** your PC speakers/headset (pick your Windows
   playback device from the list).

**Talking (TX) — through a computer microphone:**

2. Know which kind of mic you have — both appear in Windows as an **input
   device**:
   - a **USB** microphone or USB headset shows up as its own device;
   - an **analog** mic in the PC's pink MIC jack, or into a sound-card / USB
     interface input, shows up as that card's *Microphone* / line-in.
3. **Set the mic up in Windows first** — Lyra can't fix a mic Windows can't
   hear. In **Windows → Settings → Sound → Input**, choose that microphone
   and set its **mic level** to a usable value (not muted, not maxed). Many
   mics ship muted or at 0; some USB mics/headsets also have their own
   manufacturer app or a hardware volume/mute — check it. Speak and confirm
   the Windows input bar moves.
4. Point Lyra at it — **Settings → TX → Mic source = "PC Soundcard (VAC1)"**,
   then **Settings → Audio → VAC1** and set the **Input device** to that same
   Windows microphone.
5. Pick your mode and frequency (as in Path A step 5), hold **MOX** (or your
   PTT), and talk — watch Lyra's **MIC** meter move. No movement → go back to
   step 3 (Windows).

---

## Display panel

The handful of spectrum/waterfall controls you reach for most often, one
click away instead of buried in Settings. These mirror **Settings →
Visuals** — change them in either place and both stay in sync. The panel
is laid out in old Lyra's three-row arrangement:

- **Zoom** — magnify the panadapter around center: a preset combo
  (**1× / 2× / 4× / 8× / 16×**) plus a fine slider and a live "N.Nx"
  readout. Zooming in narrows the displayed span so you can pull a single
  signal apart.
- **Panafall** — the step the mouse wheel tunes by when you scroll over
  the panadapter/waterfall (**1 Hz … 100 kHz**). This is your coarse
  band-skim step, separate from the fine VFO step on the Tuning panel.
- **Exact / 100 Hz** — how a panadapter **click/drag/wheel** tune lands:
  - **Exact** — go to where you clicked, snapped to the **Panafall step**
    above. With Panafall = 1 Hz that's truly exact; set it to 10/50 Hz
    (or higher) and clicks snap to that grid. The RX1 readout shows the
    full resolved frequency.
  - **100 Hz** — quantize every click/drag/wheel tune to the nearest
    100 Hz, regardless of the Panafall step.

  (At low zoom the panadapter is pixel-limited — each pixel can be tens
  of Hz wide — so to actually land on a 1 Hz boundary, zoom in or use the
  Tuning panel's fine step.)
- **Spec** — spectrum frame rate (how many times per second the panadapter
  redraws).
- **WF** — waterfall scroll rate (history rows per second).
- **Peak Hold** — how the [peak markers](#peak-markers) behave:
  - **Off** — no peak markers.
  - **Live** — markers ride the current spectrum.
  - **1 / 2 / 5 / 10 / 30 s** — hold each peak for that long, then fade.
  - **Hold** — peaks never fade until you press **Clear**.
- **Decay** — how fast held peaks fade once their hold time is up:
  **Fast / Med / Slow** (only matters in the timed modes).
- **Clear** — wipe the held peaks and start fresh (handy in **Hold** mode).

The look-and-feel settings — palettes, smoothing, glow, gridline, peak
style/color, noise floor — live in **Settings → Visuals**.

---

## Meter panel

A GPU-drawn **RX signal-strength meter** with three looks — pick whichever
you like with the small **Arc | Bar | Ladder** toggle in the panel's
top-right corner (your choice is remembered):

- **Horizon Arc** (default) — a sweep dial against a fixed, calibrated
  S-scale. The fill climbs through colour zones (cyan → green → amber →
  red over S9) with a glowing "comet head" at the current level, a thin
  **peak-hold** pip that holds then fades, and a faint **history trail**
  in the dial's belly showing the last few seconds (handy for watching
  QSB / a station rising). Big **S-unit** readout in the centre with the
  **dBm** and **SNR** lines beneath.
- **Plasma Bar** — a continuous level bar through the same colour zones,
  with a bright leading edge, peak-hold cap, a glass reflection beneath,
  and the readout (S-units / dBm / SNR) along the top.
- **Vertical Ladder** — a compact multi-source column that shows several
  readouts at once as labelled bars (e.g. PWR / SWR / ALC together on
  transmit) instead of one big primary readout — handy when you want to
  watch the whole picture at a glance.

Both styles also carry an optional **max-peak marker** — a second, **red**
"high-water mark" that latches the highest level reached and falls back
*gently*, so a fleeting DX or QSB crest stays readable long after the fast
yellow peak pip has dropped. The fast pip tracks recent activity; the red
max marker remembers the loudest moment.

In **[Meter panel](#meter-panel)** you can tune both timings:
**peak-hold** (how long the fast pip hangs before falling, default 800 ms)
and the **max-peak marker** (on/off + its own hold time, default 3 s)
before its gentle decay.

**Separate RX / TX style (optional).** By default one style is used for
both receive and transmit. Tick **Separate RX / TX meter style** in
**Settings → Meter** to use a different look on each — for example
**Horizon Arc** while receiving and **Vertical Ladder** for a clean
PWR/SWR read on transmit. Just like the source swap below, the style then
changes automatically the instant you key up and back on unkey. The header
**Arc | Bar | Ladder** buttons stay a live override for whichever state is
showing at that moment.

What the meter shows:

- **S-units** — standard scale (S1…S9, then +20/+40/+60 dB over S9). The
  scale follows the band: S9 = −73 dBm below 30 MHz, −93 dBm above, the
  same convention major HF SDR software uses.
- **dBm** — the calibrated signal level.
- **SNR** — signal *above the noise floor* in dB (shows `—` when there's
  nothing above the floor). The faint band/marker on the meter is the
  current noise floor, so you can see at a glance how far a signal stands
  out — i.e. how readable it is, not just how strong.

The meter reads the in-passband signal level from the DSP engine (the
same source professional SDR software uses) and only moves while you're
receiving (press **▶ Start** first; it rests at S0 when idle).

> **Calibration:** the meter taps WDSP's in-passband `RXA_S_PK` level —
> the standard in-passband signal-strength point — and maps it through
> the HF/VHF S-unit scale (S9 = −73 dBm below 30 MHz, −93 dBm above). To make the
> *absolute* dBm/S reading exact, trim it once in
> **[Meter panel](#meter-panel)**: tune to a known reference
> (WWV, or a signal generator at a known level) and adjust the **S-meter
> calibration** offset until it matches. With this tap the trim is only a
> few dB. The meter compensates for the **LNA gain** automatically, so the
> reading stays put as you adjust LNA — you can calibrate at any setting.
> The relative movement, peak-hold, SNR, and
> noise-floor behaviour are all live regardless.
>
> **Transmit meters** are fully wired. The wire / safety / telemetry
> set — **PWR** (forward power, watts), **SWR** (antenna match),
> **ID** (PA bias current), **VDD** (PA supply), **Temp** (HL2
> board) — plus the full TX-chain dynamics set that taps every
> stage of the modulator chain in signal-flow order:
>
> | Picker entry | What it shows | Unit |
> |---|---|---|
> | **MIC** | mic peak into the modulator | dBFS |
> | **LEV** | leveler **output** level | dBFS |
> | **LVL G** | leveler gain (how much it's boosting weak passages) | dB (typically positive) |
> | **ALC** | ALC **output** level — what the wire sees | dBFS |
> | **ALC G** | ALC gain reduction (how hard the limiter is working) | dB (typically negative or 0) |
> | **ALC Σ** | ALC_PK + ALC_GAIN — what the wire **would** see if the ALC weren't acting | dBFS |
>
> All read live from the running TX chain the moment MOX engages,
> and show "—" on RX. Each meter uses the reference's ballistic
> exactly: the DSP engine smooths internally over ~100 ms, and the
> peak marker decays exponentially over ~500 ms (no UI-side
> over-smoothing — needles respond crisply to voice attack so
> transients show their true peak). Set the active TX source (and
> an optional secondary readout line, or a third line under that)
> in **[Meter panel](#meter-panel)**. The panel auto-swaps from
> your RX source to your TX source on every MOX edge and back, so
> you can watch what you need without manual switching.
>
> **On CW** the meter swaps to your TX set and shows forward power
> too — for keyboard/macro (console) sending and for a paddle,
> straight key, or external keyer into the radio. CW keys the PA
> at the radio's gateware (so the wire doesn't go through the
> normal MOX path), but Lyra still flips the meter for the duration
> of the over, then back to the S-meter when you stop.
>
> **Tip for dialing in the chain:** pick **MIC** as primary +
> **ALC G** as secondary, key the mic, and adjust Mic Gain until
> MIC peaks land near −6 dBFS with ALC G staying under 3 dB. Want
> to see the leveler doing its work? Pick **LVL G** — a positive
> reading means the leveler is boosting a quiet passage to keep
> your modulator level consistent.
>
> **What's coming with v0.2.1:** the **Combinator** (5-band
> multiband compressor + speech enhancements + parametric EQ +
> tube plating) lands as a Lyra-native processor before the WDSP
> TXA chain. Its per-band activity and total gain reduction will
> arrive as additional Meter Panel sources at that time — picker
> values 10/11/12 are reserved for them now.

---

## TX panel

The operating-time TX controls — a single strip across one of the
docks. Layout (left → right):

```
[TX]  Drive ──●── 25 %  Mic ──●── +10 dB     [ VOX ] [ Tune ] [ MOX ]
```

(The full button cluster also carries **ATT** and **Prot** lamps — see
[ATT on TX](#att-on-tx-rx-adc-protection) and
[SWR protection](#swr-protection--tx-power-limiting). All five buttons
share one tidy size, and each lamp always reflects the true on/off state
no matter where you changed it — front panel or Settings.)

- **TX Drive** (slider, 0–100 %, default 0) — how hard the HL2
  drives its on-board PA. **0 % = no carrier even with PA enabled
  and MOX keyed.** Start LOW (5–10 %) on a dummy load; raise gradually
  while watching the watt-meter. **Mouse-wheel** adjusts (Shift + wheel
  = 5 % steps). Live readback to the right shows the current %.
- **Mic Gain** (slider, −90 dB to +40 dB, default 0 dB) — input gain
  into the WDSP TXA modulator (PanelGain1 — the only operator-tunable
  software gain stage in the TX chain, before the bandpass / leveler /
  ALC / everything downstream). **0 dB is WDSP unity** = no boost vs
  the raw mic signal. Typical SSB voice runs **+10 to +20 dB**; ESSB
  with headroom **+25 to +35 dB**. The range matches the reference's
  Default TX profile (−90 deep-attenuate floor, +40 ceiling), so the
  full operating travel is preserved. **Mouse-wheel** adjusts (Shift +
  wheel = 5 dB steps). Bidirectionally bound with the spin-box in
  **Settings → TX → Mic + ALC** — moving either updates both, so you
  can adjust on the fly here or type a precise value in Settings.
  Watch the **ALC meter** as you raise — back off when ALC engages
  hard (more than ~3 dB of gain reduction means you're driving past
  the limiter ceiling and creating splatter risk).
- **VOX button** (labelled **VOX off** / **VOX** / **VOX ●**) — arms
  **voice-operated transmit**: when armed, speaking into the mic keys
  TX and TX drops after your voice stops (see
  [Settings → VOX](#settings--vox-voice-operated-transmit) for the
  threshold / timing / anti-VOX knobs). Gray = off; **green = armed**
  (listening to your mic); **red ●** = keying right now on your voice.
  Click to arm / disarm — bidirectional with the Settings → VOX tab.
  VOX keys **voice modes only** (never CW), and **never overrides a
  manual or foot-switch key**. Clicking it off is an **instant kill**:
  it drops a VOX-held transmission immediately. Default OFF.
- **Tune button** (labelled **Tune**) — single-click gesture: arms a
  1 kHz **tune carrier**
  AND keys MOX in one click. Click again to release MOX; the carrier
  auto-disarms on the next MOX-off edge for any reason (your click,
  the safety timeout, the FSM unwinding). When armed the button
  border glows amber. Carrier power follows the **Tune drive mode** you
  pick in [Settings → TX](#settings--tx-mic--alc-tr-sequencing--cos-fade)
  (your TX Drive slider, a separate tune slider, or a fixed value — see
  there), and is always held to your **Max TX drive** ceiling.
- **MOX button** — the keying button. Funnels through Lyra's TR-
  sequenced FSM (the same one your **space-bar momentary** uses when
  no text widget has focus). Goes **bold red** the instant the wire-
  MOX bit settles after the TR-delay window. A short tap shorter
  than the TR-delay (≈ 65 ms by default) still flashes the button
  **orange** for ~220 ms so you get visual feedback that your press
  registered — even if the FSM cancelled mid-keydown.

What the button colours mean, at a glance:

| MOX button colour | Meaning |
|---|---|
| Gray | Idle (RX) |
| **Orange flash** (~220 ms) | You just pressed; wire-MOX hasn't settled yet |
| **Bold red** | Wire-MOX bit is high — radio is keyed, RF flowing if PA + Drive permit |

TUN is a strict superset of MOX for tune-up: it does what MOX does
PLUS injects the 1 kHz tune carrier. Use **MOX** when you want the
relay to switch (CW key-down testing, mic feed-through, or with
future SSB modulator); use **TUN** when you want a steady carrier
on your dial frequency to set drive level / read an SWR / cal an
external watt-meter / bias-tune an external amp.

> **Both buttons honour the safety knobs.** They route through the
> same FSM that respects the **TR-sequencing delays** (see
> [Settings → TX](#settings--tx-mic--alc-tr-sequencing--cos-fade)) and the
> **PA Enable** safety gate (see
> [Settings → Hardware → Transmit](#transmit-pa-enable--safety-timeout--hardware-ptt)).
> Carrier amplitude follows the **cos² fade envelope** on every
> keydown and keyup — no hard step, no click.

> **PA Enable is separate from MOX.** With PA disabled, keying MOX
> or TUN gives you the relay click + the wire-MOX state but **zero
> RF** (the gateware PA bias never engages). This is the safe state
> for first-time bench work — leave PA off until you're confident
> in your Drive setting, fade defaults, and amp configuration.

> **Foot switch / hand-mic PTT** — the HL2's hardware PTT-input pin
> is supported via an opt-in checkbox in
> [Settings → Hardware → Transmit](#transmit-pa-enable--safety-timeout--hardware-ptt).
> Default OFF for safety; tick it after a quick bench check on your
> specific HL2 unit.

---

## Waterfall ID (TX callsign courtesy ID)

Lyra can paint your callsign as a small image in the top of your
transmitted SSB waterfall — a **courtesy ID** the receiving station can
read on their own waterfall. It's a visual nicety, not a substitute for
identifying by voice.

- **Arm it** with the **WF-ID** chip (top toolbar, **Options** group).
  Armed (amber) = it sends one ID now and then repeats every N minutes.
- **Set it up** on the **TX panel → Waterfall ID** section: the repeat
  **interval**, the **Level** (how strong the image is in the passband —
  keep it low so it doesn't dominate your audio), and a manual **Send**
  button for a one-shot ID.
- **USB/LSB voice only.** In digital modes your call is already in the
  payload, so WF-ID is disabled there.
- **Ham bands only.** The chip is dead — and the ID can never fire, from
  any tune source — anywhere **outside an amateur allocation for your band
  plan** (Settings → Hardware region). So 11m / CB and, say, a US ham who
  spins onto **7.310** (the 41m broadcast band) can't raster a callsign
  where it doesn't belong. The check follows your **region** (US / IARU-R1
  / R3), so it uses the right band edges.
- **Safe by design.** It re-arms **OFF** every session (so it can never
  auto-key on a fresh launch), and any band-edge crossing or mode change
  disarms it — a courtesy ID can't carry over into a context you didn't
  intend.

> Right-click the **WF-ID** chip for this help; the same goes for **CTUN**.

---

## TX DSP rack (EQ + Speech + Combinator + Plating)

Lyra ships four native, operator-tunable DSP stages that sit on the mic
audio **before** the WDSP TXA modulator — your own EQ, speech-conditioning,
multiband-compressor and plate-reverb rack, done Lyra-native. They run in
chain order **Speech → EQ → Combinator → Plating**. Each lives in its own
dockable / collapsible
panel (show/hide from the window's dock menu; drag, float and resize like
any Lyra panel), and your layout is saved with the rest of your window
state.

### How bypass works (read this first)

Every item in the rack is **independently bypassable**, and bypass means
exactly what you'd hope:

> **When an item is OFF it leaves the mic audio untouched — that stage's
> processing is skipped entirely (a true pass-through), bit-for-bit.**

This matches the reference's behaviour — its TX EQ and compressor are
manual on/off, and off means "not applied." Three things to keep in mind
so you're never surprised:

- **The EQ ships ON.** Its default layout includes a gentle high-pass and
  a high-shelf that shape the audio *even at 0 dB gain*. For a genuinely
  flat, unprocessed mic path, turn the EQ's **ON** button off — don't just
  zero the gains.
- **The rack is pre-modulator.** Turning rack items off does **not**
  bypass the radio's transmit DSP: the always-on **ALC** limiter (splatter
  protection) and your selected **TX bandwidth** filter always run
  downstream — plus the Leveler / PHROT if you've enabled them in
  [Settings → TX](#settings--tx-mic--alc--leveler-tr-sequencing--cos-fade).
  "Item off" means clean *through that item*, not clean *to the antenna*.
- **Default states differ.** The **EQ ships ON**; **TX Speech, the
  Combinator and Plating ship OFF** (they materially process the audio, so
  they wait for you to enable them).
- **Digital modes auto-bypass the whole rack.** In **DIGU / DIGL** Lyra
  automatically skips the entire native rack (EQ + Speech + Combinator +
  Plating) so FT8 / JS8 / etc. transmit unshaped — you don't have to switch
  anything off. (This auto-gate is a Lyra convenience; the reference leaves
  it to you to untick manually or load a digital profile.) The panels show
  this at a glance: in **DIGU / DIGL** — and in **CW**, where there's no mic
  audio to shape — each rack panel's **ON** lamp greys out, its controls dim,
  and an amber **"bypassed (MODE)"** hint appears in the header. It's purely
  cosmetic: your settings are untouched and the panels re-light the instant
  you return to a voice mode (USB / LSB / AM / FM).

### TX EQ — 10-band parametric

A draggable response curve over a live spectrum analyzer.

- **Bands** — drag a node to set its **frequency + gain**; **mouse-wheel**
  over the selected node sets **Q** (width). Each band has a **Type** chip
  that cycles Peak / Low-shelf / High-shelf / Low-pass / High-pass /
  Bandpass / Notch. The default layout opens with a 60 Hz high-pass and a
  high-shelf; that high-pass — like any band — can be dragged down to the
  **20 Hz** frequency floor or up.
- **Tile row** under the curve shows each band's #, type, freq and gain/Q.
  **Right-click a tile** to reset just that one band.
- **Top bar** — **ON** (bypass; see above) · **Makeup** output trim
  (± dB) · **Reset** (flatten *all* bands back to their defaults) ·
  collapse ▼.
- **Analyzer behind the curve** — the **Spec** button cycles
  **Off → Spectrum → RTA** (a line/fill spectrum, or log-spaced 1/N-octave
  band bars). **Acc** adds a **red peak-hold** overlay; **right-click Acc**
  for the hang preset: **Live** (tracks the instantaneous signal, no hold) /
  **Fast** / **Medium** / **Slow** / **Hold** (never decays). **B/A**
  overlays the pre-EQ ("before") trace in **white** against the post-EQ
  **cyan** so you can see exactly what the EQ did. On an HL2+ the mic codec free-
  runs, so the analyzer animates on receive too — you can shape your EQ
  while just listening, no need to key up (it's analysed, not
  transmitted, until you actually key).

### RX EQ — receive parametric EQ

The receive-side twin of the TX EQ — the **same 10-band panel and the same
controls** (drag a node for frequency + gain, wheel for Q, Type chips, the
tile row, Makeup, Reset, and the analyzer behind the curve), applied to what
you **hear** instead of to your mic. Open it from the **RX DSP → RX EQ** chip
on the header strip; it pops as a movable window and your layout is
remembered, exactly like the TX DSP panels.

- **What it shapes** — the receive audio after WDSP, so it colours the
  headphone/speaker output *and* anything bridged out (recording, VAC, a
  digital-mode app).
- **Graph width tracks the RX bandwidth** — the curve spans the receive
  passband actually in use, so the bands sit where the audio is. Change your
  RX filter width and the EQ rescales to match.
- **ON / bypass** — the top-bar **ON** button bypasses the RX EQ in **any**
  mode. It additionally **auto-bypasses in the digital data modes (DIGU /
  DIGL)** so decoders always get flat audio; it stays available in CW and SSB.
- **Separate from TX** — the RX EQ is a standalone listening setting (its own
  bands + bypass + makeup, remembered across sessions). It is **not** part of
  a TX profile — switching TX profiles never touches your receive EQ.

### TX Speech — Noise Gate → Auto-AGC → De-esser

Three independent stages, processed in that order, each with its own
**ON** toggle. **All three default OFF**, so the panel changes nothing
until you enable a stage.

- **Noise Gate** (first) — gates shack background noise between words.
  **Threshold** (default −45 dBFS), **Range/Depth** (how far it attenuates
  when closed, default 60 dB) and **Hold** (default 120 ms). It freezes the
  Auto-AGC gain while closed so pauses don't "breathe."
- **Auto-AGC** — an input leveller that rides quiet and loud passages
  toward a target level (**Target**, **Max-gain** cap). This is distinct
  from the WDSP output **Leveler** in
  [Settings → TX](#settings--tx-mic--alc--leveler-tr-sequencing--cos-fade).
- **De-esser** — a frequency-selective compressor that tames harsh
  sibilance ("s" / "sh"). **Frequency** (where it listens), **Threshold**
  and **Range** (maximum ducking).

### TX Combinator — 5-band multiband compressor

The last native stage (after the EQ). It splits the mic into five bands
with 24 dB/oct crossovers, compresses each independently, sums them, and
optionally auto-balances the bands — the classic "broadcast / ESSB"
multiband sound. **Default OFF.**

- **ON** — engages the stage (top-bar button). Off = true pass-through.
- **Global controls:** **Mix** (wet/dry — 100 % = fully processed),
  **Attack** / **Release**, **Ratio** (one ratio, all bands), **X-Over**
  (shifts the whole band split), and global **Thresh** / **Makeup** that
  every band rides on top of.
- **SBC (Spectral Balance Control)** — when engaged, automatically balances
  the level *between* the bands; **Speed** sets how aggressively it works.
- **Per-band** — pick a band (**LOW / LO-MID / MID / HI-MID / HIGH**) and
  set its **Thresh** offset and **Gain** (makeup), or switch the band off.
  The five colour-matched meters show each band's gain reduction live while
  you transmit.

The panel mirrors a hardware multiband processor's layout; the shipped
default is N8SDR's own preset. It auto-bypasses in DIGU/DIGL with the rest
of the rack.

### TX Plating — plate reverb (ESSB air)

The last native stage (after the Combinator). A plate-class reverb for the
ESSB "broadcast air" sound — a subtle wet tail that adds space without
washing out the voice. **Default OFF.** Bandplan-constrained operators
(contest, crowded DX) just leave it off.

- **ON** — engages the stage. Off = true pass-through.
- **Preset picker** — **W5UDX** and **N8SDR** load the two ship presets
  (the eight reverb params); your **Mix** is left untouched (it's a
  personal wet-level).
- **Controls:** **Pre-Delay** (gap before the tail), **Decay** (tail
  length / RT60), **Damp** (how fast the tail's highs roll off), **Size**
  (plate size), **Density** + **Diffusion** (echo smoothness), **Bass** /
  **Treble** (wet-tail tone), and **Mix** — the reverb amount, **capped at
  15 %** (the useful ESSB range) and shipped at **7 %**.

It sits before the always-on ALC limiter, so the reverb tail is caught by
the limiter; it auto-bypasses in DIGU/DIGL with the rest of the rack.

---

## Voice keyer & recording

The voice keyer is the phone-mode counterpart to the CW macro bank: record
your standard messages once — a CQ call, a contest exchange, a "please stand
by" — and send them on the air from a labelled button or an **F-key**. It
also doubles as a recorder for **what you hear** (RX), so you can grab a
weak signal or an odd bit of traffic to a WAV file.

Open it from the **Voice Keyer** chip in the header **Options** group — it's
a floating panel you can dock or leave floating, laid out like the CW
console's macro chips.

**Recording a message**

1. Pick the source with the **Mic / RX** toggle — **Mic** records your voice
   through the mic (for a keyer clip), **RX** records the receiver audio
   you're listening to.
2. Press **REC**. If you set a **record countdown** (see Settings below), the
   button shows **⏱ 5… ⏱ 4…** so you can get set before capture starts —
   click it again to cancel.
3. Press **Stop**. The clip is saved, auto-named ("Message 1"…), and appears
   as a labelled row.

**Managing clips** — each row has an edit mode (rename, assign an **F-key**,
set a per-clip **gain** trim, or **delete** with the trash-can). **Import
WAV** pulls in a clip you recorded elsewhere; the footer shows the storage
folder (clips live as 48 kHz WAV in `%APPDATA%\Lyra\clips\`).

**Playing a clip two ways**

- **▶ Review** plays the clip **locally** through your speakers — it does
  **not** transmit. Use it to check a recording before you send it.
- **▶ OTA** (or the clip's **F-key** in a voice mode) **transmits** the clip.

**Transmitting on the air (opt-in)** — OTA transmit is gated behind the
amber **Enable TX** toggle at the top of the panel, **off by default**. Until
you arm it, the keyer can only Review locally — so you can build and audition
your clips with no risk of keying up. With it armed, a clip is injected at
the **mic input with MOX**, so it runs your full TX DSP chain (EQ, speech,
compression, ALC) and is **PureSignal-safe** exactly like your live voice.
The **Bypass DSP** toggle sends the clip as-recorded instead. The keyer keys
with its own priority and **never overrides** a manual, foot-switch, or VOX
key.

**Gain** is adjustable three ways — a global keyer level, the per-clip trim,
and the input level while recording — and the OTA level is **live while
you're transmitting**, so you can ride it up or down mid-message.

**Recording options (Settings → Audio → "Voice keyer recording")**

- **Maximum length** — caps any single recording (default **5 minutes**).
- **Record countdown** — **Off / 5 s / 10 s**, the "get set" timer shown on
  the REC button.

---

## Session recorder (RX audio + snapshots + MP4)

Where the voice keyer grabs short clips, the **session recorder** captures a
whole listening session — the **receive audio** to a WAV, plus **timed
snapshots of the panadapter/waterfall** — into one timestamped folder. You
can later turn that folder into a single **MP4** where the snapshots play as a
slideshow over the audio, handy for sharing a band opening or an unusual
signal.

**Open it from the `⏺ Recorder` chip in the header**, between the *RX DSP* and
*Options* groups. Click it to pop out the **Recorder** panel (a floating tool
window). While a recording is running, that same chip turns into a red
**`● REC hh:mm:ss`** timer so you always know at a glance that it's rolling —
click it to reopen the panel, where **Stop** lives.

**The Recorder panel**

- **● Rec / ■ Stop** — start or stop a session.
- The **timer** shows elapsed time.
- **Snapshots** — a checkbox plus a **/min** rate. Off = audio-only (no
  images). The rate also lives in Settings and stays in sync.
- **⚙** — jumps to **Settings → Recording** for the folder, limits, and the
  saved-sessions list.

> **You have to be on the air to record.** Recording needs live receive
> audio, so **Record does nothing while the radio stream is stopped** — press
> **▶ Start** in the header first. (Otherwise you'd just capture silence.)

### Settings → Recording

- **Recordings folder** — where sessions are saved (default
  `Documents\Lyra\Recordings`). **Browse** to pick your own, **Default** to
  go back.
- **Snapshots** — master on/off and the capture **rate** (per minute).
- **Auto-split** — start a fresh audio file every **N minutes** and/or every
  **N MB** (both default **off**). Useful for very long sessions.
- **Hard stop after** — an automatic stop (default **10 min**). This is the
  "did I walk away and leave it running?" guard — set it to *off* only if you
  really mean to record unattended.
- **Storage cap** — a ceiling on the whole recordings folder (default
  **5 GB**). When you're over it, Lyra **refuses to start a new recording**
  rather than silently deleting anything; tick **Auto-delete oldest** if you'd
  prefer a rolling "keep the newest" behaviour instead.
- **Recorded sessions** — a radio-button list of what you've saved (name +
  size), with **Convert to MP4**, **Open folder**, **Rename**, and
  **Delete**. The line underneath shows how many sessions you have and how
  much of the cap they use (it turns red when you're over).

**What's in a session folder** — the raw pieces, always kept: `audio_001.wav`
(and `_002…` if it split), `snap_0001.png`, `snap_0002.png`… and a small
`session.json` that records when each snapshot was taken. The MP4 (if you make
one) lands here too as `session.mp4`.

### Making an MP4 (the Convert button)

**Convert to MP4** builds one playable video from a session: the snapshots
become a slideshow, each shown at the moment it was actually captured (so it
lines up with the audio), with the receive audio underneath. An audio-only
session (snapshots off) exports as a `session.m4a` instead.

The conversion runs **in the background at low priority as a separate
program**, so it never interrupts your receive audio — you can keep operating
while it works; a progress bar tracks it.

- **It needs ffmpeg.** Lyra doesn't bundle it. Put **`ffmpeg.exe`** next to
  Lyra (or anywhere on your system **PATH**), or use the **Browse** button in
  the *Video encoder (ffmpeg)* box on this page to point at it. It's a free
  download from **ffmpeg.org/download**. Until Lyra finds it, that box shows
  **"not found"** and Convert is disabled.
- **Transmit is locked out while converting**, and Convert won't start while
  you're transmitting — the encoder and the radio don't fight over the moment.

### Is it legal to record?

For most operators, yes — but you own the local details. Amateur radio is an
**open service** (there's no expectation of privacy on the ham bands the way
there is on a phone call), so recording the traffic **you receive** for your
own use — a QSO, a band opening, a contest run — is normal and generally fine.
Two common-sense cautions: some regions have their own rules about recording,
and **before you publish** a recording (post a video, share a clip) it's
courteous — and in some places required — to have the **consent** of the other
operators you recorded. This isn't legal advice; when in doubt, check your
own country's amateur regulations.

---

## CW operating (paddle, keyboard, TCI)

Set the mode to **CWU** or **CWL** and your tuned carrier sits on the
marker, offset into the passband by your **CW Pitch** (the single pitch
shared by the RX beat note, the marker, the keyed carrier, and the
sidetone — set it on the Tuning panel or the CW console). The HL2's
gateware makes the carrier envelope and the hardware sidetone; Lyra just
tells it when to key. There are **three ways to key**:

**1 — Paddle / straight key (into the HL2 KEY jack).** The radio's
built-in iambic keyer reads your paddle and keys the carrier + sidetone
on its own — the host stays out of the timing, so it's crisp regardless
of PC load. Speed, weight, iambic A/B, and reverse are in **Settings →
CW**. This is also where an **external keyer or Winkeyer** goes: plug its
**KEY output** into the HL2 KEY jack and turn **Iambic OFF** in Settings →
CW — the keyer already does its own timing, so the radio passes the key
straight through (leaving Iambic ON would re-key it as a paddle). A
Winkeyer's USB/COM port is for its own config app (WK3tools, your logger),
not Lyra.

**1b — CW key over a COM port.** If your straight key, bug, or external
keyer's key line is wired to a **serial port pin** instead of the KEY jack,
turn on **Settings → CW → "CW key over COM port"**, pick the port and the
key line (**CTS** or **DSR**), and it keys Lyra's CW. It's off by default.
Because a host-polled serial line adds a little timing jitter, the KEY jack
above is the better route for a paddle or high-speed sending — this option
is best for a straight key or a keyer that times itself.

**2 — Keyboard + macros (the CW console).** Click the **CW** chip on the
top toolbar to pop open the floating **CW Console** (it floats by default
and remembers where you put it — only open it when you want it). It works
only in CWU/CWL; outside CW the send controls dim, so switch to CW first.
The **WPM** slider sets the speed; **Esc** or the red **Stop · Esc** button
aborts whatever is sending. The bottom **type-and-send** line keys a line
live — type and press **Enter**. Lyra also has a separate **RX CW
decoder** panel (the **CW Dec** toolbar chip) that reads the other station
and can drop their call straight into your macros — see
[Reading CW — the RX decoder](#reading-cw--the-rx-decoder).

The console's centerpiece is the **macro bank** — named, click-to-send CW
memories:

- **Send a macro** — click its chip, *or* press its **F-key** (F1–F12).
  The F-keys work anywhere in Lyra while you're in CW (you don't have to
  click into the console first), like a contest keyer. The chip lights and
  shows "sending…" until the keyed text finishes; **Esc**/**Stop** kills it.
- **Defaults** ship for the common calls (CQ, CQ contest, His call, Reply,
  Exchange, TU 73, AGN?, QRZ?). Below the **── My macros ──** divider is
  *your* space: click **+ Add macro** to build your own; they save
  automatically and survive restarts.
- **Repeat** (the toggle + seconds box) re-sends the *last* macro you fired
  every N seconds — handy for calling CQ. Stop also clears Repeat.
- **Edit** (top-right) flips every chip into editable **name + text**
  fields with a delete (✕); click **Done** when finished.

**Tokens — fill-in-the-blanks for your macros.** Instead of fixed text, a
macro can contain tokens in `{BRACES}` that expand when you send:

- **About the contact** (from the console's **contact row** — His call /
  Name / RST / #): `{CALL}` `{NAME}` `{RST}` `{#}`. Copy the other
  station's call into **His call** — type it, or grab it straight from the
  [CW decoder](#reading-cw--the-rx-decoder) — set RST/serial, and one
  "Reply" macro works for every QSO. `{MYCALL}` is your own call from
  Settings → Hardware.
- **Your personal tokens** — reusable facts about *you* that you set once.
  In **Edit** mode, under **My tokens**, add a name + value (e.g.
  `{ME}` = Rick, `{PWR}` = 25 watts, `{QTH}` = Hamilton OH, `{RIG}`,
  `{ANT}`). Then any macro using `{ME}` / `{PWR}` / `{QTH}` fills in
  automatically. (The five built-in names above are reserved.)
- **`{LOG}` — an *action* token, not text.** Drop `{LOG}` into a macro and
  it is **stripped from the keyed Morse** (never sent on the air) — instead
  it logs the current QSO to a linked **SDRLogger+** when the macro is sent.
  Put it on the end of your sign-off (`TU 73 {MYCALL} ee {LOG}`) to log as
  you say 73, or make a macro that is *only* `{LOG}` for a one-click
  "log it" button that keys nothing. It does something only when the
  **[SDRLogger+ Combo link](#sdrlogger-combo-link)** is on; otherwise it's
  harmlessly ignored.
- **Click to insert** — the **token palette** sits just above the send
  line. Click into a macro's text field (in Edit mode) or the send line,
  then click a token chip to drop it in at the cursor — no need to type the
  braces. Built-in tokens are **amber**, your personal ones **cyan**, and
  the **`{LOG}`** action token is **yellow** (it lights up only when your
  cursor is in a macro's text field — it doesn't belong on the live send
  line).

**3 — From a logger over TCI.** A TCI logging/contest program can key CW
through Lyra — see [CW keying over TCI](#cw-keying-over-tci).

**Break-in (Settings → CW):**

- **QSK** (default) — full break-in: the gateware keys the carrier and you
  stay on receive *between* elements, so you hear the band right up to the
  next dit. The panadapter stays on the RX waterfall and you can watch
  your own dits/dahs land on the marker.
- **Semi** — the radio holds transmit through the character and drops back
  to RX after a short hang.
- **Manual** — you hold a foot switch (PTT) to put the radio in transmit
  and key with your paddle inside it; release the foot switch to return to
  RX.

**Sidetone level** — the **CW MON** slider on the **Audio panel** (it
appears only in CW) sets how loud you hear your own keying. It's the
hardware-generated CW sidetone and is independent of the **MON** monitor
used for SSB.

### Reading CW — the RX decoder

Lyra has a built-in CW reader. Click the **CW Dec** chip on the top toolbar
to pop open the floating **CW Decoder** (it floats and remembers where you
put it). It reads **only in CWU/CWL** — outside CW the detector controls
dim and a "switch to CW to decode" note shows.

The decoder is a faithful port of **fldigi's** CW receiver (the W1HKJ
open-source Morse engine), so if you know fldigi's CW behaviour, this is it.

> **Quick copy — the 60-second recipe.** The one thing that matters is
> putting the signal *on your pitch*; the rest is defaults.
>
> 1. **CW Dec** chip → set mode **CWU/CWL**.
> 2. **Land the note on your CW pitch** — by ear, or turn on the
>    [zero-beat needle](#zero-beat-markers--dead-tune-a-carrier) and centre
>    it. There's **no AFC**, so this is the whole game — a signal on pitch
>    copies, off-pitch doesn't.
> 3. **Set Speed** near the fist (≈18 rag-chew, 30+ contest) and leave
>    **Tracking on** — it learns and follows the real speed from there.
> 4. **Only if copy is rough:** narrow **Bandwidth** to ~80–100 Hz to shake
>    a close neighbour; flip **Matched filter** on for a steady signal in
>    QRM; turn **Squelch** on and set it (watch the **Signal bar**) just
>    under the signal peaks to kill `*` noise between stations.
> 5. **Answer in one move** — double-click their call in the text → **His
>    call**, and a `{CALL}` "Reply" macro is ready. Turn on **Match TX speed
>    to RX WPM** to send back at their pace.

**Tune the signal onto your CW pitch.** The decoder listens at the same
single **CW Pitch** the rest of the radio uses, so just put the signal's
beat note where you normally hear it and the decoder is on it. There is
**no AFC** (same as fldigi) — you tune the signal to your pitch and it
copies at that pitch; the **Bandwidth** control gives you the tolerance
for small drift.

**The text pane.** Decoded characters scroll live, and the pane
**auto-scrolls** to keep the newest text in view. The **WPM** readout (top
right) shows the speed it's tracking (it seeds from your **Speed** setting
and, with **Tracking** on, follows the sender's real fist). **Decoding**
toggles the reader; **Clear** wipes the pane. Under **Display** you can set
the **font size** and pick a **text colour**. An unrecognised element
string prints as `*` (fldigi's "noise" character) rather than a wrong guess.

**Grab a call or name into your macros.** Worked a station you want to
answer? Pull their call straight out of the decoded text:

- **Double-click a word** → it goes to **His call**.
- **Right-click a word** → a menu offers **→ His Call** or **→ Name**.
- Or drag-select and click the **→ His Call** / **→ Name** chips.

That fills the CW console's **contact row**, so a "Reply" macro built from
`{CALL}` / `{NAME}` instantly addresses that station — no typing. And if the
**[SDRLogger+ Combo link](#sdrlogger-combo-link)** is on, that same grab is
pushed to SDRLogger+: the call drops into its log entry, its callbook lookup
fires, and the operator's first name comes back to fill your `{NAME}` token
— so one double-click on the decoded call sets up both the reply *and* the
log.

**Match TX speed to RX WPM** (under **Keyer**) — turn this on and your
keyer's send speed follows the speed you're *copying*, so you answer at the
other operator's pace automatically. The current keyer speed shows beside
it.

**The detector knobs.** These are exactly fldigi's CW controls — nothing
more, nothing less. The shipped defaults (Speed 18, Tracking on, Matched
filter off, Bandwidth 150 Hz, Squelch off) are fldigi's own defaults and
copy well on typical signals; reach for the rest only when copy is rough:

- **Speed** — the WPM the decoder *starts* from, set with the **± chips**
  (or type a value). It's a seed, not a lock: with **Tracking** on the
  decoder learns the sender's real speed from the first few characters and
  the header **WPM** follows. Set it near the signal you're working (say 18
  for a rag-chew, 30+ for contest CW) so it locks in faster.
- **Tracking** — the adaptive speed follower. **On** (default) the decoder
  measures dot/dash lengths from validated element pairs and tracks a
  drifting or uneven fist automatically — the right setting for hand-sent
  CW. Turn it **off** to pin the decoder to your **Speed** value when you
  want it to ignore a ragged or interfering fist.
- **Matched filter** — swaps the decoder's front-end low-pass for a tighter
  **matched filter** whose bandwidth follows the speed (fldigi sizes it at
  5 × WPM). **On** gives cleaner element edges and better copy on a single
  steady signal in QRM; **off** (default) uses the plain **Bandwidth**
  filter, which is more forgiving of drift and speed changes. When it's on,
  the Bandwidth slider reads **"auto"** because the filter width is derived
  from the speed.
- **Bandwidth** — the width of the detection low-pass, in Hz (default 150).
  Narrow it toward ~80–100 Hz to reject a close neighbour and tighten the
  spacing between letters; widen it for fast CW or a drifting signal.
  Disabled while Matched filter is on.
- **Squelch** — an on/off **chip** plus a level slider. Off (default) the
  decoder always tries to copy. On, it gates on fldigi's own signal metric
  (an SNR-style 0–100 reading): raise the level to stop it printing `*`
  noise between stations, lower it if a weak signal isn't being read. The
  slider is greyed while the chip is off (no squelching — the decoder runs
  wide open). Because that metric is measured *against the noise floor*, one
  setting holds across bands — it's already "auto-levelling," so a value that
  works stays working when the band noise rises or falls.
- **Signal bar.** Just under the slider is a live **Signal** meter — the cyan
  fill is the decoder's current SNR reading, and (when Squelch is on) the amber
  tick marks your threshold. This is the tool for setting the level: watch the
  bar while a signal comes and goes, then set the threshold **just under the
  signal peaks** so it clears the between-station noise without choking the
  copy. (Keep it a touch low — the same signal that feeds the print also feeds
  the speed tracking, so too-high a squelch starves both.)

> **Tip.** There's no AFC — tune the signal onto your CW pitch on the
> panadapter and leave it there; **Tracking** handles fist and speed, and
> **Bandwidth** gives you the tolerance for small drift.

---

## Tuner (manual ATU memory)

If you run a **manual antenna tuner** (the kind you set by hand with
Input / Output / Inductor controls), the **Tuner** panel remembers your
settings so you can return to a known-good match without starting from
scratch. Open it from the header — **Options: → Tuner**. It's a floating
panel; drag it anywhere. (Pure tuning memory — it doesn't drive or read your
tuner, it just remembers what *you* dialled in.)

**Pick your antenna.** Three antenna slots across the top — **Beam /
Dipole / Vertical** by default; rename any of them (Settings → Tuner) to
match your station. Pick the one you're using; each antenna keeps its own
set of saved points.

**Live SWR.** The pill in the header shows your real **SWR** while you
transmit, colour-coded — **green** ≤ 1.5, **amber** 1.6–2.4, **red** ≥ 2.5
— so you can watch the match as you adjust the tuner. It reads "—" until
you key up.

**Reading the panel.** The big tiles show the **Input / Output / Inductor**
settings for the nearest stored point to where you're tuned:

- **Green "exact match"** — you're sitting on a saved frequency; the tiles
  are exactly what you set last time. Dial in those three numbers and you're
  matched.
- **Amber "nearest …"** — you're between saved points; the tiles show the
  closest one, with a **±x.x kHz** badge telling you how far off it is
  (**+** = the saved point is *above* you on the band, **−** = *below*). It's
  a starting position — set those numbers, then fine-tune to your SWR.
- The list below shows just the points that **bracket** your dial — the one
  just below and the one just above (and the exact one if you're on it). No
  endless scrolling: the complete table lives in **Settings → Tuner**.

**Saving a point.** When the amber banner says nothing's stored near you (or
you've improved a match), click **Store this freq** (or **Add current
freq**), type your Input / Output / Inductor numbers (whatever your tuner
shows — they're free text), and **Save**. Storing on a frequency you already
have overwrites that point.

**Editing / deleting.** Click **✎** to edit. Then **click a single row** to
make just that row editable (so you can't fat-finger a neighbour); change a
cell and click away to save. The **−** button on that row asks **✓ / ✗**
before it deletes. For bulk maintenance — add, rename antennas, delete,
clear, or set the "nearest match" window (how close counts as *exact*) — use
**Settings → Tuner**.

**Collapse** (**▸ / ▾**) tucks the panel down to just the SWR pill and the
match tiles for a compact at-a-glance reminder.

> The match is by **dial (carrier) frequency** — it reads the same in USB or
> LSB, which is correct: an antenna tuner matches the RF frequency, not the
> sideband.

---

## Frequency calibration (WWV / time station)

Your radio's reference oscillator (the HL2's TCXO) is a hair off, so a signal
on a *known* frequency lands a few hertz from where the dial says. Frequency
calibration measures that error against a rock-solid reference — a time
station like **WWV**, **WWVH** or **CHU** — and nudges every RX **and** TX
frequency by a tiny factor so the dial reads true.

> **`1.00000000 = no correction`** — exactly how the radio tunes out of the
> box. You can always get back there, so you can't paint yourself into a
> corner. Most HL2 boards are within a hertz or two on the HF bands and never
> need this; it's here for the operator who wants the dial spot-on, or who
> works a mode where a few Hz matters.

There are two ways in, and they run the **same measurement** — pick whichever
you like:

### The guided panel (Freq Cal chip)

Open **Options: → Freq Cal** in the header. A floating instrument appears:

1. **Tap a time station chip** (only the ones your region can actually hear
   are shown — see [Band plan (Region)](#band-plan-region)). Lyra saves your
   current dial and mode, switches to **USB**, and tunes about 1 kHz below the
   carrier so you hear it as a steady ~1 kHz tone.
2. Watch the **hero readout**: the carrier marker walks across a centre-null
   scale and the big **"Hz off"** number colours green as the error nulls
   toward zero. `SNR ✓` means the carrier is strong enough to trust.
3. Press **Apply** to take the suggested correction. **Stop** (or closing the
   panel) puts your dial and mode back exactly where they were.

### Settings → Calibration (no visual)

Same flow without the moving dial, plus the manual controls:

- **Time station** dropdown — pick one and Lyra auto-tunes USB and starts
  measuring, just like the panel. Or type an **exact frequency** by hand for a
  station that isn't listed.
- **Measure** arms/disarms; **Apply suggested correction** takes the reading.
- **Set correction** — type a factor by hand (e.g. copy a known-good value
  from another app as a starting point).
- **Reset to 1.0** — always returns to how the radio tunes today.
- **Restore previous** — one-level undo of the last change.

### Good to know

- **USB on every band.** Use USB even on 2.5 / 5 MHz where you'd normally run
  LSB. WWV is AM, and calibrating in USB on every band keeps the sign of the
  correction consistent.
- **Filter and sample rate.** Use a normal SSB filter (**RX BW ≈ 2.4–4 kHz**)
  so the ~1 kHz carrier sits well inside the passband — a tight CW filter can
  clip it. **Sample rate makes no difference**: the measurement reads the
  demodulated audio, not the raw spectrum.
- **No Reset needed to re-calibrate.** Apply *composes* onto whatever
  correction is already in effect, so measuring again from a non-1.0 factor is
  exact in one shot — it won't push you back toward 1.0.
- **Restore previous holds one level.** It remembers the single value in effect
  before your last change. Reset moves your current correction *into* that slot
  (it doesn't erase it), so Reset then Restore-previous brings it back — but a
  Reset followed by a fresh Apply overwrites the slot with 1.0.
- **The correction is stored per install** and applied inside the one frequency
  choke point every tune path funnels through, so RIT/XIT, CTUN, split and
  PureSignal all track it automatically.

> Right-click the **Freq Cal** chip for this help.

---

## Profiles (TX/RX chain presets)

A **profile** is a named snapshot of your operator TX/RX *signal chain* —
recalled as a single unit so you can flip between, say, an SSB-voice
setup and an FT8/digital setup without re-touching a dozen controls. This
is the openHPSDR "TX profile" idea, done Lyra-native.

**What a profile stores:** RX bandwidth, TX bandwidth + the BW-lock,
filter low edge; mic source, mic gain, 20 dB mic boost; the Tune-drive
toggle + tune-drive %, and the TX drive level (your power knob); the TCI
RX and TX gains; the **VAC source** (VAC1 enable, auto-enable-for-digital,
and the VAC RX/TX gains); AGC mode; auto-mute-on-TX; and the TX
safety-timeout (minutes + bypass). Because mic source *and* VAC enable are
both stored, one profile can flip you from a TCI/voice setup to a
VAC/digital setup as a unit — see
[Digital modes over VAC](#digital-modes-over-vac-virtual-audio-cable).

**What a profile deliberately does NOT store:**

- **The operating mode / sideband.** Like the reference's TX profiles, a
  profile is a pure signal chain — recalling one (manually *or* via
  auto-recall) **never changes your mode or which sideband you're on**.
  It applies the chain and leaves you exactly where you were tuned.
- **Safety / input-method settings that are global by design:** PA enable,
  hardware-PTT-input enable, and the space-bar-PTT toggle all live in
  [Settings → Hardware](#settings--hardware) and are *never* swept by a
  profile (you don't want a recalled preset silently arming your PA or
  changing how you key).
- **EQ, Speech, the Combinator and Plating** now ship as live TX DSP panels
  (see [TX DSP rack](#tx-dsp-rack-eq--speech--combinator--plating)), but
  their settings are **not yet swept into profiles** — that field lands
  with a future profile update, and older saved profiles migrate forward
  automatically. A separate monitor output remains **reserved** for the
  same reason. (VAC is no longer reserved — it's stored as of v0.2.4. Audio
  *device* names stay global station setup, not per-profile, since they're
  machine-specific.)

### The Profiles dock (front panel — quick recall)

A small dockable bar (show/hide it from the window's dock menu, like the
TX or Display panels). It has just three things:

- **Profile dropdown** — pick a saved profile to recall it instantly.
- **● modified dot** — lights **orange** when your live chain differs from
  the active profile (i.e. you've changed something worth saving); grey
  when they match. Hover it for which profile it's comparing against.
- **Save** — opens the **Save Profile** dialog: either *overwrite the
  active profile* with your current settings, or *give it a new name* —
  and (optionally) tick which **mode family** should auto-recall it. That
  last part means you can set up a chain for the mode you're on and save
  it as a brand-new bound profile without ever opening Settings.

### Settings → Profiles (the full editor)

The complete manager lives in **Settings → Profiles**:

- A **list** of your profiles — each with a **radio button**; click one to
  select it. The active (on-air) profile shows a green **`ACTIVE`** badge;
  the startup default is tagged `[default]`.
- **Save** (overwrite active), **Save As…** (capture current settings to a
  new name), **Load** (recall the selected profile), **Rename…**,
  **Delete**, and **Set Default** (apply this profile automatically every
  time Lyra starts).
- A status line showing the active profile and a `● modified` flag.
- **View & quick edit** — under the list, a panel shows the selected
  profile's core chain (**RX/TX bandwidth**, filter low, **Lock TX/RX BW**,
  **mic source / gain / boost**, **AGC**, **Leveler**, **PHROT**,
  **auto-mute on TX**). Change any of them and click **Apply changes** to
  save them back into that profile; **Revert** discards your edits. A
  `● You have unsaved changes` notice appears while edits are pending. This
  edits the **saved** profile only — it does **not** change what's on the
  air; click **Load** to apply it live. (The RX and TX *filter low* boxes
  currently share one value, so editing either moves both.)
- **Auto-recall by mode family** — one dropdown per family:
  **CW, SSB, Digital, AM, SAM, DSB, FM**. Bind a profile to a family and
  it's recalled automatically whenever you switch into a mode in that
  family. Sidebands collapse on purpose — **USB and LSB both map to SSB**,
  **CWU/CWL → CW**, **DIGU/DIGL → Digital** (the chain is identical
  regardless of sideband), while AM / SAM / DSB / FM each stand alone. Set
  a family to **(none)** to disable auto-recall for it.
- **Companion app** — optionally launch a digital-mode program (VarAC,
  MSHV, WSJT-X, …) when you **explicitly select** this profile from the
  front dock or the **Load** button. Tick **Launch a program when this
  profile is selected**, set a friendly **name**, **Browse** to the
  `.exe`/`.bat`/`.cmd`, add any command-line **arguments**, and a **start
  delay** (a couple of seconds lets Lyra's CAT server and VAC come up
  first). **Test launch** tries it now. It fires *only* on an explicit pick
  — never on the mode-family auto-recall above, and never at startup — and
  it never closes the app for you when you switch away (close it yourself).
  This binding is **per-PC only**: it lives on your machine, is never
  written into the profile, and is never exported in a shared `.lyra` — a
  shared profile carries the DSP chain only, never "run this program."

### Good to know

- **Recall is blocked while transmitting.** Loading a profile is a no-op
  while you're keyed — Lyra never switches mic source or bandwidth mid-TX
  (the same safety rule that governs every chain change). Drop PTT, then
  recall.
- **Switching is always manual** *except* for the per-family auto-recall
  table you configure above — nothing changes your profile on its own
  (e.g. Lyra never picks a profile based on who you're working).
- A typical setup: a **"Voice"** profile bound to SSB (your mic, mic gain,
  speech bandwidth) and a **"Digital"** profile bound to Digital (TCI mic
  source, flat gain, wide filter) — then changing mode from USB to DIGU
  flips the whole chain for you, but leaves you on the frequency and
  sideband you chose.

---

## Auto-starting apps at launch

**Settings → Hardware → "Startup — launch apps when Lyra opens"** lets you
have companion programs open automatically a few seconds after Lyra starts
(long enough for Lyra's CAT and TCI servers to come up first).

- **SDRLogger+** — Lyra's companion logger. If it's installed, the status
  reads *installed* and you can tick **Auto-start with Lyra**; the button
  launches it now. If it isn't installed, the button opens the download
  page so you can grab it.
- **Two generic slots** — tick the box, **Browse** to any program
  (`.exe`/`.bat`/`.cmd`), and it starts with Lyra too.

These are **per-PC** settings, kept on your machine and never written into
an exported profile. Launches are fire-and-forget — closing Lyra never
closes the apps; close them yourself. (This is separate from the per-profile
**Companion app** above, which fires only when you *select* that profile.)

---

## Solar / Propagation panel

A compact strip showing current **HF propagation** conditions from
[HamQSL](https://www.hamqsl.com/) (N0NBH's solar data):

- **SFI** (Solar Flux Index), **A** (A-index), **K** (K-index) — each
  colour-coded green / amber / red so you can read conditions at a glance
  (higher SFI is better; lower A and K are better). Hover the **SFI** box
  for the full picture — sunspot number, X-ray level, solar wind, and when
  HamQSL last updated.
- **A 10-band heat-map** — `160` through `6` m, each tinted **green
  (Good)**, **amber (Fair)**, **red (Poor)**, or **gray (no prediction)**.
  Hover a band for its rating.

The ratings are shown for **your current day or night** — Lyra works out
whether it's daylight at your location and picks HamQSL's day or night
figures accordingly. For that to be right, set your **grid square** (or
manual lat/lon) in
[Settings → Hardware → Operator / Station](#operator--station); without a
location it assumes daytime. HamQSL doesn't predict **160 m** or **6 m**,
so those two always show gray.

The data is fetched when Lyra starts and refreshed about every 15 minutes
(the day/night split updates on its own as the sun rises and sets). It's
read-only and needs no account or key — just an internet connection. If
the feed can't be reached, the last-known figures stay on screen and the
SFI tooltip notes the error.

**NCDXF beacon auto-follow.** The **Follow** button on the right of the
panel lets you chase a single NCDXF beacon station through the rotation.
Pick a station (e.g. **4U1UN**) from the dropdown and Lyra automatically
tunes (in CW) to whichever of the five beacon bands that station is
transmitting on *right now* — 20 → 17 → 15 → 12 → 10 m — re-checking every
second and hopping with it through the 3-minute cycle. It's a quick way to
hear how one station is propagating across the bands. Set it to **Off** to
stop; your choice is remembered between sessions. (While following, Lyra
re-tunes on its own, so manual tuning won't "stick" until you turn Follow
off.)

If you don't want the panel, close it like any other (the **View** menu
re-opens it). It's a dock — drag, float, or tab it wherever you like.

---

## Weather alerts

Lyra can watch for nearby **lightning**, high **wind**, and **severe-storm
warnings**, and show flashing badges in the header so you know to think
about your antennas. Turn it on and configure it in
**[Settings → Weather](#settings--weather)**.

> ⚠ **Advisory only.** These alerts are a convenience, **not** a substitute
> for official warnings or your own judgment. You must accept a short
> disclaimer before enabling them.

**What you'll see** — badges appear toward the right of the header when an
alert is active, and **flash** on the most serious tier:

- **⚡ Lightning** — shows the closest strike distance + compass bearing.
  Yellow ≈ far, orange ≈ mid, **red = close** (flashing).
- **💨 Wind** — shows gust (or sustained) speed. The badge appears only
  once wind **reaches the thresholds you set** (Settings → Weather):
  **orange** when sustained or gust crosses your value, **red = extreme**
  (your value + 15, flashing). In calm air below your thresholds there's
  no wind badge at all.
- **⚠ Severe** — an NWS thunderstorm/severe warning is active (red,
  flashing). Hover for the headline.

**Where the data comes from** depends on which sources you enable
(Settings → Weather): **Blitzortung** (global lightning, free),
**NWS / weather.gov** (wind + severe + live station wind, free, US), and
your own **Ambient Weather** or **Ecowitt** station (needs that account's
keys). Lyra needs your **location** to know what's "nearby" — set your
grid square in [Settings → Hardware → Operator / Station](#operator--station).

**Notifications** — optionally a desktop pop-up and an audible chime when
an alert first crosses a tier (with a cool-down so it doesn't nag). Lyra's
own weather pop-ups are titled **"Lyra — …"** so you can tell them apart
from other notifiers (your Ambient app, a Windows weather widget, etc.).
The wind pop-up only fires when your **local** readings cross your
thresholds; an area **NWS High/Extreme Wind Warning** pops a toast only if
you opt in (**"Pop NWS High/Extreme Wind Warnings"**, off by default — the
badge still reflects it either way). Toggle the desktop/chime options in
Settings → Weather, and use **Send test alert** there to see the badges
light up.

---

## Updates

Lyra checks GitHub for new releases so you don't miss one.

- A quiet check runs a few seconds after launch. If a newer version
  exists, you get a one-time pop-up (**Open release page / Remind me later
  / Skip this version**) and an **⬆** indicator stays in the toolbar.
- Check whenever you like: **Help → Check for Updates…**.
- Downloads are on the project's GitHub Releases page; grab the new
  installer and run it over the top.

---

## Backing up & sharing your settings

Everything lives on the **Settings → Backup & Restore** tab (the File menu's
**Export settings…** / **Import settings…** do the same jobs).

Lyra can save your **entire configuration** — panel layout, saved layouts,
and every preference (visuals, mode/bandwidths, TX chain, CW, spots,
weather…) — to a single portable `.lyra` file, and it keeps **automatic
dated snapshots** so you always have a known-good setup to fall back to.

**Export** — *Export to a file…* writes a `.lyra` you choose. Keep it as a
backup, or hand it to another operator for your exact setup.

**Snapshots** — Lyra drops an **automatic snapshot every N launches** (set
the cadence and how many to keep on the tab; 0 launches turns auto off).
*Save snapshot now…* takes one on demand and lets you name it — named
snapshots are kept until you delete them; automatic ones roll over. Pick one
from the list and **Restore selected…**, **Export selected…** (copy it out to
a USB stick or cloud folder), or **Restore from file…** for a `.lyra` you
saved earlier.

Snapshots live in your Windows app-data folder — the tab shows the exact
path and an **Open folder** button. Crucially, that folder **survives an
uninstall / reinstall** (and the registry reset), so if Lyra ever gets wiped,
do a fresh install, go to Backup & Restore → **Restore from file…**, and pick
your last snapshot out of that folder to get your setup back. (Copying that
folder somewhere safe now is a good habit.)

**Selective restore — the important part.** Restoring doesn't have to be
all-or-nothing. When you restore, Lyra shows a **checklist of sections**
(Audio & VAC, TX audio chain, CW, Calibration & meters, Spots, Display,
Panel layout, and so on). Tick only the groups you want back; everything
unticked stays exactly as it is now. So if a change to one subsystem went
sideways, you can roll back **just that part** without disturbing the rest.
(Panel layout is unticked by default, since restoring it rearranges your
window.)

**Safe to share.** A backup never includes your **radio's address** or the
**graphics backend** — those are per-machine, so a file is always safe to
move to another PC or send to a friend.

**Share just a layout.** The same tab has a **Share a layout** group for when
you only want to pass along your *screen arrangement* — where the panadapter,
meters, tuning, and other panels sit — not your settings. **Export layout…**
saves it to a small **`.lyralayout`** file (post it with a screenshot on
Discord, or email a friend your setup); **Import layout…** drops a
`.lyralayout` someone sent you straight onto your screen. Only the layout
travels — never your settings, radio address, or window size — so a shared
layout lands cleanly on any display. Importing applies it live; use **View →
Layouts → Save current layout to** a slot afterward if you want to keep it.

---

## Operating with an external amplifier (hot-switch protection)

If you're driving an external solid-state HF linear amplifier from
the HL2, **read this section first.** Hot-switching — applying RF
into a T/R relay that hasn't finished switching — can destroy a SS
linear's PA. Lyra has two layers of protection against this; they're
load-bearing, not cosmetic, and the defaults are chosen for typical
1 kW SS HF linears.

### How the two layers compose

| Layer | Knob | What it does |
|---|---|---|
| 1. **Scheduling** | RF Delay (default 50 ms) | After the wire-MOX bit goes hot, Lyra waits 50 ms before allowing any RF onto the antenna. Gives the external amp's T/R relay time to fully close. |
| 2. **Amplitude shape** | Fade-In Duration (default 50 ms) | Even WITHIN the RF Delay window, the host-side I/Q amplitude rises smoothly from zero via a cos² envelope. Redundant protection against any residual MOX-bit / relay timing skew. |

Both knobs live on **[Settings → TX](#settings--tx-mic--alc-tr-sequencing--cos-fade)**.

### Workflow before the first on-air keying

The discipline that's saved more amps than anyone wants to admit:

1. **Dummy load on the antenna port.** Until you've confirmed the
   full chain works the way you expect, the HL2 talks to a 50 Ω
   resistor, not your antenna. A dummy load tolerates hot-switching;
   a relay-bypassed antenna won't.
2. **External amp OFF / bypassed.** First-RF goes to the dummy load
   straight from the HL2's on-board PA (a few watts). Confirm
   wattmeter behaviour, panadapter trace, and that TUN / MOX both
   ramp cleanly.
3. **PA Enable OFF in [Settings → Hardware → Transmit](#transmit-pa-enable--safety-timeout--hardware-ptt).**
   Run through your keying ergonomics first — TUN button, MOX
   button, space-bar, drive-slider movement — with **no actual RF**.
   You should hear the HL2 relays click and see the wire-MOX state
   flip; the wattmeter should read zero.
4. **PA Enable ON, Drive low (5–10 %).** Key TUN once. Watch the
   wattmeter trace ramp up smoothly over ~50 ms, hold steady, then
   ramp down smoothly. **No clicks, no spikes** at either edge.
5. **Verify your [Settings → TX](#settings--tx-mic--alc-tr-sequencing--cos-fade) values match your amp.**
   The defaults are bench-safe for typical 1 kW SS linears.
   If your amp's documentation specifies a T/R relay settle time
   greater than 30 ms, **leave RF Delay and Fade-In Duration at
   their defaults or HIGHER**. Only consider lowering them if your
   amp's spec explicitly says it can handle a faster onset (e.g.
   QSK-rated amps with vacuum relays).
6. **External amp ON / inline.** Re-do the TUN test with the amp in
   line, still on the dummy load. Confirm the amp keys and reads
   the right output for your drive level. Watch for any sign of
   relay arcing or other distress.
7. **Antenna connected.** Final step. By the time you reach this,
   the chain has been verified end-to-end.

### What to tune (and what NOT to tune)

| If you're seeing… | Tune this | NOT this |
|---|---|---|
| Amp T/R relay arcing on key-up | **Space MOX Delay** (and Fade-Out Duration must stay ≤ Space MOX Delay) | Don't touch RF Delay — keyup arcing is a different problem from keydown hot-switching |
| Amp shows symptoms of hot-switching on key-DOWN | **RF Delay** (raise it) — match your amp's T/R relay settle spec | Don't reduce Fade-In Duration as a "fix" — it's the second protective layer |
| Audible click in your monitor at keydown | **Fade-In Duration** — should be ≥ ~30 ms for clean SSB-band sound | Don't extend RF Delay just to remove a click — that's the amplitude envelope's job |
| Audible click in your monitor at keyup | **Fade-Out Duration** (raise it up to Space MOX Delay) | Don't extend Space MOX Delay just for a click — that lengthens the re-key window too |

### When Lyra is killed mid-transmit (process crash, power glitch)

The HL2 gateware has its own watchdog: no EP2 keepalive frames for
N seconds (gateware-specific, typically ~1 sec) → gateware drops PA
bias. This is your last-resort safety if Lyra dies while keyed.

**But it's slow.** A second of stuck carrier into an antenna is
enough to embarrass yourself or worse. The discipline above —
dummy-load → external-amp-off → progressively widen — exists
because the gateware watchdog isn't fast enough to substitute for
operator-side discipline.

---

## Settings → Hardware

The **Hardware** tab (Settings — **Ctrl+,**) holds your station identity
and connects to the radio + optional station hardware.

### Operator / Station

Your identity and location, used by the weather alerts (and, in time, the
solar panel and logging):

- **Callsign** — your call (stored uppercase).
- **Grid square** — your Maidenhead locator (e.g. `EN82`, `EN82dk`). When
  it's valid, Lyra computes your latitude/longitude from it — shown live
  in **Computed lat/lon**.
- **Manual lat / lon (°)** — a fallback if you'd rather not use a grid (or
  don't have one). Used only when the grid is blank/invalid.

Grid wins when it's valid; otherwise the manual lat/lon is used. Either way
that's the location the weather sources use for "nearby."

### Band plan (Region)

**Region** — your IARU region (**US / IARU Region 2**, **IARU Region 1**,
**IARU Region 3**, or **None**). This selects which amateur band-plan the
panadapter overlay draws. Set it to **None** to turn the whole overlay off.

**Country** — an optional refinement under Region, for the bands where a
country's allocation differs from its IARU region (today this is **60 m**).
Leave it on **Auto (use region)** and you get the region default; pick your
country to override just the bands that differ:

- **Auto** — US gets the five fixed **60 m channels**; IARU R1 / R3 get the
  contiguous **WRC-15 band (5351.5–5366.5 kHz)**.
- **United Kingdom** — replaces 60 m with the UK's own set of permitted
  segments (per the RSGB / Ofcom band plan), which differ from the plain
  WRC-15 band.
- **Canada** — uses the WRC-15 60 m band rather than the US channels (Canada
  is in IARU Region 2 but does not use the US channel plan).

The overlay paints a thin strip across the **top of the panadapter**:

- **Sub-band segments** — a coloured bar showing the mode allocations in
  view: **CW** (blue), **digital** (magenta), **SSB** (green), **FM**
  (orange). The label (CW / DIG / SSB / FM) shows when the segment is wide
  enough on screen.
- **60 m channel markers** — on the US 60 m band the five channels show as
  labelled **CH1–CH5** bars. The labels are always visible (the channels are
  narrow), and you can **click a channel to QSY** straight to its USB dial
  frequency.
- **Digital landmarks** — small **▼** markers at the common calling
  frequencies (**FT8 / FT4 / WSPR / PSK**), gold. **Click a marker to tune
  straight to it** (and Lyra switches to the suggested mode).
- **NCDXF beacon markers** — the five International Beacon Project
  frequencies, in cyan. **Hover one** and the tooltip shows the station
  that's transmitting *right now* (the 18 worldwide beacons rotate every
  10 seconds) — e.g. "Now: 4U1UN — United Nations, NY". Click to QSY
  there in CW.
- **Band-edge warning lines** — red dashed lines at each band's edges, so
  you can see at a glance when you're tuning toward the edge of an
  allocation.

Each of those four layers has its own checkbox here so you can show only
what you want. They're all on by default. **Segment colors** — the four
swatch buttons (CW / DIG / SSB / FM) let you recolor each mode category;
click a swatch to pick a color, or choose the original color to clear the
override.

As you tune, Lyra shows a brief message at the **bottom of the window**
when you cross a band edge — "In band: 40m (US)" when you're inside an
allocation, or "⚠ Out of band — X.XXX MHz is outside the US amateur
allocations" when you're not. (Gated on the band-edge layer above.)

> The band plan is **advisory only** — the HL2 is unlocked and Lyra will
> tune anywhere it can receive. Sub-band boundaries vary by license class
> and country and change over time; verify against your own regulator
> before transmitting near an edge. The strip is a navigation aid, not a
> legal reference.

### Shortwave broadcasters (EiBi)

Lyra can overlay the **EiBi shortwave-broadcast schedule** on the
panadapter so you can see *who's on the air* across the SW broadcast and
utility ranges. Each station shows as a small tick + name:

- **Cyan** = broadcasting **right now** (the current UTC time falls inside
  the station's scheduled window for today).
- **Grey** = scheduled on this frequency but **off-air** at the moment
  (only shown if you turn off "Hide off-air").
- **Hover** a station for its frequency, language, target area, and on-air
  status; **click it to tune there in AM**.

Turn it on in **Settings → Bands → SW Database**:

- **Enable EiBi shortwave overlay** — master on/off (off by default).
- **Update database now** — downloads the current season's schedule
  (~3 MB) from eibispace.de. **You must do this once** before anything
  appears — the data isn't bundled (EiBi's licence is free but
  no-redistribution). Re-run it when the season changes (roughly April and
  October).
- **Load CSV file…** — the fallback if the download fails. eibispace.de's
  security certificate expires periodically, which can block the in-app
  download; when that happens, download the `sked-<season>.csv` from the
  EiBi website in your browser, then point Lyra at it with this button.
  (Lyra copies it into its own folder, so you can delete the download
  afterward.)
- **Hide off-air** — show only stations broadcasting now (on by default).
- **Show in all bands** — by default the overlay is hidden inside the
  amateur bands (so it doesn't clutter ham operating); tick this to show it
  everywhere.
- **Minimum transmitter power** — hide the weak stations; show only
  ≥50/100/250 kW transmitters to cut clutter.

> EiBi schedules are a planning aid — actual broadcasts vary with
> propagation, season, and last-minute changes. A cyan station may be
> inaudible (wrong band conditions) and you may hear stations the database
> doesn't list. Data © the EiBi project (eibispace.de).

### Radio

Find and connect to your HL2 / HL2+. **Discover** scans the LAN, **Open**
connects to the selected radio (or just **double-click** it), **Close**
disconnects, and the status line shows what you're connected to. The
**connected radio is shown green and bold** in the list, so with several
radios it's obvious which one is live. Lyra remembers the last radio and
shows it here on launch. (The toolbar **▶ Start / ■ Stop** does the same
thing.)

**Multiple radios.** Keep as many radios in the list as you like and switch
between them: select one (or double-click) → **Open**; to change radios,
**Close** the current one first, then Open another. Lyra connects to one
radio at a time.

- **Add by IP** — type a radio's address (e.g. `192.168.1.50`) and click
  **Add** for a radio **Discover** can't reach: a fixed-IP HL2, one on a
  different subnet, or a network where broadcast is blocked. Lyra sends a
  direct probe to that address and fills in the real board / gateware / RX
  count if it answers; either way the entry is added so you can **Open** it.
- **Discover** now broadcasts two ways (the limited and the subnet-directed
  broadcast), so it finds radios on networks where one form is blocked.

- **Auto-start radio on launch** (checkbox, default **ON**) — when on,
  Lyra connects to the last radio automatically at startup (the historical
  behaviour). Untick it to have Lyra open without connecting; you then
  click **▶ Start** when you're ready.

### Transmit (PA enable + safety timeout + hardware PTT)

The deliberate-arm safety gates for getting on the air. Both are
intentionally OUT of the operating-time TX panel — they're set-and-
forget config you arm once when you're ready to transmit, not knobs
you touch between QSOs.

- **Enable PA** (checkbox, default **OFF**) — toggles the HL2 gateware's
  PA bias. With this OFF, keying MOX or TUN gives you the relay click
  and the wire-MOX state but **zero RF** (the gateware never biases
  the PA). With this ON, MOX + non-zero Drive % produces real RF. **The
  safest first-time-on-the-air bench gate**: leave OFF for relay /
  panadapter / wattmeter wiring checks; turn ON only after you're
  confident in your Drive setting, fade defaults, and amp configuration.
  Lyra defensively clears PA on every stream close and open — restart
  Lyra and PA is OFF again, no matter where you left it.
- **TX safety timeout** (spin box, default **600 s = 10 min**, range
  60 – 1200 s) — if the radio stays continuously keyed past this
  duration, the FSM auto-releases MOX through the normal keyup
  sequence (no shortcut on the wire — full TR-delay unwind, ATT-on-TX
  restore, the works). A pop-up toast notes the auto-release.
- **Bypass safety timeout** (checkbox, default OFF) — for long-form
  AM ragchews or slow CW beacons where the 10-minute default is
  genuinely too short. Bypass disables the timer entirely. **Use
  with intent** — a stuck PTT or a software fault now has no host-
  side rescue (the HL2 gateware watchdog is still in play, but
  treats EP2 keepalive as the only liveness signal).
- **Enable hardware PTT input (foot switch / hand mic)** (checkbox,
  **default OFF**) — when ticked, the HL2's hardware PTT-input pin
  is read on every status frame and an edge (press / release) is
  forwarded to MOX, so a foot switch, hand-mic PTT, or external
  mic-button keys Lyra just like the on-screen MOX button.
  **⚠ Bench-verify before enabling.** Some HL2 units (notably most
  observed HL2+ / AK4951 units) carry a non-zero PTT-input level at
  RX rest, so an always-on forwarder would mis-read it as a press
  and produce a phantom-TX surge the moment you launch Lyra.
  Default OFF lets you control when the read goes live: confirm
  with a scope or with the foot-switch unplugged that the pin
  reads stable 0 at rest, then tick this box. If your foot-switch
  press doesn't key after enabling, the issue is upstream of Lyra
  (HL2 connector / switch / cable / wiring) — Lyra can't paper
  over a dirty level at rest.
- **RX resume delay after unkey** (spin box, default **50 ms**, range
  0 – 500 ms) — how long RX audio stays muted *after* you unkey, so the
  transmit tail still working through the receiver's DSP pipeline drains
  out as silence and the T/R relay settles before you hear RX again.
  Removes the "thud" and the quick echo of your own voice on unkey.
  Raise it if you still hear a bump or echo; lower it for snappier
  break-in. **0** resumes RX instantly (the old behaviour). Works with
  the **Auto-mute RX while transmitting** option above — the auto-mute
  holds RX silent *during* TX, this delays the *return* to RX after.

> **Why these are in Hardware, not in TX:** Settings → TX is for
> *timing* knobs you might tune to match an external amp's switching
> spec. Settings → Hardware → Transmit is the *arm/disarm* surface
> for "ready to actually radiate" — different operational gesture,
> different cognitive bucket. Both honour each other: PA off ⇒ no
> RF regardless of timing, and the timing knobs are inert when PA
> is off.

### External band-following (filter board / USB-BCD)

The external filter board (OC) and the USB-BCD amp band-code output now
live on their own tab — see [Settings → Filters / BCD](#settings--filters--bcd).

### Diagnostics (debug log)

Lyra runs without a console window, so if something misbehaves there's no
black terminal to copy errors from. Instead, Lyra keeps a **diagnostic
log** you can read, copy, and send to us.

- **Help → Show Log…** opens the log viewer — a scrolling view of this
  session's diagnostic messages with **Copy all**, **Save…**, and **Open
  log folder** buttons. It updates live while open.
- **Enable verbose debug logging** (here in Diagnostics, also a checkbox
  in the log viewer) turns on detailed capture. Leave it **off** for
  normal use — the log then keeps only warnings and errors and stays
  small. Turn it **on** when you're chasing a problem: enable it,
  reproduce the issue, then Copy or Save the log.
- A copy of the log is always written to a file on disk
  (`…/N8SDR/Lyra-cpp/logs/lyra-log.txt`, reachable via **Open log
  folder**), refreshed each launch — so even a crash leaves a trace.

**To capture a log for a bug report:**

1. Open **Help → Show Log…**.
2. Tick **Verbose debug logging** (bottom-left of the viewer). Leave the
   window open.
3. **Reproduce the problem** — make it happen, and let it run for 30–60
   seconds so it's well captured.
4. Click **Save…** (pick a filename like `lyra-log.txt`) — or **Copy all**
   and paste it into your message.
5. Send the log with a one-line note of what it was doing and what you
   expected instead.

### Getting help / reporting a bug

Testers and bug reports are welcome — Lyra is in active development.

- **Discord (preferred for questions & quick help):**
  [discord.gg/BwjsQvjcSc](https://discord.gg/BwjsQvjcSc) — questions, feature
  ideas, and getting unstuck.
- **Bug reports:** file a
  [GitHub issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues) (or post in
  Discord). Attach the **log as a text file** (use **Save…** in the log
  viewer — don't worry about the on-disk copy, the saved `.txt` is easiest
  to attach).

To make a report actionable, please include:

- **The diagnostic log** — saved as a `.txt` and attached (see the capture
  steps above).
- **Lyra version** — **Help → About**, or the installer filename.
- **Windows version** (e.g. Windows 11 23H2) and your **PC basics** (CPU,
  RAM) if it's a performance or audio-glitch issue.
- **Your radio** — **HL2 or HL2+**, and your gateware version if you know
  it.
- **What you did and what happened** — the steps to reproduce, and what you
  expected instead.

---

## Settings → Filters / BCD

Everything that makes an **external band-following accessory follow your
tuning** lives here: an external band-pass **filter board** on the HL2's
open-collector (OC) outputs, and a **USB-BCD** band code for a linear
amp. Leave the whole tab alone if you don't have either — nothing here
does anything until you enable it.

### Filter board — OC Control (J16 pins)

The HL2's **J16 open-collector (OC) pins** can drive an external
band-pass filter board (N2ADR or compatible) so its filters follow the
band you're on — front-end protection against strong out-of-band
signals (a nearby AM broadcaster, say).

- **Enable external filter board (N2ADR / compatible)** — turns the OC
  band-switching on. Off = the OC pins drive nothing (harmless with no
  board).
- **Live pins** (top-right) — the seven cells light to show which J16
  pins are being driven **right now**, on the wire. They follow the band
  as you tune and flip to the transmit pattern while you're keyed.
- **The per-band grid** — one row per band, with a **J16 Receive pins**
  block (green) and a **J16 Transmit pins** block (red), pins 1–7 each.
  Tick a pin to drive it on that band for RX and/or TX. Edits apply and
  save immediately. This is the heart of it: the grid is what tells the
  board which filter to select on each band.
- **N2ADR preset** — loads the standard N2ADR filter-board pin map (the
  default). **Clear all** — blanks every band (start from scratch).
- **Allow hot switching** — lets transmit-pin edits take effect while
  you're transmitting. Off (default) is safer — edits apply on the next
  key.

> ⚠ **Pre-antenna gate.** Before you key RF into a filter board, verify
> the pins in the grid match your board's physical wiring, at low power,
> **on every band**. Wrong pins = wrong filter for the band =
> out-of-band emissions or a blown LPF relay.

### Transmit pin action + external-PA gating

For each of the seven pins you can choose **when** its transmit pattern
engages, and whether it also gates an external PA:

- **Transmit action** — the condition that counts as "transmit" for that
  pin: **Mox**, **Tune**, **2-Tone**, or any combination (default
  **Mox / Tune / 2-Tone** = engage on all three).
- **PA·RX / PA·TX** — mark a pin as feeding an external power amplifier's
  enable on receive and/or transmit.

**Split pins (per-VFO)** is reserved — it activates with the second
receiver (RX2).

### USB-BCD (linear-amp band switching)

Drives a Yaesu-style **BCD band code** out an FTDI cable so an external
linear amp's auto-bandswitch follows Lyra's band. Tick **Enable
USB-BCD**, choose your **BCD cable**, and the **BCD output** readout shows
the current code. Two bands have no standard BCD code of their own, with an
option to borrow the adjacent band's filter:

- **60 m uses the 40 m filter (BCD 3)** — sends the 40 m code on 60 m
  instead of bypassing.
- **11 m uses the 10 m filter (BCD 9)** — sends the 10 m code on the
  11 m / CB band (the appropriate adjacent filter) instead of bypassing.

> ⚠ **Operate only within the maximum power and band limits permitted by
> your country / region's regulations.** This control selects an amp
> filter; it does not change what you are licensed to transmit.

> ⚠ **Verify before you key.** Confirm the wiring and do a low-power test
> on each band before transmitting at full power — the wrong code can
> route TX through the wrong filter.

(If the FTDI driver, `ftd2xx.dll`, isn't installed, this section says so
instead — install the FTDI D2XX driver to use USB-BCD.)

### Band-voltage output (fan-PWM pin)

Some amps, tuners, and antenna switches band-follow off a single **analog
band voltage** rather than serial CAT or logic pins. The HL2 gateware can
emit that voltage on its **fan-PWM pin** (the "band volts" feature in the
MI0BOT / Ramdor gateware builds), and Lyra can turn it on:

- **Output per-band analog voltage on the fan-PWM pin** — enables it. Off
  (default) leaves the pin as normal fan control.

> ⚠ **Trade-off:** while this is on, that pin outputs band voltage
> **instead of** fan-speed control. Only enable it if your wiring actually
> uses the band voltage. It also needs a gateware build that includes the
> band-volts feature.

This is independent of the OC/J16 pins above and the serial band-follow
used by an HL2+ AK4951 companion board (which drives a HardRock-50 / AH-4
automatically from the gateware — no setting needed here).

## Settings → Audio

Where Lyra sends received audio:

- **HL2 audio jack (AK4951)** — the default on HL2+ hardware. Audio is
  decoded on the PC, sent back to the radio, and played from the HL2's own
  headphone jack. Single-crystal, lowest latency, bypasses the PC sound
  card.
- **PC sound device** — pick any output Windows offers (sound card,
  headphones, a virtual audio cable to WSJT-X / FLDigi, etc.).

Pick your output device here; everyday **Mute** and **Vol** stay on the
[Audio panel](#audio-panel).

**Filter Low edge (RX + TX)** — single shared low cutoff for the
SSB / DIG audio bandpass on both receive and transmit. Range
**0–500 Hz**, default **100 Hz**.

- **100–200 Hz** suppresses 50/60 Hz mains coupling on the mic path
  and reduces low-end rumble on receive — safe choice for narrow
  comms.
- **50–70 Hz** preserves chest-resonance / low-end body for
  ESSB-style wide audio — verify your station isn't picking up
  mains hum at very low cuts.
- **0 Hz** opens the filter all the way down to the carrier (no
  low cut) — some operators want this for ultra-wide ESSB into a
  clean RF environment.

Applies to **SSB and DIG modes only** (USB / LSB / DIGU / DIGL —
the asymmetric passband case). CW is pitch-centred and AM / DSB /
FM are symmetric around DC; those modes ignore this setting.

You can also **drag the low edge of the passband rectangle** on the
panadapter (left edge on USB / right edge on LSB) to set this value
in real time. Dragging below 0 just pins at 0.

> **Profiles carry their own bandwidth.** A saved
> [profile](#profiles-txrx-chain-presets) stores its own RX + TX
> bandwidth + filter-low edge and applies them on recall (manual or
> per-mode-family auto-recall). This control is the live value used
> when no profile is active — save it into a profile to make it stick
> per setup.

### Virtual Audio Cable (VAC1)

VAC1 bridges audio between Lyra and a PC sound device — typically a
**virtual audio cable** (VB-Audio CABLE, VAC, etc.) — so a digital-mode
or soundcard program can hear Lyra's receive audio and feed Lyra its
transmit audio. As of **v0.3.0** both directions run on one full-duplex
audio stream, and receive audio is automatically muted out of the cable
while you transmit (no open-mic monitor feedback into the cable). For the
end-to-end digital-mode wiring see
[Digital modes over VAC](#digital-modes-over-vac-virtual-audio-cable); the
controls here are:

- **Enable VAC1 (RX→PC and PC→TX)** — master switch; powers both
  directions at once.
- **Auto-enable for digital modes (disable for others)** — when ticked,
  VAC1 turns on automatically when you switch to a digital mode
  (DIGU / DIGL) and off for every other mode, so moving into a digital
  setup opens the cable for you.
- **Driver** — the audio backend (PortAudio host API: **WASAPI**,
  DirectSound, MME, or WDM-KS) the VAC devices live under. **WASAPI is the
  right choice for virtual cables.** Changing the driver repopulates the
  Output and Input device lists below with that backend's devices;
  existing selections carry over by name where the device still exists.
- **Output device** — the PC output Lyra sends RX audio to (the cable's
  playback endpoint your digital app records from). Pick the **virtual
  cable, not your speakers**. `(none)` leaves the RX→PC direction off.
- **VAC RX gain** — trims the level of the RX audio Lyra feeds into the
  cable.
- **Input device** — the PC input Lyra reads transmit audio from (the
  cable's recording endpoint your digital app plays into). Used **only
  when Settings → TX → Mic source = "PC Soundcard (VAC1)"**. `(none)`
  leaves the PC→TX direction off.
- **VAC TX gain** — preamp on the captured PC mic audio before it enters
  the transmit chain.
- **Combine input (mono)** — sum the captured left + right channels to
  mono (use when the app puts its modulator audio on a single channel).

> **For a full receive-and-transmit bridge, select BOTH an Output and an
> Input device** — leaving either on `(none)` disables that direction.
> Transmit also requires **Settings → TX → Mic source = "PC Soundcard
> (VAC1)"**; with the mic source on anything else (the codec mic, or TCI)
> a VAC transmit produces **no power**. The
> [Digital modes over VAC](#digital-modes-over-vac-virtual-audio-cable)
> section walks the whole setup, including the no-power fix.

---

## Settings → DSP (filter type)

This tab sets the DSP **filter type** per mode family — and it is
deliberately the only DSP knob here. If you've used Thetis you'll
remember its **DSP → Options** page full of buffer-size, filter-size
(taps) and FFT-mode controls; Lyra does not copy that, on purpose.

### What the control does

For each mode family — **Phone** (SSB / AM / SAM / DSB), **FM**,
**CW** (RX only), and **Digital** (DIGU / DIGL / DRM) — you pick the
filter type for RX and (where applicable) TX:

- **Linear Phase** *(default)* — the cleanest, perfectly symmetric
  filter response. This is what every install ships with, and what
  Lyra has always used.
- **Low Latency** — a *minimum-phase* filter that trims the filter's
  group delay (the built-in delay every sharp filter adds). Useful
  where round-trip timing matters: digital modes (VarAC / WSJT-X
  acking faster) and CW monitoring. The trade-off is a little phase
  asymmetry — most operators won't hear it on SSB.

It's **opt-in**: out of the box every cell is Linear Phase, so an
upgrade changes nothing. Flip a family to Low Latency only if you
want it. The choice applies automatically when you change mode and is
remembered across sessions. **CW transmit** has no entry — CW keying
is handled by the keyer, not a TX filter.

### Why Lyra doesn't have Thetis's buffer / taps / FFT options

Thetis exposes buffer size, filter (tap) size and FFT options because
it was built for the PCs of its era, where those were genuine
CPU-versus-latency trade-offs you had to balance by hand on slower
machines.

Lyra is a native **Qt 6 / C++23** application built for **modern
hardware** — multi-core CPUs, plenty of RAM, and a **Vulkan** GPU
rendering pipeline. It simply runs the DSP at a high-quality fixed
buffer all the time, so there is no trade-off left to make: you get
low latency *and* sharp filters at once, with no tuning required. The
one setting that still changes the *feel* on a fast machine is filter
*type* (group delay) — which is exactly what this tab exposes.
Everything Thetis made you tune for performance, Lyra handles
automatically.

## Settings → TX (Mic + ALC + Leveler, TR sequencing + cos² fade)

> **⚠ Read [Operating with an external amplifier](#operating-with-an-external-amplifier-hot-switch-protection) FIRST**
> if you're driving a solid-state HF linear from the HL2. The TR-
> sequencing + fade defaults on this tab are **hot-switch-safe** for
> typical 1 kW SS linears; reducing them without knowing your amp's
> T/R relay settle spec can damage the PA. Tooltips on the most
> dangerous knobs carry the warning; don't dismiss it.

**Layout (v0.2.x §15.28 polish):** the tab is now organized in **two
columns** with cyan column headers at the top — **TIMING** (left:
TR Sequencing + Amplitude Envelope + Tune — set once for your amp +
station) and **AUDIO + GAIN** (right: Mic + ALC + Leveler — operating
gain stages you tune actively).  All values persisted across Lyra
restarts; live-apply (changes take effect on the next MOX edge).

### Mic + ALC (TXA input + output gain stages)

The full operator-tunable TX-input-to-modulator-output gain chain,
rendered top-to-bottom in **signal-flow order** — what the audio
sees first is at the top, what it sees last is at the bottom:

```
Mic source → Mic Boost (+20 dB HW) → Mic Gain (SW) → Leveler (optional) → bp0 → ALC → wire
```

| Knob | Default | What it controls |
|---|---|---|
| **Mic source** | Mic In (codec) | Picks the audio source driving the TX chain. **Mic In** = the HL2 / HL2+ codec mic input (the v0.2.x default; this is the hand-mic / headset-mic / desk-mic path). **TCI** = inbound TX_AUDIO_STREAM from a digital-modes TCI client (MSHV / JTDX / FlDigi); pick this for digital-mode operation so the client's modulator audio replaces the hand-mic. **PC Soundcard (VAC1)** routes TX audio captured from a PC audio cable (a Virtual Audio Cable) into the TX chain — pick this to transmit digital modes whose audio comes over a soundcard/VAC instead of TCI (see [Digital modes over VAC](#digital-modes-over-vac-virtual-audio-cable)); it needs VAC1 enabled with an input device on Settings → Audio. **Line In / VAC2** anchor entries are visible for layout but reserved for a later release. A TCI client that sends `TRX:0,true,tci` auto-selects TCI — the picker tracks it. |
| **Mic Boost** | OFF | HL2 hardware +20 dB analog mic preamp (codec PGA, single bit on the wire — C0 0x12 C2 bit 0). Pure hardware boost ahead of the digital chain. Enable when your hand mic / headset mic is genuinely too quiet to hit the modulator at a reasonable level even with the Mic Gain slider near max. Hardware is 2-state (off / +20 dB); intermediate trim comes from Mic Gain stacked on top. **Only affects the codec mic input** — PC mic / TCI sources bypass the codec PGA entirely, so this checkbox has no effect on those routes. Persisted across launches. |
| **Mic Gain** | 0 dB | The mic-into-modulator gain (WDSP TXA PanelGain1 — TXA chain stage #3, before phrot / EQ / leveler / CFCOMP / bandpass / compressor / OSCtrl / ALC). **Bidirectionally bound with the TxPanel front-UI slider** — slider for quick QSO-time adjustments, spin-box here for typed precision. 0 dB = WDSP unity; +10 to +20 dB typical SSB; +25 to +35 dB ESSB with headroom. Range −90 dB to +40 dB matches the reference's Default TX profile. **Stacks on top of Mic Boost** — if Mic Boost is ON, the modulator sees Mic Boost +20 dB + Mic Gain combined. |
| **ALC Max Gain** ⚠ | 3 (LINEAR) | The ALC (Automatic Level Control) max-gain ceiling — the always-on output limiter that catches peaks the leveler and compressor didn't bound, **before** the I/Q reaches the wire. **LINEAR amplitude factor (NOT dB) — units corrected in §15.27**: 1 = unity (limiter cannot amplify, only attenuate); 3 = the verified reference's default = 3× amplitude headroom = +9.54 dB of allowed amplification before the ALC pulls down. Earlier Lyra builds shipped this property as dB and called `dbToLin(3.0) = 1.413` — capping the ceiling at 47% of the reference's value and producing a ~6 dB power deficit on continuous mic-input tones (the §15.27 / #79 root cause; fixed 2026-06-03). Range 0..120 LINEAR matches the reference spinner exactly. Operator tuning: lower (1–2) for tighter splatter protection at the cost of headroom; higher (5–20) for ESSB-style program-level headroom. |
| **ALC Decay** | 10 ms | The ALC release time constant — exponential-curve tau, NOT an absolute time. Sets how quickly the limiter releases gain reduction after a peak. Default 10 ms matches the verified reference's Setup-load value EXACTLY. Operator tuning: lower (3–5 ms) for snappier release that lets program material breathe but can sound 'pumpy' on aggressive content; higher (30–50 ms) for smoother release that holds gain steadier but compresses dynamics more. Range 1..50 ms matches the reference spinner exactly. Attack stays at the WDSP create-time default (1 ms) and is not exposed (reference UI doesn't expose attack either). |

> **The ALC ceiling is a SAFETY knob, not a daily-use one.** Set it
> once to match your mic + voice + amp combination, then leave it.
> The daily-use control is Mic Gain — that's what you tune per QSO.
> Watch the ALC meter as you raise Mic Gain; if ALC is engaging hard
> (more than ~3 dB of gain reduction visible on the meter), you're
> driving past the limiter ceiling and creating splatter risk —
> either reduce Mic Gain or carefully raise the ALC ceiling.

### Leveler (TXA pre-ALC amplifier stage)

The Leveler is a pre-ALC amplifier stage that **boosts weak input
signals up toward unity before the always-on ALC limiter sees them**
— particularly helpful for quiet passages of voice, or for ESSB
operators who want consistent on-air loudness without riding the mic
gain knob constantly.  Sits in the chain BEFORE bp0 (the SSB
bandpass) and the ALC — so on a quiet syllable, the Leveler can
amplify by up to its Max-Gain ceiling, then bp0 + ALC produce a
wire-level signal that's much closer to full scale than the raw
mic level would otherwise allow.

**Operator-preference default: Leveler is OFF in Lyra.** The
verified reference's UI ships this Enabled checkbox **ON** by
default (and silently auto-enables it when you create a new TX
profile), but some operators (including the project's primary
tester) prefer it off — typical reason is that always-on
leveler interaction with predistortion / PureSignal calibration
can be unpredictable.  Tick the Enable checkbox if you want
reference-parity gain behaviour; leave it off if you prefer to
ride mic gain manually.

> **Not recommended for digital modes (FT8 / FT4 / MSK144 / Q65 / RTTY,
> etc.).** Digital-mode software already outputs a controlled, constant
> level, and the modes need a clean, *linear* TX chain. The Leveler rides
> gain on the envelope — on a digital signal that pumps the level and adds
> distortion, which widens your signal, raises IMD, and makes your reported
> levels inconsistent. **Leave the Leveler OFF for digital.** It's a
> voice/ESSB tool. The easy way to keep this straight is a per-mode-family
> profile: a digital/VAC profile with Leveler off, a voice profile with it
> set how you like — switching mode recalls the right one.

| Knob | Default | What it controls |
|---|---|---|
| **Enabled** | OFF (Lyra) / ON (reference UI default) | Master Leveler on/off.  Default OFF in Lyra by operator preference; tick this if your reference profile has it on and you want Lyra to match the reference's signal behaviour exactly.  Mostly noticeable on quiet voice content — the Leveler does little when input is already loud. |
| **Max Gain** | 15 (LINEAR) | Maximum amplitude factor the Leveler will amplify weak signals by.  **LINEAR factor (NOT dB)**: 1 = unity (no boost); 15 = the reference's default = 15× amplitude ceiling = +23.5 dB of amplification headroom for weak input.  Same unit-correction class as ALC Max Gain (the WDSP API takes LINEAR; the reference's "(dB)" label on its UI is misleading — the value is passed straight through with no conversion).  Range 0..20 LINEAR matches the reference spinner exactly.  Operator tuning: lower (5–10) for gentler boost that preserves natural mic dynamics; higher (15–20) for ESSB-style consistent loudness.  Has effect only when Enabled is ticked. |
| **Decay** | 100 ms | Leveler release time constant (exponential-curve tau, NOT absolute time).  Sets how quickly the Leveler releases its boost after a stronger signal subsides.  Default 100 ms matches the verified reference's Setup-load value EXACTLY.  Operator tuning: lower (30–50 ms) for faster release that follows voice envelope more tightly; higher (200–500 ms) for smoother release that holds boost steadier across syllables.  Range 1..5000 ms matches the reference spinner exactly.  Has effect only when Enabled is ticked. |

> **When to enable Leveler:** if your reference setup has it on (most
> reference TX profiles do — check Setup → DSP → AGC/ALC → Leveler
> in the reference for your active profile), enable it in Lyra and
> set Max Gain + Decay to match.  With Leveler on at typical values
> (15 LINEAR / 100 ms) you'll see Lyra produce **reference-parity
> actual RF on the same mic input** — the missing pre-ALC gain stage
> was the root cause of the §15.27 / #79 "Lyra produces less power
> than the reference on the same whistle/voice" investigation; the
> Leveler closes that gap completely.

### Phase Rotator (PHROT)

The phase rotator is an all-pass network that **symmetrizes asymmetric
speech waveforms**. Human voice peaks much harder in one direction than
the other; evening that out lowers the peak-to-average ratio, so more
*average* talk power gets through for the same ALC / peak ceiling. It's a
classic SSB voice-processing trick and matches the reference's Setup
phase-rotator control.

| Setting | Default | What it does |
|---|---|---|
| **Enabled** | ON | Master phase-rotator on/off. Default ON (the WDSP / reference posture). |

> **Auto-off in digital modes.** Phase rotation distorts FT8 / FT4 / RTTY /
> other digital waveforms — they need a clean, linear TX chain — so Lyra
> automatically disables PHROT whenever you're in **DIGU / DIGL**, even if
> the toggle is on, and re-enables it when you return to a voice mode. The
> Enabled checkbox is therefore your *voice-mode* intent; you don't have to
> remember to flip it for digital.

#### PHROT and wide / ESSB audio (4–10 kHz)

Phase rotation is tuned around the voice fundamental range (its corner is
down in the low hundreds of Hz), where speech asymmetry actually lives — so
most of its peak-flattening benefit is in the low end regardless of how wide
your TX filter is opened. On a **communications-grade** wide passband it
still earns its keep: more average talk power without raising your peak/ALC
ceiling, which is exactly what cuts through on a busy band.

On **high-fidelity ESSB** (wide, natural-sounding audio for its own sake)
the trade-off flips for many operators. PHROT is an all-pass network, so it
imposes frequency-dependent group delay across the band; on a 4–10 kHz
passband that can smear transients and take the "natural" edge off the audio
that the wide filter was opened to capture. ESSB-focused stations commonly
leave PHROT **off** and lean on the multiband processor / leveler / careful
gain staging instead, reserving PHROT for when punch matters more than
fidelity. Because it's a single toggle, the honest answer is to A/B it on
*your* voice and *your* passband — keep it on if it sounds punchier, turn it
off if it muddies your wide audio.

### AM Carrier (AM / SAM modulation)

AM and SAM are **double-sideband-with-carrier** modes — they put a steady
carrier on the air plus your audio in both sidebands. This control sets how
much power goes into that carrier, as a **percent of the standard AM
carrier**:

| Setting | Meaning |
|---|---|
| **100 %** (default) | **Standard full-carrier AM.** The carrier sits at one quarter of your peak (PEP) power — on 100 % modulation peaks the envelope swings up to full power, the textbook AM relationship. |
| **< 100 %** | **Reduced / controlled carrier** — softens the carrier and puts relatively more of your power into the sidebands (talk power / efficiency). Many AM operators run 40–85 % here. |
| **0 %** | Suppressed carrier (DSB-like). |

The percent is a **power ratio** (it maps to the modulator coefficient as
`√(pct/100) × 0.5`), so the number tracks carrier power linearly and "100 %"
is the standard carrier. The control affects **AM and SAM only** — DSB is
always suppressed-carrier, and FM / SSB / CW ignore it, so you can leave it
set and it only bites when you key an AM/SAM signal. The value persists
across sessions.

> **Carrier vs PEP:** at 100 % (standard AM) the carrier amplitude is half
> the peak envelope amplitude, so carrier power = 25 % of PEP. On a rig
> rated for 100 W PEP that's a ~25 W carrier, with the sidebands swinging
> the envelope toward 100 W on modulation peaks.

### FM (deviation, pre-emphasis & CTCSS)

FM transmit has three operator knobs in **Settings → TX → FM**. All affect
**FM only** — they're inert in SSB / AM / CW, so you can set them and they
only bite when you key FM. All persist across sessions.

| Setting | Default | What it does |
|---|---|---|
| **Deviation** | 5.0 kHz | Peak FM deviation. The spin box flags the two standard presets — **5.0 kHz — Wide (US)** and **2.5 kHz — Narrow (US/EU)** — and reads plain `kHz` at any other value. Too much deviation splatters into adjacent channels; too little sounds weak and quiet. Changing it also re-sizes the RX filter and the TX occupied-width readout to match (see below). |
| **Pre-emphasis** | Comm | The treble-boost curve applied before the modulator. **Comm** is the standard 6 dB/oct (300–3000 Hz) communications curve every FM rig and repeater expects — leave it here for **voice**. **Off** is flat (no boost) — use it for **FM data** (packet, 1200/9600 baud, VARA FM), where the treble tilt would distort the data tones, or for a deliberately flat/warm sound. (A true *Off* is a Lyra edge — most rigs force pre-emphasis on.) |
| **CTCSS sub-tone** | Off | Transmit a sub-audible **CTCSS** tone to open a tone-protected FM repeater. Tick **enable** and pick your repeater's tone from the standard list (67.0–254.1 Hz). Leave it off for simplex. |

> **Why pre-emphasis exists:** FM brings high audio frequencies out of the
> receiver noisier than low ones. Every FM radio boosts the highs on
> transmit (pre-emphasis) and cuts them back on receive (de-emphasis) — that
> matching cut flattens your audio *and* knocks the now-amplified
> high-frequency hiss back down, so you come through clean. **Comm** sends
> the curve the receiving station expects; **Off** sends flat audio for data.

**FM runs a clean audio chain automatically.** In FM, Lyra bypasses the
whole TX speech rack (EQ, Combinator, Plate reverb, speech enhancements) —
exactly as it does for the digital data modes. Feeding a multiband
compressor / reverb into FM's pre-emphasis just over-drives into "mush", so
FM audio is shaped only by its band-limit and the pre-emphasis curve above.
Nothing to switch off by hand.

**Quick access on the Tuning panel.** In FM the Tuning panel's front row
mirrors these controls — a **Dev** spin box and an **Emph** (Comm / Off)
chip sit right next to the **RPT** repeater button, two-way-synced with
Settings → TX → FM. And **choosing a Deviation auto-sizes the RX bandwidth**
to the matching FM channel width (±2.5 kHz → 12 kHz, ±5 kHz → 16 kHz); the
TX bandwidth in FM is always shown as the auto-derived occupied width
(`2 × (deviation + 3 kHz)`), so you never set a TX filter by hand in FM.
You can still override the RX bandwidth in the Filters panel afterward.

**Deviation by region (rule of thumb — match your band plan / the
repeater):**

| Region / use | Deviation |
|---|---|
| **US ham FM** (10 m / 6 m / VHF–UHF, wide) | **5.0 kHz** (≈16 kHz channel) |
| **US narrowband** | 2.5 kHz |
| **Much of Europe & elsewhere** | **2.5 kHz** narrow is the common norm (some use 3.0 kHz) |

When in doubt, match what the repeater or local band plan specifies — running
wide into a narrow channel splatters your neighbours; running narrow where
everyone else is wide makes you sound quiet and distant.

CTCSS is a *transmit* sub-tone only (Lyra sends the access tone; it doesn't
decode incoming tones). If you can hear a repeater but can't bring it up,
the tone is the usual culprit — set it to the repeater's published CTCSS /
"PL" frequency.

> **HL2 FM is 10 m / 6 m.** On a bare Hermes Lite 2/2+ (no transverter),
> FM lives on 29.5–29.7 MHz (10 m) and the 6 m FM segment. CTCSS only
> matters when you're working a tone-protected repeater there.

### ATT on TX (RX-ADC protection)

While you transmit, your own TX carrier couples back into the receiver.
Without protection it can drive the RX front-end ADC into overload — the
panadapter goes wide / off-scale and the S-meter pegs on key-down. **ATT
on TX** forces the HL2 step attenuator (drives the AD9866 RX LNA to
minimum gain) for the duration of every transmission, then restores your
RX setting on key-up. This mirrors the reference's Setup → General →
Ant/Filters → "ATT on Tx" + "ATT: 31".

| Knob | Default | What it controls |
|---|---|---|
| **Enabled** | ON | Master ATT-on-TX. Default ON (the reference HL2 working posture). Turning it OFF removes RX-ADC protection during TX — only do so for a specific reason. |
| **ATT** | 31 dB | Step-attenuator value forced on the RX front end while transmitting. 31 dB = maximum attenuation (LNA to minimum gain) = strongest protection and the reference default. Range 0..31. Effective only when Enabled. |

**Front-panel "ATT" lamp.** The TX panel shows a live ATT lamp (in the
gap between the Mic and Tune sliders) so you can see the protection
state at a glance:
- **gray `ATT off`** — disabled.
- **orange `ATT 31`** — enabled + armed (on RX): the value it will apply on key-down.
- **red `ATT -31`** — engaged (keyed): the attenuation now applied to the front end.

Click the lamp to toggle ATT-on-TX (same control as the Settings
checkbox). The lamp snaps orange → red on key-down — your visible
confirmation that the front end is protected. (Note: the LNA gain
*readout* itself keeps showing your RX setpoint — the ATT lamp is the
TX-state indicator, not the LNA number.)

### External TX Inhibit (lock out all transmit)

A hard safety lockout for when sensitive gear — a second receiver / SDR, a
scope, a spectrum analyser — shares your antenna or bench and you must be
sure the radio can't key while it's connected. Tick **Settings → TX →
External TX Inhibit** and the radio **cannot transmit by any means**: MOX,
foot switch / hand-mic PTT, CW, Tune, and TCI are all blocked at the source.
Engaging it while you're transmitting drops you straight to receive.

- It lives in **Settings → TX** on purpose — not a one-click front-panel
  button — so an accidental click can't release it; you must open the dialog
  to turn it back off.
- It is **remembered across restarts** (fail-safe): leave it on and it stays
  on until you consciously clear it.
- While it's active a red **⛔ TX INHIBIT** badge shows in the top toolbar so
  you always know why keying is dead.

### SWR protection & TX power limiting

Two operator-set guards that protect your PA / finals and a downstream
amplifier. Both live in **Settings → TX** (the safety column, next to
ATT-on-TX); SWR protection also has a front-panel **PROT lamp** on the
TX panel.

**SWR protection (auto-cut / fold on high reflected power).** While you
transmit, Lyra watches forward/reflected power and acts if the SWR stays
above your limit — protection against a bad antenna, a disconnected
feedline, or an ATU that didn't latch. It's deliberately hard to
false-trigger: a key-down **blanking** window skips the T/R + ALC settle,
forward/reflected power **floors** ignore the low-power regime where the
ratio is just noise, and the over-limit condition must persist for a
short **dwell** before it acts. The SWR reading is calibration-free (it's
a power ratio), so it works without the PWR-meter calibration. After it
acts it **latches** — re-key (MOX or TUN) to resume once you've fixed the
antenna; it never recovers on its own while keyed.

| Knob | Default | What it controls |
|---|---|---|
| **Enabled** | ON | Master SWR protection. |
| **Limit** | 5 : 1 | SWR at/above which it acts once sustained. 5:1 is conservative — most antennas run under 3:1. Range 1.5..10:1. |
| **On high SWR** | Cut TX | **Cut TX** unkeys immediately (safest). **Fold back power** halves TX drive in steps to try to bring the SWR down while staying on the air, escalating to a hard cut if it reaches the fold floor and the SWR is still over the limit. |
| **Fold floor** | 10 % | Lowest TX drive the Fold action steps down to before it gives up and cuts. Used only with the Fold action. |
| **Protect during tune (TUN)** | ON | Whether the SWR cut also applies to a deliberate ATU tune carrier. |

**Front-panel "PROT" lamp** (TX panel, next to the ATT lamp):
- **gray `PROT off`** — disabled.
- **green `PROT`** — enabled + armed (watching reflected power).
- **red `SWR x.x:1`** — it acted (cut or folded): the latched ratio that
  tripped it. With Fold, the live TX **Drive %** readout shows the reduced
  level. Re-key to reset. Click the lamp to toggle enable.

**TX power cap (watts, per band).** The amp-protection power limit now
lives on its own **Settings → PA Gain** tab and is set in **watts**, not
drive %. It protects a low-drive amplifier whose input can't take the
HL2's full output by holding your TX output at or under a watts ceiling
on **every band** — and because each HL2's drive→watts curve differs by
band, it **auto-calibrates** instead of guessing. See
[Settings → PA Gain](#settings--pa-gain) for the full set-up. In short:
enter each band's full output, tick **Limit TX output to ___ W**, then
key **TUN** on each band into a **dummy load (amplifier out of line)** —
Lyra walks the power up to your cap from below and locks that band exactly
at it (the band turns green ✓). SSB and the other modes then hold each
tuned band at your cap automatically, capping voice peaks without chasing
them. *(The old "Max TX drive %" cap is retired — the watts cap supersedes
it.)*

### TR Sequencing + Amplitude Envelope (timing knobs below)

Seven operator-tunable knobs that control the **timing of the MOX → RF
edges** and the **amplitude shape** of the TX I/Q at the start and
end of every keying event. All values are in milliseconds; live-
apply (changes take effect on the next MOX edge — no restart, no
MOX cycle required). Persisted across Lyra restarts.

### TR Sequencing (PTT → wire-MOX → RF timing)

Four scheduling knobs. They unwind in this order on every keying
event:

| Knob | Default | What it controls |
|---|---|---|
| **MOX Delay** | 15 ms | From operator-PTT (your click / space-bar / TUN) to the wire-MOX bit going hot. Gives the RX-protect step-attenuator + external filter-board relays a window to settle into TX configuration **before** RF appears. |
| **RF Delay** ⚠ | 50 ms | From wire-MOX hot to the "RF settled" edge (when actual RF appears on the antenna). **This is the hot-switch protection window for external SS linears** — their T/R relay needs typically 30–50 ms to settle. The default is bench-safe for typical 1 kW SS HF linears. **Reducing this below your amp's T/R settle spec can destroy the PA.** |
| **Space MOX Delay** | 13 ms | Keyup re-key window: the time between operator-keyup and the wire-MOX bit clearing. Allows a mic-clip / CW-dot-tail re-key in this window to collapse-stay-TX (no on-the-air drop, no extra T/R cycle). Also sets the upper bound on **Fade-Out Duration** below — fade-out must fit inside this window or it gets truncated at MOX-clear. |
| **PTT-Out Delay** | 5 ms | Final cleanup window after wire-MOX clears — before step-att restores, OC pattern flips back to RX, and `moxActive` emits false. Lets external relays finish switching back **before** the RX front-end re-opens. |
| **TX-Stop Delay** | 10 ms | **In-flight UDP datagram clear window.** Time between the TX-DSP channel stopping (blocking flush) and the wire MOX bit clearing on keyup. UDP datagrams already-sent (or sitting in your OS network buffer) carrying MOX=1 + non-zero TX I/Q need this window to actually reach + be processed by the HL2 BEFORE the wire MOX state flips — otherwise the gateware could see momentary MOX=0 with stale TX I/Q from a previous datagram. Default 10 ms matches the verified-reference value. Reduce only on a very low-latency NIC + LAN where you're confident no datagrams sit in OS buffer. |

### Amplitude Envelope (cos² fade on TX I/Q)

Two knobs that shape the **amplitude** of the TX I/Q at the EP2
wire-pack stage. The shape is a cos² half-cycle (raised cosine —
smooth, no high-frequency content, the gold-standard envelope for
PTT-onset shaping).

| Knob | Default | What it controls |
|---|---|---|
| **Fade-In Duration** ⚠ | 50 ms | The cos² ramp from zero to full amplitude at keydown. **Belt-and-suspenders amp protection** — even if RF Delay is right, the soft amplitude rise INSIDE the RF Delay window adds redundant protection against any residual MOX-bit / relay timing skew. Default matches the default RF Delay so the ramp completes exactly when "RF settled" fires. **Reducing this without knowing your amp's switching behaviour can expose the PA to hot-switch transients.** |
| **Fade-Out Duration** | 13 ms | The cos² ramp from full amplitude to zero at keyup. **Must fit inside Space MOX Delay above** — the gateware DAC stops consuming TX I/Q the instant wire-MOX clears, so a fade-out longer than Space MOX Delay gets truncated mid-ramp (audible click). RF going DOWN doesn't damage amps (the relay disengages cleanly), so this is shorter than Fade-In — purely click-prevention. |

The asymmetric default (50 ms in / 13 ms out) reflects the
**asymmetric risk**: RF coming UP into a still-switching relay can
destroy amps; RF coming DOWN cannot.

### Restore hot-switch-safe defaults

A single button that resets all seven values to **15 / 50 / 13 / 5 /
50 / 13 / 10** ms — the bench-validated profile that's safe for typical
1 kW SS HF linears (and reference-faithful for the in-flight UDP
datagram clear). Use it any time you've experimented with values and
want a known-good starting point.

> **The two layers compose into one "clean PTT onset" story.** The
> TR-sequencing values control *scheduling* of the MOX → step-att →
> RF → cleanup edges; the amplitude envelope shapes the *I/Q
> amplitude* itself. At defaults the fade-in completes exactly when
> "RF settled" fires (50 ms after wire-MOX hot), and the fade-out
> completes exactly when wire-MOX clears (13 ms after operator-keyup).

> **Live-apply behaviour:** changes take effect on the next MOX edge,
> not the in-flight one. If you change a value while the radio is
> keyed, the in-flight transition finishes at the prior rate; the
> new value applies at the next keydown or keyup. So you can A/B
> values mid-session by changing then re-keying — no MOX cycle,
> no app restart.

### Tune drive (what the TUN button keys at)

A 3-way **Tune drive** picker decides the power the [TUN
button](#transmit-row-tx) uses when you key it, plus the two values it
can draw from:

| Mode | TUN keys at |
|---|---|
| **Use TX Drive slider** | your live TX Drive % — same as a normal transmit (the classic behaviour). |
| **Use Tune slider** | the separate **Tune slider** % below — a per-band, live-adjustable level (remembered per band, and also shown inline on the TX panel while armed). |
| **Use fixed drive** | the **Fixed drive** % below — one value, the same on every band, ignored sliders. |

In the two non-slider modes Lyra stashes your TX Drive on key-down and
restores it when you un-key, so tuning at a safe low power never disturbs
your voice-TX setting. Only the spinner the active mode uses is enabled.

> **TUN can never exceed your Max TX drive.** Both the Tune and Fixed
> spinners are capped at your **Max TX drive** ceiling (Settings → TX),
> and the wire enforces the same ceiling on every drive write regardless
> — so a tune level can never overdrive past the maximum you've set. The
> typical workflow: set a low tune level (e.g. 25 % into a dummy / tuner)
> and a higher voice-TX drive, and TUN tunes safe while your QSO drive is
> untouched.

---

## Settings → VOX (voice-operated transmit)

**VOX** keys the radio when you speak and drops it after your voice
stops — hands-free SSB, no PTT. It has its own Settings tab (kept off
the TX tab to keep that one uncluttered), and an arm/disarm button on
the [TX panel](#tx-panel).

VOX is a **keying decision only** — it watches your mic and RX audio and
decides when to key. It adds **zero** DSP to your transmit signal, so it
can't colour your audio and is safe alongside every TX feature. It keys
**voice modes only** (never CW), and **never overrides a manual or
foot-switch key** — a hardware key always wins, and VOX only ever
releases the key it took itself. Default **OFF**.

### Arming

Tick **Enable VOX** here, or click the **VOX** button on the TX panel —
the two stay in sync. Green = armed (listening); red **●** = keying now.
To stop instantly, click the button off (or in Settings untick Enable):
that drops a VOX-held transmission the same moment, without waiting for
the timing to run out. A foot-switch or MOX press also overrides VOX at
any time.

### The two level meters (dial against these)

dBFS threshold numbers are hard to guess, so the tab shows two live
colour bars — green at low level rising through yellow to red — with a
**cyan marker** on each showing the level you've set. They're live even
when VOX is off, so you can set everything up before arming.

- **Mic level** — your live mic level. The cyan marker is your
  **Threshold**. Speak normally and watch where your voice peaks; set
  Threshold **just below** those peaks but **above** where the bar sits
  when you're silent (desk / room noise).
- **RX level** — your live received-audio level (what you hear on the
  speaker). The cyan marker is the **Anti-VOX level**. This meter greys
  out when Anti-VOX is off.

### The knobs

- **Threshold** (dBFS, default −35) — how loud your voice must be to key
  TX. Higher (toward 0) = you must speak up / desk noise won't trip it;
  lower = more sensitive. Set it with the Mic-level bar (above).
- **Open delay** (ms, default 10) — how long your voice must stay above
  the threshold before VOX keys. Rejects clicks, thumps and short
  noises. Raise it if stray sounds key you; keep it small so speech
  onset isn't clipped.
- **Hang time** (ms, default 300) — how long TX is held after your voice
  drops below the threshold. Bridges the gaps between words so VOX
  doesn't chatter mid-sentence. Raise it if VOX drops between words;
  lower it to release faster at the end of a transmission.
- **Anti-VOX** (toggle + level, default ON / −45 dBFS) — stops your own
  **received audio** from keying VOX. Essential if you run open speakers
  instead of headphones (speaker audio bleeds into the mic). When the RX
  audio is above the Anti-VOX level, VOX won't key. Set the level with
  the RX-level bar: put the marker **just above** where the bar sits when
  the radio is receiving voice. On headphones you can leave Anti-VOX off.

### Quick setup

1. Untick Anti-VOX for a moment (or use headphones) and watch the
   **Mic level** bar while speaking normally.
2. Set **Threshold** a few dB below your speech peaks, above the silent
   noise floor.
3. Talk a normal sentence: if VOX drops between words, raise **Hang
   time**; if stray desk noise keys you, raise **Threshold** or **Open
   delay**.
4. On open speakers, turn **Anti-VOX** on and set its level just above
   where the **RX level** bar sits on received voice.
5. Arm the **VOX** button and go — the button lamp shows armed (green)
   and keying (red) at a glance.

All settings persist across sessions.

---

## Settings → PA Gain

Per-band TX power calibration and amplifier protection, all set from a
**dummy load with your amplifier out of line** and PA Gain left at **100**.

> ⚠ **Always calibrate into a dummy load, amp OUT.** Calibrate at **full
> drive (100 %)**, and **not in CW** — CW won't make a steady tune carrier,
> so use SSB / AM / FM.

**Calibrate each band (do this first).** Under **PWR meter calibration**:
key a **full-drive TUN** carrier, read your **external watt-meter**, type
that figure into **"Your watt-meter reads:"**, and press **Calibrate this
band**. You don't have to hold TUN while you type — Lyra captures the
reading *while you tune*, so you can drop TUN, type, and calibrate. At full
drive that one press does **two** jobs: it trims the on-screen PWR meter to
match your external meter, **and** it fills that band's **Full Output (W)**
(the amp-cap reference) — so you measure each band just once. Repeat for the
bands you use. (Calibrate below full drive and only the meter trim is set;
the status line tells you.)

**Ready-to-calibrate indicator.** A live box sits right beside the
**Calibrate this band** button so you don't have to remember the two setup
traps — it's **green "READY ✓"** when you're set, and warns otherwise:
**red "IN CW — CAN'T TUNE"** if the band is in CW (CW makes no steady tune
carrier — switch to SSB / AM / FM), or **amber "…% — NOT FULL"** if the drive
TUN will use isn't at 100 %. It follows the *right* drive automatically — your
**TX Drive** slider, or the **Tune** slider / **Fixed** tune drive if you've
set a separate tune drive in **Settings → TX** — and names which one to bump.

**PA Gain By Band.** One number per band, default **100 = neutral** — leave
it at 100 while calibrating. Each HL2, and each band, varies, so nothing is
computed: it's a per-band trim you can later nudge to tame a hot band or
push a weak one. Drive % stays the dial; this calibrates what the dial
*means* in watts. Each row's **Clear** button starts that band over — it
resets **PA Gain → 100**, wipes its **Full Output**, and un-learns any cap
tuning for the band, so you can re-calibrate it from scratch.

**Full Output (W).** The watts each band makes at full drive — **filled for
you** when you Calibrate that band at full drive (or type it by hand). Shows
"—" until measured. It's the reference the watts cap needs.

**Max Output (amp protection).** Once you've calibrated the bands you use:
tick **Limit TX output to ___ W** and set your cap, then tick **Enable Max
cap (arm)** — the cap does **nothing until it's armed**, and you can't arm
it until at least one band is calibrated. Now key **TUN** on each band: Lyra
walks the power up from below and **parks that band on the highest drive
step that stays *under* your cap** — the **Cap tuned** column shows a green
✓ when locked, a red — when not yet tuned. SSB and the other modes then hold
each tuned band at that level automatically, capping voice peaks without
chasing them. A band you haven't tuned runs *conservatively under* the cap
until you do. Change the cap → re-key TUN on each band to re-learn.

Because the approach is always *from below* and parks on the under-cap
step, the output never overshoots — safe for a solid-state amp where a
fraction of a watt over matters. *(This replaces the old "Max TX drive %"
control — the watts cap supersedes it.)*

**Why a band can land noticeably under the cap.** The Hermes Lite's drive
control is **coarse** — the on-board DAC has only about **16 hardware
steps**, and each step is a *different* number of watts on every band. Your
cap almost always falls **between** two of those steps (for example, one
step makes 3.0 W and the next jumps to 3.7 W, with nothing available in
between). Lyra always chooses the step just **under** your cap, so on that
band a 3.5 W cap holds at ~3.0 W — not because the servo is timid, but
because that is the closest the radio can get without going *over*. This is
deliberate: under-is-safer is the right call for amp protection, and the
level is rock-steady rather than flickering across the gap. Landing exactly
on the cap would require finer-than-hardware drive control (filling the gaps
between the coarse steps with the continuous digital gain) — a planned
future refinement, not something worth rushing into the safety path.

> **⚠ The cap won't limit until you arm it.** Ticking **Limit TX output to**
> only sets the *value* — the cap does nothing until you also tick **Enable
> Max cap (arm)**, which stays greyed until you've calibrated at least one
> band. This is deliberate: a cap can never *silently* mis-limit your power.
> If you arm before a band has a **Full Output** reference, that band runs a
> safe conservative **~30 % drive** fallback (power reads LOW) until you
> calibrate it. When the cap is actively holding power down a **CAP** chip
> shows on the TX panel: amber **CAP ~30%** = uncalibrated fallback (low),
> cyan **CAP nW** = holding a calibrated band at your set watts. If you don't
> run an amplifier, just leave the cap unticked and Lyra transmits at full
> drive. *(Upgrading from an older version keeps an existing cap armed, so
> you never lose amp protection on update.)*

---

## Settings → Network (TCI)

**TCI** (Transceiver Control Interface, the Expert Electronics protocol)
lets logging, cluster, and digital-mode programs drive Lyra over your
local network — set frequency, mode, and filter; read your current state;
and pull DX-cluster spots onto the panadapter. SDRLogger+, Log4OM, N1MM,
WSJT-X and others speak it.

Lyra runs a TCI **server**; the other program connects to it as a client.

- **Bind address** — which network interface to listen on. Leave blank /
  `0.0.0.0` to accept connections from any interface (e.g. another PC on
  your LAN); use `127.0.0.1` to allow only programs on this same PC.
- **Port** — the TCI listening port (default **50001**, the Expert
  default). Match this in your logger's TCI settings.
- **Rate limit** — minimum gap between repeated broadcasts of the same
  value, to avoid flooding a client during fast tuning.
- **Send full state to clients on connect** — push the current
  frequency/mode/filter the moment a client connects, so it starts in
  sync.
- **Add "CW" to the modulations list (CWL/CWU alias)** — some clients
  only understand a bare `CW`; enable this so they can select it.
- **Emulate ExpertSDR3 / Emulate SunSDR2 PRO** — report an Expert
  protocol/device name. Enable these only if a client refuses to talk to
  Lyra unless it sees a SunSDR/ExpertSDR rig.
- **TCI server running** — the master on/off. When on, the header shows
  the **● TCI** indicator (green with a client count, amber when idle).

**Audio / IQ streaming** is automatic: if a connected client asks for an
audio or IQ stream, Lyra sends it the receiver audio (or raw I/Q) as TCI
binary frames — no extra toggle needed. Lyra advertises the audio format
(48 kHz / float32 / stereo / 2048-sample frames / 50 ms TX-buffer hint)
at connect so the client decoder configures itself correctly the moment
it attaches.

**Signal-strength readings** ride the same link, too. Lyra continuously
sends its receiver's meter reading over TCI as a *calibrated dBm* value —
the same number your S-meter shows — so a connected logger's own signal
meter mirrors Lyra's rather than guessing from raw audio level. (This is
sent to every TCI client; it's what the Combo auto-RST feature below builds
on.)

> RX2 over TCI is deferred until Lyra has a second receiver. Today the
> server advertises a single channel.

### SDRLogger+ Combo link

**SDRLogger+** is the companion logging program (by the same developer).
Over an ordinary TCI link it already drives Lyra and shows spots — but
turning on **Combo** upgrades that one-way link into a two-way
*collaboration* that runs over the **same TCI socket**: no bridge app, no
second connection, no extra port. The CW Console, the CW Decoder and the
logger start acting as one desk.

**Engage it** with a single switch:

1. **Settings → Network** → tick **"SDRLogger+ Combo (share CW Console
   contact)"**. It's **off by default**, and it's a per-machine setting
   that's remembered.
2. Make sure **TCI server running** is on (Combo rides the TCI link), and
   that **SDRLogger+ is connected to Lyra as a TCI client** (point its
   radio/TCI connection at Lyra's IP + port, the same **50001** above).
3. When the two are linked, SDRLogger+ shows a **`● Lyra Combo`** badge in
   its Log-Entry header — that badge is your confirmation the link is live.

With Combo on, four things happen automatically as you work a station:

- **Call → logger.** Put a call in Lyra's CW Console **His call** — type
  it, or grab it from the [CW Decoder](#reading-cw--the-rx-decoder) — and
  it lands in SDRLogger+'s log entry and fires its callbook (QRZ / HamQTH)
  lookup. You copy the call once, in Lyra, and the logger catches up.
- **Name → back to `{NAME}`.** After that lookup resolves, SDRLogger+
  sends the operator's **first name** back to Lyra, which fills the
  **`{NAME}`** token in the CW Console. A reply macro like
  `{CALL} DE {MYCALL} GE {NAME}` now greets them by name with no typing.
- **Received signal → RST.** SDRLogger+ can auto-fill the **S** digit of
  **RST-Received** from the same shared, *calibrated* S-meter reading
  described above — so the number it logs is exactly what your meter shows,
  not a guess. Lyra also sends a small signal-to-noise figure alongside it
  (Combo only) so the logger peak-holds and fills the S **only on a real
  signal**, not on the band noise. Turn on the **S-auto** control next to
  the RST-Rcvd field in SDRLogger+; typing a value latches it to manual,
  and working a new call re-arms it. Works on SSB / CW / digital (not SAT).
- **One-click log with `{LOG}`.** Add the **`{LOG}`** action token to a CW
  macro — e.g. `TU 73 {MYCALL} ee {LOG}` — and sending that macro sends
  the sign-off *and* logs the QSO in SDRLogger+ (call, RST, mode and
  frequency all stamped from the shared state). A macro that is **only**
  `{LOG}` is a log-only button that keys nothing. See
  [CW operating](#cw-operating-paddle-keyboard-tci) for how `{LOG}` sits in
  the token palette (it's the **yellow** action chip).

Combo is a Lyra ↔ SDRLogger+ conversation — a plain third-party TCI logger
simply ignores the extra messages, so leaving it on does no harm. Nothing
here changes the normal TCI control/spots behaviour; it only *adds* the
contact-sharing, name-back, signal-report and one-click-log conveniences on
top.

### CW keying over TCI

A TCI logging or contest program can **key CW through Lyra** — it sends
the text, Lyra's keyer generates the Morse and the radio puts it on the
air (the same path the on-screen CW Console uses). Lyra implements the
Expert Electronics TCI CW commands:

- **`cw_macros`** — send a plain message ("CQ TEST DE N8SDR"), the
  everyday "send this text" command.
- **`cw_msg`** — the contest exchange as prefix / callsign / suffix
  (e.g. `TU` / `N8SDR` / `599 004`), with `$N` to repeat the callsign.
- **`cw_macros_speed`** — set the keying speed (WPM); a bare query reads
  it back.
- **`cw_macros_stop`** — abort the current send immediately.

Keying only happens in **CWU/CWL** (in any other mode the commands are
ignored). Set your logger's TCI keyer to Lyra and it sends straight
through. SDRLogger+, N1MM and similar work this way.

### Digital modes over TCI (FT8 / FT4 / MSK144 / Q65 / etc.)

Lyra is a fully bidirectional TCI partner for the digital-modes clients
operators already use — MSHV, JTDX, WSJT-X and similar. The client
both **receives** Lyra's RX audio over TCI and **sends** its modulator
audio back over TCI for Lyra to transmit; no VAC, no virtual cables,
no host-side sound card needed.

**One-time setup:**

1. **Settings → Network (TCI)** — make sure the server is running and
   the port matches what your digital-modes client expects (default
   **50001**).
2. **Settings → TX → Mic + ALC → Mic source** — pick **TCI**. This
   routes the digital-mode client's audio into Lyra's TX chain in
   place of the hand-mic. (The client can also auto-select TCI by
   sending a `TRX:0,true,tci` command — supported, but having the
   picker on TCI means manual TUNE buttons in the client also work.)
3. In the client (MSHV / WSJT-X / JTDX) configure: TCI server
   `127.0.0.1:50001`, sample rate **48 kHz**, block size **2048**,
   buffering **50 ms**. Most clients set these defaults out of the box;
   just confirm.

**Operating flow:**

* The client tunes Lyra's VFO and sets the mode (typically **DIGU** for
  FT8 on the standard 14.074 / 7.074 / 21.074 / … MHz dial frequencies).
* RX audio streams to the client continuously; its waterfall decodes the
  band exactly as if the client were attached to a Thetis-class radio.
* When the client wants to transmit, it sends a TRX-on command. Lyra
  engages MOX, the client's modulator audio is fed through the WDSP TXA
  chain (with your **TX mic gain**, **ALC**, **leveler**, **TX bandpass**,
  and any EQ active), and onto the HL2 wire. At keyup the client sends
  TRX-off; Lyra returns to RX.
* The **Mic gain** slider on the TX panel still applies to TCI audio —
  the same chain handles voice and digital. [Profiles](#profiles-txrx-chain-presets)
  now save mic gain (and the TCI gains), so you can keep an SSB-voice
  profile and an FT8/MSHV digital profile each with their own trim and
  bind them per mode family — switching mode then recalls the right trim
  automatically.

**Dedicated TCI-only gain sliders** (Settings → Network → TCI server
group). Two independent ±dB knobs sized for the common digital-mode
mismatches, so you can dial the TCI path without disturbing your voice
mic gain:

- **RX-out gain** (−40 … +10 dB, default **0 dB** = unity) — attenuates
  or boosts the audio Lyra **sends** to TCI clients. Drop this if
  MSHV / JTDX / WSJT-X show an unusably hot waterfall and the
  client-side RX gain slider isn't bringing it down enough. 0 dB =
  byte-identical to Lyra's RX level. Takes effect at the next emitted
  audio packet (~43 ms at 48 kHz / 2048-sample frames).
- **TX-in gain** (−40 … +10 dB, default **0 dB** = unity) — attenuates
  or boosts the TX audio Lyra **receives** from the client before it
  hits the WDSP TXA chain. Drop this if your client outputs audio
  hotter than Lyra's ALC handles gracefully (the common case —
  digital-mode software often sends near full-scale by default and
  overdrives Lyra's TXA limiter slightly). Boost it for an unusually
  quiet client. 0 dB = byte-identical to the client's stream level.
  Takes effect at the next received audio packet.

Both sliders are the live, always-available adjustment. The TCI RX and
TX gains are also saved in [profiles](#profiles-txrx-chain-presets), so
you can store balanced ESSB-voice and digital setups and recall them —
manually or via per-mode-family auto-recall — instead of retouching the
gains every time you switch.

**Troubleshooting:**

* **MOX engages but the client says "no audio"** — confirm Mic source =
  TCI on **Settings → TX → Mic + ALC**; without it Lyra processes audio
  from the configured hand-mic source while the client's audio gets
  dropped.
* **Client RX waterfall looks unusably hot** — Lyra advertises the
  format at connect so the client's RX gain slider should land in a
  sensible position. If your client doesn't honour the advertised
  parameters, drop **Settings → Network → RX-out gain** (Lyra side,
  works for all clients at once) or the client's own RX gain slider
  until the waterfall sits in the normal −20 to −10 dB range.
* **MSHV / JTDX TX overdrives Lyra's ALC** — drop **Settings → Network
  → TX-in gain** by 3-6 dB (the client's modulator is louder than
  Lyra's TXA chain wants by default). Watch the MIC and ALC bars on
  the multimeter while keying — MIC should peak around −10 to −6 dBFS
  with ALC barely budging.
* **You're transmitting but nobody's decoding you (no PSKReporter
  spots)** — usually means a frequency / band / power issue on the
  station side (wrong dial, antenna fault, low drive). Check the
  external watt-meter, confirm the band is open, and try a known-good
  digital-mode sked partner. The TCI audio path itself is verified
  reference-faithful end-to-end.

### Digital modes over VAC (Virtual Audio Cable)

If you'd rather wire audio the classic way — through a **Virtual Audio
Cable** instead of over TCI — Lyra has a built-in VAC path. This suits
clients you prefer to run by soundcard, or any setup where TCI handles
**control** (PTT, CAT, frequency, band) while the **audio** rides a cable.
That's the recommended split for MSHV: let TCI key the radio and follow
the band, and route the audio over VAC.

> **VAC carries audio only — it does not key the radio.** Keying still
> comes from TCI (or CAT). TCI *control* and TCI *audio* are independent;
> using TCI to PTT does **not** mean you're using TCI audio.

**You need two cables** (e.g. VB-Audio "CABLE" + "CABLE-B", or VAC's
"Line 1" + "Line 2") — one for each direction:

| Direction | Lyra side (Settings → Audio → VAC1) | Client side |
|---|---|---|
| **RX** (decode) | **Output device** = cable A (e.g. Line 1) | **Input** = cable A |
| **TX** (transmit) | **Input device** = cable B (e.g. Line 2) | **Output** = cable B |

**Setup:**

1. **Settings → Audio → VAC1** — tick **Enable** (or **Auto-enable for
   digital** so it follows DIGU/DIGL), set the **Output device** to the
   cable your client reads (RX), and the **Input device** to the cable your
   client writes (TX). Leave RX/TX gain at the defaults to start (0 dB /
   +3 dB). The status log shows `[vac1] LIVE: out='…' | in='…'` when it's
   up — if `in='(none)'`, no input device is selected and TX audio can't
   reach the radio.
2. **Settings → TX → Mic + ALC → Mic source = "PC Soundcard (VAC1)".**
   **This is the step that catches people.** Without it the TX chain reads
   a different source (the codec mic, or TCI) and you get **no power out**
   even though everything else looks right.
3. **TCI for control:** keep your TCI client connected for PTT/frequency
   (Settings → Network). In MSHV's audio panel, point **Output** at the TX
   cable and **Input** at the RX cable (the reverse of Lyra), and pick the
   channel (Left) your cable carries.
4. Leave the **[Leveler](#leveler-txa-pre-alc-amplifier-stage) OFF** for
   digital (see that section).

**The fastest fix when a VAC transmit produces no power:** check the
**Mic source** — if it's on **TCI** (e.g. your TCI/voice profile left it
there), the TCI side claims the TX-audio path and pulls from an empty TCI
stream → silent → no RF. Set it to **PC Soundcard (VAC1)**. Saving a
**digital/VAC [profile](#profiles-txrx-chain-presets)** (which stores the
mic source *and* the VAC enable) lets you flip the whole setup with one
recall instead of toggling these by hand.

### DX-cluster spots

Lyra paints DX spots right on the panadapter at each spotted frequency —
callsign-tagged, with the spotter's **country** abbreviation. CW spots are
placed allowing for your **CW pitch** so they sit on the actual signal, not
zero-beat. **Click a spot** to tune to it *and* grab the call into the
CW-console contact row (so a `{CALL}` macro instantly addresses that
station). All controls live in **Settings → Network → DX-cluster spots**.

#### Three spot sources

Spots can pour in from any (or all) of three sources at once — they share
one list, and the filters, clutter caps and marker colours below apply to
all of them.

- **TCI** — spots pushed in by a logger or cluster app (e.g. **SDRLogger+**)
  connected to Lyra's TCI server. This works automatically: whenever such an
  app is wired to a DX cluster, RBN or Skimmer and connected over TCI, its
  spots land on the panadapter with no extra setup.
- **SpotHole** — a standalone internet aggregator (DX cluster + POTA +
  skimmer, all modes) that needs **no logger** of your own. It's an opt-in
  network feature, **off by default**:
  - **Fetch DX spots from SpotHole** — turn it on.
  - **Sources** — which SpotHole sub-feeds to include (e.g. `Cluster,POTA`;
    blank = every source).
  - **Only the band I'm on** — request just your current band so the list
    stays focused.
  - **Poll every** — how often Lyra refreshes from SpotHole.
  - **Initial age** — on connect, pull spots received within this window.
  - **Refresh now** — fetch immediately; a status line beside it reports
    what happened.
- **DX-cluster telnet** — connect to a *specific* node (VE7CC / DXSpider /
  AR-Cluster). Standalone, no SpotHole needed:
  - **Connect to a DX-cluster node** — turn it on, then set **Host** and
    **Port** for the node.
  - **Login call** — the callsign you log in with; **blank = your MYCALL**.
    Set a club call, a friend's login, or a node-registered call if the node
    requires one.
  - **Login cmds** — optional commands sent right after login (separate
    several with `;` or new lines).

  > **Tip — get more than CW.** Many nodes default to **RBN CW skimmer spots
  > only**. To also receive FT8/FT4 on VE7CC, set **Login cmds** to something
  > like `set/ft8;set/ft4` (and `set/nocw` to quiet the CW firehose). SSB
  > spots are human-posted, so they're always sparse.

#### Filters

These are display-only — the underlying list always keeps everything; the
filters just decide what's drawn.

- **Show modes** — a comma list, family-aware (e.g. `CW`, `SSB`, `DIGI`).
  Blank = every mode.
- **Show regions** — continent codes (NA/EU/AS/SA/AF/OC) and/or ISO-2
  country codes (US, CA, DE… — the same codes shown on the spot markers).
  For example `US,EU` = the US plus all of Europe. Blank = worldwide.

#### Clutter caps

These keep a busy band readable.

- **Show at most** — the maximum number of spots drawn at once (newest win);
  0 = unlimited. Default 25.
- **Per-freq max** — the maximum spots per frequency before the rest
  collapse into a **"+K"** badge on the newest marker (tames FT8 pile-ups);
  0 = off. Default 3.
- **Bucket width** — how close two spots must be to count as the same
  frequency. Default 500 Hz.

#### Marker colours

Set how the markers are coloured under **Marker colour**:

- **Source / client colour** (default) — colour by where the spot came from.
- **Single colour** — one custom colour you pick via the swatch.
- **By mode** — CW / phone / FT8-digital / RTTY tints.
- **By region** — a colour per continent.
- **By country** — a distinct colour per DXCC entity.

Tick **Show colour legend on the panadapter** to draw a small key.

- **Colour new spots** / **New-spot colour** / **New for** — a freshly
  arrived spot shows a distinct colour for N seconds so you notice it, then
  settles to the normal colour rules. (It's a *solid* colour, not a blink.)

The own-callsign highlight (below) and its optional toast still apply on top
of whichever colour mode you choose.

#### Your own callsign + notifications

- **Highlight my callsign when spotted** + **Highlight colour** — draw
  *your* spots in a colour you pick (default pink/magenta) so they stand out.
- **Pop up a notification when I'm spotted** — show a toast (and the header
  **★ Spotted!** badge) when your own callsign is spotted.
- **Re-notify after** — how long to wait before notifying again about your
  callsign, so you're not spammed (set to *every time* to disable the
  cooldown).

#### Other controls

- **Show spots on the panadapter** — master on/off for the overlay.
- **Max spots** — how many spots to keep in memory (the display caps above
  decide how many are *drawn*).
- **Spot lifetime** — how long (in **minutes**) a spot stays before it
  ages out.
- **Clear spots now** — wipe the current spot list.

---

## Settings → CAT / Serial (rig control over COM / TCP)

This tab lets a **logger or digital-mode app control Lyra over a serial port
or a TCP socket**, using the Kenwood CAT protocol — the same way you'd
connect software to a real Kenwood rig.  It's separate from the TCI server
(previous section): TCI is for TCI-aware apps like SDRLogger+; CAT is for the
huge world of apps that speak **Kenwood CAT** (WSJT-X, VarAC, fldigi, N1MM,
Log4OM, Hamlib, and most loggers).  So between the two tabs you have **three
ways** for software to talk to Lyra: TCI, CAT-over-COM, and CAT-over-TCP.

### How apps connect to Lyra

Lyra is the *rig*; your app is the *client*.  On Windows, a serial connection
between two programs on the same PC needs a **virtual COM-port pair** —
install **com0com** (free), which creates a linked pair like COM10 ↔ COM11.
Point your app at one end and Lyra at the other.  **TCP needs none of that** —
the app just connects to Lyra's host/port directly, which is why it's the
simpler choice if your app supports it.

### Serial PTT input

A digital-mode app keys Lyra over a serial PTT line (its "PTT method = RTS"
or "DTR").  The app asserts the line; Lyra goes to transmit, which keys your
HL2 / ANAN / Brick rig exactly as the on-screen MOX button does.

- **Enable** — turn it on (off by default).
- **COM port** — the port Lyra watches (the *other* end of your com0com pair
  from the app's PTT port).  **Rescan ports** refreshes the list.
- **PTT line** — **CTS** (if the app uses RTS) or **DSR** (if it uses DTR).
  On a com0com pair the app's RTS crosses to Lyra's CTS and DTR to DSR.  If
  PTT seems stuck or backwards, try the other line or **Invert**.
- **Invert** — key on the line going *low* instead of high.

The status line shows *Watching for PTT* / *PTT ASSERTED — transmitting* /
*Disabled*.

### Kenwood CAT server (two instances)

Each **Kenwood CAT** group is an independent server, so you can run (say) a
logger on one and a digital app on another, each on its own port:

- **Label** — a free-text note for what the port's for (e.g. *N1MM*, *VarAC*,
  *FLDIGI*) — purely for your own reference.
- **Transport** — **COM Port** or **TCP**.  Choosing one grays out the
  other's fields:
  - *COM Port:* pick the **COM port** (Lyra's end of the com0com pair) +
    **Baud / Data bits / Parity / Stop bits** to match the app (most apps
    are **115200, 8-N-1**).
  - *TCP:* set **TCP host** (usually `127.0.0.1`) and **TCP port** (e.g.
    `60000`) — no com0com needed.  Multiple apps can connect to one TCP
    instance at once.
- **Rig model** — **TS-480** (reports ID 020) or **TS-2000** (ID 019).
  Match this to the rig model selected in *your app*.  Thetis-flavoured
  profiles (e.g. VarAC's "Anan Thetis") expect **TS-2000**.
- **CAT server running** — start / stop the instance.  Watch the bottom
  status bar: it confirms *listening on …* / *on COM… @ …*, or tells you
  *cannot listen / cannot open* with the reason.

The app can then read and set **VFO frequency** and **mode**, and key TX
(`RX;` / `TX;`).  Data modes map the standard way: **USB-D → DIGU (MD9)**,
**LSB-D → DIGL (MD6)**.

> **Tip — fastest digital turnaround.** For modes like VarAC/VARA that flip
> RX↔TX rapidly, keying via **CAT** (set the app's PTT method to *CAT*) is
> event-driven and snappier than a polled RTS/DTR line.  The bigger lever for
> turnaround is **Settings → TX → TR Sequencing** (RF delay etc.) — safe to
> shorten into a dummy load / barefoot; mind the hot-switch note if a linear
> is in line.

---

## Settings → Visuals

Everything that controls how the panadapter *looks* lives on the
**Visuals** tab of Settings (**Ctrl+,**). Changes apply live — you see
them immediately. Your choices are saved between sessions.

### Trace color

How the spectrum trace (the moving signal line) is colored. Pick a mode
from the dropdown:

- **Solid color** — one fixed color for the whole trace. Choose from the
  preset color chips, or click **Custom color…** for any color you like.
- **By signal strength** — the trace color changes with how strong each
  part of the signal is, like a heat map (weak = dark/cool, strong =
  bright/hot). Pick a **palette** and a **preview strip** shows it (weak
  left, strong right). Choose **Custom color…** in the palette list to
  build your own dark-to-bright ramp from a base color.

### Spectrum fill

**Show fill beneath the trace** — fills the area under the signal line
with a soft gradient for a richer look. Turn it off for a clean,
line-only trace. (You can pick a separate **fill color** in solid mode.)

### Peak markers

Highlights the **held peak** of each signal across the tuned passband — a
running "high-water mark" so you can see the strongest a signal has been.
Drive the **mode** (Off / Live / timed / Hold) and **Decay** from the
[Display panel](#display-panel); the look lives here:

- **Show peak markers** — master on/off (the Display panel's Peak Hold
  combo also turns this on when you pick a mode).
- **Style** — **Line**, **Dots**, or **Triangles** (default Dots).
- **Show dB** — label the strongest few peaks with their level in dB.
- **Colour** — the marker color (default warm amber).
- **Decay** — fade rate in dB/s for the timed hold modes (the Display
  panel's Fast/Med/Slow are presets for this).

Peaks are drawn only inside your RX filter passband, so they track the
signal you're actually listening to.

### Zero-beat markers

**Show zero-beat markers** (off by default) enables the small **± needle**
under the frequency readout that helps you dead-tune a carrier. It appears
only in the carrier modes — **CWU, CWL, AM, SAM, FM** — and is described in
full under [Tuning panel → Zero-beat markers](#zero-beat-markers--dead-tune-a-carrier)
(it's also the fastest way to land a signal on frequency for the CW decoder,
which has no AFC).

### Noise-floor line

**Show noise-floor line** — a dashed reference line at the current noise
floor, with an **"NF −NN dBFS"** label. Handy for judging how far a signal
sticks up out of the noise. Pick its **Colour** (default sage green). On
by default.

### Trace smoothing

Smooths the trace over time so it's less jittery. **0 = Off** (rawest,
most responsive); higher values (up to **10**) give a calmer trace. This
is time-based smoothing — it does **not** flatten or hide real peaks.

### Peak glow

Adds a luminous bloom around strong peaks so they pop off the screen.
**0 % = Off**; raise it (up to **100 %**) for more glow. Costs a little
graphics work only when it's on.

### Glass sheen

A subtle glassy highlight across the panadapter for the "floating glass"
look. **0 % = Off**; **20 %** is the default. Raise for a stronger sheen.

### Tooltips

**Show tooltips** (default on) toggles the hover-help bubbles on controls
across the **whole app** — the front panels *and* the Settings dialog. Turn
it off for a quieter, no-popup UI once you know your way around. It's a
single global switch (Settings → Visuals → Tooltips).

### Gridline brightness

How bright the reference grid lines are, **0** (no grid) to **100**
(bright). Default **35**.

### Frame rate

How many times per second the panadapter redraws, **1–240 fps**. Default
**60**. Higher is smoother but uses more graphics power.

### dB range (floor / ceiling)

The exact numbers for the bottom (**floor**) and top (**ceiling**) of the
signal-strength scale. Most operators just drag the scale on the
panadapter edge, but you can type precise values here.

### Waterfall dB range (RX and TX)

The waterfall has its **own** dB floor / ceiling, independent of the
spectrum scale above, so you can tune the colour map for detail (a dark
waterfall with colours that **POP** on weak SSB) without changing how
the panadapter looks. Four numeric spinboxes plus an Auto switch:

- **Auto-fit the waterfall dB range** — tracks the band automatically
  (noise floor − 15 dB to peak + 15 dB). When on, the four manual rows
  below gray out. Off = use the floor/ceiling values you set.
- **Waterfall dB — RX floor / RX ceiling** — active when **MOX is OFF**.
  This is the scale your normal listening uses; drag the waterfall's
  right-edge during RX and these update automatically. Defaults
  **−120 / −20 dB**.
- **Waterfall dB — TX floor / TX ceiling** — active when **MOX is ON**.
  Separate from the RX pair so a key-down doesn't blow out your
  carefully-tuned listening scale. Defaults **−70 / +30 dB**, which
  frame a clean TX-tone line at typical HL2 drive levels (matching the
  reference's TX-side waterfall convention). Drag the waterfall's
  right-edge during MOX to tune from the panadapter instead — the
  spinboxes follow the drag live.

The RX and TX backings live on separate QSettings keys, so each pair
persists independently across sessions; existing operators who already
tuned their RX waterfall keep their settings on the same keys (no
upgrade migration needed).

The MOX edge switches which pair is **live** instantly — your RX
preferences (dark with pop) come back the moment you un-key, and the
TX scale snaps in the moment you key. If the live waterfall still
looks too hot on TX after first try, drag its right edge down a few dB
while keyed (or tighten the **TX ceiling** spinner) and it'll stick
for next time.

### Watermark

**Show Lyra constellation watermark** — a faint Lyra (the lyre /
constellation) image with a gently pulsing **Vega** star, over the
panadapter. On by default. Untick to hide it.

### Meteors

Occasional "shooting star" streaks that drift across the panadapter —
ambient sky-weather, deliberately rare.

- **Show occasional meteor streaks** — master on/off (on by default).
- **Meteor frequency** — average gap between meteors, **5–120 s**
  (default **30 s**). Lower = more frequent.
- **Gold fireballs** — percent chance a meteor is a warm-gold "fireball"
  instead of cool blue, **0–100 %** (default **15 %**).

### Graphics backend

*Advanced — restart to apply.* Which graphics engine Lyra renders with.
Leave it on **Auto (recommended)** — on Windows that's Direct3D 11, the
most compatible. **Vulkan / D3D12 / D3D11 / OpenGL** can be forced if you
know your card prefers one. (If panels ever fail to draw or crash when you
drag them, set this to **Direct3D 11** or **Auto** and restart.) This
setting stays on the machine — it isn't carried in an exported profile.

**Graphics safe mode (automatic).** If Lyra ever crashes *while starting up*
(a bad GPU driver can do this before the window is even up), the **next**
launch automatically drops to **OpenGL** so Lyra opens instead of crashing
again — you'll see a one-time notice, and no registry editing is needed. It
stays in safe mode until you pick a backend here (which clears it). If OpenGL
still crashes at startup, it steps down again to the **software** renderer
(slow, but it always works — enough to get in and change settings). If a
backend keeps crashing, the real fix is usually a **GPU-driver update**.

---

## Settings → Weather

Configure the [weather alerts](#weather-alerts) here. Nothing runs until
you accept the disclaimer and tick **Enable**.

- **Disclaimer** — tick to acknowledge the alerts are advisory only; this
  unlocks **Enable weather alerts**.
- **Sources** (enable any combination):
  - **Blitzortung** — global lightning network, free, no key. Uses your
    location.
  - **NWS / weather.gov** — US wind + severe-storm alerts, free, no key.
  - **NWS METAR** — live wind from a nearby station (set its **ICAO** below).
  - **Ambient Weather** — your own station (needs API + Application keys).
  - **Ecowitt** — your own station (needs Application key + API key + the
    gateway MAC).
- **Thresholds:**
  - **Lightning range** — how far out to consider strikes (shown in your
    chosen distance unit).
  - **Wind sustained ≥ / gust ≥** — the speeds (mph) that raise the wind
    tier.
  - **METAR station** — the ICAO id (e.g. `KTOL`) for live wind.
  - **Distance unit** — Miles or Kilometres (also used by the ⚡ badge).
- **Notifications:**
  - **Desktop notification on alert** — a system pop-up on a new alert.
  - **Audible chime on alert** — a beep on a new alert.
  - **Send test alert** — lights the badges + fires one notification for a
    few seconds so you can check it all works.
- **Station API credentials** — Ambient API/Application keys, Ecowitt
  Application/API keys, and Ecowitt gateway MAC (only needed for those
  sources).

Remember to set your location in
[Settings → Hardware → Operator / Station](#operator--station) — without
it, the location-based sources can't tell what's nearby.

---

## Credits and References

Lyra is a native C++23 / Qt 6 / Vulkan rebuild written from scratch by
**Rick Langford (N8SDR)**. The projects, documents, and conventions below
were studied for *ideas, ballistics, and protocol structure* while
building it. **No source code was copied** — each is referenced as
"how the standard idiom works," and Lyra implements each idea natively
in C++.

### Inspiration and references (studied, not copied)

- **Thetis SDR** (openHPSDR) — primary reference for HPSDR Protocol 1
  wire format, TX-chain ordering, meter ballistics, ATT-on-TX policy,
  TR-sequencing conventions, AAmixer routing.
- **openHPSDR project** — HPSDR Protocol 1 / 2 specifications, the
  authoritative wire-protocol documents.
- **PowerSDR** — Thetis's predecessor; convention provenance for many
  UI idioms (per-mode bandwidth memory, AGC profiles, etc.).
- **HermesLite 2 wiki + ak4951v4 gateware (RTL)** — the gateware-
  designer's authoritative register documentation, plus the Verilog
  source of the operator's specific HL2+ variant. The RTL is the
  ground-truth source for EP6 telemetry slot decoding, PA-enable
  mechanism, and TX-I/Q wire gating on the AK4951 gateware revision.
- **pihpsdr** — cross-reference for HL2 PA-enable bit, TX-drive
  nibble, and the generic Protocol-1 HL2 wire path.
- **Quisk** — cross-reference for HermesLite hardware register
  conventions (`hermes/quisk_hardware.py`).
- **linHPSDR** — additional cross-reference for the Protocol-1
  HL2 wire path (`protocol1.c`).
- **EESDR V3** — UI convention reference and the public **TCI**
  (Transceiver Control Interface) protocol specification that Lyra
  implements against.
- **Behringer X-Air mixer series** — operator-supplied screenshots
  drive the upcoming Lyra-native **Combinator** + 5-band parametric
  EQ design (lands in v0.2.1). The X-Air's multiband compressor +
  EQ behaviour is the reference for Lyra's TX dynamics chain.
- **SparkSDR** (binaries only — no source) — referenced for the HL2
  push-pull PA bias procedure documentation.
- **Lyra (Python predecessor)** — the author's earlier PySide6 SDR
  application. Several Lyra-cpp modules (band table, band-plan data,
  open-collector pin tables, grid-square math, frequency-display
  widget) are explicit ports from the Python predecessor — same
  author, same project lineage, internal port.

Lyra's source tree intentionally keeps these names out of code,
comments, commits, and operator-visible UI strings (the
"no-attribution rule"): the references inform the design, but every
line of Lyra is Lyra-native C++. This Credits section is the single
operator-facing place that names them, openly and once.

### Licensed components (GPL v3+)

Lyra is **GPL v3 or later** and is therefore license-compatible with
the GPL-licensed components it depends on:

- **WDSP** — DSP engine by **Warren Pratt (NR0V)**. GPL v3+. Lyra
  links to the WDSP shared library (`wdsp.dll` + FFTW companions)
  and calls its public C API through C++ wrappers. **No source
  modifications.** WDSP powers the RXA chain (noise reduction,
  AGC, bandpass, demod) and the TXA chain (modulator, ALC, leveler,
  bandpass, future EQ/compressor).
- **fldigi** — the RX **CW (Morse) decoder** is a faithful,
  no-drift source port of fldigi's CW *receive* chain (mixer →
  overlap-add FFT low-pass → adaptive slicer → timing FSM → Morse
  lookup) by **Dave Freese (W1HKJ)**, with the adaptive
  speed-tracking algorithm by **Lawrence Glaister (VE7IT)**. GPL
  v3+. Unlike the studied-but-not-copied references above, this is
  an actual code port — so, like WDSP, it is credited here as a
  licensed component. The port lives in `src/dsp/cw_fldigi/` with
  per-file provenance headers; see `CREDITS.md` / `NOTICE.md`.
- **TCI protocol** (Transceiver Control Interface) — public
  specification from **Expert Electronics** (EESDR). Lyra
  implements a TCI server compatible with the v1.9 / v2.0 spec so
  external logging / cluster / digital-modes software (SDRLogger+,
  any TCI-aware client) can drive Lyra.
- **WDSP FFTW** (Fastest Fourier Transform in the West) — GPL.
  Used by WDSP internally; bundled as `libfftw3-3.dll` /
  `libfftw3f-3.dll`. Lyra's first-launch FFTW WISDOM build optimises
  FFT plans for the host CPU and caches them under
  `%LOCALAPPDATA%\N8SDR\Lyra-cpp\fftw\`.

### Contributors

- **Rick Langford (N8SDR)** — project lead, architecture, all code.
- **Brent Crier (N9BC)** — co-contributor (joined during v0.0.9.1
  testing on the Python predecessor; continues into the C++ rebuild).
- **Timmy Davis (KC8TYK)** — tester (v0.1 tester flight).
- **W5UDX** — DSP2024P Plate Reverb presets and verification (lands
  with the Plate Reverb in v0.2.1).

### License

**GPL v3 or later.** Source repository:
[github.com/N8SDR1/Lyra-SDR-cpp](https://github.com/N8SDR1/Lyra-SDR-cpp).
Full license text: `LICENSE` / `NOTICE.md` in the source tree. See
the **About Lyra** dialog (**Help → About Lyra…**) for the version
+ build date of the running binary.

---

### Disclaimer — use at your own risk

Lyra is provided **"as is"**, with **no warranty** of any kind, express or
implied — including, without limitation, any warranty of merchantability,
fitness for a particular purpose, or non-infringement. This is the standard
GPL v3+ posture; see the `LICENSE` file for the complete legal terms. The
summary below is written plainly for operators, and does not replace or
limit that license text.

Lyra controls a **radio transmitter** and drives **external equipment**
(amplifiers, tuners, antennas, other SDRs, computers, and audio gear).
Radio transmission carries real risks — RF exposure, high voltage, fire,
equipment damage, and interference — and is legally regulated.

**You, the licensed operator, are solely responsible for:**

- Operating only within your **licence privileges** and your country's
  regulations — correct **bands, frequencies, power limits, emission
  types, and identification**.
- **RF-safety / exposure** compliance for your station and everyone near
  it.
- Protecting your **amplifier, antenna, feedline, and other equipment** —
  including correct drive levels, SWR, grounding, and switching sequences.
- **Verifying** Lyra's behaviour on your own station with your own
  instruments before trusting it on the air.

**The authors and contributors accept no liability** for any damage,
interference, injury, loss, regulatory violation, or other consequence
arising from the use, misuse, misconfiguration, or malfunction of this
software or of any equipment connected to it.

**Protection features are aids, not guarantees.** The amplifier watts cap,
SWR fold/cut, TX safety timeout, TR-sequencing delays, and similar
safeguards are designed to *reduce* risk, but they depend on correct
calibration, working hardware, and accurate readings. They can fail or be
misconfigured. **Never rely on them as your only line of defence** —
calibrate into a **dummy load**, confirm with an external watt-meter / SWR
meter, and keep your amplifier out of line until you have verified every
band. When in doubt, run less power.

---

## Support Lyra — buy the developer a coffee ☕

Lyra is **free**, **open-source** (GPL v3 or later — see the License), and
primarily built by **Rick Langford (N8SDR)** in his spare time, with
**Brent Crier (N9BC)** joining as co-contributor during early testing and
**Timmy Davis (KC8TYK)** joining for the v0.1 tester flight. There are no
ads, no telemetry, no subscription tier, no "pro" upsells.

If Lyra has saved you from a clunky SDR workflow, helped you work a new
state on FT8, or just makes your shack a little more comfortable, **a small
donation goes a long way** — it pays for the development hardware and keeps
the project moving through the long TX-path work still ahead.

**[☕ Donate via PayPal](https://www.paypal.com/donate/?business=NP2ZQS4LR454L&no_recurring=0&item_name=Built+by+a+fellow+ham%2C+for+the+community.++Free+to+use%2C+free+to+share.+A+small+donation+keeps+the+code+flowing.+73+de+N8SDR&currency_code=USD)**

Any amount is welcome — $5 buys a coffee, $20 buys a pint at the hamfest,
$100 keeps the development bench rolling for a month. You can also reach the
donate page any time from **Help → About Lyra**.

> *Built by a fellow ham, for the community. Free to use, free to share.
> A small donation keeps the code flowing. 73 de N8SDR.*

---

*Last updated as features ship. Sections are added when their feature
lands — see the maintainer note at the top.*

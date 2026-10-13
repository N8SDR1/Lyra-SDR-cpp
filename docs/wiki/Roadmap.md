# Roadmap

Where Lyra is headed. This is a **direction, not a dated schedule** — items land
when they're solid. Everything shipped today is on the [Feature Status](Feature-Status)
page.

**Now (v0.25.18 Sheliak):** HL2 / HL2+ P1, BrickSDR2 / Brick3SDR P2, SUB +
SPLIT, PureSignal, TX rack. Brick3 / ANAN-100D get a second SUB panadapter.
**Next major feature:** [Diversity](#diversity-next-dual-adc-feature) (two ADCs,
same frequency — not a second VFO). **Then:** G2 on-air validation, 7000/8000
when OrionMkII BPF is in scope, Linux then macOS.

> **Legend:** ✅ done · 🚧 in progress · 🗺️ planned · 💡 exploring (early idea, not scheduled)

## Major features

### ✅ SUB / RX2 + SPLIT (HL2, BrickSDR2, Brick3SDR, P2 ANAN)

Same operator model everywhere: second DDC **and** independent **SPLIT**
TX on VFO B. One ADC (HL2 / Brick2 / ANAN-10 / 100): overlay on the main
pane; N2ADR / analog filter follows **RX1** (cross-band SUB is much weaker).
Two ADC (**Brick3SDR / ANAN-100D / 200D**): **SUB** opens a **second
panadapter** (own waterfall, band-plan, click-tune, Visuals floor/ceiling).
**RX2 jack isolate on TX** (default on) for those two-converter boxes.
Cues: orange **TUNE A** / lime **TUNE B**, cyan vs green passbands,
red vs green band chips, off-span **◀ RX2** / **RX2 ▶**, lime **TX**
marker (red on key). TCI: `channel_count:2`; `vfo:0,1` is SPLIT VFO B;
`vfo:1,0` / `dds:1` is SUB. Logger Combo RST still uses **RX1**.

### 🗺️ Diversity (next dual-ADC feature)

Two converters, **same frequency**, phase/gain combine — not a second VFO
(that is SUB). Parked until we start it. Brick2 / HL2 cannot (one ADC).
Does **not** use ANT2; the second RF is the **RX2 jack** (Alex Ext1).

### ✅ PureSignal — adaptive predistortion

Live on **HL2 / HL2+** (coupler hardware mod), **BrickSDR2** (Protocol-2
ADC0 pad), and **Brick3SDR** (rear **PS** jack). Compact **PureSignal** dock plus **Amp View** plot
(Snap / Hold). Feedback glance chip (teal below 129, green 129–181, red
above). Auto-att **persists across PTT** so SSB does not re-hunt every
keyup. Attestation in **Settings → TX** (default off). **2-tone** on the
TX panel is the usual tune-up carrier.
Operator detail: **[User Guide → PureSignal](User-Guide#puresignal)**.

### ✅ Protocol 2 on BrickSDR2 / Brick3SDR · 🚧 Classic ANAN P2 on-air (G2 dummy-load) · 🗺️ 7000/8000

HPSDR **Protocol 2** is live on **BrickSDR2** and **Brick3SDR** (RX + TX,
including radio mic → modulator, analog drive, watts-cap, ATT-on-TX).
Brick3 is **beta testing** (v0.25.18) — TX like BrickSDR2 (no dummy-load
arm); first RF dummy load. **SUB** opens a second panadapter. A Brick that
still answers **Protocol 1** is **discovered but not opened** — P1 TX in
Lyra is HL2-only. Flash **P2** (Anton / linoobs for Brick images).
**ANAN-10 / 10E** = BrickSDR2 TX; **ANAN-100 / 100B / 100D / 200D** =
Brick3SDR TX. **G2 / G2-1K** still dummy-load arm. Boxes that shipped as
Protocol 1 should run the **P2 FPGA** when they can. **7000DLE / 8000**
(OrionMkII BPF) stay locked.

## Platforms

### 🗺️ Linux, then macOS

Lyra is **Windows-only today**. Linux support is planned as a future version,
with macOS to follow. The codebase is C++23 / Qt 6, so the ground is prepared —
these are real roadmap items, not "maybe someday."

## Smaller items on the list

- ✅ **WDSP 2.10** — Windows engine + NNR (v0.25.8; factory model Standard from v0.25.9). Mac/Linux binaries later.
- ✅ **NR-C rectangular panadapter** — display on uncleaned IQ; Floor −24 (v0.25.10).
- ✅ **Session recorder** — RX WAV on P1 and P2; Vulkan snapshots (v0.25.10).
- ✅ **First-run FFTW vs companion launch** — Hardware Startup waits until FFTW and the radio layer are up (v0.25.11).
- ✅ **TCI STOP** does not close the radio (v0.25.11) and does not drop MOX (v0.25.13). **TUNE / TRX / CHRONO** key like the host panel (v0.25.14).
- ✅ **Brick3 Beta Testing** — Protocol 2 RX+TX, MAP, Auto Step, per-band PS pad, Reset wipe (v0.25.16); dual-ADC SUB pane, on-air TX, ANAN mapping (v0.25.17); RX2 jack isolate on TX (v0.25.18).
- ✅ **HL2 analog J4 pin 8** — leftover I2C burst dropped; Pico latches on BYTE0 (v0.25.18).
- ✅ **11 m / CB chips on the SUB pane** — click-tune mode follows Hardware Region (v0.25.18).
- ✅ **Windows per-app mixer** volume remembered across Stop / Start (v0.25.17).
- ✅ **PureSignal Voice** on the compact dock; engine chips on Amp View (v0.25.17).
- ✅ **RIT / XIT** on the Tuning panel.
- ✅ **Protocol 2 TX follows the VFO after retune** (v0.25.15; BrickSDR2).
- ✅ **Keyboard VFO** Left/Right 1 kHz, Up/Down 10 Hz (v0.25.15).
- ✅ **Startup profile** uses mode binding then last-used, not always Default (v0.25.15).
- ✅ **Tuning Mem** left-click after idle (v0.25.15).
- ✅ **Rec default folder** is `%USERPROFILE%\Lyra\Recordings`, never Program Files (v0.25.12).
- ✅ **VAC2** — second independent virtual-audio cable (RX2 / SUB); enable,
  auto-digital, gains, and latency store in TX profiles (schema 6). Audio
  device names stay global.
- ✅ **2-tone test generator** — on the TX panel (linearity / dummy-load
  tune-up); PureSignal uses it as the usual tune-up carrier
- 🗺️ Per-profile independent RX/TX filter lows
- 🗺️ Continued polish across the DSP, UI, and metering as testers report back
- ✅ **Companion Apps** — third-party digital / SSTV / logger programs launched from Lyra (native RTTY modem will not return; CW stays in-radio)

## Exploring — further out 💡

Ideas we're interested in but haven't scheduled. They sit behind the major
features above and may change shape or timing.

### 💡 Remote operation

Run a radio at one location from a Lyra somewhere else, over the internet — a
purpose-built **Lyra-to-Lyra** link that carries the DSP, compressed audio, and
spectrum, with the operating position's controls driving the remote radio. This
is an early idea, not a dated feature: it sits **behind** the remaining
platform work (Linux / macOS, 7000/8000),
and would only ship with **mandatory authentication, encryption,
and fail-safe transmit** — a dropped or degraded link must never leave the
transmitter keyed.

### 💡 TX rack setup without RF (looped speech + bandwidth)

Dial in EQ / speech / Combinator / Plate **without keying** — no MOX, no
TX I/Q, PA stays off. Optional **20–30 s recorded speech**, looped into the
same pre-rack mic funnel so rack knobs and **TX bandwidth (3 / 4 / 6 kHz
and ESSB)** can be A/B'd on identical words.

Today: **▶ Review** is raw WAV (no rack); **MON TX** is post-rack **before**
ALC and TX bandpass, and only while keyed. Hearing 3 kHz vs 6 kHz needs a
**post-bandpass listen tap**, not the current MON tap. Parked — later add-in.

## Want to influence it?

Feature requests and bug reports genuinely steer the order of work —
**[open an issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)** or join the
community on Discord. Tester hardware for non-HL2 radios is especially welcome.

---

**See also:** [Feature Status](Feature-Status) · [Supported Radios](Supported-Radios) · [Home](Home)

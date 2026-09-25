# Roadmap

Where Lyra is headed. This is a **direction, not a dated schedule** — items land
when they're solid. Everything shipped today is on the [Feature Status](Feature-Status)
page.

> **Legend:** ✅ done · 🚧 in progress · 🗺️ planned · 💡 exploring (early idea, not scheduled)

## Major features

### ✅ SUB / RX2 + SPLIT on HL2 (P1) and BrickSDR2 (P2)

**Hermes Lite 2 / 2+** and **BrickSDR2** share the same SUB / SPLIT
operator model: second DDC (DDC1) **and** independent **SPLIT** TX on
VFO B. One ADC — N2ADR / analog filter follows **RX1** (cross-band SUB
is much weaker). Operator cues: orange **TUNE A** / lime **TUNE B**,
**cyan** RX1 passband vs **green** RX2 overlay, **red** vs **green** band
chips, off-span **◀ RX2** / **RX2 ▶** (click to swap onto the panadapter),
lime **TX** marker (red on key). TCI: `channel_count:2`; `vfo:0,1` is
SPLIT VFO B; `vfo:1,0` / `dds:1` is SUB. Logger Combo RST still uses
**RX1**.

### 🗺️ PureSignal — adaptive predistortion

Real-time linearization of the transmit signal using the radio's feedback path,
for a cleaner, stronger signal with less IMD. Requires the HL2 PureSignal
hardware mod. (A **2-tone test generator** ships alongside it as the tune-up
companion.)

### ✅ Protocol 2 on BrickSDR2 · 🚧 Classic ANAN P2 dummy-load · 🗺️ 7000/8000

HPSDR **Protocol 2** is live on the **BrickSDR2** (RX + TX, including radio
mic → modulator, analog drive, watts-cap, ATT-on-TX). **ANAN-10 / 10E / 100 /
100B / 100D / 200D** now have classic-Alex P2 profiles (deskHPSDR HPF edges);
TX stays dummy-load until a tester validates RF. Boxes that shipped as
Protocol 1 should run the **P2 FPGA** when they can — Lyra will not grow a
separate P1 ANAN TX driver. **G2 / G2-1K** already had Saturn profiles.
**7000DLE / 8000** (OrionMkII BPF) stay locked.

## Platforms

### 🗺️ Linux, then macOS

Lyra is **Windows-only today**. Linux support is planned as a future version,
with macOS to follow. The codebase is C++23 / Qt 6, so the ground is prepared —
these are real roadmap items, not "maybe someday."

## Smaller items on the list

- ✅ **VAC2** — second independent virtual-audio cable (RX2 / SUB); enable,
  auto-digital, gains, and latency store in TX profiles (schema 6). Audio
  device names stay global.
- ✅ **2-tone test generator** — on the TX panel (linearity / dummy-load
  tune-up); PureSignal still uses it as a companion when PS lands
- 🗺️ Per-profile independent RX/TX filter lows
- 🗺️ Continued polish across the DSP, UI, and metering as testers report back

## Exploring — further out 💡

Ideas we're interested in but haven't scheduled. They sit behind the major
features above and may change shape or timing.

### 💡 Remote operation

Run a radio at one location from a Lyra somewhere else, over the internet — a
purpose-built **Lyra-to-Lyra** link that carries the DSP, compressed audio, and
spectrum, with the operating position's controls driving the remote radio. This
is an early idea, not a dated feature: it sits **behind PureSignal**,
and would only ship with **mandatory authentication, encryption,
and fail-safe transmit** — a dropped or degraded link must never leave the
transmitter keyed.

## Want to influence it?

Feature requests and bug reports genuinely steer the order of work —
**[open an issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)** or join the
community on Discord. Tester hardware for non-HL2 radios is especially welcome.

---

**See also:** [Feature Status](Feature-Status) · [Supported Radios](Supported-Radios) · [Home](Home)

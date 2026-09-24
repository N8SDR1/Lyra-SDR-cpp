# Roadmap

Where Lyra is headed. This is a **direction, not a dated schedule** — items land
when they're solid. Everything shipped today is on the [Feature Status](Feature-Status)
page.

> **Legend:** ✅ done · 🚧 in progress · 🗺️ planned · 💡 exploring (early idea, not scheduled)

## Major features

### ✅ SUB / RX2 on BrickSDR2 · 🗺️ SPLIT pile-up polish

**BrickSDR2** already runs a second receiver (DDC1): two VFOs, a second
pan/waterfall overlay, and audio, including split-band and an off-span jump
to the other RX. Still planned: **SPLIT** pile-up UX (dedicated TX marker /
tri-state SUB–SPLIT) and dual RX on Hermes Lite 2 Protocol 1.

### 🗺️ PureSignal — adaptive predistortion

Real-time linearization of the transmit signal using the radio's feedback path,
for a cleaner, stronger signal with less IMD. Requires the HL2 PureSignal
hardware mod. (A **2-tone test generator** ships alongside it as the tune-up
companion.)

### ✅ Protocol 2 on BrickSDR2 · 🗺️ ANAN family still planned

HPSDR **Protocol 2** is live on the **BrickSDR2** (RX + TX, including radio
mic → modulator, analog drive, watts-cap, ATT-on-TX). Making the **ANAN**
family (G2, G2-1K, 7000DLE, 8000, …) first-class is still roadmap work —
same protocol family, different DDC/PA/filter models, and it needs a tester
with the hardware. Other HPSDR Protocol-1 boards are planned the same way.

## Platforms

### 🗺️ Linux, then macOS

Lyra is **Windows-only today**. Linux support is planned as a future version,
with macOS to follow. The codebase is C++23 / Qt 6, so the ground is prepared —
these are real roadmap items, not "maybe someday."

## Smaller items on the list

- 🗺️ **VAC2** — a second independent virtual-audio channel (e.g. a logger's
  audio separate from your digital-mode app)
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
is an early idea, not a dated feature: it sits **behind SPLIT polish and
PureSignal**, and would only ship with **mandatory authentication, encryption,
and fail-safe transmit** — a dropped or degraded link must never leave the
transmitter keyed.

## Want to influence it?

Feature requests and bug reports genuinely steer the order of work —
**[open an issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)** or join the
community on Discord. Tester hardware for non-HL2 radios is especially welcome.

---

**See also:** [Feature Status](Feature-Status) · [Supported Radios](Supported-Radios) · [Home](Home)

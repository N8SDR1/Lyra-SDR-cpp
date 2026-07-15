# Roadmap

Where Lyra is headed. This is a **direction, not a dated schedule** — items land
when they're solid. Everything shipped today is on the [Feature Status](Feature-Status)
page.

> **Legend:** ✅ done · 🚧 in progress · 🗺️ planned · 💡 exploring (early idea, not scheduled)

## Major features

### 🗺️ RX2 — dual receiver

A second receiver: watch two frequencies at once, stereo-split audio
(one receiver in each ear), a focus model for which VFO the controls follow,
and **SPLIT** operation for working DX pile-ups. RIT/XIT extend per-receiver.

### 🗺️ PureSignal — adaptive predistortion

Real-time linearization of the transmit signal using the radio's feedback path,
for a cleaner, stronger signal with less IMD. Requires the HL2 PureSignal
hardware mod. (A **2-tone test generator** ships alongside it as the tune-up
companion.)

### 🗺️ HPSDR Protocol 2 + ANAN family

Add HPSDR **Protocol 2** so the **ANAN** family (G2, G2-1K, 7000DLE, 8000, …)
becomes first-class, alongside the existing HL2 Protocol-1 path. Other HPSDR
Protocol-1 boards are planned too — those mostly need a **tester with the
hardware** to bring live.

## Platforms

### 🗺️ Linux, then macOS

Lyra is **Windows-only today**. Linux support is planned as a future version,
with macOS to follow. The codebase is C++23 / Qt 6, so the ground is prepared —
these are real roadmap items, not "maybe someday."

## Smaller items on the list

- 🗺️ **VAC2** — a second independent virtual-audio channel (e.g. a logger's
  audio separate from your digital-mode app)
- 🗺️ **2-tone test generator** — the PureSignal / linearity tune-up tool
- 🗺️ Per-profile independent RX/TX filter lows
- 🗺️ Continued polish across the DSP, UI, and metering as testers report back

## Exploring — further out 💡

Ideas we're interested in but haven't scheduled. They sit behind the major
features above and may change shape or timing.

### 💡 Remote operation

Run a radio at one location from a Lyra somewhere else, over the internet — a
purpose-built **Lyra-to-Lyra** link that carries the DSP, compressed audio, and
spectrum, with the operating position's controls driving the remote radio. This
is an early idea, not a dated feature: it sits **behind RX2 and PureSignal**,
and would only ship with **mandatory authentication, encryption, and fail-safe
transmit** — a dropped or degraded link must never leave the transmitter keyed.

## Want to influence it?

Feature requests and bug reports genuinely steer the order of work —
**[open an issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)** or join the
community on Discord. Tester hardware for non-HL2 radios is especially welcome.

---

**See also:** [Feature Status](Feature-Status) · [Supported Radios](Supported-Radios) · [Home](Home)

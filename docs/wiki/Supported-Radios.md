# Supported Radios

Lyra speaks **HPSDR Protocol 1** (Hermes Lite 2 family) and **Protocol 2**
(**BrickSDR2**). The architecture is built to grow to other HPSDR hardware
as testers come on board.

> **Legend:** ✅ supported today · 🚧 in progress · 🗺️ planned

## Currently supported ✅

| Radio | Notes |
|---|---|
| **Hermes Lite 2 (HL2)** | ✅ Full RX + TX over Protocol 1. Audio to/from the PC (see [First Voice Setup](First-Voice-Setup)). |
| **Hermes Lite 2+ (HL2+, AK4951 codec)** | ✅ Full RX + TX over Protocol 1. Adds the on-board **mic + headphone jacks** — plug a headset straight into the radio, no PC audio setup needed. |
| **BrickSDR2** | ✅ Full RX + TX over Protocol 2, including **SUB / RX2** (second DDC, split-band). Radio mic modulates SSB/AM/FM; TUN / two-tone / analog drive / watts-cap / ATT-on-TX are live. Dummy-load first. Dual RX needs current Brick2 FPGA. Discovery firmware is shown as **v10.6**-style. |

All connect over a **wired Ethernet** link and are found automatically by
Lyra's discovery (or **Add by IP** for a fixed address / different subnet).

### Which one do I have?

Look at the radio: if it has a **MIC jack and a headphone jack on the box**,
it's an **HL2+ (AK4951)**. If it only has network + power + antenna, it's a
**standard HL2**. Both work great — the "+" just lets you keep all the audio on
the radio instead of the PC. A **BrickSDR2** is a separate Protocol 2 box;
Lyra lists it in discovery as Brick, not as an HL2.

## Planned 🗺️

| Radio / family | Protocol | Status |
|---|---|---|
| **ANAN family** (G2, G2-1K, 7000DLE, 8000, …) | HPSDR **Protocol 2** | 🗺️ Planned — Protocol 2 is live on BrickSDR2; ANAN still needs per-model DDC/PA/filter bring-up plus a tester with the hardware. |
| Other **HPSDR Protocol-1** boards | Protocol 1 | 🗺️ Planned — the transmit wire layer already has non-HL2 branches; bringing them live needs a **tester with the hardware**. |

If you'd like to help test Lyra on a non-HL2 HPSDR radio, please
**[open an issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)** — tester
hardware is exactly what unblocks this.

---

**See also:** [PC Requirements](PC-Requirements) · [Feature Status](Feature-Status) · [Roadmap](Roadmap)

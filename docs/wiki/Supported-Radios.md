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
| **BrickSDR2** | ✅ Full RX + TX over Protocol 2, including **SUB / RX2** (second DDC) and **SPLIT** (TX on VFO B, independent of SUB). Cues: orange **TUNE A** / lime **TUNE B**, cyan vs green passbands, **◀ RX2** / **RX2 ▶**. Radio mic modulates SSB/AM/FM; TUN / two-tone / analog drive / watts-cap / ATT-on-TX are live. Dummy-load first. Dual RX needs current Brick2 FPGA. Discovery firmware is shown as **v10.6**-style. |

## In progress 🚧

| Radio | Notes |
|---|---|
| **ANAN-10 / 10E / 100 / 100B / 100D / 200D** (Protocol 2) | 🚧 RX + TX **arm** using deskHPSDR classic Alex HPF/LPF words. **Not on-air validated** — dummy-load + Arm P2 TX. These radios often shipped Protocol 1; **use the P2 FPGA** if the box can run it — that is Lyra's path. Discovery **Hermes** still defaults to BrickSDR2 (same board id); pick the marketed ANAN model in **Settings → Hardware**. A leftover Protocol 1 discovery row is refused (wrong HL2 TX layout). |
| **ANAN-G2 / G2-1K** | 🚧 Saturn BPF profile; TX dummy-load arm until on-air validated. |

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
| **ANAN-7000DLE / 8000** | Protocol 2 | 🗺️ Locked — OrionMkII BPF / PA not in this pass. |
| Classic ANAN still on **Protocol 1** firmware | Protocol 1 | 🗺️ Not a Lyra TX path (HL2 layout). Flash **Protocol 2** if the hardware allows, then use the P2 profile above. |

If you'd like to help test Lyra on a non-HL2 HPSDR radio, please
**[open an issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)** — tester
hardware is exactly what unblocks this.

---

**See also:** [PC Requirements](PC-Requirements) · [Feature Status](Feature-Status) · [Roadmap](Roadmap)

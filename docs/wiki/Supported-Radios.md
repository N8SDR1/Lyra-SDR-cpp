# Supported Radios

Lyra speaks **HPSDR Protocol 1** (Hermes Lite 2 family) and **Protocol 2**
(**BrickSDR2**). The architecture is built to grow to other HPSDR hardware
as testers come on board.

> **Legend:** ✅ supported today · 🚧 in progress · 🗺️ planned

> ### Protocol 1 Brick / ANAN — listed, not opened
>
> Discovery still **finds** a Brick or classic ANAN that answers **Protocol 1**
> (Metis on UDP 1024). Lyra **will not Open** that row.
>
> Protocol 1 TX in Lyra is the **Hermes Lite 2** wire layout (PA enable, drive,
> attenuator, C&C, TX I/Q packing). A Brick on the old P1 FPGA (or an ANAN
> still on P1) answers as generic **Hermes**, not HermesLite. Connecting it
> would send HL2 bytes into the wrong gateware — typical results are **no RF**,
> **wrong T/R / PA**, **wrong power**, **RX left wide open while keyed**, or a
> **dirty / off-frequency** first burst. We refuse rather than half-drive it.
>
> **BrickSDR2 is supported on Protocol 2 only.** Flash a **P2 FPGA**, rediscover,
> and Open the **Protocol 2** row. For Brick flash / image help, ask **Anton
> (linoobs)** on Discord. Dummy load on the first TX after a flash.
>
> Classic ANAN that can run P2 should do the same, then pick the marketed
> model in **Settings → Hardware**. Leftover P1 rows stay refused.

## Currently supported ✅

| Radio | Notes |
|---|---|
| **Hermes Lite 2 (HL2)** | ✅ Full RX + TX over Protocol 1, including **SUB / RX2** (DDC1, same ADC) and **SPLIT**. **PureSignal** with the hardware coupler mod. N2ADR / filter board follows RX1 — cross-band SUB is much weaker. Audio to/from the PC (see [First Voice Setup](First-Voice-Setup)). |
| **Hermes Lite 2+ (HL2+, AK4951 codec)** | ✅ Same Protocol 1 RX/TX/SUB/PS as HL2. Adds the on-board **mic + headphone jacks** — plug a headset straight into the radio, no PC audio setup needed. |
| **BrickSDR2** | ✅ Full RX + TX over **Protocol 2 only**, including **SUB / RX2** (second DDC, same ADC), **SPLIT**, and **PureSignal** (ADC0 feedback pad). Cues: orange **TUNE A** / lime **TUNE B**, cyan vs green passbands, **◀ RX2** / **RX2 ▶**. Radio mic modulates SSB/AM/FM; TUN / two-tone / analog drive / watts-cap / ATT-on-TX are live. Dual RX needs current Brick2 FPGA. Discovery firmware is shown as **v10.6**-style. A Brick still on **Protocol 1** will appear in Discover and is **refused on Open** (see callout above). |

## In progress 🚧

| Radio | Notes |
|---|---|
| **ANAN-10 / 10E / 100 / 100B / 100D / 200D** (Protocol 2) | 🚧 RX + TX **arm** using classic Alex HPF/LPF words. **Not on-air validated** — dummy-load + Arm P2 TX. These radios often shipped Protocol 1; **use the P2 FPGA** if the box can run it — that is Lyra's path. Discovery **Hermes** still defaults to BrickSDR2 (same board id); pick the marketed ANAN model in **Settings → Hardware**. A leftover Protocol 1 discovery row is refused (wrong HL2 TX layout). |
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

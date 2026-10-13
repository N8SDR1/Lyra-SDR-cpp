# Credits

Full file in the repo: [CREDITS.md](https://github.com/N8SDR1/Lyra-SDR-cpp/blob/main/CREDITS.md).
Lyra is GPL-3.0-or-later. Ported engines (WDSP, ChannelMaster / Thetis TX
baseline, fldigi CW receive) are attributed there file-by-file.

## Reference projects (studied, not copied)

These hosts are cross-checked when Lyra needs ground truth on the wire.
**No code is incorporated from them.**

| Project | Who | Why Lyra looks |
|---|---|---|
| **DeskHPSDR** | **Heiko (DL1BZ)** | Protocol 2, BrickSDR / Angelia DDC maps, PureSignal host path, analog / Band Volts, portable I/O |
| **pihpsdr** | John Melton, G0ORX | HL2 protocol (PA enable, ATT-on-TX) |
| **Quisk** | Jim Ahlstrom | HL2 C&C register decode |
| **linHPSDR** | — | HL2 protocol fidelity |
| **Hermes Lite 2 gateware** | Steve Haynal, KF7O + community | FPGA RTL as wire-byte ground truth (`hl2b5up_ak4951v4`) |

## Special thanks

- **Heiko (DL1BZ)** — again: DeskHPSDR is the Protocol 2 / Brick
  reference we keep coming back to. Time, builds, and straight talk
  whenever Lyra needed a sanity check.
- **Anton (linoobs)** — BrickSDR / Brick3SDR FPGA images and flash
  help so operators can run Protocol 2 (the layout Lyra actually
  transmits on).
- The **openHPSDR community**, **TAPR**, **Dr. Warren Pratt (NR0V)**,
  **Reid (MI0BOT / GI8TME)**, **Richie Samphire (ramdor)**, **Laurence
  Barker**, and the **Hermes Lite 2 community**.

## Contributors

- **Rick Langford (N8SDR)** — project author, architecture, bench operator.
- **Brent Crier (N9BC)** — co-contributor and tester; DeepFist neural CW.
- **Timmy Davis (KC8TYK)** — tester (v0.1 tester flight).
- **W5UDX** — DSP2024P Plate Reverb presets and verification.

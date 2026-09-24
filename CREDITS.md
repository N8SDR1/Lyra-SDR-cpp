# Credits — Lyra-cpp

## Project author

- **Rick Langford (N8SDR)** — project author, primary
  developer, bench operator.  Built Lyra-cpp on his Hermes
  Lite 2+ across daily bench cycles + field testing.

---

## Upstream open-source projects

Lyra-cpp would not be possible without these GPL-compatible
upstream open-source projects.  See [NOTICE.md](NOTICE.md)
for the full attribution detail.

### openHPSDR Thetis — TX baseline architecture

Lyra-cpp ports the TX baseline architecture (ChannelMaster
orchestration layer, audio mixer, TX interleave) from the
openHPSDR Thetis project.  Specific source: the **MI0BOT
OpenHPSDR-Thetis fork**, version 2.10.3.13, which bundles the
upstream ramdor mainline with Reid's HL2-specific modifications.

#### Core architecture & modern maintainers

- **Richie Samphire (ramdor)** — maintainer of the primary
  Thetis GitHub repository.  Responsible for the modern
  ongoing development, C-runtime optimizations, and software
  releases tailored for high-end transceivers (Apache Labs
  ANAN family etc.).
  https://github.com/ramdor/Thetis
- **Laurence Barker** — key developer for contemporary
  Thetis variants.  Author of the Thetis User Manual that
  ships with the application.

#### Hardware integration & branch maintainers

- **Reid (MI0BOT / GI8TME)** — maintainer of the mi0bot
  OpenHPSDR-Thetis fork.  Heavily modified and ported the
  upstream codebase to provide native functionality,
  firmware emulations, and I2C controls for the Hermes
  Lite 2 / 2+ open-hardware SDR ecosystem.  **Lyra-cpp's
  TX baseline ports specifically from this fork**, which
  is why Reid's HL2-specific work is foundational to
  Lyra-cpp's HL2/HL2+ TX path.
  https://github.com/mi0bot/OpenHPSDR-Thetis

#### Foundational DSP & base code contributors

- **Dr. Warren Pratt (NR0V)** — author of the foundational
  WDSP (Warren's Digital Signal Processing) library that
  drives the spectrum/FFT processing, filtering, and
  underlying DSP math the entire openHPSDR family
  (including Thetis and Lyra-cpp) relies on.  Also the
  author of the ChannelMaster core code (cmaster.c,
  aamix.c, ilv.c) that Lyra-cpp's TX baseline ports.
  Separately credited below as Lyra-cpp's RX DSP engine
  author.
- **The TAPR / OpenHPSDR development team** — the
  historical project group that maintained the transition
  from PowerSDR to OpenHPSDR-Thetis, providing the
  long-running open-source foundation.  TAPR (Tucson
  Amateur Packet Radio) hosts the openHPSDR project
  infrastructure.
  https://www.tapr.org/ — https://openhpsdr.org/

### WDSP — DSP algorithm library

- **Dr. Warren Pratt (NR0V)** — author of the WDSP DSP
  engine used by Lyra-cpp for the RX audio chain (NR,
  AGC, ANF, LMS, NB, AEPF, bandpass, demod, panel pan)
  and the TX audio chain (ALC, leveler, compressor,
  CFC, PHROT).  PureSignal (v0.3) will additionally port
  WDSP's calcc.c and iqc.c.  WDSP is GPL v3+.
  https://github.com/TAPR/OpenHPSDR-wdsp

### fldigi — CW (Morse) receive decoder

Lyra-cpp's RX CW decoder is a faithful, no-drift port of the
CW *receive* chain from **fldigi** (mixer → overlap-add FFT
low-pass → magnitude → decimate → adaptive slicer → timing
FSM → Morse lookup).  fldigi is GPL v3+.

- **Dave Freese (W1HKJ)** — author and maintainer of fldigi,
  including the CW modem (`cw.cxx`/`morse.cxx`), the
  overlap-add FFT filter (`fftfilt.cxx`), and the running-mean
  helpers (`filters.h`) that Lyra-cpp's decoder ports.
- **Lawrence Glaister (VE7IT)** — author of fldigi's adaptive
  CW speed-tracking algorithm, which Lyra-cpp ports verbatim.
  http://www.w1hkj.com/ — https://sourceforge.net/projects/fldigi/

### DeepFist — neural CW (Morse) decoder

Lyra-cpp's optional neural CW decode engine embeds **DeepFist**,
a clean-room, self-trained neural (spectrogram → CNN + CTC)
Morse decoder that runs via **ONNX Runtime** (Microsoft, MIT).
Both are MIT-licensed — see [NOTICE.md](NOTICE.md).

- **Brent Crier (N9BC)** — author of DeepFist: the synthetic-data
  generator, the CNN+CTC model, its training, and the ONNX
  export the decoder embeds.

---

## Reference projects (cross-checked, not source-incorporated)

These projects are referenced in Lyra-cpp's investigation
trail but **no code is incorporated from them**:

- **pihpsdr** (John Melton, G0ORX) — used as a cross-
  reference for HL2 protocol verification during the
  PA-enable + ATT-on-TX reconcile work (see CLAUDE.md
  §15.26 for the cross-reference investigation trail).
  https://github.com/dl1ycf/pihpsdr
- **Quisk** — additionally cross-referenced for HL2 C&C
  register decoding during the same investigation.
  https://github.com/jimahlstrom/quisk
- **linHPSDR** — additionally cross-referenced for HL2
  protocol fidelity verification.
- **Hermes Lite 2 gateware** (Steve Haynal, KF7O +
  community) — the HL2 FPGA gateware itself.  Lyra-cpp
  reads the gateware RTL (`hl2b5up_ak4951v4` variant) as
  ground truth for HL2+ AK4951 wire-byte semantics; no
  gateware code is ported into Lyra-cpp (Lyra-cpp is the
  host software, not the gateware).
  https://github.com/softerhardware/Hermes-Lite2

---

## Testers and field operators

(Will be filled in as testers come on board for v0.2 TX
bench testing.  Currently: N8SDR is the primary tester and
bench operator.  Anyone who contributes field reports or
bench data will be credited here with their consent.)

---

## Special thanks

- The **openHPSDR community** — for keeping HPSDR Protocol
  alive and documented over many years.
- **TAPR** — for hosting the openHPSDR project
  infrastructure and the Thetis source repositories.
- **Dr. Warren Pratt (NR0V)** — for WDSP itself + the
  ChannelMaster core code, which together form the DSP
  and TX-orchestration heart of every modern HPSDR-class
  SDR application.
- **Reid (MI0BOT / GI8TME)** — for the HL2-specific work
  that makes Hermes Lite 2 / 2+ first-class on every
  HPSDR app including this one.
- **Richie Samphire (ramdor)** — for maintaining and
  modernizing Thetis, which provides the proven TX
  baseline Lyra-cpp ports.
- **Laurence Barker** — for contemporary Thetis
  development and the Thetis User Manual.
- **Hermes Lite 2 community** — for the open hardware
  that this project transmits on.

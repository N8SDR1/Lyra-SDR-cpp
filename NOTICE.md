# NOTICE — Lyra-cpp Upstream Attribution

Lyra-cpp is licensed under the GNU General Public License v3 or
later (see [LICENSE](LICENSE)).  It incorporates source code
ported from the following GPL-compatible upstream open-source
projects.

---

## WDSP — DSP algorithm library

- **Project:** WDSP (Warren's Digital Signal Processing)
- **Author:** Dr. Warren Pratt, NR0V
- **Upstream:** https://github.com/TAPR/OpenHPSDR-wdsp
- **Original copyright:** (C) 2012-present Warren Pratt, NR0V
- **License:** GNU General Public License v3 or later

### Usage in Lyra-cpp

Lyra-cpp links the pre-built WDSP DLL (`wdsp.dll`) and calls into
it from native C++ via cffi-equivalent bindings.  The DLL is
distributed as-is with Warren Pratt's original copyright and
license preserved.

Source-level WDSP ports (where Lyra-cpp incorporates WDSP source
directly rather than calling the DLL — e.g. parts of the
captured-profile noise reduction, the eventual PureSignal calcc.c
and iqc.c port for v0.3) carry per-file attribution comments
naming the original WDSP source file and Warren Pratt's copyright.

---

## fldigi — CW (Morse) receive decoder

Lyra-cpp's RX CW decoder is a faithful, no-drift source port of
the CW *receive* chain from fldigi.

### Source

- **Project:** fldigi (Fast Light Digital Modem)
- **Ported source files:** `src/cw_rtty/cw.cxx` + `cw.h`,
  `src/cw_rtty/morse.cxx` + `morse.h`, `src/filters/fftfilt.cxx`,
  `src/include/filters.h`, `src/include/gfft.h`
- **Upstream:** https://sourceforge.net/projects/fldigi/
- **Project home:** http://www.w1hkj.com/

### Contributors credited

- **Dave Freese (W1HKJ)** — author and maintainer of fldigi,
  including the CW modem, the overlap-add FFT filter, and the
  running-mean helpers Lyra-cpp ports.
- **Lawrence Glaister (VE7IT)** — author of fldigi's adaptive
  CW speed-tracking algorithm, ported verbatim.

### Original copyright + license

- **Original copyright:** (C) various fldigi contributors (see
  per-file headers in upstream fldigi source).
- **License:** GNU General Public License v3 or later.

### Usage in Lyra-cpp

Lyra-cpp's `src/dsp/cw_fldigi/` (`fldigi_cw.{h,cpp}`, `gfft.h`)
is a C++23 port of fldigi's CW receive chain — mixer → `fftfilt`
overlap-add FFT low-pass → magnitude → decimate-by-16 →
`decode_stream` adaptive slicer → `handle_event` timing FSM →
`morse` lookup — running at fldigi's native 8000 Hz CW sample
rate.  The ported files carry provenance comments naming the
fldigi source files and the W1HKJ / VE7IT attribution.  The
adapter (`src/dsp/CwDecoder.{h,cpp}`) decimates Lyra's 48 kHz
demod audio to 8 kHz and is Lyra-native glue around the port.

---

## openHPSDR Thetis — TX baseline architecture

Lyra-cpp's TX baseline is a C++23 port of the openHPSDR Thetis
ChannelMaster orchestration layer.

### Source

- **Specific source repository:** MI0BOT OpenHPSDR-Thetis fork,
  version 2.10.3.13 (the HL2-focused fork — bundles the upstream
  ramdor mainline plus Reid's HL2-specific work).
- **HL2 fork (Lyra-cpp port source):** https://github.com/mi0bot/OpenHPSDR-Thetis
- **Upstream mainline:** https://github.com/ramdor/Thetis

### Contributors credited

- **Richie Samphire (ramdor)** — maintainer of the primary
  Thetis GitHub repository.  Responsible for modern ongoing
  development, C-runtime optimizations, and software releases
  for high-end transceivers (Apache Labs ANAN family).
- **Reid (MI0BOT / GI8TME)** — maintainer of the mi0bot
  OpenHPSDR-Thetis fork.  Heavily modified and ported the
  upstream codebase to provide native functionality, firmware
  emulations, and I2C controls for the Hermes Lite 2 / 2+
  open-hardware SDR ecosystem.  Lyra-cpp's TX baseline ports
  specifically from this fork.
- **Laurence Barker** — key developer for contemporary Thetis
  variants.  Author of the Thetis User Manual that ships with
  the application.
- **Dr. Warren Pratt (NR0V)** — author of the ChannelMaster
  core code (cmaster.c, aamix.c, ilv.c, supporting modules)
  alongside WDSP.
- **The TAPR / openHPSDR development team** — historical
  development group that maintained the transition from
  PowerSDR to openHPSDR-Thetis.  https://www.tapr.org/ /
  https://openhpsdr.org/
- **Various individual contributors** — see per-file headers
  in upstream Thetis source for additional contributors to
  specific modules.

### Original copyright + license

- **Original copyright:** (C) 2014-present various contributors
  (see per-file headers in upstream source for specifics).
- **License:** GNU General Public License v3 or later.

### Usage in Lyra-cpp

Lyra-cpp's TX baseline is a C++23 port of the Thetis
ChannelMaster orchestration layer (`cmaster.c`, `aamix.c`,
`ilv.c`) and supporting modules (vox, txgain, sidetone, pipe,
sync, analyzers, cmbuffs, cmsetup, bandwidth_monitor — ported
as each becomes needed).

Each ported file carries:

- The original copyright and GPL v3+ license header
- A "Ported from: openHPSDR Thetis (MI0BOT fork) [version] /
  [source file]" provenance comment
- A list of the C → C++23 mechanical idiom translations applied
  (Win32 CRITICAL_SECTION → std::mutex, C function pointers →
  std::function, malloc/calloc → std::vector/RAII, etc.)
- `[Lyra-native]` markers identifying any additions that are
  not part of the reference port

### Why we port instead of writing from scratch

The Thetis ChannelMaster architecture is tested across years of
community use on the entire HPSDR family (Hermes Lite 2/2+,
ANAN-10/100/200/Orion/Orion-MkII, ANAN-G2, etc.).  Porting brings
this maturity into Lyra-cpp directly, with full GPL attribution,
rather than re-implementing equivalents from spec.

Lyra-native TX DSP enhancements (Combinator multiband
compressor, Plate Reverb, parametric EQ, formant boost,
sibilance enhance, DX cut-through, de-esser, auto-AGC) layer on
top of the ported Thetis TXA chain as pre-WDSP-TXA stages.

---

## HPSDR Protocol 1 wire-layer reference

- **Reference file:** `ChannelMaster/networkproto1.c` (within
  the Thetis project above)
- **Usage:** Lyra-cpp's wire-layer C&C round-robin scheduler
  (`src/wire/FrameComposer.{h,cpp}`) is a Lyra-native
  implementation that matches reference byte-for-byte at the
  C&C frame byte-encoding level (per Rule 24 reference-quirk
  preservation).  Where Lyra has direct ports rather than
  Lyra-native implementations, per-file attribution headers
  identify the reference source line ranges.

---

## Mention but not ported

The following projects are referenced in Lyra-cpp documentation
but **are not source-incorporated**:

- **PowerSDR** (legacy Flex Radio SDR app) — historical
  reference only; no code in Lyra-cpp.
- **SparkSDR** (Mike Burnham W7DDG) — historical reference
  only; no code in Lyra-cpp.
- **pihpsdr** (John Melton G0ORX) — used as cross-reference
  for HL2 protocol verification (see `CLAUDE.md §15.26` for
  the cross-reference investigation trail).  No code in
  Lyra-cpp.  https://github.com/dl1ycf/pihpsdr
- **Quisk** — cross-referenced for HL2 C&C register decoding
  verification.  No code in Lyra-cpp.
- **linHPSDR** — cross-referenced for HL2 protocol fidelity
  verification.  No code in Lyra-cpp.
- **Hermes Lite 2 gateware** (Steve Haynal, KF7O + community)
  — the FPGA gateware itself.  Lyra-cpp reads the gateware
  RTL as ground truth for HL2+ AK4951 wire-byte semantics;
  no gateware code is ported into Lyra-cpp (Lyra-cpp is the
  host software, not the gateware).
  https://github.com/softerhardware/Hermes-Lite2

---

## Compliance with GPL v3+

The GPL grants the right to redistribute source-incorporated
code provided that:

1. **Original copyright and license notices are preserved.**
   Lyra-cpp preserves these in every ported file's header.
2. **Modifications are documented.** Each ported file lists
   the C → C++23 idiom translations applied, plus any
   `[Lyra-native]` additions, in the file header.
3. **The derivative work is distributed under GPL v3+.**
   Lyra-cpp is GPL v3+.
4. **Complete source code is provided to recipients.**
   Lyra-cpp's complete source is at
   https://github.com/N8SDR1/Lyra-SDR-cpp.

If you find any attribution issue in this file or a per-file
header, please open an issue at the Lyra-cpp GitHub repository.

---

## PortAudio (vendored, MIT license)

Lyra-cpp vendors the **PortAudio 19.7.0** source under
`third_party/portaudio/` and links it statically to drive the
host-audio device layer for the Virtual Audio Cable engine
(the full-duplex callback that bridges the PC sound devices to
the IVAC rings).  PortAudio is © 1999-2006 Ross Bencina and
Phil Burk and is distributed under the MIT license; its full
license text is preserved verbatim at
`third_party/portaudio/LICENSE.txt`.  PortAudio's MIT terms are
compatible with Lyra-cpp's GPL v3+.  Unlike the WDSP/reference
ports, PortAudio is unmodified upstream source — its copyright
notice is retained as the MIT license requires.

---

## DeepFist neural CW decoder (MIT license)

Lyra-cpp's optional neural CW (Morse) decode engine
(`src/dsp/deepfist/`) embeds **DeepFist**, a clean-room,
self-trained neural (spectrogram → CNN + CTC) Morse decoder.
DeepFist is © 2026 Brent Crier and is distributed under the MIT
license; its terms are compatible with Lyra-cpp's GPL v3+.  The
trained model artifact (`deepfist.onnx` + JSON sidecar) is
DeepFist's own, trained on its own synthetically generated data —
no third-party model weights are used or redistributed.  The C++
front-end (decimator, spectrogram, greedy CTC) is an original
re-implementation of DeepFist's own MIT Python; no AGPL-licensed
code (e.g. DeepCW / HamNoise, used only as conceptual references
during DeepFist's development) is present in Lyra-cpp.

## ONNX Runtime (vendored, MIT license)

The DeepFist engine runs its model through **ONNX Runtime**
(prebuilt CPU build, version 1.20.1), vendored under
`third_party/onnxruntime/`.  ONNX Runtime is © Microsoft
Corporation and is distributed under the MIT license, preserved
at `third_party/onnxruntime/LICENSE` (third-party notices at
`third_party/onnxruntime/ThirdPartyNotices.txt`).  Its MIT terms
are compatible with Lyra-cpp's GPL v3+.

---

## Project author

**Lyra-cpp** is N8SDR (Rick Langford)'s project, building on
top of the upstream open-source work credited above.  See
[CREDITS.md](CREDITS.md) for the full contributor list
including the contributors who shaped the project through
field reports and bench testing.

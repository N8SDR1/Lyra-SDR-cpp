# Roadmap

Where Lyra is headed. This is a **direction, not a dated schedule** — items land
when they're solid. Everything shipped today is on the [Feature Status](Feature-Status)
page.

**Now (v0.25.29 Sheliak):** HL2 / HL2+
P1, BrickSDR2 / Brick3SDR P2, SUB + SPLIT, PureSignal, TX rack. Brick3 /
ANAN-100D get a second SUB panadapter. **Diversity** (two ADCs, same
frequency) is in and confirmed on Brick3SDR. Protocol 1 **RF1 / Alt RX**
on the Xvtr row. Protocol 2 DUC-IQ on its own TX pump.
**Lyra to Lyra (v0.25.25):** desk RX2 picture and audio, CW from the
desk, on top of v0.25.24 Sheliak. Steps are in the user guide,
[Lyra to Lyra](User-Guide#lyra-to-lyra). **v0.25.24** is the previous
public build. **v0.25.22** remains the return point.
**Next:** graduate Orion MkII testers (ANAN-7000DLE / 8000DLE / Orion MkII /
Anvelina-Pro3) off dummy-load TX, G2 on-air validation, then Linux.
**macOS stays a separate branch** (`macos-port`);
it is not merged into the Windows tree. Brent tracks trunk into that
branch. P2 on Mac is untested.

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

### ✅ Diversity (two ADCs, same frequency)

Phase/gain combine of the two converters on **one** frequency — not a
second VFO (that is **SUB**). Confirmed on **Brick3SDR**. The same
two-converter path is **ANAN-100D / 200D**. HL2 and BrickSDR2 have one
ADC and do not show the chip.

The **DIV** chip opens the dock (size and position are remembered).
**ON** inside the dock arms the mix; opening the dock does not. Turning
Diversity on turns **SUB** off. Second RF is the **RX2 jack** (Ext1),
not ANT2. Gain **−27…+27 dB**, phase **−180…+180°**, on the polar dot
or the steppers. Each band keeps its own pair; a band you have not set
starts at **0 dB / 0°**. The master on/off is global. While you
transmit, or while PureSignal is live, the mix drops and comes back
when you unkey. **RX2 jack isolate on TX** still grounds that jack for
the key.

### ✅ PureSignal — adaptive predistortion

Live on **HL2 / HL2+** (coupler hardware mod), **BrickSDR2** (Protocol-2
ADC0 pad), and **Brick3SDR** (rear **PS** jack). Compact **PureSignal** dock plus **Amp View** plot
(Snap / Hold). Feedback glance chip (teal below 129, green 129–181, red
above). Auto-att **persists across PTT** so SSB does not re-hunt every
keyup. Attestation in **Settings → TX** (default off). **2-tone** on the
TX panel is the usual tune-up carrier.
Operator detail: **[User Guide → PureSignal](User-Guide#puresignal)**.

### ✅ Protocol 2 on BrickSDR2 / Brick3SDR · 🚧 Classic ANAN P2 on-air (G2 dummy-load · Orion MkII in tester hands)

HPSDR **Protocol 2** is live on **BrickSDR2** and **Brick3SDR** (RX + TX,
including radio mic → modulator, analog drive, watts-cap, ATT-on-TX).
Brick3 is **beta testing** (v0.25.20) — TX like BrickSDR2 (no dummy-load
arm); first RF dummy load. **SUB** opens a second panadapter. A Brick that
still answers **Protocol 1** is **discovered but not opened** — P1 TX in
Lyra is HL2-only. Flash **P2** (Anton / linoobs for Brick images).
**ANAN-10 / 10E** = BrickSDR2 TX; **ANAN-100 / 100B / 100D / 200D** =
Brick3SDR TX. **G2 / G2-1K** still dummy-load arm. **7000DLE / 8000DLE / Orion MkII /
Anvelina-Pro3** are **in tester hands** (not locked) — RX and BPF follow
reported on Anvelina PROIII; TX stays dummy-load + drive ceiling until RF
is confirmed. Boxes that shipped as Protocol 1 should run the **P2 FPGA**
when they can.

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
- ✅ **Diversity** — two-ADC phase/gain combine (v0.25.20; Brick3SDR confirmed; 100D / 200D same path). Not SUB. See above.
- ✅ **HL2 filter-board follow** — voltage follows the band you selected, including a return to a band already on the dial (v0.25.20, Issue #17).
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
- ✅ **Band stack** — each band chip stores three frequencies (corner digit **1 / 2 / 3**). A click from another band returns the slot you last used; a second click on the band you are already on steps to the next. SUB has its own three (v0.25.20).
- ✅ **Separate RX and TX filter lows** — Filters dock, under the bandwidths (**RX lo** / **TX lo**, 0–500 Hz). The lock keeps them together; unlock and they stay apart. Older profiles copy the old shared low onto both sides (v0.25.20).
- 🗺️ Continued polish across the DSP, UI, and metering as testers report back
- ✅ **Companion Apps** — third-party digital / SSTV / logger programs launched from Lyra (native RTTY modem will not return; CW stays in-radio)

## Exploring — further out 💡

Ideas we're interested in but haven't scheduled. They sit behind the major
features above and may change shape or timing.

### 🚧 Lyra to Lyra (beta, v0.25.23+)

**0.25.23+** is a beta of Lyra-to-Lyra. A second Lyra on the laptop is
the desk. The shack Lyra stays on the radio and does not hand the rig
to the laptop. Install the same version on both PCs. Start here:
[User Guide → Lyra to Lyra](User-Guide#lyra-to-lyra).

**v0.25.22** remains the return point. Reinstall that GitHub installer
if this beta misbehaves. Settings stay. That build has no Lyra-to-Lyra.

The operate shape (host owns the radio, Tailscale, three sockets, the
25 Mbps / 100 ms floor) is
[docs/architecture/lyra_to_lyra.md](../architecture/lyra_to_lyra.md).

**Drop:** Windows service / headless-only package; in-app gateware flash;
naked WAN / port-forward of HPSDR UDP to the radio (Reid-style). LTE IQ
is huge even on Wireguard (tester note). LAN **Lyra Remote** (TCI browser)
stays what we ship for remote-on-the-house-net.

### TX rack setup without RF — dropped

On-air TX always sounds different from a local tap. **MON TX** into a dummy
load is enough to hear whether the rack grabbed; a Kiwi or a trusted ear
is how you set it. No no-RF listen path.

## Want to influence it?

Feature requests and bug reports genuinely steer the order of work —
**[open an issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)** or join the
community on Discord. Tester hardware for non-HL2 radios is especially welcome.

---

**See also:** [Feature Status](Feature-Status) · [Supported Radios](Supported-Radios) · [Home](Home)

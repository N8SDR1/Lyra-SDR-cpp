# Feature Status

Where Lyra stands today. Everything marked ✅ is shipped and working in
**v0.25.21 Sheliak** on the **Hermes Lite 2 / 2+** (Protocol 1) and
**BrickSDR2 / Brick3SDR** (Protocol 2), unless a row says otherwise.

> **Legend:** ✅ working now · 🚧 in progress · 🗺️ planned (see [Roadmap](Roadmap))

### Last several builds (0.25.16 → 0.25.21)

| Build | What landed |
|---|---|
| **0.25.21** | **[Lyra Remote](Lyra-Remote)** voice session (separate browser desk) · Brick3 holds the first key until PA enable is already in receive · TCI meter line carries ALC headroom |
| **0.25.20** | **Diversity** (Brick3 / 100D / 200D) · **band stack** (three frequencies per chip) · **separate RX and TX filter lows** · filter-board voltage follows a return to a band already on the dial (Issue #17) |
| **0.25.19** | IQ backlog cap · PC-ring underrun fade · NB/AF atomics |
| **0.25.18** | HL2 analog J4 / Pico **BYTE0** latch (no leftover-band voltage) · **RX2 jack isolate on TX** (two-converter) · **11 m / CB chips on the SUB pane** (mode follows Hardware Region) |
| **0.25.17** | Dual-ADC **SUB second panadapter** (Brick3 / 100D / 200D) · Brick3 TX like Brick2 (no dummy-load arm) · ANAN-10 = Brick2 TX, 100-series = Brick3 TX · PureSignal **Voice** + engine chips on Amp View · Windows mixer volume remembered (Issue #18) |
| **0.25.16** | **Brick3 Beta Testing** · **MAP** ANT / RX chips · Protocol 2 **Auto Step** 0–31 · per-band PureSignal pad · PS Reset |
| **0.25.15** | Protocol 2 **TX DUC follows the VFO** after retune · keyboard VFO · startup profile (mode binding → last-used) · Tuning Mem left-click after idle |

## Radio &amp; connection

- ✅ HPSDR **Protocol 1** discovery (multi-NIC, subnet-directed broadcast) + **Add by IP** for fixed-IP / cross-subnet radios
- ✅ HPSDR **Protocol 2** discovery + live RX/TX on **BrickSDR2** and **Brick3SDR** (DUC I/Q, analog drive, ATT-on-TX, radio mic → modulator); discovery firmware shown as **v10.6**-style, not “fw v0”. **TX DUC follows the VFO after retune** (v0.25.15). **Brick3 Beta Testing** (v0.25.16–0.25.20): MAP ANT/RX chips, Auto Step 0–31, per-band PS pad, TX like BrickSDR2 (no dummy-load arm), **SUB second pan**, **RX2 jack isolate on TX**. **P1 Brick / P1 ANAN: Discover lists, Open refuses** (HL2 TX layout) — flash P2; Brick FPGA help: Anton (linoobs)
- ✅ **Keyboard VFO** (global arrows: 1 kHz / 10 Hz), **startup profile** (mode binding → last-used → Default), **Tuning Mem** left-click after idle (v0.25.15)
- 🚧 **ANAN-10 / 10E** Protocol 2 — classic Alex; **BrickSDR2 TX** (on-air, no dummy-load arm). Pick marketed model in Settings (Hermes discovery still defaults to Brick). Boxes that shipped P1 should use a P2 FPGA when they can.
- 🚧 **ANAN-100 / 100B / 100D / 200D** Protocol 2 — **Brick3SDR TX** (on-air, independent Enable PA). **100D / 200D**: two converters, **SUB** opens a second panadapter.
- ✅ **SUB / RX2 on HL2, BrickSDR2, Brick3SDR, and P2 ANAN** — second DDC; orange **TUNE A** / cyan RX1 vs lime **TUNE B** / green RX2 overlay; band chips **red** (RX1) vs **green** (SUB); **Shift+click / right-click** hops SUB; off-span **◀ RX2** / **RX2 ▶**. One ADC (HL2 / Brick2 / 10 / 100): overlay + N2ADR follows RX1. Two ADC (Brick3 / 100D / 200D): **SUB pane** with own waterfall, band-plan, click-tune, **Visuals SUB floor/ceiling** (per RX2 band), optional **SUB layout** recall. **SUB and SPLIT are independent**. SUB + SPLIT click-tune on the SUB pane moves **VFO B** only. Diversity is a different mode (below) and turns SUB off.
- ✅ **Diversity** — two converters, **same frequency**, phase/gain combine. Not SUB. **DIV** chip opens the dock; **ON** arms the mix. Second RF is the **RX2 jack** (Ext1), not ANT2. Gain **−27…+27 dB**, phase **−180…+180°**. Per-band pair (unset band starts at 0 dB / 0°); master on/off is global. Drops while transmitting or while PureSignal is live, and returns on unkey. Confirmed on **Brick3SDR**; same path on **ANAN-100D / 200D**. HL2 and BrickSDR2 do not show the chip (one ADC).
- ✅ **Band stack** — each Ham / BC / 11m chip stores **three** frequencies. Corner digit **1 / 2 / 3**. A click from another band returns the last-used slot and does not skip. Click the band you are already on to step. SUB has its own three. Shift+click and right-click still hop SUB. Solar chips tune VFO A to the current slot and do not step it.
- ✅ **Protocol 2 Auto Step** — DSP+Audio **Step** walks ADC pad 0–31 (clip +3 / unwind −1), frozen while keyed. Default off. Brick3 **MAP** ANT / RX jack chips.
- ✅ **SPLIT** pile-up — TX on VFO B independent of SUB; gray TX pips; right-click SPLIT 1/5/10 kHz shift; lime TX marker (red on key); **◀ TX** / **TX ▶** when TX is off-span
- ✅ Multi-radio list, auto-connect to the last radio, installer firewall rules (connect without admin rights)
- ✅ **Stale-IP** guard (won't freeze trying to reach a radio that moved)
- ✅ Graphics **crash ladder** (OpenGL → software, MSAA off on software) so a bad GPU driver does not leave Lyra with no window
- ✅ **MSAA** picker **Off / 2× / 4× / 8×** (Settings → Visuals; default 4×; restart)
- ✅ **Xvtr** band chips — RF dial (ten-digit LED, ~2.147 GHz), IF NCO, Disable PA default on; Shift+click SUB hop; right-click slot editor. Band-plan **region does not rewrite** slot MHz — set RF low/high/LO for your allocation. See User Guide **Band panel**.
- ✅ HL2 **N2ADR / IO board** — OC LPF (I2C **0x20**) is the **Enable N2ADR / IO board** checkbox. Filter-board follow is always Protocol 1 TX frequency on I2C **0x1D**, not gated on that box. **v0.25.20 / #17:** a return to a band already on the dial is written again. Leave **Band Volts on J3** off unless the amp is on the fan header.

## Receive (RX)

- ✅ Full WDSP receive chain — **USB / LSB / CW / AM / SAM / DSB / FM / DIGU / DIGL / SPEC**
- ✅ Per-mode filters, AGC (Fast/Med/Slow/Long/Auto), **NR** (EMNR 1–4) **or**
  **NNR** (WDSP 2.10 neural; Standard default, Premium optional, mask −10…−50 dB, default −40), exclusive
  vs each other; noise blanker, **auto-notch (ANF)**, **LMS**, all-mode **squelch**
- ✅ **8-band RX parametric EQ** (draggable curve)
- ✅ **Captured-noise profile (NR-C)** — grab your band noise and subtract it
  (IQ-domain; panadapter stays on uncleaned IQ so a strong station stays
  rectangular; Floor default **−24 dB**; can run with NR or NNR; last
  profile restores on restart)
- ✅ **Centre-tune (CTUN)** — drag the marker onto a signal while the LO stays put
- ✅ **RIT** and **XIT** (receiver / transmitter incremental tuning)
- ✅ **Zero-beat markers** — Kenwood-style ± needle to dead-tune a CW / AM / SAM / FM carrier by eye
- ✅ Audio out the **HL2 codec jack (AK4951)** or a **PC sound device**

## Transmit (TX)

- ✅ **SSB** (USB / LSB), **AM** (proper carrier + both sidebands), **SAM**, **DSB** (suppressed carrier), **FM** (deviation / pre-emphasis / CTCSS / Region-1 **1750 Hz** TX burst while keyed)
- ✅ **CW** — internal iambic keyer (paddle / straight key), keyboard send, **CWX**, contest **macro bank** ({CALL}/{RST}/{NAME}…), QSK / semi / manual break-in, adjustable sidetone
- ✅ **Digital** via **TCI** and **VAC1 / VAC2** — WSJT-X / MSHV / JTDX / FLDigi / VarAC, or a logger on a second cable. VAC **Enable** is RX; **Mic source = VAC1 or VAC2** is TX. VAC2 carries **RX2** (turn **SUB** on). TCI audio and VAC TX are mutually exclusive.
- ✅ TX power / drive, separate **tune drive**, AM carrier level, mic gain + **20 dB mic boost**; Protocol 2 uses analog drive (HP [345]) plus per-band **PA Gain / Full Output / CAP**, including a dedicated **11m** slot
- ✅ **Separate RX and TX filter lows** (current build) — **RX lo** under **RX BW**, **TX lo** under **TX BW** on the Filters dock (also Settings → Audio). **0–500 Hz**. The lock keeps them equal; unlock and a profile can hold different lows. Older profiles copy the old shared low onto both sides.
- ✅ Always-on **ALC** + operator **Leveler**, **PHROT** phase rotator
- ✅ **PureSignal** — adaptive TX predistortion (HL2 / HL2+ coupler mod; BrickSDR2 ADC0 pad; Brick3 rear **PS** jack). Compact dock: ON / 2-tone / **Voice** / ATT / FB / Reset. Engine chips (st / cal / D0 / D1) on **Amp View**. Auto-att persisted **per TX band** (v0.25.16). Attestation in Settings → TX (default off).
- ✅ **Waterfall callsign ID** — paints your call in the SSB passband (ham bands only)

### Native TX audio rack (studio-in-the-radio)

- ✅ **8-band parametric EQ** with draggable curve + live RTA
- ✅ **5-band Combinator** (multiband compressor — Lyra’s CFC equivalent)
- ✅ **Plate reverb** for ESSB "air"
- ✅ **Speech processing** — formant boost, sibilance/consonant emphasis, DX cut-through, de-esser, auto-AGC
- ✅ **Voice keyer** (message memory) + **VOX** (with anti-VOX)
- ✅ **TX profiles** — save/recall the whole chain (including VAC1 **and VAC2** knobs; audio device names stay global); a profile can even launch its companion app (VarAC / MSHV / WSJT-X). Startup: mode-family binding → last-used → Default (v0.25.15). How to build one: in-app **Help → How to create a profile**.
- ✅ **Hot-mic monitor** / SSB sidetone, separate monitor output device

### Metering &amp; TX safety

- ✅ Multimeter — **PO / SWR / MIC / COMP / ALC / PA current**. The **PWR** needle keeps the loudest coupler sample between ticks so voice peaks land on the scale (factory peak-hold 700 ms if you have not saved your own)
- ✅ **ATT-on-TX** RX-front-end protection · **RX2 jack isolate on TX** (two-converter radios, default on) · **TR-sequencing** for amp hot-switch safety
- ✅ **SWR protection** (auto-cut above threshold) · **max power / drive cap** for low-drive amps
- ✅ **TX time-out** · hard **External TX Inhibit**

## Panadapter, waterfall &amp; UI

- ✅ **Vulkan / RHI** scene-graph spectrum — glassy fill/glow, peak-hold, noise-floor line, palettes
- ✅ Click / drag / wheel tuning; draggable passband edges; **click-to-tune on the waterfall**; collapsible waterfall
- ✅ **Dockable UI** — snap to edge / split / tab / float, four named layout slots + factory default, lock panels; optional **SUB layout** recall (Visuals) that restores the pre-SUB arrangement when SUB goes off
- ✅ **Windows per-app mixer** volume remembered across Stop / Start
  (v0.25.17; Issue #18 — Stop no longer slams the mixer to 100 % / unmute)
- ✅ **11 m / CB channel chips on the SUB pane** — click-tune mode follows Hardware **Region** (v0.25.18)
- ✅ **Crash-safe graphics** — auto-steps down to a safer backend if a launch ever fails; an **operator-pinned** Vulkan/D3D/OpenGL choice in Settings is not overridden by that ladder (v0.24.5)
- ✅ Band switching + **per-band memory**; GEN / time-station / 20-slot memory bank; **EiBi** shortwave overlay

## Tools &amp; extras

- ✅ **RX CW decoder** — prints what it hears on-screen
- ✅ **DX spots** — cluster / RBN / TCI sources, filters, panadapter overlay, click-to-tune
- ✅ **Tuner memory** — manual-ATU settings per band and per antenna
- ✅ **Frequency calibration** against WWV / time stations
- ✅ **Session recorder** — RX audio on Protocol 1 and Protocol 2 + timed
  panadapter snapshots (Vulkan-safe) → a synced **MP4**; default folder
  `%USERPROFILE%\Lyra\Recordings` (v0.25.12, never Program Files)
- ✅ **CAT / Serial** — Kenwood TS-480/2000 CAT over COM/TCP, serial PTT input, Winkeyer
- ✅ **TCI server** — two channels; `vfo:0,1` = SPLIT VFO B, `vfo:1,0` / `dds:1` = SUB; RX1 + RX2 S-meter sensors; **CW terminal** (key line stays up between letters; callsign correct mid-send) and **TX sensors** that keep reporting at receive; handshake sends `start` only when the radio session is running (otherwise `stop` then `ready`); **STOP** never closes the radio and never drops MOX (echoes `start` while live so WSJT-X / JTDX stay attached and can key); **TUNE** keys the host TUN path, **TRX** keys MOX, **CHRONO** TX-audio headers use the negotiated rate / samples / format / channels (v0.25.14; MSHV / WSJT-X / JTDX / SDRLogger+); **[SDRLogger+ Combo](SDRLogger-Plus-Combo)** (call/name/RST from **RX1** + one-click log); **[Lyra Remote](Lyra-Remote)** voice session (password proof, then that browser's microphone) and a fifth TX-sensor number, ALC headroom (v0.25.21)
- ✅ **USB-BCD** band data for linear-amp band switching
- ✅ **Backup &amp; Restore** — export config, dated snapshots (survive reinstall), selective restore
- ✅ Solar / propagation panel (band chips tune VFO A to that band's current stored frequency; they do not step the 1 / 2 / 3 stack), weather alerts
- ✅ **Apps** — named shortcuts for third-party programs; **Tested with and recommended** links (Open SSTV, MSHV / WSJT-X, VarAC, midi2tci); auto-start after Hardware Startup (default off)
- ✅ **USB encoder (HID wheel)** — Settings → Hardware → Navigation (Windows); tunes the focused VFO by the Tuning **Step**
- ✅ **MIDI via TCI sidecar** — not native MIDI; Browse an operator-installed MIDI→TCI program under Settings → Apps (default TCI port **40001**)

## Not yet — see the Roadmap 🗺️

- 🚧 **ANAN-G2 / G2-1K** — Saturn BPF; TX still dummy-load arm until on-air validated. **ANAN-10…200D** already use Brick2 / Brick3 TX (see above).
- 🗺️ **ANAN-7000DLE / 8000** — OrionMkII BPF / PA not in this pass
- 🗺️ **Linux, then macOS**
- 💡 **TX rack setup without RF** — looped speech through the rack + hear TX bandwidth (3/4/6 kHz); not scheduled ([Roadmap](Roadmap#tx-rack-setup-without-rf-looped-speech--bandwidth))

---

**See also:** [Roadmap](Roadmap) · [User Guide](User-Guide) · [Supported Radios](Supported-Radios)

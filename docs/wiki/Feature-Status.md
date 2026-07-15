# Feature Status

Where Lyra stands today. Everything marked ✅ is shipped and working in the
current release on the **Hermes Lite 2 / 2+**.

> **Legend:** ✅ working now · 🚧 in progress · 🗺️ planned (see [Roadmap](Roadmap))

## Radio &amp; connection

- ✅ HPSDR **Protocol 1** discovery (multi-NIC, subnet-directed broadcast) + **Add by IP** for fixed-IP / cross-subnet radios
- ✅ Multi-radio list, auto-connect to the last radio, installer firewall rules (connect without admin rights)
- ✅ **Stale-IP** guard (won't freeze trying to reach a radio that moved)

## Receive (RX)

- ✅ Full WDSP receive chain — **USB / LSB / CW / AM / SAM / DSB / FM / DIGU / DIGL / SPEC**
- ✅ Per-mode filters, AGC (Fast/Med/Slow/Long/Auto), **noise reduction**, noise blanker, **auto-notch (ANF)**, manual notches, all-mode **squelch**
- ✅ **8-band RX parametric EQ** (draggable curve)
- ✅ **Captured-noise profile** — grab your band noise and subtract it
- ✅ **Centre-tune (CTUN)** — drag the marker onto a signal while the LO stays put
- ✅ **RIT** (receiver incremental tuning)
- ✅ **Zero-beat markers** — Kenwood-style ± needle to dead-tune a CW / AM / SAM / FM carrier by eye
- ✅ Audio out the **HL2 codec jack (AK4951)** or a **PC sound device**

## Transmit (TX)

- ✅ **SSB** (USB / LSB), **AM** (proper carrier + both sidebands), **SAM**, **DSB** (suppressed carrier), **FM** (deviation / pre-emphasis / CTCSS)
- ✅ **CW** — internal iambic keyer (paddle / straight key), keyboard send, **CWX**, contest **macro bank** ({CALL}/{RST}/{NAME}…), QSK / semi / manual break-in, adjustable sidetone
- ✅ **Digital** via **TCI** and **virtual audio cable (VAC1)** — WSJT-X / MSHV / JTDX / FLDigi / VarAC, or drive Lyra from your logger
- ✅ TX power / drive, separate **tune drive**, AM carrier level, mic gain + **20 dB mic boost**
- ✅ Always-on **ALC** + operator **Leveler**, **PHROT** phase rotator
- ✅ **Waterfall callsign ID** — paints your call in the SSB passband (ham bands only)

### Native TX audio rack (studio-in-the-radio)

- ✅ **8-band parametric EQ** with draggable curve + live RTA
- ✅ **5-band Combinator** (multiband compressor, X-Air-style)
- ✅ **Plate reverb** for ESSB "air"
- ✅ **Speech processing** — formant boost, sibilance/consonant emphasis, DX cut-through, de-esser, auto-AGC
- ✅ **Voice keyer** (message memory) + **VOX** (with anti-VOX)
- ✅ **TX profiles** — save/recall the whole chain; a profile can even launch its companion app (VarAC / MSHV / WSJT-X)
- ✅ **Hot-mic monitor** / SSB sidetone, separate monitor output device

### Metering &amp; TX safety

- ✅ Multimeter — **PO / SWR / MIC / COMP / ALC / PA current**
- ✅ **ATT-on-TX** RX-front-end protection · **TR-sequencing** for amp hot-switch safety
- ✅ **SWR protection** (auto-cut above threshold) · **max power / drive cap** for low-drive amps
- ✅ **TX time-out** · hard **External TX Inhibit**

## Panadapter, waterfall &amp; UI

- ✅ **Vulkan / RHI** scene-graph spectrum — glassy fill/glow, peak-hold, noise-floor line, palettes
- ✅ Click / drag / wheel tuning; draggable passband edges; **click-to-tune on the waterfall**; collapsible waterfall
- ✅ **Dockable UI** — snap to edge / split / tab / float, four named layout slots + factory default, lock panels
- ✅ **Crash-safe graphics** — auto-steps down to a safer backend if a launch ever fails
- ✅ Band switching + **per-band memory**; GEN / time-station / 20-slot memory bank; **EiBi** shortwave overlay

## Tools &amp; extras

- ✅ **RX CW decoder** — prints what it hears on-screen
- ✅ **DX spots** — cluster / RBN / TCI sources, filters, panadapter overlay, click-to-tune
- ✅ **Tuner memory** — manual-ATU settings per band and per antenna
- ✅ **Frequency calibration** against WWV / time stations
- ✅ **Session recorder** — RX audio + timed panadapter snapshots → a synced **MP4**
- ✅ **CAT / Serial** — Kenwood TS-480/2000 CAT over COM/TCP, serial PTT input, Winkeyer
- ✅ **TCI server** — logger / cluster integration, incl. the **[SDRLogger+ Combo](SDRLogger-Plus-Combo)** link (call/name/RST sharing + one-click log)
- ✅ **USB-BCD** band data for linear-amp band switching
- ✅ **Backup &amp; Restore** — export config, dated snapshots (survive reinstall), selective restore
- ✅ Solar / propagation panel, weather alerts, auto-launch companion apps at startup

## Not yet — see the Roadmap 🗺️

- 🗺️ **RX2** dual receiver (stereo split, focus model, SPLIT)
- 🗺️ **PureSignal** adaptive predistortion
- 🗺️ **HPSDR Protocol 2 + ANAN** hardware
- 🗺️ **VAC2** (second virtual-audio channel) · 2-tone test generator
- 🗺️ **Linux, then macOS**

---

**See also:** [Roadmap](Roadmap) · [User Guide](User-Guide) · [Supported Radios](Supported-Radios)

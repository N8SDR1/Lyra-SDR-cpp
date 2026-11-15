# Lyra — Known issues & what's not built yet

Lyra is in **active development**. Daily-driver transceiver for Hermes Lite 2 / 2+
(Protocol 1) and Protocol 2 **BrickSDR2 / Brick3SDR** plus **ANAN** (G2 / G2-1K
still dummy-load arm). **Dual panadapters**, **Diversity**, and **PureSignal**
are shipped — they are not roadmap items. This page is leftover bugs and
honest gaps, not a feature catalog.

Current tree: **v0.25.42 Aladfar**. Bundled DSP is **WDSP 2.11** (CW NNR models in CW modes). **ASIO for VAC is in test** (opt-in, default off; Qt Out / HL2 jack unchanged; not CMASIO). Live checklist:
**[Feature Status](https://github.com/N8SDR1/Lyra-SDR-cpp/wiki/Feature-Status)** ·
**[Releases](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest)**
(installer **Lyra-Setup-0.25.42.exe**). First start after the update rebuilds the FFT cache. **ASIO for VAC** already shipped in **v0.25.41**. **ANAN / Brick3 filters on Filters / BCD** already shipped in v0.25.40. **HiDPI display**, **ANAN blank model → filter profile**, and **Log window stays on screen** already shipped in v0.25.39. **DeepFist** pitch lock is already in v0.25.38. **Classic** is unchanged. **ANAN-7000DLE / 8000DLE / Orion MkII / Anvelina-Pro3** transmit without **Arm Protocol 2 TX** (Enable PA + MOX + Drive). HL2 **Xvtr hops** send on-air RF on BYTE0–4; **Stop** sends Metis STOP before the socket closes. **G2 / G2-1K** still need the dummy-load arm. Protocol 1 ANAN is still refused. v0.25.41 Sulafat is the previous public build. The v0.25.34 P2 drive warning still applies if you skip from ≤0.25.31: **turn Drive down and walk it up before your first TX on a P2 radio**. v0.25.22 remains the long return point.

> **What already works** (so you don't wonder): full RX DSP, and **transmit
> on every mode** — SSB / AM / DSB / SAM / FM, **CW** (internal iambic keyer,
> paddle/straight-key on the HL2 KEY jack, keyboard send, a macro bank,
> external keyer / Winkeyer, and a serial CW-key input), and digital via
> TCI / virtual audio cable / the **Apps** launcher (no in-radio RTTY
> decoder — **CW send and decode stay native**). Plus **VOX**, a native TX audio rack (EQ /
> speech / combinator / plate), TX profiles, per-band power calibration +
> an auto-tuning amp watts-cap, waterfall callsign ID, a manual-ATU tuner
> memory, a **voice keyer** (record phone messages in-app, play them on the
> air from F-keys) with an **RX/TX recorder**, **frequency calibration**
> against WWV / CHU, a **Backup & Restore** tab (whole-config export +
> selective restore + automatic dated snapshots that survive a reinstall), a
> layout **undo**, TCI server for logging / cluster software, and
> **PureSignal** (HL2 coupler mod / Brick ADC0).

---

## Not implemented yet (please don't file these as bugs)

These are on the roadmap, not broken:

* **macOS / Linux** — **Windows only.** The DSP engine and wire layer are
  Windows binaries today; Linux/macOS are planned but **not yet buildable**.
  The native Windows installer is the only supported way to run Lyra.

## Expected behavior (not bugs)

* **PureSignal needs a coupler and RF.** HL2 / HL2+ without the hardware
  mod, or dummy-load drive of only 2–3 W, often shows **FB ≈ 0**. Raise
  drive (typically ~5–8 W on a bare HL2) and attest in Settings → TX.
  SUB is paused while MOX+PS is live (preference is not wiped).
  **Brick2 / Brick3 PureSignal never finishing (FB 0 / cal 0 / pad
  walked to 0 dB) was a v0.25.16–0.25.31 bug** — Protocol-2 TX IQ was
  scaled with Drive, so the TX-DAC replica never reached SetPk below
  full drive. Fixed in **v0.25.33** (P2 TX IQ at unity; see
  `docs/releases/v0.25.33.md`). **After that upgrade the same Drive
  position makes more RF on P2** (about +8 dB at 40 %, +10 dB at 30 %,
  TUN included) — turn Drive down and walk it up before keying into an
  amp, and re-run TUN per band.
* **Cross-band SUB is much weaker** (HL2, BrickSDR2, ANAN-10 / 100). One
  ADC and one analog filter. With an N2ADR / filter board on, OC follows
  **RX1** — RX2 on another amateur band sits behind that LPF/BPF.
  Same-band dual watch is full strength. The status log notes the
  drop once when the two ham bands differ. **Brick3SDR / ANAN-100D /
  200D** have two converters — SUB opens a **second panadapter**; that
  path is not LPF-starved the same way. **Diversity** (DIV chip) is
  shipped on those two-converter boxes; it is not a second VFO.
* **Windows mixer vs Lyra Volume.** The system mixer slider for Lyra is
  remembered across Stop / Start. **Vol** / **AF** in the app are
  separate. If one is quiet and the other is loud, check both.
* **First launch is slow / unresponsive for a minute.** Lyra does a one-time
  FFT optimization tuned to your CPU and caches it; later launches are fast.
  (If you change processors, **Settings → Backup & Restore → Rebuild FFT
  optimization on next start** re-tunes it to the new hardware.)
* **The amp watts-cap lands a little *under* the number you set.** The
  Hermes Lite's drive control is coarse (about 16 hardware steps), so your
  cap usually falls *between* two steps. Lyra parks on the highest step that
  stays **under** your cap — the safe side for your amp — so a band may sit
  noticeably below the number (e.g. ~3.0 W under a 3.5 W cap). This is
  intentional protection, not an error. If you change the cap, re-key **TUN**
  on each band so it re-learns.
* **Panadapter looks "wide / smeared" while transmitting.** During TX the RX
  front-end is deliberately attenuated to protect it, and the panadapter is
  fed from that protected receiver — so the on-screen trace is **not** a
  measure of your transmitted signal quality. Judge the actual signal with an
  external monitor (a second SDR, or a watt / SWR meter).
* **11m / CB channel markers are missing.** They only appear when the
  **11m / CB band is enabled** in Settings *and* you're tuned to the 27 MHz
  range. With it on, the channel numbers and the nicknames (Super Bowl,
  Emergency, Highway…) draw on the panadapter.

## A note on safety

Lyra's protection features (amp watts-cap, SWR fold, TX timeout,
TR-sequencing, ATT-on-TX) are **aids, not guarantees** — they depend on
correct calibration, working hardware, and accurate readings. Always set up
into a **dummy load** with your amplifier out of line, verify with your own
watt / SWR meter, and operate within your licence privileges. See the
disclaimer in **Help → About Lyra** and the User Guide.

---

## Reporting a bug

Open a [GitHub issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues) or post
in [Discord](https://discord.gg/BwjsQvjcSc). To make it actionable, include:

1. **Lyra version** (Help → About, or the installer filename).
2. **Windows version**, and the radio (**HL2 / HL2+**, BrickSDR2 / Brick3SDR, or ANAN model).
3. **What you did and what happened** — steps to reproduce.
4. The in-app **diagnostic log** (open the log viewer in Lyra and copy the
   relevant lines).

Thanks for testing Lyra! — N8SDR

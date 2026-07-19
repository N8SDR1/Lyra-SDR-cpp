# Lyra — Known issues & what's not built yet

Lyra is in **active development**. It's a capable daily-driver transceiver
for the Hermes Lite 2 / 2+ — full receive *and* transmit — but it isn't
finished. This page sets expectations so you know what's a real bug versus
a feature that simply hasn't landed yet. (Pin or link this in Discord.)

Current release: **v0.21.1**. Always grab the latest from the
[Releases page](https://github.com/N8SDR1/Lyra-SDR-cpp/releases).

> **What already works** (so you don't wonder): full RX DSP, and **transmit
> on every mode** — SSB / AM / DSB / SAM / FM, **CW** (internal iambic keyer,
> paddle/straight-key on the HL2 KEY jack, keyboard send, a macro bank,
> external keyer / Winkeyer, and a serial CW-key input), and digital via
> TCI / virtual audio cable. Plus **VOX**, a native TX audio rack (EQ /
> speech / combinator / plate), TX profiles, per-band power calibration +
> an auto-tuning amp watts-cap, waterfall callsign ID, a manual-ATU tuner
> memory, a **voice keyer** (record phone messages in-app, play them on the
> air from F-keys) with an **RX/TX recorder**, **frequency calibration**
> against WWV / CHU, a **Backup & Restore** tab (whole-config export +
> selective restore + automatic dated snapshots that survive a reinstall), a
> layout **undo**, and TCI server for logging / cluster software.

---

## Not implemented yet (please don't file these as bugs)

These are on the roadmap, not broken:

* **Dual receiver (RX2)** — single receiver today; a second receiver with
  stereo-split audio and SPLIT pile-up operation is planned.
* **PureSignal** — adaptive pre-distortion (linearizer) is on the roadmap,
  not present yet.
* **Second virtual audio cable (VAC2)** — Lyra bridges one virtual audio
  cable today; an independent second cable (e.g. a logger separate from your
  digital-mode app) is designed but waits on RX2.
* **macOS / Linux** — **Windows only.** The DSP engine and wire layer are
  Windows binaries today; Linux/macOS are planned but **not yet buildable**.
  The native Windows installer is the only supported way to run Lyra.

## Expected behavior (not bugs)

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
2. **Windows version**, and whether your radio is an **HL2 or HL2+**.
3. **What you did and what happened** — steps to reproduce.
4. The in-app **diagnostic log** (open the log viewer in Lyra and copy the
   relevant lines).

Thanks for testing Lyra! — N8SDR

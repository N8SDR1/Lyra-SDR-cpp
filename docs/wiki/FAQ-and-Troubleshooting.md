# FAQ &amp; Troubleshooting

Quick answers to common questions and fixes. For full detail see the
**[User Guide](User-Guide)**; to report something new, open an
**[issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)**.

---

## General

**What hardware does Lyra support?**
Hermes Lite 2 and 2+ over HPSDR Protocol 1. (Dual receiver / RX2 and
PureSignal are on the roadmap.) Other ANAN / HPSDR radios aren't supported
yet.

**Is it free? What's the license?**
Yes — GPL v3+ (compatible with the WDSP DSP engine it uses). See
[NOTICE](https://github.com/N8SDR1/Lyra-SDR-cpp/blob/main/NOTICE.md).

**Where's my version number?**
Header → the title/about area, or **Help → About**. Releases and notes are
on the [Releases page](https://github.com/N8SDR1/Lyra-SDR-cpp/releases).

**Does it run on macOS / Linux?**
The codebase is cross‑platform C++/Qt, but the shipped installer is Windows
only today. See
[CROSS_PLATFORM.md](https://github.com/N8SDR1/Lyra-SDR-cpp/blob/main/docs/CROSS_PLATFORM.md).

---

## Connecting

**Lyra doesn't find my radio.**
- Make sure you pressed **▶ Start**, and the PC + HL2 are on the same LAN.
- You don't need an "Any Subnet" or "Via all NICs" option — Lyra **always**
  scans every adapter (including direct‑connect `169.254.x.x`) with both a
  limited ("any subnet") and subnet‑directed broadcast, for P1 and P2. So a
  no‑show usually means the radio's **reply** can't route back (different
  subnets, no IP assigned yet, or a firewall), not that a broadcast mode is
  off.
- Use **Settings → Hardware → Radio → Add by IP** and type the radio's
  address — this sends a directed probe and works across subnets or where
  broadcast is blocked. On a direct cable with no router, give the radio a
  static IP in the **same subnet** as the PC's NIC so its reply can get home.
- Check the radio is powered and its link LED is up; a direct NIC‑to‑radio
  cable is the most reliable setup.

**Do I need to "Run as administrator"?**
No. The installer adds the firewall rules Lyra needs. If you skipped that or
removed the rules, re‑run the installer.

**It froze / connected to the wrong IP on launch.**
Fixed in current versions: Lyra probes the remembered IP first and
**self‑heals** by re‑scanning if the radio isn't there. Update to the
[latest release](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest).

**Switching between two radios doesn't work.**
**Close** the current radio before you **Open** another — only one is live
at a time. The connected one is shown green/bold in the list.

---

## Audio

**No receive audio.**
- Press **▶ Start**, raise **AF Gain / Volume**, and check the squelch
  isn't holding it closed.
- Confirm the **output path** (Audio panel): **HL2 audio jack** = plug into
  the radio; **PC sound device** = pick the right Windows output + host API.
- On the HL2 jack, some firmware swaps L/R — there's a swap option if one ear
  is silent.

**Pops / clicks / glitches in the audio.**
- Use a **wired** connection (ideally a dedicated NIC straight to the HL2),
  not Wi‑Fi.
- Tick the installer's **network‑throttling** option (or set
  `NetworkThrottlingIndex` accordingly) — it stops Windows starving the
  radio's UDP cadence.
- On the PC sound path, try **WASAPI Exclusive** for lower, steadier
  latency.

**How do I get RX audio into WSJT‑X / FLDigi / a logger?**
Use a **Virtual Audio Cable** (VAC) as the PC output, or **TCI**. See
**[User Guide → Digital modes](User-Guide#digital-modes--getting-rx-audio-to-another-program)**.

---

## Transmit

**I key up but there's no/low power.**
- Make sure **TX drive** is above 0 and, if your build requires it,
  **Enable PA** is ticked (Settings → TX → Advanced). With the PA off the
  radio keys (relays click) but makes no power — by design.
- Power reads on the **meter** (set it to **PWR**); the HL2 telemetry banner
  also shows PA current.

**Does the meter show CW transmit?**
Yes (current versions). Sending CW from the console **or** a paddle/key
flips the meter to forward power and reds the VFO on‑air. The panadapter
stays on RX during CW so you still see the keyed carrier.

**I'm running an amplifier — anything to set first?**
Yes — configure **TR‑sequencing** and the **RF‑delay / ATT‑on‑TX**
protections before keying into an amp. Read
**[User Guide → external amplifier](User-Guide#operating-with-an-external-amplifier-hot-switch-protection)**.

**JTDX won't connect over TCI (but MSHV does).**
Fixed in current versions (the TCI audio handshake now echoes the ack JTDX
expects). Update to the latest release.

---

## Settings &amp; layout

**How do I rearrange / lock the panels?**
Drag a panel by its title bar — a cyan zone previews where it lands (edge /
split / tab / float). Drag the cyan separators to resize. **View → Layouts**
has four save‑able slots plus the factory default. **View → Lock panels**
(Ctrl+L) freezes move *and* resize. See
**[User Guide → Getting around the window](User-Guide#getting-around-the-window)**.

**Back up or move my settings to another PC.**
Settings export/import writes a single profile file you can copy. See
**[User Guide → Backing up &amp; sharing your settings](User-Guide#backing-up--sharing-your-settings)**.

**First launch is stuck "optimizing".**
That's the one‑time FFT plan‑cache build — let it finish (a few minutes).
It only happens once (or after **Clear &amp; rebuild**).

---

Still stuck? Open an
**[issue](https://github.com/N8SDR1/Lyra-SDR-cpp/issues)** with your
Windows version, Lyra version, radio (HL2 / HL2+ + gateware), and what you
saw.

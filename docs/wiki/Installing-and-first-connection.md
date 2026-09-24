# Installing &amp; first connection

This page gets you from a downloaded installer to hearing your first
signal. For the full reference, see the **[User Guide](User-Guide)**.

> **You need:** a Hermes Lite 2 or 2+ on your network, a Windows 10 (1809+)
> or 11 PC (64‑bit, a GPU with OpenGL 3.3+), and the two connected to the
> same LAN (a direct NIC‑to‑radio cable works great).

---

## 1. Install

1. Download the latest **[`Lyra-Setup-X.Y.Z.exe`](https://github.com/N8SDR1/Lyra-SDR-cpp/releases/latest)**.
2. Run it and follow the prompts. The installer:
   - adds the **Windows Firewall** inbound rules Lyra needs (UDP for the
     radio link, TCP for the TCI server) — so Lyra connects **without
     "Run as administrator"**;
   - offers an **optional, unticked** checkbox to disable Windows'
     multimedia **network throttling** (the classic `NetworkThrottlingIndex`
     tweak) for glitch‑free SDR audio. Tick it if you want it (it's left in
     place on uninstall, since other HPSDR apps may rely on it).
3. A new version installs **over** the old one in place — your settings are
   kept.

## 2. First launch (one‑time)

The very first time Lyra runs it builds an **FFT plan cache** ("optimizing"
splash). This is a one‑time, few‑minute step — let it finish and Lyra opens
normally. After that, launches are fast. (If you ever change CPU/RAM or want
to rebuild it, **Settings → Radio → FFT optimization → Clear &amp; rebuild**.)

## 3. Find and open your radio

1. Press **▶ Start** in the header.
2. Open **Settings → Hardware → Radio**. Lyra **discovers** HL2s on your
   network and lists them (board / gateware / RX count).
3. **Double‑click a radio** (or select it and click **Open**). The connected
   radio shows **green and bold**.
4. **Close** the current radio before opening a different one.

> **No "Any Subnet" checkbox needed.** Lyra's discovery is always maximal:
> every scan probes on **all** network adapters — including direct‑connect
> link‑local addresses (`169.254.x.x`, the address Windows self‑assigns with
> no router/DHCP) — and sends **both** a limited broadcast (`255.255.255.255`,
> i.e. "any subnet") **and** a subnet‑directed broadcast, for **both** the P1
> (HL2) and P2 (Brick/ANAN) protocols. So there's nothing to turn on — the
> scan other apps gate behind "Any Subnet" / "Via all NICs" checkboxes is
> simply how Lyra always scans.

### Radio not showing up?
Since the scan above is already maximal, a radio that still doesn't appear is
almost never a missing broadcast mode — the probe reached it; the reply
couldn't get back (different subnets so the answer can't route home, no IP
assigned yet, or a firewall). The fix is to make the reply routable. Use
**Add by IP** — type the radio's address and Lyra sends a **directed unicast
probe** straight to it. This is the answer for:

- a **fixed‑IP** HL2,
- a radio on a **different subnet**,
- a network where **broadcast is blocked**.

If it answers, the entry fills in with the real board info; either way you
can **Open** it. Lyra remembers the last radio and re‑probes its address on
launch — if the radio has moved, it **self‑heals** by re‑scanning instead of
hanging on the stale address.

## 4. Tune and listen

1. Pick a **band** (Band panel) and a **mode** (Mode/Filter panel).
2. Click or scroll on the **panadapter** to tune; drag the passband edges to
   set bandwidth.
3. Set **AF Gain / Volume** on the Audio panel.

### Where's the audio coming out?
Two paths — pick in the Audio panel:

- **HL2 audio jack** (the radio's onboard codec) — single‑crystal, no PC
  audio device needed. Plug headphones/speakers into the radio.
- **PC sound device** — choose your PC output (and host API: WASAPI shared /
  exclusive, etc.).

See **[User Guide → Setting up audio output](User-Guide#setting-up-audio-output)**
for the full rundown, including routing RX audio to another program for
digital modes.

## 5. Before you transmit

Lyra transmits SSB / AM / DSB / SAM / FM, CW, and digital — but set these up
first:

- **Mic input** (Settings → TX, or the Audio panel) — radio codec mic, a PC
  mic via VAC1, or TCI for digital.
- **TX power / drive**, and **enable the PA** if your build needs it
  (Settings → TX → Advanced).
- **Running an amplifier?** Read
  **[User Guide → Operating with an external amplifier](User-Guide#operating-with-an-external-amplifier-hot-switch-protection)**
  first — Lyra has TR‑sequencing and hot‑switch protection you should
  configure before keying into an amp.

Next: the full **[User Guide](User-Guide)**, or
**[FAQ &amp; Troubleshooting](FAQ-and-Troubleshooting)** if something's off.

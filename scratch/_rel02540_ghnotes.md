**ANAN / Brick3 filters on Filters / BCD.** Settings → **Filters / BCD** now shows the Protocol 2 radio's own Alex high-pass or band-pass, TX low-pass, and antenna as read-only lamps and grid ticks. **Live pins** stayed dark because those relays are not J16 OC pins. Hidden on HL2 / BrickSDR. **HiDPI display** already shipped in v0.25.39.

7000DLE / 8000DLE / Orion MkII TX, Xvtr BYTE0–4, and Metis STOP from v0.25.37 stay. DeepFist pitch lock from v0.25.38 stays.

## What's new since v0.25.39

- **Radio filters on Filters / BCD.** Read-only lamps under Live pins and ticked per-band columns, decoded from the same Alex words sent to the radio. Display only — antenna and HPF bypass still live on the Band dock.
- **Hidden on HL2 / BrickSDR.** No Alex-driven front-end to show.
- **Orion MkII receive packet.** Alex1 TX image (TX LPF + antenna, plus T/R relay when the PA is on) is now sent in receive as well as transmit, matching deskHPSDR. Transmit is unchanged. Not shown to be required on the Cyclone V 7000 that prompted it.
- **HiDPI display** and **Log window on screen** stay from **v0.25.39**. Use **Windows Display scale**; do **not** set `QT_SCALE_FACTOR`.
- **DeepFist** pitch lock (±80 Hz) and earlier commit stay from **v0.25.38**. Original decoder: **Brent Crier (N9BC)**.
- **Still armed / locked:** **G2 / G2-1K** still need **Arm Protocol 2 TX**. Red Pitaya still has no Protocol 2 TX profile. A Brick or classic ANAN on Protocol 1 is listed in Discover and **refused on Open** — flash P2.

## Upgrade notes

- Install over 0.25.39 — **settings stay.**
- Coming from ≤0.25.31 on any Protocol 2 radio: **turn Drive down and walk it up** before the first TX (that is the 0.25.33 Drive-twice fix).

## What to check

- **ANAN / Brick3:** Settings → Filters / BCD — RX HPF or BPF, TX LPF, and ANT lamps follow the dial; J16 Live pins still show only OC pins.
- **HL2 / BrickSDR:** no extra lamp row (unchanged).
- **7000 / 8000 / DeepFist / Classic / HiDPI / Log:** same as 0.25.39.

## Getting a log

Settings → **Hardware** → **Enable verbose debug logging** (also a checkbox in Help → **Show Log…**), reproduce, then send `%APPDATA%\N8SDR\Lyra-cpp\logs\lyra-log.txt`.

Help → **What's new in 0.25.40**. Detailed notes: [docs/releases/v0.25.40.md](https://github.com/N8SDR1/Lyra-SDR-cpp/blob/feature/wdsp-2.10/docs/releases/v0.25.40.md).

## Install

Download **Lyra-Setup-0.25.40.exe** (Windows 10 / 11). Click **▶ Start**, pick your radio. Settings stay. If it misbehaves, reinstall **v0.25.39**; **v0.25.22** remains the long return point.

SHA256 `Lyra-Setup-0.25.40.exe`: `838F600745D955D40F0362979D9CC51E34E80199301A2EB29135283DA1035EAD`

The browser desk is a second download: **[Lyra-Remote](https://github.com/N8SDR1/Lyra-Remote/releases/latest)**.

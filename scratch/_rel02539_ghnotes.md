**HiDPI display, ANAN model, log on-screen.** The spectrum and waterfall stay sharp on 4K at 150% Windows scale. A Protocol 2 ANAN with a blank saved model now gets its filter-board profile. **Help → Show Log…** keeps **Copy all** / **Save** / **Open log folder** on screen. **DeepFist** pitch lock already shipped in v0.25.38.

No Protocol 2 / HL2 wire change. 7000DLE / 8000DLE TX, Xvtr BYTE0–4, and Metis STOP from v0.25.37 stay.

## What's new since v0.25.38

- **HiDPI display.** The spectrum trace and peak-hold line are a ribbon one **logical** pixel thick (GPU lines were always one screen pixel, so at 150% / 4K they looked thinner and fainter). Waterfall history grows from 512 rows to the pane's on-screen height, capped at 1024, so a tall pane is no longer a stretched blur.
- **ANAN blank model → filter profile.** Relays follow Lyra instead of the last program. Orion board with a blank record becomes **ANAN-200D**. Brick and any radio that already has a model are untouched.
- **Log window stays on screen** at 4K @ 150%. A window that already fits is left alone.
- **4K:** use **Windows Display scale**. Do **not** set `QT_SCALE_FACTOR`. **Auto-fit the waterfall dB range** is the colour-map control, not a DPI substitute.
- **DeepFist** pitch lock (±80 Hz) and earlier commit stay from **v0.25.38**. Original decoder: **Brent Crier (N9BC)**.
- **Still armed / locked:** **G2 / G2-1K** still need **Arm Protocol 2 TX**. Red Pitaya still has no Protocol 2 TX profile. A Brick or classic ANAN on Protocol 1 is listed in Discover and **refused on Open** — flash P2.

## Upgrade notes

- Install over 0.25.38 — **settings stay.**
- Coming from ≤0.25.31 on any Protocol 2 radio: **turn Drive down and walk it up** before the first TX (that is the 0.25.33 Drive-twice fix).

## What to check

- **4K @ 150%:** spectrum line as solid as on 1080p; waterfall not a soft smear on a tall pane.
- **Help → Show Log…:** Copy all / Save / Open log folder visible.
- **ANAN** that used to hear only the last program's band: pick the marketed model if Settings still shows blank, Start, filters follow.
- **DeepFist / Classic / 7000 / 8000 / HL2 / Brick:** same as 0.25.38.

## Getting a log

Settings → **Hardware** → **Enable verbose debug logging** (also a checkbox in Help → **Show Log…**), reproduce, then send `%APPDATA%\N8SDR\Lyra-cpp\logs\lyra-log.txt`.

Help → **What's new in 0.25.39**. Detailed notes: [docs/releases/v0.25.39.md](https://github.com/N8SDR1/Lyra-SDR-cpp/blob/feature/wdsp-2.10/docs/releases/v0.25.39.md).

## Install

Download **Lyra-Setup-0.25.39.exe** (Windows 10 / 11). Click **▶ Start**, pick your radio. Settings stay. If it misbehaves, reinstall **v0.25.38**; **v0.25.22** remains the long return point.

SHA256 `Lyra-Setup-0.25.39.exe`: `318D59A8F376782904C98E7BCC918ECEA4E5C2D242B2065364BD59E1003C2A75`

The browser desk is a second download: **[Lyra-Remote](https://github.com/N8SDR1/Lyra-Remote/releases/latest)**.

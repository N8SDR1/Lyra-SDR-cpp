# Volker transverter notes (DL1VE)

Parked 2026-10-16 from tester mail + station photo. **Do not treat as a current sprint.** Recall file for possible later Lyra adds that match *his* HL2, not Alex/P2.

**Who:** Volker Eberwein, **DL1VE**. HL2+ (Protocol 1). Multi-version tester (Lyra + Lyra Remote).

## Station (photo)

Stack, top → bottom:

1. **Automatic switch and attenuator 30 dB** — SMA **to XVTR**, **PTT** (RCA), Out Level, TX/RX lamps.
2. **UR3LMZ TR2020 Mini** (one-band VHF) — **OUTPUT** / **INPUT**, labels **to RX** / **to ATT**.
3. **Hermes-Lite** — Ethernet, CL1/CL2, KEY/PTT. One RF jack.
4. Sitting on **XPA125B** (HF+6 m) — HF amp, not in the VHF IF path.

Path: HL2 **PA on** → 30 dB auto T/R box → Mini **TX IF**; Mini **RX IF** back on the other port. Split RX/TX is **those two silver boxes + PTT**, not a second HL2 antenna.

LO: needs **−200…−300 Hz** trim; drifts a little (crystal, not a tracking loop).

Filter board: unknown (N2ADR vs Pico). Does not change ANT count. Pico/N2ADR is band-follow only.

## Mail items (1 of 6) — keep / drop

| Item | Verdict |
|---|---|
| Right-click-only buttons until click-in-window | Win11 focus. **Not Lyra.** |
| Dead RX until Thetis Open | **Lyra.** IO-board RX mux stuck where Thetis left it. Lyra now writes REG_RF_INPUTS on Start and hop. |
| LNA global; want per-band (esp. xvtr) | **Real.** Ham bands already remember LNA; 2 m/70 cm RF is outside `BandMemory` names so the PGA stays global on the Mini. |
| Antenna per band, separate RX vs TX | **HL2 IO-board RX mux.** Photos (2026-10-19) show RF1, Alt RX, PURE. Per-band / per-Xvtr **RF1 vs Alt RX** (I2C 0x1D **register 11** only). Not P2 Alex MAP. Not analog BYTE0–4 (Randy GPA100). PURE not mapped. |
| Lyra Remote usable; want server/service | Remote stays LAN TCI. **No Windows service.** See remote park below. |
| In-app gateware flash | **No.** Vendor / hl2setup. |

## Possible later adds (his setup)

Keep this list short. All optional; none block DIV (DIV already shipped v0.25.20, Brick3 — N/A on his one-ADC HL2).

1. **Per-Xvtr LNA / ATT** — **in tree.** Each Xvtr chip recalls its last HL2 LNA and P2 ATT. Auto stays global; the settled number is stored. First visit has no stored value.
2. **Xvtr slot how-to (docs only)** — for PA-through-pad Mini: **Disable radio PA = off**, Settings **Enable PA** on, **Error (Hz) ≈ −250**. Default Disable PA is **on** (protects a no-pad xvtr). Drift: re-trim; no AFC.
3. **HL2 PTT → his T/R box** — he already keys the top box from PTT. Confirm Lyra HW-PTT-out / MOX follows that RCA if a tester ever says the switch does not follow. Not a new antenna feature.

**Do not add for him**

- Alex MAP, ANT1/2/3, TXANT, Ext1/XVTR jack chips (P2 / Brick3).
- P2 Alex MAP / ANT1/2/3 chips on HL2 (different hardware). PURE SMA until the Pico value is known.
- In-app FPGA/gateware updater.
- Tracking LO / 200–300 Hz AFC for Mini drift.

## Remote (parked, not Mini-specific)

Volker asked for headless Lyra-as-service. Rick’s intent is **not** that.

- **Now:** Lyra Remote = TCI desk on LAN.
- **Reid MI0BOT Thetis:** second Thetis talks **HPSDR UDP to the HL2** (static IP, Any Subnet, port-forward 1024). Shack Thetis **stopped**. Not Thetis-to-Thetis. **Dropped** — no WAN UDP to the radio.
- **Rick’s shape:** second Lyra on the remote PC, **Hosted Lyra** as the rig, shack Lyra stays Open. **Hold.** Design: `docs/architecture/lyra_to_lyra.md`.

## Recall

Photo + mail: hardware T/R and 30 dB; Lyra Xvtr error + **uncheck Disable PA**; **per-Xvtr LNA/ATT** recalls with the chip.

## Operator lock 2026-10-16 (backlog 1–14)

| # | Item | Call |
|---|---|---|
| 1 | Xvtr Mini how-to in User Guide | **Add** (in `USER_GUIDE.md`) |
| 2 | Lyra-to-Lyra (second Lyra as desk, host stays Open) | **Hold** — design in `lyra_to_lyra.md` |
| 3 | Reid-style unicast Open / WAN UDP to the radio | **Drop** |
| 4 | Windows service / headless server package | **Drop** |
| 5 | In-app gateware flash | **Drop** |
| 6 | HL2 MAP / ANT1–2 / Alt Rx | **Shipped v0.25.24** — RF1 vs Alt RX only (I2C 0x1D register 11). Not P2 MAP. Analog 0x1D BYTE0–4 stays Randy-safe. |
| 7 | Mini LO AFC (−200…−300 Hz tracking) | **Drop** |
| 8 | TX rack listen without keying | **Drop** — on-air always sounds different; dummy-load + MON TX for the rack, Kiwi / trusted ear for the set |
| 9 | Linux (OS I/O cut, Windows first) | **Hold** |
| 10 | macOS | **Hold** (no Mac bench) |
| 11 | DIV extra-DDC `0x0F` (D6) | **Drop** |
| 12 | DIV CAT / TCI | **Drop** |
| 13 | OC Control tab | **Drop** — already **Settings → Filters / BCD** |
| 14 | Extra K1EL Winkeyer USB host driver | **Drop** — Thetis/DeskHPSDR never speak K1EL; they use Radio KEY + optional COM CTS/DSR. Lyra already has both. |

**Remaining from this scan (features):** **#2 Lyra-to-Lyra** only. Linux (#9) and macOS (#10) stay hold as platform, behind radios.

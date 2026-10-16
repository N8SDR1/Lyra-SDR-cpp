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
| Dead RX until Thetis Open | Leftover HL2/filter/UDP. **Not a Lyra bug** unless cold Open with nothing else on the radio. |
| LNA global; want per-band (esp. xvtr) | **Real.** Ham bands already remember LNA; 2 m/70 cm RF is outside `BandMemory` names so the PGA stays global on the Mini. |
| Antenna per band, separate RX vs TX | **Not on HL2.** One SMA. His split is the 30 dB box. Do **not** add MAP/ANT1/2/TXANT for him. |
| Lyra Remote usable; want server/service | Remote stays LAN TCI. **No Windows service.** See remote park below. |
| In-app gateware flash | **No.** Vendor / hl2setup. |

## Possible later adds (his setup)

Keep this list short. All optional; none block DIV (DIV already shipped v0.25.20, Brick3 — N/A on his one-ADC HL2).

1. **Per-Xvtr LNA / ATT** — **in tree.** Each Xvtr chip recalls its last HL2 LNA and P2 ATT. Auto stays global; the settled number is stored. First visit has no stored value.
2. **Xvtr slot how-to (docs only)** — for PA-through-pad Mini: **Disable radio PA = off**, Settings **Enable PA** on, **Error (Hz) ≈ −250**. Default Disable PA is **on** (protects a no-pad xvtr). Drift: re-trim; no AFC.
3. **HL2 PTT → his T/R box** — he already keys the top box from PTT. Confirm Lyra HW-PTT-out / MOX follows that RCA if a tester ever says the switch does not follow. Not a new antenna feature.

**Do not add for him**

- Alex MAP, ANT1/2/3, TXANT, Ext1/XVTR jack chips (P2 / Brick3).
- Thetis **Alt Rx** unless he has an I/O board *and* a second SMA (photo does not show that).
- In-app FPGA/gateware updater.
- Tracking LO / 200–300 Hz AFC for Mini drift.

## Remote (parked, not Mini-specific)

Volker asked for headless Lyra-as-service. Rick’s intent is **not** that.

- **Now:** Lyra Remote = TCI desk on LAN.
- **Reid MI0BOT Thetis:** second Thetis talks **HPSDR UDP to the HL2** (static IP, Any Subnet, port-forward 1024). Shack Thetis **stopped**. Not Thetis-to-Thetis. Do **not** copy naked 1024-to-the-radio; VPN + unicast Open if we ever do this.
- **Rick’s shape:** second Lyra on the remote PC, **Hosted Lyra** as the rig, shack Lyra stays Open. Needs a TCI **client** (Lyra is server-only today) or a later encrypted Lyra-to-Lyra link. Large. Behind platform work.

## Recall

Photo + mail: hardware T/R and 30 dB; Lyra Xvtr error + **uncheck Disable PA**; **per-Xvtr LNA/ATT** recalls with the chip.

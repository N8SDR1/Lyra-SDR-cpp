# Lyra (unreleased) — Sheliak · Protocol 2 PureSignal: TX IQ back at unity

Hermes Lite 2 / 2+ (Protocol 1) and **BrickSDR2 / Brick3SDR** (Protocol 2).
Windows 10 / 11. Version number to be assigned at release (next after
v0.25.32). Commits `e95b135`, `a278e9b`, `12cd393` on `feature/wdsp-2.10`.

## The bug

**v0.25.16 through v0.25.31** (and the unreleased v0.25.32 tag) scaled the
**Protocol-2 TX IQ** with the Drive slider *on top of* the analog drive byte
HP `[345]`. Drive was applied twice. The radio's TX-DAC replica that
PureSignal uses as its reference follows the IQ, so below full drive it
never reached SetPk (0.2899) and the calibrator could not finish
collecting: **FB 0 / cal 0 / st 4**, and the auto-attenuator walked the
coupler pad down to **0 dB** on every band (Greg VK3KV, Brick2). PureSignal
on Brick2 / Brick3 only had a chance at 100 % Drive.

## The fix

- **P2 TX IQ stays at unity**; watts live on HP `[345]` only. This is what
  DeskHPSDR (`do_scale = 0` for every radio except Penelope / HL2) and
  Thetis (`SetTXFixedGainRun(0, false)`) do. PureSignal now collects at
  **any** drive on Protocol 2. **HL2 / Protocol 1 power handling is
  unchanged.**
- **Host DUC tap removed** (v0.25.32's fallback "replica" from the host TX
  stream). It ran at the wrong rate with no time alignment and was never a
  valid PureSignal reference. pscc is fed from the radio pair only
  (even = coupler, odd = TX-DAC replica), as before v0.25.32. The
  `+3` DDC-config byte write on every DDC slot from v0.25.32 is also gone.
- **48 kHz PureSignal fix**: the one-ADC 48 kHz IQ trim (−29 dB) no longer
  applies while PS is live, so a Brick2 operator at 48 kHz gets a
  full-scale coupler / replica into the calibrator.

## **OPERATOR WARNING — read before the first key-up**

**After this upgrade the same Drive slider position produces MORE RF on
Protocol-2 radios than v0.25.16–0.25.31 did** — roughly **+7 dB (about
5×) at 40 % Drive, +10 dB at 30 %**, and more the lower the slider sits.
100 % is unchanged.

**If you drive an external amplifier — especially a low-drive amp such as
the VK 1 kW — turn Drive DOWN before keying and walk it up while watching
the radio's output.** Do not key first and look second.

Learned amp **watts-cap ceilings are reset once** on the first open of each
P2 rig after upgrading (they were learned under the doubled curve and are
wrong for unity IQ). Re-run **TUN** per band to re-learn the cap. HL2 /
Protocol-1 learned ceilings are untouched.

## Notes

- `SetPSDeadlockMinFrac` is not exported by the bundled `wdsp.dll` (TAPR
  2.10); the guarded call in the PS arm path is inert and the effective
  value is the compiled-in 0.06. Left as-is.
- Auto-attenuator math, thresholds, three-tick sequence and per-band pad
  persistence are unchanged.

## If it misbehaves

Reinstall **v0.25.31** from GitHub (PureSignal on P2 will again only work at
full drive there). Settings stay. **v0.25.22** remains the long return point.

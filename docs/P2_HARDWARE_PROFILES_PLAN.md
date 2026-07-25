# Protocol 2 + Hardware Profiles — Architecture Plan

Status: living document (started 2026-07-19, KD4YAL Saturn bring-up).
Bench radio: ANAN-G2 (Saturn board, FPGA fw 27, hardened p2app v50 at
192.168.0.139).  References on the bench machine: KD4YAL Saturn fork
(radio-side p2app) and KD4YAL Thetis fork (client-side reference).

## Where the bring-up stands (all bench-verified on the G2)

Working today via `src/wire/P2Session` + `src/wire/P2RxBridge`:
discovery (dual P1+P2 sweep), session/controller lease, the four control
packets (General / DDC-specific / DUC-specific / High Priority), HP
status telemetry, DDC0 IQ at the engine rate into the WDSP RX chain +
panadapter + audio out, VFO follow, DDS phase-word frequency encoding,
Saturn front-end control (BPF/LPF/antenna via Alex words — the client
OWNS the G2 front end; zeros = disconnected antenna), hardware model
catalog + Settings model/antenna pickers.  RX-only by construction.

Hard-won wire facts live in the P2Session.h preamble; per-bug history
in the session memory notes.  Bench tool: `test_p2_session <ip> [secs]
[freqHz] [ant]`.

## Target architecture (agreed 2026-07-19)

### Session layer
```
RadioSession (common interface)
 ├── Protocol1Session   ← today's HL2Stream, renamed in place later
 └── Protocol2Session   ← grows out of P2Session/P2RxBridge
      ├── P2DiscoveryCodec      (from hl2_discovery's P2 half)
      ├── P2ControlEncoder      (General/RX/TX/HP builders)
      ├── P2ReceiveDispatcher   (per-source-port demux + per-stream seq)
      ├── P2AudioWriter         (RX audio → radio, base+4, 16-bit BE)
      └── P2TxIqWriter          (TX IQ → radio, base+5, 24-bit BE)
```
Do NOT fold P2 into HL2Stream.  The quickest safe path remains
completing the existing ChannelMaster direct port (Network.cpp's
deferred ETH branches) rather than a parallel implementation.

### Hardware profile layers
1. **HardwareModelDescriptor / HardwareCatalog** — DONE (first cut):
   `src/hardware/HardwareCatalog.{h,cpp}` — Thetis
   clsHardwareSpecific.cs as data rows (ADC count, MkII BPF, ADC
   supply, LR audio swap, volts/amps telemetry + conversion, PS peak
   defaults P2, RX meter/display calibration, audio amp, RX2 stepped
   atten, per-band PA gain defaults).  Keyed by Thetis model strings
   ("ANAN-G2"…).  Reserved upstream ordinals: model ANAN_G2E=16,
   board HermesC10=20.
2. **RadioProfile** — per physical device, keyed by MAC (nickname,
   last IP, model key, protocol mode Auto/P1/P2, antenna routing,
   calibration overrides).  DHCP change updates lastKnownIp, never
   forks a profile.
3. **StationProfile** — the Thetis Database-Manager analogue (band
   memory, layout, audio devices, CAT/TCI, references to operating
   profiles).  Stored as JSON docs (schemaVersion + profileId), NOT
   Thetis database.xml; a Thetis importer maps only recognized fields
   (comboRadioModel, PA gains, calibration, antennas, atten, PS, mic,
   OC, audio devices, TX profiles).
4. **OperatingProfile** — the EXISTING TX/RX ProfileManager; keep
   separate (rename internally later to avoid confusion).

Proposed on-disk layout: `profiles/{index.json, radios/<uuid>.json,
stations/<uuid>/…, operating/{tx,rx}/…}`.

### Model catalog (Thetis parity)
Atlas→HPSDR · Hermes→HERMES/ANAN-10/ANAN-100 · HermesII→ANAN-10E/
ANAN-100B · Angelia→ANAN-100D · Orion→ANAN-200D · OrionMKII→ORION
MKII/ANAN-7000DLE/ANAN-8000DLE/Anvelina-Pro3/Red-Pitaya · HermesLite→
HL2 · Saturn→ANAN-G2/ANAN-G2-1K · (HermesC10→ANAN-G2E, later).
Marketed model ≠ discovered board — same FPGA family, different PA
tables / calibration / mic wiring / relays / PureSignal setup.

### G2 runtime profile parity (implemented 2026-07-25)

The G2 profile now consumes the same model-specific facts as Thetis:

- selected marketed model (`ANAN-G2` / `ANAN-G2-1K`) chooses the
  Saturn front-end encoder; discovery board id alone never enables Alex
  behavior on another product;
- two ADCs, MkII BPF behavior, supply/current conversion, RX meter
  offset `-4.476 dB`, RX display offset `-4.4005 dB`, P2 PureSignal
  peak metadata `0.6121`, and the G2 PA table remain sourced from
  `HardwareCatalog`;
- DDC0 can select ADC1 or ADC2, and HP bytes 1442/1443 carry the
  selected ADC's manual `0..31 dB` step attenuation;
- status overload bits and both ADC peaks feed the protocol-neutral
  `ActiveFrontEndModel`; the Audio panel shows ATT/ADC/overload for P2
  and preserves the original LNA/Auto behavior for P1;
- ATT, ADC, TRX antenna, RX input (`TRX`, `BYPS`, `EXT1`, `XVTR`) and
  HPF bypass are persisted per rig and per band under
  `rig/<id>/band_mem/<band>/p2/`;
- the S-meter references P2 readings back to the antenna with the
  actual attenuation plus the G2 meter offset, while the spectrum and
  waterfall use the G2 display offset;
- `test_p2_g2_profile` locks the 1444-byte HP and DDC-specific packet
  images, including phase words, Alex routing, attenuation, and ADC
  source. The live G2 transport test remains the hardware gate.

Deliberately not added: automatic P2 attenuation. The first G2 profile
is manual and observable; automation belongs after overload/level
behavior is characterized across bands. TX and PureSignal remain
separate later phases.

## Phase sequence
1. **Freeze reference + golden tests** — pin the Thetis fork; byte
   golden tests for discovery, the four control packets, audio/TX-IQ
   frames (the regression checklist + capture tools in the Saturn repo
   are the harness).
2. **Discovery + profile foundation** — generic RadioInfo (WireProtocol,
   board, model), catalog (done), RadioProfile persistence, hardware
   manager UI, Auto/P1/P2 selection.
3. **P2 RX-only** — DONE for DDC0 (this bring-up); remaining: custom
   port bases, large socket buffers, per-stream seq stats surfaced.
4. **More receivers + telemetry** — RX2/DDCs, ADC select, diversity,
   wideband, full overload/telemetry decode (status bytes are already
   parsed; surface them via the catalog's conversion constants).
5. **P2 TX** — RX-audio return (base+4), TX IQ (base+5), PTT/MOX,
   CW/keyer, drive + PA gates, TX meters, antenna/filter TX words.
6. **PureSignal** — feedback DDC config, model defaults (Saturn PS
   peak 0.6121 vs 0.2899), atten safety, two-tone validation.
7. **Profile management** — create/rename/duplicate/import/export,
   per-profile backups, QSettings migration, Thetis importer.
8. **Hardware parity bench-out** — Hermes/HermesII P2 → Angelia/Orion
   → OrionMKII family → Saturn/G2 (done first here) → G2E → specialty.

## HL2 retention guarantees (locked 2026-07-19)

The HL2 is the project's original, operator-calibrated radio.  The P2
work must never degrade it:

1. **Settings ownership.**  Every key the P2 path introduces is new
   and namespaced (`radio/hardwareModel`, `p2/trxAntenna`) — nothing
   in the HL2 path reads them, and the P2 path writes NO HL2-owned key
   (LNA gain, filter board/OC table, PA Gain tab, TX chain, CW, VOX…).
   Two keys are deliberately SHARED as "the operator's dial", not
   radio state: `rx/freqHz` and band memory (the VFO carries across a
   radio switch on purpose).  `radio/sampleRate` is shared but the P2
   path only follows it (all engine rates are P2-legal) — it never
   rewrites it.
2. **Auto-connect stays HL2.**  P2 radios are never written to the
   remembered-radio record (`lastRadio`/`radio/lastIp`); startup
   auto-connect remains a P1/HL2-only behavior until Layer-2 profiles
   make startup-radio an explicit choice.
3. **Known shared-state gap — audio output device** (`audio/output`):
   one global key today.  The Saturn needs a PC device; the HL2
   default is the radio jack.  Switching radios currently means the
   operator may flip Settings → Audio.  This becomes per-radio
   routing in RadioProfile (Layer 2) — do NOT hack a second global
   key for it.
4. **Layer-2 migration rule.**  When RadioProfile lands, the EXISTING
   QSettings state is seeded as the HL2's profile (model key
   "HERMES-LITE", its calibrations intact) — new radios get fresh
   profiles; the HL2 must come out of the migration byte-identical in
   behavior.  The HL2 catalog row exists only for identity/metadata —
   HL2Stream does not consult the catalog and keeps its bench-proven
   configuration path unchanged.

## Scope guardrails
- Many saved profiles, ONE active radio.  Simultaneous radios would
  require per-session RadioNet/ChannelMaster (today they're process
  globals: prn, hpsdrModel, radioProtocol) — explicitly out of scope.
- The G2's front end is client-owned (p2app hardcodes Alex manual
  mode): any new P2 code path MUST keep sending the Alex words.
- feedIq/dispatchAudioFrame hide P1-session invariants (OutBound rings
  etc.) — audit every new consumer that drives the engine without
  HL2Stream::open (three real bugs came from this in one day).

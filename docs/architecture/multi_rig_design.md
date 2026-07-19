# Multi-Rig Support — Architecture Design

Status: **DRAFT for operator red-line** (2026-07-19)
Scope of this doc: the **configuration / capability layer** only.
The Protocol 2 wire engine (needed to actually put a BrickSDR/ANAN on
the air) is a **separate, larger workstream** and is intentionally NOT
designed here.

---

## 0. Locked operator decisions (2026-07-19)

1. **Operation model: one rig live at a time** (flip between rigs).
   NOT simultaneous two-instance SO2R. (SO2R-simultaneous is explicitly
   deferred; the namespacing below does not preclude it later but we are
   not building per-instance isolation now.)
2. **Rig identity = user label + MAC.** The MAC (already captured by
   discovery in `lastRadio/mac`) is the stable hardware key; the label is
   the human-facing name. A rig profile can be pre-staged (label + known
   MAC) before the hardware is present.
3. **Build order: config/capability layer FIRST**, protocol-agnostic,
   validated against the existing HL2. Protocol 2 engine comes after.

## 1. First target hardware

- **BrickSDR2** (EU1SW / Lin00bs). OpenHPSDR Hermes-based, Ethernet /
  LAN discovered by UDP broadcast — same family lineage as HL2/ANAN.
- **Protocol 2**, **14-bit ADC**, roughly an **ANAN-10E-class** rig.
- Value: a real P2 target to develop/test against, and the stepping
  stone to the full ANAN family.

## 2. Current state (why this is tractable, not a hole)

lyra-cpp today stores **all settings flat and global** under one scope
`HKCU\Software\N8SDR\Lyra-cpp`. There is **no radio-model concept**; the
HL2 is identified only by IP, and the radio's identity
(`radio/lastIp`, `lastRadio/*`) is treated as **disposable machine-state
and excluded from backup**. That single assumption — *"exactly one
radio, identity is throwaway"* — is the thing we reverse.

Everything else is just **nesting a flat keyspace under a rig prefix**,
and the code already has runtime-computed key-prefix patterns to model
on: the per-monitor layout slots (`session/<resolutionSlot>/…`), the CAT
instances (`cat1/2/3/`), the VAC slots (`vac1/2/`), and the existing
**Profiles feature** (`profiles/`, #49) whose apply path we reuse for rig
switching.

## 3. Config classification — per-rig / shared / machine-local

| Category | Keys today | Scope | Rationale |
|---|---|---|---|
| Freq calibration | `cal/` | **Per-rig** | Crystal error is physical per unit |
| Per-band RF/LNA, TX/TUN drive | `band_mem/<band>/{lna,txDrive,tuneDrive}` | **Per-rig** | Different front-ends & PAs |
| Per-band waterfall/spectrum dB scaling | `band_mem/<band>/{dbMin,dbMax,wfMin,wfMax}`, `panadapter/` | **Per-rig** | 14-bit Brick vs 12-bit HL2 noise floor differ |
| Power / meter cal | `meter/{pwrRatedMaxW,calDb}` | **Per-rig** | HL2 ~5 W vs Brick/ANAN differ |
| OC / BCD filter masks | `oc/…/<bandIndex>` | **Per-rig** | Different hardware wiring |
| Audio config (devices, gains, routing, VAC) | `audio/*`, `vac1/2/*` | **Machine-local / per-PC** (operator call 2026-07-19) | Audio is a PC fact. The rig only determines *whether physical audio I/O exists* — that's a capability (see below), not per-rig config |
| Layout | `session/<slot>/…` | **Per-rig, default shared** | Operator uses one layout; another op won't. Shared default, per-rig override opt-in |
| Callsign / grid | `operator/*` | **Shared** | Station-level |
| Band plan | `band_plan/*`, `bands/*` | **Shared** | Rig-independent |
| CW decoder, spots, weather | `cw/decode*`, `spots/*`, `wx/*`, `propagation/*` | **Shared** | Rig-independent |
| TCI / CAT network | `tci/*`, `cat1..3/*`, `serialptt/*` | **Shared default, per-rig override opt-in** (operator call 2026-07-19) | One rig live → no port contention, so shared "just works" for same-apps-on-both. A user who wants a rig bound to a different app/port/CAT-emulation flips a per-rig override (absent key → falls back to shared). SAME mechanism as layout — one override pattern, two category groups. Ship shared-only first; override slots in later with no migration |
| Graphics backend, gfx safe-mode | `ui/graphicsBackend`, `ui/gfx*` | **Machine-local** | Already backup-excluded |
| Companion launch paths | `profileLaunch/*` | **Machine-local** | Already backup-excluded |

> Note: TCI/CAT are **shared** only because one rig is live at a time. If
> simultaneous SO2R is ever built, network bindings and audio
> output/VAC become strictly per-instance — the per-rig namespace below
> already gives us the seam to move them without a rewrite.

## 4. Namespace scheme

```
rigs/
  active            = <rigId>                # currently selected rig
  <rigId>/
    label           = "Hermes Lite 2"        # user-facing
    mac             = "aa:bb:cc:dd:ee:ff"    # stable hardware key
    family          = "hl2" | "brick_p2" | "anan_p2" | ...
    lastIp          = ...                    # machine-local, per-rig
rig/<rigId>/
    cal/…
    band_mem/<band>/…
    meter/…
    oc/…
    panadapter/…            # scaling subset (see §3)
    tx/…                    # drive/power subset
    audio/…                # gains/routing subset (NOT device names)
    session/<slot>/…       # ONLY if this rig overrides the shared layout
# flat / shared (unchanged): operator/, band_plan/, cw/decode*, spots/,
#   wx/, propagation/, tci/, cat*/, serialptt/, serialcwkey/
# machine-local (unchanged, backup-excluded): ui/graphicsBackend,
#   ui/gfx*, audio device names, profileLaunch/, layout geometry
```

`rigId` = a stable slug derived from the MAC (e.g. `rig_aabbccddeeff`)
so it's deterministic and survives IP changes.

## 5. `RadioCapabilities` (the protocol-agnostic payoff)

A per-family capability struct populated at rig-select time. UI and
config defaults read from it, so a fresh rig profile is sane, not blank.

```
struct RadioCapabilities {
    QString family;              // "hl2", "brick_p2", ...
    int     protocol;            // 1 (HPSDR P1) | 2 (P2)
    int     nddc;                // receiver count
    int     adcBits;             // 12 (HL2) | 14 (Brick)
    bool    hasOnboardAudioIO;   // physical mic-in + audio-out on the unit.
                                 //   base HL2 = FALSE (PC audio only);
                                 //   HL2+ = TRUE (AK4951 codec jacks);
                                 //   Brick = TRUE; ANAN = TRUE.
                                 //   NOTE: within the HL2 family this varies
                                 //   per UNIT (base vs +), so it is detected/
                                 //   settable, not purely family-fixed.
    AudioPath defaultAudioPath;  // RADIO_JACK (if hasOnboardAudioIO) | PC_SOUND
    PowerModel txPowerModel;     // per-family drive→watts + rated max
    Range   lnaRange;            // front-end gain span
    bool    puresignalRequiresMod;
    // ...extend as families are added
};
```

This is where "the 14-bit Brick has a different noise floor, so the
waterfall autoscale defaults differ" is handled — a new rig gets
family-correct defaults instead of the operator hand-tuning from zero.

## 6. Rig switching (one at a time)

Reuse the existing Profiles apply machinery (#49):

1. If a rig is live: clean DSP/stream teardown (same discipline as the
   layout safe-reset).
2. Set `rigs/active = <rigId>`.
3. Reload that rig's `rig/<rigId>/…` subtree into Prefs.
4. Apply `RadioCapabilities` defaults for the family.
5. Connect by the rig's `lastIp` (or rediscover by MAC).

## 7. Backup / recovery

- `isMachineSpecificKey` flips: **MAC/label become the organizing
  identity (kept in backup)**; `lastIp` stays machine-local/excluded.
- `.lyra` export gains a scope choice: **this rig** vs **whole station**
  (all rigs + shared). The `_backup/` header carries the source rig's
  label + MAC.
- Restore can target a chosen rig (map the source rig's per-rig keys onto
  a selected `rigId`).
- Snapshots tag the active `rigId`.

## 8. Migration of the existing single-rig config (highest risk)

**Sequencing (refined at Stage 2):** the identity/registry seed is
additive and lands in Stage 2 (inert). The **actual key relocation fires
in Stage 3, co-located with the read-routing** — so the running app never
reads a per-rig key that has just been moved out from under it. The
snapshot-gated one-shot below is therefore a Stage-3 action.

First activation of the multi-rig path runs a **one-shot, snapshot-gated**
migration:

1. Take an auto-snapshot first (the backup engine already does snapshots
   that survive a registry nuke — the safety net).
2. Detect the existing flat config + `lastRadio/mac`.
3. Create the HL2 rig profile: label "Hermes Lite 2", that MAC, family
   `hl2`.
4. Move the **per-rig** keys (§3) under `rig/<rigId>/…`; leave shared
   keys flat; leave machine-local keys as-is.
5. Set `rigs/active` to the HL2 rig.

Fail-safe: if anything looks wrong, the pre-migration snapshot restores
the operator's dialed-in HL2 setup exactly. This step gets its own
careful pass + test before it ships.

## 9. Staged build order

- **Stage 0** — this design doc (operator red-line).
- **Stage 1** — `RadioCapabilities` struct + HL2 population. No behavior
  change; pure scaffolding, testable today.
- **Stage 2** — rig registry + `rigs/active` + label/MAC identity +
  legacy seed. **Additive/inert** (nothing reads it yet). The §8
  relocation is NOT fired here.
- **Stage 3** — fire the §8 snapshot-gated relocation of the per-rig keys
  under `rig/<rigId>/…` AND route the reads/writes through the active
  `rigId`, together, so the app never reads a moved key. Wire the seed +
  switch flow.
- **Stage 4** — rig picker UI + switch flow (reuse Profiles) + backup
  rig-awareness (§7).
- **Separate workstream (after):** the Protocol 2 wire engine — the
  Brick's actual on-air path.

## 10. Open items for operator

- ~~audio~~ **RESOLVED 2026-07-19:** audio config is **machine-local /
  per-PC**; the *presence* of physical audio I/O is a `RadioCapabilities`
  flag (`hasOnboardAudioIO`: base HL2 no, HL2+/Brick/ANAN yes).
- ~~TCI/CAT~~ **RESOLVED 2026-07-19:** shared default + per-rig override
  opt-in (same mechanism as layout; ship shared-only first, override
  later with no migration).
- ~~layout~~ **RESOLVED 2026-07-19:** per-rig, defaulting to the shared
  layout (per-rig override opt-in — same mechanism as TCI/CAT).
- Confirm the default label for the existing HL2 ("Hermes Lite 2 / 2+").
- Any rig-specific setting not captured above.

// Lyra — RigRegistry: the set of known rigs + which one is active.
//
// Multi-rig Stage 2 (config/capability layer).  See
// docs/architecture/multi_rig_design.md.
//
// A "rig" is a named transceiver profile the operator can select.  Its
// IDENTITY is a stable id derived from the hardware MAC (which discovery
// already captures) plus a user-facing label.  This file owns ONLY the
// identity/registry — the rig's actual per-rig config lives under
// `rig/<rigId>/…` and is relocated/routed in Stage 3 (co-located with the
// read-routing so the app never reads a key that just moved).
//
// STATUS (Stage 2): INERT scaffolding.  Nothing in the running app calls
// this yet — Stage 3 wires seedFromLegacyRadio() at startup and drives
// the switch flow.  Backed by QSettings under the `rigs/` group:
//
//   rigs/active            = <rigId>                (selected rig)
//   rigs/<rigId>/label     = "Hermes Lite 2 / 2+"   (user-facing)
//   rigs/<rigId>/mac       = "aa:bb:cc:dd:ee:ff"    (stable hardware key)
//   rigs/<rigId>/family    = "hl2" | "brick_p2" …   (stable token)
//   rigs/<rigId>/lastIp    = ...                     (machine-local)
//
// Free-function style + QSettings-backed, matching lyra::backup.

#pragma once

#include <QString>
#include <QList>

#include "RadioCapabilities.h"

namespace lyra::rig {

struct RigProfile {
    QString     rigId;                       // stable slug, e.g. "rig_aabbccddeeff"
    QString     label;                       // user-facing name
    QString     mac;                         // stable hardware key (may be empty
                                             //   for a pre-staged rig w/o known MAC)
    RadioFamily family = RadioFamily::Unknown;
    QString     lastIp;                      // last-connected IP (machine-local)

    bool isValid() const { return !rigId.isEmpty(); }
};

namespace registry {

// Active rig id (`rigs/active`), or "" if none selected yet.
QString activeRigId();
void    setActiveRigId(const QString &rigId);

// All rig profiles enumerated from `rigs/<id>/`.
QList<RigProfile> rigs();
RigProfile        rig(const QString &rigId);   // invalid profile if absent
bool              exists(const QString &rigId);

// Insert or update a rig profile's identity record.  Does NOT touch the
// rig's `rig/<rigId>/…` config subtree.
void upsertRig(const RigProfile &p);

// Remove a rig's identity record.  Does NOT delete its config subtree
// (that is a separate, deliberate operator action handled later).
void removeRig(const QString &rigId);

// Stable rig id from a MAC: "AA:BB:CC:DD:EE:FF" -> "rig_aabbccddeeff".
// Returns "" for an empty MAC (caller assigns a synthetic id if needed).
QString rigIdForMac(const QString &mac);

// Map an HPSDR discovery board-name string (e.g. "HermesLite", "Orion")
// to a RadioFamily.  Used by the discovery→rig hook so opening a radio
// stamps its rig profile with the right family.  Unknown/absent → Hl2
// (the only shipping hardware today).
RadioFamily familyForBoardName(const QString &boardName);

// Protocol-aware family resolution for the discovery→rig hook.  A P2
// reply uses a DIFFERENT board table from P1 (byte [11]), so the board
// NAME alone can't distinguish e.g. a P1 "Hermes" from the P2 Hermes-
// class BrickSDR2 — the protocol is the discriminator.
//   protocol 1 → familyForBoardName (HL2 / ANAN-P1)
//   protocol 2 → "Saturn…" ⇒ AnanP2 (ANAN G2), else ⇒ BrickP2
//                (the BrickSDR2 reports Hermes-class on P2)
RadioFamily familyForDiscovery(int protocol, const QString &boardName);

// Find-or-create a rig by MAC.  Returns the rigId.  If it already exists,
// refreshes label/family/lastIp from any non-empty argument.  Pure
// identity — never relocates config.
QString ensureRig(const QString &mac, RadioFamily family,
                  const QString &label, const QString &lastIp = QString());

// One-shot seed from the legacy single-radio record (`lastRadio/*` +
// `radio/lastIp`).  If the registry is empty AND a radio is remembered,
// create its rig profile and set it active.  ADDITIVE ONLY — writes new
// `rigs/*` keys, never removes or moves existing config.  Idempotent
// (no-op once any rig exists).  Returns the active rigId, or "".
//
// NOTE: this seeds IDENTITY only.  The operator's existing per-rig config
// keys are relocated under `rig/<rigId>/…` in Stage 3, snapshot-gated,
// together with the read-routing.
QString seedFromLegacyRadio();

} // namespace registry
} // namespace lyra::rig

// Lyra — RigScope: active-rig key routing + snapshot-gated migration.
//
// Multi-rig Stage 3 (config/capability layer).  See
// docs/architecture/multi_rig_design.md §8.
//
// This is the mechanism that makes per-rig settings actually per-rig:
//   - scope::rigKey("cal/freqCorrection") -> "rig/<activeId>/cal/freqCorrection"
//   - migrate::migrateGroupToActiveRig("cal/") relocates the operator's
//     existing FLAT per-rig keys under the active rig, ONCE, snapshot-first.
//
// Per-rig subsystems are routed one group at a time; each routed group is
// added to the startup migration so the relocation and the read-routing
// always land together (the app never reads a key that just moved).
//
// A dev/test override (env LYRA_ACTIVE_RIG=<rigId>) redirects LIVE key
// routing to a different rig WITHOUT persisting it — this lets rig
// isolation be smoke-tested on a single physical radio (route to a throwaway
// rig id, change a setting, drop the env var, confirm the real rig's config
// is untouched).  Migration always targets the PERSISTED active rig, never
// the dev override.

#pragma once

#include <QString>

namespace lyra::rig {

namespace scope {

// Active rig id for LIVE key routing.  Honors the LYRA_ACTIVE_RIG dev
// override (session-only, not persisted); otherwise the persisted registry
// active rig.
QString activeRigId();

// "rig/<activeId>/" — or "" when no rig is active, which makes rigKey() a
// transparent pass-through to the pre-multi-rig flat location.
QString activeRigPrefix();

// Rig-scope a per-rig logical key.  Returns the key unchanged when no rig
// is active (safe fallback → legacy flat behavior).
QString rigKey(const QString &logicalKey);

} // namespace scope

namespace migrate {

// One-shot, snapshot-gated relocation of a per-rig key GROUP (prefix, e.g.
// "cal/") from its legacy flat location to the PERSISTED active rig's
// namespace (rig/<id>/<prefix>).  Idempotent per group — no-op when the
// scoped location already holds keys, when there are no legacy keys, or when
// no rig is active.  Takes ONE snapshot per process before the first
// relocation so the pre-migration config is recoverable.
void migrateGroupToActiveRig(const QString &groupPrefix);

// Relocate ONE exact per-rig key (e.g. "meter/calDb") from its legacy flat
// location to rig/<id>/<key>.  For a per-rig setting that lives inside a
// MIXED group (e.g. meter/ holds hardware cal AND UI prefs), so the
// whole-group move would wrongly drag shared UI prefs into the rig
// namespace.  Same snapshot-gated, idempotent, no-op-when-absent semantics
// as migrateGroupToActiveRig, but on a single exact key.
void migrateKeyToActiveRig(const QString &flatKey);

} // namespace migrate
} // namespace lyra::rig

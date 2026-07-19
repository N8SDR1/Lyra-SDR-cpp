// Lyra — RigScope implementation.  See the header.

#include "RigScope.h"
#include "RigRegistry.h"
#include "../backup.h"

#include <QSettings>
#include <QStringList>
#include <QVariant>

namespace lyra::rig {

namespace scope {

QString activeRigId() {
    const QString ov = qEnvironmentVariable("LYRA_ACTIVE_RIG").trimmed();
    if (!ov.isEmpty()) return ov;            // dev/test override, session-only
    return registry::activeRigId();
}

QString activeRigPrefix() {
    const QString id = activeRigId();
    if (id.isEmpty()) return QString();
    return QStringLiteral("rig/%1/").arg(id);
}

QString rigKey(const QString &logicalKey) {
    const QString pfx = activeRigPrefix();
    if (pfx.isEmpty()) return logicalKey;    // no rig → legacy flat location
    return pfx + logicalKey;
}

} // namespace scope

namespace migrate {
namespace {

bool g_snapshotTaken = false;   // one migration snapshot per process

void ensureSnapshot() {
    if (g_snapshotTaken) return;
    g_snapshotTaken = true;
    const int keep = QSettings()
                         .value(QLatin1String(lyra::backup::kKeepCount),
                                lyra::backup::kKeepCountDefault)
                         .toInt();
    lyra::backup::saveSnapshot(
        QStringLiteral("Before multi-rig migration"), false, keep);
}

} // namespace

void migrateGroupToActiveRig(const QString &groupPrefix) {
    // Migration always targets the PERSISTED active rig — never the
    // LYRA_ACTIVE_RIG dev override.
    const QString id = registry::activeRigId();
    if (id.isEmpty() || groupPrefix.isEmpty()) return;

    const QString scopedPrefix =
        QStringLiteral("rig/%1/%2").arg(id, groupPrefix);

    QSettings s;
    const QStringList all = s.allKeys();

    // Already migrated? (scoped location holds ≥1 key)
    for (const QString &k : all)
        if (k.startsWith(scopedPrefix)) return;

    // Gather legacy flat keys under the group prefix.
    QStringList legacy;
    for (const QString &k : all)
        if (k.startsWith(groupPrefix)) legacy << k;
    if (legacy.isEmpty()) return;   // nothing to relocate

    ensureSnapshot();

    for (const QString &k : legacy) {
        const QVariant v   = s.value(k);
        const QString rel  = k.mid(groupPrefix.size());
        s.setValue(scopedPrefix + rel, v);
    }
    // Remove the legacy originals — the snapshot covers recovery, and a
    // single source of truth avoids stale-vs-scoped drift.
    for (const QString &k : legacy)
        s.remove(k);
    s.sync();
}

void migrateKeyToActiveRig(const QString &flatKey) {
    const QString id = registry::activeRigId();
    if (id.isEmpty() || flatKey.isEmpty()) return;

    const QString scopedKey = QStringLiteral("rig/%1/%2").arg(id, flatKey);

    QSettings s;
    if (s.contains(scopedKey)) return;   // already migrated
    if (!s.contains(flatKey))  return;   // nothing to relocate

    ensureSnapshot();
    s.setValue(scopedKey, s.value(flatKey));
    s.remove(flatKey);                   // snapshot covers recovery
    s.sync();
}

} // namespace migrate
} // namespace lyra::rig

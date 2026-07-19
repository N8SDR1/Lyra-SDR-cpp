// Lyra — RigRegistry implementation.  See the header.
//
// Stage 2: identity + registry + legacy seed, all ADDITIVE and INERT.
// The family enum is persisted as a STABLE STRING TOKEN (not the raw enum
// int) so re-ordering RadioFamily never mis-reads an operator's stored
// rig.  No config relocation happens here — that is Stage 3.

#include "RigRegistry.h"

#include <QSettings>

namespace lyra::rig {
namespace {

constexpr auto kGroup   = "rigs";
constexpr auto kActive  = "rigs/active";

// Stable persistence tokens for RadioFamily.  NEVER renumber/rename an
// existing token — it is written into operator QSettings.
QString familyToken(RadioFamily f) {
    switch (f) {
    case RadioFamily::Hl2:     return QStringLiteral("hl2");
    case RadioFamily::AnanP1:  return QStringLiteral("anan_p1");
    case RadioFamily::AnanP2:  return QStringLiteral("anan_p2");
    case RadioFamily::BrickP2: return QStringLiteral("brick_p2");
    case RadioFamily::Unknown:
    default:                   return QStringLiteral("unknown");
    }
}

RadioFamily familyFromToken(const QString &t) {
    if (t == QStringLiteral("hl2"))      return RadioFamily::Hl2;
    if (t == QStringLiteral("anan_p1"))  return RadioFamily::AnanP1;
    if (t == QStringLiteral("anan_p2"))  return RadioFamily::AnanP2;
    if (t == QStringLiteral("brick_p2")) return RadioFamily::BrickP2;
    return RadioFamily::Unknown;
}


QString rigGroup(const QString &rigId) {
    return QStringLiteral("%1/%2").arg(QLatin1String(kGroup), rigId);
}

} // namespace

namespace registry {

QString activeRigId() {
    return QSettings().value(QLatin1String(kActive)).toString();
}

void setActiveRigId(const QString &rigId) {
    QSettings().setValue(QLatin1String(kActive), rigId);
}

RadioFamily familyForBoardName(const QString &boardName) {
    // Only HL2/ANAN-P1 ship today; anything else (or empty) is treated as
    // HL2 — the only hardware in the field — so the discovery→rig hook and
    // the legacy seed can't misfile a real user's radio.
    if (boardName.startsWith(QStringLiteral("HermesLite")))
        return RadioFamily::Hl2;
    if (boardName.startsWith(QStringLiteral("Orion")))
        return RadioFamily::AnanP1;
    return RadioFamily::Hl2;
}

QString rigIdForMac(const QString &mac) {
    if (mac.isEmpty()) return QString();
    QString hex = mac.toLower();
    hex.remove(QLatin1Char(':'));
    hex.remove(QLatin1Char('-'));
    return QStringLiteral("rig_") + hex;
}

bool exists(const QString &rigId) {
    if (rigId.isEmpty()) return false;
    QSettings s;
    s.beginGroup(kGroup);
    const bool ok = s.childGroups().contains(rigId);
    s.endGroup();
    return ok;
}

RigProfile rig(const QString &rigId) {
    RigProfile p;
    if (rigId.isEmpty()) return p;
    QSettings s;
    s.beginGroup(rigGroup(rigId));
    if (!s.childKeys().isEmpty()) {
        p.rigId  = rigId;
        p.label  = s.value(QStringLiteral("label")).toString();
        p.mac    = s.value(QStringLiteral("mac")).toString();
        p.family = familyFromToken(s.value(QStringLiteral("family")).toString());
        p.lastIp = s.value(QStringLiteral("lastIp")).toString();
    }
    s.endGroup();
    return p;
}

QList<RigProfile> rigs() {
    QList<RigProfile> out;
    QSettings s;
    s.beginGroup(kGroup);
    const QStringList ids = s.childGroups();
    s.endGroup();
    out.reserve(ids.size());
    for (const QString &id : ids)
        out.append(rig(id));
    return out;
}

void upsertRig(const RigProfile &p) {
    if (p.rigId.isEmpty()) return;
    QSettings s;
    s.beginGroup(rigGroup(p.rigId));
    s.setValue(QStringLiteral("label"),  p.label);
    s.setValue(QStringLiteral("mac"),    p.mac);
    s.setValue(QStringLiteral("family"), familyToken(p.family));
    s.setValue(QStringLiteral("lastIp"), p.lastIp);
    s.endGroup();
}

void removeRig(const QString &rigId) {
    if (rigId.isEmpty()) return;
    QSettings s;
    s.beginGroup(rigGroup(rigId));
    s.remove(QString());   // clears just this rig's identity subgroup
    s.endGroup();
    if (activeRigId() == rigId)
        s.remove(QLatin1String(kActive));
}

QString ensureRig(const QString &mac, RadioFamily family,
                  const QString &label, const QString &lastIp) {
    const QString rigId = rigIdForMac(mac);
    if (rigId.isEmpty()) return QString();

    RigProfile p = rig(rigId);           // existing record if any
    p.rigId = rigId;
    p.mac   = mac;
    if (family != RadioFamily::Unknown) p.family = family;
    if (!label.isEmpty())               p.label  = label;
    if (!lastIp.isEmpty())              p.lastIp = lastIp;
    // Fill a friendly default label the first time we meet this rig.
    if (p.label.isEmpty())
        p.label = capabilitiesFor(p.family).familyName;
    upsertRig(p);
    return rigId;
}

QString seedFromLegacyRadio() {
    // Idempotent: once any rig exists, the registry is already seeded.
    if (!rigs().isEmpty())
        return activeRigId();

    QSettings s;
    s.beginGroup(QStringLiteral("lastRadio"));
    const QString mac       = s.value(QStringLiteral("mac")).toString();
    const QString boardName = s.value(QStringLiteral("boardName")).toString();
    s.endGroup();
    if (mac.isEmpty())
        return QString();   // nothing remembered — no seed to do

    const RadioFamily family = familyForBoardName(boardName);
    const QString lastIp     = QSettings().value(QStringLiteral("radio/lastIp"))
                                   .toString();
    const QString label      = capabilitiesFor(family).familyName;

    const QString rigId = ensureRig(mac, family, label, lastIp);
    if (!rigId.isEmpty())
        setActiveRigId(rigId);
    return rigId;
}

} // namespace registry
} // namespace lyra::rig

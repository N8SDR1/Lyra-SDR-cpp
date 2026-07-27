// Lyra — RigRegistry implementation.  See the header.
//
// Stage 2: identity + registry + legacy seed, all ADDITIVE and INERT.
// The family enum is persisted as a STABLE STRING TOKEN (not the raw enum
// int) so re-ordering RadioFamily never mis-reads an operator's stored
// rig.  No config relocation happens here — that is Stage 3.

#include "RigRegistry.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

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

RadioFamily familyForDiscovery(int protocol, const QString &boardName) {
    if (protocol == 2) {
        // P2 board table (hl2_discovery boardNameP2).  10/11 = Saturn /
        // SaturnMKII ⇒ AnanP2.  HermesII/Angelia/Orion/OrionMKII are
        // real ANAN-class hardware — HardwareCatalog lists every one of
        // them as P2-capable (WireSupport::Both) — genuinely speaking
        // P2, NOT the Brick; lumping them into BrickP2 mislabeled real
        // ANAN gear and pointed it at Brick's capability baseline
        // (bench finding 2026-07-20).  "Hermes" (board 1) is the one
        // id BrickSDR2 is bench-confirmed to report (N8SDR's unit, MAC
        // 00:1c:c0:a2:22:5c) — it's ALSO what a real ANAN-10/100/100B
        // would report, an ambiguity nothing in the P2 discovery reply
        // resolves; Brick is the only case anyone has bench-verified,
        // so it stays the default for that one id.  Anything else
        // (Atlas, HermesLite, or an unrecognized id) ⇒ Unknown rather
        // than guessing — none of those has a P2-capable catalog entry.
        if (boardName.startsWith(QStringLiteral("Saturn")))
            return RadioFamily::AnanP2;
        if (boardName == QStringLiteral("HermesII")  ||
            boardName == QStringLiteral("Angelia")   ||
            boardName == QStringLiteral("Orion")     ||
            boardName == QStringLiteral("OrionMKII"))
            return RadioFamily::AnanP2;
        if (boardName == QStringLiteral("Hermes"))
            return RadioFamily::BrickP2;
        return RadioFamily::Unknown;
    }
    return familyForBoardName(boardName);   // protocol 1 (or unset)
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
        p.hardwareModelKey = s.value(QStringLiteral("hardwareModelKey")).toString();
        p.trxAntenna = qBound(1, s.value(QStringLiteral("trxAntenna"), 1).toInt(), 3);
        p.audioRoute = s.value(QStringLiteral("audioRoute")).toString();
        p.firstSeen  = s.value(QStringLiteral("firstSeen")).toString();
        p.lastSeen   = s.value(QStringLiteral("lastSeen")).toString();
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
    s.setValue(QStringLiteral("hardwareModelKey"), p.hardwareModelKey);
    s.setValue(QStringLiteral("trxAntenna"), qBound(1, p.trxAntenna, 3));
    s.setValue(QStringLiteral("audioRoute"), p.audioRoute);
    s.setValue(QStringLiteral("firstSeen"), p.firstSeen);
    s.setValue(QStringLiteral("lastSeen"),  p.lastSeen);
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
    const bool isNew = p.rigId.isEmpty();
    p.rigId = rigId;
    p.mac   = mac;
    if (family != RadioFamily::Unknown) p.family = family;
    if (!label.isEmpty())               p.label  = label;
    if (!lastIp.isEmpty())              p.lastIp = lastIp;
    // Fill a friendly default label the first time we meet this rig.
    if (p.label.isEmpty())
        p.label = capabilitiesFor(p.family).familyName;
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    if (isNew || p.firstSeen.isEmpty()) p.firstSeen = now;
    p.lastSeen = now;
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

int migrateLegacyRadioProfiles() {
    const QString dir = QStandardPaths::writableLocation(
                            QStandardPaths::AppDataLocation)
                         + QStringLiteral("/profiles/radios");
    QDir d(dir);
    if (!d.exists()) return 0;

    int found = 0;
    const QStringList files =
        d.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &name : files) {
        QFile f(d.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isObject()) continue;
        const QJsonObject o = doc.object();
        const QString mac = o.value(QStringLiteral("mac")).toString();
        if (mac.isEmpty()) continue;
        ++found;

        const QString rigId = rigIdForMac(mac);
        RigProfile p = rig(rigId);
        const bool isNew = p.rigId.isEmpty();
        if (isNew) {
            p.rigId = rigId;
            p.mac   = mac;
            // The old store didn't record family/board — infer a safe
            // default: protocol 1 → Hl2 (the only P1 hardware shipping
            // when that store existed); protocol 2 → only claim AnanP2
            // when the saved model key confirms Saturn, else leave
            // Unknown for discovery/Settings to resolve later (same
            // "don't guess Brick" reasoning as familyForDiscovery).
            const int protocol = o.value(QStringLiteral("protocol")).toInt(1);
            const QString hwKey =
                o.value(QStringLiteral("hardwareModelKey")).toString();
            if (protocol == 2) {
                if (hwKey.compare(QStringLiteral("ANAN-G2"), Qt::CaseInsensitive) == 0 ||
                    hwKey.compare(QStringLiteral("ANAN-G2-1K"), Qt::CaseInsensitive) == 0)
                    p.family = RadioFamily::AnanP2;
            } else {
                p.family = RadioFamily::Hl2;
            }
        }
        if (p.label.isEmpty()) {
            const QString nickname = o.value(QStringLiteral("nickname")).toString();
            p.label = !nickname.isEmpty() ? nickname
                                          : capabilitiesFor(p.family).familyName;
        }
        if (p.lastIp.isEmpty())
            p.lastIp = o.value(QStringLiteral("lastKnownIp")).toString();
        if (p.hardwareModelKey.isEmpty())
            p.hardwareModelKey =
                o.value(QStringLiteral("hardwareModelKey")).toString();
        if (p.audioRoute.isEmpty())
            p.audioRoute = o.value(QStringLiteral("audioRoute")).toString();
        // trxAntenna has no "unset" value distinct from its 1 default —
        // only take the saved one when this rig is brand new, so an
        // existing registry entry's antenna choice is never clobbered.
        if (isNew)
            p.trxAntenna = qBound(
                1, o.value(QStringLiteral("trxAntenna")).toInt(1), 3);
        if (p.firstSeen.isEmpty())
            p.firstSeen = o.value(QStringLiteral("firstSeen")).toString();
        const QString lastSeen = o.value(QStringLiteral("lastSeen")).toString();
        if (p.lastSeen.isEmpty() ||
            (!lastSeen.isEmpty() && lastSeen > p.lastSeen))
            p.lastSeen = lastSeen;
        upsertRig(p);
    }
    return found;
}

} // namespace registry
} // namespace lyra::rig

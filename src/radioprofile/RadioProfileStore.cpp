// Lyra — RadioProfileStore implementation.  See RadioProfileStore.h.

#include "radioprofile/RadioProfileStore.h"

#include "hardware/HardwareCatalog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace lyra::radioprofile {

QJsonObject RadioProfile::toJson() const {
    QJsonObject o;
    o[QStringLiteral("schemaVersion")]    = schemaVersion;
    o[QStringLiteral("mac")]              = mac;
    o[QStringLiteral("nickname")]         = nickname;
    o[QStringLiteral("lastKnownIp")]      = lastKnownIp;
    o[QStringLiteral("protocol")]         = protocol;
    o[QStringLiteral("hardwareModelKey")] = hardwareModelKey;
    o[QStringLiteral("trxAntenna")]       = trxAntenna;
    o[QStringLiteral("audioRoute")]       = audioRoute;
    o[QStringLiteral("firstSeen")]        = firstSeen;
    o[QStringLiteral("lastSeen")]         = lastSeen;
    return o;
}

RadioProfile RadioProfile::fromJson(const QJsonObject &o) {
    RadioProfile p;
    p.schemaVersion    = o.value(QStringLiteral("schemaVersion")).toInt(1);
    p.mac              = o.value(QStringLiteral("mac")).toString();
    p.nickname         = o.value(QStringLiteral("nickname")).toString();
    p.lastKnownIp      = o.value(QStringLiteral("lastKnownIp")).toString();
    p.protocol         = o.value(QStringLiteral("protocol")).toInt(1);
    p.hardwareModelKey = o.value(QStringLiteral("hardwareModelKey")).toString();
    p.trxAntenna       = qBound(1,
        o.value(QStringLiteral("trxAntenna")).toInt(1), 3);
    p.audioRoute       = o.value(QStringLiteral("audioRoute")).toString();
    p.firstSeen        = o.value(QStringLiteral("firstSeen")).toString();
    p.lastSeen         = o.value(QStringLiteral("lastSeen")).toString();
    return p;
}

RadioProfileStore &RadioProfileStore::instance() {
    static RadioProfileStore s;
    return s;
}

QString RadioProfileStore::dir() const {
    return QStandardPaths::writableLocation(
               QStandardPaths::AppDataLocation)
           + QStringLiteral("/profiles/radios");
}

QString RadioProfileStore::pathForMac(const QString &mac) const {
    QString name = mac.toUpper();
    name.replace(QLatin1Char(':'), QLatin1Char('-'));
    return dir() + QLatin1Char('/') + name + QStringLiteral(".json");
}

RadioProfile RadioProfileStore::forMac(const QString &mac) const {
    if (mac.isEmpty()) return {};
    QFile f(pathForMac(mac));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return {};
    return RadioProfile::fromJson(doc.object());
}

void RadioProfileStore::save(const RadioProfile &p) {
    if (!p.isValid()) return;
    QDir().mkpath(dir());
    QSaveFile f(pathForMac(p.mac));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(p.toJson()).toJson(QJsonDocument::Indented));
    f.commit();
}

RadioProfile RadioProfileStore::touch(const QString &mac, int protocol,
                                      const QString &ip) {
    if (mac.isEmpty()) return {};
    RadioProfile p = forMac(mac);
    const QString now =
        QDateTime::currentDateTime().toString(Qt::ISODate);
    if (!p.isValid()) {
        p.mac       = mac.toUpper();
        p.firstSeen = now;
        const auto *hw = lyra::hardware::defaultModelForBoard(
            -1, protocol == 2);
        if (hw) {
            p.hardwareModelKey = QLatin1String(hw->key);
            p.nickname         = QLatin1String(hw->displayName);
        }
        // P2 radios can't use the HL2-jack audio path — default them
        // to PC output for the session (see RadioProfile.h).
        if (protocol == 2) p.audioRoute = QStringLiteral("pc");
    }
    // One-time upgrade for profiles created before the audioRoute
    // field existed: P2 radios default to PC output.
    if (protocol == 2 && p.audioRoute.isEmpty())
        p.audioRoute = QStringLiteral("pc");
    p.protocol = protocol;
    if (!ip.isEmpty()) p.lastKnownIp = ip;
    p.lastSeen = now;
    save(p);
    return p;
}

void RadioProfileStore::remove(const QString &mac) {
    if (mac.isEmpty()) return;
    QFile::remove(pathForMac(mac));
}

QList<RadioProfile> RadioProfileStore::all() const {
    QList<RadioProfile> out;
    const QDir d(dir());
    for (const QString &name :
         d.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile f(d.filePath(name));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) continue;
        const RadioProfile p = RadioProfile::fromJson(doc.object());
        if (p.isValid()) out.append(p);
    }
    return out;
}

} // namespace lyra::radioprofile

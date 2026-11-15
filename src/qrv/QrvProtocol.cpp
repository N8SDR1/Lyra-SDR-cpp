#include "qrv/QrvProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace lyra::qrv {
namespace {

struct BandRange {
    const char *name;
    qint64 lo;
    qint64 hi;
};

// Same names and Hz edges as qrv-service/include/validate.php QRV_BANDS.
// GEN is last so a ham band wins on overlap.
constexpr BandRange kBands[] = {
    {"160m", 1800000, 2000000},
    {"80m", 3500000, 4000000},
    {"60m", 5330000, 5407000},
    {"40m", 7000000, 7300000},
    {"30m", 10100000, 10150000},
    {"20m", 14000000, 14350000},
    {"17m", 18068000, 18168000},
    {"15m", 21000000, 21450000},
    {"12m", 24890000, 24990000},
    {"10m", 28000000, 29700000},
    {"6m", 50000000, 54000000},
    {"2m", 144000000, 148000000},
    {"70cm", 420000000, 450000000},
    {"GEN", 1, 54000000},
};

constexpr const char *kModes[] = {
    "LSB", "USB", "CWL", "CWU", "DSB", "AM", "SAM", "FM",
    "DIGU", "DIGL", "SPEC", "DRM",
};

constexpr const char *kApps[] = {"Lyra", "Logger", "STD"};

QString jsonError(const QJsonObject &o) {
    const QJsonValue e = o.value(QStringLiteral("error"));
    if (e.isString())
        return e.toString();
    return QString();
}

} // namespace

QString normalizeCall(const QString &raw) {
    QString s = raw.trimmed().toUpper();
    s.remove(QLatin1Char(' '));
    if (s.isEmpty() || s.size() > 16)
        return {};
    static const QRegularExpression re(
        QStringLiteral("^[A-Z0-9]{3,11}(/[A-Z0-9]{1,5})?$"));
    if (!re.match(s).hasMatch())
        return {};
    const QString base = s.section(QLatin1Char('/'), 0, 0);
    bool hasLetter = false;
    bool hasDigit = false;
    for (QChar c : base) {
        if (c.isLetter())
            hasLetter = true;
        if (c.isDigit())
            hasDigit = true;
    }
    if (!hasLetter || !hasDigit)
        return {};
    return s;
}

QString normalizeMode(const QString &raw) {
    const QString s = raw.trimmed().toUpper();
    for (const char *m : kModes) {
        if (s == QLatin1String(m))
            return s;
    }
    return {};
}

QString normalizeApp(const QString &raw) {
    const QString s = raw.trimmed();
    for (const char *a : kApps) {
        if (s.compare(QLatin1String(a), Qt::CaseInsensitive) == 0)
            return QString::fromLatin1(a);
    }
    return {};
}

QString sanitizeNote(const QString &raw) {
    if (raw.isEmpty())
        return {};
    QString s = raw.trimmed();
    QString cleaned;
    cleaned.reserve(s.size());
    for (QChar c : s) {
        const auto u = c.unicode();
        if (u < 0x20 || u == 0x7F)
            continue;
        if (c == QLatin1Char('`') || c == QLatin1Char('*') || c == QLatin1Char('_')
            || c == QLatin1Char('|') || c == QLatin1Char('<')
            || c == QLatin1Char('>'))
            continue;
        cleaned.append(c);
    }
    s = cleaned;
    static const QRegularExpression url(QStringLiteral("https?://\\S+"),
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression www(QStringLiteral("www\\.\\S+"),
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression at(QStringLiteral("@[\\w.]+"));
    s.replace(url, QString());
    s.replace(www, QString());
    s.replace(at, QString());
    s = s.trimmed();
    if (s.isEmpty())
        return {};
    if (s.size() > kNoteMaxChars)
        s = s.left(kNoteMaxChars);
    return s;
}

QString bandForHz(qint64 hz) {
    if (hz <= 0)
        return {};
    for (const BandRange &b : kBands) {
        if (QLatin1String(b.name) == QLatin1String("GEN"))
            continue;
        if (hz >= b.lo && hz <= b.hi)
            return QString::fromLatin1(b.name);
    }
    for (const BandRange &b : kBands) {
        if (QLatin1String(b.name) != QLatin1String("GEN"))
            continue;
        if (hz >= b.lo && hz <= b.hi)
            return QStringLiteral("GEN");
    }
    return {};
}

bool hzInBand(qint64 hz, const QString &band) {
    if (hz <= 0)
        return false;
    for (const BandRange &b : kBands) {
        if (band.compare(QLatin1String(b.name), Qt::CaseInsensitive) == 0)
            return hz >= b.lo && hz <= b.hi;
    }
    return false;
}

bool intervalElapsed(qint64 lastMs, qint64 nowMs, int minMs) {
    if (lastMs <= 0)
        return true;
    return (nowMs - lastMs) >= minMs;
}

QString normalizeApiBase(QString raw) {
    QString s = raw.trimmed();
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    if (s.endsWith(QLatin1String("/v1"), Qt::CaseInsensitive))
        s.chop(3);
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    if (s.isEmpty())
        s = QString::fromUtf8(kDefaultApiBase);
    return s;
}

QString endpointUrl(const QString &apiBase, const QString &phpFile) {
    return normalizeApiBase(apiBase) + QStringLiteral("/v1/") + phpFile;
}

QString truncatedKey(const QString &key) {
    const QString k = key.trimmed();
    if (k.isEmpty())
        return {};
    if (k.size() <= 8)
        return k;
    return k.left(8) + QStringLiteral("...");
}

QByteArray encodeHeartbeat(const HeartbeatRequest &req) {
    QJsonObject o;
    o.insert(QStringLiteral("call"), req.call);
    o.insert(QStringLiteral("app"), req.app);
    o.insert(QStringLiteral("band"), req.band);
    o.insert(QStringLiteral("mode"), req.mode);
    if (!req.installKey.isEmpty())
        o.insert(QStringLiteral("install_key"), req.installKey);
    if (req.hasHz)
        o.insert(QStringLiteral("hz"), req.hz);
    if (!req.note.isEmpty())
        o.insert(QStringLiteral("note"), req.note);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray encodeQrt(const QrtRequest &req) {
    QJsonObject o;
    o.insert(QStringLiteral("call"), req.call);
    o.insert(QStringLiteral("app"), req.app);
    o.insert(QStringLiteral("install_key"), req.installKey);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

ApiResult parseWriteResponse(const QByteArray &body, int httpStatus) {
    ApiResult r;
    r.httpStatus = httpStatus;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        r.ok = false;
        r.error = httpStatus > 0
                      ? QStringLiteral("http_%1").arg(httpStatus)
                      : QStringLiteral("invalid_json");
        return r;
    }
    const QJsonObject o = doc.object();
    r.ok = o.value(QStringLiteral("ok")).toBool(false);
    r.error = jsonError(o);
    r.installKey = o.value(QStringLiteral("install_key")).toString();
    if (o.contains(QStringLiteral("stale_s")))
        r.staleS = o.value(QStringLiteral("stale_s")).toInt(180);
    if (!r.ok && r.error.isEmpty())
        r.error = QStringLiteral("http_%1").arg(httpStatus);
    return r;
}

Roster parseRosterResponse(const QByteArray &body, int httpStatus) {
    Roster r;
    r.httpStatus = httpStatus;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        r.ok = false;
        r.error = httpStatus > 0
                      ? QStringLiteral("http_%1").arg(httpStatus)
                      : QStringLiteral("invalid_json");
        return r;
    }
    const QJsonObject o = doc.object();
    r.ok = o.value(QStringLiteral("ok")).toBool(false);
    r.error = jsonError(o);
    if (o.contains(QStringLiteral("stale_s")))
        r.staleS = o.value(QStringLiteral("stale_s")).toInt(180);
    const QJsonArray arr = o.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject e = v.toObject();
        RosterEntry row;
        row.call = e.value(QStringLiteral("call")).toString();
        row.app = e.value(QStringLiteral("app")).toString();
        row.band = e.value(QStringLiteral("band")).toString();
        row.mode = e.value(QStringLiteral("mode")).toString();
        row.lastSeen = e.value(QStringLiteral("last_seen")).toString();
        row.note = e.value(QStringLiteral("note")).toString();
        if (e.contains(QStringLiteral("hz")) && e.value(QStringLiteral("hz")).isDouble()) {
            row.hasHz = true;
            row.hz = static_cast<qint64>(e.value(QStringLiteral("hz")).toDouble());
        }
        r.entries.append(row);
    }
    if (!r.ok && r.error.isEmpty())
        r.error = QStringLiteral("http_%1").arg(httpStatus);
    return r;
}

} // namespace lyra::qrv

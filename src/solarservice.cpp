// Lyra — solar / HF-propagation service.  See solarservice.h.

#include "solarservice.h"

#include "prefs.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QTimer>
#include <QXmlStreamReader>

#include <cmath>

namespace lyra::solar {

namespace {
constexpr double kPi = 3.14159265358979323846;
inline double deg2rad(double d) { return d * kPi / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / kPi; }
} // namespace

SolarService::SolarService(lyra::ui::Prefs *prefs, QObject *parent)
    : QObject(parent), prefs_(prefs),
      nam_(new QNetworkAccessManager(this)),
      timer_(new QTimer(this)) {
    recomputeLocation();
    if (prefs_) {
        connect(prefs_, &lyra::ui::Prefs::locationChanged, this, [this]() {
            recomputeLocation();
            refresh();   // day/night may now differ (or first location set)
        });
    }
    // 60 s tick: cheap day/night recompute (catches sunrise/sunset); the
    // network feed only re-fetches on its own 60-min cache TTL.
    timer_->setInterval(60 * 1000);
    connect(timer_, &QTimer::timeout, this, &SolarService::refresh);
    timer_->start();
    // Kick the first fetch shortly after construction so the panel's QML/
    // widgets are live to receive dataChanged.
    QTimer::singleShot(1500, this, &SolarService::refresh);
}

QStringList SolarService::bands() {
    return { QStringLiteral("80"), QStringLiteral("40"),
             QStringLiteral("30"), QStringLiteral("20"), QStringLiteral("17"),
             QStringLiteral("15"), QStringLiteral("12"), QStringLiteral("10") };
}

QString SolarService::groupForBand(const QString &band) {
    if (band == QLatin1String("80") || band == QLatin1String("40"))
        return QStringLiteral("80m-40m");
    if (band == QLatin1String("30") || band == QLatin1String("20"))
        return QStringLiteral("30m-20m");
    if (band == QLatin1String("17") || band == QLatin1String("15"))
        return QStringLiteral("17m-15m");
    if (band == QLatin1String("12") || band == QLatin1String("10"))
        return QStringLiteral("12m-10m");
    return QString();   // 160 m + 6 m: HamQSL doesn't predict these
}

Rating SolarService::ratingForBand(const QString &band) const {
    const QString grp = groupForBand(band);
    if (grp.isEmpty() || !data_.valid) return Rating::Unknown;
    const QHash<QString, QString> &t = isDaylight() ? data_.bandDay
                                                    : data_.bandNight;
    const QString r = t.value(grp).trimmed().toLower();
    if (r == QLatin1String("good")) return Rating::Good;
    if (r == QLatin1String("fair")) return Rating::Fair;
    if (r == QLatin1String("poor")) return Rating::Poor;
    return Rating::Unknown;
}

// Simplified NOAA sunrise/sunset day-test (port of old Lyra is_daylight).
bool SolarService::isDaylight() const {
    if (!haveLoc_) return true;   // unknown location → assume day
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const int yday = now.date().dayOfYear();
    const double decl =
        deg2rad(23.44) * std::sin(deg2rad(360.0 / 365.0 * (284 + yday)));
    const double cosH = -std::tan(deg2rad(lat_)) * std::tan(decl);
    if (cosH > 1.0)  return false;   // polar night
    if (cosH < -1.0) return true;    // polar day
    const double hDeg = rad2deg(std::acos(cosH));
    const double solarNoon = 12.0 - lon_ / 15.0;            // UTC hours
    double sunrise = std::fmod(solarNoon - hDeg / 15.0, 24.0);
    double sunset  = std::fmod(solarNoon + hDeg / 15.0, 24.0);
    if (sunrise < 0) sunrise += 24.0;
    if (sunset  < 0) sunset  += 24.0;
    const QTime t = now.time();
    const double cur = t.hour() + t.minute() / 60.0 + t.second() / 3600.0;
    if (sunrise < sunset) return cur >= sunrise && cur <= sunset;
    return cur >= sunrise || cur <= sunset;   // wraps midnight UTC
}

QString SolarService::ratingColor(Rating r) {
    switch (r) {
    case Rating::Good: return QStringLiteral("#4caf50");
    case Rating::Fair: return QStringLiteral("#f0c040");
    case Rating::Poor: return QStringLiteral("#e05c5c");
    default:           return QStringLiteral("#5a6573");
    }
}

QString SolarService::sfiColor(const QString &sfi) {
    bool ok = false;
    const double v = sfi.trimmed().toDouble(&ok);
    if (!ok) return QStringLiteral("#cdd9e5");
    if (v >= 100) return QStringLiteral("#4caf50");
    if (v >= 80)  return QStringLiteral("#f0c040");
    return QStringLiteral("#e05c5c");
}

QString SolarService::aIndexColor(const QString &a) {
    bool ok = false;
    const double v = a.trimmed().toDouble(&ok);
    if (!ok) return QStringLiteral("#cdd9e5");
    if (v <= 7)  return QStringLiteral("#4caf50");
    if (v <= 19) return QStringLiteral("#f0c040");
    return QStringLiteral("#e05c5c");
}

QString SolarService::kIndexColor(const QString &k) {
    bool ok = false;
    const double v = k.trimmed().toDouble(&ok);
    if (!ok) return QStringLiteral("#cdd9e5");
    if (v <= 2) return QStringLiteral("#4caf50");
    if (v == 3) return QStringLiteral("#f0c040");
    return QStringLiteral("#e05c5c");
}

void SolarService::recomputeLocation() {
    haveLoc_ = prefs_ && prefs_->operatorLocation(&lat_, &lon_);
}

void SolarService::refresh() {
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool stale = (lastFetchMs_ == 0) ||
                       (nowMs - lastFetchMs_ >= kCacheTtlMs);
    if (stale && !fetching_) {
        fetch();
    } else {
        // Cache still fresh — just recompute day/night (ratings may flip)
        // and let the panel repaint.
        emit dataChanged();
    }
}

void SolarService::fetch() {
    fetching_ = true;
    QNetworkRequest req{QUrl(QStringLiteral("https://www.hamqsl.com/solarxml.php"))};
    req.setRawHeader("User-Agent", "Lyra-SDR (Hermes Lite 2 client)");
    req.setTransferTimeout(12000);
    QNetworkReply *reply = nam_->get(req);
    // HamQSL's intermediate-CA chain is rejected by some stores; this is
    // a read-only public feed, so ignore TLS errors (old Lyra's posture).
    connect(reply, &QNetworkReply::sslErrors, reply,
            [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        fetching_ = false;
        if (reply->error() != QNetworkReply::NoError) {
            data_.error = reply->errorString();   // keep last good data
            emit dataChanged();
            return;
        }
        const QByteArray body = reply->readAll();
        SolarData d;
        QXmlStreamReader xml(body);
        QString curBandName, curBandTime;
        while (!xml.atEnd()) {
            const auto tok = xml.readNext();
            if (tok == QXmlStreamReader::StartElement) {
                const QStringView name = xml.name();
                if (name == QLatin1String("band")) {
                    const auto a = xml.attributes();
                    curBandName = a.value(QLatin1String("name")).toString();
                    curBandTime = a.value(QLatin1String("time"))
                                      .toString().toLower();
                    const QString txt = xml.readElementText().trimmed();
                    if (curBandTime == QLatin1String("day"))
                        d.bandDay.insert(curBandName, txt);
                    else if (curBandTime == QLatin1String("night"))
                        d.bandNight.insert(curBandName, txt);
                } else if (name == QLatin1String("solarflux")) {
                    d.sfi = xml.readElementText().trimmed();
                } else if (name == QLatin1String("sunspots")) {
                    d.sunspots = xml.readElementText().trimmed();
                } else if (name == QLatin1String("aindex")) {
                    d.aindex = xml.readElementText().trimmed();
                } else if (name == QLatin1String("kindex")) {
                    d.kindex = xml.readElementText().trimmed();
                } else if (name == QLatin1String("xray")) {
                    d.xray = xml.readElementText().trimmed();
                } else if (name == QLatin1String("solarwind")) {
                    d.solarwind = xml.readElementText().trimmed();
                } else if (name == QLatin1String("updated")) {
                    d.updated = xml.readElementText().trimmed();
                }
            }
        }
        d.valid = !xml.hasError() &&
                  (!d.sfi.isEmpty() || !d.bandDay.isEmpty());
        if (d.valid) {
            d.error.clear();
            data_ = d;
            lastFetchMs_ = QDateTime::currentMSecsSinceEpoch();
        } else {
            data_.error = tr("HamQSL feed could not be parsed");
        }
        emit dataChanged();
    });
}

} // namespace lyra::solar

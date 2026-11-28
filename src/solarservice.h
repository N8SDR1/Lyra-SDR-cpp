// Lyra — solar / HF-propagation service (ported from old Lyra's
// propagation.py).
//
// Fetches the HamQSL solar XML feed (SFI / sunspots / A-index / K-index
// / X-ray / solar wind + per-band day/night condition ratings), caches
// it for 60 minutes (HamQSL's hourly cadence), and derives a per-band
// Good/Fair/Poor rating for the operator's day-or-night using a
// simplified NOAA sunrise/sunset calculation against their location
// (grid or manual lat/lon from Prefs).  Native C++/Qt: async
// QNetworkAccessManager GET, no worker thread, no Python.
//
// The data feeds the dockable PROP panel (SolarPanel).  Location is the
// ONLY operator input it consumes (callsign/region are not used here),
// matching old Lyra.

#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTimer;

namespace lyra::ui { class Prefs; }

namespace lyra::solar {

enum class Rating { Unknown, Good, Fair, Poor };

// Parsed snapshot of the HamQSL feed.  Numeric fields are kept as the
// raw strings the feed provides ("" / "N/A" / "?" rendered as a dash).
struct SolarData {
    bool    valid = false;
    QString sfi, sunspots, aindex, kindex, xray, solarwind, updated;
    // HamQSL <calculatedconditions> keyed by group name ("80m-40m",
    // "30m-20m", "17m-15m", "12m-10m") → "Good"/"Fair"/"Poor".
    QHash<QString, QString> bandDay;
    QString error;            // non-empty if the last fetch failed (stale shown)
    QHash<QString, QString> bandNight;
};

class SolarService : public QObject {
    Q_OBJECT
public:
    explicit SolarService(lyra::ui::Prefs *prefs, QObject *parent = nullptr);

    const SolarData &data() const { return data_; }

    // HamQSL-predicted HF bands the PROP ratings strip shows, low→high
    // (80–10 m).  160 m and 6 m have no HamQSL prediction and are omitted.
    static QStringList bands();
    // Rating for a band at the operator's current day/night.  160 m and
    // 6 m are not predicted by HamQSL → always Unknown if asked.
    Rating ratingForBand(const QString &band) const;
    // True if it is currently daylight at the operator's location.  When
    // the location is unknown it defaults to true (day), matching old Lyra.
    bool isDaylight() const;
    bool haveLocation() const { return haveLoc_; }

    // Colour helpers (hex), matching old Lyra's palette.
    static QString ratingColor(Rating r);
    static QString sfiColor(const QString &sfi);
    static QString aIndexColor(const QString &a);
    static QString kIndexColor(const QString &k);

    // Force an immediate refresh (re-fetch if the cache is stale, else
    // just recompute day/night and re-emit).
    Q_INVOKABLE void refresh();

signals:
    void dataChanged();

private:
    void fetch();
    void recomputeLocation();
    static QString groupForBand(const QString &band);

    lyra::ui::Prefs       *prefs_ = nullptr;
    QNetworkAccessManager *nam_   = nullptr;
    QTimer                *timer_ = nullptr;   // 60 s: catch sunrise/sunset flips
    SolarData              data_;
    qint64                 lastFetchMs_ = 0;   // monotonic; 0 = never
    bool                   fetching_    = false;
    bool                   haveLoc_     = false;
    double                 lat_ = 0.0, lon_ = 0.0;

    // HamQSL updates about hourly; poll TTL matches that cadence.
    static constexpr qint64 kCacheTtlMs = 60 * 60 * 1000;
};

} // namespace lyra::solar

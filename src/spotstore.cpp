// Lyra — DX-cluster spot store.  See spotstore.h.

#include "spotstore.h"

#include "hl2_stream.h"
#include "prefs.h"
#include "wdsp_engine.h"

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>

namespace lyra::ui {

namespace {
constexpr auto kKeyShow = "spots/show";
constexpr auto kKeyMax  = "spots/max";
constexpr auto kKeyLife = "spots/lifetime_sec";
constexpr auto kKeyHlOwn = "spots/highlight_own";
constexpr auto kKeyHlCol = "spots/highlight_color";
constexpr auto kKeyNotify = "spots/notify_own";
constexpr auto kKeyCooldown = "spots/notify_cooldown_min";
constexpr auto kKeyFlashNew = "spots/flash_new";
constexpr auto kKeyFlashCol = "spots/flash_color";
constexpr auto kKeyFlashSec = "spots/flash_sec";
constexpr auto kKeyModeFilt = "spots/mode_filter";
constexpr auto kKeyRegionFilt = "spots/region_filter";
constexpr auto kKeyColorMode = "spots/color_mode";
constexpr auto kKeySingleCol = "spots/single_color";
constexpr auto kKeyLegend    = "spots/legend";
constexpr auto kKeyDispMax   = "spots/display_max";
constexpr auto kKeyBktMax    = "spots/bucket_max";
constexpr auto kKeyBktHz     = "spots/bucket_hz";

bool isContinentCode(const QString &up) {
    static const QStringList kC = {
        QStringLiteral("NA"), QStringLiteral("SA"), QStringLiteral("EU"),
        QStringLiteral("AF"), QStringLiteral("AS"), QStringLiteral("OC"),
        QStringLiteral("AN")};
    return kC.contains(up);
}

// Concrete digital sub-modes that the "DIGI" family token matches.
bool isDigitalMode(const QString &up) {
    static const QStringList kDigi = {
        QStringLiteral("FT8"),  QStringLiteral("FT4"),  QStringLiteral("RTTY"),
        QStringLiteral("PSK"),  QStringLiteral("PSK31"),QStringLiteral("JS8"),
        QStringLiteral("DIGU"), QStringLiteral("DIGL"), QStringLiteral("JT65"),
        QStringLiteral("JT9"),  QStringLiteral("MSK144"),QStringLiteral("Q65"),
        QStringLiteral("FST4"), QStringLiteral("FST4W"),QStringLiteral("DATA"),
        QStringLiteral("DIGI"), QStringLiteral("OLIVIA"),QStringLiteral("VARAC"),
        QStringLiteral("WSPR"), QStringLiteral("HELL"), QStringLiteral("SSTV"),
        QStringLiteral("PACKET"),QStringLiteral("DIG")};
    return kDigi.contains(up);
}

// --- colour palettes (color modes 2/3/4) ---
QString modeColorHex(const QString &mode) {
    const QString up = mode.trimmed().toUpper();
    if (up == QStringLiteral("CW") || up == QStringLiteral("CWU")
        || up == QStringLiteral("CWL")) return QStringLiteral("#29b6f6");      // cyan
    if (up == QStringLiteral("USB") || up == QStringLiteral("LSB")
        || up == QStringLiteral("SSB") || up == QStringLiteral("AM")
        || up == QStringLiteral("SAM") || up == QStringLiteral("DSB")
        || up == QStringLiteral("FM") || up == QStringLiteral("NFM")
        || up == QStringLiteral("WFM")) return QStringLiteral("#66bb6a");      // green (phone)
    if (up == QStringLiteral("RTTY")) return QStringLiteral("#ab47bc");        // purple
    if (isDigitalMode(up)) return QStringLiteral("#ffa726");                   // orange (FT8/dig)
    return QStringLiteral("#ffd54f");                                          // gold (unknown)
}
QString regionColorHex(const QString &cont) {
    const QString c = cont.trimmed().toUpper();
    if (c == QStringLiteral("NA")) return QStringLiteral("#42a5f5");
    if (c == QStringLiteral("SA")) return QStringLiteral("#26a69a");
    if (c == QStringLiteral("EU")) return QStringLiteral("#66bb6a");
    if (c == QStringLiteral("AF")) return QStringLiteral("#ffa726");
    if (c == QStringLiteral("AS")) return QStringLiteral("#ef5350");
    if (c == QStringLiteral("OC")) return QStringLiteral("#ab47bc");
    return QStringLiteral("#bdbdbd");   // unknown / AN
}
QString countryColorHex(const QString &iso) {
    const QString k = iso.trimmed().toUpper();
    if (k.isEmpty()) return QStringLiteral("#bdbdbd");
    const int hue = int(qHash(k) % 360u);     // deterministic, distinct per country
    return QColor::fromHsv(hue, 150, 230).name();
}
}

SpotStore::SpotStore(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                     lyra::dsp::WdspEngine *engine, QObject *parent)
    : QObject(parent), prefs_(prefs), stream_(stream), engine_(engine) {
    QSettings s;
    show_        = s.value(QString::fromLatin1(kKeyShow), true).toBool();
    max_         = s.value(QString::fromLatin1(kKeyMax), 200).toInt();
    lifetimeSec_ = s.value(QString::fromLatin1(kKeyLife), 0).toInt();
    highlightOwn_   = s.value(QString::fromLatin1(kKeyHlOwn), true).toBool();
    highlightColor_ = s.value(QString::fromLatin1(kKeyHlCol),
                              QStringLiteral("#ff4081")).toString();
    notifyOwn_         = s.value(QString::fromLatin1(kKeyNotify), false).toBool();
    notifyCooldownMin_ = s.value(QString::fromLatin1(kKeyCooldown), 10).toInt();
    flashNew_   = s.value(QString::fromLatin1(kKeyFlashNew), false).toBool();
    flashColor_ = s.value(QString::fromLatin1(kKeyFlashCol),
                          QStringLiteral("#ffeb3b")).toString();
    flashSec_   = qBound(1, s.value(QString::fromLatin1(kKeyFlashSec), 8).toInt(), 60);
    setModeFilter(s.value(QString::fromLatin1(kKeyModeFilt), QString()).toString());
    setRegionFilter(s.value(QString::fromLatin1(kKeyRegionFilt), QString()).toString());
    colorMode_   = qBound(0, s.value(QString::fromLatin1(kKeyColorMode), 0).toInt(), 4);
    singleColor_ = s.value(QString::fromLatin1(kKeySingleCol),
                           QStringLiteral("#66bb6a")).toString();
    legendOn_    = s.value(QString::fromLatin1(kKeyLegend), false).toBool();
    displayMax_  = qBound(0, s.value(QString::fromLatin1(kKeyDispMax), 25).toInt(), 1000);
    bucketMax_   = qBound(0, s.value(QString::fromLatin1(kKeyBktMax), 3).toInt(), 100);
    bucketHz_    = qBound(50, s.value(QString::fromLatin1(kKeyBktHz), 500).toInt(), 20000);
    tStart_ = QDateTime::currentMSecsSinceEpoch();

    ageTimer_ = new QTimer(this);
    ageTimer_->setInterval(5000);
    connect(ageTimer_, &QTimer::timeout, this, &SpotStore::onAgeTick);
    if (lifetimeSec_ > 0) ageTimer_->start();

    // Flash repaint pulse — runs only while a spot is inside the flash
    // window; ~3 Hz is smooth enough for the eye and cheap (spotsInSpan
    // returns only the handful of in-span spots).  Self-stops once the
    // window passes (onFlashTick).
    flashTimer_ = new QTimer(this);
    flashTimer_->setInterval(330);
    connect(flashTimer_, &QTimer::timeout, this, &SpotStore::onFlashTick);
}

int SpotStore::indexOf(const QString &call) const {
    for (int i = 0; i < spots_.size(); ++i)
        if (spots_[i].call.compare(call, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

void SpotStore::addSpot(const QString &call, const QString &mode, qint64 freqHz,
                        quint32 argb, const QString &text,
                        const QString &source, const QString &continent) {
    if (call.trimmed().isEmpty() || freqHz <= 0) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int idx = indexOf(call);
    Spot sp;
    sp.call = call.trimmed();
    sp.mode = mode.trimmed();
    sp.freqHz = freqHz;
    sp.argb = argb ? argb : 0xFFFFD700u;
    sp.text = text.trimmed();
    sp.source = source.isEmpty() ? QStringLiteral("tci") : source.toLower();
    sp.country = dxcc_.isoOf(sp.call);     // DXCC country (ISO-2)
    // Prefer a source-supplied continent (SpotHole gives a precise per-call
    // one); else derive coarsely from the country.
    sp.continent = continent.trimmed().isEmpty()
                   ? DxccLookup::continentOf(sp.country)
                   : continent.trimmed().toUpper();
    sp.added = now;
    const bool isNew = (idx < 0);
    if (isNew) {
        sp.firstSeen = now;             // arrival → eligible to flash once
        spots_.append(sp);
    } else {
        sp.firstSeen = spots_[idx].firstSeen;   // preserve → a re-spot never re-flashes
        spots_[idx] = sp;               // refresh freq/text/added (freshness)
    }
    enforceCap();

    // Toast when the operator's own callsign is spotted — rate-limited by
    // the re-notify cooldown so a busy cluster can't spam.
    if (notifyOwn_ && prefs_) {
        const QString own = prefs_->callsign().trimmed().toUpper();
        if (!own.isEmpty() && sp.call.toUpper() == own) {
            const qint64 gapMs = qint64(qMax(0, notifyCooldownMin_)) * 60000;
            if (now - lastNotify_ >= gapMs) {
                lastNotify_ = now;
                emit ownCallSpotted(sp.call, sp.mode, sp.freqHz, sp.text);
            }
        }
    }
    // #172 — start the flash pulse so the just-added spot decays out of
    // flashColor_ on its own (idempotent: start() on a running timer is
    // a no-op).  Skip self-spots — they already have the own-call ★ +
    // highlight colour, which takes visual priority over a flash.
    if (flashNew_ && isNew && flashTimer_ && !flashTimer_->isActive()) {
        const QString own = prefs_ ? prefs_->callsign().trimmed().toUpper()
                                   : QString();
        if (own.isEmpty() || sp.call.toUpper() != own
            || !(highlightOwn_))
            flashTimer_->start();
    }
    emit changed();
}

void SpotStore::deleteSpot(const QString &call) {
    const int idx = indexOf(call);
    if (idx < 0) return;
    spots_.removeAt(idx);
    emit changed();
}

void SpotStore::clearAll() {
    if (spots_.isEmpty()) return;
    spots_.clear();
    emit changed();
}

void SpotStore::clearSource(const QString &source) {
    const QString tag = source.toLower();
    bool removed = false;
    for (int i = spots_.size() - 1; i >= 0; --i)
        if (spots_[i].source == tag) { spots_.removeAt(i); removed = true; }
    if (removed) emit changed();
}

void SpotStore::enforceCap() {
    if (max_ <= 0) return;
    // Drop oldest (front-most by add time) until within cap.
    while (spots_.size() > max_) {
        int oldest = 0;
        for (int i = 1; i < spots_.size(); ++i)
            if (spots_[i].added < spots_[oldest].added) oldest = i;
        spots_.removeAt(oldest);
    }
}

void SpotStore::onAgeTick() {
    if (lifetimeSec_ <= 0) return;
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch()
                          - qint64(lifetimeSec_) * 1000;
    bool removed = false;
    for (int i = spots_.size() - 1; i >= 0; --i)
        if (spots_[i].added < cutoff) { spots_.removeAt(i); removed = true; }
    if (removed) emit changed();
}

bool SpotStore::anyFlashing(qint64 nowMs) const {
    if (!flashNew_) return false;
    const qint64 flashMs = qint64(flashSec_) * 1000;
    for (const Spot &sp : spots_)
        if (nowMs - sp.firstSeen < flashMs) return true;
    return false;
}

void SpotStore::onFlashTick() {
    // Re-query forces the panadapter to re-read each spot's `flash` flag,
    // so spots whose window just lapsed settle to their normal colour.
    // Stop pulsing once nothing is flashing (idle = zero cost).
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!anyFlashing(now)) { flashTimer_->stop(); return; }
    emit changed();
}

QString SpotStore::lyraModeFor(const QString &tciMode, qint64 freqHz) {
    const QString m = tciMode.trimmed().toUpper();
    const bool high = freqHz >= 10000000;
    if (m == QStringLiteral("CW"))  return high ? QStringLiteral("CWU") : QStringLiteral("CWL");
    if (m == QStringLiteral("SSB")) return high ? QStringLiteral("USB") : QStringLiteral("LSB");
    if (m == QStringLiteral("USB") || m == QStringLiteral("LSB")
        || m == QStringLiteral("CWU") || m == QStringLiteral("CWL")
        || m == QStringLiteral("AM")  || m == QStringLiteral("SAM")
        || m == QStringLiteral("DSB") || m == QStringLiteral("FM")
        || m == QStringLiteral("DIGU") || m == QStringLiteral("DIGL"))
        return m;
    if (m == QStringLiteral("NFM") || m == QStringLiteral("WFM"))
        return QStringLiteral("FM");
    if (m == QStringLiteral("RTTY-R") || m == QStringLiteral("DIGL"))
        return QStringLiteral("DIGL");
    if (m == QStringLiteral("FT8") || m == QStringLiteral("FT4")
        || m == QStringLiteral("RTTY") || m == QStringLiteral("PSK")
        || m == QStringLiteral("DATA") || m == QStringLiteral("DIG"))
        return QStringLiteral("DIGU");
    return QString();   // unknown → leave current mode
}

void SpotStore::activate(const QString &call) {
    const int idx = indexOf(call);
    if (idx < 0) return;
    const Spot sp = spots_[idx];
    const QString lyraMode = lyraModeFor(sp.mode, sp.freqHz);
    if (prefs_ && !lyraMode.isEmpty()) prefs_->setMode(lyraMode);
    // A spot's frequency is the CARRIER.  In CW the DDS (tuned centre) must
    // sit pitch Hz off the carrier so the operator hears the signal at their
    // CW pitch instead of zero-beat: DDS = carrier − (VFO−DDS offset).
    // Compute the offset for the mode we're switching TO (the Prefs→engine
    // mode binding updates asynchronously, so don't rely on the live mode).
    qint64 dds = sp.freqHz;
    if (engine_ && !lyraMode.isEmpty())
        dds -= engine_->cwMarkerOffsetForMode(lyraMode);
    if (stream_) stream_->setRx1FreqHz(quint32(dds));
    // Echo the original CARRIER frequency so the logger logs the right QSO.
    emit spotActivated(sp.call, sp.mode, sp.freqHz, sp.argb);
}

bool SpotStore::isSpotted(const QString &call) const {
    const QString c = call.trimmed();
    return c.size() >= 3 && indexOf(c) >= 0;
}

bool SpotStore::isSpottedHere(const QString &call) const {
    const QString c = call.trimmed();
    if (c.size() < 3) return false;
    const int idx = indexOf(c);
    if (idx < 0) return false;
    if (!stream_) return true;      // no radio attached → bank-wide answer
    // Spot freqs are the CARRIER; the tuned DDS sits pitch Hz off it (same
    // convention as activate() above), so compare carrier to carrier.
    qint64 carrier = qint64(stream_->rx1FreqHz());
    if (engine_ && prefs_)
        carrier += engine_->cwMarkerOffsetForMode(prefs_->mode());
    // RBN skimmer freq error plus slightly-off tuning stay well inside this;
    // a different real station this close would be inside the RX filter too.
    constexpr qint64 kHereTolHz = 1500;
    return qAbs(spots_[idx].freqHz - carrier) <= kHereTolHz;
}

QVariantList SpotStore::spotsInSpan(double centerHz, double spanHz) const {
    QVariantList out;
    if (!show_ || spanHz <= 0) return out;
    const double lo = centerHz - spanHz / 2.0;
    const double hi = centerHz + spanHz / 2.0;
    // Gather the in-span spots and sort by frequency so the panadapter's
    // row-stagger places frequency-adjacent spots on different rows
    // (declutters callsigns that sit on top of each other).
    QVector<const Spot *> hits;
    for (const Spot &sp : spots_)
        if (sp.freqHz >= lo && sp.freqHz <= hi
            && modeShown(sp.mode) && regionShown(sp))
            hits.append(&sp);

    // --- clutter caps (§5.4) ---
    QHash<const Spot *, int> moreOf;     // bucket overflow → "+K more" badge
    auto byNewest = [](const Spot *a, const Spot *b) { return a->added > b->added; };
    // Per-frequency bucket: keep the N most-recent per bucket, collapse the
    // rest into a badge on the newest kept marker.
    if (bucketMax_ > 0 && bucketHz_ > 0) {
        QHash<qint64, QVector<const Spot *>> buckets;
        for (const Spot *sp : hits)
            buckets[sp->freqHz / bucketHz_].append(sp);
        QVector<const Spot *> kept;
        for (auto it = buckets.begin(); it != buckets.end(); ++it) {
            QVector<const Spot *> &v = it.value();
            if (v.size() <= bucketMax_) { kept += v; continue; }
            std::sort(v.begin(), v.end(), byNewest);
            for (int i = 0; i < bucketMax_; ++i) kept.append(v[i]);
            moreOf[v[0]] = v.size() - bucketMax_;
        }
        hits = kept;
    }
    // Global cap: keep the most-recent N across the whole display.
    if (displayMax_ > 0 && hits.size() > displayMax_) {
        std::sort(hits.begin(), hits.end(), byNewest);
        hits.resize(displayMax_);
    }

    std::sort(hits.begin(), hits.end(),
              [](const Spot *a, const Spot *b) { return a->freqHz < b->freqHz; });
    const QString own = prefs_ ? prefs_->callsign().trimmed().toUpper() : QString();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 flashMs = qint64(flashSec_) * 1000;
    for (const Spot *sp : hits) {
        const bool mine = highlightOwn_ && !own.isEmpty()
                          && sp->call.toUpper() == own;
        // A freshly-arrived spot wears the flash colour (solid) until it is
        // older than flashSec, then it follows the normal colour rules.
        // Never override the own-call highlight (mine), the stronger signal.
        const bool flash = flashNew_ && !mine && (nowMs - sp->firstSeen) < flashMs;
        QVariantMap m;
        m[QStringLiteral("call")]    = sp->call;          // raw (for activate/echo)
        m[QStringLiteral("country")] = sp->country;       // ISO-2 or ""
        m[QStringLiteral("label")]   = sp->country.isEmpty()
                                       ? sp->call
                                       : sp->country + QLatin1Char(' ') + sp->call;
        m[QStringLiteral("freqHz")]  = double(sp->freqHz);
        m[QStringLiteral("color")]   = mine ? highlightColor_ : colorFor(*sp);
        m[QStringLiteral("mine")]    = mine;
        m[QStringLiteral("flash")]   = flash;
        m[QStringLiteral("flashColor")] = flashColor_;
        m[QStringLiteral("mode")]    = sp->mode;
        m[QStringLiteral("text")]    = sp->text;
        m[QStringLiteral("more")]    = moreOf.value(sp, 0);   // "+K more" badge
        out.append(m);
    }
    return out;
}

void SpotStore::setShowSpots(bool on) {
    if (show_ == on) return;
    show_ = on;
    QSettings().setValue(QString::fromLatin1(kKeyShow), on);
    emit changed();
}
void SpotStore::setMaxSpots(int n) {
    n = qBound(1, n, 5000);
    if (max_ == n) return;
    max_ = n;
    QSettings().setValue(QString::fromLatin1(kKeyMax), n);
    enforceCap();
    emit changed();
}
void SpotStore::setLifetimeSec(int s) {
    s = qMax(0, s);
    if (lifetimeSec_ == s) return;
    lifetimeSec_ = s;
    QSettings().setValue(QString::fromLatin1(kKeyLife), s);
    if (lifetimeSec_ > 0) ageTimer_->start();
    else                  ageTimer_->stop();
}
void SpotStore::setHighlightOwn(bool on) {
    if (highlightOwn_ == on) return;
    highlightOwn_ = on;
    QSettings().setValue(QString::fromLatin1(kKeyHlOwn), on);
    emit changed();
}
void SpotStore::setHighlightColor(const QString &hex) {
    if (hex.isEmpty() || highlightColor_ == hex) return;
    highlightColor_ = hex;
    QSettings().setValue(QString::fromLatin1(kKeyHlCol), hex);
    emit changed();
}
void SpotStore::setNotifyOwn(bool on) {
    if (notifyOwn_ == on) return;
    notifyOwn_ = on;
    lastNotify_ = 0;   // allow an immediate first notification
    QSettings().setValue(QString::fromLatin1(kKeyNotify), on);
}
void SpotStore::setNotifyCooldownMin(int min) {
    min = qBound(0, min, 240);
    if (notifyCooldownMin_ == min) return;
    notifyCooldownMin_ = min;
    QSettings().setValue(QString::fromLatin1(kKeyCooldown), min);
}
void SpotStore::setFlashNew(bool on) {
    if (flashNew_ == on) return;
    flashNew_ = on;
    QSettings().setValue(QString::fromLatin1(kKeyFlashNew), on);
    // Turning on while recent spots exist → pulse immediately; turning
    // off → the next onFlashTick sees !flashNew_ via anyFlashing and
    // stops.  Either way re-query so the change shows at once.
    if (flashNew_ && flashTimer_ && !flashTimer_->isActive()
        && anyFlashing(QDateTime::currentMSecsSinceEpoch()))
        flashTimer_->start();
    emit changed();
}
void SpotStore::setFlashColor(const QString &hex) {
    if (hex.isEmpty() || flashColor_ == hex) return;
    flashColor_ = hex;
    QSettings().setValue(QString::fromLatin1(kKeyFlashCol), hex);
    emit changed();
}
void SpotStore::setFlashSec(int s) {
    s = qBound(1, s, 60);
    if (flashSec_ == s) return;
    flashSec_ = s;
    QSettings().setValue(QString::fromLatin1(kKeyFlashSec), s);
    emit changed();
}

bool SpotStore::modeShown(const QString &mode) const {
    if (modeFilter_.isEmpty()) return true;        // no filter → everything
    const QString up = mode.trimmed().toUpper();
    if (up.isEmpty()) return true;                 // unknown mode → don't hide
    for (const QString &t : modeFilter_) {
        if (up == t) return true;
        if (t == QStringLiteral("SSB")
            && (up == QStringLiteral("USB") || up == QStringLiteral("LSB"))) return true;
        if (t == QStringLiteral("CW")
            && (up == QStringLiteral("CWU") || up == QStringLiteral("CWL"))) return true;
        if (t == QStringLiteral("FM")
            && (up == QStringLiteral("NFM") || up == QStringLiteral("WFM"))) return true;
        if (t == QStringLiteral("AM")
            && (up == QStringLiteral("SAM") || up == QStringLiteral("DSB"))) return true;
        if ((t == QStringLiteral("DIGI") || t == QStringLiteral("DIGITAL")
             || t == QStringLiteral("DATA") || t == QStringLiteral("DIG"))
            && isDigitalMode(up)) return true;
    }
    return false;
}

void SpotStore::setModeFilter(const QString &csv) {
    QStringList toks;
    const auto parts = csv.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        const QString t = p.trimmed().toUpper();
        if (!t.isEmpty() && !toks.contains(t)) toks << t;
    }
    if (toks == modeFilter_) return;
    modeFilter_ = toks;
    QSettings().setValue(QString::fromLatin1(kKeyModeFilt),
                         modeFilter_.join(QLatin1Char(',')));
    emit changed();
}

bool SpotStore::regionShown(const Spot &sp) const {
    if (regionFilter_.isEmpty()) return true;
    const QString cont = sp.continent.toUpper();
    const QString ctry = sp.country.toUpper();
    for (const QString &t : regionFilter_) {
        // The six continent codes always mean continents (so "NA" = North
        // America, never Namibia); every other token is an ISO-2 country.
        if (isContinentCode(t)) { if (!cont.isEmpty() && cont == t) return true; }
        else                    { if (!ctry.isEmpty() && ctry == t) return true; }
    }
    return false;
}

void SpotStore::setRegionFilter(const QString &csv) {
    QStringList toks;
    const auto parts = csv.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        const QString t = p.trimmed().toUpper();
        if (!t.isEmpty() && !toks.contains(t)) toks << t;
    }
    if (toks == regionFilter_) return;
    regionFilter_ = toks;
    QSettings().setValue(QString::fromLatin1(kKeyRegionFilt),
                         regionFilter_.join(QLatin1Char(',')));
    emit changed();
}

QString SpotStore::colorFor(const Spot &sp) const {
    switch (colorMode_) {
    case 1: return singleColor_;
    case 2: return modeColorHex(sp.mode);
    case 3: return regionColorHex(sp.continent);
    case 4: return countryColorHex(sp.country);
    case 0:
    default:
        return QColor::fromRgba(sp.argb ? sp.argb : 0xFFFFD700u).name(QColor::HexArgb);
    }
}

QVariantList SpotStore::legendEntries() const {
    QVariantList out;
    if (!legendOn_) return out;
    auto add = [&out](const QString &label, const QString &color) {
        QVariantMap m;
        m[QStringLiteral("label")] = label;
        m[QStringLiteral("color")] = color;
        out.append(m);
    };
    if (colorMode_ == 2) {            // by mode
        add(QStringLiteral("CW"),      QStringLiteral("#29b6f6"));
        add(QStringLiteral("Phone"),   QStringLiteral("#66bb6a"));
        add(QStringLiteral("FT8/Dig"), QStringLiteral("#ffa726"));
        add(QStringLiteral("RTTY"),    QStringLiteral("#ab47bc"));
    } else if (colorMode_ == 3) {     // by region
        add(QStringLiteral("NA"), QStringLiteral("#42a5f5"));
        add(QStringLiteral("SA"), QStringLiteral("#26a69a"));
        add(QStringLiteral("EU"), QStringLiteral("#66bb6a"));
        add(QStringLiteral("AF"), QStringLiteral("#ffa726"));
        add(QStringLiteral("AS"), QStringLiteral("#ef5350"));
        add(QStringLiteral("OC"), QStringLiteral("#ab47bc"));
    }
    // Source / Single / Country → no legend.
    return out;
}

void SpotStore::setColorMode(int m) {
    m = qBound(0, m, 4);
    if (colorMode_ == m) return;
    colorMode_ = m;
    QSettings().setValue(QString::fromLatin1(kKeyColorMode), m);
    emit changed();
}
void SpotStore::setSingleColor(const QString &hex) {
    if (hex.isEmpty() || singleColor_ == hex) return;
    singleColor_ = hex;
    QSettings().setValue(QString::fromLatin1(kKeySingleCol), hex);
    emit changed();
}
void SpotStore::setLegendOn(bool on) {
    if (legendOn_ == on) return;
    legendOn_ = on;
    QSettings().setValue(QString::fromLatin1(kKeyLegend), on);
    emit changed();
}
void SpotStore::setDisplayMax(int n) {
    n = qBound(0, n, 1000);
    if (displayMax_ == n) return;
    displayMax_ = n;
    QSettings().setValue(QString::fromLatin1(kKeyDispMax), n);
    emit changed();
}
void SpotStore::setBucketMax(int n) {
    n = qBound(0, n, 100);
    if (bucketMax_ == n) return;
    bucketMax_ = n;
    QSettings().setValue(QString::fromLatin1(kKeyBktMax), n);
    emit changed();
}
void SpotStore::setBucketHz(int hz) {
    hz = qBound(50, hz, 20000);
    if (bucketHz_ == hz) return;
    bucketHz_ = hz;
    QSettings().setValue(QString::fromLatin1(kKeyBktHz), hz);
    emit changed();
}

} // namespace lyra::ui

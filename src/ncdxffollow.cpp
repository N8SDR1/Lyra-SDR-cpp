// Lyra — NCDXF beacon auto-follow.  See ncdxffollow.h.

#include "ncdxffollow.h"

#include "hl2_stream.h"
#include "prefs.h"

#include <QDateTime>
#include <QSettings>
#include <QTimer>

namespace lyra::ui {

namespace {
constexpr auto kKey = "propagation/ncdxf_follow_station";
// The 5 beacon bands (kHz) in canonical rotation order.
constexpr int kBandKhz[5] = {14100, 18110, 21150, 24930, 28200};
} // namespace

const QVector<NcdxfFollow::Station> &NcdxfFollow::stations() {
    static const QVector<Station> s = {
        {"4U1UN",  "United Nations, NY"},   {"VE8AT",  "Eureka, Canada"},
        {"W6WX",   "Mt Umunhum, CA"},       {"KH6RS",  "Maui, HI"},
        {"ZL6B",   "Masterton, NZ"},        {"VK6RBP", "Perth, Australia"},
        {"JA2IGY", "Mt Asama, Japan"},      {"RR9O",   "Novosibirsk, Russia"},
        {"VR2HK6", "Hong Kong"},            {"4S7B",   "Colombo, Sri Lanka"},
        {"ZS6DN",  "Pretoria, South Africa"},{"5Z4B",  "Kilimambogo, Kenya"},
        {"4X6TU",  "Tel Aviv, Israel"},     {"OH2B",   "Lohja, Finland"},
        {"CS3B",   "Madeira, Portugal"},    {"LU4AA",  "Buenos Aires, Argentina"},
        {"OA4B",   "Lima, Peru"},           {"YV5B",   "Caracas, Venezuela"},
    };
    return s;
}

int NcdxfFollow::indexOf(const QString &call) {
    const auto &s = stations();
    for (int i = 0; i < s.size(); ++i)
        if (s[i].call == call) return i;
    return -1;
}

NcdxfFollow::NcdxfFollow(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                         QObject *parent)
    : QObject(parent), prefs_(prefs), stream_(stream),
      timer_(new QTimer(this)) {
    // 1 Hz: re-evaluate the current rotation slot regardless of when
    // follow was armed (cheap; a no-op retune within a slot is harmless).
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &NcdxfFollow::pump);

    // Restore the persisted selection.
    const QString saved =
        QSettings().value(QString::fromLatin1(kKey)).toString();
    if (indexOf(saved) >= 0) {
        station_ = saved;
        timer_->start();
    }
}

void NcdxfFollow::setStation(const QString &call) {
    QString c = call;
    if (indexOf(c) < 0) c.clear();   // unknown/empty = off
    if (c == station_) return;
    station_ = c;
    QSettings().setValue(QString::fromLatin1(kKey), c);
    emit stationChanged(c);
    if (!c.isEmpty()) {
        if (!timer_->isActive()) timer_->start();
        pump();   // jump immediately
    } else {
        timer_->stop();
    }
}

void NcdxfFollow::pump() {
    if (station_.isEmpty() || !stream_) return;
    if (stream_->txDisplayActive()) return;   // no retune under power; resumes next tick
    const int target = indexOf(station_);
    if (target < 0) return;
    const QTime t = QDateTime::currentDateTimeUtc().time();
    const int slot = ((t.minute() % 3) * 60 + t.second()) / 10;   // 0..17
    for (int b = 0; b < 5; ++b) {
        if (((slot - b) % 18 + 18) % 18 == target) {
            // The followed station is on band b right now: QSY + CWU.
            if (prefs_ && prefs_->mode() != QLatin1String("CWU"))
                prefs_->setMode(QStringLiteral("CWU"));
            stream_->setRx1FreqHz(quint32(kBandKhz[b]) * 1000u);
            return;
        }
    }
    // No band this slot (never happens with 18 stations / 5 bands) — hold.
}

} // namespace lyra::ui

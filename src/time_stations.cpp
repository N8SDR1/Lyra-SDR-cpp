// Lyra — HF time-station "TIME" cycle.  See time_stations.h.

#include "time_stations.h"

#include "hl2_stream.h"
#include "prefs.h"

#include <QHash>
#include <QSet>
#include <QSettings>
#include <QVariantMap>

namespace lyra::ui {

namespace {
constexpr auto kIdxKey = "bands/time_cycle_idx";

// The 9 HF time stations old Lyra carried, in static table order.  All
// AM except CHU (USB).  Continent is hand-assigned (WWVH = OC, not NA)
// so same-continent grouping is right.  Freqs in kHz, low → high.
struct Row { const char *id, *name, *country, *continent, *mode, *notes;
             QVector<int> freqs; };
const QVector<Row> kStations = {
    {"WWV",   "WWV (Fort Collins, CO)", "US", "NA", "AM",
     "Voice + tones + 100 Hz subcarrier", {2500,5000,10000,15000,20000,25000}},
    {"WWVH",  "WWVH (Kekaha, HI)",      "US", "OC", "AM",
     "Pacific counterpart to WWV",       {2500,5000,10000,15000}},
    {"CHU",   "CHU (Ottawa, ON)",       "CA", "NA", "USB",
     "Note: USB mode, not AM",           {3330,7850,14670}},
    {"BPM",   "BPM (Pucheng, China)",   "CN", "AS", "AM",
     "One of Asia's most powerful time stations", {2500,5000,10000,15000}},
    {"RWM",   "RWM (Moscow, RU)",       "RU", "EU", "AM",
     "Morse code + pulses only — no voice", {4996,9996,14996}},
    {"HLA",   "HLA (Daejeon, KR)",      "KR", "AS", "AM", "", {5000}},
    {"YVTO",  "YVTO (Caracas, VE)",     "VE", "SA", "AM",
     "Spanish voice announcements",      {5000}},
    {"LOL",   "LOL (Buenos Aires, AR)", "AR", "SA", "AM",
     "Limited schedule, not 24/7",       {5000,10000,15000}},
    {"HD2IOA","HD2IOA (Guayaquil, EC)", "EC", "SA", "AM", "", {3810,7600}},
};

// Continent for an ISO-2 country (only the ones the table cares about,
// plus a few common neighbours so ordering does something sensible).
QString continentFor(const QString &iso) {
    static const QHash<QString, QString> m = {
        {"US","NA"},{"CA","NA"},{"MX","NA"},
        {"CN","AS"},{"KR","AS"},{"JP","AS"},
        {"RU","EU"},{"GB","EU"},{"DE","EU"},{"FR","EU"},{"IT","EU"},
        {"VE","SA"},{"AR","SA"},{"EC","SA"},{"BR","SA"},
    };
    return m.value(iso.toUpper());
}

// Compact callsign-prefix → ISO-2, covering the time-station countries
// (US/CA/CN/RU/KR/VE/AR/EC) so cycle ordering matches the operator.  Not
// a full DXCC table — just enough to lead the cycle correctly.
QString isoFromCallsign(const QString &cs) {
    const QString c = cs.trimmed().toUpper();
    if (c.isEmpty()) return QString();
    const QChar a = c.at(0);
    const QString two = c.left(2);
    // US: A, K, N, W (W/K/N + AA–AL).
    if (a == 'W' || a == 'K' || a == 'N' ||
        (a == 'A' && two >= "AA" && two <= "AL")) return "US";
    // Canada: VA–VG, VO, VY, CF–CK, XJ–XO.
    if ((two >= "VA" && two <= "VG") || two == "VO" || two == "VY" ||
        (two >= "CF" && two <= "CK") || (two >= "XJ" && two <= "XO")) return "CA";
    if (a == 'B') return "CN";                       // China
    if (a == 'R' || (two >= "UA" && two <= "UI")) return "RU";  // Russia
    if (two == "HL" || two == "DS" || two == "DT") return "KR"; // Korea
    if (two >= "YV" && two <= "YY") return "VE";     // Venezuela
    if ((two >= "LO" && two <= "LW") || (two >= "AY" && two <= "AZ")) return "AR";
    if (two == "HC" || two == "HD") return "EC";     // Ecuador
    return QString();
}
} // namespace

TimeStations::TimeStations(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                           QObject *parent)
    : QObject(parent), prefs_(prefs), stream_(stream) {}

QString TimeStations::operatorCountryIso() const {
    return prefs_ ? isoFromCallsign(prefs_->callsign()) : QString();
}

QVector<TimeStations::Station> TimeStations::ordered() const {
    const QString op = operatorCountryIso();
    const QString opCont = continentFor(op);
    QVector<Station> same, sameCont, rest;
    for (const Row &r : kStations) {
        Station s{r.id, r.name, r.country, r.continent, r.mode, r.notes, r.freqs};
        if (!op.isEmpty() && s.country == op)          same.append(s);
        else if (!opCont.isEmpty() && s.continent == opCont) sameCont.append(s);
        else                                           rest.append(s);
    }
    return same + sameCont + rest;
}

int TimeStations::cycleLength(const QVector<Station> &v) const {
    int n = 0;
    for (const Station &s : v) n += s.freqsKhz.size();
    return n;
}

int TimeStations::loadIdx() const {
    QSettings s;
    return s.value(QString::fromLatin1(kIdxKey), 0).toInt();
}
void TimeStations::saveIdx(int idx) const {
    QSettings s;
    s.setValue(QString::fromLatin1(kIdxKey), idx);
}

QString TimeStations::tune(const Station &s, int freqKhz) {
    // Blocked while transmitting across bands: empty message, no retune.
    if (stream_ && stream_->txBlocksBandJump(quint32(qint64(freqKhz) * 1000)))
        return QString();
    emit aboutToTune();
    // Freq FIRST, then mode: moving off the old band before the mode
    // changes stops per-band memory from saving this station's mode (e.g.
    // AM) against the band we just left, and lets our mode win over any
    // band-default restore the freq change triggers.
    if (stream_) stream_->setRx1FreqHz(quint32(qint64(freqKhz) * 1000));
    if (prefs_ && !s.mode.isEmpty()) prefs_->setMode(s.mode);
    const QString mhz = QString::number(freqKhz / 1000.0, 'f', 3);
    QString msg = QStringLiteral("TIME: %1 %2 MHz %3 — %4")
                      .arg(s.id, mhz, s.mode, s.name);
    return msg;
}

QString TimeStations::cycleNext() {
    const QVector<Station> v = ordered();
    const int total = cycleLength(v);
    if (total == 0) return QString();
    int n = ((loadIdx() % total) + total) % total;   // safe modulo
    const int next = (n + 1) % total;
    for (const Station &s : v) {
        const int L = s.freqsKhz.size();
        if (n < L) {
            const QString msg = tune(s, s.freqsKhz[n]);
            if (!msg.isEmpty()) saveIdx(next);   // advance + persist (not if blocked)
            return msg;
        }
        n -= L;
    }
    return tune(v.front(), v.front().freqsKhz.front());   // unreachable
}

QString TimeStations::tuneEntry(int stationIndex, int freqIndex) {
    const QVector<Station> v = ordered();
    if (stationIndex < 0 || stationIndex >= v.size()) return QString();
    const Station &s = v[stationIndex];
    if (freqIndex < 0 || freqIndex >= s.freqsKhz.size()) return QString();
    // Flat absolute index of this entry, so a later left-click follows on.
    int flat = 0;
    for (int i = 0; i < stationIndex; ++i) flat += v[i].freqsKhz.size();
    flat += freqIndex;
    const int total = cycleLength(v);
    const QString msg = tune(s, s.freqsKhz[freqIndex]);
    if (total > 0 && !msg.isEmpty()) saveIdx((flat + 1) % total);
    return msg;
}

void TimeStations::resetCycle() { saveIdx(0); }

QVariantList TimeStations::calStations(const QString &region) const {
    // Every tunable carrier as a flat pick for the calibration instrument,
    // operator-country-first (same order as the menu).  All are steady
    // carriers usable for a frequency measure regardless of their voice mode.
    // Filter by the operator's band-plan region so we don't offer a station
    // they'd never hear (RWM Moscow to a US op = clutter).  Continent codes:
    // NA/SA/EU/AF/AS/OC.  Empty set = no filter (NONE / unset → show all).
    QSet<QString> show;
    if (region == QLatin1String("US"))               // R2 — Americas + Pacific
        show = {QStringLiteral("NA"), QStringLiteral("OC")};   // WWV, WWVH, CHU
    else if (region == QLatin1String("IARU_R1"))     // EU / Africa
        show = {QStringLiteral("EU"), QStringLiteral("AF")};   // RWM
    else if (region == QLatin1String("IARU_R3"))     // Asia-Pacific
        show = {QStringLiteral("AS"), QStringLiteral("OC")};   // BPM, HLA, WWVH

    QVariantList out;
    const QVector<Station> v = ordered();
    for (const Station &s : v) {
        if (!show.isEmpty() && !show.contains(s.continent)) continue;
        for (int khz : s.freqsKhz) {
            QVariantMap m;
            m[QStringLiteral("label")] =
                QStringLiteral("%1 %2").arg(s.id, QString::number(khz / 1000.0));
            m[QStringLiteral("freqHz")]    = static_cast<double>(khz) * 1000.0;
            m[QStringLiteral("continent")] = s.continent;
            out.append(m);
        }
    }
    return out;
}

QVariantList TimeStations::menuEntries() const {
    QVariantList out;
    const QVector<Station> v = ordered();
    for (int si = 0; si < v.size(); ++si) {
        const Station &s = v[si];
        QVariantMap h;
        h[QStringLiteral("header")]  = true;
        h[QStringLiteral("text")]    = s.name;
        h[QStringLiteral("station")] = -1;
        h[QStringLiteral("freq")]    = -1;
        out.append(h);
        for (int fi = 0; fi < s.freqsKhz.size(); ++fi) {
            QVariantMap f;
            f[QStringLiteral("header")]  = false;
            f[QStringLiteral("text")]    =
                QStringLiteral("    %1 MHz %2")
                    .arg(QString::number(s.freqsKhz[fi] / 1000.0, 'f', 3), s.mode);
            f[QStringLiteral("station")] = si;
            f[QStringLiteral("freq")]    = fi;
            out.append(f);
        }
    }
    return out;
}

} // namespace lyra::ui

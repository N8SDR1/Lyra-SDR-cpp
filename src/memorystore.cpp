// Lyra — frequency memory bank.  See memorystore.h.

#include "memorystore.h"

#include "hl2_stream.h"
#include "prefs.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTextStream>
#include <QVariantMap>

namespace lyra::ui {

namespace {
constexpr auto kKey = "memory/presets";
}

MemoryStore::MemoryStore(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                         QObject *parent)
    : QObject(parent), prefs_(prefs), stream_(stream) {
    load();
}

void MemoryStore::load() {
    presets_.clear();
    const QByteArray raw =
        QSettings().value(QString::fromLatin1(kKey)).toByteArray();
    const QJsonArray arr = QJsonDocument::fromJson(raw).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Preset p;
        p.name  = o.value(QStringLiteral("name")).toString().left(kMaxName);
        p.freq  = qint64(o.value(QStringLiteral("freq")).toDouble());
        p.mode  = o.value(QStringLiteral("mode")).toString();
        p.rxBw  = o.value(QStringLiteral("rxBw")).toInt();
        p.notes = o.value(QStringLiteral("notes")).toString().left(kMaxNotes);
        p.offsetHz    = o.value(QStringLiteral("offsetHz")).toInt();      // 0 if absent
        p.ctcssToneHz = o.value(QStringLiteral("ctcssToneHz")).toDouble();// 0 if absent
        p.ctcssDlHz   = o.value(QStringLiteral("ctcssDlHz")).toDouble();
        p.burstHz     = o.value(QStringLiteral("burstHz")).toInt();
        if (p.burstHz != 1750) p.burstHz = 0;
        if (p.freq > 0 && presets_.size() < kMax) presets_.append(p);
    }
}

void MemoryStore::save() const {
    QJsonArray arr;
    for (const Preset &p : presets_) {
        QJsonObject o;
        o[QStringLiteral("name")]  = p.name;
        o[QStringLiteral("freq")]  = double(p.freq);
        o[QStringLiteral("mode")]  = p.mode;
        o[QStringLiteral("rxBw")]  = p.rxBw;
        o[QStringLiteral("notes")] = p.notes;
        o[QStringLiteral("offsetHz")]    = p.offsetHz;
        o[QStringLiteral("ctcssToneHz")] = p.ctcssToneHz;
        o[QStringLiteral("ctcssDlHz")]   = p.ctcssDlHz;
        o[QStringLiteral("burstHz")]     = p.burstHz;
        arr.append(o);
    }
    QSettings().setValue(QString::fromLatin1(kKey),
                         QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QVariantList MemoryStore::list() const {
    QVariantList out;
    for (const Preset &p : presets_) {
        QVariantMap m;
        m[QStringLiteral("name")]   = p.name;
        m[QStringLiteral("freq")]   = double(p.freq);
        m[QStringLiteral("freqMHz")]= QString::number(p.freq / 1.0e6, 'f', 4);
        m[QStringLiteral("mode")]   = p.mode;
        m[QStringLiteral("rxBw")]   = p.rxBw;
        m[QStringLiteral("notes")]  = p.notes;
        m[QStringLiteral("offsetHz")]    = p.offsetHz;
        m[QStringLiteral("ctcssToneHz")] = p.ctcssToneHz;
        m[QStringLiteral("ctcssDlHz")]   = p.ctcssDlHz;
        m[QStringLiteral("burstHz")]     = p.burstHz;
        out.append(m);
    }
    return out;
}

void MemoryStore::recall(int index) {
    if (index < 0 || index >= presets_.size()) return;
    const Preset &p = presets_[index];
    // Freq first, then mode (see TimeStations::tune) — so recalling a
    // preset doesn't overwrite the mode memory of the band we're leaving,
    // and the preset's mode wins over any band-default restore.
    if (stream_) stream_->setRx1FreqHz(quint32(p.freq));
    if (prefs_ && !p.mode.isEmpty()) prefs_->setMode(p.mode);
    if (prefs_ && p.rxBw > 0) prefs_->setRxBandwidth(p.rxBw);   // optional BW
    // Repeater state — each preset fully defines its SPLIT + CTCSS, so a
    // repeater recall arms split to the input (VFO B = output + offset) and
    // sets the access tone, and a plain (offset 0 / tone 0) recall CLEARS
    // both — so recalling simplex after a repeater drops the split + tone.
    // Drives only the single TX-freq path (setVfoBHz/setSplitEnabled), so
    // it stays PureSignal-safe like manual split.
    if (stream_) {
        if (p.offsetHz != 0) {
            stream_->setVfoBHz(quint32(qint64(p.freq) + p.offsetHz));
            stream_->setSplitEnabled(true);
        } else {
            stream_->setSplitEnabled(false);
        }
        if (p.ctcssToneHz > 0.0) {
            stream_->setCtcssToneHz(p.ctcssToneHz);
            stream_->setCtcssEnabled(true);
        } else {
            stream_->setCtcssEnabled(false);
        }
        stream_->setCtcssDlHz(p.ctcssDlHz);
        stream_->setFmBurstHz(p.burstHz);
    }
}

QString MemoryStore::currentAutoName() const {
    const double f = stream_ ? double(stream_->rx1FreqHz()) : 0.0;
    const QString mode = prefs_ ? prefs_->mode() : QString();
    return QStringLiteral("%1 %2").arg(f / 1.0e6, 0, 'f', 3).arg(mode);
}

bool MemoryStore::addCurrent(const QString &name) {
    if (full() || !stream_) return false;
    Preset p;
    p.name = (name.trimmed().isEmpty() ? currentAutoName()
                                       : name.trimmed()).left(kMaxName);
    p.freq = stream_->rx1FreqHz();
    p.mode = prefs_ ? prefs_->mode() : QString();
    p.rxBw = 0;
    // Capture the live repeater state so "Store current" while on a
    // repeater (split + tone set) saves it as a one-click recall.
    if (stream_->splitEnabled())
        p.offsetHz = int(qint64(stream_->vfoBHz()) - qint64(stream_->rx1FreqHz()));
    if (stream_->ctcssEnabled())
        p.ctcssToneHz = stream_->ctcssToneHz();
    p.ctcssDlHz = stream_->ctcssDlHz();
    p.burstHz   = stream_->fmBurstHz();
    presets_.append(p);
    save();
    emit changed();
    return true;
}

bool MemoryStore::addPreset(const Preset &p) {
    if (full() || p.freq <= 0) return false;
    presets_.append(p);
    save();
    emit changed();
    return true;
}

void MemoryStore::setPreset(int index, const Preset &p) {
    if (index < 0 || index >= presets_.size()) return;
    presets_[index] = p;
    save();
    emit changed();
}

void MemoryStore::remove(int index) {
    if (index < 0 || index >= presets_.size()) return;
    presets_.remove(index);
    save();
    emit changed();
}

void MemoryStore::clearAll() {
    if (presets_.isEmpty()) return;
    presets_.clear();
    save();
    emit changed();
}

bool MemoryStore::exportCsv(const QString &path) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    auto esc = [](QString s) {
        s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')))
            ? QStringLiteral("\"%1\"").arg(s) : s;
    };
    // Offset_Hz/CTCSS_Hz appended AFTER Notes so old 5-column CSVs still
    // import (the two new fields just default to 0 when absent).
    ts << "Name,Freq_Hz,Mode,RX_BW_Hz,Notes,Offset_Hz,CTCSS_Hz,"
          "CTCSS_DL_Hz,Burst_Hz\n";
    for (const Preset &p : presets_) {
        ts << esc(p.name) << ',' << p.freq << ',' << p.mode << ','
           << (p.rxBw > 0 ? QString::number(p.rxBw) : QString()) << ','
           << esc(p.notes) << ','
           << p.offsetHz << ','
           << (p.ctcssToneHz > 0.0 ? QString::number(p.ctcssToneHz, 'f', 1)
                                   : QString()) << ','
           << (p.ctcssDlHz > 0.0 ? QString::number(p.ctcssDlHz, 'f', 1)
                                   : QString()) << ','
           << (p.burstHz == 1750 ? QStringLiteral("1750") : QString()) << '\n';
    }
    return true;
}

MemoryStore::ImportResult MemoryStore::importCsv(const QString &path,
                                                 bool replace) {
    ImportResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.error = tr("Could not open the file.");
        return r;
    }
    QVector<Preset> incoming;
    QTextStream ts(&f);
    bool first = true;
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (first) { first = false; continue; }   // skip header
        if (line.trimmed().isEmpty()) continue;
        // Simple split (handles unquoted fields + basic quoted name/notes).
        QStringList c;
        QString cur; bool inq = false;
        for (const QChar ch : line) {
            if (ch == QLatin1Char('"')) { inq = !inq; }
            else if (ch == QLatin1Char(',') && !inq) { c << cur; cur.clear(); }
            else cur += ch;
        }
        c << cur;
        if (c.size() < 3) { r.skipped++; continue; }
        bool ok = false;
        const qint64 hz = qint64(c[1].trimmed().toDouble(&ok));
        if (!ok || hz <= 0) { r.skipped++; continue; }
        Preset p;
        p.name  = c[0].trimmed().left(kMaxName);
        p.freq  = hz;
        p.mode  = c[2].trimmed().toUpper();
        p.rxBw  = (c.size() > 3) ? c[3].trimmed().toInt() : 0;
        p.notes = (c.size() > 4) ? c[4].trimmed().left(kMaxNotes) : QString();
        p.offsetHz    = (c.size() > 5) ? c[5].trimmed().toInt() : 0;
        p.ctcssToneHz = (c.size() > 6) ? c[6].trimmed().toDouble() : 0.0;
        p.ctcssDlHz   = (c.size() > 7) ? c[7].trimmed().toDouble() : 0.0;
        if (c.size() > 8) {
            const QString b = c[8].trimmed();
            p.burstHz = (b.contains(QStringLiteral("1750")) || b == QLatin1String("1")
                         || b.compare(QLatin1String("y"), Qt::CaseInsensitive) == 0)
                            ? 1750 : 0;
        }
        incoming.append(p);
    }
    if (replace) presets_.clear();
    for (const Preset &p : incoming) {
        if (presets_.size() >= kMax) break;
        presets_.append(p);
        r.added++;
    }
    save();
    emit changed();
    return r;
}

} // namespace lyra::ui

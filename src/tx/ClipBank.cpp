// Lyra-cpp — #89 Voice keyer, Stage B (B3): clip bank + persistence.  See ClipBank.h.

#include "tx/ClipBank.h"

#include "tx/WavIo.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QVariantMap>

#include <algorithm>

namespace lyra::tx {

namespace {
constexpr double kMinFreeFraction = 0.02;   // stop writing if < 2% free (design §6 guard)
constexpr qint64 kMinFreeBytes    = 32ll * 1024 * 1024;   // ...or < 32 MB absolute
}

ClipBank::ClipBank(QObject *parent) : QObject(parent) {
    load();
}

QString ClipBank::defaultDir() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath() + QStringLiteral("/Lyra");
    return base + QStringLiteral("/clips");
}

QString ClipBank::absPath(const QString &file) const {
    return QDir(dir_).filePath(file);
}

QVariantList ClipBank::clips() const {
    QVariantList out;
    out.reserve(clips_.size());
    for (const auto &e : clips_) {
        QVariantMap m;
        m[QStringLiteral("id")]         = e.id;
        m[QStringLiteral("label")]      = e.label;
        m[QStringLiteral("kind")]       = e.kind;
        m[QStringLiteral("fkey")]       = e.fkey;
        m[QStringLiteral("gainDb")]     = e.gainDb;
        m[QStringLiteral("bypassDsp")]  = e.bypassDsp;
        m[QStringLiteral("durationMs")] = e.durationMs;
        m[QStringLiteral("file")]       = absPath(e.file);
        out.append(m);
    }
    return out;
}

ClipBank::Entry *ClipBank::find(const QString &id) {
    for (auto &e : clips_)
        if (e.id == id) return &e;
    return nullptr;
}
const ClipBank::Entry *ClipBank::find(const QString &id) const {
    for (const auto &e : clips_)
        if (e.id == id) return &e;
    return nullptr;
}

bool ClipBank::contains(const QString &id) const { return find(id) != nullptr; }

double ClipBank::gainDbOf(const QString &id) const {
    const Entry *e = find(id);
    return e ? e->gainDb : 0.0;
}
bool ClipBank::bypassDspOf(const QString &id) const {
    const Entry *e = find(id);
    return e ? e->bypassDsp : false;
}

QString ClipBank::makeId() {
    return QStringLiteral("clip_%1").arg(nextSeq_++, 5, 10, QChar('0'));
}

QString ClipBank::idForFkey(int fkey) const {
    if (fkey <= 0) return {};
    for (const auto &e : clips_)
        if (e.fkey == fkey) return e.id;
    return {};
}

bool ClipBank::freeSpaceOk(const QString &dir) {
    QStorageInfo si(dir);
    if (!si.isValid() || si.bytesTotal() <= 0) return true;   // unknown -> don't block
    const qint64 avail = si.bytesAvailable();
    const double frac  = static_cast<double>(avail) / static_cast<double>(si.bytesTotal());
    return avail >= kMinFreeBytes && frac >= kMinFreeFraction;
}

QString ClipBank::addEntry(Entry e) {
    clips_.append(std::move(e));
    save();
    emit clipsChanged();
    return clips_.last().id;
}

QString ClipBank::addFromSamples(const QString &label, int kind,
                                 const std::vector<float> &mono, int sampleRate) {
    if (!QDir().mkpath(dir_)) {
        const QString msg = tr("Can't save the clip: the clips folder %1 can't be "
                               "created. Pick another folder (Change…).").arg(dir_);
        qWarning("[clips] %s", qPrintable(msg));
        emit clipError(msg);
        return {};
    }
    if (!freeSpaceOk(dir_)) {
        const QString msg = tr("Can't save the clip: the disk holding %1 is nearly "
                               "full.").arg(dir_);
        qWarning("[clips] %s", qPrintable(msg));
        emit clipError(msg);
        return {};
    }

    std::vector<float> samples =
        (sampleRate != 48000) ? resampleLinear(mono, sampleRate, 48000) : mono;

    Entry e;
    e.id           = makeId();
    e.file         = e.id + QStringLiteral(".wav");
    e.label        = label.isEmpty() ? e.id : label;
    e.kind         = kind;
    e.durationMs   = static_cast<int>(samples.size() * 1000ll / 48000);
    e.createdEpoch = 0;   // stamped by caller if wanted; not load-bearing

    if (!writeWavMonoFloat(absPath(e.file).toStdString(), samples, 48000)) {
        const QString msg = tr("Can't save the clip: writing %1 failed (is the "
                               "folder writable?).").arg(absPath(e.file));
        qWarning("[clips] %s", qPrintable(msg));
        emit clipError(msg);
        return {};
    }
    return addEntry(std::move(e));
}

QString ClipBank::importWav(const QString &label, int kind, const QString &srcPath) {
    WavData d;
    std::string err;
    if (!readWavMono(srcPath.toStdString(), d, err) || d.mono.empty())
        return {};
    return addFromSamples(label, kind, d.mono, d.sampleRate);
}

void ClipBank::setLabel(const QString &id, const QString &label) {
    if (Entry *e = find(id)) { e->label = label; save(); emit clipsChanged(); }
}
void ClipBank::setGainDb(const QString &id, double gainDb) {
    if (Entry *e = find(id)) {
        e->gainDb = std::clamp(gainDb, -40.0, 20.0);
        save(); emit clipsChanged();
    }
}
void ClipBank::setBypassDsp(const QString &id, bool bypass) {
    if (Entry *e = find(id)) { e->bypassDsp = bypass; save(); emit clipsChanged(); }
}
void ClipBank::setFkey(const QString &id, int fkey) {
    Entry *e = find(id);
    if (!e) return;
    fkey = std::clamp(fkey, 0, 12);
    if (fkey > 0) {                          // an F-key is unique — steal it
        for (auto &o : clips_)
            if (&o != e && o.fkey == fkey) o.fkey = 0;
    }
    e->fkey = fkey;
    save();
    emit clipsChanged();
}

void ClipBank::remove(const QString &id) {
    for (int i = 0; i < clips_.size(); ++i) {
        if (clips_[i].id == id) {
            QFile::remove(absPath(clips_[i].file));
            clips_.removeAt(i);
            save();
            emit clipsChanged();
            return;
        }
    }
}

void ClipBank::setClipsDir(const QString &path) {
    if (path.isEmpty() || path == dir_) return;
    dir_ = QDir::cleanPath(path);
    QDir().mkpath(dir_);
    save();
    emit clipsDirChanged();
    emit clipsChanged();   // file paths in the model changed
}

std::shared_ptr<const std::vector<float>>
ClipBank::loadSamples(const QString &id) const {
    const Entry *e = find(id);
    if (!e) return {};
    WavData d;
    std::string err;
    if (!readWavMono(absPath(e->file).toStdString(), d, err) || d.mono.empty())
        return {};
    if (d.sampleRate != 48000)
        d.mono = resampleLinear(d.mono, d.sampleRate, 48000);
    return std::make_shared<const std::vector<float>>(std::move(d.mono));
}

// ---- persistence (QSettings; one JSON array of clip rows) ----

void ClipBank::save() const {
    QSettings s;
    s.beginGroup(QStringLiteral("clips"));
    s.setValue(QStringLiteral("dir"), dir_);
    s.setValue(QStringLiteral("nextSeq"), nextSeq_);
    QJsonArray arr;
    for (const auto &e : clips_) {
        QJsonObject o;
        o[QStringLiteral("id")]     = e.id;
        o[QStringLiteral("label")]  = e.label;
        o[QStringLiteral("file")]   = e.file;
        o[QStringLiteral("kind")]   = e.kind;
        o[QStringLiteral("fkey")]   = e.fkey;
        o[QStringLiteral("gainDb")] = e.gainDb;
        o[QStringLiteral("bypass")] = e.bypassDsp;
        o[QStringLiteral("durMs")]  = e.durationMs;
        o[QStringLiteral("epoch")]  = static_cast<double>(e.createdEpoch);
        arr.append(o);
    }
    s.setValue(QStringLiteral("entries"),
               QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    s.endGroup();
}

void ClipBank::load() {
    QSettings s;
    s.beginGroup(QStringLiteral("clips"));
    dir_     = s.value(QStringLiteral("dir"), defaultDir()).toString();
    // A folder saved on another OS (settings carried over from Windows:
    // "C:/Users/.../clips" is a RELATIVE path on macOS/Linux) can never be
    // used here — saving into it silently discarded every recording.  Reset
    // it to this platform's default and persist that.  An absolute folder is
    // kept even if it's unavailable right now (an unplugged drive): moving it
    // would hide the clips stored there; a failed save reports clipError.
    const bool foreignDir = !QDir::isAbsolutePath(dir_);
    if (foreignDir) {
        qWarning("[clips] clips folder \"%s\" is not a valid path on this "
                 "system — using %s", qPrintable(dir_), qPrintable(defaultDir()));
        dir_ = defaultDir();
    }
    nextSeq_ = std::max(1, s.value(QStringLiteral("nextSeq"), 1).toInt());
    clips_.clear();
    const auto doc = QJsonDocument::fromJson(
        s.value(QStringLiteral("entries")).toString().toUtf8());
    for (const auto v : doc.array()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id           = o.value(QStringLiteral("id")).toString();
        e.label        = o.value(QStringLiteral("label")).toString();
        e.file         = o.value(QStringLiteral("file")).toString();
        e.kind         = o.value(QStringLiteral("kind")).toInt(Voice);
        e.fkey         = o.value(QStringLiteral("fkey")).toInt(0);
        e.gainDb       = o.value(QStringLiteral("gainDb")).toDouble(0.0);
        e.bypassDsp    = o.value(QStringLiteral("bypass")).toBool(false);
        e.durationMs   = o.value(QStringLiteral("durMs")).toInt(0);
        e.createdEpoch = static_cast<qint64>(o.value(QStringLiteral("epoch")).toDouble(0));
        if (!e.id.isEmpty() && !e.file.isEmpty())
            clips_.append(e);
    }
    s.endGroup();
    if (foreignDir)
        save();
}

} // namespace lyra::tx

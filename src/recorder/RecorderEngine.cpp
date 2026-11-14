// Lyra-cpp — Session Recorder (#201), Stage 1: recording engine impl.

#include "recorder/RecorderEngine.h"
#include "recorder/WavStreamWriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <chrono>

namespace lyra::recorder {

namespace {

// User-writable default.  Never the install folder — a Start-Menu launch
// has cwd under Program Files, and QDir("") / a relative path lands there
// (not writable without elevation → Rec appears to "do nothing").
QString defaultRecordRoot()
{
    const QString home =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!home.isEmpty())
        return QDir::cleanPath(QDir(home).filePath(QStringLiteral("Lyra/Recordings")));
    const QString app =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!app.isEmpty())
        return QDir::cleanPath(QDir(app).filePath(QStringLiteral("Recordings")));
    const QString tmp =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir::cleanPath(QDir(tmp).filePath(QStringLiteral("Lyra/Recordings")));
}

bool pathUnderInstallDir(const QString &path)
{
    if (path.isEmpty()) return false;
    const QString abs  = QFileInfo(path).absoluteFilePath();
    const QString exe  = QFileInfo(QCoreApplication::applicationDirPath())
                             .absoluteFilePath();
    if (!exe.isEmpty() &&
        abs.startsWith(exe, Qt::CaseInsensitive))
        return true;
#ifdef Q_OS_WIN
    const QString pf  = QDir::toNativeSeparators(
        qEnvironmentVariable("ProgramFiles"));
    const QString pf86 = QDir::toNativeSeparators(
        qEnvironmentVariable("ProgramFiles(x86)"));
    const QString native = QDir::toNativeSeparators(abs);
    if (!pf.isEmpty() && native.startsWith(pf, Qt::CaseInsensitive))
        return true;
    if (!pf86.isEmpty() && native.startsWith(pf86, Qt::CaseInsensitive))
        return true;
#endif
    return false;
}

bool pathUnusable(const QString &path)
{
    if (path.isEmpty()) return true;
    if (QDir::isRelativePath(path)) return true;
    if (pathUnderInstallDir(path)) return true;
    return false;
}

}  // namespace

// ── Lock-free SPSC float ring (audio thread → writer thread) ──────────────
class RecorderEngine::Fifo {
public:
    explicit Fifo(size_t capacityFloats)
        : cap_(nextPow2(std::max<size_t>(capacityFloats, 2))),
          mask_(cap_ - 1), buf_(cap_) {}

    // Producer (audio thread).  Returns floats accepted (rest dropped).
    size_t push(const float *p, size_t n) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t freeN = cap_ - (head - tail);
        const size_t w = std::min(n, freeN);
        for (size_t i = 0; i < w; ++i) buf_[(head + i) & mask_] = p[i];
        head_.store(head + w, std::memory_order_release);
        return w;
    }

    // Consumer (writer thread).  Returns floats copied out.
    size_t pop(float *out, size_t maxN) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t avail = head - tail;
        const size_t r = std::min(maxN, avail);
        for (size_t i = 0; i < r; ++i) out[i] = buf_[(tail + i) & mask_];
        tail_.store(tail + r, std::memory_order_release);
        return r;
    }

    size_t available() const {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_relaxed);
    }
    void clear() {                 // GUI thread, between sessions (no peers)
        tail_.store(head_.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
    }

private:
    static size_t nextPow2(size_t v) { size_t p = 1; while (p < v) p <<= 1; return p; }
    const size_t cap_, mask_;
    std::vector<float> buf_;
    std::atomic<size_t> head_{0}, tail_{0};
};

// ── ctor / dtor ───────────────────────────────────────────────────────────
RecorderEngine::RecorderEngine(QObject *parent) : QObject(parent) {
    loadConfig();
    // ~2.7 s of stereo @48k of headroom — the writer drains far faster than
    // this fills, so it only ever matters across a transient stall.
    fifo_ = std::make_unique<Fifo>(size_t(1) << 19);   // 512 Ki floats

    tick_ = new QTimer(this);
    connect(tick_, &QTimer::timeout, this, [this] {
        if (writeError_.load(std::memory_order_acquire)) {
            emit error(tr("A file-write error occurred — recording stopped."));
            stop();
            return;
        }
        const qint64 ms = clock_.elapsed();
        emit elapsed(ms);
        if (cfg_.hardLimitMin > 0 && ms >= qint64(cfg_.hardLimitMin) * 60000) {
            stop();   // hard-limit auto-stop is expected, not an error
        }
    });

    snapTimer_ = new QTimer(this);
    connect(snapTimer_, &QTimer::timeout, this, [this] { emit snapshotDue(); });
}

RecorderEngine::~RecorderEngine() {
    if (recording_) stop();
}

// ── Config ────────────────────────────────────────────────────────────────
void RecorderEngine::loadConfig() {
    QSettings s;
    cfg_.path            = s.value("recorder/path").toString();
    cfg_.recordTx        = s.value("recorder/recordTx", false).toBool();
    cfg_.snapshotsOn     = s.value("recorder/snapshotsOn", true).toBool();
    cfg_.snapshotsPerMin = s.value("recorder/snapshotsPerMin", 5.0).toDouble();
    cfg_.splitMinutes    = s.value("recorder/splitMinutes", 0).toInt();
    cfg_.splitMB         = s.value("recorder/splitMB", 0).toInt();
    cfg_.hardLimitMin    = s.value("recorder/hardLimitMin", 10).toInt();
    cfg_.capMB           = s.value("recorder/capMB", qint64(5120)).toLongLong();
    cfg_.autoPrune       = s.value("recorder/autoPrune", false).toBool();
    cfg_.audioRateHz     = s.value("recorder/audioRateHz", 48000).toInt();
    if (cfg_.audioRateHz <= 0) cfg_.audioRateHz = 48000;
    if (cfg_.snapshotsPerMin < 0.1) cfg_.snapshotsPerMin = 0.1;
    // Migrate empty / relative / Program Files roots to the writable default.
    if (pathUnusable(cfg_.path)) {
        cfg_.path = defaultRecordRoot();
        s.setValue("recorder/path", cfg_.path);
    }
}

void RecorderEngine::saveConfig() const {
    QSettings s;
    s.setValue("recorder/path", cfg_.path);
    s.setValue("recorder/recordTx", cfg_.recordTx);
    s.setValue("recorder/snapshotsOn", cfg_.snapshotsOn);
    s.setValue("recorder/snapshotsPerMin", cfg_.snapshotsPerMin);
    s.setValue("recorder/splitMinutes", cfg_.splitMinutes);
    s.setValue("recorder/splitMB", cfg_.splitMB);
    s.setValue("recorder/hardLimitMin", cfg_.hardLimitMin);
    s.setValue("recorder/capMB", cfg_.capMB);
    s.setValue("recorder/autoPrune", cfg_.autoPrune);
    s.setValue("recorder/audioRateHz", cfg_.audioRateHz);
}

void RecorderEngine::setConfig(const RecorderConfig &c) {
    cfg_ = c;
    if (cfg_.audioRateHz <= 0) cfg_.audioRateHz = 48000;
    if (cfg_.snapshotsPerMin < 0.1) cfg_.snapshotsPerMin = 0.1;
    saveConfig();
}

QString RecorderEngine::recordRoot() const {
    if (!pathUnusable(cfg_.path)) return cfg_.path;
    return defaultRecordRoot();
}

// ── paths ─────────────────────────────────────────────────────────────────
QString RecorderEngine::audioName(int index) const {
    return QStringLiteral("audio_%1.wav").arg(index, 3, 10, QLatin1Char('0'));
}
QString RecorderEngine::audioPath(int index) const {
    return QDir(sessionDir_).filePath(audioName(index));
}

bool RecorderEngine::ensureRootWritable(QString *err) const {
    const QString root = recordRoot();
    if (!QDir().mkpath(root)) {
        if (err) *err = tr("Couldn't create the recordings folder:\n%1").arg(root);
        return false;
    }
    if (!QFileInfo(root).isWritable()) {
        if (err) *err = tr("The recordings folder isn't writable:\n%1").arg(root);
        return false;
    }
    return true;
}

// ── storage size ────────────────────────────────────────────────────────────
qint64 RecorderEngine::recordingsSizeBytes() const {
    const QString root = recordRoot();
    if (!QDir(root).exists()) return 0;
    qint64 total = 0;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
    return total;
}

// ── start / stop ────────────────────────────────────────────────────────────
bool RecorderEngine::start(qint64 freqHz, const QString &mode) {
    if (recording_.load()) return true;

    // Don't record silence: refuse unless the radio is actually streaming.
    if (canRecord_ && !canRecord_()) {
        emit error(tr("Start the radio (▶ Start) before recording — there's no "
                      "receive audio while the stream is stopped."));
        return false;
    }

    QString err;
    if (!ensureRootWritable(&err)) { emit error(err); return false; }

    if (cfg_.capMB > 0) {
        const qint64 cap = cfg_.capMB * 1024LL * 1024LL;
        if (recordingsSizeBytes() >= cap) {
            // Auto-prune-oldest is an opt-in Stage-later hook; for now refuse
            // rather than silently delete the operator's recordings.
            emit error(tr("Recordings are at the %1 MB storage limit — free "
                          "space or raise the cap in Settings → Recording.")
                           .arg(cfg_.capMB));
            return false;
        }
    }

    // Session folder: 2026-07-11_143210_14074kHz_USB
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    const QString fk = QString::number(freqHz / 1000);
    QString safeMode = mode;
    safeMode.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")));
    if (safeMode.isEmpty()) safeMode = QStringLiteral("RX");
    const QString name = QStringLiteral("%1_%2kHz_%3").arg(stamp, fk, safeMode);
    sessionDir_ = QDir(recordRoot()).filePath(name);
    if (!QDir().mkpath(sessionDir_)) {
        emit error(tr("Couldn't create the session folder:\n%1").arg(sessionDir_));
        return false;
    }

    startFreqHz_  = freqHz;
    startMode_    = mode;
    fileChannels_ = cfg_.recordTx ? 2 : 1;
    fileIndex_    = 1;
    framesInFile_ = 0;
    audioFiles_.clear();
    { QMutexLocker lk(&snapMutex_); snapIndex_ = 0; snapshots_ = QJsonArray(); }

    wav_ = std::make_unique<WavStreamWriter>();
    if (!wav_->open(audioPath(1).toStdString(), fileChannels_, cfg_.audioRateHz)) {
        emit error(tr("Couldn't open the recording file in:\n%1").arg(sessionDir_));
        wav_.reset();
        return false;
    }

    fifo_->clear();
    writeError_.store(false);
    running_.store(true, std::memory_order_release);
    clock_.start();
    recording_.store(true, std::memory_order_release);

    writer_ = std::thread(&RecorderEngine::writerLoop, this);

    tick_->start(1000);
    if (cfg_.snapshotsOn) {
        const int ivl = std::max(1, int(60000.0 / cfg_.snapshotsPerMin));
        snapTimer_->start(ivl);
        // First PNG at Rec, not after a full interval (5/min used to wait 12 s).
        emit snapshotDue();
    }

    emit recordingChanged(true);
    return true;
}

void RecorderEngine::stop() {
    if (!recording_.load()) return;

    recording_.store(false, std::memory_order_release);   // gate feedAudio first
    tick_->stop();
    snapTimer_->stop();

    running_.store(false, std::memory_order_release);
    if (writer_.joinable()) writer_.join();               // drains + closes wav_
    wav_.reset();

    finalizeManifest();

    emit recordingChanged(false);
    emit sessionFinished(sessionDir_);
}

// ── audio in (RT-safe) ──────────────────────────────────────────────────────
void RecorderEngine::feedAudio(const float *in, int frames, int channels) {
    if (!recording_.load(std::memory_order_acquire) || !in || frames <= 0) return;
    if (channels == fileChannels_) {
        fifo_->push(in, size_t(frames) * size_t(channels));
        return;
    }
    // Convert incoming layout → file layout in fixed chunks (no allocation).
    constexpr int kChunk = 2048;                 // frames
    float tmp[kChunk * 2];
    int done = 0;
    while (done < frames) {
        const int n = std::min(kChunk, frames - done);
        if (fileChannels_ == 2 && channels == 1) {           // mono → stereo
            for (int i = 0; i < n; ++i) {
                const float s = in[done + i];
                tmp[2 * i] = s; tmp[2 * i + 1] = s;
            }
            fifo_->push(tmp, size_t(n) * 2);
        } else if (fileChannels_ == 1 && channels == 2) {    // stereo → mono
            for (int i = 0; i < n; ++i)
                tmp[i] = 0.5f * (in[2 * (done + i)] + in[2 * (done + i) + 1]);
            fifo_->push(tmp, size_t(n));
        } else {                                             // fallback: ch0
            for (int i = 0; i < n; ++i) {
                const float s = in[size_t(done + i) * size_t(channels)];
                for (int c = 0; c < fileChannels_; ++c) tmp[i * fileChannels_ + c] = s;
            }
            fifo_->push(tmp, size_t(n) * size_t(fileChannels_));
        }
        done += n;
    }
}

void RecorderEngine::feedAudioDoubles(const double *in, int frames, int channels) {
    if (!recording_.load(std::memory_order_acquire) || !in || frames <= 0) return;
    // Convert doubles → float in a fixed stack chunk, then hand each chunk to
    // feedAudio (which does the channel-layout mapping).  No allocation.
    constexpr int kChunk = 2048;                 // frames
    float tmp[kChunk * 2];
    const int ch = std::min(channels, 2);
    int done = 0;
    while (done < frames) {
        const int n = std::min(kChunk, frames - done);
        const int m = n * ch;
        const double *src = in + size_t(done) * size_t(ch);
        for (int i = 0; i < m; ++i) tmp[i] = float(src[i]);
        feedAudio(tmp, n, ch);
        done += n;
    }
}

// ── QML control ──────────────────────────────────────────────────────────────
void RecorderEngine::setContextProvider(std::function<QPair<qint64, QString>()> fn) {
    ctxProvider_ = std::move(fn);
}

void RecorderEngine::toggle() {
    if (recording_.load(std::memory_order_acquire)) {
        stop();
        return;
    }
    qint64 freqHz = 0;
    QString mode;
    if (ctxProvider_) {
        const QPair<qint64, QString> ctx = ctxProvider_();
        freqHz = ctx.first;
        mode   = ctx.second;
    }
    start(freqHz, mode);
}

void RecorderEngine::setSnapshotsOn(bool on) {
    if (cfg_.snapshotsOn == on) return;
    cfg_.snapshotsOn = on;
    saveConfig();
    // Live-toggle the snapshot cadence if a session is already running.
    if (recording_.load(std::memory_order_acquire)) {
        if (on) {
            const int ivl = std::max(1, int(60000.0 / cfg_.snapshotsPerMin));
            snapTimer_->start(ivl);
        } else {
            snapTimer_->stop();
        }
    }
    emit snapshotsOnChanged(on);
}

void RecorderEngine::setSnapshotsPerMin(int perMin) {
    perMin = std::clamp(perMin, 1, 30);
    if (int(cfg_.snapshotsPerMin + 0.5) == perMin) return;
    cfg_.snapshotsPerMin = perMin;
    saveConfig();
    // Re-arm the running cadence at the new rate.
    if (recording_.load(std::memory_order_acquire) && cfg_.snapshotsOn) {
        const int ivl = std::max(1, int(60000.0 / cfg_.snapshotsPerMin));
        snapTimer_->start(ivl);
    }
    emit snapshotsPerMinChanged(perMin);
}

// ── writer thread ────────────────────────────────────────────────────────────
void RecorderEngine::writerLoop() {
    std::vector<float> buf(65536);
    const size_t maxPop = (buf.size() / size_t(fileChannels_)) * size_t(fileChannels_);
    const qint64 rate = cfg_.audioRateHz;
    const qint64 splitFrames =
        cfg_.splitMinutes > 0 ? qint64(cfg_.splitMinutes) * 60 * rate : 0;
    const qint64 splitBytes =
        cfg_.splitMB > 0 ? qint64(cfg_.splitMB) * 1024 * 1024 : 0;

    while (running_.load(std::memory_order_acquire) || fifo_->available() > 0) {
        const size_t n = fifo_->pop(buf.data(), maxPop);
        if (n == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
            continue;
        }
        if (!wav_->write(buf.data(), int(n / size_t(fileChannels_)))) {
            writeError_.store(true, std::memory_order_release);
            break;
        }
        framesInFile_ += qint64(n / size_t(fileChannels_));

        if ((splitFrames && framesInFile_ >= splitFrames) ||
            (splitBytes && qint64(wav_->dataBytes()) >= splitBytes)) {
            rollFile();
            if (writeError_.load(std::memory_order_acquire)) break;
        }
    }

    // Record and close the final (still-open) file.
    if (wav_ && wav_->isOpen()) {
        audioFiles_.push_back({ audioName(fileIndex_), wav_->sampleRate(),
                                wav_->channels(), qint64(wav_->frames()) });
        wav_->close();
    }
}

void RecorderEngine::rollFile() {
    if (!wav_) return;
    audioFiles_.push_back({ audioName(fileIndex_), wav_->sampleRate(),
                            wav_->channels(), qint64(wav_->frames()) });
    wav_->close();
    ++fileIndex_;
    framesInFile_ = 0;
    if (!wav_->open(audioPath(fileIndex_).toStdString(), fileChannels_,
                    cfg_.audioRateHz)) {
        writeError_.store(true, std::memory_order_release);
    }
}

// ── snapshots (Stage-2 hooks) ────────────────────────────────────────────────
QString RecorderEngine::reserveSnapshotFile() {
    QMutexLocker lk(&snapMutex_);
    ++snapIndex_;
    return QDir(sessionDir_).filePath(
        QStringLiteral("snap_%1.png").arg(snapIndex_, 4, 10, QLatin1Char('0')));
}

void RecorderEngine::noteSnapshot(const QString &absPngPath, qint64 freqHz,
                                  const QString &mode) {
    const qint64 offsetSec = recording_.load() ? clock_.elapsed() / 1000 : 0;
    QJsonObject o;
    o["file"]      = QFileInfo(absPngPath).fileName();
    o["offsetSec"] = double(offsetSec);
    o["freqHz"]    = double(freqHz);
    o["mode"]      = mode;
    QMutexLocker lk(&snapMutex_);
    snapshots_.append(o);
}

// ── manifest ─────────────────────────────────────────────────────────────────
void RecorderEngine::finalizeManifest() {
    qint64 totalFrames = 0;
    QJsonArray audio;
    for (const auto &e : audioFiles_) {
        totalFrames += e.frames;
        QJsonObject a;
        a["file"]     = e.name;
        a["rateHz"]   = e.rate;
        a["channels"] = e.channels;
        a["frames"]   = double(e.frames);
        audio.append(a);
    }
    const qint64 rate = cfg_.audioRateHz > 0 ? cfg_.audioRateHz : 48000;

    QJsonObject root;
    root["created"]     = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["durationSec"] = double(totalFrames / rate);
    root["source"]      = cfg_.recordTx ? QStringLiteral("rx_tx")
                                        : QStringLiteral("rx");
    root["freqHz"]      = double(startFreqHz_);
    root["mode"]        = startMode_;
    root["audio"]       = audio;
    { QMutexLocker lk(&snapMutex_); root["snapshots"] = snapshots_; }

    QFile f(QDir(sessionDir_).filePath(QStringLiteral("session.json")));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
    }
}

} // namespace lyra::recorder

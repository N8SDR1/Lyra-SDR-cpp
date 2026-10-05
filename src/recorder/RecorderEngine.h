// Lyra-cpp — Session Recorder (#201), Stage 1: headless recording engine.
//
// Captures RX (and, when recordTx, TX-monitor) audio to a streaming WAV,
// auto-splits at X minutes / X MB, enforces a hard time limit, and writes a
// timestamped session folder + session.json manifest.  Snapshot cadence +
// manifest snapshot entries are driven here too (the actual framebuffer grab
// lives in the UI layer, Stage 2 — the engine only emits snapshotDue() and
// records the metadata, so it stays free of any rendering dependency).
//
// THREADING / REAL-TIME CONTRACT (load-bearing — see RECORDER_DESIGN.md §3/§6):
//   * feedAudio() is called on the AUDIO THREAD and must never block or do
//     disk I/O — it only pushes into a lock-free SPSC ring.
//   * A dedicated writer thread drains the ring and does all file I/O.
//   * All Qt signals are emitted from the engine's (GUI) thread only.
//
// Config lives in QSettings under "recorder/*" (RECORDER_DESIGN.md §7).

#pragma once

#include <QElapsedTimer>
#include <QJsonArray>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

class QTimer;

namespace lyra::recorder {

class WavStreamWriter;

struct RecorderConfig {
    QString path;                 // record root ("" = ~/Lyra/Recordings)
    bool    recordTx     = false; // false = RX only; true = RX(L)+TX(R) stereo
    bool    snapshotsOn  = true;  // capture pan/waterfall snapshots
    double  snapshotsPerMin = 5.0;
    int     splitMinutes = 0;     // 0 = off
    int     splitMB      = 0;     // 0 = off
    int     hardLimitMin = 10;    // 0 = off (auto-stop cap)
    qint64  capMB        = 5120;  // 0 = off (storage cap on the root)
    bool    autoPrune    = false; // opt-in ring-delete of oldest sessions
    int     audioRateHz  = 48000;
    // videoFormat is fixed to "mp4" in v1 (MKV deferred) — no field yet.
};

class RecorderEngine : public QObject {
    Q_OBJECT
    // QML-facing state (the Recorder panel + status chip bind to this).
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    // Live elapsed (ms) while recording — the panel timer binds to this and
    // updates on the ~1 Hz elapsed() tick.
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY elapsed)
    // Master snapshot on/off (audio-only when off).  The panel toggle binds
    // here; persisted to QSettings on change.
    Q_PROPERTY(bool snapshotsOn READ snapshotsOn WRITE setSnapshotsOn
               NOTIFY snapshotsOnChanged)
    // Snapshot rate (per minute) — the panel stepper + Settings both bind here.
    Q_PROPERTY(int snapshotsPerMin READ snapshotsPerMin WRITE setSnapshotsPerMin
               NOTIFY snapshotsPerMinChanged)
public:
    explicit RecorderEngine(QObject *parent = nullptr);
    ~RecorderEngine() override;

    // ── Config ──────────────────────────────────────────────────────────
    void           loadConfig();                 // from QSettings
    void           saveConfig() const;           // to QSettings
    RecorderConfig config() const { return cfg_; }
    void           setConfig(const RecorderConfig &c);

    // Resolve the effective record root (config.path or the default folder).
    QString recordRoot() const;

    // ── State ───────────────────────────────────────────────────────────
    bool    isRecording() const { return recording_; }
    bool    snapshotsOn() const { return cfg_.snapshotsOn; }
    int     snapshotsPerMin() const { return int(cfg_.snapshotsPerMin + 0.5); }
    QString sessionDir()  const { return sessionDir_; }
    qint64  elapsedMs()   const { return recording_ ? clock_.elapsed() : 0; }

    // The panel-facing snapshot toggle (persists + live-updates the running
    // snapshot timer if a session is active).
    void    setSnapshotsOn(bool on);
    void    setSnapshotsPerMin(int perMin);

    // Panel "Settings…" shortcut — emits settingsRequested() for MainWindow to
    // open Settings → Recording.  Keeps the panel decoupled from the dialog.
    Q_INVOKABLE void requestSettings() { emit settingsRequested(); }

    // QML control: start (reading freq/mode from the context provider) if idle,
    // else stop.  The panel REC/⏹ button and the status chip both call this.
    Q_INVOKABLE void toggle();

    // MainWindow installs a provider that returns the CURRENT (freqHz, mode) at
    // start time — so toggle()/start() stamp the folder + manifest correctly
    // without the engine reaching into the radio.  Set once at construction.
    void setContextProvider(std::function<QPair<qint64, QString>()> fn);

    // MainWindow installs a probe that returns true only while the radio is
    // streaming — start() refuses (and emits error) otherwise, so we never
    // record a silent WAV with the stream stopped.
    void setCanRecordProbe(std::function<bool()> fn) { canRecord_ = std::move(fn); }

    // Total bytes currently under the record root (for the storage-cap UI).
    qint64  recordingsSizeBytes() const;

    // ── Lifecycle ───────────────────────────────────────────────────────
    // Start a session.  freqHz/mode stamp the folder name + manifest.
    // Returns false (and emits error) on over-cap / unwritable path.
    bool start(qint64 freqHz, const QString &mode);
    void stop();

    // RT-safe (audio thread).  interleaved float [-1,+1]; channels 1 or 2.
    void feedAudio(const float *interleaved, int frames, int channels);

    // RT-safe doubles variant — the WdspEngine RX-record tap hands post-RX-DSP
    // audio as interleaved doubles (stereo-dup, L==R).  Converts to float in a
    // fixed stack chunk (no allocation) and feeds feedAudio().
    void feedAudioDoubles(const double *interleaved, int frames, int channels);

    // ── Snapshots (Stage 2 hooks) ───────────────────────────────────────
    // Reserve the next numbered PNG path in the session folder (does not
    // create the file).  The UI grabber saves its QImage there, then calls
    // noteSnapshot() to record the manifest entry.  Safe from any thread.
    QString reserveSnapshotFile();
    void    noteSnapshot(const QString &absPngPath, qint64 freqHz,
                         const QString &mode);

signals:
    void recordingChanged(bool on);
    void elapsed(qint64 ms);          // ~1 Hz while recording (for the chip)
    void snapshotDue();               // Stage-2 UI grabs a frame on this
    void snapshotsOnChanged(bool on);
    void snapshotsPerMinChanged(int perMin);
    void settingsRequested();         // panel gear → MainWindow opens Settings
    void error(const QString &msg);
    void sessionFinished(const QString &dir);

private:
    struct AudioFileEntry { QString name; int rate; int channels; qint64 frames; };
    class  Fifo;                      // lock-free SPSC float ring (in .cpp)

    void    writerLoop();             // writer thread body
    void    rollFile();               // close current WAV, open the next
    void    finalizeManifest();       // write session.json (GUI thread)
    bool    ensureRootWritable(QString *err) const;
    QString audioName(int index) const;   // "audio_001.wav"
    QString audioPath(int index) const;   // <sessionDir>/audio_001.wav

    RecorderConfig cfg_;

    // MainWindow-supplied "(freqHz, mode) right now" — read at start().
    std::function<QPair<qint64, QString>()> ctxProvider_;
    // MainWindow-supplied "is the radio streaming right now?" gate.
    std::function<bool()> canRecord_;

    std::atomic<bool> recording_{false};
    std::atomic<bool> running_{false};      // writer thread run flag
    std::atomic<bool> writeError_{false};

    QString sessionDir_;
    qint64  startFreqHz_ = 0;
    QString startMode_;
    QElapsedTimer clock_;

    std::unique_ptr<Fifo> fifo_;              // allocated once (ctor), reused
    std::unique_ptr<WavStreamWriter> wav_;    // writer-thread-owned during a run
    std::thread writer_;
    int   fileChannels_ = 1;

    // Writer-thread-owned (read on the GUI thread only after join()):
    int    fileIndex_   = 1;
    qint64 framesInFile_ = 0;
    std::vector<AudioFileEntry> audioFiles_;

    // Snapshots (GUI/render thread) — guarded because noteSnapshot may be
    // called from a render callback.
    mutable QMutex snapMutex_;
    int        snapIndex_ = 0;
    QJsonArray snapshots_;

    QTimer *tick_     = nullptr;      // ~1 Hz: elapsed + hard-limit + error poll
    QTimer *snapTimer_ = nullptr;     // snapshot cadence
};

} // namespace lyra::recorder

// Lyra-cpp — #89 Voice keyer, Stage B (B3): clip bank + persistence.
//
// The bank owns the collection of stored audio clips (voice-keyer messages +
// RX recordings — design doc §6).  Each clip is a 48 kHz mono float WAV on
// disk under %APPDATA%\Lyra\clips\ plus a metadata row (label, kind, F-key,
// per-clip gain trim, bypass-DSP default, duration).  Metadata persists as
// one JSON array in QSettings (clips/entries) — same idiom as the tuner
// memory (#188) and the CW macro bank (#176).
//
// This is the shared list behind BOTH surfaces (design doc §7): the floating
// "Voice Keyer" panel binds `clips` for its labelled/editable rows, and the
// Settings tab configures the storage folder.  Stage C's RX recorder writes
// new clips in via addFromSamples(); an operator can importWav() an external
// file.  The voice-keyer trigger path pulls samples via loadSamples() and
// hands them to the ClipRecorderPlayer injector (Stage A).
//
// Qt-based (QObject + QSettings + QML context property); the actual WAV read/
// write is the Qt-free WavIo (B2), so this layer is just model + I/O glue.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <memory>
#include <vector>

namespace lyra::tx {

class ClipBank : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
    Q_PROPERTY(QString clipsDir READ clipsDir NOTIFY clipsDirChanged)
public:
    enum Kind { Voice = 0, Rx = 1 };
    Q_ENUM(Kind)

    explicit ClipBank(QObject *parent = nullptr);

    // [{id,label,kind,fkey,gainDb,bypassDsp,durationMs,file}] for the QML Repeater.
    QVariantList clips() const;
    // Storage folder (created on first use).  Operator-overridable (setClipsDir).
    QString clipsDir() const { return dir_; }

    // Create a clip from recorded/48k-mono samples (Stage C recorder feeds this).
    // Writes a 32-bit-float WAV into clipsDir, adds the row, returns its id
    // ("" on write failure or if free space is below the guard threshold).
    QString addFromSamples(const QString &label, int kind,
                           const std::vector<float> &mono, int sampleRate = 48000);

    // Import an external WAV (any common format, normalised to 48 kHz mono).
    // Returns the new id, or "" on read/write failure.
    Q_INVOKABLE QString importWav(const QString &label, int kind,
                                  const QString &srcPath);

    // Editing (persisted immediately + emits clipsChanged).
    Q_INVOKABLE void setLabel(const QString &id, const QString &label);
    Q_INVOKABLE void setGainDb(const QString &id, double gainDb);
    Q_INVOKABLE void setBypassDsp(const QString &id, bool bypass);
    Q_INVOKABLE void setFkey(const QString &id, int fkey);   // 0 = unassigned
    Q_INVOKABLE void remove(const QString &id);              // deletes WAV + row

    // Change the storage folder (persisted); existing rows keep their files.
    Q_INVOKABLE void setClipsDir(const QString &path);

    // Read a clip's samples as mono float @ 48 kHz (resampled if needed) for
    // the player.  Returns an empty shared_ptr on any failure.
    std::shared_ptr<const std::vector<float>> loadSamples(const QString &id) const;

    // F-key lookup for the voice-keyer trigger (fn = 1..12).  "" if unassigned.
    QString idForFkey(int fkey) const;

    // Per-clip trims (for the player at play time).  0 / false if unknown.
    double gainDbOf(const QString &id) const;
    bool   bypassDspOf(const QString &id) const;

    // True if a clip with this id exists.
    Q_INVOKABLE bool contains(const QString &id) const;

signals:
    void clipsChanged();
    void clipsDirChanged();
    // A clip could not be saved (folder unusable, disk nearly full, or the
    // WAV write failed); `message` is operator-readable.
    void clipError(const QString &message);

private:
    struct Entry {
        QString id;
        QString label;
        QString file;            // filename within clipsDir (e.g. "clip_00001.wav")
        int     kind        = Voice;
        int     fkey        = 0; // 0 = unassigned
        double  gainDb      = 0.0;
        bool    bypassDsp   = false;
        int     durationMs  = 0;
        qint64  createdEpoch = 0;
    };

    void load();
    void save() const;
    QString defaultDir() const;
    QString absPath(const QString &file) const;
    QString makeId();
    QString addEntry(Entry e);          // append + save + emit, returns id
    Entry  *find(const QString &id);
    const Entry *find(const QString &id) const;
    static bool freeSpaceOk(const QString &dir);

    QString         dir_;
    int             nextSeq_ = 1;
    QVector<Entry>  clips_;
};

} // namespace lyra::tx

// Lyra — frequency memory bank (ported from old Lyra memory.py).
//
// Up to 20 presets, each { name, freq, mode, RX bandwidth, notes }.
// Persisted as a JSON array in QSettings ("memory/presets").  Surfaced
// two ways (like old Lyra): the "Mem" button on the Band panel's GEN row
// (left-click = recall menu, right-click = save current / manage) and the
// Settings -> Bands -> Memory tab (table + CSV import/export).

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace lyra::ipc { class HL2Stream; }

namespace lyra::ui {

class Prefs;

class MemoryStore : public QObject {
    Q_OBJECT
public:
    struct Preset {
        QString name;
        qint64  freq = 0;     // Hz (the RX / repeater-OUTPUT frequency)
        QString mode;
        int     rxBw = 0;     // Hz; 0 = leave RX BW at the mode default
        QString notes;
        // Repeater fields — recall programs SPLIT + CTCSS atop freq/mode:
        int     offsetHz    = 0;    // signed shift; TX (VFO B) = freq + offset.
                                    // 0 = simplex (recall turns SPLIT off).
        double  ctcssToneHz = 0.0;  // FM CTCSS uplink (TX encode) Hz; 0 = off.
        double  ctcssDlHz   = 0.0;  // Downlink (RX tone) Hz; 0 = same as UL / none.
        int     burstHz     = 0;    // 1750 = remember a 1750 Hz burst; 0 = none.
    };
    static constexpr int kMax = 20;
    static constexpr int kMaxName = 30;
    static constexpr int kMaxNotes = 80;

    MemoryStore(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                QObject *parent = nullptr);

    int count() const { return int(presets_.size()); }
    const QVector<Preset> &presets() const { return presets_; }

    // QML-facing: list of {name, freq, freqMHz, mode, rxBw, notes}.
    Q_INVOKABLE QVariantList list() const;
    // Tune to preset <index> (mode first, then freq, then optional BW).
    Q_INVOKABLE void recall(int index);
    // Store the current VFO (freq + mode) as a new preset; auto-name if
    // <name> is empty.  Returns false if the bank is full (20).
    Q_INVOKABLE bool addCurrent(const QString &name);
    // A sensible auto-name for the current VFO, e.g. "14.074 USB".
    Q_INVOKABLE QString currentAutoName() const;
    Q_INVOKABLE bool full() const { return presets_.size() >= kMax; }

    // Management (used by the Settings table).
    bool addPreset(const Preset &p);
    void setPreset(int index, const Preset &p);
    void remove(int index);
    void clearAll();

    // CSV: Name, Freq_Hz, Mode, RX_BW_Hz, Notes, Offset_Hz, CTCSS_Hz
    //      [, CTCSS_DL_Hz, Burst_Hz]. Older files still import.
    bool exportCsv(const QString &path) const;
    struct ImportResult { int added = 0; int skipped = 0; QString error; };
    ImportResult importCsv(const QString &path, bool replace);

signals:
    void changed();

private:
    void load();
    void save() const;

    Prefs                *prefs_  = nullptr;
    lyra::ipc::HL2Stream *stream_ = nullptr;
    QVector<Preset>       presets_;
};

} // namespace lyra::ui

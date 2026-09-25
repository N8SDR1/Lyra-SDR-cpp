// Lyra — per-band settings memory (ported from old Lyra's band_memory).
//
// Remembers, per amateur band, the operator's demod MODE, panadapter dB
// min/max, waterfall dB min/max, and manual LNA gain.  As you move across
// a band edge it restores that band's saved values; while you're on a
// band, any change is saved live to it.  The LNA value saved is the
// operator's MANUAL set point only (via lnaSetByOperator) — Auto-LNA's
// roaming is never captured; Auto simply roams from the restored set
// point.  Auto-scale on/off and RX bandwidth stay global / per-mode
// (matching old Lyra — bandwidth is already per-mode in Prefs).
//
// Pure C++: watches the stream's RX1 frequency, maps it to a band via the
// amateur band table, and drives the shared Prefs (which the panadapter /
// mode panels are bound to).  Persisted under QSettings "band_mem/<band>/*".

#pragma once

#include <QObject>
#include <QString>

namespace lyra::ipc { class HL2Stream; }

namespace lyra::ui {

class Prefs;

class BandMemory : public QObject {
    Q_OBJECT
public:
    BandMemory(Prefs *prefs, lyra::ipc::HL2Stream *stream,
               QObject *parent = nullptr);

    // Last frequency the operator was on in <band> (Hz), or 0 if none —
    // the Band panel buttons use this to return you to where you were.
    Q_INVOKABLE int freqFor(const QString &band) const;

    // Last SUB (RX2) frequency on <band>, or 0 if SUB has never parked there.
    // Independent of RX1 memory so dual-watch can keep two last-freqs.
    Q_INVOKABLE int freqForRx2(const QString &band) const;

    // Restore SUB demod for <band> (saved modeRx2, else the band default).
    // Does NOT touch RX1 mode, LNA, TX drive, or panadapter ranges.
    Q_INVOKABLE void applyRx2Band(const QString &band);

    // Band string for a frequency (Hz): "" / "40m" / "bc_49m" / "cb_11m".
    // Pure static helper — shared (e.g. SpotHole band-param derivation).
    static QString bandNameFor(int hz);

private:
    void onFreqChanged();          // band-edge crossing → restore new band
    void onRx2FreqChanged();       // remember SUB last-freq; no RX1 restore
    void saveCurrent();            // live-save the current band on a change
    void saveRx2Mode();            // live-save SUB mode on the current RX2 band
    void applyBand(const QString &band);
    static QString defaultModeFor(const QString &band);   // band-table default mode

    Prefs                *prefs_  = nullptr;
    lyra::ipc::HL2Stream *stream_ = nullptr;
    QString               currentBand_;     // "" = none/out-of-band
    QString               currentBandRx2_;  // SUB's band; independent of RX1
    bool                  applying_ = false; // guard: don't re-save during restore
    bool                  applyingRx2_ = false;
};

} // namespace lyra::ui

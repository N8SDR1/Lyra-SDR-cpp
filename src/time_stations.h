// Lyra — HF time-station "TIME" cycle (ported from old Lyra
// time_stations.py + the TIME button in panels.py).
//
// A single TIME button on the Band panel's GEN row (order: GEN1/2/3 ·
// TIME · Mem).  Left-click cycles to the next (station, frequency)
// entry across the whole table and advances a persisted index;
// right-click offers the full list grouped by station for a direct
// pick (which then sets the cycle to follow on).  Mode is per-station
// (AM for all but CHU, which is USB).  Stations are ordered by the
// operator's country/continent (from the callsign) so the most useful
// ones lead the cycle — a US op gets WWV first, etc.
//
// LF stations (JJY/MSF/DCF77/BPC) are deliberately omitted: the HL2
// can't receive below ~100 kHz.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace lyra::ipc { class HL2Stream; }

namespace lyra::ui {

class Prefs;

class TimeStations : public QObject {
    Q_OBJECT
public:
    TimeStations(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                 QObject *parent = nullptr);

    // Left-click: tune the current cycle entry, then advance + persist.
    // Returns a status line, e.g. "TIME: WWV 5.000 MHz AM — Fort Collins".
    Q_INVOKABLE QString cycleNext();
    // Right-click direct pick: <stationIndex>/<freqIndex> into the ORDERED
    // list (matches stations()); sets the cycle to continue after it.
    Q_INVOKABLE QString tuneEntry(int stationIndex, int freqIndex);
    // Reset the cycle so the next left-click lands on the first entry.
    Q_INVOKABLE void resetCycle();

    // Flat right-click menu model (ordered).  Each entry is either a
    // station header or a tunable frequency row, so a single flat
    // Repeater of MenuItems renders the whole list reliably:
    //   { header:true,  text:"WWV (Fort Collins, CO)" }
    //   { header:false, text:"   5.000 MHz AM", station:0, freq:1 }
    Q_INVOKABLE QVariantList menuEntries() const;

    // Flat list of tunable carriers for the frequency-calibration picker:
    //   { label:"WWV 10", freqHz:10000000, continent:"NA" }
    // one per (station, frequency), operator-country-first order.  `region`
    // is the Prefs band-plan region (US / IARU_R1 / IARU_R3 / NONE); it filters
    // to stations that region would actually hear (empty / NONE = show all).
    Q_INVOKABLE QVariantList calStations(const QString &region = QString()) const;

signals:
    // The jump is going ahead (not blocked by the TX band lockout): MainWindow
    // drops the GEN slot's ownership of the VFO, before the retune lands.
    void aboutToTune();

private:
    struct Station {
        QString id, name, country, continent, mode, notes;
        QVector<int> freqsKhz;   // low → high
    };
    QVector<Station> ordered() const;          // operator-country-first order
    QString operatorCountryIso() const;        // from Prefs callsign ("" = none)
    int cycleLength(const QVector<Station> &v) const;
    QString tune(const Station &s, int freqKhz);   // set mode + freq
    void saveIdx(int idx) const;
    int  loadIdx() const;

    Prefs                *prefs_  = nullptr;
    lyra::ipc::HL2Stream *stream_ = nullptr;
};

} // namespace lyra::ui

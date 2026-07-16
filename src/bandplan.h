// Lyra — amateur band-plan data (ported from old Lyra's band_plan.py).
//
// Per-region sub-band segments (CW / DIG / SSB / FM …), band edges, and
// "watering-hole" landmarks (FT8 / FT4 / WSPR / PSK + NCDXF beacons).
// Purely advisory — the HL2 is unlocked; this drives the panadapter's
// top-strip overlay and out-of-band hints.  Region comes from Prefs
// (US / IARU_R1 / IARU_R3 / NONE).  Exposed to QML as `BandPlan`.
//
// The DSP/protocol side never reads this — it's a navigation aid only.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace lyra::ui {

class Prefs;

// True when hz falls inside any amateur allocation for the given band-plan
// region + country (whole-band edges, so there are no in-band sub-segment
// gaps).  Region "NONE" falls back to the US / IARU Region-2 table so the
// test still rejects out-of-band frequencies rather than allowing everything.
// Used by the #175 waterfall-ID lockout to keep a courtesy ID inside the ham
// bands: 11m / CB and every other out-of-band segment (e.g. a SW-broadcast
// slot at 7.310) are rejected because they appear in no amateur table.
bool amateurBandContains(const QString &region, const QString &country,
                         double hz);

class BandPlan : public QObject {
    Q_OBJECT
    // Mirrors Prefs.bandPlanRegion; QML overlay bindings depend on this
    // so they re-query when the operator changes region.
    Q_PROPERTY(QString region READ region NOTIFY regionChanged)
public:
    explicit BandPlan(Prefs *prefs, QObject *parent = nullptr);

    QString region() const;

    // Segments intersecting the visible window [center±span/2].  Each
    // entry: { lo, hi (Hz, clipped), color (#RRGGBB), label }.
    Q_INVOKABLE QVariantList segments(double centerHz, double spanHz) const;
    // Band edges (low/high of each allocation) in view.
    // Each: { freq, name }.
    Q_INVOKABLE QVariantList edges(double centerHz, double spanHz) const;
    // US license-class phone-edge markers in view (Extra/Advanced/General
    // /Tech sub-band boundaries).  Each: { freq, label }.  US-only: returns
    // empty for every other region (class-dependent phone edges are a US/FCC
    // peculiarity; most countries gate license classes by power + whole-band
    // access, not sub-band edges).  Purely advisory — the operator is
    // responsible for knowing their own class limits.
    Q_INVOKABLE QVariantList classEdges(double centerHz, double spanHz) const;
    // Watering-hole landmarks in view, gated by group.
    // Each: { freq, label, mode, beacon (bool) }.
    Q_INVOKABLE QVariantList landmarks(double centerHz, double spanHz,
                                       bool showDigital, bool showBeacons) const;
    // NCDXF International Beacon Project: the station transmitting RIGHT
    // NOW on the given beacon frequency, as "<call> — <QTH>", or "" if
    // the freq isn't an NCDXF beacon.  Computed from the 18-station /
    // 10-second-slot / 3-minute rotation.  Used in the beacon tooltip.
    Q_INVOKABLE QString ncdxfStation(double freqHz) const;
    // Name of the amateur band containing freqHz ("40m"…), or "" if the
    // freq is outside every allocation in the current region (or NONE).
    Q_INVOKABLE QString bandContaining(double freqHz) const;
    // The 40 US CB channels visible in the window, each { freq, label }.
    // Empty unless the 11m/CB band is enabled (Settings → Hardware).
    Q_INVOKABLE QVariantList cbChannels(double centerHz, double spanHz) const;
    // True when freqHz falls within the 11m / CB band extent (the 40-channel
    // plan + a small guard for AM sidebands).  A pure range test — the caller
    // gates on cbBandEnabled.  Used to suppress the amateur out-of-band TX
    // warning when the operator is deliberately on the CB band they enabled.
    Q_INVOKABLE bool inCbBand(double freqHz) const;
    // Channel compliance for a CHANNELIZED band (US/Canada 60m — 5 discrete
    // 2.8 kHz USB channels), given the carrier and its emission window
    // [minF, maxF].  Returns:
    //   ""            ordinary band (no channel rule) / carrier not in a band
    //   "offchannel"  in the band span but in a between-channel gap
    //   "toowide"     on a channel but the emission exceeds the channel width
    // Channel windows come from the band data, so the 2.8 kHz limit tracks
    // whatever the region/country table defines.  Used by the TX warning.
    Q_INVOKABLE QString channelStatus(double carrierHz, double minFHz,
                                      double maxFHz) const;

signals:
    void regionChanged();

private:
    // Optional country override (Prefs.bandPlanCountry; "AUTO" = none).
    QString country() const;

    Prefs *prefs_ = nullptr;
};

} // namespace lyra::ui

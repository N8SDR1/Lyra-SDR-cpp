// Lyra — amateur band table (ported from old Lyra bands.py).
//
// US / IARU Region-2 defaults (N8SDR's station).  Each band carries its
// edges, a sensible default tune-to frequency, and a default mode that
// the Band panel applies on switch (mode is wired through once the C++
// build has mode-switching — for now the band buttons set frequency).
//
// `Bands` is a thin QObject exposed to QML (context property "Bands") so
// the Band panel can build its buttons from this single source of truth
// and highlight the band that contains the current RX frequency.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <array>
#include <vector>

namespace lyra {

struct Band {
    const char *name;     // "40m"
    int         low;      // band edge low (Hz)
    int         high;     // band edge high (Hz)
    int         defaultHz;// tune-to default (Hz)
    const char *mode;     // default mode ("LSB"/"USB"/"CWU"/…)
};

// The 11 HF/6m amateur bands, low → high.
const std::vector<Band> &amateurBands();

// The shortwave broadcast meter bands (120m … 13m bc), low → high.
const std::vector<Band> &broadcastBands();

// The 11m / CB Citizens Band (26.965–27.405 MHz).  Optional row, enabled
// via Settings → Hardware.
const std::vector<Band> &cbBands();

// Index of the amateur band whose [low,high] contains hz, else -1.
int bandIndexForFreq(int hz);
// Index of the broadcast band whose [low,high] contains hz, else -1.
int broadcastBandIndexForFreq(int hz);
// Index of the CB band whose [low,high] contains hz, else -1.
int cbBandIndexForFreq(int hz);

// PA Gain / Full Output / cap-learn / PWR-trim table: the 11 amateur
// bands (indices 0..10, same order as amateurBands) plus 11m as index 11.
// Band-panel highlight stays amateur-only; this is the TX-power table.
constexpr int kPaPowerBandCount   = 12;
constexpr int kPaPowerBandElevenM = 11;
const char *paPowerBandName(int idx);
int         paPowerBandIndexForFreq(int hz);
int         paPowerBandMidHz(int idx);
// Visual grid: 160…12, then 11m, then 10m, 6m. Values are storage indices
// (11m stays kPaPowerBandElevenM so 10m/6m QSettings slots do not shift).
std::array<int, kPaPowerBandCount> paPowerBandDisplayOrder();

// N2ADR HL2 filter-board OC pattern for amateur band <bandIndex>.
// Returns the 7-bit J16 open-collector pattern (pin N -> bit 1<<(N-1));
// the RX pattern adds the 3 MHz HPF (pin 7) on bands above 80m to reject
// strong out-of-band signals (e.g. a nearby AM broadcaster).  0 if the
// index is out of range (no pins driven = harmless).
int n2adrOcPattern(int bandIndex, bool transmitting);

// Human-readable lit-pin list for an OC pattern, e.g. "3.7" or "(none)".
QString ocPatternText(int pattern);

// Yaesu BCD band code for amateur band <bandIndex> (160m=1 … 10m=9,
// 6m=10).  60m has no standard code: 0 (amp bypasses) unless
// <sixtyAsForty>, then 3 (the 40m code, which most amps share).  0 for
// out-of-range / no-amp-band.
int bcdForBand(int bandIndex, bool sixtyAsForty);

namespace ui {

class Bands : public QObject {
    Q_OBJECT
public:
    explicit Bands(QObject *parent = nullptr) : QObject(parent) {}

    // Band list for a QML Repeater: each entry a map
    // {name, low, high, hz, mode}.
    Q_INVOKABLE QVariantList amateur() const;
    // Broadcast (SW) band list for a QML Repeater (same map shape).
    Q_INVOKABLE QVariantList broadcast() const;
    // CB / 11m band list for a QML Repeater (same map shape).
    Q_INVOKABLE QVariantList cb() const;
    // Active-band index for highlighting (-1 = outside all bands).
    Q_INVOKABLE int indexForFreq(double hz) const;
    // Active broadcast-band index for highlighting (-1 = none).
    Q_INVOKABLE int broadcastIndexForFreq(double hz) const;
    // Active CB-band index for highlighting (-1 = none).
    Q_INVOKABLE int cbIndexForFreq(double hz) const;
};

} // namespace ui
} // namespace lyra

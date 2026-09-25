// Lyra — amateur band table.  See bands.h.

#include "bands.h"

#include <QStringList>
#include <QVariantMap>

#include <array>

namespace lyra {

const std::vector<Band> &amateurBands() {
    // US / Region-2 defaults, matching old Lyra bands.py AMATEUR_BANDS.
    static const std::vector<Band> kBands = {
        {"160m",  1800000,  2000000,  1840000, "LSB"},
        {"80m",   3500000,  4000000,  3750000, "LSB"},
        {"60m",   5330000,  5407000,  5368000, "USB"},
        {"40m",   7000000,  7300000,  7200000, "LSB"},
        {"30m",  10100000, 10150000, 10136000, "CWU"},
        {"20m",  14000000, 14350000, 14200000, "USB"},
        {"17m",  18068000, 18168000, 18140000, "USB"},
        {"15m",  21000000, 21450000, 21200000, "USB"},
        {"12m",  24890000, 24990000, 24940000, "USB"},
        {"10m",  28000000, 29700000, 28400000, "USB"},
        {"6m",   50000000, 54000000, 50125000, "USB"},
    };
    return kBands;
}

int bandIndexForFreq(int hz) {
    const auto &b = amateurBands();
    for (size_t i = 0; i < b.size(); ++i) {
        if (hz >= b[i].low && hz <= b[i].high) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const std::vector<Band> &broadcastBands() {
    // Shortwave broadcast meter bands, matching old Lyra BROADCAST_BANDS.
    static const std::vector<Band> kBc = {
        {"120m",  2300000,  2495000,  2400000, "AM"},
        {"90m",   3200000,  3400000,  3300000, "AM"},
        {"75m",   3900000,  4000000,  3950000, "AM"},
        {"60m",   4750000,  5060000,  4900000, "AM"},
        {"49m",   5900000,  6200000,  5975000, "AM"},
        {"41m",   7200000,  7450000,  7255000, "AM"},
        {"31m",   9400000,  9900000,  9580000, "AM"},
        {"25m",  11600000, 12100000, 11870000, "AM"},
        {"22m",  13570000, 13870000, 13700000, "AM"},
        {"19m",  15100000, 15830000, 15400000, "AM"},
        {"16m",  17480000, 17900000, 17690000, "AM"},
        {"13m",  21450000, 21850000, 21600000, "AM"},
    };
    return kBc;
}

int broadcastBandIndexForFreq(int hz) {
    const auto &b = broadcastBands();
    for (size_t i = 0; i < b.size(); ++i) {
        if (hz >= b[i].low && hz <= b[i].high) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const std::vector<Band> &cbBands() {
    // 11m / Citizens Band — US 40-channel allocation; default = Ch 19.
    static const std::vector<Band> kCb = {
        {"11m", 26965000, 27405000, 27185000, "AM"},
    };
    return kCb;
}

int cbBandIndexForFreq(int hz) {
    const auto &b = cbBands();
    for (size_t i = 0; i < b.size(); ++i) {
        if (hz >= b[i].low && hz <= b[i].high) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const char *paPowerBandName(int idx) {
    const auto &a = amateurBands();
    if (idx >= 0 && idx < static_cast<int>(a.size()))
        return a[static_cast<size_t>(idx)].name;
    if (idx == kPaPowerBandElevenM)
        return "11m";
    return "";
}

int paPowerBandIndexForFreq(int hz) {
    const int a = bandIndexForFreq(hz);
    if (a >= 0)
        return a;
    if (cbBandIndexForFreq(hz) >= 0)
        return kPaPowerBandElevenM;
    return -1;
}

int paPowerBandMidHz(int idx) {
    const auto &a = amateurBands();
    if (idx >= 0 && idx < static_cast<int>(a.size()))
        return (a[static_cast<size_t>(idx)].low + a[static_cast<size_t>(idx)].high) / 2;
    if (idx == kPaPowerBandElevenM) {
        const auto &cb = cbBands();
        if (!cb.empty())
            return (cb[0].low + cb[0].high) / 2;
    }
    return 0;
}

std::array<int, kPaPowerBandCount> paPowerBandDisplayOrder() {
    return {0, 1, 2, 3, 4, 5, 6, 7, 8,
            kPaPowerBandElevenM, 9, 10};
}

int n2adrOcPattern(int bandIndex, bool transmitting) {
    // (rx, tx) J16 pin bits per amateur band (index-aligned with
    // amateurBands()).  Ported from old Lyra hardware/oc.py N2ADR_PRESET.
    // pin N -> bit (1 << (N-1)).  Pin 7 (bit 0x40) = 3 MHz HPF, added on
    // RX above 80m.  Pins 1-6 select the per-band LPF.
    static constexpr int kRx[] = {
        0x01,        // 160m  pin1
        0x02 | 0x40, // 80m   pin2 + HPF
        0x04 | 0x40, // 60m   pin3 + HPF
        0x04 | 0x40, // 40m   pin3 + HPF
        0x08 | 0x40, // 30m   pin4 + HPF
        0x08 | 0x40, // 20m   pin4 + HPF
        0x10 | 0x40, // 17m   pin5 + HPF
        0x10 | 0x40, // 15m   pin5 + HPF
        0x20 | 0x40, // 12m   pin6 + HPF
        0x20 | 0x40, // 10m   pin6 + HPF
        0x00,        // 6m    pass-through (N2ADR doesn't cover 6m)
    };
    static constexpr int kTx[] = {
        0x01, 0x02, 0x04, 0x04, 0x08, 0x08,
        0x10, 0x10, 0x20, 0x20, 0x00,
    };
    constexpr int n = static_cast<int>(sizeof(kRx) / sizeof(kRx[0]));
    if (bandIndex < 0 || bandIndex >= n) {
        return 0;
    }
    return transmitting ? kTx[bandIndex] : kRx[bandIndex];
}

int bcdForBand(int bandIndex, bool sixtyAsForty) {
    // Yaesu BCD codes, index-aligned with amateurBands() (160m..6m).
    // 60m (index 2) = 0 (no standard code -> amp bypasses) unless the
    // operator opts to share the 40m filter.
    static constexpr int kBcd[] = {1, 2, 0, 3, 4, 5, 6, 7, 8, 9, 10};
    constexpr int n = static_cast<int>(sizeof(kBcd) / sizeof(kBcd[0]));
    if (bandIndex < 0 || bandIndex >= n) {
        return 0;
    }
    if (sixtyAsForty && bandIndex == 2) {   // 60m -> 40m code
        return 3;
    }
    return kBcd[bandIndex];
}

QString ocPatternText(int pattern) {
    QStringList pins;
    for (int i = 0; i < 7; ++i) {
        if (pattern & (1 << i)) {
            pins << QString::number(i + 1);
        }
    }
    return pins.isEmpty() ? QStringLiteral("(none)") : pins.join('.');
}

namespace ui {

QVariantList Bands::amateur() const {
    QVariantList out;
    for (const auto &b : amateurBands()) {
        QVariantMap m;
        m[QStringLiteral("name")] = QString::fromLatin1(b.name);
        m[QStringLiteral("low")]  = b.low;
        m[QStringLiteral("high")] = b.high;
        m[QStringLiteral("hz")]   = b.defaultHz;
        m[QStringLiteral("mode")] = QString::fromLatin1(b.mode);
        out.append(m);
    }
    return out;
}

QVariantList Bands::broadcast() const {
    QVariantList out;
    for (const auto &b : broadcastBands()) {
        QVariantMap m;
        m[QStringLiteral("name")] = QString::fromLatin1(b.name);
        m[QStringLiteral("low")]  = b.low;
        m[QStringLiteral("high")] = b.high;
        m[QStringLiteral("hz")]   = b.defaultHz;
        m[QStringLiteral("mode")] = QString::fromLatin1(b.mode);
        out.append(m);
    }
    return out;
}

int Bands::indexForFreq(double hz) const {
    return bandIndexForFreq(static_cast<int>(hz));
}

int Bands::broadcastIndexForFreq(double hz) const {
    return broadcastBandIndexForFreq(static_cast<int>(hz));
}

QVariantList Bands::cb() const {
    QVariantList out;
    for (const auto &b : cbBands()) {
        QVariantMap m;
        m[QStringLiteral("name")] = QString::fromLatin1(b.name);
        m[QStringLiteral("low")]  = b.low;
        m[QStringLiteral("high")] = b.high;
        m[QStringLiteral("hz")]   = b.defaultHz;
        m[QStringLiteral("mode")] = QString::fromLatin1(b.mode);
        out.append(m);
    }
    return out;
}

int Bands::cbIndexForFreq(double hz) const {
    return cbBandIndexForFreq(static_cast<int>(hz));
}

} // namespace ui
} // namespace lyra

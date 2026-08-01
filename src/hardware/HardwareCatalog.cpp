// Lyra — hardware model catalog data.  See HardwareCatalog.h.
// Every number transcribed from KD4YAL Thetis fork
// Console/clsHardwareSpecific.cs (2026-07 state); keep 1:1 when
// updating against upstream.

#include "hardware/HardwareCatalog.h"

namespace lyra::hardware {

namespace {

// PA gain rows shared by model groups (clsHardwareSpecific.cs
// DefaultPAGainsForBands switch grouping).
#define PA_CLASSIC  {41.0f, 41.2f, 41.3f, 41.3f, 41.0f, 40.5f, 39.9f, 38.8f, 38.8f, 38.8f, 38.8f}
#define PA_100      {50.0f, 50.5f, 50.5f, 50.0f, 49.5f, 48.5f, 48.0f, 47.5f, 46.5f, 42.0f, 43.0f}
#define PA_100D     {49.5f, 50.5f, 50.5f, 50.0f, 49.0f, 48.0f, 47.0f, 46.5f, 46.0f, 43.5f, 43.0f}
#define PA_7000_G2  {47.9f, 50.5f, 50.8f, 50.8f, 50.9f, 50.9f, 50.5f, 47.0f, 47.9f, 46.5f, 44.6f}
#define PA_NONE     {100.f, 100.f, 100.f, 100.f, 100.f, 100.f, 100.f, 100.f, 100.f, 100.f, 100.f}

// HPSDRModel ordinals (Thetis enums.cs / wire/RadioNet.h — identical).
enum { mHPSDR = 0, mHERMES, mANAN10, mANAN10E, mANAN100, mANAN100B,
       mANAN100D, mANAN200D, mORIONMKII, mANAN7000D, mANAN8000D,
       mANAN_G2, mANAN_G2_1K, mANVELINAPRO3, mHERMESLITE, mREDPITAYA };
// RESERVED upstream-Thetis additions: model ANAN_G2E = 16 (board
// HermesC10 = 20).  Do not reuse these ordinals.

// HPSDRHW board ordinals.
enum { bAtlas = 0, bHermes, bHermesII, bAngelia, bOrion, bOrionMKII,
       bHermesLite, bSaturn = 10 };

// Column legend:
//  key, display, model, board, protocols, adc, mkiiBpf, supply, lrSwap,
//  volts, amps, voltOff, voltSens, psPeakP2, meterOff, dispOff,
//  audioAmpP2, rx2Att, paHf[11], paVhf
const HardwareModelDescriptor kCatalog[] = {
    {"HPSDR",        "HPSDR (Atlas)",   mHPSDR,       bAtlas,     WireSupport::P1Only, 1, false, 33, true,
     false, false, 360.f, 120.f, 0.2899, 0.98f,      -2.1f,    false, true,  PA_CLASSIC, 56.2f},
    {"HERMES",       "Hermes",          mHERMES,      bHermes,    WireSupport::Both,   1, false, 33, true,
     false, false, 360.f, 120.f, 0.2899, 0.98f,      -2.1f,    false, false, PA_CLASSIC, 56.2f},
    {"ANAN-10",      "ANAN-10",         mANAN10,      bHermes,    WireSupport::Both,   1, false, 33, true,
     false, false, 360.f, 120.f, 0.2899, 0.98f,      -2.1f,    false, false, PA_CLASSIC, 56.2f},
    {"ANAN-10E",     "ANAN-10E",        mANAN10E,     bHermesII,  WireSupport::Both,   1, false, 33, true,
     false, false, 360.f, 120.f, 0.2899, 0.98f,      -2.1f,    false, false, PA_CLASSIC, 56.2f},
    {"ANAN-100",     "ANAN-100",        mANAN100,     bHermes,    WireSupport::Both,   1, false, 33, true,
     false, false, 360.f, 120.f, 0.2899, 0.98f,      -2.1f,    false, false, PA_100,     56.2f},
    {"ANAN-100B",    "ANAN-100B",       mANAN100B,    bHermesII,  WireSupport::Both,   1, false, 33, true,
     false, false, 360.f, 120.f, 0.2899, 0.98f,      -2.1f,    false, false, PA_100,     56.2f},
    {"ANAN-100D",    "ANAN-100D",       mANAN100D,    bAngelia,   WireSupport::Both,   2, false, 33, false,
     false, false, 360.f, 120.f, 0.2899, 0.98f,      -2.1f,    false, true,  PA_100D,    56.2f},
    {"ANAN-200D",    "ANAN-200D",       mANAN200D,    bOrion,     WireSupport::Both,   2, false, 50, false,
     false, false, 360.f, 120.f, 0.2899, 0.98f,      -2.1f,    false, true,  PA_100D,    56.2f},
    {"ORIONMKII",    "Orion MkII",      mORIONMKII,   bOrionMKII, WireSupport::Both,   2, true,  50, false,
     false, false, 360.f, 120.f, 0.2899, 4.841644f,  5.259f,   false, true,  PA_CLASSIC, 56.2f},
    {"ANAN-7000DLE", "ANAN-7000DLE",    mANAN7000D,   bOrionMKII, WireSupport::Both,   2, true,  50, false,
     true,  true,  340.f, 88.f,  0.2899, 4.841644f,  5.259f,   true,  true,  PA_7000_G2, 63.1f},
    {"ANAN-8000DLE", "ANAN-8000DLE",    mANAN8000D,   bOrionMKII, WireSupport::Both,   2, true,  50, false,
     true,  true,  360.f, 120.f, 0.2899, 4.841644f,  5.259f,   true,  true,  PA_100,     56.2f},
    {"ANAN-G2",      "ANAN-G2 (Saturn)", mANAN_G2,    bSaturn,    WireSupport::P2Only, 2, true,  50, false,
     true,  true,  0.001f, 66.23f, 0.6121, -4.476f,  -4.4005f, true,  true,  PA_7000_G2, 63.1f},
    {"ANAN-G2-1K",   "ANAN-G2-1K",      mANAN_G2_1K,  bSaturn,    WireSupport::P2Only, 2, true,  50, false,
     true,  true,  0.001f, 66.23f, 0.6121, -4.476f,  -4.4005f, true,  true,  PA_7000_G2, 63.1f},
    {"ANVELINA-PRO3","Anvelina-Pro3",   mANVELINAPRO3, bOrionMKII, WireSupport::Both,  2, true,  50, false,
     true,  true,  340.f, 88.f,  0.2899, 4.841644f,  5.259f,   true,  true,  PA_7000_G2, 63.1f},
    {"HERMES-LITE",  "Hermes Lite 2",   mHERMESLITE,  bHermesLite, WireSupport::P1Only, 1, false, 33, true,
     false, false, 360.f, 120.f, 0.2899, 0.98f,      -2.1f,    false, false, PA_NONE,    100.f},
    {"RED-PITAYA",   "Red Pitaya",      mREDPITAYA,   bOrionMKII, WireSupport::Both,   2, false, 50, false,
     true,  true,  340.f, 88.f,  0.2899, 4.841644f,  5.259f,   true,  true,  PA_7000_G2, 63.1f},
    // BrickSDR — Hermes-class Protocol-2 transceiver (discovers as board
    // "Hermes"; run as model HERMES in Thetis).  First-class Lyra key
    // instead of the pretend-it's-a-Hermes workaround.  Wire behaviour is
    // Hermes-class; filtering is the ONBOARD Apollo filter, so it emits
    // NO per-band Alex/OC words (bench-confirmed 2026-08-01: RX all-zero
    // across every band, TX a fixed band-independent assertion) — on this
    // (Jerry) tree that falls out automatically because p2ProfileForModel()
    // returns null for any non-G2 model, so setProfile(null) emits no Alex
    // words and TX stays RX-only.  Telemetry/PA constants are Hermes-class
    // placeholders pending Brick bench calibration.  Generic name: this ONE
    // row covers the P2 Hermes-class Brick line (Brick2 bench-validated).
    // Brick3 is Angelia/ANAN-100D-class (deskHPSDR ties it to ANAN-100D) --
    // a Brick3 uses the ANAN-100D model, NOT this Hermes-class row.
    {"BRICK-SDR",    "BrickSDR",        mHERMES,      bHermes,    WireSupport::P2Only, 1, false, 33, true,
     false, false, 360.f, 120.f, 0.2899, 0.98f,      -2.1f,    false, false, PA_CLASSIC, 56.2f},
};

#undef PA_CLASSIC
#undef PA_100
#undef PA_100D
#undef PA_7000_G2
#undef PA_NONE

constexpr int kCatalogCount =
    static_cast<int>(sizeof(kCatalog) / sizeof(kCatalog[0]));

} // namespace

const HardwareModelDescriptor *catalog(int *count) {
    if (count) *count = kCatalogCount;
    return kCatalog;
}

const HardwareModelDescriptor *modelByKey(const QString &key) {
    for (const auto &m : kCatalog)
        if (key.compare(QLatin1String(m.key), Qt::CaseInsensitive) == 0)
            return &m;
    // Back-compat: the P2 Brick model was briefly "BRICK-SDR2" before it
    // was generalized to "BRICK-SDR" (Brick2 = one variant of the
    // Hermes-class P2 Brick line).  Resolve saved profiles on the old key.
    if (key.compare(QLatin1String("BRICK-SDR2"), Qt::CaseInsensitive) == 0)
        return modelByKey(QStringLiteral("BRICK-SDR"));
    return nullptr;
}

const HardwareModelDescriptor *defaultModelForBoard(int hpsdrHw,
                                                    bool protocol2) {
    // Board → most common marketed model (Thetis leaves this to the
    // operator's comboRadioModel; these are the sensible presets).
    const char *key = nullptr;
    switch (hpsdrHw) {
        case bAtlas:      key = "HPSDR";        break;
        case bHermes:     key = "ANAN-100";     break;
        case bHermesII:   key = "ANAN-100B";    break;
        case bAngelia:    key = "ANAN-100D";    break;
        case bOrion:      key = "ANAN-200D";    break;
        case bOrionMKII:  key = "ANAN-7000DLE"; break;
        case bHermesLite: key = "HERMES-LITE";  break;
        case bSaturn:
        case bSaturn + 1: key = "ANAN-G2";      break;   // SaturnMKII
        default:          key = protocol2 ? "ANAN-G2" : "HERMES-LITE"; break;
    }
    return modelByKey(QLatin1String(key));
}

QStringList p2ModelKeys() {
    QStringList out;
    for (const auto &m : kCatalog)
        if (m.protocols != WireSupport::P1Only)
            out << QLatin1String(m.key);
    return out;
}

} // namespace lyra::hardware

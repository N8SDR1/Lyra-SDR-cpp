// Lyra — hardware model catalog (Layer 1 of the radio-profile stack).
//
// The data-table equivalent of Thetis's clsHardwareSpecific.cs: one
// immutable descriptor per MARKETED model, replacing the C# per-model
// switch blocks (Model setter, HasVolts/HasAmps, PSDefaultPeak,
// RXMeterCalbrationOffsetDefaults, DefaultPAGainsForBands,
// HasSteppedAttenuation, GetDefaultVoltCalibration…) with rows.
// Values are transcribed 1:1 from the KD4YAL Thetis fork
// (Console/clsHardwareSpecific.cs + enums.cs) — when a number here
// looks odd (0.001 volt offset on the G2), it's odd in Thetis too.
//
// Marketed model vs discovered board: several products share a
// gateware family (HPSDRHW) but need different PA tables, meter
// calibration, telemetry conversion and capability gates — exactly
// why Thetis separates comboRadioModel from the discovery board id.
// Discovery gives the BOARD; the operator (or a saved radio profile)
// picks the MODEL; defaultModelForBoard() supplies the sane default.
//
// Serialization: profiles reference models by the string `key`
// (Thetis's EnumModelToString strings — "ANAN-G2", "ANAN-7000DLE"…)
// NOT by enum ordinal, so the identifiers stay stable as models are
// added and a Thetis-database importer can map comboRadioModel
// directly.  Enum ordinals mirror this Thetis fork (…REDPITAYA=15);
// later upstream Thetis adds ANAN_G2E=16 and board HermesC10=20 —
// those ordinals are RESERVED here and must not be reused.
//
// Layers above this one (see docs/P2_HARDWARE_PROFILES_PLAN.md):
//   L2 RadioProfile (per-MAC saved device), L3 StationProfile,
//   L4 the existing TX/RX operating profiles (ProfileManager).

#pragma once

#include <QString>
#include <QStringList>

namespace lyra::hardware {

// Which wire protocols a model can run.
enum class WireSupport { P1Only, P2Only, Both };

struct HardwareModelDescriptor {
    const char *key;            // stable id + Thetis comboRadioModel string
    const char *displayName;

    int  model;                 // HPSDRModel ordinal (Thetis/RadioNet.h identical)
    int  expectedBoard;         // HPSDRHW ordinal expected from discovery
    WireSupport protocols;

    int  adcCount;              // NetworkIO.SetRxADC(n)
    bool mkiiBpf;               // OrionMkII/Saturn BPF board semantics
    int  adcSupply;             // cmaster.SetADCSupply units (33 or 50)
    bool lrAudioSwap;           // NetworkIO.LRAudioSwap(1) models

    bool hasVolts;              // supply-volts telemetry
    bool hasAmps;               // PA-current telemetry
    float voltOff;              // GetDefaultVoltCalibration()
    float voltSens;

    double psDefaultPeakP2;     // PSDefaultPeak under Protocol 2
                                // (Protocol 1 is 0.4072 for every model)

    float rxMeterOffset;        // RXMeterCalbrationOffsetDefaults
    float rxDisplayOffset;      // RXDisplayCalbrationOffsetDefauls

    bool hasAudioAmplifierP2;   // on-radio speaker amp (P2 only)
    bool rx2SteppedAtten;       // HasSteppedAttenuation(rx=2)

    // DefaultPAGainsForBands, HF rows 160/80/60/40/30/20/17/15/12/10/6 m
    // (PA "gains" are attenuations — 100 = no output) + the VHF default.
    float paGainsHf[11];
    float paGainVhf;
};

// The full catalog (Thetis's model list, this fork's ordering).
const HardwareModelDescriptor *catalog(int *count);

// Lookup by stable key ("ANAN-G2"); nullptr when unknown.
const HardwareModelDescriptor *modelByKey(const QString &key);

// Sane default model for a discovered board type (HPSDRHW ordinal) +
// wire protocol — e.g. Saturn board → ANAN-G2, HermesLite → HL2.
const HardwareModelDescriptor *defaultModelForBoard(int hpsdrHw,
                                                    bool protocol2);

// Keys of every model that can run Protocol 2 (for the Settings combo).
QStringList p2ModelKeys();

} // namespace lyra::hardware

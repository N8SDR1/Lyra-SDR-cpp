// Lyra — RadioCapabilities: per-family hardware capability descriptor.
//
// Multi-rig Stage 1 (config/capability layer).  See
// docs/architecture/multi_rig_design.md.
//
// This is PURE, INERT scaffolding for Stage 1: it defines the capability
// struct and a family-baseline lookup.  Nothing in the running app reads
// it yet — later stages (rig registry, namespacing, rig picker) consume
// it so a fresh rig profile gets family-correct defaults instead of a
// blank slate, and so UI/DSP branch on capabilities rather than a
// hard-coded "this is an HL2" assumption.
//
// Ground truth for the HL2 values below comes from the live lyra-cpp
// source (hl2_stream.h kLnaMinDb/kLnaMaxDb, hl2_discovery board-id map,
// the HL2 TX power model) — NOT ported from anywhere.
//
// A capability struct is a *family baseline*.  Per-UNIT facts that the
// family cannot fix on its own (notably hasOnboardAudioIO: a plain HL2
// and an HL2+ share board-id 6 but differ in whether the AK4951 codec /
// physical audio jacks are present) are refined by discovery /
// operator setting in a later stage.

#pragma once

#include <QString>

namespace lyra::rig {

// Radio hardware family.  Brick/ANAN entries are forward-looking — their
// capability values are stubbed until the Protocol 2 wire engine work
// confirms them on real hardware (see capabilitiesFor()).
enum class RadioFamily {
    Unknown,
    Hl2,      // Hermes Lite 2 / 2+   — HPSDR Protocol 1, discovery board-id 6
    AnanP1,   // Orion / older ANAN   — HPSDR Protocol 1, board-id 5/10
    AnanP2,   // ANAN G2 / P2 family  — HPSDR Protocol 2
    BrickP2,  // BrickSDR2 (~ANAN-10E)— HPSDR Protocol 2, 14-bit
};

// Where receive/transmit audio physically lives for this rig.
enum class AudioPath {
    PcSound,    // audio travels over the network to/from the PC soundcard / VAC
    RadioJack,  // onboard codec: physical mic-in + audio-out on the unit itself
};

// Inclusive front-end LNA gain span, dB.
struct GainRange {
    int minDb = 0;
    int maxDb = 0;
};

// Nominal TX power characterisation (family default; the operator's own
// power calibration — tx/maxOutputW etc. — is the live authority).
struct TxPowerModel {
    double ratedMaxW = 0.0;  // nominal PA rated output, watts
    int    driveSteps = 0;   // discrete drive resolution (HL2 = 16 coarse steps)
};

struct RadioCapabilities {
    RadioFamily family     = RadioFamily::Unknown;
    QString     familyName;              // human-facing, e.g. "Hermes Lite 2 / 2+"
    int         protocol   = 0;          // 1 = HPSDR Protocol 1, 2 = Protocol 2
    int         maxReceivers = 0;        // logical DDC / receiver count
                                         //   (discovery numRxs is authoritative per unit)
    int         adcBits    = 0;          // 12 (HL2) | 14 (Brick / ANAN-class)

    // Physical mic-in + audio-out jacks on the unit.
    //   base HL2 = false (all audio via the PC);
    //   HL2+ / Brick / ANAN = true (onboard codec jacks).
    // NOTE: within the HL2 family this varies per UNIT (base vs +), which
    // share board-id 6 — so this is the family BASELINE, refined by
    // detection / operator setting in a later stage.  Audio *config*
    // itself is machine-local / per-PC (see the design doc); this flag
    // only says whether the physical path exists.
    bool        hasOnboardAudioIO = false;
    AudioPath   defaultAudioPath  = AudioPath::PcSound;

    GainRange    lna;                    // front-end LNA gain span
    TxPowerModel txPower;

    // HL2 PureSignal needs the hardware mod (operator self-attestation);
    // ANAN G2-class PS is built into stock gateware.
    bool        puresignalRequiresMod = false;
};

// Family-baseline capabilities.  Pure lookup, no side effects, no I/O.
RadioCapabilities capabilitiesFor(RadioFamily family);

// Map an HPSDR discovery board-id (see the boardName switch in
// hl2_discovery.cpp) to a family.  board 6 = HermesLite (HL2/HL2+);
// 5 = Orion, 10 = OrionMKII (ANAN family, P1).  Protocol-2 rigs
// (Brick, ANAN G2) are identified by the P2 discovery path, not this
// P1 board-id, and are handled when that engine lands.
RadioFamily familyForBoardId(int boardId);

} // namespace lyra::rig

// Lyra — RadioCapabilities family-baseline lookup.  See the header.
//
// Multi-rig Stage 1: HL2 is fully populated from live source constants.
// Brick/ANAN are honest stubs — their real values land with the
// Protocol 2 wire engine, verified against hardware.  Until then they
// carry only the facts we can state safely (protocol number, ADC bits,
// onboard-audio presence per the operator) and leave the rest at
// conservative defaults, clearly not pretending to know a Brick's
// receiver count or power model.

#include "RadioCapabilities.h"

namespace lyra::rig {

RadioCapabilities capabilitiesFor(RadioFamily family) {
    RadioCapabilities c;
    c.family = family;

    switch (family) {
    case RadioFamily::Hl2:
        c.familyName          = QStringLiteral("Hermes Lite 2 / 2+");
        c.protocol            = 1;      // HPSDR Protocol 1
        c.maxReceivers        = 4;      // HL2 advertises 4 logical DDCs;
                                        //   discovery numRxs refines per unit
        c.adcBits             = 12;     // AD9866, 12-bit
        c.hasOnboardAudioIO   = false;  // family baseline = plain HL2 (PC audio).
                                        //   HL2+ (AK4951 codec) flips this true —
                                        //   detected / operator-set in a later stage.
        c.defaultAudioPath    = AudioPath::PcSound;
        c.lna                 = { -12, 48 };   // AD9866 PGA range (hl2_stream.h)
        c.txPower.ratedMaxW   = 5.0;    // nominal ~5 W; operator power cal is authoritative
        c.txPower.driveSteps  = 16;     // top-nibble of the drive byte = 16 coarse steps
        c.puresignalRequiresMod = true; // HL2 PS needs the hardware mod
        c.nRx                   = 2;    // DDC0 RX1 + DDC1 RX2 (SUB); one ADC
        c.independentBand       = false;
        c.diversityCapable      = false;
        c.psDdcReserved         = true; // DDC2/DDC3 stay PureSignal
        c.psDdcFirst            = 2;
        c.psDdcCount            = 2;
        break;

    case RadioFamily::BrickP2:
        // BrickSDR2 — Hermes-class Protocol 2, 14-bit, one ADC / one
        // antenna. deskHPSDR still runs two VFOs (RECEIVERS=2, DDC0+DDC1
        // both on ADC 0). independentBand stays false (not dual-ADC /
        // diversity). SUB/RX2 is the second VFO on that one ADC.
        c.familyName          = QStringLiteral("BrickSDR2");
        c.protocol            = 2;
        c.maxReceivers        = 4;
        c.adcBits             = 14;
        c.hasOnboardAudioIO   = true;   // physical mic + audio I/O on the unit
        c.defaultAudioPath    = AudioPath::RadioJack;
        c.puresignalRequiresMod = false;
        c.nRx                   = 2;
        c.independentBand       = false;
        c.diversityCapable      = false;
        c.psDdcReserved         = true; // leave a P2 feedback DDC free
        c.psDdcFirst            = 2;
        c.psDdcCount            = 2;
        break;

    case RadioFamily::AnanP2:
        c.familyName          = QStringLiteral("ANAN (Protocol 2)");
        c.protocol            = 2;
        c.adcBits             = 14;     // ANAN-class; refined per model with the P2 engine
        c.hasOnboardAudioIO   = true;
        c.defaultAudioPath    = AudioPath::RadioJack;
        c.puresignalRequiresMod = false;
        c.nRx                   = 2;
        c.independentBand       = true;  // dual-ADC path later; not this SUB slice
        c.diversityCapable      = true;
        c.psDdcReserved         = true;
        c.psDdcFirst            = 2;
        c.psDdcCount            = 2;
        break;

    case RadioFamily::AnanP1:
        c.familyName          = QStringLiteral("ANAN / Orion (Protocol 1)");
        c.protocol            = 1;
        c.hasOnboardAudioIO   = true;
        c.defaultAudioPath    = AudioPath::RadioJack;
        break;

    case RadioFamily::Unknown:
    default:
        c.familyName          = QStringLiteral("Unknown");
        break;
    }

    return c;
}

RadioFamily familyForBoardId(int boardId) {
    switch (boardId) {
    case 6:            return RadioFamily::Hl2;      // HermesLite (HL2 / HL2+)
    case 5:            // Orion
    case 10:           return RadioFamily::AnanP1;   // OrionMKII (ANAN, P1)
    default:           return RadioFamily::Unknown;
    }
}

} // namespace lyra::rig

// Lyra — TX drive split (coarse drive byte vs. ChannelMaster fixed IQ gain).
//
// Pure, header-only decision shared by HL2Stream::applyTxPower_ and
// scratch/test_tx_drive_split.cpp so the P1-vs-P2 rule is unit-tested.
//
// RadioVolume rv (0..1) is the one operator lever. Where it lands
// depends on the protocol family:
//
//   HL2 Protocol 1 : byte = round(255·rv)  AND  fixedGain = rv.
//                    The AD9866 drive byte has 16 coarse steps; the
//                    ChannelMaster fixed gain fills between them and
//                    below the PA floor. With the watts cap armed in
//                    CW the gateware substitutes a keyed carrier that
//                    bypasses fixed gain, so the byte carries rv·rv·
//                    kCwCapHeadroom instead (#170c).
//
//   Protocol 2     : byte = round(255·rv)  AND  fixedGain = 1.0, ALWAYS.
//                    Watts live on HP [345] only. The TX IQ stream
//                    stays at unity on every P2 radio, in every PS
//                    state. See the long note in applyTxPower_ before
//                    changing this: scaling P2 IQ with drive starves
//                    PureSignal collect below SetPk because the radio's
//                    TX-DAC replica (DDC1 / odd samples) follows the IQ
//                    amplitude, not [345]. v0.25.16–0.25.31 shipped that
//                    bug (FB 0 / cal 0 / pad walked to 0 on Brick2).

#pragma once

#include <cmath>

namespace lyra::tx {

struct TxDriveSplit {
    double fixedGain = 1.0;   // SetTXFixedGain(0, g, g)
    int    driveByte = 0;     // 0..255 coarse drive byte / HP [345]
};

// #170c CW watts-cap fold margin (P1 only). Conservative amplitude
// margin covering coarse-DAC quantisation + PA-curve residual so the
// folded byte lands UNDER the cap on any band.
constexpr double kCwCapHeadroom = 0.85;

// rv = RadioVolume 0..1 (clamped here). p2Path = P2 analog-drive
// formula. cwFold = P1 CW watts-cap fold requested by the caller;
// ignored on P2 (P2 streams host IQ, there is no keyed carrier).
inline TxDriveSplit splitTxDrive(double rv, bool p2Path, bool cwFold) noexcept {
    if (!(rv > 0.0)) rv = 0.0;
    if (rv > 1.0)    rv = 1.0;
    const bool   fold   = !p2Path && cwFold;
    const double byteRv = fold ? rv * rv * kCwCapHeadroom : rv;
    int byte = static_cast<int>(std::lround(255.0 * byteRv));
    if (byte < 0)   byte = 0;
    if (byte > 255) byte = 255;
    TxDriveSplit s;
    s.driveByte = byte;
    s.fixedGain = p2Path ? 1.0 : rv;
    return s;
}

}  // namespace lyra::tx

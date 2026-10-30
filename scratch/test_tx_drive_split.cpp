// TX drive split: P2 keeps IQ fixed gain at unity (watts on HP [345]
// only) so the radio's TX-DAC replica reaches the PureSignal SetPk at
// any drive; P1 carries rv on both the byte and the fixed gain.
// Regression for v0.25.16–0.25.31 (P2 IQ scaled with Drive → FB 0,
// cal 0, pad walked to 0 on Brick2).

#include "tx/TxDriveSplit.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

int main() {
    using lyra::tx::kCwCapHeadroom;
    using lyra::tx::splitTxDrive;

    int fails = 0;
    auto expect = [&](bool ok, const char *msg) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", msg);
            ++fails;
        }
    };
    auto near = [](double a, double b) { return std::fabs(a - b) < 1e-12; };

    // P2 path: fixed gain == 1.0 and byte == round(255*rv) at 25/50/100 %.
    for (double rv : {0.25, 0.50, 1.00}) {
        const auto s = splitTxDrive(rv, /*p2Path=*/true, /*cwFold=*/false);
        char msg[96];
        std::snprintf(msg, sizeof msg, "P2 rv=%.2f fixed gain must be 1.0 (got %.4f)",
                      rv, s.fixedGain);
        expect(near(s.fixedGain, 1.0), msg);
        std::snprintf(msg, sizeof msg, "P2 rv=%.2f byte must be round(255*rv)=%d (got %d)",
                      rv, static_cast<int>(std::lround(255.0 * rv)), s.driveByte);
        expect(s.driveByte == static_cast<int>(std::lround(255.0 * rv)), msg);
    }
    expect(splitTxDrive(0.25, true, false).driveByte == 64,  "P2 25 % → byte 64");
    expect(splitTxDrive(0.50, true, false).driveByte == 128, "P2 50 % → byte 128");
    expect(splitTxDrive(1.00, true, false).driveByte == 255, "P2 100 % → byte 255");

    // P2 ignores the CW fold request (no keyed-carrier substitution on P2).
    {
        const auto s = splitTxDrive(0.5, true, /*cwFold=*/true);
        expect(near(s.fixedGain, 1.0), "P2 cwFold must not touch fixed gain");
        expect(s.driveByte == 128,      "P2 cwFold must not fold the byte");
    }

    // P1 path: fixed gain == rv, byte == round(255*rv).
    for (double rv : {0.25, 0.50, 1.00}) {
        const auto s = splitTxDrive(rv, /*p2Path=*/false, /*cwFold=*/false);
        char msg[96];
        std::snprintf(msg, sizeof msg, "P1 rv=%.2f fixed gain must be rv (got %.4f)",
                      rv, s.fixedGain);
        expect(near(s.fixedGain, rv), msg);
        expect(s.driveByte == static_cast<int>(std::lround(255.0 * rv)),
               "P1 byte must be round(255*rv)");
    }

    // P1 CW watts-cap fold: byte = round(255*rv*rv*0.85), gain stays rv.
    {
        const double rv = 0.5;
        const auto s = splitTxDrive(rv, false, /*cwFold=*/true);
        expect(near(s.fixedGain, rv), "P1 cwFold keeps fixed gain = rv");
        expect(s.driveByte ==
                   static_cast<int>(std::lround(255.0 * rv * rv * kCwCapHeadroom)),
               "P1 cwFold byte = round(255*rv*rv*headroom)");
        expect(near(kCwCapHeadroom, 0.85), "kCwCapHeadroom stays 0.85");
    }

    // Clamp.
    expect(splitTxDrive(-0.1, true, false).driveByte == 0,   "rv<0 → byte 0");
    expect(splitTxDrive(1.7, true, false).driveByte == 255,  "rv>1 → byte 255");
    expect(near(splitTxDrive(1.7, false, false).fixedGain, 1.0), "P1 rv>1 → gain 1.0");
    expect(near(splitTxDrive(0.0, false, false).fixedGain, 0.0), "P1 rv=0 → gain 0");

    if (fails) {
        std::fprintf(stderr, "%d fail(s)\n", fails);
        return 1;
    }
    std::puts("test_tx_drive_split: ok");
    return 0;
}

// #199 Stage 1 — OcControl unit test.
//
// Verifies the pure family-branched compute() (raw byte select +
// adjustForTX/RX + per-pin group/PA gating) and the −1-seeded change gate,
// against the Thetis Penny.UpdateExtCtrl reference behaviour.  Wire-inert:
// no HL2Stream, no radio state, no event loop.
//
// Build + run:  cmake --build build --target test_oc_control
//               ./build/test_oc_control    (exit 0 = all pass)

#include "oc/OcControl.h"
#include "bands.h"

#include <cstdio>

using lyra::oc::OcControl;
using lyra::oc::TxPinAction;
using lyra::oc::Family;

static int g_fail = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__,       \
                         #cond);                                               \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)
#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        const int _a = static_cast<int>(a);                                    \
        const int _b = static_cast<int>(b);                                    \
        if (_a != _b) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d  %s == %s  (0x%02X != 0x%02X)\n", \
                         __FILE__, __LINE__, #a, #b, _a, _b);                  \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

// Amateur-band indices (amateurBands(): 160m..6m).
enum { B160 = 0, B80, B60, B40, B30, B20, B17, B15, B12, B10, B6 };

int main() {
    // ---- 1. Master gate: disabled -> always 0 -----------------------------
    {
        OcControl oc;
        oc.setTxMaskA(B40, 0x7F);
        oc.setRxMaskA(B40, 0x7F);
        // enabled defaults false
        CHECK_EQ(oc.targetBits(B40, B40, /*tx*/ true,  false, false, false), 0);
        CHECK_EQ(oc.targetBits(B40, B40, /*tx*/ false, false, false, false), 0);
        oc.setEnabled(true);
        CHECK(oc.targetBits(B40, B40, true, false, false, false) != 0);
    }

    // ---- 2. Straight RX/TX pass-through with default pin actions -----------
    {
        OcControl oc;
        oc.setEnabled(true);
        oc.setTxMaskA(B40, 0x08);        // 40m TX = pin4
        oc.setRxMaskA(B40, 0x48);        // 40m RX = pin4 + HPF pin7
        // default TxPinAction = MoxTuneTwoTone, all xPA off -> full pass.
        CHECK_EQ(oc.compute(B40, B40, /*tx*/ true,  false, false, false), 0x08);
        CHECK_EQ(oc.compute(B40, B40, /*tx*/ false, false, false, false), 0x48);
    }

    // ---- 3. Change gate: −1 seed emits first (incl. all-off) --------------
    {
        OcControl oc;
        CHECK(oc.takeIfChanged(0x00) == true);   // −1 seed -> first emit fires
        CHECK(oc.takeIfChanged(0x00) == false);  // unchanged
        CHECK(oc.takeIfChanged(0x08) == true);
        CHECK(oc.takeIfChanged(0x08) == false);
        oc.resetChangeGate();
        CHECK(oc.takeIfChanged(0x08) == true);   // re-armed
        oc.takeIfChanged(0x08);
        oc.setEnabled(true);                     // setEnabled re-arms the gate
        CHECK(oc.takeIfChanged(0x08) == true);
    }

    // ---- 4. Transmit pin action (Tune-only pin gated by state) ------------
    {
        OcControl oc;
        oc.setEnabled(true);
        oc.setTxMaskA(B40, 0x7F);                // all pins wired
        oc.setTxPinAction(/*group HF*/ 0, /*pin1*/ 1, TxPinAction::Tune);
        // pin1 = Tune only; others default MoxTuneTwoTone.
        CHECK_EQ(oc.compute(B40, B40, /*tx*/ true, /*tune*/ false, false, false), 0x7E);
        CHECK_EQ(oc.compute(B40, B40, /*tx*/ true, /*tune*/ true,  false, false), 0x7F);
        // pure MOX pin fires only on plain TX (not tune/two-tone):
        oc.setTxPinAction(0, 2, TxPinAction::Mox);
        CHECK((oc.compute(B40, B40, true, false, false, false) & 0x02) != 0); // MOX on
        CHECK((oc.compute(B40, B40, true, true,  false, false) & 0x02) == 0); // tune -> off
    }

    // ---- 5. External-PA gating (TX and RX) --------------------------------
    {
        OcControl oc;
        oc.setEnabled(true);
        oc.setTxMaskA(B40, 0x01);                // only pin1
        oc.setTxPinPa(0, 1, true);               // pin1 needs external PA
        CHECK_EQ(oc.compute(B40, B40, true, false, false, /*pa*/ false), 0x00);
        CHECK_EQ(oc.compute(B40, B40, true, false, false, /*pa*/ true),  0x01);

        oc.setRxMaskA(B40, 0x40);                // pin7 on RX
        oc.setRxPinPa(0, 7, true);
        CHECK_EQ(oc.compute(B40, B40, false, false, false, /*pa*/ false), 0x00);
        CHECK_EQ(oc.compute(B40, B40, false, false, false, /*pa*/ true),  0x40);
    }

    // ---- 6. HermesLite RX2 "pick the higher band" ------------------------
    {
        OcControl oc;                            // family defaults HermesLite
        oc.setEnabled(true);
        oc.setRxMaskA(B40, 0x08);                // 40m
        oc.setRxMaskA(B20, 0x20);                // 20m (higher band)
        oc.setRx2Enabled(true);
        CHECK_EQ(oc.compute(/*A*/ B40, /*B*/ B20, false, false, false, false), 0x20);
        oc.setRx2Enabled(false);
        CHECK_EQ(oc.compute(B40, B20, false, false, false, false), 0x08);
    }

    // ---- 7. VFO-B-TX picks the B-band TX mask (HermesLite) ----------------
    {
        OcControl oc;
        oc.setEnabled(true);
        oc.setTxMaskA(B40, 0x08);
        oc.setTxMaskA(B20, 0x20);
        oc.setVfoBTx(true);
        CHECK_EQ(oc.compute(/*A*/ B40, /*B*/ B20, /*tx*/ true, false, false, false), 0x20);
        oc.setVfoBTx(false);
        CHECK_EQ(oc.compute(B40, B20, true, false, false, false), 0x08);
    }

    // ---- 8. Split pins: A-half from RxABitMask, B-half from the B table ---
    {
        OcControl oc;
        oc.setEnabled(true);
        oc.setSplitPins(true);
        oc.setRxABitMask(0x0f);                  // pins1-4 = A, pins5-7 = B
        oc.setTxMaskA(B40, 0x0F);                // A-half candidate
        oc.setTxMaskB(B20, 0x30);                // B-half candidate
        CHECK_EQ(oc.compute(/*A*/ B40, /*B*/ B20, /*tx*/ true, false, false, false), 0x3F);
    }

    // ---- 9. Out-of-range bands -> no pins ---------------------------------
    {
        OcControl oc;
        oc.setEnabled(true);
        oc.setTxMaskA(B40, 0x7F);
        CHECK_EQ(oc.compute(-1,  B40, true, false, false, false), 0x00);
        CHECK_EQ(oc.compute(999, B40, true, false, false, false), 0x00);
    }

    // ---- 10. GenericP1 family ignores the RX2 higher-band pick -----------
    {
        OcControl oc;
        oc.setFamily(Family::GenericP1);
        oc.setEnabled(true);
        oc.setRxMaskA(B40, 0x08);
        oc.setRxMaskA(B20, 0x20);
        oc.setRx2Enabled(true);
        // generic branch uses bandA only -> 0x08, NOT the HL 0x20.
        CHECK_EQ(oc.compute(B40, B20, false, false, false, false), 0x08);
    }

    // ---- 11. Xvtr rows default 0; N2ADR HF fill does not touch them ------
    {
        OcControl oc;
        oc.setEnabled(true);
        oc.setTxMaskA(B10, 0x60);
        oc.setRxMaskA(B10, 0x60);
        CHECK_EQ(oc.rxMaskXvtr(0), 0);
        CHECK_EQ(oc.txMaskXvtr(0), 0);
        CHECK_EQ(oc.computeXvtr(0, true,  false, false, false), 0);
        CHECK_EQ(oc.computeXvtr(0, false, false, false, false), 0);
        CHECK_EQ(oc.compute(B10, B10, true, false, false, false), 0x60);
    }

    // ---- 12. Xvtr ticks emit; out-of-range slot is 0 ----------------------
    {
        OcControl oc;
        oc.setEnabled(true);
        oc.setRxMaskXvtr(0, 0x02);   // 2 m chip, pin 2
        oc.setTxMaskXvtr(0, 0x04);   // pin 3
        CHECK_EQ(oc.computeXvtr(0, false, false, false, false), 0x02);
        CHECK_EQ(oc.computeXvtr(0, true,  false, false, false), 0x04);
        CHECK_EQ(oc.computeXvtr(-1, true, false, false, false), 0);
        CHECK_EQ(oc.computeXvtr(99, true, false, false, false), 0);
        // Other chips stay empty
        CHECK_EQ(oc.computeXvtr(1, true, false, false, false), 0);
    }

    // ---- 13. Xvtr TX pin-action uses HF group 0 gates --------------------
    {
        OcControl oc;
        oc.setEnabled(true);
        oc.setTxMaskXvtr(1, 0x01);
        oc.setTxPinAction(0, 1, TxPinAction::Tune);
        CHECK_EQ(oc.computeXvtr(1, true, /*tune*/ false, false, false), 0x00);
        CHECK_EQ(oc.computeXvtr(1, true, /*tune*/ true,  false, false), 0x01);
    }

    // ---- 14. Yaesu BCD nibble on OC pins 1–4 ------------------------------
    {
        CHECK_EQ(lyra::bcdForBand(B160, false), 1);
        CHECK_EQ(lyra::bcdForBand(B40, false), 3);
        CHECK_EQ(lyra::bcdForBand(B10, false), 9);
        CHECK_EQ(lyra::bcdForBand(B6, false), 10);
        CHECK_EQ(lyra::yaesuOcPattern(1, false), 0x01);
        CHECK_EQ(lyra::yaesuOcPattern(3, false), 0x03);
        CHECK_EQ(lyra::yaesuOcPattern(9, false), 0x09);
        CHECK_EQ(lyra::yaesuOcPattern(10, false), 0x0A);
        CHECK_EQ(lyra::yaesuOcPattern(1, true), 0x0E);   // ANAN invert
        CHECK_EQ(lyra::yaesuOcPattern(0, true), 0x0F);   // bypass → all nibble
        CHECK_EQ(lyra::yaesuBcdCodeForFreq(7050000, false, true), 3);
        CHECK_EQ(lyra::yaesuBcdCodeForFreq(144200000, false, true), 0);
    }

    if (g_fail == 0) {
        std::printf("test_oc_control: ALL PASS\n");
        return 0;
    }
    std::printf("test_oc_control: %d FAILURE(S)\n", g_fail);
    return 1;
}

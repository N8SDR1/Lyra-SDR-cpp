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
#include <string>
#include <vector>

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

    // ---- 12b. Eight Xvtr rows (shared lyra::xvtr::kSlotCount) -------------
    {
        static_assert(OcControl::kXvtrSlots == lyra::xvtr::kSlotCount,
                      "OcControl Xvtr row count must track the slot count");
        CHECK_EQ(OcControl::kXvtrSlots, 8);
        OcControl oc;
        oc.setEnabled(true);
        for (int i = 0; i < OcControl::kXvtrSlots; ++i) {
            CHECK_EQ(oc.rxMaskXvtr(i), 0);          // new rows start empty
            CHECK_EQ(oc.computeXvtr(i, false, false, false, false), 0);
        }
        oc.setRxMaskXvtr(7, 0x40);                 // last row is addressable
        oc.setTxMaskXvtr(7, 0x20);
        CHECK_EQ(oc.computeXvtr(7, false, false, false, false), 0x40);
        CHECK_EQ(oc.computeXvtr(7, true,  false, false, false), 0x20);
        CHECK_EQ(oc.computeXvtr(8, true,  false, false, false), 0);   // past the end
        oc.setRxMaskXvtr(8, 0x7F);                 // ignored, no overflow
        CHECK_EQ(oc.rxMaskXvtr(8), 0);
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

    // ---- 15. SWL rows: index space + precedence (Xvtr > ham > BC > none) --
    // broadcastBands(): 120m=0 90m 75m 60m 49m 41m 31m 25m 22m 19m 16m 13m=11
    enum { BC120 = 0, BC90, BC75, BC60, BC49, BC41, BC31, BC25, BC22, BC19, BC16, BC13 };
    {
        OcControl oc;
        const int nB = oc.nBands();
        CHECK_EQ(nB, 11);
        CHECK_EQ(oc.nBcBands(), 12);
        CHECK_EQ(oc.nRows(), 23);
        CHECK_EQ(oc.bcRow(BC49), nB + BC49);
        CHECK_EQ(oc.bcRow(-1), -1);
        CHECK_EQ(oc.bcRow(12), -1);
        CHECK(OcControl::isBcRow(nB));
        CHECK(!OcControl::isBcRow(nB - 1));
        CHECK(!OcControl::isBcRow(nB + 12));
        CHECK_EQ(OcControl::bcIndexOfRow(nB + BC31), BC31);
        CHECK_EQ(OcControl::bcIndexOfRow(B40), -1);

        // amateur rows unchanged
        CHECK_EQ(OcControl::rowForHz(7'200'000), B40);     // 41 m BC overlap -> ham wins
        CHECK_EQ(OcControl::rowForHz(3'950'000), B80);     // 75 m BC inside 80 m -> ham
        CHECK_EQ(OcControl::rowForHz(21'450'000), B15);    // 13 m BC lower edge -> ham
        // broadcast rows where no ham band claims the dial
        CHECK_EQ(OcControl::rowForHz(5'975'000), oc.bcRow(BC49));
        CHECK_EQ(OcControl::rowForHz(7'350'000), oc.bcRow(BC41));
        CHECK_EQ(OcControl::rowForHz(9'580'000), oc.bcRow(BC31));
        CHECK_EQ(OcControl::rowForHz(21'600'000), oc.bcRow(BC13));
        CHECK_EQ(OcControl::rowForHz(2'400'000), oc.bcRow(BC120));
        // none: CB, GEN-ish dial positions, LW/MW
        CHECK_EQ(OcControl::rowForHz(27'185'000), -1);     // CB stays pins-off
        CHECK_EQ(OcControl::rowForHz(1'000'000), -1);      // MW
        CHECK_EQ(OcControl::rowForHz(8'000'000), -1);      // gap
        // Xvtr slot wins over everything
        CHECK_EQ(OcControl::rowForHz(7'200'000, 0), -1);
        CHECK_EQ(OcControl::rowForHz(5'975'000, 7), -1);
    }

    // ---- 16. N2ADR seed for the SWL rows (ham LPF directly above) ---------
    {
        // RX = LPF pin (+ pin 7 HPF when the band is above 3 MHz).
        // TX = 0 for EVERY broadcast band (RX-only rows).
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC120, false), 0x02);        // 80 m LPF, HPF dropped
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC90,  false), 0x02 | 0x40); // 80 m
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC75,  false), 0x02 | 0x40); // 80 m
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC60,  false), 0x04 | 0x40); // 60/40 m
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC49,  false), 0x04 | 0x40); // 40 m
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC41,  false), 0x04 | 0x40); // 40 m (straddle override)
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC31,  false), 0x08 | 0x40); // 30/20 m
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC25,  false), 0x08 | 0x40);
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC22,  false), 0x08 | 0x40);
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC19,  false), 0x10 | 0x40); // 17/15 m
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC16,  false), 0x10 | 0x40);
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC13,  false), 0x10 | 0x40); // 15 m (straddle override)
        CHECK_EQ(OcControl::n2adrBroadcastSeed(-1, false), 0);
        CHECK_EQ(OcControl::n2adrBroadcastSeed(12, false), 0);
        // Straddle seeds equal the adjacent ham row's RX mask exactly, so
        // the change gate is silent across 7.300 / 21.450.
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC41, false), lyra::n2adrOcPattern(B40, false));
        CHECK_EQ(OcControl::n2adrBroadcastSeed(BC13, false), lyra::n2adrOcPattern(B15, false));
        for (int bc = 0; bc < 12; ++bc) {
            CHECK_EQ(OcControl::n2adrBroadcastSeed(bc, true), 0);   // TX always 0
            // Every RX seed is some ham row's LPF pin (modulo the HPF bit)
            // — no invented pin patterns.
            const int rxLpf = OcControl::n2adrBroadcastSeed(bc, false) & ~0x40;
            bool found = false;
            for (int b = 0; b < 11 && !found; ++b)
                found = (lyra::n2adrOcPattern(b, true) == rxLpf);
            CHECK(found);
        }
    }

    // ---- 16b. First-run / rig-switch N2ADR stamp leaves SWL rows at 0 ----
    // Mirrors HL2Stream::seedOcN2adr() (hl2_stream.cpp) — the ONLY seed
    // the reloadOcFromDisk() default path runs: amateur rows via
    // n2adrOcPattern, nothing else.  scratch/oc_swl_seed_paths.cmake
    // (ctest oc_swl_seed_paths_static) guards that the stream code really
    // keeps seedOcSwlN2adr() out of that path.
    {
        OcControl oc;
        oc.setEnabled(true);
        const int nB = oc.nBands();
        for (int bi = 0; bi < nB; ++bi) {
            oc.setRxMaskA(bi, quint8(lyra::n2adrOcPattern(bi, false)));
            oc.setTxMaskA(bi, quint8(lyra::n2adrOcPattern(bi, true)));
        }
        for (int bc = 0; bc < oc.nBcBands(); ++bc) {
            const int row = oc.bcRow(bc);
            CHECK_EQ(oc.rxMaskA(row), 0);
            CHECK_EQ(oc.txMaskA(row), 0);
            CHECK_EQ(oc.rxMaskB(row), 0);
            CHECK_EQ(oc.txMaskB(row), 0);
            CHECK_EQ(oc.compute(row, row, false, false, false, false), 0);
        }
        CHECK_EQ(oc.compute(B40, B40, false, false, false, false), 0x44);   // ham rows did stamp
    }

    // ---- 17. SWL rows compute through pin-action group 2 -----------------
    {
        OcControl oc;
        oc.setEnabled(true);
        const int r49 = oc.bcRow(BC49);
        oc.setRxMaskA(r49, 0x44);
        oc.setTxMaskA(r49, 0x04);
        CHECK_EQ(oc.rxMaskA(r49), 0x44);
        CHECK_EQ(oc.compute(r49, r49, false, false, false, false), 0x44);
        CHECK_EQ(oc.compute(r49, r49, true,  false, false, false), 0x04);
        // group 2 gates apply to the SWL row ...
        oc.setTxPinAction(/*SWL*/ 2, /*pin1*/ 3, TxPinAction::Tune);
        CHECK_EQ(oc.compute(r49, r49, true, /*tune*/ false, false, false), 0x00);
        CHECK_EQ(oc.compute(r49, r49, true, /*tune*/ true,  false, false), 0x04);
        oc.setRxPinPa(2, 7, true);
        CHECK_EQ(oc.compute(r49, r49, false, false, false, /*pa*/ false), 0x04);
        // ... and NOT to the ham rows (group 0 untouched)
        oc.setTxMaskA(B40, 0x04);
        oc.setRxMaskA(B40, 0x44);
        CHECK_EQ(oc.compute(B40, B40, true,  false, false, false), 0x04);
        CHECK_EQ(oc.compute(B40, B40, false, false, false, false), 0x44);
        // out-of-range row past the SWL block -> 0
        CHECK_EQ(oc.compute(oc.nRows(), oc.nRows(), false, false, false, false), 0);
        oc.setRxMaskA(oc.nRows(), 0x7F);   // ignored
        CHECK_EQ(oc.rxMaskA(oc.nRows()), 0);
    }

    // ---- 18. HL2 RX2 "higher band" pick compares frequency, not row # ----
    {
        OcControl oc;
        oc.setEnabled(true);
        oc.setRx2Enabled(true);
        const int r49 = oc.bcRow(BC49);        // 5.9 MHz, row 15
        oc.setRxMaskA(r49, 0x44);
        oc.setRxMaskA(B20, 0x48);              // 14 MHz, row 5
        // A = 49 m BC (row 15), B = 20 m (row 5): 20 m is the HIGHER band
        CHECK_EQ(oc.compute(r49, B20, false, false, false, false), 0x48);
        // A = 20 m, B = 49 m BC: A is already higher -> A's mask
        CHECK_EQ(oc.compute(B20, r49, false, false, false, false), 0x48);
        // ham vs ham unchanged
        oc.setRxMaskA(B40, 0x44);
        CHECK_EQ(oc.compute(B40, B20, false, false, false, false), 0x48);
        CHECK_EQ(oc.compute(B20, B40, false, false, false, false), 0x48);
    }

    // ---- 19. Bounds: rowForHz never yields a row a getter rejects; BC rows
    //          never alias ham rows ---------------------------------------
    {
        OcControl oc;
        oc.setEnabled(true);
        const int nB  = oc.nBands();
        const int nBc = oc.nBcBands();
        // Distinct fingerprint per row so aliasing would show.
        for (int b = 0; b < nB; ++b)
            oc.setRxMaskA(b, quint8(0x01 + b));            // 0x01..0x0B
        for (int bc = 0; bc < nBc; ++bc)
            oc.setRxMaskA(oc.bcRow(bc), quint8(0x40 | bc)); // 0x40..0x4B
        for (int b = 0; b < nB; ++b)
            CHECK_EQ(oc.rxMaskA(b), 0x01 + b);             // ham rows untouched by BC writes
        for (int bc = 0; bc < nBc; ++bc)
            CHECK_EQ(oc.rxMaskA(oc.bcRow(bc)), 0x40 | bc); // BC rows read their own slot
        // Sweep the dial 0..60 MHz in 1 kHz steps.
        int hamHits = 0, bcHits = 0;
        for (int hz = 0; hz <= 60'000'000; hz += 1000) {
            const int row = OcControl::rowForHz(hz);
            if (row < 0) continue;
            CHECK(row < oc.nRows());
            // A getter accepts it: write a marker through the setter and read it back.
            const quint8 was = oc.rxMaskA(row);
            oc.setTxMaskB(row, 0x55);
            CHECK_EQ(oc.txMaskB(row), 0x55);
            oc.setTxMaskB(row, 0);
            CHECK_EQ(oc.rxMaskA(row), was);                // setter hit the same row only
            if (row < nB) {
                ++hamHits;
                CHECK_EQ(OcControl::bcIndexOfRow(row), -1);
                CHECK_EQ(lyra::bandIndexForFreq(hz), row);
            } else {
                ++bcHits;
                const int bc = OcControl::bcIndexOfRow(row);
                CHECK(bc >= 0 && bc < nBc);
                CHECK_EQ(oc.bcRow(bc), row);
                CHECK_EQ(lyra::broadcastBandIndexForFreq(hz), bc);
                CHECK_EQ(lyra::bandIndexForFreq(hz), -1);  // never a BC row inside a ham band
            }
        }
        CHECK(hamHits > 0 && bcHits > 0);
        // Rejected indices: every getter returns 0, every setter is a no-op.
        for (int bad : {-1, oc.nRows(), oc.nRows() + 5, 999}) {
            oc.setRxMaskA(bad, 0x7F); oc.setTxMaskA(bad, 0x7F);
            oc.setRxMaskB(bad, 0x7F); oc.setTxMaskB(bad, 0x7F);
            CHECK_EQ(oc.rxMaskA(bad), 0); CHECK_EQ(oc.txMaskA(bad), 0);
            CHECK_EQ(oc.rxMaskB(bad), 0); CHECK_EQ(oc.txMaskB(bad), 0);
            CHECK_EQ(oc.compute(bad, bad, false, false, false, false), 0);
            CHECK_EQ(oc.compute(bad, B40, true, false, false, false), 0);
        }
        CHECK_EQ(oc.rxMaskA(nB - 1), 0x01 + nB - 1);       // fingerprints survived
        CHECK_EQ(oc.rxMaskA(oc.bcRow(nBc - 1)), 0x40 | (nBc - 1));
    }

    // ---- 20. Consistency with BandMemory::bandNameFor --------------------
    // BandMemory is not linked here (it needs HL2Stream), so the lookup is
    // replicated verbatim from bandmemory.cpp:118-132: amateur first, then
    // "bc_" + broadcast, then "cb_" + CB, else "".  The OC row space has no
    // CB row by decision, so a "cb_" name must map to row -1.
    {
        auto bandNameFor = [](int hz) -> std::string {
            const int a = lyra::bandIndexForFreq(hz);
            if (a >= 0) return lyra::amateurBands()[size_t(a)].name;
            const int b = lyra::broadcastBandIndexForFreq(hz);
            if (b >= 0) return std::string("bc_") + lyra::broadcastBands()[size_t(b)].name;
            const int c = lyra::cbBandIndexForFreq(hz);
            if (c >= 0) return std::string("cb_") + lyra::cbBands()[size_t(c)].name;
            return "";
        };
        auto rowName = [](int row) -> std::string {
            const int nB = int(lyra::amateurBands().size());
            if (row >= 0 && row < nB) return lyra::amateurBands()[size_t(row)].name;
            const int bc = OcControl::bcIndexOfRow(row);
            if (bc >= 0) return std::string("bc_") + lyra::broadcastBands()[size_t(bc)].name;
            return "";
        };
        std::vector<int> probes = {7'200'000, 7'300'000, 7'301'000,
                                   21'450'000, 21'451'000, 3'900'000};
        for (const auto &b : lyra::amateurBands())
            for (int hz : {b.low - 1, b.low, b.low + 1, b.high - 1, b.high, b.high + 1})
                probes.push_back(hz);
        for (const auto &b : lyra::broadcastBands())
            for (int hz : {b.low - 1, b.low, b.low + 1, b.high - 1, b.high, b.high + 1})
                probes.push_back(hz);
        for (const auto &b : lyra::cbBands())
            for (int hz : {b.low - 1, b.low, b.high, b.high + 1})
                probes.push_back(hz);
        int checked = 0;
        for (int hz : probes) {
            const std::string bm = bandNameFor(hz);
            const std::string oc = rowName(OcControl::rowForHz(hz, -1));
            const std::string expect = (bm.rfind("cb_", 0) == 0) ? std::string() : bm;
            if (oc != expect) {
                std::fprintf(stderr, "FAIL %s:%d  rowForHz(%d) -> '%s', BandMemory -> '%s'\n",
                             __FILE__, __LINE__, hz, oc.c_str(), bm.c_str());
                ++g_fail;
            }
            ++checked;
        }
        CHECK(checked > 100);
        // Spot-check the named cases explicitly.
        CHECK_EQ(OcControl::rowForHz(7'200'000), B40);
        CHECK_EQ(OcControl::rowForHz(7'300'000), B40);
        CHECK_EQ(OcControl::rowForHz(7'301'000), OcControl::rowForHz(7'350'000));  // 41 m BC
        CHECK_EQ(OcControl::rowForHz(21'450'000), B15);
        CHECK_EQ(OcControl::rowForHz(21'451'000), OcControl::rowForHz(21'600'000)); // 13 m BC
        CHECK_EQ(OcControl::rowForHz(3'900'000), B80);
    }

    if (g_fail == 0) {
        std::printf("test_oc_control: ALL PASS\n");
        return 0;
    }
    std::printf("test_oc_control: %d FAILURE(S)\n", g_fail);
    return 1;
}

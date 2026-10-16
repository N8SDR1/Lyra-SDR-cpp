// Unit test for the rack-state save/load round-trip (#49 Stage 1).
// Build + run:  cmake --build build --target test_profile_rack
//               build/test_profile_rack.exe
//
// Each TX DSP model's saveState() -> loadState() must round-trip every
// captured field, and loadState({}) must be a tolerant no-op.

#include "eqmodel.h"
#include "speechmodel.h"
#include "combinatormodel.h"
#include "platemodel.h"

#include <QCoreApplication>
#include <QJsonObject>

#include <cmath>
#include <cstdio>

using namespace lyra::ui;

namespace {
int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++g_fail;                                                      \
        }                                                                  \
    } while (0)
bool dEq(double a, double b) { return std::fabs(a - b) < 1e-6; }
}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);   // the models create QTimers

    // ── EqModel ─────────────────────────────────────────────────────────
    {
        EqModel a(EqModel::Side::Tx);
        a.setBypass(true); a.setMakeupDb(3.0);
        a.setBandFreq(0, 80.0); a.setBandGain(2, -5.0);
        a.setBandQ(2, 2.5); a.setBandEnabled(5, false);
        const QJsonObject o = a.saveState();
        EqModel b(EqModel::Side::Tx);
        b.loadState(o);
        CHECK(b.bypass() == true);
        CHECK(dEq(b.makeupDb(), 3.0));
        CHECK(dEq(b.bandFreq(0), 80.0));
        CHECK(dEq(b.bandGain(2), -5.0));
        CHECK(dEq(b.bandQ(2), 2.5));
        CHECK(b.bandEnabled(5) == false);
        // tolerant no-op
        EqModel c(EqModel::Side::Tx); const double mk = c.makeupDb();
        c.loadState(QJsonObject{});
        CHECK(dEq(c.makeupDb(), mk));
        std::printf("EqModel round-trip ok\n");
    }

    // ── SpeechModel ───────────────────────────────────────────────────────
    {
        SpeechModel a;
        a.setGateEnabled(true); a.setGateThreshDb(-50.0);
        a.setAgcEnabled(true); a.setAgcMaxGainDb(12.0);
        a.setDeessEnabled(true); a.setDeessFreqHz(7000.0);
        const QJsonObject o = a.saveState();
        SpeechModel b; b.loadState(o);
        CHECK(b.gateEnabled() == true);
        CHECK(dEq(b.gateThreshDb(), -50.0));
        CHECK(b.agcEnabled() == true);
        CHECK(dEq(b.agcMaxGainDb(), 12.0));
        CHECK(b.deessEnabled() == true);
        CHECK(dEq(b.deessFreqHz(), 7000.0));
        std::printf("SpeechModel round-trip ok\n");
    }

    // ── CombinatorModel ───────────────────────────────────────────────────
    {
        CombinatorModel a;
        a.setBypass(false); a.setMix(0.5); a.setRatio(4.0);
        a.setThreshDb(-28.0); a.setXover(5.0); a.setSbcEnabled(false);
        a.setSbcSpeed(2.0); a.setBandThreshDb(3, -7.0); a.setBandGainDb(1, 4.0);
        a.setBandEnabled(4, false);
        const QJsonObject o = a.saveState();
        CombinatorModel b; b.loadState(o);
        CHECK(b.bypass() == false);
        CHECK(dEq(b.mix(), 0.5));
        CHECK(dEq(b.ratio(), 4.0));
        CHECK(dEq(b.threshDb(), -28.0));
        CHECK(dEq(b.xover(), 5.0));
        CHECK(b.sbcEnabled() == false);
        CHECK(dEq(b.sbcSpeed(), 2.0));
        CHECK(dEq(b.bandThreshDb(3), -7.0));
        CHECK(dEq(b.bandGainDb(1), 4.0));
        CHECK(b.bandEnabled(4) == false);
        std::printf("CombinatorModel round-trip ok\n");
    }

    // ── PlateModel ────────────────────────────────────────────────────────
    {
        PlateModel a;
        a.setBypass(false); a.setDecayS(3.0); a.setMix(0.10);
        a.setSize(50.0); a.setDamp(40.0); a.setPreDelayMs(25.0);
        a.setBassDb(-6.0); a.setTrebDb(9.0);
        const QJsonObject o = a.saveState();
        PlateModel b; b.loadState(o);
        CHECK(b.bypass() == false);
        CHECK(dEq(b.decayS(), 3.0));
        CHECK(dEq(b.mix(), 0.10));
        CHECK(dEq(b.size(), 50.0));
        CHECK(dEq(b.damp(), 40.0));
        CHECK(dEq(b.preDelayMs(), 25.0));
        CHECK(dEq(b.bassDb(), -6.0));
        CHECK(dEq(b.trebDb(), 9.0));
        std::printf("PlateModel round-trip ok\n");
    }

    std::printf(g_fail ? "\n%d CHECK(s) FAILED\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}

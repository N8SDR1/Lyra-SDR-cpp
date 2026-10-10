// Isolated ddc_map gates: attestation-equivalent is the ps_armed
// argument (HL2Stream ANDs attestation before calling). No RadioNet.

#include "ps/DdcMap.h"

#include <cstdio>
#include <cstdlib>

using lyra::ps::ddc_map;
using lyra::rig::RadioFamily;

static int g_fail = 0;

static void expect(bool ok, const char *msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_fail;
    }
}

int main() {
    {
        const auto r = ddc_map(false, false, true, RadioFamily::Hl2);
        expect(!r.puresignalRun, "idle !armed: puresignalRun");
        expect(r.adcCntrl1 == 0, "idle !armed: cntrl1");
        expect(!r.feedPsccFromDdc0Ddc1, "idle !armed: pscc");
        expect(!r.pauseSub, "idle !armed: pauseSub");
        expect(!r.bypassCapturedProfile, "idle !armed: bypass");
        expect(!r.alexPsBit, "idle !armed: alex");
    }
    {
        const auto r = ddc_map(true, false, true, RadioFamily::Hl2);
        expect(!r.puresignalRun, "MOX !armed: puresignalRun");
        expect(r.adcCntrl1 == 0, "MOX !armed: cntrl1 stays 0");
        expect(!r.feedPsccFromDdc0Ddc1, "MOX !armed: no pscc");
    }
    {
        const auto r = ddc_map(false, true, true, RadioFamily::Hl2);
        expect(r.puresignalRun, "armed RX: puresignal_run");
        expect(r.adcCntrl1 == 0, "armed RX: cntrl1 still 0");
        expect(!r.feedPsccFromDdc0Ddc1, "armed RX: no live remux");
        expect(!r.pauseSub, "armed RX: no SUB pause");
        expect(!r.bypassCapturedProfile, "armed RX: no captured bypass");
    }
    {
        const auto r = ddc_map(true, true, true, RadioFamily::Hl2);
        expect(r.puresignalRun, "live HL2: puresignal_run");
        expect(r.adcCntrl1 == 4, "live HL2: Thetis cntrl1=4");
        expect(r.feedPsccFromDdc0Ddc1, "live HL2: DDC0+DDC1 → pscc");
        expect(r.psRxDdc == 0 && r.psTxDdc == 1, "live HL2: rx=0 tx=1");
        expect(r.pauseSub, "live HL2: pause SUB");
        expect(r.bypassCapturedProfile, "live HL2: captured bypass");
        expect(!r.alexPsBit, "live HL2: no ALEX_PS_BIT");
    }
    {
        const auto r = ddc_map(true, true, false, RadioFamily::BrickP2);
        expect(r.puresignalRun, "live Brick: puresignal_run");
        expect(r.adcCntrl1 == 0, "live Brick: no P1 cntrl1");
        expect(r.alexPsBit, "live Brick: ALEX_PS_BIT");
        expect(r.feedPsccFromDdc0Ddc1, "live Brick: DDC0+DDC1 → pscc");
    }
    {
        const auto r = ddc_map(false, true, false, RadioFamily::BrickP2);
        expect(r.puresignalRun, "armed Brick RX: run");
        expect(!r.alexPsBit, "armed Brick RX: Alex only when live");
        expect(r.adcCntrl1 == 0, "armed Brick RX: cntrl1 0");
    }

    if (g_fail) {
        std::fprintf(stderr, "%d ddc_map assertion(s) failed\n", g_fail);
        return EXIT_FAILURE;
    }
    std::printf("test_puresignal_ddc_map: OK\n");
    return EXIT_SUCCESS;
}

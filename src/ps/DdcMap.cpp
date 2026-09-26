#include "ps/DdcMap.h"

#include <atomic>

namespace lyra::ps {

namespace {
std::atomic<bool> g_capturedBypass{false};
}

DdcRoute ddc_map(bool mox, bool ps_armed, bool /*rx2_enabled*/,
                 lyra::rig::RadioFamily family) {
    DdcRoute r;
    if (!ps_armed) {
        return r;
    }
    r.puresignalRun = true;
    const bool live = mox && ps_armed;
    if (!live) {
        return r;
    }
    r.pauseSub = true;
    r.bypassCapturedProfile = true;
    r.feedPsccFromDdc0Ddc1 = true;
    r.psRxDdc = 0;
    r.psTxDdc = 1;
    if (family == lyra::rig::RadioFamily::Hl2) {
        r.adcCntrl1 = 4;  // Thetis cntrl1=4
    }
    if (family == lyra::rig::RadioFamily::BrickP2) {
        r.alexPsBit = true;
        r.adcCntrl1 = 0;  // P2 uses ALEX_PS_BIT, not P1 C&C
    }
    return r;
}

void set_captured_profile_ps_bypass(bool on) {
    g_capturedBypass.store(on, std::memory_order_relaxed);
}

bool captured_profile_ps_bypass() {
    return g_capturedBypass.load(std::memory_order_relaxed);
}

}  // namespace lyra::ps

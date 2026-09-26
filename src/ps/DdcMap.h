// Lyra — PureSignal DDC / mux state product.
// See docs/architecture/puresignal_hl2.md.

#pragma once

#include "rig/RadioCapabilities.h"

namespace lyra::ps {

struct DdcRoute {
    bool pauseSub = false;
    bool bypassCapturedProfile = false;
    bool feedPsccFromDdc0Ddc1 = false;
    int  adcCntrl1 = 0;          // Thetis HL2: 4 while MOX+PS
    bool puresignalRun = false;  // C2 bit 6 frames 11/16
    int  psRxDdc = 0;            // coupler → pscc rx
    int  psTxDdc = 1;            // TX replica → pscc tx
    bool alexPsBit = false;      // P7 Brick P2 ALEX_PS_BIT
};

// State product, not a static DDC2/3 table.
// Live HL2 MOX+PS (Thetis): cntrl1=4, DDC0+DDC1 at TX freq → pscc.
// Idle psDdcFirst=2 is DeskHPSDR numbering only — not this mux.
DdcRoute ddc_map(bool mox, bool ps_armed, bool rx2_enabled,
                 lyra::rig::RadioFamily family);

void set_captured_profile_ps_bypass(bool on);
bool captured_profile_ps_bypass();

}  // namespace lyra::ps

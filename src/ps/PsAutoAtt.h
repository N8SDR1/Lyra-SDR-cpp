// DeskHPSDR ps_menu.c::ps_calibration_timer auto-att math.
// Hunt from GetPSInfo[4] (FB), never from cal-count [5]. A muted
// coupler (FB=0, pad 31) must still step ATT down or collect never starts.

#pragma once

#include <cmath>

namespace lyra::ps {

inline int autoAttDeltaDb(int fb) {
    if (fb > 275)
        return 15;
    if (fb < 25)
        return -15;
    return static_cast<int>(
        std::lround(20.0 * std::log10(static_cast<double>(fb) / 152.293)));
}

// P2 (min>=0): Desk 165 / 140. HL2: Thetis 181 / 128.
inline bool autoAttNeedsStep(int fb, int attDb, int minDb, int maxDb) {
    (void)maxDb;
    if (minDb >= 0)
        return fb > 165 || (fb < 140 && attDb > minDb);
    return fb > 181 || (fb <= 128 && attDb > minDb);
}

}  // namespace lyra::ps

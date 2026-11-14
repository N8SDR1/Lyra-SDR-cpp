// Greg VK3KV deadlock: pad 31, FB 0, cal never completes.
// DeskHPSDR hunts ATT from FB; Thetis-style cal-count hunt does not.

#include "ps/PsAutoAtt.h"

#include <cstdio>

int main() {
    using lyra::ps::autoAttDeltaDb;
    using lyra::ps::autoAttNeedsStep;

    int fails = 0;
    auto expect = [&](bool ok, const char *msg) {
        if (!ok) {
            std::fprintf(stderr, "FAIL: %s\n", msg);
            ++fails;
        }
    };

    expect(autoAttNeedsStep(0, 31, 0, 31), "P2 FB=0 at pad 31 must hunt");
    expect(autoAttDeltaDb(0) == -15, "starved FB steps −15 dB");
    expect(autoAttNeedsStep(0, 0, 0, 31) == false,
           "P2 already at 0 dB, nowhere to go");
    expect(autoAttNeedsStep(152, 16, 0, 31) == false,
           "in-window FB must not hunt");
    expect(autoAttNeedsStep(200, 0, 0, 31), "hot FB must hunt up");
    expect(autoAttNeedsStep(0, 31, -28, 31), "HL2 FB=0 at 31 must hunt");
    expect(autoAttDeltaDb(24) == -15, "FB<25 is the coarse −15 step");

    if (fails) {
        std::fprintf(stderr, "%d fail(s)\n", fails);
        return 1;
    }
    std::puts("test_ps_att_hunt: ok");
    return 0;
}

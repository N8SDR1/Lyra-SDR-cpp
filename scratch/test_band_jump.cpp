// Offline test for lyra::bandJumpCrosses (src/bands.h) — the TX band-change
// lockout's "does this one-click jump leave the current amateur band?" rule.
//   build: cmake --build build --target test_band_jump
//   run:   build/test_band_jump
#include "bands.h"

#include <cstdio>

using lyra::bandJumpCrosses;

static int g_failures = 0;

static void check(const char *name, bool got, bool want)
{
    const bool ok = got == want;
    std::printf("  %-52s got=%d want=%d  %s\n", name, got, want,
                ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

int main()
{
    std::printf("test_band_jump\n");
    check("same band (20m, 14.074 -> 14.200)", bandJumpCrosses(14074000, 14200000), false);
    check("different band (20m -> 40m)",        bandJumpCrosses(14200000, 7100000), true);
    check("in-band -> out-of-band (20m -> 14.5)", bandJumpCrosses(14200000, 14500000), true);
    check("out-of-band -> in-band (WWV 10 -> 30m)", bandJumpCrosses(10000000 - 200000, 10100000), true);
    check("out-of-band -> out-of-band (WWV 10 -> 15)", bandJumpCrosses(10000000, 15000000), true);
    check("out-of-band -> out-of-band (WWV 5 -> 15)", bandJumpCrosses(5000000, 15000000), true);
    check("same gap (WWV 10.000 -> 10.050)",     bandJumpCrosses(10000000, 10050000), false);
    check("below 160m (0.5 -> 1.0 MHz)",         bandJumpCrosses(500000, 1000000), false);
    check("below 160m -> 80/160 gap (1.0 -> 2.5)", bandJumpCrosses(1000000, 2500000), true);
    check("above 6m, same region (60 -> 70 MHz)", bandJumpCrosses(60000000, 70000000), false);
    check("low edge inclusive (20m 14.000)",    bandJumpCrosses(14200000, 14000000), false);
    check("high edge inclusive (20m 14.350)",   bandJumpCrosses(14200000, 14350000), false);
    check("just below low edge (13.999999)",    bandJumpCrosses(14200000, 13999999), true);
    check("just above high edge (14.350001)",   bandJumpCrosses(14200000, 14350001), true);
    check("transverter 144 MHz from 20m",       bandJumpCrosses(14200000, 144200000), true);

    std::printf(g_failures ? "FAILED (%d)\n" : "ALL PASS (0 failures)\n", g_failures);
    return g_failures ? 1 : 0;
}

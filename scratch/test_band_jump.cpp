// Offline test for lyra::bandJumpCrosses — TX band-change ignore rule.
// cmake --build build --target test_band_jump && build/test_band_jump
#include "bands.h"

#include <cstdio>

using lyra::bandJumpCrosses;

static int g_failures = 0;

static void check(const char *name, bool got, bool want)
{
    const bool ok = got == want;
    std::printf("  %-52s got=%d want=%d %s\n", name, got, want,
                ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

int main()
{
    std::printf("test_band_jump\n");
    check("same band (20m)", bandJumpCrosses(14074000, 14200000), false);
    check("20m -> 40m", bandJumpCrosses(14200000, 7100000), true);
    check("20m -> 14.5 out", bandJumpCrosses(14200000, 14500000), true);
    check("WWV 10 -> 30m", bandJumpCrosses(9800000, 10100000), true);
    check("WWV 5 -> 15", bandJumpCrosses(5000000, 15000000), true);
    check("same gap WWV 10", bandJumpCrosses(10000000, 10050000), false);
    check("below 160m same region", bandJumpCrosses(500000, 1000000), false);
    check("1.0 -> 2.5 crosses gaps", bandJumpCrosses(1000000, 2500000), true);
    check("above 6m same region", bandJumpCrosses(60000000, 70000000), false);
    check("20m low edge", bandJumpCrosses(14200000, 14000000), false);
    check("20m high edge", bandJumpCrosses(14200000, 14350000), false);
    check("just below 20m", bandJumpCrosses(14200000, 13999999), true);
    check("xvtr 144 from 20m", bandJumpCrosses(14200000, 144200000), true);

    std::printf(g_failures ? "FAILED (%d)\n" : "ALL PASS (0 failures)\n", g_failures);
    return g_failures ? 1 : 0;
}

// Offline test for lyra::ui::meterstyle (src/meter_styles.h) — the saved
// meter style index.  Kept faces load unchanged, each retired index maps to
// its nearest kept face, and anything outside the range clamps.
//   build: cmake --build build --target test_meter_styles
//   run:   build/test_meter_styles
#include "meter_styles.h"

#include <cstdio>

using namespace lyra::ui::meterstyle;

static int g_failures = 0;

static void check(const char *name, int got, int want)
{
    const bool ok = got == want;
    std::printf("  %-44s got=%3d want=%3d  %s\n", name, got, want,
                ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

int main()
{
    std::printf("test_meter_styles\n");

    // Kept faces load as themselves.
    for (int s : {0, 1, 2, 7, 10, 11, 12, 16, 17}) {
        char name[48];
        std::snprintf(name, sizeof name, "kept %d", s);
        check(name, keptStyle(s), s);
    }
    check("kept Cluster (17)",           keptStyle(Cluster), Cluster);
    check("Cluster is a new index",      Cluster, 17);
    check("kStyleMax is the last face",  kStyleMax, Cluster);

    // Retired faces.
    check("3 Porthole -> Iris",          keptStyle(3), 11);
    check("4 Fader -> Iris",             keptStyle(4), 11);
    check("5 Nixie -> VFD",              keptStyle(5), 7);
    check("6 Numitron -> VFD",           keptStyle(6), 7);
    check("8 Dekatron -> VFD",           keptStyle(8), 7);
    check("9 Panaplex -> VFD",           keptStyle(9), 7);
    check("13 Ingot -> Plasma Bar",      keptStyle(13), 1);
    check("14 Sunburst -> Phosphor",     keptStyle(14), 16);
    check("15 Lamps -> Phosphor",        keptStyle(15), 16);

    // Out of range.
    check("-1 clamps to Arc",            keptStyle(-1), 0);
    check("-100 clamps to Arc",          keptStyle(-100), 0);
    check("kStyleMax+1 clamps to max",   keptStyle(kStyleMax + 1), kStyleMax);
    check("999 clamps to max",           keptStyle(999), kStyleMax);

    // Every mapped value is itself a kept face (no chains).
    for (int s = -2; s <= kStyleMax + 2; ++s) {
        char name[48];
        std::snprintf(name, sizeof name, "keptStyle(%d) is stable", s);
        check(name, keptStyle(keptStyle(s)), keptStyle(s));
    }

    // Faces that read the ladder rows.
    check("Ladder uses ladder rows",     usesLadderRows(VerticalLadder), 1);
    check("Cluster uses ladder rows",    usesLadderRows(Cluster), 1);
    check("13 (retired) does not",       usesLadderRows(13), 0);
    check("Arc does not",                usesLadderRows(HorizonArc), 0);

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASS",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

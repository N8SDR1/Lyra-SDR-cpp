// Offline test for lyra::ui::migrateVfoStyleId (src/vfo_style_migrate.h) —
// the saved VFO-style id of a removed style maps to its successor on load.
//   build: cmake --build build --target test_vfo_style_migrate
//   run:   build/test_vfo_style_migrate
#include "vfo_style_migrate.h"

#include <cstdio>

using lyra::ui::migrateVfoStyleId;

static int g_failures = 0;

static void check(const char *name, const QString &got, const QString &want)
{
    const bool ok = got == want;
    std::printf("  %-40s got=%-10s want=%-10s %s\n", name, qPrintable(got),
                qPrintable(want), ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

int main()
{
    std::printf("test_vfo_style_migrate\n");
    check("aluminum -> lyraMac", migrateVfoStyleId("aluminum"), "lyraMac");
    check("empty (first run) unchanged", migrateVfoStyleId(""), "");
    check("lyraMac unchanged", migrateVfoStyleId("lyraMac"), "lyraMac");
    check("vfd unchanged", migrateVfoStyleId("vfd"), "vfd");
    check("unknown id unchanged", migrateVfoStyleId("bogus"), "bogus");
    check("case-sensitive: Aluminum unchanged", migrateVfoStyleId("Aluminum"), "Aluminum");
    std::printf(g_failures ? "FAILED (%d)\n" : "ALL PASS\n", g_failures);
    return g_failures ? 1 : 0;
}

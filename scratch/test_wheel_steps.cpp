// Offline test for lyra::ui::wheelDigitSteps (src/wheel_steps.h) — how many
// digit steps the VFO frequency display takes for one wheel event.
// Deltas below are the real per-notch values logged from a smooth-scrolling
// mouse on macOS (±24/26 slow, ±206 normal, ±410 fast), plus Windows-style
// 120-unit notches, a hi-res wheel's fractions, and a trackpad gesture.
//   build: cmake --build build --target test_wheel_steps
//   run:   build/test_wheel_steps
#include "wheel_steps.h"

#include <cstdio>

using lyra::ui::wheelDigitSteps;

static int g_failures = 0;

static void check(const char *name, int got, int want)
{
    const bool ok = got == want;
    std::printf("  %-60s got=%3d want=%3d  %s\n", name, got, want,
                ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

int main()
{
    std::printf("test_wheel_steps\n");
    int acc = 0;

    // macOS: each un-phased event is one notch, whatever its size.
    const bool mac = true;
    check("mac notch +24 (slow)",  wheelDigitSteps(24,   Qt::NoScrollPhase, mac, acc), 1);
    check("mac notch -26 (slow)",  wheelDigitSteps(-26,  Qt::NoScrollPhase, mac, acc), -1);
    check("mac notch +206",        wheelDigitSteps(206,  Qt::NoScrollPhase, mac, acc), 1);
    check("mac notch +410 (fast)", wheelDigitSteps(410,  Qt::NoScrollPhase, mac, acc), 1);
    check("mac notch -410 (fast)", wheelDigitSteps(-410, Qt::NoScrollPhase, mac, acc), -1);
    check("mac zero delta",        wheelDigitSteps(0,    Qt::NoScrollPhase, mac, acc), 0);

    // Elsewhere (Windows): 120 per notch, unchanged; multiples step several.
    const bool win = false;
    acc = 0;
    check("win notch +120",        wheelDigitSteps(120,  Qt::NoScrollPhase, win, acc), 1);
    check("win notch -120",        wheelDigitSteps(-120, Qt::NoScrollPhase, win, acc), -1);
    check("win double notch +240", wheelDigitSteps(240,  Qt::NoScrollPhase, win, acc), 2);
    // Hi-res wheel: a notch arrives as 4 x 30 — previously all dropped.
    acc = 0;
    int sum = 0;
    for (int i = 0; i < 4; ++i)
        sum += wheelDigitSteps(30, Qt::NoScrollPhase, win, acc);
    check("win hi-res 4 x +30 = one notch", sum, 1);
    acc = 0;
    sum = 0;
    for (int i = 0; i < 3; ++i)
        sum += wheelDigitSteps(-30, Qt::NoScrollPhase, win, acc);
    check("win hi-res 3 x -30 = not yet a notch", sum, 0);

    // Trackpad gesture (phased) on either platform: 120 per step, and a new
    // gesture starts from zero.
    for (const bool platformMac : {true, false}) {
        acc = 0;
        sum  = wheelDigitSteps(50,  Qt::ScrollBegin,    platformMac, acc);
        sum += wheelDigitSteps(50,  Qt::ScrollUpdate,   platformMac, acc);
        sum += wheelDigitSteps(50,  Qt::ScrollUpdate,   platformMac, acc);
        sum += wheelDigitSteps(100, Qt::ScrollMomentum, platformMac, acc);
        sum += wheelDigitSteps(0,   Qt::ScrollEnd,      platformMac, acc);
        check(platformMac ? "mac trackpad 250 units = 2 steps"
                          : "win trackpad 250 units = 2 steps", sum, 2);
        // Leftover 10 units must not leak into the next gesture.
        sum = wheelDigitSteps(110, Qt::ScrollBegin, platformMac, acc);
        check(platformMac ? "mac new gesture starts from zero"
                          : "win new gesture starts from zero", sum, 0);
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASS",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}

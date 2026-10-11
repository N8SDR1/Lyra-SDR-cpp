// Offline test for lyra::ui::digitPlaceAt / digitRectsFromVariant
// (src/vfo_digit_hit.h) — how a click or wheel position on a VFO style maps
// to a digit place, given the per-digit rects the style reports
// (index = place: 0 = Hz units … 9 = 1 GHz).
//   build: cmake --build build --target test_vfo_digit_hit
//   run:   build/test_vfo_digit_hit
#include "vfo_digit_hit.h"

#include <QString>
#include <cstdio>

using lyra::ui::digitPlaceAt;
using lyra::ui::digitRectsFromVariant;

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
    std::printf("test_vfo_digit_hit\n");

    // Ten 20x30 cells left→right; the leftmost cell is place 9.
    QVariantList row;
    for (int place = 0; place < 10; ++place)
        row.append(QRectF((9 - place) * 20.0, 0.0, 20.0, 30.0));
    const auto rects = digitRectsFromVariant(row, 10);
    check("ten slots",                         int(rects.size()), 10);
    check("leftmost cell -> place 9",          digitPlaceAt(rects, {5, 10}), 9);
    check("rightmost cell -> place 0",         digitPlaceAt(rects, {195, 10}), 0);
    check("fourth cell -> place 6",            digitPlaceAt(rects, {65, 10}), 6);
    check("below the row -> none",             digitPlaceAt(rects, {65, 40}), -1);
    check("left of the row -> none",           digitPlaceAt(rects, {-1, 10}), -1);

    // A style that reports fewer rects: the missing places are never hit.
    QVariantList shortList;
    shortList.append(QRectF(0, 0, 10, 10));
    const auto s = digitRectsFromVariant(shortList, 10);
    check("short list still ten slots",        int(s.size()), 10);
    check("short list place 0 hit",            digitPlaceAt(s, {5, 5}), 0);
    check("short list missing place not hit",  digitPlaceAt(s, {50, 5}), -1);

    // Junk entries and empty rects (a hidden digit) are ignored.
    QVariantList junk;
    junk.append(QStringLiteral("nope"));
    junk.append(QRectF(0, 0, 0, 0));
    junk.append(QRectF(0, 0, 10, 10));
    const auto j = digitRectsFromVariant(junk, 10);
    check("junk and empty skipped, place 2 hit", digitPlaceAt(j, {5, 5}), 2);

    // A list longer than ten: extras ignored.
    QVariantList longList = row;
    longList.append(QRectF(500, 0, 20, 30));
    check("eleventh rect ignored", digitPlaceAt(digitRectsFromVariant(longList, 10), {505, 5}), -1);

    check("no rects -> none", digitPlaceAt(digitRectsFromVariant({}, 10), {5, 5}), -1);

    std::printf(g_failures ? "FAILED: %d\n" : "ALL PASS\n", g_failures);
    return g_failures ? 1 : 0;
}

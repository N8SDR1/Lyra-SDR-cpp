// Isolated P2 DDC0 Hz selection vs CTUN (no radio, no P2RxBridge).
// Build:  cmake --build build --target test_p2_ctune
// Run:    build\test_p2_ctune.exe

#include "wire/P2TuneHz.h"

#include <cstdio>
#include <cstdlib>

using lyra::wire::p2Ddc0RfHz;

static int g_fail = 0;

static void expectEq(quint32 got, quint32 want, const char *msg) {
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s (got %u want %u)\n", msg, got, want);
        ++g_fail;
    }
}

int main() {
    constexpr quint32 kDial   = 14'200'000u;
    constexpr quint32 kCentre = 14'187'000u;  // locked pan, ~13 kHz below dial
    constexpr int     kRit    = 250;

    expectEq(p2Ddc0RfHz(kDial, false, 0, 0), kDial,
             "CTUN off, no RIT → dial");
    expectEq(p2Ddc0RfHz(kDial, true, kRit, 0), kDial + kRit,
             "CTUN off, RIT on → dial+RIT");
    expectEq(p2Ddc0RfHz(kDial, false, kRit, 0), kDial,
             "CTUN off, RIT armed but disabled → dial");
    expectEq(p2Ddc0RfHz(100u, true, -250, 0), 0u,
             "CTUN off, RIT would go negative → 0");

    expectEq(p2Ddc0RfHz(kDial, false, 0, kCentre), kCentre,
             "CTUN on → centre, not dial");
    expectEq(p2Ddc0RfHz(kDial, true, kRit, kCentre), kCentre,
             "CTUN on → centre; RIT is WDSP shift, not DDC");
    expectEq(p2Ddc0RfHz(kCentre, false, 0, kCentre), kCentre,
             "CTUN on at engage (dial == centre) → centre");

    if (g_fail) {
        std::fprintf(stderr, "%d p2Ddc0RfHz assertion(s) failed\n", g_fail);
        return EXIT_FAILURE;
    }
    std::printf("test_p2_ctune: OK\n");
    return EXIT_SUCCESS;
}

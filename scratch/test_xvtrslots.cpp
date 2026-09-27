// Transverter RF → radio IF math.  Header-only; no HL2Stream.
//
// Build + run:  cmake --build build --target test_xvtrslots
//               ./build/test_xvtrslots    (exit 0 = all pass)

#include "xvtr_math.h"

#include <cstdio>

static int g_fail = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__,       \
                         #cond);                                               \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)
#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        const int _a = static_cast<int>(a);                                    \
        const int _b = static_cast<int>(b);                                    \
        if (_a != _b) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d  %s == %s  (%d != %d)\n",         \
                         __FILE__, __LINE__, #a, #b, _a, _b);                  \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

int main() {
    using lyra::xvtr::ddsHz;
    CHECK_EQ(ddsHz(144300000, 116000000, 0), 28300000);
    CHECK_EQ(ddsHz(432200000, 392000000, 0), 40200000);
    CHECK_EQ(ddsHz(14100000, 0, 0), 14100000);
    CHECK_EQ(ddsHz(144300000, 116000000, 100), 28299900);
    CHECK_EQ(ddsHz(10000000, 20000000, 0), 0);
    CHECK_EQ(ddsHz(0, 0, 1), 0);
    if (g_fail) {
        std::fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    std::fprintf(stdout, "test_xvtrslots: all pass\n");
    return 0;
}

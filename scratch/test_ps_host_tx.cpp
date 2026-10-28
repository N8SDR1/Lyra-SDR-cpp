// Host DUC IQ → coupler-pair TX replica (P2 interleaved DDC0).
// cmake --build build --target test_ps_host_tx
// build\test_ps_host_tx.exe

#include "ps/PsCalcThread.h"

#include <stdio.h>
#include <vector>

static int g_fail = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

int main() {
    // 8 host samples (192 kHz) → 4 coupler pairs (2:1).
    std::vector<double> host(16);
    for (int i = 0; i < 8; ++i) {
        host[2 * i]     = 10.0 + i;  // I
        host[2 * i + 1] = 20.0 + i;  // Q
    }
    std::vector<double> tx(8);
    CHECK(lyra::ps::copyHostTxForCoupler(host.data(), 8, tx.data(), 4) == 4);
    // Tail 8 host samples, take even indices: host 0,2,4,6
    CHECK(tx[0] == 10.0 && tx[1] == 20.0);
    CHECK(tx[2] == 12.0 && tx[3] == 22.0);
    CHECK(tx[4] == 14.0 && tx[5] == 24.0);
    CHECK(tx[6] == 16.0 && tx[7] == 26.0);

    // Short host: 1:1 from the tail.
    std::vector<double> shortHost{1, 2, 3, 4, 5, 6};
    std::vector<double> tx2(4);
    CHECK(lyra::ps::copyHostTxForCoupler(shortHost.data(), 3, tx2.data(), 2) == 2);
    CHECK(tx2[0] == 3 && tx2[1] == 4);
    CHECK(tx2[2] == 5 && tx2[3] == 6);

    CHECK(lyra::ps::copyHostTxForCoupler(host.data(), 8, tx.data(), 9) == 0);

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("test_ps_host_tx: ok\n");
    return 0;
}

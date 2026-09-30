// Pico I2C TX-freq byte pack (HL2IOBoard 0x1D BYTE4..BYTE0).
// Must match lyra::wire::pack_hl2_ioboard_tx_freq_bytes in FrameComposer.h.
// Wire-inert: no stream, no event loop, no Qt.
//
// cmake --build build --target test_hl2_ioboard_i2c
// build\test_hl2_ioboard_i2c.exe

#include <stdio.h>

static void pack_hl2_ioboard_tx_freq_bytes(unsigned long long hz,
                                           unsigned char out[5])
{
    out[0] = static_cast<unsigned char>((hz >> 32) & 0xFFu);
    out[1] = static_cast<unsigned char>((hz >> 24) & 0xFFu);
    out[2] = static_cast<unsigned char>((hz >> 16) & 0xFFu);
    out[3] = static_cast<unsigned char>((hz >>  8) & 0xFFu);
    out[4] = static_cast<unsigned char>( hz        & 0xFFu);
}

static int g_fail = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__,       \
                         #cond);                                               \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

int main() {
    unsigned char b[5];
    pack_hl2_ioboard_tx_freq_bytes(14074000ull, b);
    CHECK(b[0] == 0);
    CHECK(b[1] == 0x00);
    CHECK(b[2] == 0xD6);
    CHECK(b[3] == 0xC0);
    CHECK(b[4] == 0x90);  // 14074000 = 0x00D6C090

    pack_hl2_ioboard_tx_freq_bytes(7000000ull, b);
    CHECK(b[0] == 0);
    CHECK(b[1] == 0x00);
    CHECK(b[2] == 0x6A);
    CHECK(b[3] == 0xCF);
    CHECK(b[4] == 0xC0);  // 7000000 = 0x006ACFC0

    pack_hl2_ioboard_tx_freq_bytes(0, b);
    CHECK(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0 && b[4] == 0);

    if (g_fail) {
        fprintf(stderr, "%d check(s) failed\n", g_fail);
        return 1;
    }
    printf("test_hl2_ioboard_i2c: ok\n");
    return 0;
}

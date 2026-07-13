// Offline sanity test for lyra::dsp::ZeroBeat (Qt-free).
// Feeds synthetic complex carriers at known baseband offsets and checks the
// estimator locks the right signed offset + gates on SNR.
//   build: _build_test_zerobeat.bat   run: scratch\test_zerobeat.exe
#include "../src/dsp/ZeroBeat.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using lyra::dsp::ZeroBeat;
static const double kPi = 3.14159265358979323846;

// Feed `secs` of a complex tone at f0 (Hz) + noise, in inSize blocks.
static void feedTone(ZeroBeat& zb, double sr, double f0, double amp,
                     double noise, double secs, int inSize) {
    std::mt19937 rng(1234);
    std::normal_distribution<double> nd(0.0, noise);
    const int total = (int)(sr * secs);
    std::vector<double> blk(2 * inSize);
    double ph = 0.0, dph = 2.0 * kPi * f0 / sr;
    int done = 0;
    while (done < total) {
        int n = std::min(inSize, total - done);
        for (int i = 0; i < n; ++i) {
            blk[2*i+0] = amp * std::cos(ph) + nd(rng);
            blk[2*i+1] = amp * std::sin(ph) + nd(rng);
            ph += dph;
        }
        zb.process(blk.data(), n);
        done += n;
    }
}

static int check(const char* name, double got, double want, double tolHz,
                 bool valid, bool wantValid) {
    bool ok = (valid == wantValid) &&
              (!wantValid || std::fabs(got - want) <= tolHz);
    std::printf("  %-22s got=%+8.1f Hz want=%+8.1f valid=%d(want %d)  %s\n",
                name, got, want, valid, wantValid, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int main() {
    const double sr = 192000.0;
    const int inSize = 2048;
    int fails = 0;

    ZeroBeat zb; zb.setSampleRate(sr);

    zb.reset(); feedTone(zb, sr, +300.0, 0.5, 0.01, 0.6, inSize);
    fails += check("carrier +300 Hz", zb.offsetHz(), +300.0, 12.0, zb.valid(), true);

    zb.reset(); feedTone(zb, sr, -450.0, 0.5, 0.01, 0.6, inSize);
    fails += check("carrier -450 Hz", zb.offsetHz(), -450.0, 12.0, zb.valid(), true);

    zb.reset(); feedTone(zb, sr, +25.0, 0.5, 0.01, 0.6, inSize);
    fails += check("carrier +25 Hz (lock)", zb.offsetHz(), +25.0, 8.0, zb.valid(), true);

    // Pure noise, no carrier → must NOT validate (gate rejects random peaks).
    zb.reset(); feedTone(zb, sr, 0.0, 0.0, 1.0, 1.0, inSize);
    fails += check("noise only", zb.offsetHz(), 0.0, 0.0, zb.valid(), false);

    std::printf("\n%s (%d failures)\n", fails ? "*** FAIL ***" : "ALL PASS", fails);
    return fails ? 1 : 0;
}

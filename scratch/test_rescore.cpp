// DeepFist CTC-lattice rescorer unit test — verifies ctcNll against PyTorch
// F.ctc_loss reference values (from diddle dsp/rescore.rs tests) and that
// rescoreCalls flips a lattice-supported candidate. Qt-free, no ONNX Runtime.
//   cmake --build build --target test_rescore  &&  build/test_rescore
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include "dsp/deepfist/DeepFistRescore.h"
#include "dsp/deepfist/DeepFistScp.h"

using namespace lyra::dsp;

static int fails = 0;
static void check(bool ok, const std::string& msg) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", msg.c_str());
    if (!ok) ++fails;
}

int main() {
    // Reference lattice + nlls from PyTorch F.ctc_loss (reduction="sum"), blank=0.
    const int T = 5, C = 3;
    const std::vector<float> LP = {
        -0.464369f, -1.464369f, -1.964369f,
        -2.491498f, -0.191498f, -2.391498f,
        -0.632166f, -1.732166f, -1.232166f,
        -2.380924f, -1.880924f, -0.280924f,
        -0.370524f, -1.970524f, -1.770524f,
    };
    struct Case { std::vector<int> t; float want; };
    const Case cases[] = {
        {{1, 2}, 0.675926f}, {{1}, 2.914297f}, {{1, 1}, 2.875531f}, {{2, 1, 2}, 2.393456f},
    };
    std::puts("ctcNll vs PyTorch F.ctc_loss:");
    for (const auto& c : cases) {
        const float got = ctcNll(LP.data(), T, C, c.t);
        char m[64]; std::snprintf(m, sizeof m, "got %.6f want %.6f", got, c.want);
        check(std::fabs(got - c.want) < 1e-4f, m);
    }
    std::puts("ctcNll impossible targets:");
    check(std::isinf(ctcNll(LP.data(), T, C, {})), "empty -> inf");
    check(std::isinf(ctcNll(LP.data(), T, C, {1, 2, 1, 2, 1, 2})), "longer than T -> inf");
    check(std::isfinite(ctcNll(LP.data(), T, C, {1, 1, 1})), "[1,1,1] finite");
    check(std::isinf(ctcNll(LP.data(), T, C, {1, 1, 1, 1})), "[1,1,1,1] -> inf");

    std::puts("rescoreCalls flips A1B -> A1C:");
    {
        const std::string scpFile =
            std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/lyra_toy_scp.txt";
        { std::ofstream f(scpFile); f << "A1C\nA1B\nB1C\n"; }
        DeepFistScp scp; scp.loadFile(scpFile);
        const std::vector<std::string> tokens = {"", " ", "A", "B", "C", "1"};
        const int t = 7, c = 6;
        std::vector<float> lp;
        for (int sym : {0, 2, 0, 5, 0, 4, 0})       // audio says A(2) 1(5) C(4)
            for (int k = 0; k < c; ++k) lp.push_back(k == sym ? -0.05f : -6.0f);
        const std::vector<int> ids = {2, 5, 3};      // greedy (wrongly) "A1B"
        const auto res = rescoreCalls(lp.data(), t, c, ids, tokens, scp);
        check(res.size() == 1, "one verdict");
        if (res.size() == 1) {
            check(res[0].orig == "A1B", "orig=" + res[0].orig);
            check(res[0].best == "A1C", "best=" + res[0].best);
            check(res[0].confident, "confident");
        }
    }
    std::printf("\n%s (%d failures)\n", fails == 0 ? "ALL PASS" : "FAILURES", fails);
    return fails;
}

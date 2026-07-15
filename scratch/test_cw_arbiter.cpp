// Lyra — CwArbiter unit tests (Auto CW engine ownership/state machine).
// Qt-free, ONNX-free.  Build + run:
//   cmake --build build --target test_cw_arbiter && build/test_cw_arbiter
#include "dsp/CwArbiter.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using lyra::dsp::CwArbiter;

namespace {
int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

// Capture arbiter output as (text, fallback) pairs and the concatenated string.
struct Sink {
    std::vector<std::pair<std::string,bool>> runs;
    std::string text;
    void attach(CwArbiter& a) {
        a.onOutput = [this](const std::string& s, bool fb) {
            runs.push_back({s, fb}); text += s;
        };
    }
    bool anyFallback() const {
        for (auto& r : runs) if (r.second) return true;
        return false;
    }
};
}  // namespace

int main() {
    // 1) Solid signal: DeepFist owns, output is not fallback.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(50.0f);
        a.pushDeepFist("C"); a.pushDeepFist("Q");
        CHECK(a.owner() == CwArbiter::Source::DeepFist);
        CHECK(k.text == "CQ");
        CHECK(!k.anyFallback());
    }

    // 2) Fade: DeepFist gates (one space then silence), we fall back to Classic
    //    and keep producing text — never silent.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(50.0f); a.pushDeepFist("C");     // DeepFist solid, owns
        a.updateKeying(4.0f);                            // fade -> desired=Classic (nFall=1)
        a.pushDeepFist(" ");                             // DeepFist idle space -> switch here
        CHECK(a.owner() == CwArbiter::Source::Classic);
        a.pushDeepFist("X");                             // muted engine -> dropped
        a.pushClassic("W"); a.pushClassic("1");          // Classic drives, fallback
        CHECK(k.text == "C W1");
        CHECK(k.anyFallback());
        CHECK(k.runs.back() == std::make_pair(std::string("1"), true));
    }

    // 3) Recovery hysteresis: one solid tick is NOT enough (nRise=3); three are.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(4.0f); a.pushDeepFist(" ");       // in fade, Classic owns
        CHECK(a.owner() == CwArbiter::Source::Classic);
        a.updateKeying(50.0f);                           // 1 solid
        a.pushClassic(" ");                              // gap, but desired still Classic
        CHECK(a.owner() == CwArbiter::Source::Classic);
        a.updateKeying(50.0f); a.updateKeying(50.0f);    // 3 solid -> desired=DeepFist
        a.pushClassic(" ");                              // gap -> switch back
        CHECK(a.owner() == CwArbiter::Source::DeepFist);
    }

    // 4) Dead-band dither (tLow..tHigh) causes no ownership flip.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(50.0f);                           // DeepFist owns
        for (int i = 0; i < 20; ++i) a.updateKeying(15.0f); // between 12 and 20
        a.pushDeepFist(" ");
        CHECK(a.owner() == CwArbiter::Source::DeepFist);
    }

    // 5) No-dup / no-drop: only the owner's chars appear, exactly once, in order.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(50.0f);
        a.pushDeepFist("A"); a.pushClassic("z");         // z dropped (muted)
        a.updateKeying(4.0f); a.pushDeepFist(" ");       // fade -> switch to Classic
        a.pushClassic("B"); a.pushDeepFist("q");         // q dropped (muted)
        CHECK(k.text == "A B");
    }

    // 6) Switch only at a gap: a pending switch waits for a space.
    {
        CwArbiter a; Sink k; k.attach(a);
        a.updateKeying(4.0f);                            // desired=Classic immediately
        a.pushDeepFist("N"); a.pushDeepFist("R");        // no gap yet -> DeepFist still owns
        CHECK(a.owner() == CwArbiter::Source::DeepFist);
        CHECK(k.text == "NR");
        a.pushDeepFist(" ");                             // gap -> now switch
        CHECK(a.owner() == CwArbiter::Source::Classic);
    }

    if (g_fail == 0) std::printf("test_cw_arbiter: ALL PASS\n");
    else             std::printf("test_cw_arbiter: %d CHECK(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}

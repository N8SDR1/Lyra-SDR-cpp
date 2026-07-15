// Lyra — ScpLocal unit tests (RBN-confirmed local callsign list, Phase 3).
// Qt-free.  Build + run:
//   cmake --build build --target test_scp_local && build/test_scp_local
#include "dsp/deepfist/ScpLocal.h"

#include <cstdio>
#include <string>

using lyra::dsp::ScpLocal;

namespace {
int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

const char* kPath = "test_scp_local_tmp.txt";
const long long kDay = 86400;
}  // namespace

int main() {
    std::remove(kPath);
    const long long now = 1800000000;   // fixed epoch: deterministic tests

    // 1) Fresh store: empty, note() adds, contains() sees it, count grows.
    {
        ScpLocal db;
        CHECK(db.load(kPath) == 0);
        CHECK(!db.contains("NA2DX"));
        CHECK(db.note("NA2DX", now));           // new -> changed
        CHECK(db.contains("NA2DX"));
        CHECK(db.count() == 1);
        CHECK(!db.note("NA2DX", now));          // same call same day -> no churn
        db.note("K7CO", now);
        CHECK(db.count() == 2);
    }

    // 2) Persistence round-trip: a second instance reads what the first wrote.
    {
        ScpLocal db;
        CHECK(db.load(kPath) == 2);
        CHECK(db.contains("NA2DX"));
        CHECK(db.contains("K7CO"));
        // calls() is sorted for the SCP merge.
        auto v = db.calls();
        CHECK(v.size() == 2 && v[0] == "K7CO" && v[1] == "NA2DX");
    }

    // 3) Timestamp refresh: noting an existing call >1 day later persists the
    //    newer time (protects it from age-out), and reports changed.
    {
        ScpLocal db;
        db.load(kPath);
        CHECK(db.note("K7CO", now + 2 * kDay));  // refresh -> changed
    }

    // 4) Age-out on load: entries older than maxAgeDays are dropped.
    {
        ScpLocal db;
        // NA2DX stamped `now`, K7CO stamped now+2d.  Load "1 day" after K7CO's
        // stamp with maxAgeDays=1: NA2DX (3 days old) ages out, K7CO survives.
        // load() measures age against the NEWEST entry (no wall clock in tests).
        CHECK(db.load(kPath, /*maxAgeDays=*/1) == 1);
        CHECK(db.contains("K7CO"));
        CHECK(!db.contains("NA2DX"));
    }

    // 5) Cap eviction: oldest timestamps evicted first.
    {
        std::remove(kPath);
        ScpLocal db;
        db.load(kPath, 365, /*cap=*/3);
        db.note("A1AA", now + 1);
        db.note("B2BB", now + 2);
        db.note("C3CC", now + 3);
        db.note("D4DD", now + 4);               // overflows cap=3 -> evict A1AA
        CHECK(db.count() == 3);
        CHECK(!db.contains("A1AA"));
        CHECK(db.contains("D4DD"));
    }

    // 6) Garbage tolerance: malformed lines are skipped, not fatal.
    {
        std::remove(kPath);
        FILE* f = std::fopen(kPath, "w");
        std::fputs("NA2DX\t1800000000\n"
                   "not a valid line\n"
                   "\n"
                   "K7CO\t1800000001\n", f);
        std::fclose(f);
        ScpLocal db;
        CHECK(db.load(kPath) == 2);
    }

    std::remove(kPath);
    if (g_fail == 0) std::printf("test_scp_local: ALL PASS\n");
    else             std::printf("test_scp_local: %d CHECK(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}

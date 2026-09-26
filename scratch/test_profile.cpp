// Lyra — unit test for the Stage-0 profile engine (Profile / ProfileStore
// / ProfileManager).  Plain-assert harness (the scratch/test_* style),
// Qt Core only.  Build+run via _build_test_profile.bat.
//
// Verifies: JSON round-trip, store CRUD + order + modeBind cleanup,
// manager save/load/dirty, the mid-TX apply guard, and per-mode recall.

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryFile>
#include <QDir>
#include <cstdio>

#include "profile/Profile.h"
#include "profile/ProfileStore.h"
#include "profile/ProfileBindings.h"
#include "profile/ProfileManager.h"

using namespace lyra::profile;

static int g_fail = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } \
} while (0)

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    // --- 1. Profile JSON round-trip ---
    {
        Profile p;
        p.name = "X"; p.rxBandwidth = 2700; p.txBandwidth = 3100;
        p.bwLocked = true; p.filterLow = 50; p.micSource = "tci";
        p.micGainDb = 4.5; p.micBoost = true; p.tuneDriveMode = 1;
        p.tuneDrivePct = 25; p.txDriveLevel = 200; p.tciRxGainDb = -3.0;
        p.tciTxGainDb = 2.0; p.agcMode = "slow"; p.autoMuteOnTx = false;
        p.txTimeoutSec = 300; p.txTimeoutBypass = true;
        Profile q = Profile::fromJson("X", p.toJson());
        CHECK(p.sameValues(q));
        CHECK(q.name == "X");
        // tolerant fromJson: missing keys keep defaults
        Profile def = Profile::fromJson("Y", QJsonObject{});
        CHECK(def.micSource == "mic1");

        // v3 native rack blobs (#49): round-trip + dirty-detect + clean omit.
        Profile r; r.name = "R";
        QJsonObject eqj;  eqj["bypass"] = false; eqj["makeupDb"] = 2.0;
        QJsonObject plj;  plj["mix"] = 0.07;     plj["decayS"]   = 1.5;
        r.eq = eqj; r.plate = plj;
        Profile r2 = Profile::fromJson("R", r.toJson());
        CHECK(r2.schemaVersion == r.schemaVersion);
        CHECK(r.sameValues(r2));
        CHECK(r2.eq == eqj);
        CHECK(r2.plate == plj);
        // any rack-blob change flags ● modified
        Profile r3 = r2; QJsonObject eqj2 = eqj; eqj2["makeupDb"] = 5.0; r3.eq = eqj2;
        CHECK(!r.sameValues(r3));
        // empty blobs are omitted from the JSON (pre-rack profiles stay clean)
        Profile e; e.name = "E";
        CHECK(!e.toJson().contains("eq"));
        CHECK(!e.toJson().contains("plate"));

        // v6 VAC2 (#103 V2-4): round-trip + dirty-detect + pre-v6 omit
        // keeps defaults (enabled off).
        Profile v; v.name = "V";
        v.vac2Enabled = true; v.vac2AutoDigital = true;
        v.vac2RxGainDb = -6.0; v.vac2TxGainDb = 1.5;
        v.vac2LatencyMs = 40; v.vac2VacSize = 512;
        Profile v2 = Profile::fromJson("V", v.toJson());
        CHECK(v.sameValues(v2));
        CHECK(v2.vac2Enabled && v2.vac2AutoDigital);
        CHECK(v2.vac2LatencyMs == 40 && v2.vac2VacSize == 512);
        Profile v3 = v2; v3.vac2Enabled = false;
        CHECK(!v.sameValues(v3));
        Profile preV6 = Profile::fromJson("Z", QJsonObject{});
        CHECK(!preV6.vac2Enabled);
        CHECK(preV6.vac2TxGainDb == 3.0);
        CHECK(preV6.vac2LatencyMs == 120);
    }

    // --- temp QSettings (no registry/app pollution) ---
    QString iniPath = QDir::tempPath() + "/lyra_test_profile.ini";
    QFile::remove(iniPath);
    QSettings settings(iniPath, QSettings::IniFormat);
    ProfileStore store(&settings);

    // --- 2. ProfileStore CRUD ---
    {
        Profile a; a.name = "A"; a.micGainDb = 5.0;
        Profile b; b.name = "B"; b.micGainDb = 6.0;
        store.put(a); store.put(b);
        CHECK(store.names() == QStringList({"A", "B"}));   // insertion order
        CHECK(store.contains("A"));
        CHECK(store.get("A").micGainDb == 5.0);
        store.setActive("A"); CHECK(store.active() == "A");
        store.setDefault("B"); CHECK(store.defaultName() == "B");
        store.setModeBinding("Digital", "B");              // bind by family
        CHECK(store.modeBinding("Digital") == "B");
        store.rename("A", "A2");
        CHECK(store.names() == QStringList({"A2", "B"}));   // order preserved
        CHECK(store.contains("A2") && !store.contains("A"));
        CHECK(store.active() == "A2");                      // active followed rename
        store.remove("B");                                  // also clears default + modeBind->B
        CHECK(store.names() == QStringList({"A2"}));
        CHECK(store.defaultName().isEmpty());
        CHECK(store.modeBinding("Digital").isEmpty());
    }

    // --- 3. ProfileManager (fake bindings over a local "live" state) ---
    // Own ini file: the block-2 `settings` object is still alive at
    // function scope and QSettings shares an in-memory cache across
    // instances on the same path, so reusing iniPath would leak block-2
    // state in.  A distinct file is the clean isolation.
    {
        const QString iniPath2 = QDir::tempPath() + "/lyra_test_profile_mgr.ini";
        QFile::remove(iniPath2);
        QSettings s2(iniPath2, QSettings::IniFormat);
        ProfileStore st(&s2);

        Profile live; live.micGainDb = 5.0;
        bool txActive = false;
        ProfileBindings b;
        b.capture    = [&] { return live; };
        b.apply      = [&](const Profile &p) { const QString n = live.name; live = p; live.name = n; };
        b.isTxActive = [&] { return txActive; };
        ProfileManager mgr(std::move(b), std::move(st));

        bool lastModified = false;
        QObject::connect(&mgr, &ProfileManager::modifiedChanged,
                         &mgr, [&](bool m) { lastModified = m; });

        mgr.saveAs("A");
        CHECK(mgr.activeName() == "A");
        CHECK(mgr.names().contains("A"));
        CHECK(!mgr.isModified());

        live.micGainDb = 9.0;                 // mutate live state
        mgr.refreshModified();
        CHECK(mgr.isModified());
        CHECK(lastModified == true);

        CHECK(mgr.load("A"));                  // recall
        CHECK(live.micGainDb == 5.0);          // restored
        CHECK(!mgr.isModified());
        CHECK(lastModified == false);

        // mid-TX guard: load is refused, live untouched
        live.micGainDb = 12.0; txActive = true;
        CHECK(!mgr.load("A"));
        CHECK(live.micGainDb == 12.0);
        txActive = false;

        // mode -> family mapping (sidebands collapse; others stand alone)
        CHECK(ProfileManager::modeFamily("USB")  == "SSB");
        CHECK(ProfileManager::modeFamily("LSB")  == "SSB");
        CHECK(ProfileManager::modeFamily("CWU")  == "CW");
        CHECK(ProfileManager::modeFamily("DIGL") == "Digital");
        CHECK(ProfileManager::modeFamily("AM")   == "AM");
        CHECK(ProfileManager::modeFamily("FM")   == "FM");

        // second profile + per-FAMILY auto-recall.  Bind the Digital
        // family to A; BOTH DIGU and DIGL must trigger the same recall.
        live.micGainDb = 20.0; mgr.saveAs("B");
        CHECK(mgr.names() == QStringList({"A", "B"}));
        mgr.bindMode("Digital", "A");
        live.micGainDb = 99.0;
        mgr.onModeChanged("DIGU");             // DIGU -> Digital -> recall A
        CHECK(live.micGainDb == 5.0);
        CHECK(mgr.activeName() == "A");
        // DIGL is the same family -> would recall A too, but A is already
        // active (bound==active early-out), so prove the family map via a
        // round-trip: load B, then DIGL must pull A again.
        CHECK(mgr.load("B"));
        live.micGainDb = 88.0;
        mgr.onModeChanged("DIGL");             // DIGL -> Digital -> recall A
        CHECK(live.micGainDb == 5.0);
        CHECK(mgr.activeName() == "A");
        mgr.onModeChanged("USB");              // SSB family unbound -> no change
        CHECK(mgr.activeName() == "A");

        // per-family recall is mid-TX guarded too.  Make active != bound
        // (load B) so onModeChanged actually attempts the A recall and
        // the TX guard — not the bound==active early-out — is what stops it.
        CHECK(mgr.load("B"));                  // active -> B
        live.micGainDb = 77.0; txActive = true;
        mgr.onModeChanged("DIGU");             // bound A != active B, but TX active
        CHECK(live.micGainDb == 77.0);         // not recalled mid-TX
        CHECK(mgr.activeName() == "B");         // active unchanged by the guarded recall
        txActive = false;
    }

    QFile::remove(iniPath);
    QFile::remove(QDir::tempPath() + "/lyra_test_profile_mgr.ini");
    if (g_fail == 0) std::printf("test_profile: ALL PASS\n");
    else             std::printf("test_profile: %d FAILURE(S)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}

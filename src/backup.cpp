// Lyra — Backup & Restore engine.  See backup.h.

#include "backup.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace lyra::backup {

namespace {

// Header group written INTO each backup file (never a real setting; skipped
// on restore).  Self-describes the snapshot for the store list.
constexpr auto kHdrName = "_backup/name";
constexpr auto kHdrAuto = "_backup/automatic";
constexpr auto kHdrWhen = "_backup/created";
constexpr auto kHdrPfx  = "_backup/";

// The section catalogue.  Order = display order.  Prefixes use startsWith
// matching; exact ui/* keys keep the Display vs Layout split clean (no
// section claims a bare "ui/", so unlisted ui/* keys fall to "advanced").
const QVector<Section> &catalogue() {
    static const QVector<Section> kS = {
        {QStringLiteral("radio"), QStringLiteral("Radio & connection"),
         QStringLiteral("Sample rate, hardware options (BCD / filter board / "
                        "auto-start), band-plan region & colours, your "
                        "callsign / grid"),
         {QStringLiteral("radio/"), QStringLiteral("hw/"),
          QStringLiteral("band_plan/"), QStringLiteral("operator/")}, false},

        {QStringLiteral("audio"), QStringLiteral("Audio & VAC"),
         QStringLiteral("RX output device & host API, virtual-audio-cable "
                        "devices and gains"),
         {QStringLiteral("audio/"), QStringLiteral("vac1/"),
          QStringLiteral("vac2/")}, false},

        {QStringLiteral("rxdsp"), QStringLiteral("RX, DSP & noise"),
         QStringLiteral("Per-mode RX bandwidth, RX EQ, DSP filter type, noise "
                        "reduction & captured profiles"),
         {QStringLiteral("rx/"), QStringLiteral("rxeq/"), QStringLiteral("dsp/"),
          QStringLiteral("modefilter/")}, false},

        {QStringLiteral("tx"), QStringLiteral("TX audio chain"),
         QStringLiteral("Drive, mic, ALC / leveler, EQ, speech, combinator, "
                        "plate, PHROT, waterfall-ID, SWR protect, PA-enable, "
                        "PTT options, tune drive"),
         {QStringLiteral("tx/")}, false},

        {QStringLiteral("cw"), QStringLiteral("CW keyer, decoder & macros"),
         QStringLiteral("CW keyer, the RX decoder settings, and the macro bank"),
         {QStringLiteral("cw/"), QStringLiteral("CW/")}, false},

        {QStringLiteral("calmeter"), QStringLiteral("Calibration & meters"),
         QStringLiteral("Frequency calibration, PA-gain / power calibration, "
                        "meter styles & ballistics"),
         {QStringLiteral("cal/"), QStringLiteral("meter/")}, false},

        {QStringLiteral("spots"), QStringLiteral("Spots & DX cluster"),
         QStringLiteral("DX-spot sources, filters, colours and clutter caps"),
         {QStringLiteral("spots/")}, false},

        {QStringLiteral("weather"), QStringLiteral("Weather & solar"),
         QStringLiteral("Weather-station and solar / propagation panel config"),
         {QStringLiteral("wx/"), QStringLiteral("propagation/")}, false},

        {QStringLiteral("network"), QStringLiteral("Network — TCI / CAT / Serial"),
         QStringLiteral("TCI server, CAT server instances, serial PTT & "
                        "CW-key devices"),
         {QStringLiteral("tci/"), QStringLiteral("cat1/"), QStringLiteral("cat2/"),
          QStringLiteral("serialptt/"), QStringLiteral("serialcwkey/")}, false},

        {QStringLiteral("memory"), QStringLiteral("Memory, bands & tuner"),
         QStringLiteral("Per-band memory, the memory-bank channels, tuner "
                        "memory, shortwave database, GEN slots"),
         {QStringLiteral("band_mem/"), QStringLiteral("memory/"),
          QStringLiteral("bands/"), QStringLiteral("tuner/"),
          QStringLiteral("swdb/"), QStringLiteral("gen/")}, false},

        {QStringLiteral("profiles"), QStringLiteral("TX / RX profiles"),
         QStringLiteral("Saved TX/RX profile-manager bundles"),
         {QStringLiteral("profiles/")}, false},

        {QStringLiteral("display"), QStringLiteral("Display & spectrum look"),
         QStringLiteral("Panadapter / waterfall colours, dB ranges, fill / "
                        "peak / meteors, tooltips, CTUN state"),
         {QStringLiteral("panadapter/"), QStringLiteral("ui/tooltips_enabled"),
          QStringLiteral("ui/ctunEnabled")}, false},

        // "session/" holds the per-display-setup auto-saved layouts (one slot
        // per monitor configuration — see layoutSlotKey() in mainwindow.cpp).
        // A backup carries every setup's layout, so restoring on a machine with
        // a different monitor still finds its own arrangement waiting.
        {QStringLiteral("layout"), QStringLiteral("Panel layout & window"),
         QStringLiteral("Dock arrangement, window size / position, saved "
                        "layout slots, panel lock"),
         {QStringLiteral("layouts/"), QStringLiteral("session/"),
          QStringLiteral("ui/geometry"),
          QStringLiteral("ui/windowState"), QStringLiteral("ui/userGeometry"),
          QStringLiteral("ui/userWindowState"), QStringLiteral("ui/panadapterSplit"),
          QStringLiteral("ui/userPanadapterSplit"), QStringLiteral("ui/panelsLocked")},
         true},
    };
    return kS;
}

}  // namespace

const QVector<Section> &sections() { return catalogue(); }

QString advancedId() { return QStringLiteral("advanced"); }

bool isMachineSpecificKey(const QString &k) {
    // Mirror of the old MainWindow helper — hardware / connection identity and
    // per-PC launch paths never travel in a backup.
    if (k == QStringLiteral("ui/graphicsBackend")) return true;
    // Graphics crash-recovery state (ui/gfxStartupPending / gfxSafeMode /
    // gfxSafeDepth) is per-machine + transient — never carry it in a backup.
    if (k.startsWith(QStringLiteral("ui/gfx"))) return true;
    // The next-launch factory-reset sentinel is transient control state — a
    // backup that carried it true would wipe itself on the next start.
    if (k == QStringLiteral("app/factoryResetPending")) return true;
    // Same class: the one-shot "reset the layout after a repeated startup
    // crash" sentinel.  A restored backup carrying it true would purge the
    // layout on the next launch for no reason.
    if (k == QStringLiteral("ui/uiSafeReset")) return true;
    if (k == QStringLiteral("radio/lastIp"))       return true;
    if (k.startsWith(QStringLiteral("lastRadio/"))) return true;
    if (k.startsWith(QStringLiteral("profileLaunch/"))) return true;
    return false;
}

// Strip a leading per-rig scope ("rig/<id>/") so a rig-scoped config key
// maps to the SAME backup section as its legacy flat form (e.g.
// "rig/rig_aabb.../cal/freqCorrection" → "cal/freqCorrection" → calmeter).
// The rig identity registry ("rigs/…") is a separate namespace, untouched.
static QString stripRigScope(const QString &key) {
    if (!key.startsWith(QStringLiteral("rig/"))) return key;
    const int slash = key.indexOf(QLatin1Char('/'), 4);   // '/' after "rig/"
    if (slash < 0) return key;
    return key.mid(slash + 1);
}

QString sectionForKey(const QString &key) {
    const QString logical = stripRigScope(key);
    if (isMachineSpecificKey(logical)) return QString();
    for (const Section &s : catalogue())
        for (const QString &p : s.prefixes)
            if (logical.startsWith(p)) return s.id;
    return advancedId();
}

// ── Export ────────────────────────────────────────────────────────────────
bool exportToFile(const QString &path, const QString &name, bool automatic) {
    QSettings src;                                   // live scope
    QSettings dst(path, QSettings::IniFormat);
    dst.clear();                                     // fresh file
    for (const QString &k : src.allKeys()) {
        if (isMachineSpecificKey(k)) continue;
        if (k.startsWith(QLatin1String(kHdrPfx))) continue;  // never re-nest a header
        dst.setValue(k, src.value(k));
    }
    dst.setValue(QLatin1String(kHdrName), name);
    dst.setValue(QLatin1String(kHdrAuto), automatic);
    dst.setValue(QLatin1String(kHdrWhen),
                 QDateTime::currentDateTime().toString(Qt::ISODate));
    dst.sync();
    return dst.status() == QSettings::NoError;
}

// ── Restore ─────────────────────────────────────────────────────────────────
int restoreFromFile(const QString &path, const QSet<QString> &sectionIds,
                    bool *touchedLayout) {
    if (touchedLayout) *touchedLayout = false;
    QSettings file(path, QSettings::IniFormat);
    if (file.status() != QSettings::NoError) return 0;
    QSettings live;
    int n = 0;
    for (const QString &k : file.allKeys()) {
        if (k.startsWith(QLatin1String(kHdrPfx))) continue;   // skip header
        if (isMachineSpecificKey(k)) continue;                // never import hw id
        const QString sid = sectionForKey(k);
        if (sid.isEmpty() || !sectionIds.contains(sid)) continue;
        live.setValue(k, file.value(k));
        ++n;
        if (touchedLayout && k.startsWith(QStringLiteral("ui/"))
            && !*touchedLayout) {
            // any layout-section ui/* key came back
            for (const Section &s : catalogue())
                if (s.layout && sid == s.id) { *touchedLayout = true; break; }
        }
    }
    live.sync();
    return n;
}

QSet<QString> sectionsPresentInFile(const QString &path) {
    QSet<QString> out;
    QSettings file(path, QSettings::IniFormat);
    for (const QString &k : file.allKeys()) {
        if (k.startsWith(QLatin1String(kHdrPfx))) continue;
        const QString sid = sectionForKey(k);
        if (!sid.isEmpty()) out.insert(sid);
    }
    return out;
}

// ── Snapshot store ──────────────────────────────────────────────────────────
QString snapshotDir() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dir = base + QStringLiteral("/snapshots");
    QDir().mkpath(dir);
    return dir;
}

QVector<SnapInfo> listSnapshots() {
    QVector<SnapInfo> out;
    QDir d(snapshotDir());
    const QFileInfoList files =
        d.entryInfoList({QStringLiteral("*.lyra")}, QDir::Files);
    for (const QFileInfo &fi : files) {
        QSettings s(fi.absoluteFilePath(), QSettings::IniFormat);
        SnapInfo si;
        si.path      = fi.absoluteFilePath();
        si.name      = s.value(QLatin1String(kHdrName)).toString();
        si.automatic = s.value(QLatin1String(kHdrAuto), false).toBool();
        si.when      = QDateTime::fromString(
                           s.value(QLatin1String(kHdrWhen)).toString(),
                           Qt::ISODate);
        if (!si.when.isValid()) si.when = fi.lastModified();  // legacy fallback
        out.append(si);
    }
    std::sort(out.begin(), out.end(),
              [](const SnapInfo &a, const SnapInfo &b) { return a.when > b.when; });
    return out;
}

namespace {
void pruneAuto(int keepAuto) {
    if (keepAuto < 0) keepAuto = 0;
    int seen = 0;
    for (const SnapInfo &si : listSnapshots()) {   // newest first
        if (!si.automatic) continue;               // manual snaps are never pruned
        if (++seen > keepAuto) QFile::remove(si.path);
    }
}
}  // namespace

QString saveSnapshot(const QString &name, bool automatic, int keepAuto) {
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString file = (automatic ? QStringLiteral("auto-")
                                    : QStringLiteral("snap-")) + stamp
                         + QStringLiteral(".lyra");
    const QString path = snapshotDir() + QLatin1Char('/') + file;
    if (!exportToFile(path, name, automatic)) return QString();
    if (automatic) pruneAuto(keepAuto);
    return path;
}

void deleteSnapshot(const QString &path) { QFile::remove(path); }

void autoSnapshotOnLaunch() {
    QSettings s;
    const int every = s.value(QLatin1String(kAutoEvery), kAutoEveryDefault).toInt();
    if (every <= 0) return;                        // auto snapshots disabled
    int ctr = s.value(QLatin1String(kLaunchCtr), 0).toInt() + 1;
    if (ctr >= every) {
        const int keep = s.value(QLatin1String(kKeepCount), kKeepCountDefault).toInt();
        saveSnapshot(QString(), /*automatic=*/true, keep);
        ctr = 0;
    }
    s.setValue(QLatin1String(kLaunchCtr), ctr);
}

}  // namespace lyra::backup

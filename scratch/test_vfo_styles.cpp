// Contract test for the Tuning-panel VFO styles (src/qml/VfoStyle*.qml).
// Loads the registry (src/qml/VfoStyles.js) and instantiates EVERY
// registered style straight from the source tree, headless, then checks
// what VfoBox.qml relies on:
//   - it loads with no QML errors or warnings;
//   - naturalWidth/Height > 0;
//   - tuneRect / stepModeRect / txPipRect lie inside the style; the Step/Mode
//     row does NOT overlap tuneRect (FreqDisplay sits over tuneRect and would
//     swallow its clicks), and the TX pip covers no digit;
//   - exactly 10 digitRects, all inside tuneRect, places 0-5 (kHz + Hz,
//     always shown) non-empty;
//   - theme carries every key VfoStepMode / VfoTxPip read;
// at 0 Hz, 136 kHz, 14.168 MHz and 2.147 GHz.  Also: the registry resolves
// an empty or unknown id to its default, and the default is registered.
//   build: cmake --build build --target test_vfo_styles
//   run:   build/test_vfo_styles
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRectF>
#include <QUrl>
#include <QVariant>

#include <cstdio>
#include <memory>

#ifndef LYRA_QML_DIR
#error "LYRA_QML_DIR must point at src/qml"
#endif

static int g_failures = 0;

static void check(const QString &name, bool ok)
{
    std::printf("  %-76s %s\n", qPrintable(name), ok ? "PASS" : "FAIL");
    if (!ok) ++g_failures;
}

static QVariant plain(const QVariant &v)
{
    return v.canConvert<QJSValue>() ? v.value<QJSValue>().toVariant() : v;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    std::printf("test_vfo_styles\n");

#ifdef LYRA_FONT_DIR
    {
        QDir fontDir(QStringLiteral(LYRA_FONT_DIR));
        for (const QString &f : fontDir.entryList({QStringLiteral("*.ttf")}, QDir::Files))
            check(QStringLiteral("font loads: ") + f,
                  QFontDatabase::addApplicationFont(fontDir.filePath(f)) >= 0);
        const QStringList fams = QFontDatabase::families();
        for (const char *fam : {"Share Tech Mono", "Nixie One", "Inter", "DSEG7 Classic"})
            check(QStringLiteral("font family present: ") + QLatin1String(fam),
                  fams.contains(QLatin1String(fam)));
    }
#endif

    const QString dir = QStringLiteral(LYRA_QML_DIR);
    QQmlEngine engine;
    QList<QQmlError> warnings;
    QObject::connect(&engine, &QQmlEngine::warnings,
                     [&](const QList<QQmlError> &w) { warnings += w; });

    // ── Registry ──
    QQmlComponent reg(&engine);
    reg.setData(R"(import QtQml
import "VfoStyles.js" as VfoStyles
QtObject {
    property var styles: VfoStyles.styles
    property string defaultId: VfoStyles.defaultId
    property string unknownId: VfoStyles.resolve("no-such-style").id
    property string emptyId: VfoStyles.resolve("").id
})", QUrl::fromLocalFile(dir + QStringLiteral("/registry_probe.qml")));
    std::unique_ptr<QObject> r(reg.create());
    check(QStringLiteral("registry loads"), r != nullptr);
    if (!r) {
        std::printf("%s\n", qPrintable(reg.errorString()));
        return 1;
    }
    const QString defId = r->property("defaultId").toString();
    check(QStringLiteral("unknown id resolves to the default"),
          r->property("unknownId").toString() == defId);
    check(QStringLiteral("empty id resolves to the default"),
          r->property("emptyId").toString() == defId);
    const QVariantList styles = plain(r->property("styles")).toList();
    check(QStringLiteral("registry is not empty"), !styles.isEmpty());
    bool defaultListed = false;
    for (const QVariant &sv : styles)
        defaultListed |= sv.toMap().value(QStringLiteral("id")).toString() == defId;
    check(QStringLiteral("default id is a registered style"), defaultListed);

    // ── Every style, several frequencies ──
    const int freqs[] = {0, 136'000, 14'168'000, 2'147'483'647};
    const char *themeKeys[] = {"label", "field", "fieldBorder", "text", "accent",
                               "pipIdle", "pipIdleBorder", "pipIdleText", "compact"};

    for (const QVariant &sv : styles) {
        const QVariantMap e = sv.toMap();
        const QString id = e.value(QStringLiteral("id")).toString();
        const QString file = e.value(QStringLiteral("file")).toString();
        for (int hz : freqs) {
            const QString tag = QStringLiteral("%1 @ %2 Hz: ").arg(id).arg(hz);
            warnings.clear();
            QQmlComponent comp(&engine, QUrl::fromLocalFile(dir + QLatin1Char('/') + file));
            std::unique_ptr<QObject> st(comp.createWithInitialProperties({
                {QStringLiteral("freqHz"), hz},
                {QStringLiteral("selectedDigit"), 3},
                {QStringLiteral("vfoState"), QStringLiteral("rx")},
                {QStringLiteral("roleText"), QStringLiteral("RX")},
                {QStringLiteral("vfoLetter"), QStringLiteral("A")},
                {QStringLiteral("modeText"), QStringLiteral("USB")},
                {QStringLiteral("stepHz"), 100},
                {QStringLiteral("showMode"), true}}));
            check(tag + QStringLiteral("loads"), st != nullptr);
            if (!st) {
                std::printf("%s\n", qPrintable(comp.errorString()));
                continue;
            }
            check(tag + QStringLiteral("no component errors"), comp.errors().isEmpty());
            for (const QQmlError &e : comp.errors())
                std::printf("      %s\n", qPrintable(e.toString()));
            QCoreApplication::processEvents();
            check(tag + QStringLiteral("no QML warnings"), warnings.isEmpty());
            for (const QQmlError &w : warnings)
                std::printf("      %s\n", qPrintable(w.toString()));

            const double nw = st->property("naturalWidth").toDouble();
            const double nh = st->property("naturalHeight").toDouble();
            check(tag + QStringLiteral("natural size > 0"), nw > 0 && nh > 0);
            const QRectF bounds(0, 0, nw, nh);
            const QRectF tune = st->property("tuneRect").toRectF();
            const QRectF sm = st->property("stepModeRect").toRectF();
            const QRectF pip = st->property("txPipRect").toRectF();
            check(tag + QStringLiteral("tuneRect inside"), !tune.isEmpty() && bounds.contains(tune));
            check(tag + QStringLiteral("stepModeRect inside"), !sm.isEmpty() && bounds.contains(sm));
            check(tag + QStringLiteral("txPipRect inside"), !pip.isEmpty() && bounds.contains(pip));
            check(tag + QStringLiteral("stepModeRect clear of tuneRect"), !sm.intersects(tune));
            const QVariantList rects = plain(st->property("digitRects")).toList();
            // The pip is stacked above FreqDisplay in VfoBox, so it may sit in
            // a corner of tuneRect (Amber's does) — but never on a digit.
            bool pipClear = true;
            for (const QVariant &rv : rects)
                if (pip.intersects(rv.toRectF())) pipClear = false;
            check(tag + QStringLiteral("txPipRect clear of every digit"), pipClear);
            check(tag + QStringLiteral("exactly 10 digit rects"), rects.size() == 10);
            bool inside = true;
            bool lowShown = true;
            for (int p = 0; p < rects.size(); ++p) {
                const QRectF rr = rects.at(p).toRectF();
                if (rr.isEmpty()) {
                    if (p <= 5) lowShown = false;
                    continue;
                }
                if (!tune.contains(rr)) inside = false;
            }
            check(tag + QStringLiteral("digit rects inside tuneRect"), inside);
            check(tag + QStringLiteral("places 0-5 always shown"), lowShown);
            bool noOverlap = true;
            for (int p = 0; p < rects.size(); ++p) {
                const QRectF rp = rects.at(p).toRectF();
                if (rp.isEmpty()) continue;
                for (int q = p + 1; q < rects.size(); ++q) {
                    const QRectF rq = rects.at(q).toRectF();
                    if (!rq.isEmpty() && rp.intersects(rq)) noOverlap = false;
                }
            }
            check(tag + QStringLiteral("shown digit rects don't overlap"), noOverlap);

            const QVariantMap theme = plain(st->property("theme")).toMap();
            bool keys = true;
            for (const char *k : themeKeys)
                keys &= theme.contains(QLatin1String(k));
            check(tag + QStringLiteral("theme has every key"), keys);
        }
    }

    std::printf(g_failures ? "FAILED: %d\n" : "ALL PASS\n", g_failures);
    return g_failures ? 1 : 0;
}

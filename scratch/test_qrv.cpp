// Offline tests for QRV JSON + interval gating + PHP field rules.
// cmake --build build --target test_qrv && build/test_qrv
#include "qrv/QrvProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

using namespace lyra::qrv;

static int g_failures = 0;

static void check(const char *name, bool ok)
{
    std::printf("  %-56s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok)
        ++g_failures;
}

int main()
{
    std::printf("test_qrv\n");

    check("call N8SDR", normalizeCall(QStringLiteral("n8sdr")) == QLatin1String("N8SDR"));
    check("call N8SDR/P", normalizeCall(QStringLiteral("n8sdr/p")) == QLatin1String("N8SDR/P"));
    check("call reject no digit", normalizeCall(QStringLiteral("NSDR")).isEmpty());
    check("call reject empty", normalizeCall(QString()).isEmpty());
    check("mode USB", normalizeMode(QStringLiteral("usb")) == QLatin1String("USB"));
    check("mode reject FOO", normalizeMode(QStringLiteral("FOO")).isEmpty());
    check("app Lyra", normalizeApp(QStringLiteral("lyra")) == QLatin1String("Lyra"));
    check("band 40m", bandForHz(7200000) == QLatin1String("40m"));
    check("band GEN WWV", bandForHz(10000000) == QLatin1String("GEN"));
    check("band 2m", bandForHz(144200000) == QLatin1String("2m"));
    check("band 23cm empty", bandForHz(1296000000).isEmpty());
    check("hz in 40m", hzInBand(7200000, QStringLiteral("40m")));
    check("hz not in 20m", !hzInBand(7200000, QStringLiteral("20m")));
    check("interval first post", intervalElapsed(0, 1000, 15000));
    check("interval gated", !intervalElapsed(1000, 5000, 15000));
    check("interval elapsed", intervalElapsed(1000, 17000, 15000));
    check("api base trim",
          normalizeApiBase(QStringLiteral("https://qrvsdr.cc/v1/"))
              == QLatin1String("https://qrvsdr.cc"));
    check("endpoint heartbeat",
          endpointUrl(QStringLiteral("https://qrvsdr.cc"),
                      QStringLiteral("heartbeat.php"))
              == QLatin1String("https://qrvsdr.cc/v1/heartbeat.php"));
    check("truncated key",
          truncatedKey(QStringLiteral("0123456789abcdef"))
              == QLatin1String("01234567..."));

    HeartbeatRequest hb;
    hb.call = QStringLiteral("N8SDR");
    hb.band = QStringLiteral("40m");
    hb.mode = QStringLiteral("LSB");
    hb.hasHz = true;
    hb.hz = 7200000;
    hb.note = QStringLiteral("test");
    const QJsonObject ho =
        QJsonDocument::fromJson(encodeHeartbeat(hb)).object();
    check("hb fields call", ho.value(QStringLiteral("call")).toString() == QLatin1String("N8SDR"));
    check("hb fields app", ho.value(QStringLiteral("app")).toString() == QLatin1String("Lyra"));
    check("hb fields band", ho.value(QStringLiteral("band")).toString() == QLatin1String("40m"));
    check("hb fields mode", ho.value(QStringLiteral("mode")).toString() == QLatin1String("LSB"));
    check("hb fields hz", ho.value(QStringLiteral("hz")).toDouble() == 7200000);
    check("hb no key omitted", !ho.contains(QStringLiteral("install_key")));

    HeartbeatRequest hb2 = hb;
    hb2.hasHz = false;
    hb2.installKey = QStringLiteral("abc");
    hb2.note.clear();
    const QJsonObject ho2 =
        QJsonDocument::fromJson(encodeHeartbeat(hb2)).object();
    check("hb omit hz", !ho2.contains(QStringLiteral("hz")));
    check("hb omit note", !ho2.contains(QStringLiteral("note")));
    check("hb has key", ho2.value(QStringLiteral("install_key")).toString() == QLatin1String("abc"));

    QrtRequest q;
    q.call = QStringLiteral("N8SDR");
    q.installKey = QStringLiteral("abc");
    const QJsonObject qo = QJsonDocument::fromJson(encodeQrt(q)).object();
    check("qrt three fields",
          qo.contains(QStringLiteral("call")) && qo.contains(QStringLiteral("app"))
              && qo.contains(QStringLiteral("install_key")) && qo.size() == 3);

    const ApiResult issued = parseWriteResponse(
        QByteArray("{\"ok\":true,\"stale_s\":180,\"install_key\":\"deadbeef\"}"),
        200);
    check("issue key", issued.ok && issued.installKey == QLatin1String("deadbeef")
                           && issued.staleS == 180);

    const ApiResult bad = parseWriteResponse(
        QByteArray("{\"ok\":false,\"error\":\"rate_limited\"}"), 429);
    check("rate_limited", !bad.ok && bad.error == QLatin1String("rate_limited"));

    const Roster empty = parseRosterResponse(
        QByteArray("{\"ok\":true,\"stale_s\":180,\"entries\":[]}"), 200);
    check("empty roster", empty.ok && empty.entries.isEmpty() && empty.staleS == 180);

    const Roster one = parseRosterResponse(
        QByteArray("{\"ok\":true,\"stale_s\":180,\"entries\":["
                   "{\"call\":\"N8SDR\",\"app\":\"Lyra\",\"band\":\"40m\","
                   "\"mode\":\"LSB\",\"last_seen\":\"2026-11-15T00:00:00Z\","
                   "\"hz\":7200000,\"note\":\"hi\"}]}"),
        200);
    check("roster row",
          one.ok && one.entries.size() == 1 && one.entries[0].call == QLatin1String("N8SDR")
              && one.entries[0].hasHz && one.entries[0].hz == 7200000
              && one.entries[0].note == QLatin1String("hi"));

    RosterEntry spot = one.entries[0];
    check("spottable Lyra+hz", isSpottableRow(spot));
    RosterEntry noHz = spot;
    noHz.hasHz = false;
    noHz.hz = 0;
    check("not spottable missing hz", !isSpottableRow(noHz));
    RosterEntry stdApp = spot;
    stdApp.app = QStringLiteral("STD");
    check("not spottable STD", !isSpottableRow(stdApp));
    RosterEntry loggerApp = spot;
    loggerApp.app = QStringLiteral("Logger");
    check("not spottable Logger", !isSpottableRow(loggerApp));
    RosterEntry lyraWrongCase = spot;
    lyraWrongCase.app = QStringLiteral("lyra");
    check("not spottable lyra case", !isSpottableRow(lyraWrongCase));
    RosterEntry hzZero = spot;
    hzZero.hasHz = true;
    hzZero.hz = 0;
    check("not spottable hz 0", !isSpottableRow(hzZero));

    const QString note = sanitizeNote(QStringLiteral("  hello @bob http://x.com *hi*  "));
    check("note stripped", note == QLatin1String("hello   hi"));

    std::printf(g_failures ? "FAILED (%d)\n" : "ALL PASS (0 failures)\n", g_failures);
    return g_failures ? 1 : 0;
}

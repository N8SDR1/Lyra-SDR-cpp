// QRV board JSON + field rules matching qrv-service/include/validate.php
// and public_html/v1/{heartbeat,qrt,roster}.php.

#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

namespace lyra::qrv {

inline constexpr auto kDefaultApiBase = "https://qrvsdr.cc";
inline constexpr auto kAppLyra = "Lyra";
inline constexpr int kMinPostIntervalMs = 15000;
inline constexpr int kHeartbeatPeriodMs = 30000;
inline constexpr int kBandModeDebounceMs = 10000;
inline constexpr int kRosterPeriodMs = 30000;
inline constexpr int kHttpTimeoutMs = 12000;
inline constexpr int kNoteMaxChars = 40;

struct HeartbeatRequest {
    QString call;
    QString app = QStringLiteral("Lyra");
    QString band;
    QString mode;
    QString installKey;   // empty = first post; PHP issues a key
    QString note;         // omitted in JSON when empty
    bool    hasHz = false;
    qint64  hz = 0;
};

struct QrtRequest {
    QString call;
    QString app = QStringLiteral("Lyra");
    QString installKey;
};

struct ApiResult {
    bool    ok = false;
    int     httpStatus = 0;
    QString error;
    QString installKey;   // set when PHP issues a new key
    int     staleS = 180;
};

struct RosterEntry {
    QString call;
    QString app;
    QString band;
    QString mode;
    QString lastSeen;
    QString note;
    bool    hasHz = false;
    qint64  hz = 0;
};

struct Roster {
    bool              ok = false;
    int               httpStatus = 0;
    QString           error;
    int               staleS = 180;
    QList<RosterEntry> entries;
};

QString normalizeCall(const QString &raw);
QString normalizeMode(const QString &raw);
QString normalizeApp(const QString &raw);

// QRV-panel click-to-tune (qrv_client.md §10.1 LOCKED): a roster row is a
// spot iff Hz was posted AND app is exactly "Lyra" as PHP stores it.
inline bool isSpottableRow(const RosterEntry &e) {
    return e.hasHz && e.hz > 0 && e.app == QLatin1String("Lyra");
}
QString sanitizeNote(const QString &raw);
QString bandForHz(qint64 hz);
bool    hzInBand(qint64 hz, const QString &band);
bool    intervalElapsed(qint64 lastMs, qint64 nowMs, int minMs);
QString normalizeApiBase(QString raw);
QString endpointUrl(const QString &apiBase, const QString &phpFile);
QString truncatedKey(const QString &key);

QByteArray encodeHeartbeat(const HeartbeatRequest &req);
QByteArray encodeQrt(const QrtRequest &req);
ApiResult  parseWriteResponse(const QByteArray &body, int httpStatus);
Roster     parseRosterResponse(const QByteArray &body, int httpStatus);

} // namespace lyra::qrv

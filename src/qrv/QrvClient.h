// QRV board HTTPS client. Talks only to the QRV service (/v1/*.php).
// Never Discord. Async QNetworkAccessManager on the UI thread.

#pragma once

#include "qrv/QrvProtocol.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace lyra::qrv {

class QrvClient : public QObject {
    Q_OBJECT
public:
    explicit QrvClient(QObject *parent = nullptr);

    bool    optIn() const { return optIn_; }
    QString callsign() const { return callsign_; }
    QString note() const { return note_; }
    bool    shareHz() const { return shareHz_; }
    QString apiBase() const { return apiBase_; }
    QString installKey() const { return installKey_; }
    bool    publishing() const { return publishing_; }
    bool    radioConnected() const { return radioConnected_; }
    QString lastError() const { return lastError_; }
    const Roster &roster() const { return roster_; }

    void setOptIn(bool on);
    void prefillCallsignIfEmpty(const QString &stationCall);
    void setCallsign(const QString &call);
    void setNote(const QString &note);
    void setShareHz(bool on);
    void setApiBase(const QString &url);
    void regenerateInstallKey();

    // Session QRV/QRT. Requires opt-in, a valid call, and a live radio.
    void setPublishing(bool on);

    void setRadioConnected(bool on);
    void setVfo(qint64 hz, const QString &mode);

    // GET roster only while the QRV panel is visible.
    void setRosterWatching(bool on);

    // Best-effort POST /v1/qrt.php so the row leaves the board on exit.
    void flushQrtBlocking(int timeoutMs = 2500);

signals:
    void optInChanged();
    void callsignChanged();
    void noteChanged();
    void shareHzChanged();
    void apiBaseChanged();
    void installKeyChanged();
    void publishingChanged();
    void lastErrorChanged();
    void rosterChanged();

private:
    void load();
    void saveKeyAndCall();
    void persistSettings();
    void setError(const QString &e);
    void maybeHeartbeat(bool force);
    void postHeartbeat();
    void postQrt(bool blocking, int timeoutMs);
    void fetchRoster();
    void armHeartbeatTimer();
    HeartbeatRequest currentHeartbeat() const;
    bool canPublish() const;
    void onReplyFinished(QNetworkReply *reply, const QString &kind, bool blocking);

    QNetworkAccessManager *nam_ = nullptr;
    QTimer *heartbeatTimer_ = nullptr;
    QTimer *debounceTimer_ = nullptr;
    QTimer *rosterTimer_ = nullptr;

    bool    optIn_ = false;
    bool    shareHz_ = false;
    bool    publishing_ = false;
    bool    radioConnected_ = false;
    bool    rosterWatching_ = false;
    bool    inFlight_ = false;
    QString callsign_;
    QString note_;
    QString apiBase_;
    QString installKey_;
    QString lastError_;
    qint64  vfoHz_ = 0;
    QString mode_;
    QString lastBand_;
    QString lastModeSent_;
    qint64  lastPostMs_ = 0;
    Roster  roster_;
};

} // namespace lyra::qrv

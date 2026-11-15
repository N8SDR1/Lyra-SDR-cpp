#include "qrv/QrvClient.h"

#include <QDateTime>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace lyra::qrv {
namespace {

const auto kOptIn = QStringLiteral("qrv/opt_in");
const auto kCall = QStringLiteral("qrv/call");
const auto kNote = QStringLiteral("qrv/note");
const auto kShareHz = QStringLiteral("qrv/share_hz");
const auto kApiBase = QStringLiteral("qrv/api_base");
const auto kInstallKey = QStringLiteral("qrv/install_key");

QNetworkRequest jsonRequest(const QString &url) {
    QNetworkRequest r{QUrl(url)};
    r.setHeader(QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json; charset=utf-8"));
    r.setRawHeader("Accept", "application/json");
    r.setRawHeader("User-Agent", "Lyra-SDR (QRV)");
    r.setTransferTimeout(kHttpTimeoutMs);
    return r;
}

} // namespace

QrvClient::QrvClient(QObject *parent)
    : QObject(parent),
      nam_(new QNetworkAccessManager(this)),
      heartbeatTimer_(new QTimer(this)),
      debounceTimer_(new QTimer(this)),
      rosterTimer_(new QTimer(this)) {
    heartbeatTimer_->setInterval(kHeartbeatPeriodMs);
    connect(heartbeatTimer_, &QTimer::timeout, this, [this]() { maybeHeartbeat(false); });
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(kBandModeDebounceMs);
    connect(debounceTimer_, &QTimer::timeout, this, [this]() { maybeHeartbeat(true); });
    rosterTimer_->setInterval(kRosterPeriodMs);
    connect(rosterTimer_, &QTimer::timeout, this, [this]() { fetchRoster(); });
    load();
}

void QrvClient::load() {
    QSettings s;
    optIn_ = s.value(kOptIn, false).toBool();
    callsign_ = normalizeCall(s.value(kCall).toString());
    note_ = sanitizeNote(s.value(kNote).toString());
    shareHz_ = s.value(kShareHz, false).toBool();
    apiBase_ = normalizeApiBase(
        s.value(kApiBase, QString::fromUtf8(kDefaultApiBase)).toString());
    installKey_ = s.value(kInstallKey).toString().trimmed();
}

void QrvClient::persistSettings() {
    QSettings s;
    s.setValue(kOptIn, optIn_);
    s.setValue(kCall, callsign_);
    s.setValue(kNote, note_);
    s.setValue(kShareHz, shareHz_);
    s.setValue(kApiBase, apiBase_);
    s.setValue(kInstallKey, installKey_);
}

void QrvClient::saveKeyAndCall() { persistSettings(); }

void QrvClient::setError(const QString &e) {
    if (lastError_ == e)
        return;
    lastError_ = e;
    emit lastErrorChanged();
}

bool QrvClient::canPublish() const {
    return optIn_ && publishing_ && radioConnected_
           && !normalizeCall(callsign_).isEmpty();
}

void QrvClient::armHeartbeatTimer() {
    if (canPublish()) {
        if (!heartbeatTimer_->isActive())
            heartbeatTimer_->start();
    } else {
        heartbeatTimer_->stop();
        debounceTimer_->stop();
    }
}

void QrvClient::setOptIn(bool on) {
    if (optIn_ == on)
        return;
    optIn_ = on;
    persistSettings();
    emit optInChanged();
    if (!optIn_ && publishing_)
        setPublishing(false);
    else
        armHeartbeatTimer();
}

void QrvClient::prefillCallsignIfEmpty(const QString &stationCall) {
    if (!callsign_.isEmpty())
        return;
    const QString n = normalizeCall(stationCall);
    if (n.isEmpty())
        return;
    callsign_ = n;
    persistSettings();
    emit callsignChanged();
}

void QrvClient::setCallsign(const QString &call) {
    const QString n = normalizeCall(call);
    if (n == callsign_)
        return;
    const bool wasPub = publishing_;
    if (wasPub)
        postQrt(false, 0);
    callsign_ = n;
    installKey_.clear();
    persistSettings();
    emit callsignChanged();
    emit installKeyChanged();
    if (wasPub && canPublish())
        maybeHeartbeat(true);
}

void QrvClient::setNote(const QString &note) {
    const QString n = sanitizeNote(note);
    if (n == note_)
        return;
    note_ = n;
    persistSettings();
    emit noteChanged();
    if (canPublish())
        debounceTimer_->start();
}

void QrvClient::setShareHz(bool on) {
    if (shareHz_ == on)
        return;
    shareHz_ = on;
    persistSettings();
    emit shareHzChanged();
    if (canPublish())
        debounceTimer_->start();
}

void QrvClient::setApiBase(const QString &url) {
    const QString n = normalizeApiBase(url);
    if (n == apiBase_)
        return;
    apiBase_ = n;
    persistSettings();
    emit apiBaseChanged();
}

void QrvClient::regenerateInstallKey() {
    const bool wasPub = publishing_;
    if (wasPub)
        postQrt(false, 0);
    installKey_.clear();
    persistSettings();
    emit installKeyChanged();
    if (wasPub && canPublish())
        maybeHeartbeat(true);
}

void QrvClient::setPublishing(bool on) {
    if (publishing_ == on)
        return;
    if (on) {
        if (!optIn_) {
            setError(QStringLiteral("Enable QRV in Settings first."));
            return;
        }
        if (normalizeCall(callsign_).isEmpty()) {
            setError(QStringLiteral("Set a valid callsign in Settings → Network."));
            return;
        }
        if (!radioConnected_) {
            setError(QStringLiteral("Start the radio before going QRV."));
            return;
        }
        publishing_ = true;
        emit publishingChanged();
        armHeartbeatTimer();
        maybeHeartbeat(true);
        return;
    }
    publishing_ = false;
    emit publishingChanged();
    armHeartbeatTimer();
    postQrt(false, 0);
}

void QrvClient::setRadioConnected(bool on) {
    if (radioConnected_ == on)
        return;
    radioConnected_ = on;
    if (!radioConnected_ && publishing_) {
        postQrt(false, 0);
        publishing_ = false;
        emit publishingChanged();
        setError(QStringLiteral("Radio stopped — QRT sent."));
    }
    armHeartbeatTimer();
}

void QrvClient::setVfo(qint64 hz, const QString &mode) {
    const QString m = normalizeMode(mode);
    const bool bandChanged = bandForHz(hz) != lastBand_;
    const bool modeChanged = m != lastModeSent_ && !m.isEmpty();
    vfoHz_ = hz;
    if (!m.isEmpty())
        mode_ = m;
    if (canPublish() && (bandChanged || modeChanged))
        debounceTimer_->start();
}

void QrvClient::setRosterWatching(bool on) {
    if (rosterWatching_ == on)
        return;
    rosterWatching_ = on;
    if (rosterWatching_) {
        fetchRoster();
        rosterTimer_->start();
    } else {
        rosterTimer_->stop();
    }
}

HeartbeatRequest QrvClient::currentHeartbeat() const {
    HeartbeatRequest req;
    req.call = normalizeCall(callsign_);
    req.app = QString::fromLatin1(kAppLyra);
    req.band = bandForHz(vfoHz_);
    req.mode = normalizeMode(mode_);
    req.installKey = installKey_.trimmed();
    req.note = sanitizeNote(note_);
    if (shareHz_ && hzInBand(vfoHz_, req.band)) {
        req.hasHz = true;
        req.hz = vfoHz_;
    }
    return req;
}

void QrvClient::maybeHeartbeat(bool force) {
    if (!canPublish() || inFlight_)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && !intervalElapsed(lastPostMs_, now, kHeartbeatPeriodMs))
        return;
    if (!intervalElapsed(lastPostMs_, now, kMinPostIntervalMs))
        return;
    postHeartbeat();
}

void QrvClient::postHeartbeat() {
    const HeartbeatRequest req = currentHeartbeat();
    if (req.call.isEmpty() || req.band.isEmpty() || req.mode.isEmpty()) {
        setError(QStringLiteral("Need call, band, and mode before QRV."));
        return;
    }
    inFlight_ = true;
    lastPostMs_ = QDateTime::currentMSecsSinceEpoch();
    lastBand_ = req.band;
    lastModeSent_ = req.mode;
    const QString url = endpointUrl(apiBase_, QStringLiteral("heartbeat.php"));
    QNetworkReply *reply = nam_->post(jsonRequest(url), encodeHeartbeat(req));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply, QStringLiteral("heartbeat"), false);
    });
}

void QrvClient::postQrt(bool blocking, int timeoutMs) {
    const QString call = normalizeCall(callsign_);
    if (call.isEmpty() || installKey_.trimmed().isEmpty())
        return;
    QrtRequest req;
    req.call = call;
    req.app = QString::fromLatin1(kAppLyra);
    req.installKey = installKey_.trimmed();
    const QString url = endpointUrl(apiBase_, QStringLiteral("qrt.php"));
    QNetworkReply *reply = nam_->post(jsonRequest(url), encodeQrt(req));
    if (!blocking) {
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            onReplyFinished(reply, QStringLiteral("qrt"), false);
        });
        return;
    }
    QEventLoop loop;
    QTimer killer;
    killer.setSingleShot(true);
    connect(&killer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    killer.start(timeoutMs > 0 ? timeoutMs : 2500);
    loop.exec();
    onReplyFinished(reply, QStringLiteral("qrt"), true);
}

void QrvClient::fetchRoster() {
    if (!rosterWatching_)
        return;
    const QString url = endpointUrl(apiBase_, QStringLiteral("roster.php"));
    QNetworkRequest r{QUrl(url)};
    r.setRawHeader("Accept", "application/json");
    r.setRawHeader("User-Agent", "Lyra-SDR (QRV)");
    r.setTransferTimeout(kHttpTimeoutMs);
    QNetworkReply *reply = nam_->get(r);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply, QStringLiteral("roster"), false);
    });
}

void QrvClient::flushQrtBlocking(int timeoutMs) {
    if (!publishing_ && installKey_.isEmpty())
        return;
    if (publishing_ || !installKey_.isEmpty()) {
        // Always try QRT if we were on the board this session.
        if (publishing_ || lastPostMs_ > 0)
            postQrt(true, timeoutMs);
    }
    publishing_ = false;
    heartbeatTimer_->stop();
}

void QrvClient::onReplyFinished(QNetworkReply *reply, const QString &kind,
                                bool /*blocking*/) {
    reply->deleteLater();
    if (kind == QLatin1String("heartbeat"))
        inFlight_ = false;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                           .toInt();
    const QByteArray body = reply->readAll();
    if (kind == QLatin1String("roster")) {
        Roster parsed = parseRosterResponse(body, status);
        if (reply->error() != QNetworkReply::NoError && !parsed.ok) {
            if (parsed.error.isEmpty())
                parsed.error = reply->errorString();
            setError(parsed.error);
        } else if (parsed.ok) {
            if (!lastError_.isEmpty() && lastError_.startsWith(QLatin1String("http_")))
                setError(QString());
            else if (lastError_.startsWith(QLatin1String("roster")))
                setError(QString());
        } else if (!parsed.error.isEmpty()) {
            setError(parsed.error);
        }
        roster_ = parsed;
        emit rosterChanged();
        return;
    }

    ApiResult parsed = parseWriteResponse(body, status);
    if (reply->error() != QNetworkReply::NoError && !parsed.ok
        && parsed.error.startsWith(QLatin1String("http_"))) {
        parsed.error = reply->errorString();
    }
    if (kind == QLatin1String("heartbeat")) {
        if (parsed.ok) {
            if (!parsed.installKey.isEmpty() && parsed.installKey != installKey_) {
                installKey_ = parsed.installKey;
                persistSettings();
                emit installKeyChanged();
            }
            setError(QString());
        } else {
            if (parsed.error == QLatin1String("bad_key")
                || parsed.error == QLatin1String("call_mismatch")) {
                installKey_.clear();
                persistSettings();
                emit installKeyChanged();
            }
            setError(parsed.error.isEmpty() ? QStringLiteral("heartbeat failed")
                                            : parsed.error);
        }
        return;
    }
    if (kind == QLatin1String("qrt") && !parsed.ok && !parsed.error.isEmpty())
        setError(parsed.error);
}

} // namespace lyra::qrv

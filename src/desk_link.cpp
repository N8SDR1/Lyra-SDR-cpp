// Lyra-to-Lyra control link. See desk_link.h.

#include "desk_link.h"

#include <QAudio>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QMediaDevices>
#include <QIODevice>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <thread>

namespace lyra::ui {

namespace {

QString tailscaleExe()
{
    const QStringList candidates = {
        QStringLiteral("C:/Program Files/Tailscale/tailscale.exe"),
        QStringLiteral("C:/Program Files (x86)/Tailscale/tailscale.exe"),
    };
    for (const QString &path : candidates) {
        if (QFile::exists(path)) return path;
    }
    return QStringLiteral("tailscale");
}

bool ipv4InTailscale(const QHostAddress &addr)
{
    if (addr.protocol() != QAbstractSocket::IPv4Protocol) return false;
    const quint32 ip = addr.toIPv4Address();
    // 100.64.0.0/10 — Tailscale's CGNAT range.
    return (ip & 0xFFC00000u) == 0x64400000u;
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

constexpr int kRingCap = 12000;   // 250 ms at 48 kHz

double deskPosToGain(double pos)
{
    if (pos <= 0.0) return 0.0;
    if (pos >= 1.0) return 1.0;
    const double db = -60.0 * (1.0 - pos);
    return std::pow(10.0, db / 20.0);
}

float deskMicLinear(int db)
{
    return static_cast<float>(std::pow(10.0, static_cast<double>(db) / 20.0));
}

} // namespace

DeskLink::DeskLink(QObject *parent) : QObject(parent)
{
    QSettings s;
    allow_ = s.value(QStringLiteral("desk/allow"), false).toBool();
    savedHost_ = s.value(QStringLiteral("desk/hostName")).toString();
    {
        const QVariant raw = s.value(QStringLiteral("deskRemote/micGainDb"));
        int db = kMicGainDefaultDb;
        if (raw.isValid())
            db = raw.toInt();
        const int clamped = std::clamp(db, kMicGainMinDb, kMicGainMaxDb);
        micGainDb_ = clamped;
        micGainLinear_ = deskMicLinear(clamped);
        if (raw.isValid() && clamped != db)
            s.setValue(QStringLiteral("deskRemote/micGainDb"), clamped);
    }
    setLamp(tr("Tailscale is not connected."));

    probeTimer_ = new QTimer(this);
    probeTimer_->setInterval(3000);
    connect(probeTimer_, &QTimer::timeout, this, &DeskLink::probe);
    probeTimer_->start();

    pumpTimer_ = new QTimer(this);
    pumpTimer_->setInterval(100);
    connect(pumpTimer_, &QTimer::timeout, this, &DeskLink::pump);

    hostRing_.assign(kRingCap, 0);
    playRing_.assign(kRingCap, 0);
    audioTimer_ = new QTimer(this);
    audioTimer_->setInterval(20);
    connect(audioTimer_, &QTimer::timeout, this, &DeskLink::flushHostAudio);
    audioTimer_->start();

    specTimer_ = new QTimer(this);
    specTimer_->setInterval(16);
    connect(specTimer_, &QTimer::timeout, this, &DeskLink::flushHostSpectrum);
    specTimer_->start();

    probe();
}

DeskLink::~DeskLink()
{
    sendAudio_.store(false);
    sendSpec_.store(false);
    if (specPeer_) specPeer_->disconnect(this);
    if (audioPeer_) audioPeer_->disconnect(this);
    if (peer_) peer_->disconnect(this);
    stopDeskPlay();
    stopDeskMic();
}

QString DeskLink::nameProblem(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return tr("Enter the shack name.");
    if (n.contains(QLatin1Char(' ')) || n.contains(QLatin1Char(':'))
        || n.contains(QLatin1Char('/')))
        return tr("Enter only the Tailscale name, with no port.");

    QHostAddress addr(n);
    if (!addr.isNull()) {
        if (ipv4InTailscale(addr)) return {};
        return tr("That address is not a Tailscale address. Paste the Tailscale name.");
    }

    static const QRegularExpression ok(
        QStringLiteral("^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
                       "(?:\\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$"));
    if (!ok.match(n).hasMatch())
        return tr("That name is not a Tailscale name.");
    return {};
}

bool DeskLink::isTailscale4(const QString &ip)
{
    return ipv4InTailscale(QHostAddress(ip));
}

void DeskLink::setLamp(const QString &text)
{
    if (lamp_ == text) return;
    lamp_ = text;
}

void DeskLink::publish()
{
    emit changed();
}

void DeskLink::probe()
{
    if (probeProc_) return;
    probeProc_ = new QProcess(this);
    probeProc_->setProgram(tailscaleExe());
    probeProc_->setArguments({QStringLiteral("status"), QStringLiteral("--json")});
    connect(probeProc_, &QProcess::finished, this, &DeskLink::readProbe);
    probeProc_->start();
}

void DeskLink::readProbe()
{
    QProcess *proc = probeProc_;
    probeProc_ = nullptr;
    const QByteArray out = proc ? proc->readAllStandardOutput() : QByteArray();
    if (proc) proc->deleteLater();

    QString name;
    QString addr;
    bool up = false;
    const QJsonDocument doc = QJsonDocument::fromJson(out);
    if (doc.isObject()) {
        const QJsonObject self = doc.object().value(QStringLiteral("Self")).toObject();
        up = self.value(QStringLiteral("Online")).toBool();
        name = self.value(QStringLiteral("DNSName")).toString();
        if (name.endsWith(QLatin1Char('.'))) name.chop(1);
        if (name.isEmpty())
            name = self.value(QStringLiteral("HostName")).toString();
        const QJsonArray ips = self.value(QStringLiteral("TailscaleIPs")).toArray();
        for (const QJsonValue &v : ips) {
            const QString ip = v.toString();
            if (isTailscale4(ip)) { addr = ip; break; }
        }
    }
    if (!up || addr.isEmpty()) {
        name.clear();
        addr.clear();
        up = false;
    }

    const bool changed = name != selfName_ || addr != selfAddr_ || up != tailscaleUp_;
    selfName_ = name;
    selfAddr_ = addr;
    tailscaleUp_ = up;
    if (changed) {
        if (!up)
            setLamp(tr("Tailscale is not connected."));
        else if (!peer_)
            setLamp(allow_ ? tr("Waiting for a desk.")
                           : tr("This computer's Tailscale name is ready to copy."));
    }
    if (allow_) tryListen();
    if (changed || allow_) publish();
}

void DeskLink::tryListen()
{
    if (!allow_ || !tailscaleUp_ || selfAddr_.isEmpty()) {
        if (server_) server_->close();
        if (audioServer_) audioServer_->close();
        if (specServer_) specServer_->close();
        specListenLogged_ = false;
        hosting_ = false;
        return;
    }
    if (!server_) {
        server_ = new QTcpServer(this);
        connect(server_, &QTcpServer::newConnection, this, &DeskLink::onNewConnection);
    }
    const QHostAddress want(selfAddr_);
    if (!(server_->isListening() && server_->serverAddress() == want
          && server_->serverPort() == kPort)) {
        server_->close();
        hosting_ = server_->listen(want, kPort);
    } else {
        hosting_ = true;
    }
    if (!hosting_) {
        setLamp(tr("Could not listen on the Tailscale address. %1")
                    .arg(server_->errorString()));
        emit statusMessage(lamp_);
        return;
    }
    if (!audioServer_) {
        audioServer_ = new QTcpServer(this);
        connect(audioServer_, &QTcpServer::newConnection,
                this, &DeskLink::onAudioConnection);
    }
    if (!(audioServer_->isListening() && audioServer_->serverAddress() == want
          && audioServer_->serverPort() == kAudioPort)) {
        audioServer_->close();
        if (!audioServer_->listen(want, kAudioPort)) {
            setLamp(tr("Could not listen for desk audio. %1")
                        .arg(audioServer_->errorString()));
            emit statusMessage(lamp_);
            qWarning("[desk] audio listen failed: %s",
                     qPrintable(audioServer_->errorString()));
            return;
        }
    }
    if (!audioListenLogged_) {
        audioListenLogged_ = true;
        qWarning("[desk] listening for receiver audio on %s:%d",
                 qPrintable(selfAddr_), int(kAudioPort));
    }
    if (!specServer_) {
        specServer_ = new QTcpServer(this);
        connect(specServer_, &QTcpServer::newConnection,
                this, &DeskLink::onSpecConnection);
    }
    if (!(specServer_->isListening() && specServer_->serverAddress() == want
          && specServer_->serverPort() == kSpecPort)) {
        specServer_->close();
        if (!specServer_->listen(want, kSpecPort)) {
            qWarning("[desk] spectrum listen failed: %s",
                     qPrintable(specServer_->errorString()));
        }
    }
    if (specServer_->isListening() && !specListenLogged_) {
        specListenLogged_ = true;
        qWarning("[desk] listening for spectrum on %s:%d",
                 qPrintable(selfAddr_), int(kSpecPort));
    }
    if (!peer_)
        setLamp(tr("Waiting for a desk."));
}

void DeskLink::setAllowDesk(bool on)
{
    if (on && peer_ && !weHost_) {
        setLamp(tr("Detach from the shack before allowing a desk."));
        emit statusMessage(lamp_);
        publish();
        return;
    }
    if (allow_ == on) return;
    allow_ = on;
    QSettings s;
    s.setValue(QStringLiteral("desk/allow"), allow_);
    if (!allow_) {
        closePeer(tr("Desk allow is off."));
        if (server_) server_->close();
        if (audioServer_) audioServer_->close();
        if (specServer_) specServer_->close();
        specListenLogged_ = false;
        hosting_ = false;
        setLamp(tailscaleUp_ ? tr("This computer's Tailscale name is ready to copy.")
                             : tr("Tailscale is not connected."));
    } else {
        tryListen();
        emit statusMessage(hosting_ ? tr("A desk may attach.") : lamp_);
    }
    publish();
}

void DeskLink::dropDesk()
{
    if (!peer_ || !weHost_) return;
    sendLine("drop");
    if (peer_) peer_->flush();
    closePeer({});
    setLamp(tr("Waiting for a desk."));
    emit statusMessage(tr("The desk was dropped."));
    publish();
}

void DeskLink::attachToHost(const QString &name)
{
    const QString problem = nameProblem(name);
    if (!problem.isEmpty()) {
        setLamp(problem);
        emit statusMessage(problem);
        publish();
        return;
    }
    if (allow_ || weHost_) {
        setLamp(tr("Turn Allow desk off before attaching to a shack."));
        emit statusMessage(lamp_);
        publish();
        return;
    }
    const QString host = name.trimmed();
    savedHost_ = host;
    QSettings s;
    s.setValue(QStringLiteral("desk/hostName"), savedHost_);
    closePeer({});
    attaching_ = true;
    setLamp(tr("Looking up %1.").arg(host));
    publish();

    const int gen = ++lookupGen_;
    QHostAddress literal(host);
    if (ipv4InTailscale(literal)) {
        auto *sock = new QTcpSocket(this);
        connect(sock, &QTcpSocket::connected, this, [this, gen, sock]() {
            if (gen != lookupGen_) { sock->deleteLater(); return; }
            attaching_ = false;
            takeSocket(sock, false);
        });
        connect(sock, &QTcpSocket::errorOccurred, this, [this, gen, sock](QAbstractSocket::SocketError) {
            if (gen != lookupGen_) { sock->deleteLater(); return; }
            attaching_ = false;
            setLamp(tr("Could not reach the shack. %1").arg(sock->errorString()));
            emit statusMessage(lamp_);
            sock->deleteLater();
            publish();
        });
        sock->connectToHost(literal, kPort);
        return;
    }

    QHostInfo::lookupHost(host, this, [this, gen, host](const QHostInfo &info) {
        if (gen != lookupGen_) return;
        QHostAddress pick;
        for (const QHostAddress &a : info.addresses()) {
            if (ipv4InTailscale(a)) { pick = a; break; }
        }
        if (pick.isNull()) {
            attaching_ = false;
            setLamp(tr("That name is not on the Tailscale mesh."));
            emit statusMessage(lamp_);
            publish();
            return;
        }
        auto *sock = new QTcpSocket(this);
        connect(sock, &QTcpSocket::connected, this, [this, gen, sock]() {
            if (gen != lookupGen_) { sock->deleteLater(); return; }
            if (!ipv4InTailscale(sock->peerAddress())) {
                attaching_ = false;
                setLamp(tr("That name did not stay on the Tailscale mesh."));
                sock->deleteLater();
                publish();
                return;
            }
            attaching_ = false;
            takeSocket(sock, false);
        });
        connect(sock, &QTcpSocket::errorOccurred, this,
                [this, gen, sock](QAbstractSocket::SocketError) {
            if (gen != lookupGen_) { sock->deleteLater(); return; }
            attaching_ = false;
            setLamp(tr("Could not reach the shack. %1").arg(sock->errorString()));
            emit statusMessage(lamp_);
            sock->deleteLater();
            publish();
        });
        sock->connectToHost(pick, kPort);
    });
}

void DeskLink::detach()
{
    ++lookupGen_;
    attaching_ = false;
    if (!peer_ || weHost_) {
        setLamp(tailscaleUp_ ? tr("This computer's Tailscale name is ready to copy.")
                             : tr("Tailscale is not connected."));
        publish();
        return;
    }
    sendLine("drop");
    if (peer_) peer_->flush();
    closePeer({});
    setLamp(tr("Detached."));
    emit statusMessage(lamp_);
    publish();
}

void DeskLink::onNewConnection()
{
    if (!server_) return;
    while (QTcpSocket *sock = server_->nextPendingConnection()) {
        if (peer_ || !ipv4InTailscale(sock->peerAddress())) {
            sock->write(peer_ ? "busy\n" : "refused\n");
            sock->flush();
            sock->disconnectFromHost();
            sock->deleteLater();
            continue;
        }
        takeSocket(sock, true);
    }
}

void DeskLink::takeSocket(QTcpSocket *sock, bool hosting)
{
    peer_ = sock;
    weHost_ = hosting;
    peerName_.clear();
    rttMs_ = -1;
    rxBuf_.clear();
    lastRxMs_ = nowMs();
    pingSeq_ = 0;
    pingAtMs_ = -1;
    sock->setParent(this);
    connect(sock, &QTcpSocket::readyRead, this, &DeskLink::onSocketRead);
    connect(sock, &QTcpSocket::disconnected, this, &DeskLink::onSocketClosed);
    pumpTimer_->start();
    if (!hosting) {
        ensureDeskAudio();
        ensureDeskSpec();
    }
    emit peerReady();
    if (hosting) {
        sendLine("hello 1");
        if (!selfName_.isEmpty())
            sendLine("host " + selfName_.toUtf8());
        setLamp(tr("Desk attached. Measuring the path."));
        emit statusMessage(tr("A desk attached."));
    } else {
        const QString who = selfName_.isEmpty()
            ? QHostInfo::localHostName() : selfName_;
        sendLine("desk " + who.toUtf8());
        setLamp(tr("Attached. Measuring the path."));
        emit statusMessage(tr("Attached to the shack."));
    }
    publish();
}

void DeskLink::onSocketRead()
{
    if (!peer_) return;
    rxBuf_.append(peer_->readAll());
    while (true) {
        const int nl = rxBuf_.indexOf('\n');
        if (nl < 0) break;
        QByteArray line = rxBuf_.left(nl);
        rxBuf_.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (line.size() > 512) continue;
        lastRxMs_ = nowMs();
        noteLine(line);
    }
}

void DeskLink::noteLine(const QByteArray &line)
{
    if (line == "drop") {
        const bool hosted = weHost_;
        closePeer({});
        setLamp(hosted ? tr("The desk detached.") : tr("The shack dropped this desk."));
        emit statusMessage(lamp_);
        publish();
        return;
    }
    if (line.startsWith("ping ")) {
        sendLine("pong " + line.mid(5));
        return;
    }
    if (line.startsWith("pong ")) {
        bool ok = false;
        const quint32 seq = line.mid(5).toUInt(&ok);
        if (ok && seq == pingSeq_ && pingAtMs_ >= 0)
            applyRtt(int(nowMs() - pingAtMs_));
        return;
    }
    if (line == "voice") {
        if (weHost_) emit voiceRequested();
        return;
    }
    if (line == "vend") {
        if (weHost_) emit voiceEnded();
        return;
    }
    if (line == "vgrant") {
        if (!weHost_) {
            voiceHeld_ = true;
            setLamp(tr("Voice is on. MOX and Tune key the shack."));
            emit statusMessage(lamp_);
            publish();
        }
        return;
    }
    if (line.startsWith("vchal ")) {
        if (weHost_) return;
        const QList<QByteArray> p = line.mid(6).split(' ');
        if (p.size() != 3 || voicePassword_.isEmpty()) {
            voicePassword_.fill(QChar(u'\0'));
            voicePassword_.clear();
            emit statusMessage(tr("Type the Remote password and choose Start voice."));
            return;
        }
        const QByteArray nonce = QByteArray::fromHex(p[0]);
        const QByteArray salt = QByteArray::fromBase64(p[1]);
        bool ok = false;
        const int iters = p[2].toInt(&ok);
        if (!ok || nonce.size() != 32 || salt.size() < 8 || iters < 1) {
            voicePassword_.fill(QChar(u'\0'));
            voicePassword_.clear();
            emit statusMessage(tr("The shack challenge was not usable."));
            return;
        }
        QString phrase = voicePassword_;
        voicePassword_.fill(QChar(u'\0'));
        voicePassword_.clear();
        const int gen = ++voiceProofGen_;
        // 600k PBKDF2 iterations take longer than the 350 ms quiet
        // limit. Derive off this thread so the keepalive still runs.
        std::thread([this, gen, phrase = std::move(phrase), nonce, salt, iters]() mutable {
            const QByteArray dk = QPasswordDigestor::deriveKeyPbkdf2(
                QCryptographicHash::Sha256, phrase.trimmed().toUtf8(),
                salt, iters, 32);
            phrase.fill(QChar(u'\0'));
            phrase.clear();
            const QByteArray mac = QMessageAuthenticationCode::hash(
                nonce, dk, QCryptographicHash::Sha256);
            QMetaObject::invokeMethod(this, [this, gen, mac]() {
                if (gen != voiceProofGen_ || !peer_ || weHost_) return;
                sendLine("vproof " + mac.toHex());
            }, Qt::QueuedConnection);
        }).detach();
        return;
    }
    if (line.startsWith("vproof ")) {
        if (!weHost_) return;
        const QByteArray mac = QByteArray::fromHex(line.mid(7).trimmed());
        emit voiceProof(mac);
        return;
    }
    if (line.startsWith("vdeny ")) {
        if (!weHost_) {
            voiceHeld_ = false;
            const QByteArray why = line.mid(6).trimmed();
            QString text = tr("The shack refused voice.");
            if (why == "no_password")
                text = tr("The shack has no Remote password.");
            else if (why == "rtt")
                text = tr("The path is too slow to transmit.");
            else if (why == "busy")
                text = tr("Another remote already holds the microphone.");
            else if (why == "mode")
                text = tr("This mode does not transmit from the desk.");
            else if (why == "inhibit")
                text = tr("Transmit is inhibited on the shack.");
            emit statusMessage(text);
            publish();
        }
        return;
    }
    if (line.startsWith("vlock ") || line.startsWith("vwait ")) {
        if (!weHost_) {
            voiceHeld_ = false;
            bool ok = false;
            const int sec = line.mid(6).toInt(&ok);
            emit statusMessage(line.startsWith("vlock ")
                ? tr("Voice is locked for %1 seconds.").arg(ok ? sec : 0)
                : tr("Wait %1 seconds, then choose Start voice again.").arg(ok ? sec : 0));
            publish();
        }
        return;
    }
    if (line.startsWith("mox ")) {
        const QList<QByteArray> p = line.mid(4).split(' ');
        if (weHost_) {
            bool ok = false;
            const int on = p.value(0).toInt(&ok);
            if (ok) emit remoteMox(on != 0);
        } else if (!p.isEmpty()) {
            bool a = false, b = false;
            const int on = p.at(0).toInt(&a);
            const int own = p.size() > 1 ? p.at(1).toInt(&b) : 0;
            if (a) {
                moxLit_ = on != 0;
                deskOwnsKey_ = b && own != 0;
                if (!moxLit_ || !deskOwnsKey_) stopDeskMic();
                else if (voiceHeld_) startDeskMic();
                publish();
            }
        }
        return;
    }
    if (line.startsWith("tun ")) {
        bool ok = false;
        const int on = line.mid(4).toInt(&ok);
        if (!ok) return;
        if (weHost_) emit remoteTune(on != 0);
        else {
            tuneLit_ = on != 0;
            publish();
        }
        return;
    }
    if (line.startsWith("rtt ")) {
        bool ok = false;
        const int ms = line.mid(4).toInt(&ok);
        if (ok && weHost_) applyRtt(ms);
        return;
    }
    if (line.startsWith("desk ")) {
        peerName_ = QString::fromUtf8(line.mid(5)).trimmed();
        publish();
        return;
    }
    if (line.startsWith("host ")) {
        peerName_ = QString::fromUtf8(line.mid(5)).trimmed();
        publish();
        return;
    }
    if (line.startsWith("freq ")) {
        bool ok = false;
        const quint32 hz = line.mid(5).toUInt(&ok);
        if (ok && hz >= 1000 && hz <= 600000000u)
            emit remoteFreq(hz);
        return;
    }
    if (line.startsWith("mode ")) {
        const QString mode = QString::fromUtf8(line.mid(5)).trimmed();
        if (mode.size() >= 2 && mode.size() <= 5)
            emit remoteMode(mode);
        return;
    }
    if (line.startsWith("bw ")) {
        bool ok = false;
        const int hz = line.mid(3).toInt(&ok);
        if (ok && hz >= 50 && hz <= 20000) emit remoteBandwidth(hz);
        return;
    }
    if (line.startsWith("rit ")) {
        const QList<QByteArray> p = line.mid(4).split(' ');
        if (p.size() == 2) {
            bool a = false, b = false;
            const int on = p[0].toInt(&a);
            const int off = p[1].toInt(&b);
            if (a && b && off >= -9999 && off <= 9999)
                emit remoteRit(on != 0, off);
        }
        return;
    }
    if (line.startsWith("split ")) {
        bool ok = false;
        const int on = line.mid(6).toInt(&ok);
        if (ok) emit remoteSplit(on != 0);
        return;
    }
    if (line.startsWith("agc ")) {
        const QString m = QString::fromLatin1(line.mid(4)).trimmed().toLower();
        if (m == QLatin1String("off") || m == QLatin1String("fast")
            || m == QLatin1String("med") || m == QLatin1String("slow"))
            emit remoteAgc(m);
        return;
    }
    if (line.startsWith("nb ")) {
        const QList<QByteArray> p = line.mid(3).split(' ');
        if (p.size() == 2) {
            bool a = false, b = false;
            const int on = p[0].toInt(&a);
            const int st = p[1].toInt(&b);
            if (a && b && st >= 0 && st <= 1000)
                emit remoteNb(on != 0, st / 1000.0);
        }
        return;
    }
    if (line.startsWith("bin ")) {
        const QList<QByteArray> p = line.mid(4).split(' ');
        if (p.size() == 2) {
            bool a = false, b = false;
            const int on = p[0].toInt(&a);
            const int depth = p[1].toInt(&b);
            if (a && b && depth >= 0 && depth <= 1000)
                emit remoteBin(on != 0, depth / 1000.0);
        }
        return;
    }
    if (line.startsWith("anf ")) {
        bool ok = false;
        const int on = line.mid(4).toInt(&ok);
        if (ok) emit remoteAnf(on != 0);
        return;
    }
    if (line.startsWith("lms ")) {
        const QList<QByteArray> p = line.mid(4).split(' ');
        if (p.size() == 2) {
            bool a = false, b = false;
            const int on = p[0].toInt(&a);
            const int st = p[1].toInt(&b);
            if (a && b && st >= 0 && st <= 1000)
                emit remoteLms(on != 0, st / 1000.0);
        }
        return;
    }
    if (line.startsWith("apf ")) {
        const QList<QByteArray> p = line.mid(4).split(' ');
        if (p.size() == 2) {
            bool a = false, b = false;
            const int on = p[0].toInt(&a);
            const int gain = p[1].toInt(&b);
            if (a && b && gain >= 0 && gain <= 240)
                emit remoteApf(on != 0, gain / 10.0);
        }
        return;
    }
    if (line.startsWith("nnr ")) {
        bool ok = false;
        const int on = line.mid(4).toInt(&ok);
        if (ok) emit remoteNnr(on != 0);
        return;
    }
    if (line.startsWith("nr ")) {
        bool ok = false;
        const int on = line.mid(3).toInt(&ok);
        if (ok) emit remoteNr(on != 0);
        return;
    }
    if (line.startsWith("txbw ")) {
        bool ok = false;
        const int hz = line.mid(5).toInt(&ok);
        if (ok && hz >= 50 && hz <= 20000) emit remoteTxBandwidth(hz);
        return;
    }
    if (line.startsWith("txlo ")) {
        bool ok = false;
        const int hz = line.mid(5).toInt(&ok);
        if (ok && hz >= 0 && hz <= 500) emit remoteTxLow(hz);
        return;
    }
    if (line.startsWith("rxlo ")) {
        bool ok = false;
        const int hz = line.mid(5).toInt(&ok);
        if (ok && hz >= 0 && hz <= 500) emit remoteRxLow(hz);
        return;
    }
    if (line.startsWith("dapply ")) {
        const QByteArray raw = QByteArray::fromBase64(line.mid(7).trimmed());
        if (!raw.isEmpty() && raw.size() <= 65536)
            emit remoteDeskProfile(raw);
        return;
    }
    if (line == "psrst") {
        emit remotePsReset();
        return;
    }
    if (line.startsWith("psplot ")) {
        const QByteArray raw = QByteArray::fromBase64(line.mid(7).trimmed());
        if (raw.size() == 64 * 4 * 2)
            emit remotePsPlot(raw);
        return;
    }
    if (line.startsWith("ps ")) {
        const QList<QByteArray> p = line.mid(3).split(' ');
        if (p.size() == 9) {
            bool ok = true;
            int v[9];
            for (int i = 0; i < 9; ++i) {
                bool one = false;
                v[i] = p[i].toInt(&one);
                ok = ok && one;
            }
            if (ok)
                emit remotePs(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
        }
        return;
    }
    if (line.startsWith("rig ")) {
        const QByteArray rest = line.mid(4).trimmed();
        const int sp = rest.indexOf(' ');
        bool ok = false;
        const int proto = (sp < 0 ? rest : rest.left(sp)).toInt(&ok);
        const QString label = sp < 0
            ? QString()
            : QString::fromUtf8(rest.mid(sp + 1)).trimmed();
        if (ok && proto >= 0 && proto <= 2) {
            hostProto_ = proto;
            hostRig_ = label;
            emit remoteRig(proto, label);
            publish();
        }
        return;
    }
    if (line.startsWith("antb ")) {
        const QList<QByteArray> p = line.mid(5).split(' ');
        if (p.size() == 3) {
            bool a = false, b = false;
            const int ant = p[1].toInt(&a);
            const int rx = p[2].toInt(&b);
            const QString band = QString::fromUtf8(p[0]);
            if (a && b && !band.isEmpty() && ant >= 1 && ant <= 8
                && rx >= 0 && rx <= 3)
                emit remoteBandAntenna(band, ant, rx);
        }
        return;
    }
    if (line.startsWith("ant ")) {
        const QList<QByteArray> p = line.mid(4).split(' ');
        if (p.size() >= 5) {
            bool a = false, b = false, c = false, d = false, e = false;
            const int alex = p[0].toInt(&a);
            const int count = p[1].toInt(&b);
            const int mask = p[2].toInt(&c);
            const int trx = p[3].toInt(&d);
            const int rx = p[4].toInt(&e);
            const QString ext = p.size() >= 6 ? QString::fromUtf8(p[5]) : QString();
            if (a && b && c && d && e && count >= 0 && count <= 8
                && mask >= 0 && mask <= 15 && trx >= 1 && trx <= 8
                && rx >= 0 && rx <= 3)
                emit remoteAntenna(alex != 0, count, mask, trx, rx, ext);
        }
        return;
    }
    if (line.startsWith("sub ")) {
        bool ok = false;
        const int on = line.mid(4).toInt(&ok);
        if (ok) emit remoteSub(on != 0);
        return;
    }
    if (line.startsWith("rx2 ")) {
        bool ok = false;
        const uint hz = line.mid(4).toUInt(&ok);
        if (ok && hz <= 200000000u) emit remoteRx2(hz);
        return;
    }
    if (line.startsWith("fe ")) {
        const QList<QByteArray> p = line.mid(3).split(' ');
        if (p.size() == 3) {
            bool a = false, b = false, c = false;
            const int value = p[0].toInt(&a);
            const int autoOn = p[1].toInt(&b);
            const int adc = p[2].toInt(&c);
            if (a && b && c && value >= -12 && value <= 48 && adc >= 0 && adc <= 1)
                emit remoteFrontEnd(value, autoOn != 0, adc);
        }
        return;
    }
    if (line.startsWith("sq ")) {
        const QList<QByteArray> p = line.mid(3).split(' ');
        if (p.size() == 2) {
            bool a = false, b = false;
            const int on = p[0].toInt(&a);
            const int th = p[1].toInt(&b);
            if (a && b && th >= 0 && th <= 1000)
                emit remoteSquelch(on != 0, th / 1000.0);
        }
        return;
    }
    if (line.startsWith("drive ")) {
        bool ok = false;
        const int level = line.mid(6).toInt(&ok);
        if (ok && level >= 0 && level <= 255) emit remoteDrive(level);
        return;
    }
    if (line.startsWith("tdm ")) {
        const QList<QByteArray> p = line.mid(4).split(' ');
        if (p.size() == 3) {
            bool a = false, b = false, c = false;
            const int mode = p[0].toInt(&a);
            const int tune = p[1].toInt(&b);
            const int fixed = p[2].toInt(&c);
            if (a && b && c && mode >= 0 && mode <= 2
                && tune >= 0 && tune <= 100 && fixed >= 0 && fixed <= 100)
                emit remoteTuneDrive(mode, tune, fixed);
        }
        return;
    }
    if (line.startsWith("mtr ")) {
        const QList<QByteArray> p = line.mid(4).split(' ');
        if (p.size() == 3) {
            bool a = false, b = false, c = false;
            const int dbm = p[0].toInt(&a);
            const int fwd = p[1].toInt(&b);
            const int rev = p[2].toInt(&c);
            if (a && b && c)
                emit remoteMeters(dbm / 10.0, fwd / 100.0, rev / 100.0);
        }
        return;
    }
}

void DeskLink::applyRtt(int ms)
{
    if (ms < 0) ms = 0;
    if (ms > 10000) ms = 10000;
    rttMs_ = ms;
    if (!weHost_)
        sendLine("rtt " + QByteArray::number(ms));
    const QString who = peerName_.isEmpty()
        ? (weHost_ ? tr("Desk") : tr("Shack")) : peerName_;
    if (ms > 150)
        setLamp(tr("Not supported, %1 ms to %2. Transmit stays off.").arg(ms).arg(who));
    else if (ms >= 100)
        setLamp(tr("Attached to %1, %2 ms. Transmit stays off.").arg(who).arg(ms));
    else
        setLamp(tr("Attached to %1, %2 ms. %3").arg(who).arg(ms).arg(audioClause()));
    publish();
}

QString DeskLink::audioClause() const
{
    if (!playBlock_.isEmpty()) return playBlock_;
    const bool sock = audioUp_.load();
    const qint64 now = nowMs();
    if (weHost_) {
        const bool live = (now - lastLocalAudioMs_.load()) < 1000;
        if (!sock)
            return tr("The desk is not on the audio port.");
        if (!live)
            return tr("Start this radio to send the receiver.");
        return tr("Receiver audio is going to the desk.");
    }
    const bool heard = (now - lastPlayMs_.load()) < 1000;
    if (!sock)
        return tr("Receiver audio port is not connected.");
    if (!heard)
        return tr("Waiting for the shack radio.");
    if (lastPeak_.load() < 8)
        return tr("Shack audio is arriving silent. Raise the shack volume and clear mute.");
    return tr("Playing the shack receiver.");
}

void DeskLink::armAudioClient()
{
    if (audioPeer_) return;
    audioPeer_ = new QTcpSocket(this);
    connect(audioPeer_, &QTcpSocket::readyRead, this, &DeskLink::onAudioRead);
    connect(audioPeer_, &QTcpSocket::connected, this, [this]() {
        audioUp_.store(true);
        qWarning("[desk] receiver audio connected");
    });
    connect(audioPeer_, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        audioUp_.store(false);
        if (audioPeer_)
            qWarning("[desk] receiver audio: %s",
                     qPrintable(audioPeer_->errorString()));
    });
}

void DeskLink::ensureDeskAudio()
{
    if (weHost_ || !peer_) return;
    if (audioPeer_ && audioPeer_->state() == QAbstractSocket::ConnectedState) {
        audioUp_.store(true);
        return;
    }
    const qint64 now = nowMs();
    if (audioPeer_ && audioPeer_->state() == QAbstractSocket::ConnectingState) {
        if (audioConnectAt_ > 0 && (now - audioConnectAt_) > 3000)
            audioPeer_->abort();
        return;
    }
    if (audioPeer_ && audioPeer_->state() != QAbstractSocket::UnconnectedState)
        return;
    if (now < audioRetryAt_) return;
    audioRetryAt_ = now + 1000;
    audioConnectAt_ = now;
    armAudioClient();
    qWarning("[desk] receiver audio connecting to %s:%d",
             qPrintable(peer_->peerAddress().toString()), int(kAudioPort));
    audioPeer_->connectToHost(peer_->peerAddress(), kAudioPort);
}

void DeskLink::pump()
{
    if (!peer_) {
        pumpTimer_->stop();
        return;
    }
    if (lastRxMs_ >= 0 && (nowMs() - lastRxMs_) > 350) {
        closePeer({});
        setLamp(tr("The link went quiet."));
        emit statusMessage(lamp_);
        publish();
        return;
    }
    if (!weHost_) {
        ++pingSeq_;
        pingAtMs_ = nowMs();
        sendLine("ping " + QByteArray::number(pingSeq_));
    } else if (++meterDiv_ >= 2) {
        meterDiv_ = 0;
        emit metersTick();
    }
    if (weHost_ && keying_ && lastRxMs_ >= 0 && (nowMs() - lastRxMs_) > 300) {
        keying_ = false;
        emit forceUnkey();
    }
}

void DeskLink::sendLine(const QByteArray &line)
{
    if (!peer_) return;
    peer_->write(line);
    peer_->write("\n");
}

void DeskLink::sendTune(quint32 hz)
{
    if (!peer_ || hz < 1000) return;
    sendLine("freq " + QByteArray::number(hz));
}

void DeskLink::sendMode(const QString &mode)
{
    const QString m = mode.trimmed();
    if (!peer_ || m.size() < 2 || m.size() > 5) return;
    sendLine("mode " + m.toLatin1());
}

void DeskLink::sendBandwidth(int hz)
{
    if (!peer_ || hz < 50 || hz > 20000) return;
    sendLine("bw " + QByteArray::number(hz));
}

void DeskLink::sendRit(bool on, int offsetHz)
{
    if (!peer_) return;
    offsetHz = std::clamp(offsetHz, -9999, 9999);
    sendLine("rit " + QByteArray::number(on ? 1 : 0)
             + " " + QByteArray::number(offsetHz));
}

void DeskLink::sendSplit(bool on)
{
    if (!peer_) return;
    sendLine("split " + QByteArray::number(on ? 1 : 0));
}

void DeskLink::sendAgc(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (!peer_ || (m != QLatin1String("off") && m != QLatin1String("fast")
                   && m != QLatin1String("med") && m != QLatin1String("slow")))
        return;
    sendLine("agc " + m.toLatin1());
}

void DeskLink::sendNr(bool on)
{
    if (!peer_) return;
    sendLine("nr " + QByteArray::number(on ? 1 : 0));
}

void DeskLink::sendNnr(bool on)
{
    if (!peer_) return;
    sendLine("nnr " + QByteArray::number(on ? 1 : 0));
}

void DeskLink::sendNb(bool on, double strength)
{
    if (!peer_) return;
    const int st = std::clamp(int(std::lrint(strength * 1000.0)), 0, 1000);
    sendLine("nb " + QByteArray::number(on ? 1 : 0)
             + " " + QByteArray::number(st));
}

void DeskLink::sendBin(bool on, double depth)
{
    if (!peer_) return;
    const int d = std::clamp(int(std::lrint(depth * 1000.0)), 0, 1000);
    sendLine("bin " + QByteArray::number(on ? 1 : 0)
             + " " + QByteArray::number(d));
}

void DeskLink::sendAnf(bool on)
{
    if (!peer_) return;
    sendLine("anf " + QByteArray::number(on ? 1 : 0));
}

void DeskLink::sendLms(bool on, double strength)
{
    if (!peer_) return;
    const int st = std::clamp(int(std::lrint(strength * 1000.0)), 0, 1000);
    sendLine("lms " + QByteArray::number(on ? 1 : 0)
             + " " + QByteArray::number(st));
}

void DeskLink::sendApf(bool on, double gainDb)
{
    if (!peer_) return;
    const int g = std::clamp(int(std::lrint(gainDb * 10.0)), 0, 240);
    sendLine("apf " + QByteArray::number(on ? 1 : 0)
             + " " + QByteArray::number(g));
}

void DeskLink::sendTxBandwidth(int hz)
{
    if (!peer_ || hz < 50 || hz > 20000) return;
    sendLine("txbw " + QByteArray::number(hz));
}

void DeskLink::sendRxLow(int hz)
{
    if (!peer_ || hz < 0 || hz > 500) return;
    sendLine("rxlo " + QByteArray::number(hz));
}

void DeskLink::sendDeskProfile(const QByteArray &json)
{
    if (!peer_ || weHost_ || json.isEmpty() || json.size() > 65536) return;
    sendLine("dapply " + json.toBase64());
}

void DeskLink::sendPs(int live, int fb, int att, int correcting, int cal, int fsm,
                      int d0, int d1, int spr)
{
    if (!peer_ || !weHost_) return;
    sendLine("ps " + QByteArray::number(live ? 1 : 0)
             + " " + QByteArray::number(fb)
             + " " + QByteArray::number(att)
             + " " + QByteArray::number(correcting ? 1 : 0)
             + " " + QByteArray::number(cal)
             + " " + QByteArray::number(fsm)
             + " " + QByteArray::number(d0)
             + " " + QByteArray::number(d1)
             + " " + QByteArray::number(spr));
}

void DeskLink::sendPsPlot(const QByteArray &raw)
{
    if (!peer_ || !weHost_ || raw.size() != 64 * 4 * 2) return;
    sendLine("psplot " + raw.toBase64());
}

void DeskLink::requestRemotePsReset()
{
    if (!peer_ || weHost_) return;
    sendLine("psrst");
}

void DeskLink::sendTxLow(int hz)
{
    if (!peer_) return;
    hz = std::clamp(hz, 0, 500);
    sendLine("txlo " + QByteArray::number(hz));
}

void DeskLink::sendRig(int protocol, const QString &label)
{
    if (!peer_ || !weHost_) return;
    protocol = std::clamp(protocol, 0, 2);
    QString safe = label;
    safe.replace(QLatin1Char('\n'), QLatin1Char(' '));
    safe.replace(QLatin1Char('\r'), QLatin1Char(' '));
    sendLine("rig " + QByteArray::number(protocol)
             + (safe.isEmpty() ? QByteArray() : " " + safe.toUtf8()));
}

void DeskLink::sendFrontEnd(int value, bool autoOn, int adc)
{
    if (!peer_) return;
    value = std::clamp(value, -12, 48);
    adc = std::clamp(adc, 0, 1);
    sendLine("fe " + QByteArray::number(value)
             + " " + QByteArray::number(autoOn ? 1 : 0)
             + " " + QByteArray::number(adc));
}

void DeskLink::sendAntenna(bool alex, int count, int mask, int trx, int rx,
                           const QString &ext1)
{
    if (!peer_) return;
    count = std::clamp(count, 0, 8);
    mask = std::clamp(mask, 0, 15);
    trx = std::clamp(trx, 1, 8);
    rx = std::clamp(rx, 0, 3);
    QString lab = ext1;
    lab.replace(QLatin1Char(' '), QLatin1Char('_'));
    lab.replace(QLatin1Char('\n'), QLatin1Char('_'));
    lab.replace(QLatin1Char('\r'), QLatin1Char('_'));
    sendLine("ant " + QByteArray::number(alex ? 1 : 0)
             + " " + QByteArray::number(count)
             + " " + QByteArray::number(mask)
             + " " + QByteArray::number(trx)
             + " " + QByteArray::number(rx)
             + (lab.isEmpty() ? QByteArray() : " " + lab.toUtf8()));
}

void DeskLink::sendBandAntenna(const QString &band, int ant, int rx)
{
    if (!peer_) return;
    QString b = band;
    b.replace(QLatin1Char(' '), QLatin1Char('_'));
    b.replace(QLatin1Char('\n'), QLatin1Char('_'));
    if (b.isEmpty()) return;
    ant = std::clamp(ant, 1, 8);
    rx = std::clamp(rx, 0, 3);
    sendLine("antb " + b.toUtf8()
             + " " + QByteArray::number(ant)
             + " " + QByteArray::number(rx));
}

void DeskLink::sendSub(bool on)
{
    if (!peer_) return;
    sendLine("sub " + QByteArray::number(on ? 1 : 0));
}

void DeskLink::sendRx2(quint32 hz)
{
    if (!peer_ || hz > 200000000u) return;
    sendLine("rx2 " + QByteArray::number(hz));
}

void DeskLink::sendSquelch(bool on, double threshold)
{
    if (!peer_) return;
    const int th = std::clamp(int(std::lrint(threshold * 1000.0)), 0, 1000);
    sendLine("sq " + QByteArray::number(on ? 1 : 0)
             + " " + QByteArray::number(th));
}

void DeskLink::sendDrive(int level)
{
    if (!peer_) return;
    level = std::clamp(level, 0, 255);
    sendLine("drive " + QByteArray::number(level));
}

void DeskLink::sendTuneDrive(int mode, int tunePct, int fixedPct)
{
    if (!peer_) return;
    mode = std::clamp(mode, 0, 2);
    tunePct = std::clamp(tunePct, 0, 100);
    fixedPct = std::clamp(fixedPct, 0, 100);
    sendLine("tdm " + QByteArray::number(mode)
             + " " + QByteArray::number(tunePct)
             + " " + QByteArray::number(fixedPct));
}

void DeskLink::sendMeters(double dbm, double fwdW, double revW)
{
    if (!peer_ || !weHost_) return;
    if (std::isnan(dbm)) dbm = -200.0;
    if (std::isnan(fwdW) || fwdW < 0) fwdW = 0;
    if (std::isnan(revW) || revW < 0) revW = 0;
    sendLine("mtr " + QByteArray::number(int(std::lrint(dbm * 10.0)))
             + " " + QByteArray::number(int(std::lrint(fwdW * 100.0)))
             + " " + QByteArray::number(int(std::lrint(revW * 100.0))));
}

void DeskLink::setSpectrumSource(
    std::function<int(float *, int, int *, int *)> src)
{
    specSrc_ = std::move(src);
}

void DeskLink::ensureDeskSpec()
{
    if (weHost_ || !peer_) return;
    if (specPeer_ && specPeer_->state() == QAbstractSocket::ConnectedState)
        return;
    const qint64 now = nowMs();
    if (specPeer_ && specPeer_->state() == QAbstractSocket::ConnectingState) {
        if (specConnectAt_ > 0 && (now - specConnectAt_) > 3000)
            specPeer_->abort();
        return;
    }
    if (specPeer_ && specPeer_->state() != QAbstractSocket::UnconnectedState)
        return;
    if (now < specRetryAt_) return;
    specRetryAt_ = now + 1000;
    specConnectAt_ = now;
    if (!specPeer_) {
        specPeer_ = new QTcpSocket(this);
        connect(specPeer_, &QTcpSocket::readyRead, this, &DeskLink::onSpecRead);
        connect(specPeer_, &QTcpSocket::connected, this, []() {
            qWarning("[desk] spectrum connected");
        });
        connect(specPeer_, &QTcpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) {
            if (specPeer_)
                qWarning("[desk] spectrum: %s",
                         qPrintable(specPeer_->errorString()));
        });
    }
    qWarning("[desk] spectrum connecting to %s:%d",
             qPrintable(peer_->peerAddress().toString()), int(kSpecPort));
    specPeer_->connectToHost(peer_->peerAddress(), kSpecPort);
}

void DeskLink::onSpecConnection()
{
    if (!specServer_) return;
    while (QTcpSocket *sock = specServer_->nextPendingConnection()) {
        const bool same = peer_ && sock->peerAddress() == peer_->peerAddress()
                          && ipv4InTailscale(sock->peerAddress());
        if (!same) {
            sock->disconnectFromHost();
            sock->deleteLater();
            continue;
        }
        if (specPeer_) {
            specPeer_->abort();
            specPeer_->deleteLater();
        }
        specPeer_ = sock;
        specPeer_->setParent(this);
        sendSpec_.store(true);
        qWarning("[desk] spectrum client %s",
                 qPrintable(sock->peerAddress().toString()));
        connect(specPeer_, &QTcpSocket::disconnected, this, [this]() {
            sendSpec_.store(false);
            qWarning("[desk] spectrum client closed");
        });
    }
}

void DeskLink::flushHostSpectrum()
{
    if (!weHost_) {
        ensureDeskSpec();
        return;
    }
    if (!sendSpec_.load(std::memory_order_relaxed) || !specPeer_ || !specSrc_
        || specPeer_->state() != QAbstractSocket::ConnectedState)
        return;
    if (specPeer_->bytesToWrite() > 24000)
        return;
    float db[4096];
    int center = 0;
    int span = 0;
    const int n = specSrc_(db, 4096, &center, &span);
    if (n < 2 || n > 4096 || span <= 0) return;
    QByteArray pkt(14 + n * 2, Qt::Uninitialized);
    char *p = pkt.data();
    p[0] = 'S'; p[1] = 'P'; p[2] = 'C'; p[3] = '1';
    p[4] = char(n & 0xff);
    p[5] = char((n >> 8) & 0xff);
    const auto put32 = [](char *d, quint32 v) {
        d[0] = char(v & 0xff);
        d[1] = char((v >> 8) & 0xff);
        d[2] = char((v >> 16) & 0xff);
        d[3] = char((v >> 24) & 0xff);
    };
    put32(p + 6, quint32(std::max(0, center)));
    put32(p + 10, quint32(span));
    auto *s = reinterpret_cast<qint16 *>(p + 14);
    for (int i = 0; i < n; ++i) {
        const long t = std::lrint(double(db[i]) * 10.0);
        s[i] = static_cast<qint16>(std::clamp(t, -32768L, 32767L));
    }
    specPeer_->write(pkt);
}

void DeskLink::onSpecRead()
{
    if (!specPeer_) return;
    specRx_.append(specPeer_->readAll());
    while (specRx_.size() >= 14) {
        if (!(specRx_[0] == 'S' && specRx_[1] == 'P'
              && specRx_[2] == 'C' && specRx_[3] == '1')) {
            specRx_.remove(0, 1);
            continue;
        }
        const int n = quint8(specRx_[4]) | (quint8(specRx_[5]) << 8);
        if (n < 2 || n > 4096) {
            specRx_.remove(0, 1);
            continue;
        }
        const int need = 14 + n * 2;
        if (specRx_.size() < need) return;
        const auto u32 = [](const QByteArray &b, int o) -> quint32 {
            return quint32(quint8(b[o]))
                 | (quint32(quint8(b[o + 1])) << 8)
                 | (quint32(quint8(b[o + 2])) << 16)
                 | (quint32(quint8(b[o + 3])) << 24);
        };
        const int span = int(u32(specRx_, 10));
        specDb_.resize(static_cast<size_t>(n));
        const auto *s = reinterpret_cast<const qint16 *>(specRx_.constData() + 14);
        for (int i = 0; i < n; ++i)
            specDb_[static_cast<size_t>(i)] = float(s[i]) / 10.0f;
        specCount_ = n;
        specRx_.remove(0, need);
        if (span > 0) emit spectrumFrame(span);
    }
}

void DeskLink::onSocketClosed()
{
    if (!peer_) return;
    const bool hosted = weHost_;
    closePeer({});
    setLamp(hosted && allow_ ? tr("Waiting for a desk.")
                             : tr("The shack link closed."));
    emit statusMessage(lamp_);
    publish();
}

void DeskLink::closePeer(const QString &why)
{
    ++voiceProofGen_;
    const bool wasHost = weHost_;
    const bool wasKey = keying_;
    const bool wasVoice = voiceHeld_;
    if (wasHost && (wasKey || wasVoice)) emit voiceEnded();
    emit sessionEnded(wasHost);
    keying_ = false;
    voiceHeld_ = false;
    moxLit_ = false;
    tuneLit_ = false;
    deskOwnsKey_ = false;
    voiceNonce_.clear();
    voicePassword_.fill(QChar(u'\0'));
    voicePassword_.clear();
    stopDeskMic();
    if (wasKey && wasHost) emit forceUnkey();
    attaching_ = false;
    weHost_ = false;
    meterDiv_ = 0;
    rttMs_ = -1;
    peerName_.clear();
    const bool hadRig = hostProto_ != 0 || !hostRig_.isEmpty();
    hostProto_ = 0;
    hostRig_.clear();
    if (hadRig) emit remoteRig(0, {});
    rxBuf_.clear();
    lastRxMs_ = -1;
    pumpTimer_->stop();
    if (peer_) {
        peer_->disconnect(this);
        peer_->abort();
        peer_->deleteLater();
        peer_ = nullptr;
    }
    sendAudio_.store(false);
    sendSpec_.store(false);
    audioUp_.store(false);
    lastLocalAudioMs_.store(0);
    lastPlayMs_.store(0);
    lastPeak_.store(0);
    audioConnectAt_ = 0;
    playBlock_.clear();
    stopDeskPlay();
    audioRx_.clear();
    {
        std::lock_guard<std::mutex> lk(hostMtx_);
        hostRd_ = 0;
        hostCount_ = 0;
    }
    {
        std::lock_guard<std::mutex> lk(playMtx_);
        playRd_ = 0;
        playCount_ = 0;
        playReady_ = false;
    }
    if (audioPeer_) {
        audioPeer_->disconnect(this);
        audioPeer_->abort();
        audioPeer_->deleteLater();
        audioPeer_ = nullptr;
    }
    specRx_.clear();
    specCount_ = 0;
    if (specPeer_) {
        specPeer_->disconnect(this);
        specPeer_->abort();
        specPeer_->deleteLater();
        specPeer_ = nullptr;
    }
    if (!why.isEmpty()) {
        setLamp(why);
        emit statusMessage(why);
    }
}

void DeskLink::setPlayPos(double pos)
{
    playGain_.store(deskPosToGain(pos), std::memory_order_relaxed);
}

void DeskLink::setPlayMuted(bool muted)
{
    playMuted_.store(muted, std::memory_order_relaxed);
}

void DeskLink::offerRxAudio(const double *lr, int nframes)
{
    if (!lr || nframes <= 0) return;
    lastLocalAudioMs_.store(nowMs(), std::memory_order_relaxed);
    if (!sendAudio_.load(std::memory_order_relaxed))
        return;
    // Never wait. This runs on the thread that feeds the shack speakers.
    // If the network timer has the ring, the desk misses this block.
    std::unique_lock<std::mutex> lk(hostMtx_, std::try_to_lock);
    if (!lk.owns_lock()) return;
    for (int i = 0; i < nframes; ++i) {
        if (hostCount_ >= kRingCap) {
            hostRd_ = (hostRd_ + 1) % kRingCap;
            --hostCount_;
        }
        const double m = (lr[i * 2] + lr[i * 2 + 1]) * 0.5;
        const int wr = (hostRd_ + hostCount_) % kRingCap;
        hostRing_[static_cast<size_t>(wr)] = static_cast<qint16>(
            std::clamp(std::lrint(m * 32767.0), -32768L, 32767L));
        ++hostCount_;
    }
}

void DeskLink::flushHostAudio()
{
    if (!weHost_) {
        ensureDeskAudio();
        feedDeskSpeaker();
    }
    if (!sendAudio_.load(std::memory_order_relaxed) || !audioPeer_
        || audioPeer_->state() != QAbstractSocket::ConnectedState)
        return;
    for (int burst = 0; burst < 4; ++burst) {
        qint16 chunk[960];
        int n = 0;
        {
            std::lock_guard<std::mutex> lk(hostMtx_);
            n = std::min(hostCount_, 960);
            for (int i = 0; i < n; ++i) {
                chunk[i] = hostRing_[static_cast<size_t>(hostRd_)];
                hostRd_ = (hostRd_ + 1) % kRingCap;
            }
            hostCount_ -= n;
        }
        if (n <= 0) return;
        char hdr[6] = {'A', 'U', 'D', '1', char(n & 0xff), char((n >> 8) & 0xff)};
        audioPeer_->write(hdr, 6);
        audioPeer_->write(reinterpret_cast<const char *>(chunk), n * 2);
    }
}

void DeskLink::onAudioConnection()
{
    if (!audioServer_) return;
    while (QTcpSocket *sock = audioServer_->nextPendingConnection()) {
        const bool same = peer_ && sock->peerAddress() == peer_->peerAddress()
                          && ipv4InTailscale(sock->peerAddress());
        if (!same) {
            sock->write("refused\n");
            sock->flush();
            sock->disconnectFromHost();
            sock->deleteLater();
            continue;
        }
        if (audioPeer_) {
            audioPeer_->abort();
            audioPeer_->deleteLater();
        }
        audioPeer_ = sock;
        audioPeer_->setParent(this);
        sendAudio_.store(true);
        audioUp_.store(true);
        qWarning("[desk] desk audio client %s",
                 qPrintable(sock->peerAddress().toString()));
        // Shack → desk only. Bytes that arrive here are discarded.
        // Playback of this socket would mix a second copy into the
        // shack speakers.
        connect(audioPeer_, &QTcpSocket::readyRead, this, &DeskLink::onHostMic);
        connect(audioPeer_, &QTcpSocket::disconnected, this, [this]() {
            sendAudio_.store(false);
            audioUp_.store(false);
            qWarning("[desk] desk audio client closed");
        });
    }
}

void DeskLink::onAudioRead()
{
    if (!audioPeer_) return;
    audioRx_.append(audioPeer_->readAll());
    while (audioRx_.size() >= 6) {
        if (!(audioRx_[0] == 'A' && audioRx_[1] == 'U'
              && audioRx_[2] == 'D' && audioRx_[3] == '1')) {
            audioRx_.remove(0, 1);
            continue;
        }
        const int n = quint8(audioRx_[4]) | (quint8(audioRx_[5]) << 8);
        if (n <= 0 || n > 2048) {
            audioRx_.remove(0, 1);
            continue;
        }
        const int need = 6 + n * 2;
        if (audioRx_.size() < need) return;
        pushPlay(reinterpret_cast<const qint16 *>(audioRx_.constData() + 6), n);
        audioRx_.remove(0, need);
    }
}

void DeskLink::pushPlay(const qint16 *mono, int nframes)
{
    if (weHost_) return;
    bool start = false;
    {
        std::lock_guard<std::mutex> lk(playMtx_);
        int peak = 0;
        for (int i = 0; i < nframes; ++i) {
            if (playCount_ >= kRingCap) {
                playRd_ = (playRd_ + 1) % kRingCap;
                --playCount_;
            }
            const int wr = (playRd_ + playCount_) % kRingCap;
            playRing_[static_cast<size_t>(wr)] = mono[i];
            ++playCount_;
            const int a = mono[i] < 0 ? -int(mono[i]) : int(mono[i]);
            if (a > peak) peak = a;
        }
        lastPlayMs_.store(nowMs(), std::memory_order_relaxed);
        lastPeak_.store(peak, std::memory_order_relaxed);
        if (!playReady_ && playCount_ >= playPrime_) {
            playReady_ = true;
            start = true;
        }
    }
    if (start) startDeskPlay();
    feedDeskSpeaker();
}

void DeskLink::feedDeskSpeaker()
{
    if (weHost_ || !playReady_) return;
    if (!playSink_) startDeskPlay();
    if (!playDev_ || !playSink_) return;
    if (playSink_->state() == QAudio::IdleState) {
        std::lock_guard<std::mutex> lk(playMtx_);
        if (playCount_ < playPrime_) {
            playReady_ = false;
            playPrime_ = std::min(playPrime_ + 960, 9600);
            return;
        }
    }
    for (int burst = 0; burst < 4; ++burst) {
        const int freeFrames = int(playSink_->bytesFree() / 2);
        if (freeFrames <= 0) return;
        qint16 chunk[960];
        int n = 0;
        {
            std::lock_guard<std::mutex> lk(playMtx_);
            n = std::min(playCount_, std::min(freeFrames, 960));
            for (int i = 0; i < n; ++i) {
                chunk[i] = playRing_[static_cast<size_t>(playRd_)];
                playRd_ = (playRd_ + 1) % kRingCap;
            }
            playCount_ -= n;
        }
        if (n <= 0) return;
        const double g = playMuted_.load(std::memory_order_relaxed)
            ? 0.0 : playGain_.load(std::memory_order_relaxed);
        if (g != 1.0) {
            for (int i = 0; i < n; ++i) {
                chunk[i] = static_cast<qint16>(std::clamp(
                    std::lrint(chunk[i] * g), -32768L, 32767L));
            }
        }
        playDev_->write(reinterpret_cast<const char *>(chunk), n * 2);
    }
}

void DeskLink::startDeskPlay()
{
    if (weHost_ || playSink_) return;
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull() || !dev.isFormatSupported(fmt)) {
        playBlock_ = tr("This PC has no 48 kHz output for the desk audio.");
        qWarning("[desk] %s", qPrintable(playBlock_));
        emit statusMessage(playBlock_);
        publish();
        return;
    }
    playBlock_.clear();
    playSink_ = new QAudioSink(dev, fmt, this);
    playSink_->setVolume(1.0);
    playSink_->setBufferSize(19200);
    playDev_ = playSink_->start();
    qWarning("[desk] speaker %s", qPrintable(dev.description()));
    if (!playDev_) {
        playBlock_ = tr("The desk speaker did not start.");
        qWarning("[desk] %s", qPrintable(playBlock_));
        emit statusMessage(playBlock_);
        publish();
    }
}

void DeskLink::stopDeskPlay()
{
    playDev_ = nullptr;
    if (playSink_) {
        playSink_->stop();
        delete playSink_;
        playSink_ = nullptr;
    }
}

void DeskLink::stageVoicePassword(const QString &password)
{
    voicePassword_ = password;
}

void DeskLink::askVoice()
{
    if (!deskSide()) {
        emit statusMessage(tr("Attach to the shack before Start voice."));
        return;
    }
    if (voicePassword_.trimmed().isEmpty()) {
        emit statusMessage(tr("Type the Remote password, then choose Start voice."));
        return;
    }
    sendLine("voice");
}

void DeskLink::endVoice()
{
    if (peer_ && !weHost_) sendLine("vend");
    voiceHeld_ = false;
    voicePassword_.fill(QChar(u'\0'));
    voicePassword_.clear();
    stopDeskMic();
    publish();
}

void DeskLink::requestRemoteMox(bool on)
{
    if (!deskSide() || !voiceHeld_) {
        emit statusMessage(tr("Start voice in Settings → Remote before MOX."));
        return;
    }
    sendLine(on ? "mox 1" : "mox 0");
}

void DeskLink::requestRemoteTune(bool on)
{
    if (!deskSide() || !voiceHeld_) {
        emit statusMessage(tr("Start voice in Settings → Remote before Tune."));
        return;
    }
    sendLine(on ? "tun 1" : "tun 0");
}

void DeskLink::sendMox(bool on, bool deskOwns)
{
    if (!peer_ || !weHost_) return;
    sendLine(QByteArray("mox ") + (on ? "1" : "0") + (deskOwns ? " 1" : " 0"));
}

void DeskLink::sendTuneOn(bool on)
{
    if (!peer_ || !weHost_) return;
    sendLine(on ? "tun 1" : "tun 0");
}

void DeskLink::sendVoiceChallenge(const QByteArray &nonce, const QByteArray &salt, int iters)
{
    if (!peer_ || !weHost_ || nonce.size() != 32) return;
    voiceNonce_ = nonce;
    sendLine("vchal " + nonce.toHex() + " " + salt.toBase64() + " "
             + QByteArray::number(iters));
}

void DeskLink::sendVoiceDeny(const QString &reason)
{
    if (!peer_ || !weHost_) return;
    QString safe = reason;
    safe.replace(QLatin1Char(' '), QLatin1Char('_'));
    sendLine("vdeny " + safe.toUtf8());
}

void DeskLink::sendVoiceHold(const QString &word, int seconds)
{
    if (!peer_ || !weHost_) return;
    const QByteArray w = (word == QLatin1String("lock")) ? "vlock " : "vwait ";
    sendLine(w + QByteArray::number(std::max(0, seconds)));
}

void DeskLink::noteVoiceGrant()
{
    voiceNonce_.clear();
    voiceFails_ = 0;
    voiceWaitMs_ = 0;
    voiceLockMs_ = 0;
    voiceHeld_ = true;
    sendLine("vgrant");
}

void DeskLink::dropVoiceHold()
{
    voiceHeld_ = false;
    voiceNonce_.clear();
    keying_ = false;
    publish();
}

QByteArray DeskLink::takeVoiceNonce()
{
    QByteArray n = voiceNonce_;
    voiceNonce_.clear();
    return n;
}

bool DeskLink::voiceGateOpen(QString *word, int *seconds) const
{
    const qint64 now = nowMs();
    if (voiceLockMs_ > now) {
        if (word) *word = QStringLiteral("lock");
        if (seconds) *seconds = int((voiceLockMs_ - now + 999) / 1000);
        return false;
    }
    if (voiceWaitMs_ > now) {
        if (word) *word = QStringLiteral("wait");
        if (seconds) *seconds = int((voiceWaitMs_ - now + 999) / 1000);
        return false;
    }
    if (word) word->clear();
    if (seconds) *seconds = 0;
    return true;
}

void DeskLink::noteVoiceFail()
{
    voiceNonce_.clear();
    static const int kBackoff[] = {1, 2, 4, 8, 16};
    ++voiceFails_;
    const qint64 now = nowMs();
    if (voiceFails_ >= 5) {
        voiceLockMs_ = now + 15 * 60 * 1000;
        voiceWaitMs_ = voiceLockMs_;
        const int sec = 15 * 60;
        sendLine("vlock " + QByteArray::number(sec));
        emit statusMessage(tr("That desk is locked for 15 minutes."));
        return;
    }
    const int sec = kBackoff[std::clamp(voiceFails_ - 1, 0, 4)];
    voiceWaitMs_ = now + qint64(sec) * 1000;
    sendLine("vwait " + QByteArray::number(sec));
}

void DeskLink::setKeying(bool on)
{
    keying_ = on;
}

void DeskLink::setMicSink(std::function<void(const qint16 *, int)> sink)
{
    micSink_ = std::move(sink);
}

void DeskLink::setMicGainDb(int db)
{
    db = std::clamp(db, kMicGainMinDb, kMicGainMaxDb);
    micGainLinear_ = deskMicLinear(db);
    if (db == micGainDb_) return;
    micGainDb_ = db;
    QSettings().setValue(QStringLiteral("deskRemote/micGainDb"), db);
    emit micGainDbChanged(db);
}

void DeskLink::onHostMic()
{
    if (!weHost_ || !audioPeer_) return;
    micRx_.append(audioPeer_->readAll());
    while (micRx_.size() >= 6) {
        if (!(micRx_[0] == 'M' && micRx_[1] == 'I'
              && micRx_[2] == 'C' && micRx_[3] == '1')) {
            micRx_.remove(0, 1);
            continue;
        }
        const int n = quint8(micRx_[4]) | (quint8(micRx_[5]) << 8);
        if (n <= 0 || n > 2048) {
            micRx_.remove(0, 1);
            continue;
        }
        const int need = 6 + n * 2;
        if (micRx_.size() < need) return;
        if (micSink_ && keying_)
            micSink_(reinterpret_cast<const qint16 *>(micRx_.constData() + 6), n);
        micRx_.remove(0, need);
    }
}

void DeskLink::startDeskMic()
{
    if (weHost_ || micSrc_) return;
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    // The Mic source list is Mic In / TCI / VAC. The laptop device the
    // operator picked is Settings → Audio → VAC1 Input device. Use that
    // for the shack, then the Windows default.
    QAudioDevice dev;
    QString want = QSettings().value(QStringLiteral("deskRemote/micDevice")).toString();
    if (want.isEmpty())
        want = QSettings().value(QStringLiteral("vac1/inputDevice")).toString();
    const auto inputs = QMediaDevices::audioInputs();
    if (!want.isEmpty()) {
        for (const QAudioDevice &d : inputs) {
            if (d.description().compare(want, Qt::CaseInsensitive) == 0) {
                dev = d;
                break;
            }
        }
        if (dev.isNull()) {
            for (const QAudioDevice &d : inputs) {
                if (d.description().contains(want, Qt::CaseInsensitive)
                    || want.contains(d.description(), Qt::CaseInsensitive)) {
                    dev = d;
                    break;
                }
            }
        }
    }
    if (dev.isNull())
        dev = QMediaDevices::defaultAudioInput();
    if (dev.isNull()) {
        emit statusMessage(tr("This PC has no microphone for the shack."));
        return;
    }
    micSrc_ = new QAudioSource(dev, fmt, this);
    micDev_ = micSrc_->start();
    if (!micDev_) {
        delete micSrc_;
        micSrc_ = nullptr;
        emit statusMessage(tr("The microphone did not start."));
        return;
    }
    connect(micDev_, &QIODevice::readyRead, this, &DeskLink::sendMicChunk);
    emit statusMessage(tr("Laptop microphone: %1").arg(dev.description()));
}

void DeskLink::stopDeskMic()
{
    micDev_ = nullptr;
    if (micSrc_) {
        micSrc_->stop();
        delete micSrc_;
        micSrc_ = nullptr;
    }
}

void DeskLink::sendMicChunk()
{
    if (weHost_ || !micDev_ || !audioPeer_) return;
    if (audioPeer_->state() != QAbstractSocket::ConnectedState) return;
    QByteArray pcm = micDev_->readAll();
    const int frames = pcm.size() / 2;
    if (frames <= 0) return;
    auto *s = reinterpret_cast<qint16 *>(pcm.data());
    // Desk input gain, before the samples are packed into MIC1.
    // 0 dB is unity. Full-scale int16 is the ceiling.
    const float g = micGainLinear_;
    for (int i = 0; i < frames; ++i) {
        const long r = std::lround(static_cast<float>(s[i]) * g);
        s[i] = static_cast<qint16>(std::clamp(r, -32768L, 32767L));
    }
    int off = 0;
    while (off < frames) {
        const int n = std::min(960, frames - off);
        char hdr[6] = {'M', 'I', 'C', '1', char(n & 0xff), char((n >> 8) & 0xff)};
        audioPeer_->write(hdr, 6);
        audioPeer_->write(reinterpret_cast<const char *>(s + off), n * 2);
        off += n;
    }
}

} // namespace lyra::ui

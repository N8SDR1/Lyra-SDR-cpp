// Lyra — Kenwood CAT serial server.  See CatServer.h + com_port_design.md §3.

#include "cat/CatServer.h"

#include "hl2_stream.h"
#include "prefs.h"
#include "wdsp_engine.h"

#include <QHostAddress>
#include <QSerialPort>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>

namespace lyra::cat {

CatServer::CatServer(const QString &settingsGroup, lyra::ui::Prefs *prefs,
                     lyra::ipc::HL2Stream *stream,
                     lyra::dsp::WdspEngine *engine, QObject *parent)
    : QObject(parent), group_(settingsGroup), prefs_(prefs), stream_(stream),
      engine_(engine) {
    QSettings s;
    enabled_   = s.value(key("enabled"), false).toBool();
    label_     = s.value(key("label"), QString()).toString();
    transport_ = static_cast<Transport>(
        s.value(key("transport"), int(Transport::Serial)).toInt());
    portName_  = s.value(key("port"), QString()).toString();
    baud_      = s.value(key("baud"), 115200).toInt();
    dataBits_  = s.value(key("data_bits"), 8).toInt();
    parity_    = s.value(key("parity"), 0).toInt();
    stopBits_  = s.value(key("stop_bits"), 1).toInt();
    host_      = s.value(key("host"), QStringLiteral("127.0.0.1")).toString();
    tcpPort_   = s.value(key("tcp_port"), 60000).toInt();
    model_     = static_cast<RigModel>(
        s.value(key("rig_model"), int(RigModel::Ts480)).toInt());

    // AI (Auto-Information): when armed, push an unsolicited IF; on every
    // radio change so a client tracks Lyra without polling.  The slot is
    // always connected; it no-ops unless aiMode_ > 0 and the port is open.
    if (stream_) {
        connect(stream_, &lyra::ipc::HL2Stream::rx1FreqChanged,
                this, &CatServer::onRadioChanged);
        connect(stream_, &lyra::ipc::HL2Stream::moxActiveChanged,
                this, &CatServer::onRadioChanged);
    }
    if (prefs_)
        connect(prefs_, &lyra::ui::Prefs::modeChanged,
                this, &CatServer::onRadioChanged);

    if (enabled_)
        start();
}

CatServer::~CatServer() { stop(); }

bool CatServer::running() const {
    if (transport_ == Transport::Tcp)
        return tcpServer_ && tcpServer_->isListening();
    return port_ && port_->isOpen();
}

QString CatServer::key(const char *leaf) const {
    return group_ + QLatin1Char('/') + QLatin1String(leaf);
}

void CatServer::persist() const {
    QSettings s;
    s.setValue(key("enabled"), enabled_);
    s.setValue(key("label"), label_);
    s.setValue(key("transport"), int(transport_));
    s.setValue(key("port"), portName_);
    s.setValue(key("baud"), baud_);
    s.setValue(key("data_bits"), dataBits_);
    s.setValue(key("parity"), parity_);
    s.setValue(key("stop_bits"), stopBits_);
    s.setValue(key("host"), host_);
    s.setValue(key("tcp_port"), tcpPort_);
    s.setValue(key("rig_model"), int(model_));
}

void CatServer::applySerialParams() {
    if (!port_) return;
    port_->setBaudRate(baud_);
    port_->setDataBits(static_cast<QSerialPort::DataBits>(dataBits_));
    port_->setParity(parity_ == 1   ? QSerialPort::EvenParity
                     : parity_ == 2 ? QSerialPort::OddParity
                                    : QSerialPort::NoParity);
    port_->setStopBits(stopBits_ == 2 ? QSerialPort::TwoStop
                                      : QSerialPort::OneStop);
    port_->setFlowControl(QSerialPort::NoFlowControl);
}

void CatServer::setPortName(const QString &p) {
    if (p == portName_) return;
    const bool wasRunning = (port_ != nullptr);
    if (wasRunning) stop();
    portName_ = p;
    persist();
    if (enabled_ && !portName_.isEmpty()) start();
}

void CatServer::setLabel(const QString &s) {
    if (s == label_) return;
    label_ = s;
    persist();   // cosmetic — no port effect
}

void CatServer::setTransport(Transport t) {
    if (t == transport_) return;
    const bool wasRunning = running();
    if (wasRunning) stop();
    transport_ = t;
    persist();
    if (enabled_) start();
}

void CatServer::setHost(const QString &h) {
    if (h == host_) return;
    const bool wasRunning = running() && transport_ == Transport::Tcp;
    if (wasRunning) stop();
    host_ = h;
    persist();
    if (wasRunning) start();
}

void CatServer::setTcpPort(int p) {
    if (p == tcpPort_) return;
    const bool wasRunning = running() && transport_ == Transport::Tcp;
    if (wasRunning) stop();
    tcpPort_ = p;
    persist();
    if (wasRunning) start();
}

void CatServer::setBaud(int b) {
    if (b == baud_) return;
    baud_ = b;
    persist();
    applySerialParams();
}

void CatServer::setDataBits(int d) {
    if (d == dataBits_) return;
    dataBits_ = d;
    persist();
    applySerialParams();
}

void CatServer::setParity(int p) {
    if (p == parity_) return;
    parity_ = p;
    persist();
    applySerialParams();
}

void CatServer::setStopBits(int s) {
    if (s == stopBits_) return;
    stopBits_ = s;
    persist();
    applySerialParams();
}

void CatServer::setRigModel(RigModel m) {
    if (m == model_) return;
    model_ = m;
    persist();   // only ID;/IF; differ — no restart needed
}

void CatServer::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    persist();
    emit enabledChanged(enabled_);
    if (enabled_) start();
    else          stop();
}

bool CatServer::start() {
    if (running()) return true;
    return transport_ == Transport::Tcp ? startTcp() : startSerial();
}

bool CatServer::startSerial() {
    if (port_) return true;
    if (portName_.isEmpty()) {
        emit statusMessage(QStringLiteral("CAT: no COM port selected"));
        return false;
    }
    port_ = new QSerialPort(portName_, this);
    applySerialParams();
    if (!port_->open(QIODevice::ReadWrite)) {
        emit statusMessage(QStringLiteral("CAT: cannot open %1 (%2)")
                               .arg(portName_, port_->errorString()));
        delete port_;
        port_ = nullptr;
        return false;
    }
    connect(port_, &QSerialPort::readyRead, this, &CatServer::onReadyRead);
    rxBuf_.clear();
    const QString tag = label_.isEmpty() ? group_ : label_;
    emit statusMessage(QStringLiteral("CAT [%1] on %2 @ %3 (%4)")
                           .arg(tag, portName_)
                           .arg(baud_)
                           .arg(model_ == RigModel::Ts2000
                                    ? QStringLiteral("TS-2000")
                                    : QStringLiteral("TS-480")));
    return true;
}

bool CatServer::startTcp() {
    if (tcpServer_) return true;
    tcpServer_ = new QTcpServer(this);
    connect(tcpServer_, &QTcpServer::newConnection,
            this, &CatServer::onNewConnection);
    const QHostAddress addr = host_.isEmpty() ? QHostAddress(QHostAddress::LocalHost)
                                              : QHostAddress(host_);
    if (!tcpServer_->listen(addr, quint16(tcpPort_))) {
        emit statusMessage(QStringLiteral("CAT: cannot listen on %1:%2 (%3)")
                               .arg(host_)
                               .arg(tcpPort_)
                               .arg(tcpServer_->errorString()));
        tcpServer_->deleteLater();
        tcpServer_ = nullptr;
        return false;
    }
    const QString tag = label_.isEmpty() ? group_ : label_;
    emit statusMessage(QStringLiteral("CAT [%1] listening on %2:%3 (%4)")
                           .arg(tag, host_)
                           .arg(tcpPort_)
                           .arg(model_ == RigModel::Ts2000
                                    ? QStringLiteral("TS-2000")
                                    : QStringLiteral("TS-480")));
    return true;
}

void CatServer::stop() {
    if (port_) {
        port_->close();
        port_->deleteLater();
        port_ = nullptr;
    }
    rxBuf_.clear();
    if (tcpServer_) {
        tcpServer_->close();
        tcpServer_->deleteLater();
        tcpServer_ = nullptr;
    }
    for (auto it = tcpBufs_.constBegin(); it != tcpBufs_.constEnd(); ++it)
        it.key()->deleteLater();
    tcpBufs_.clear();
}

void CatServer::onNewConnection() {
    if (!tcpServer_) return;
    while (QTcpSocket *sock = tcpServer_->nextPendingConnection()) {
        tcpBufs_.insert(sock, QByteArray());
        connect(sock, &QTcpSocket::readyRead, this,
                [this, sock]() { handleTcpData(sock); });
        connect(sock, &QTcpSocket::disconnected, this, [this, sock]() {
            tcpBufs_.remove(sock);
            sock->deleteLater();
        });
    }
}

void CatServer::handleTcpData(QTcpSocket *sock) {
    if (!sock || !tcpBufs_.contains(sock)) return;
    QByteArray &buf = tcpBufs_[sock];
    buf.append(sock->readAll());
    int idx;
    while ((idx = buf.indexOf(';')) >= 0) {
        const QByteArray cmd = buf.left(idx);
        buf.remove(0, idx + 1);
        if (cmd.isEmpty()) continue;
        const QByteArray resp = dispatch(cmd);
        if (!resp.isEmpty()) sock->write(resp);
    }
    if (buf.size() > kRxBufCap) buf.clear();
}

void CatServer::broadcast(const QByteArray &resp) {
    if (resp.isEmpty()) return;
    if (transport_ == Transport::Tcp) {
        for (auto it = tcpBufs_.constBegin(); it != tcpBufs_.constEnd(); ++it)
            it.key()->write(resp);
    } else if (port_ && port_->isOpen()) {
        port_->write(resp);
    }
}

void CatServer::onReadyRead() {
    if (!port_) return;
    rxBuf_.append(port_->readAll());
    int idx;
    while ((idx = rxBuf_.indexOf(';')) >= 0) {
        const QByteArray cmd = rxBuf_.left(idx);   // command WITHOUT the ';'
        rxBuf_.remove(0, idx + 1);
        if (cmd.isEmpty()) continue;
        const QByteArray resp = dispatch(cmd);
        if (!resp.isEmpty()) port_->write(resp);
    }
    // A client that floods bytes with no ';' must not grow the buffer
    // unbounded.
    if (rxBuf_.size() > kRxBufCap) rxBuf_.clear();
}

qint64 CatServer::carrierHz() const {
    const qint64 dds = stream_ ? qint64(stream_->rx1FreqHz()) : 0;
    const qint64 off = engine_ ? engine_->markerOffsetHz() : 0;
    return dds + off;
}

// Kenwood TS-480/2000 mode numbering (the de-facto standard every client +
// Hamlib agree on): 1=LSB 2=USB 3=CW 4=FM 5=AM 6=FSK(data-L) 7=CW-R 9=FSK-R
// (data-U).  Maps Lyra's mode tokens onto it.
char CatServer::modeToKenwood(const QString &m) const {
    if (m == QLatin1String("LSB")) return '1';
    if (m == QLatin1String("USB")) return '2';
    if (m == QLatin1String("CWU")) return '3';
    if (m == QLatin1String("FM"))  return '4';
    if (m == QLatin1String("AM") || m == QLatin1String("SAM")
        || m == QLatin1String("DSB"))
        return '5';
    if (m == QLatin1String("DIGL")) return '6';
    if (m == QLatin1String("CWL"))  return '7';
    if (m == QLatin1String("DIGU") || m == QLatin1String("DRM")) return '9';
    return '2';   // SPEC / unknown → USB
}

QString CatServer::kenwoodToMode(int n) const {
    switch (n) {
        case 1: return QStringLiteral("LSB");
        case 2: return QStringLiteral("USB");
        case 3: return QStringLiteral("CWU");
        case 4: return QStringLiteral("FM");
        case 5: return QStringLiteral("AM");
        case 6: return QStringLiteral("DIGL");
        case 7: return QStringLiteral("CWL");
        case 9: return QStringLiteral("DIGU");
        default: return {};
    }
}

// Kenwood IF; transceiver-status reply — 35-byte body (per Thetis
// CATCommands.cs:317-398 + the TS-2000 spec): freq(11) step(4) IT(6)
// RIT(1) XIT(1) memBank(3) TX(1) mode(1) FR-FT(1) scan(1) split(1)
// balance(4).  Lyra fills the fields it knows; RIT/XIT/step/scan/split
// are placeholders (no CAT exposure yet) — clients read freq + TX + mode.
QByteArray CatServer::buildIf() const {
    QByteArray s = "IF";
    s += QByteArray::number(carrierHz()).rightJustified(11, '0');  // P1 freq
    s += "0000";                                                   // P2 step
    s += "+00000";                                                 // P3 IT
    s += "0";                                                      // P4 RIT
    s += "0";                                                      // P5 XIT
    s += "000";                                                    // P6/7 mem
    s += (stream_ && stream_->moxActive()) ? "1" : "0";            // P8 TX
    s += QByteArray(1, modeToKenwood(prefs_ ? prefs_->mode()
                                            : QStringLiteral("USB")));  // P9 mode
    s += "0";                                                      // P10 FR/FT
    s += "0";                                                      // P11 scan
    s += "0";                                                      // P12 split
    s += "0000";                                                   // P13 balance
    s += ";";
    return s;
}

QByteArray CatServer::dispatch(const QByteArray &c) {
    if (c.size() < 2) return "?;";
    const QByteArray id  = c.left(2).toUpper();
    const QByteArray arg = c.mid(2);
    const bool       rd  = arg.isEmpty();   // read = no payload

    if (id == "ID")
        return QByteArray("ID")
             + (model_ == RigModel::Ts2000 ? "019" : "020") + ";";

    if (id == "IF") return buildIf();

    if (id == "FA") {
        if (rd)
            return "FA"
                 + QByteArray::number(carrierHz()).rightJustified(11, '0') + ";";
        bool ok = false;
        const qint64 hz = arg.toLongLong(&ok);
        if (ok && stream_) {
            const qint64 off = engine_ ? engine_->markerOffsetHz() : 0;
            const quint32 target = quint32(std::max<qint64>(0, hz - off));
            // Cross-band while transmitting: ignored; the client's next FA
            // poll reads the unchanged frequency back.
            if (!stream_->txBlocksBandJump(target))
                stream_->setRx1FreqHz(target);
        }
        return {};   // set: no reply
    }

    if (id == "FB") {
        // Single-VFO today: FB read echoes the VFO-A carrier; FB set writes
        // the VFO-B register (used by split-aware clients).
        if (rd)
            return "FB"
                 + QByteArray::number(carrierHz()).rightJustified(11, '0') + ";";
        bool ok = false;
        const qint64 hz = arg.toLongLong(&ok);
        if (ok && stream_) {
            const qint64 off = engine_ ? engine_->markerOffsetHz() : 0;
            const quint32 target = quint32(std::max<qint64>(0, hz - off));
            if (!stream_->txBlocksVfoBJump(target))
                stream_->setVfoBHz(target);
        }
        return {};
    }

    if (id == "MD") {
        if (rd) {
            const char k = modeToKenwood(prefs_ ? prefs_->mode()
                                                : QStringLiteral("USB"));
            return QByteArray("MD") + k + ";";
        }
        const QString lm = kenwoodToMode(arg.toInt());
        if (!lm.isEmpty() && prefs_) prefs_->setMode(lm);
        return {};
    }

    if (id == "FR") return rd ? QByteArray("FR0;") : QByteArray();   // RX = VFO A
    if (id == "FT") return rd ? QByteArray("FT0;") : QByteArray();   // split: stub

    if (id == "PS") {
        if (rd)
            return QByteArray("PS") + ((stream_ && stream_->isRunning()) ? "1"
                                                                         : "0")
                 + ";";
        if (arg.startsWith('1')) emit startRequested();
        else                     emit stopRequested();
        return {};
    }

    if (id == "AI") {
        if (rd) return QByteArray("AI") + QByteArray::number(aiMode_) + ";";
        aiMode_ = std::clamp(arg.toInt(), 0, 4);
        return {};
    }

    // PTT — write-only, no reply.  TX (or TX0/TX1/TX2) = transmit, RX =
    // receive.  CAT-sourced PTT records PttSource::Manual (per the enum doc).
    if (id == "TX") {
        if (stream_) stream_->requestMox(true);
        return {};
    }
    if (id == "RX") {
        if (stream_) stream_->requestMox(false);
        return {};
    }

    return "?;";   // unrecognised — standard Kenwood error reply
}

void CatServer::onRadioChanged() {
    if (aiMode_ > 0 && running())
        broadcast(buildIf());
}

} // namespace lyra::cat

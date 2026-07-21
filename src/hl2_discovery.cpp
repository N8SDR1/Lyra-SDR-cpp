// Lyra — HPSDR Protocol 1 discovery implementation.  See
// hl2_discovery.h for the protocol summary + the operator-bench-
// verified Python reference at ../lyra/protocol/discovery.py.

#include "hl2_discovery.h"

#include <QNetworkInterface>
#include <QSettings>
#include <QtEndian>
#include <QtDebug>
#include <algorithm>

namespace lyra::ipc {

namespace {
QString boardName(int boardId) {
    switch (boardId) {
        case 0:  return QStringLiteral("Atlas");
        case 1:  return QStringLiteral("Hermes");
        case 2:  return QStringLiteral("HermesII");
        case 4:  return QStringLiteral("Angelia");
        case 5:  return QStringLiteral("Orion");
        case 6:  return QStringLiteral("HermesLite");
        case 10: return QStringLiteral("OrionMKII");
        default: return QStringLiteral("Unknown(%1)").arg(boardId);
    }
}

// Protocol-2 board table — reply byte [11].  DIFFERENT numbering from
// the P1 table above (per the openHPSDR P2 discovery spec / p2app
// reference).  Board 1 = Hermes-class is the BrickSDR2's reply, verified
// on N8SDR's unit (MAC 00:1c:c0:a2:22:5c); 10 = Saturn / ANAN G2.
QString boardNameP2(int boardId) {
    switch (boardId) {
        case 0:  return QStringLiteral("Atlas");
        case 1:  return QStringLiteral("Hermes");
        case 2:  return QStringLiteral("HermesII");
        case 3:  return QStringLiteral("Angelia");
        case 4:  return QStringLiteral("Orion");
        case 5:  return QStringLiteral("OrionMKII");
        case 6:  return QStringLiteral("HermesLite");
        case 10: return QStringLiteral("Saturn (ANAN G2)");
        default: return QStringLiteral("P2 board %1").arg(boardId);
    }
}
} // namespace

void HL2Discovery::rememberRadio(const QString &ip, const QString &mac,
                                 const QString &boardName_,
                                 int codeVersion, int betaVersion,
                                 bool busy, int numRxs, int protocol) {
    QSettings s;
    s.beginGroup(QStringLiteral("lastRadio"));
    s.setValue(QStringLiteral("ip"), ip);
    s.setValue(QStringLiteral("mac"), mac);
    s.setValue(QStringLiteral("boardName"), boardName_);
    s.setValue(QStringLiteral("codeVersion"), codeVersion);
    s.setValue(QStringLiteral("betaVersion"), betaVersion);
    s.setValue(QStringLiteral("busy"), busy);
    s.setValue(QStringLiteral("numRxs"), numRxs);
    s.setValue(QStringLiteral("protocol"), protocol);
    s.endGroup();
}

QVariantMap HL2Discovery::savedRadio() const {
    QSettings s;
    s.beginGroup(QStringLiteral("lastRadio"));
    QVariantMap m;
    // Keys match the QML radioModel fields so QML can append directly.
    m[QStringLiteral("ip")]          = s.value(QStringLiteral("ip"));
    m[QStringLiteral("mac")]         = s.value(QStringLiteral("mac"));
    m[QStringLiteral("boardName")]   = s.value(QStringLiteral("boardName"));
    m[QStringLiteral("codeVersion")] = s.value(QStringLiteral("codeVersion"), 0);
    m[QStringLiteral("betaVersion")] = s.value(QStringLiteral("betaVersion"), 0);
    // busy is a LIVE discovery state (another client holds the radio).
    // The remembered entry can't know it without a fresh sweep, so
    // never show stale BUSY — Lyra's own connection is conveyed by the
    // green "connected" border + Close button instead.
    m[QStringLiteral("busy")]        = false;
    m[QStringLiteral("numRxs")]      = s.value(QStringLiteral("numRxs"), 0);
    // Default 1 (P1) for records written before the protocol key existed.
    m[QStringLiteral("protocol")]    = s.value(QStringLiteral("protocol"), 1);
    s.endGroup();
    return m;
}

void HL2Discovery::forgetRadio(const QString &ip) {
    if (ip.isEmpty()) return;
    QSettings s;
    // Clear the remembered record only if it's this IP.
    s.beginGroup(QStringLiteral("lastRadio"));
    const bool savedMatch = (s.value(QStringLiteral("ip")).toString() == ip);
    s.endGroup();
    if (savedMatch) s.remove(QStringLiteral("lastRadio"));
    // Clear the auto-connect IP if it points here, so next launch
    // doesn't reconnect to the radio the operator just removed.
    if (s.value(QStringLiteral("radio/lastIp")).toString() == ip)
        s.remove(QStringLiteral("radio/lastIp"));
}

HL2Discovery::HL2Discovery(QObject *parent) : QObject(parent) {
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout,
            this, &HL2Discovery::onSweepDeadline);

    attemptTimer_.setSingleShot(false);
    connect(&attemptTimer_, &QTimer::timeout, this, [this]() {
        if (attemptsRemaining_-- > 0) {
            sendBroadcastFromAllSockets();
        } else {
            attemptTimer_.stop();
        }
    });

    // Unicast probe(): close the probe socket when its window ends.  If the
    // radio replied, onProbeReadyRead already emitted radioFound; if not, the
    // caller's manual list entry simply stays (Open still connects).
    probeDeadline_.setSingleShot(true);
    connect(&probeDeadline_, &QTimer::timeout, this,
            [this]() {
                if (!probeResolved_) {
                    probeResolved_ = true;
                    emit probeFinished(false, probeIp_);
                }
                probeSock_.reset();
            });
}

HL2Discovery::~HL2Discovery() = default;

QByteArray HL2Discovery::buildDiscoveryPacket() {
    QByteArray pkt(kPacketLen, char{0});
    pkt[0] = static_cast<char>(0xEF);
    pkt[1] = static_cast<char>(0xFE);
    pkt[2] = static_cast<char>(0x02);
    return pkt;
}

QByteArray HL2Discovery::buildDiscoveryPacketP2() {
    // 60 bytes, all zero except byte [4] = 0x02 (discovery command).  The
    // size IS part of the protocol: a P2 radio only acts on a size-60
    // packet on port 1024, and a P1 radio ignores it (wrong magic/size).
    QByteArray pkt(kPacketLenP2, char{0});
    pkt[4] = static_cast<char>(0x02);
    return pkt;
}

QList<HL2Discovery::LocalIf> HL2Discovery::localIPv4Interfaces() const {
    QList<LocalIf> out;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning)) {
            continue;
        }
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            const QHostAddress addr = entry.ip();
            if (addr.protocol() != QAbstractSocket::IPv4Protocol) continue;
            const QString s = addr.toString();
            if (s.startsWith(QStringLiteral("127."))) continue;
            // Link-local / APIPA (169.254.x.x) interfaces are DELIBERATELY
            // INCLUDED here — do NOT re-add a skip.  On a direct PC<->HL2
            // cable with no DHCP server, both ends fall back to 169.254.x.x
            // and the radio is ONLY reachable on that interface; skipping it
            // made direct-connect radios undiscoverable (Add-by-IP was the
            // only workaround — a second user reported exactly this).  The
            // IsUp + IsRunning checks above already drop dead/junk link-local
            // NICs, so this only adds an *active* direct-connect segment.
            // entry.broadcast() is the subnet-directed broadcast (IP|~mask),
            // e.g. 10.10.30.255 — or 169.254.255.255 for a /16 APIPA NIC; it
            // may be null on some adapters, in which case the limited
            // 255.255.255.255 broadcast in sendBroadcastFromAllSockets()
            // still reaches the radio on that segment.
            out.append(LocalIf{addr, entry.broadcast()});
        }
    }
    return out;
}

bool HL2Discovery::parseReply(const QByteArray &data,
                              const QHostAddress &sender,
                              RadioInfo &out) const {
    if (data.size() < 24) return false;
    const auto u = reinterpret_cast<const std::uint8_t*>(data.constData());

    // The sender IP is common to both reply shapes.  QHostAddress::
    // toString() can prefix IPv4 with "::ffff:" on a dual-stack socket —
    // strip it once here.
    QString ip = sender.toString();
    if (ip.startsWith(QStringLiteral("::ffff:"))) ip = ip.mid(7);

    // ---- Protocol 1 (Metis) reply: 0xEFFE magic + status ------------
    if (u[0] == 0xEF && u[1] == 0xFE) {
        const std::uint8_t status = u[2];
        if (status != 0x02 && status != 0x03) return false;

        out.protocol    = 1;
        out.ip          = ip;
        out.mac = QString::asprintf(
            "%02X:%02X:%02X:%02X:%02X:%02X",
            u[3], u[4], u[5], u[6], u[7], u[8]);
        out.codeVersion = u[9];
        out.boardId     = u[10];
        out.boardName   = boardName(out.boardId);
        out.isBusy      = (status == 0x03);

        if (out.boardId == kBoardHL2) {
            out.eeConfig = u[11];
            if (data.size() > 16) {
                out.fixedIpHl2 = QString::asprintf(
                    "%d.%d.%d.%d", u[16], u[15], u[14], u[13]);
            }
            if (data.size() > 19) out.numRxs      = u[19];
            if (data.size() > 21) out.betaVersion = u[21];
        } else {
            if (data.size() > 20) out.numRxs = u[20];
        }
        return true;
    }

    // ---- Protocol 2 (openHPSDR) reply: zero sequence + status -------
    // Distinguished from P1 by the four leading zero bytes (P1 leads with
    // 0xEFFE) and from stray traffic by the size-24 minimum + status byte.
    if (u[0] == 0 && u[1] == 0 && u[2] == 0 && u[3] == 0 &&
        (u[4] == 0x02 || u[4] == 0x03)) {
        out.protocol    = 2;
        out.ip          = ip;
        out.mac = QString::asprintf(
            "%02X:%02X:%02X:%02X:%02X:%02X",
            u[5], u[6], u[7], u[8], u[9], u[10]);
        out.boardId     = u[11];
        out.boardName   = boardNameP2(out.boardId);
        out.codeVersion = u[13];              // FPGA firmware version
        out.isBusy      = (u[4] == 0x03);
        if (data.size() > 20) out.numRxs      = u[20];   // DDC count
        if (data.size() > 23) out.betaVersion = u[23];   // p2app sw version
        return true;
    }

    return false;
}

void HL2Discovery::scan(double timeoutSeconds, int attempts) {
    foundMacs_.clear();
    totalFound_ = 0;
    sockets_.clear();
    socketBroadcast_.clear();
    attemptsRemaining_ = std::max(0, attempts - 1);

    const QList<LocalIf> ifaces = localIPv4Interfaces();
    if (ifaces.isEmpty()) {
        emit logLine(QStringLiteral("ERROR: no usable IPv4 interfaces"));
        emit scanFinished(0);
        return;
    }
    emit logLine(QStringLiteral("Scanning %1 local interface(s)...")
                 .arg(ifaces.size()));

    for (const LocalIf &lif : ifaces) {
        auto sock = std::make_unique<QUdpSocket>(this);
        // SO_REUSEADDR so multiple interface-bound sockets coexist
        // on the same port (the HL2 reply lands on whichever socket
        // its routing table associated with the broadcast).
        if (!sock->bind(lif.ip, 0,
                        QAbstractSocket::ShareAddress |
                        QAbstractSocket::ReuseAddressHint)) {
            emit logLine(QStringLiteral("  bind FAILED on %1: %2")
                         .arg(lif.ip.toString(), sock->errorString()));
            continue;
        }
        connect(sock.get(), &QUdpSocket::readyRead,
                this, &HL2Discovery::onReadyRead);
        emit logLine(QStringLiteral("  socket bound to %1:%2  (subnet bcast %3)")
                     .arg(lif.ip.toString())
                     .arg(sock->localPort())
                     .arg(lif.broadcast.isNull()
                              ? QStringLiteral("n/a")
                              : lif.broadcast.toString()));
        sockets_.push_back(std::move(sock));
        socketBroadcast_.push_back(lif.broadcast);   // index-aligned
    }

    if (sockets_.empty()) {
        emit logLine(QStringLiteral("ERROR: zero sockets bound; aborting"));
        emit scanFinished(0);
        return;
    }

    // First broadcast immediately, then schedule retries at half the
    // total timeout window (so a 2-attempt 1.5s scan retries at 0.75s).
    sendBroadcastFromAllSockets();
    if (attemptsRemaining_ > 0) {
        const int periodMs = std::max(100,
            static_cast<int>(timeoutSeconds * 1000.0 / attempts));
        attemptTimer_.start(periodMs);
    }

    deadline_.start(static_cast<int>(timeoutSeconds * 1000.0));
}

void HL2Discovery::sendBroadcastFromAllSockets() {
    // Probe BOTH protocols every sweep: P1 (63 B, 0xEFFE) for the HL2
    // family, P2 (60 B, cmd byte [4]=0x02) for BrickSDR2 / ANAN G2.  A
    // radio ignores the other protocol's probe, so one sweep covers a
    // mixed bench.
    const QByteArray pktP1 = buildDiscoveryPacket();
    const QByteArray pktP2 = buildDiscoveryPacketP2();
    const QHostAddress limited(QHostAddress::Broadcast);   // 255.255.255.255
    for (std::size_t i = 0; i < sockets_.size(); ++i) {
        auto &sock = sockets_[i];
        // (1) Limited broadcast — reaches radios on the attached segment.
        const qint64 sent = sock->writeDatagram(pktP1, limited, kDiscoveryPort);
        if (sent != pktP1.size()) {
            emit logLine(QStringLiteral("  send via %1 short/failed: %2")
                         .arg(sock->localAddress().toString(),
                              sock->errorString()));
        }
        sock->writeDatagram(pktP2, limited, kDiscoveryPort);
        // (2) Subnet-directed broadcast (e.g. 10.10.30.255) — some NICs /
        // switches / firewall configs drop (1) but pass this (and vice
        // versa); sending both maximizes the chance the probe arrives.
        const QHostAddress &b = (i < socketBroadcast_.size())
                                    ? socketBroadcast_[i] : QHostAddress();
        if (!b.isNull() && b != limited) {
            sock->writeDatagram(pktP1, b, kDiscoveryPort);
            sock->writeDatagram(pktP2, b, kDiscoveryPort);
        }
    }
}

void HL2Discovery::onReadyRead() {
    auto *sock = qobject_cast<QUdpSocket*>(sender());
    if (!sock) return;
    while (sock->hasPendingDatagrams()) {
        QByteArray buf;
        buf.resize(static_cast<int>(sock->pendingDatagramSize()));
        QHostAddress sender;
        quint16 port = 0;
        const qint64 n = sock->readDatagram(buf.data(), buf.size(),
                                            &sender, &port);
        if (n < 0) continue;
        buf.resize(static_cast<int>(n));

        RadioInfo info;
        if (!parseReply(buf, sender, info)) {
            emit logLine(QStringLiteral(
                "  reply from %1: %2 bytes (not HPSDR — ignored)")
                .arg(sender.toString()).arg(buf.size()));
            continue;
        }
        if (foundMacs_.contains(info.mac)) continue;
        foundMacs_.insert(info.mac);
        ++totalFound_;
        emit logLine(QStringLiteral(
            "  FOUND: %1  %2  %3  P%8  gw=v%4.%5  busy=%6  rxs=%7")
            .arg(info.ip, info.mac, info.boardName)
            .arg(info.codeVersion).arg(info.betaVersion)
            .arg(info.isBusy ? QStringLiteral("yes")
                              : QStringLiteral("no"))
            .arg(info.numRxs).arg(info.protocol));
        emit radioFound(info.ip, info.mac, info.boardName,
                        info.codeVersion, info.betaVersion,
                        info.isBusy, info.numRxs, info.protocol);
    }
}

void HL2Discovery::onSweepDeadline() {
    attemptTimer_.stop();
    sockets_.clear();
    socketBroadcast_.clear();
    emit logLine(QStringLiteral("Discovery complete: %1 radio(s) found")
                 .arg(totalFound_));
    emit scanFinished(totalFound_);
}

void HL2Discovery::probe(const QString &ip, double timeoutSeconds) {
    QHostAddress target;
    if (ip.isEmpty() || !target.setAddress(ip) ||
        target.protocol() != QAbstractSocket::IPv4Protocol) {
        emit logLine(QStringLiteral("probe: invalid IPv4 '%1'").arg(ip));
        return;
    }
    // Fresh socket per probe (resetting closes any prior one + drops its
    // readyRead connection).  Bind AnyIPv4:0 so the OS routes the unicast
    // out the correct interface (incl. the default gateway for a radio on
    // another subnet).  The HL2 replies to our source port → lands here.
    probeSock_ = std::make_unique<QUdpSocket>(this);
    if (!probeSock_->bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        emit logLine(QStringLiteral("probe: bind failed: %1")
                     .arg(probeSock_->errorString()));
        probeSock_.reset();
        return;
    }
    connect(probeSock_.get(), &QUdpSocket::readyRead,
            this, &HL2Discovery::onProbeReadyRead);
    probeIp_       = ip;
    probeResolved_ = false;
    // Probe BOTH protocols — a fixed-IP target could be a P1 HL2 or a P2
    // Brick / ANAN G2; whichever it is answers its own format.
    probeSock_->writeDatagram(buildDiscoveryPacket(), target, kDiscoveryPort);
    probeSock_->writeDatagram(buildDiscoveryPacketP2(), target, kDiscoveryPort);
    emit logLine(QStringLiteral("probe: unicast discovery to %1:%2")
                 .arg(ip).arg(kDiscoveryPort));
    probeDeadline_.start(static_cast<int>(timeoutSeconds * 1000.0));
}

void HL2Discovery::onProbeReadyRead() {
    if (!probeSock_) return;
    while (probeSock_->hasPendingDatagrams()) {
        QByteArray buf;
        buf.resize(static_cast<int>(probeSock_->pendingDatagramSize()));
        QHostAddress sender;
        quint16 port = 0;
        const qint64 n = probeSock_->readDatagram(buf.data(), buf.size(),
                                                  &sender, &port);
        if (n < 0) continue;
        buf.resize(static_cast<int>(n));

        RadioInfo info;
        if (!parseReply(buf, sender, info)) continue;
        // No foundMacs_ dedup here (that's sweep bookkeeping) — the UI
        // de-dupes/updates by IP, so re-emitting is harmless.
        emit logLine(QStringLiteral(
            "  PROBE FOUND: %1  %2  %3  P%8  gw=v%4.%5  busy=%6  rxs=%7")
            .arg(info.ip, info.mac, info.boardName)
            .arg(info.codeVersion).arg(info.betaVersion)
            .arg(info.isBusy ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(info.numRxs).arg(info.protocol));
        emit radioFound(info.ip, info.mac, info.boardName,
                        info.codeVersion, info.betaVersion,
                        info.isBusy, info.numRxs, info.protocol);
        // Resolve the probe immediately on the first valid reply (don't
        // wait out the deadline) so a present radio connects snappily.
        if (!probeResolved_) {
            probeResolved_ = true;
            probeDeadline_.stop();
            emit probeFinished(true, probeIp_);
        }
    }
}

} // namespace lyra::ipc

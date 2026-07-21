// Lyra — HPSDR discovery (Protocol 1 + Protocol 2).
//
// Mirrors the verified byte layout from the existing Python
// implementation at ../lyra/protocol/discovery.py (which has been
// running cleanly on N8SDR's real HL2+ for months).  Translated to
// native C++ + QtNetwork.  Runs on its OWN worker QThread — the
// wire path NEVER touches the Qt main thread.
//
// One sweep probes BOTH protocols so a mixed bench (an HL2 on P1 and a
// BrickSDR2 / ANAN G2 on P2) answers in a single pass: each radio
// ignores the other protocol's probe.  The reply SHAPE disambiguates.
//
// Protocol 1 (Metis) — Hermes Lite 2 / 2+, older ANAN:
//   Send: 63-byte UDP packet
//     bytes [0..2] = 0xEF 0xFE 0x02
//     bytes [3..62] = zero padding
//     destination = 255.255.255.255:1024 (broadcast)
//
//   Reply (>= 24 bytes):
//     bytes [0..1] = 0xEF 0xFE (magic — reject anything else)
//     byte  [2]    = status (0x02 = idle, 0x03 = busy, anything
//                            else = not an HPSDR reply)
//     bytes [3..8] = MAC (6 bytes)
//     byte  [9]    = code_version  (gateware major)
//     byte  [10]   = board_id      (6 = HermesLite family — both
//                                   HL2 and HL2+ report the same ID)
//   HL2-specific extras (HL2-fork bytes, present on N8SDR's unit):
//     byte  [11]   = ee_config
//     byte  [12]   = ee_config_reserved
//     bytes [13..16] = fixed_ip (stored REVERSED on the wire)
//     byte  [19]   = num_rxs
//     byte  [21]   = beta_version
//
// Protocol 2 (openHPSDR) — Saturn / ANAN G2, BrickSDR2:
//   Send: 60-byte UDP packet, all zero except byte [4] = 0x02
//     (command).  P1 radios reject the size-60 packet; P2 radios only
//     act on a size-60 packet arriving on port 1024.
//
//   Reply (>= 24 bytes):
//     bytes [0..3] = 0x00 (zero sequence — distinguishes from the P1
//                          0xEFFE magic and from stray traffic)
//     byte  [4]    = status (0x02 = idle, 0x03 = in use)
//     bytes [5..10] = MAC (6 bytes)
//     byte  [11]   = board_id  (P2 numbering, NOT the P1 table:
//                               1 = Hermes-class — the BrickSDR2's
//                               reply, bench-captured on N8SDR's unit;
//                               10 = Saturn / ANAN G2)
//     byte  [13]   = fpga firmware version
//     byte  [20]   = DDC count (num rxs)
//     byte  [23]   = software (p2app) version
//
// Multi-NIC: walks every local IPv4 interface, binds a socket per
// interface, broadcasts from each.  Without this, a host with both
// Ethernet + Wi-Fi silently misses the HL2 on the wrong NIC because
// QUdpSocket on 0.0.0.0 lets the OS pick exactly one route.

#pragma once

#include <QObject>
#include <QString>
#include <QHostAddress>
#include <QUdpSocket>
#include <QTimer>
#include <QList>
#include <QSet>
#include <QVariantMap>
#include <cstdint>
#include <memory>
#include <vector>

namespace lyra::ipc {

struct RadioInfo {
    QString ip;
    QString mac;
    int     boardId      = 0;
    QString boardName;
    int     codeVersion  = 0;
    int     betaVersion  = 0;
    int     numRxs       = 0;
    bool    isBusy       = false;
    int     eeConfig     = 0;
    QString fixedIpHl2;
    // HPSDR protocol the radio answered on: 1 = Metis/P1 (Hermes Lite 2/2+,
    // older ANAN), 2 = openHPSDR Protocol 2 (Saturn/ANAN G2, BrickSDR2).
    // Set by parseReply from the reply shape; carried on radioFound so the
    // connect path can refuse to open a P2 rig on the P1 HL2Stream (the P2
    // receive engine is separate, deferred work).
    int     protocol     = 1;
};

class HL2Discovery : public QObject {
    Q_OBJECT
    // Exposed to QML via QQmlContext::setContextProperty("Discovery",
    // ...) in main.cpp (simpler than QML_ELEMENT registration for
    // Step 1 — and lets the worker live on its own QThread).

public:
    explicit HL2Discovery(QObject *parent = nullptr);
    ~HL2Discovery() override;

    // Radio memory: persist / restore the last opened radio's full
    // record so launch can show it in the Found-radios list.  Done in
    // C++ QSettings (not QML's QtCore Settings, whose plugin isn't in
    // the local deploy) — same store the auto-connect radio/lastIp
    // uses.  savedRadio() returns a QVariantMap with the same keys the
    // radioModel uses (ip/mac/boardName/codeVersion/betaVersion/busy/
    // numRxs); the "ip" key is empty when nothing is remembered.
    Q_INVOKABLE void rememberRadio(const QString &ip, const QString &mac,
                                   const QString &boardName,
                                   int codeVersion, int betaVersion,
                                   bool busy, int numRxs, int protocol = 1);
    Q_INVOKABLE QVariantMap savedRadio() const;

    // Remove a radio from persistence: clears the remembered record
    // (lastRadio group) AND the auto-connect IP (radio/lastIp) when
    // either matches <ip>, so a radio the operator removes from the
    // Hardware list doesn't reappear or silently auto-connect next
    // launch.  No-op for an IP that was never persisted (a transient
    // discovered or manually-added entry).
    Q_INVOKABLE void forgetRadio(const QString &ip);

public slots:
    // Fire one discovery sweep.  Safe to call repeatedly; each
    // sweep opens its own sockets, broadcasts, listens, closes.
    void scan(double timeoutSeconds = 1.5, int attempts = 2);

    // Directed UNICAST discovery probe to ONE radio IP — for a fixed-IP /
    // different-subnet / broadcast-blocked radio the sweep can't reach.
    // Sends the same 0xEFFE 0x02 packet to <ip>:1024; on a reply, emits
    // radioFound with the real board/gw/rx info (so an "Add by IP" entry
    // shows real details instead of "manual").  No reply within the
    // timeout → silent (the caller's manual entry stays; Open still works).
    // Independent of scan() — its own socket, its own deadline.
    void probe(const QString &ip, double timeoutSeconds = 1.0);

signals:
    // Emitted once per UNIQUE radio found (de-duped by MAC).  `protocol`
    // is 1 (Metis/P1) or 2 (openHPSDR P2) — the connect path uses it to
    // avoid opening a P2 rig on the P1 HL2Stream.
    void radioFound(QString ip, QString mac, QString boardName,
                    int codeVersion, int betaVersion,
                    bool busy, int numRxs, int protocol);
    // Emitted when the sweep finishes (timeout reached, all
    // attempts done).  `count` = unique radios found.
    void scanFinished(int count);
    // Emitted when a unicast probe() resolves: found=true the instant a
    // reply arrives (also emits radioFound), found=false on timeout.  Lets
    // the connect logic probe a remembered IP and fall back to a scan when
    // the radio isn't there (DHCP lease changed, moved subnets, powered
    // off) instead of blindly opening a dead IP.
    void probeFinished(bool found, QString ip);
    // Diagnostic log line — fed to the QML view for the operator.
    void logLine(QString line);

private slots:
    void onReadyRead();
    void onSweepDeadline();
    void onProbeReadyRead();   // reply to a unicast probe() (own socket)

private:
    // A usable local IPv4 interface: its address + the subnet-directed
    // broadcast (e.g. 10.10.30.255) Qt computes from IP+netmask.  We
    // broadcast to BOTH this and the limited 255.255.255.255 because some
    // NICs / managed switches / firewall configs pass one but not the other.
    struct LocalIf {
        QHostAddress ip;
        QHostAddress broadcast;   // may be null if the OS didn't supply one
    };
    QList<LocalIf> localIPv4Interfaces() const;
    static QByteArray buildDiscoveryPacket();     // P1 (Metis) — 63 B
    static QByteArray buildDiscoveryPacketP2();   // P2 (openHPSDR) — 60 B
    bool parseReply(const QByteArray &data,
                    const QHostAddress &sender,
                    RadioInfo &out) const;
    void sendBroadcastFromAllSockets();

    // std::vector (not QList) because QList is value-semantic
    // (implicitly shared / copy-on-write) and unique_ptr is
    // move-only — QList tries to copy elements internally.
    std::vector<std::unique_ptr<QUdpSocket>> sockets_;
    // Per-socket subnet-directed broadcast, index-aligned with sockets_.
    std::vector<QHostAddress>           socketBroadcast_;
    QSet<QString>                       foundMacs_;
    QTimer                              deadline_;
    QTimer                              attemptTimer_;
    int                                 attemptsRemaining_ = 0;
    int                                 totalFound_       = 0;

    // Unicast probe() state — separate from the broadcast sweep.
    std::unique_ptr<QUdpSocket>         probeSock_;
    QTimer                              probeDeadline_;
    QString                             probeIp_;            // IP this probe targets
    bool                                probeResolved_ = false;  // probeFinished emitted?

    static constexpr quint16 kDiscoveryPort = 1024;
    static constexpr int     kPacketLen     = 63;   // P1 probe size
    static constexpr int     kPacketLenP2   = 60;   // P2 probe size (exact)
    static constexpr int     kBoardHL2      = 6;
};

} // namespace lyra::ipc

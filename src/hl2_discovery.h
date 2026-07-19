// Lyra — HPSDR discovery: Protocol 1 (Hermes Lite 2 / 2+) plus
// Protocol 2 (Saturn / ANAN G2 family) detection.
//
// P1 mirrors the verified byte layout from the existing Python
// implementation at ../lyra/protocol/discovery.py (which has been
// running cleanly on N8SDR's real HL2+ for months).  Translated to
// native C++ + QtNetwork.  Runs on its OWN worker QThread — the
// wire path NEVER touches the Qt main thread.
//
// Protocol 1 summary:
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
// Protocol 2 summary (detection only for now — the P2 wire layer is
// roadmap work; bench reference = KD4YAL's Saturn p2app,
// sw_projects/P2_app/p2app.c, which is the radio-side implementation
// running on the ANAN G2's Pi):
//   Send: 60-byte UDP packet
//     bytes [0..3] = 0 (sequence)
//     byte  [4]    = 0x02 (discovery command)
//     bytes [5..59] = zero padding
//     destination = same 255.255.255.255:1024 broadcast
//   The two probes coexist in one sweep: a P1 radio checks the
//   0xEFFE magic and ignores the 60-byte P2 packet; p2app processes
//   ONLY size-60 packets on 1024 (p2app.c: "to exclude protocol 1
//   discovery") and so ignores our 63-byte P1 packet.
//
//   Reply (60 bytes; useful bytes per p2app DiscoveryReply):
//     bytes [0..3] = 0 (sequence — never incremented)
//     byte  [4]    = state (0x02 = idle, 0x03 = in use / running)
//     bytes [5..10] = MAC (6 bytes)
//     byte  [11]   = board type (10 = Saturn / ANAN G2)
//     byte  [12]   = protocol version (43 = P2 v4.3)
//     byte  [13]   = firmware (FPGA) version
//     byte  [20]   = DDC count (Saturn advertises 10)
//     byte  [23]   = p2app software version (Saturn extra)
//   P1 vs P2 replies are unambiguous: P1 starts 0xEF 0xFE, a P2
//   reply starts with four zero sequence bytes.
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
    // 1 = HPSDR Protocol 1 (HL2 — openable today), 2 = Protocol 2
    // (Saturn / ANAN G2 — detected + listed, wire layer is roadmap).
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
                                   bool busy, int numRxs);
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
    // Emitted once per UNIQUE radio found (de-duped by MAC).
    // `protocol` = 1 (HL2 / P1) or 2 (Saturn / ANAN G2 / P2 —
    // listed for the operator but not openable until the P2 wire
    // layer lands; consumers gate Open/auto-connect on this).
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
    static QByteArray buildDiscoveryPacket();     // P1: 63 B, 0xEFFE 0x02
    static QByteArray buildDiscoveryPacketP2();   // P2: 60 B, [4]=0x02
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
    static constexpr int     kPacketLen     = 63;   // P1 discovery request
    static constexpr int     kPacketLenP2   = 60;   // P2 discovery request/reply
    static constexpr int     kBoardHL2      = 6;
    static constexpr int     kBoardSaturnP2 = 10;   // P2 board type: Saturn / ANAN G2
};

} // namespace lyra::ipc

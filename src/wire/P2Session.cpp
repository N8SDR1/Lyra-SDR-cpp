// Lyra — HPSDR Protocol 2 control-plane session implementation.
// See P2Session.h for the protocol summary + references.

#include "P2Session.h"

#include <QtEndian>

namespace lyra::wire {

namespace {
inline void wrBeU32(char *p, quint32 v) {
    qToBigEndian<quint32>(v, reinterpret_cast<uchar *>(p));
}
inline quint32 rdBeU32(const char *p) {
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(p));
}
inline quint16 rdBeU16(const char *p) {
    return qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(p));
}

// Saturn master clock.  HP-packet frequencies MUST be DDS phase
// words: the radio's HP handler hardcodes phase-word decode
// (p2app InHighPriority.c:75 SetDDCFrequency(..., true) — the
// general-packet Hz flag is NOT consulted there; bench 2026-07-18:
// sending plain Hz parked the DDC at freq × 122.88e6/2^32 ≈ 2.9 %
// of the dial — 14.1 MHz came out as 403 kHz of static).
// word = Hz × 2^32 / 122.88 MHz, ~0.0286 Hz resolution.
constexpr double kSaturnClockHz = 122'880'000.0;
inline quint32 phaseWord(quint32 hz) {
    return static_cast<quint32>(
        hz * (4294967296.0 / kSaturnClockHz) + 0.5);
}

// ---- Saturn / ANAN G2 front-end words --------------------------------
// Bit layout = Thetis network.h rbpfilter (the 32-bit Alex word whose
// high half goes to HP bytes 1432-33 and low half to 1434-35).
// Saturn uses the OrionMkII/Saturn BPF semantics (console.cs
// setBPF1ForOrionIISaturn) — same wire bits as classic Alex HPF
// masks, band edges per Thetis's BPF1 defaults.
//
// RX halfword bits: 1=13MHz HPF  2=20MHz HPF  3=6M BPF/preamp
//   4=9.5MHz HPF  5=6.5MHz HPF  6=1.5MHz HPF  12=bypass
//   (8/9/10 = XVTR/EXT1/EXT2 inputs, 11 = RX bypass out, 13/14 =
//   Alex attens — all 0: RX comes from the TRX antenna via the TR
//   relay, no ext routing.)
quint16 saturnAlexRxWord(quint32 hz) {
    if (hz <  1'500'000u) return 1u << 12;   // below BPF range → bypass
    if (hz <  6'500'000u) return 1u << 6;
    if (hz <  9'500'000u) return 1u << 5;
    if (hz < 13'000'000u) return 1u << 4;
    if (hz < 20'000'000u) return 1u << 1;
    if (hz < 50'000'000u) return 1u << 2;
    return 1u << 3;                          // 6 m BPF/preamp
}

// TX halfword bits (32-bit word bits 16..31 → u16 0..15):
//   4=30/20 LPF  5=60/40 LPF  6=80 LPF  7=160 LPF  8/9/10=ANT1/2/3
//   11=TR relay (0 = RX)  13=6 LPF  14=12/10 LPF  15=17/15 LPF
// The LPF sits in the shared antenna path, so RX wants the band's
// LPF selected too (Thetis sends it continuously).  Band edges =
// Thetis's Alex LPF defaults.
quint16 saturnAlexTxWord(quint32 hz, int trxAnt) {
    quint16 w = 0;
    if      (hz <  2'000'000u) w |= 1u << 7;    // 160 m LPF
    else if (hz <  4'000'000u) w |= 1u << 6;    // 80 m
    else if (hz <  7'300'000u) w |= 1u << 5;    // 60/40 m
    else if (hz < 14'350'000u) w |= 1u << 4;    // 30/20 m
    else if (hz < 21'450'000u) w |= 1u << 15;   // 17/15 m
    else if (hz < 29'700'000u) w |= 1u << 14;   // 12/10 m
    else                       w |= 1u << 13;   // 6 m
    switch (trxAnt) {
        default:
        case 1: w |= 1u << 8;  break;
        case 2: w |= 1u << 9;  break;
        case 3: w |= 1u << 10; break;
    }
    return w;
}

const P2HardwareProfile kSaturnProfile = {
    "Saturn (ANAN G2)", 10, 10, 2,
    &saturnAlexRxWord, &saturnAlexTxWord,
};
} // namespace

const P2HardwareProfile *p2ProfileForBoard(int boardId) {
    // Saturn (board 10/11) is the only family bench-verified against
    // real hardware.  It is ALSO the correct table for the rest of
    // the classic-Alex-board family (Hermes/HermesII/Angelia/Orion/
    // OrionMKII) — Thetis's own console.cs draws the Saturn/OrionMKII
    // split only on WHICH UI Setup-form fields supply the band-edge
    // thresholds (setBPF1ForOrionIISaturn vs setAlexHPF); the actual
    // bits written (NetworkIO.SetAlexHPFBits/SetAlexLPFBits) are
    // IDENTICAL either way.  So this covers every Alex-equipped P2
    // board with the correct BIT ENCODING; only the hardcoded band-
    // edge THRESHOLDS in saturnAlexRxWord/TxWord are Saturn-typical
    // approximations on non-Saturn hardware, not independently
    // bench-verified there.  Atlas (no Alex board) and anything
    // unrecognized get nullptr rather than a silently-possibly-wrong
    // guess; the caller (P2RxBridge::open) logs a warning when this
    // happens instead of defaulting quietly.
    switch (boardId) {
        case 1: case 2: case 3: case 4: case 5:   // Hermes..OrionMKII
        case 10: case 11:                          // Saturn, SaturnMKII
            return &kSaturnProfile;
        default:
            return nullptr;
    }
}

P2Session::P2Session(QObject *parent)
    // The socket + timer MUST be children of the session: P2RxBridge
    // constructs this object on the main thread and then
    // moveToThread()s it onto the session thread — only the QObject
    // CHILD TREE moves.  As parentless value members they kept
    // main-thread affinity while open()/onHpTick() ran on the session
    // thread — cross-thread QUdpSocket/QTimer use, which crashed the
    // app on the first P2 open (bench 2026-07-18, AV in lyra.exe).
    : QObject(parent), sock_(this), hpTimer_(this) {
    profile_ = p2ProfileForBoard(10);   // Saturn default
    // Bench-sane default: every DDC parked on 20 m until told otherwise
    // (the radio requires a plausible frequency word even for DDCs that
    // will never be enabled — zeros are legal but this keeps DDC0 useful
    // the moment Phase C turns its stream on).
    ddcFreqHz_.fill(14'100'000);

    hpTimer_.setInterval(kHpPeriodMs);
    connect(&hpTimer_, &QTimer::timeout, this, &P2Session::onHpTick);
    connect(&sock_, &QUdpSocket::readyRead, this, &P2Session::onReadyRead);
}

P2Session::~P2Session() {
    if (open_) close();
}

void P2Session::setDdcFrequencyHz(int ddc, quint32 hz) {
    if (ddc < 0 || ddc >= static_cast<int>(ddcFreqHz_.size())) return;
    ddcFreqHz_[static_cast<std::size_t>(ddc)] = hz;
}

void P2Session::enableDdc(int ddc, quint16 rateKhz) {
    if (ddc < 0 || ddc >= kNumDdc) return;
    const auto i = static_cast<std::size_t>(ddc);
    ddcEnabled_[i]    = true;
    ddcRateKhz_[i]    = rateKhz;
    ddcSeqStarted_[i] = false;   // stream (re)starts — reseed seq tracking
    // Push the new config now rather than waiting for the 5 s refresh.
    if (open_)
        sock_.writeDatagram(buildDdcSpecificPacket(), radioAddr_, kPortDdcConfig);
}

void P2Session::disableDdc(int ddc) {
    if (ddc < 0 || ddc >= kNumDdc) return;
    ddcEnabled_[static_cast<std::size_t>(ddc)] = false;
    if (open_)
        sock_.writeDatagram(buildDdcSpecificPacket(), radioAddr_, kPortDdcConfig);
}

QByteArray P2Session::buildGeneralPacket() const {
    QByteArray pkt(kGeneralLen, char{0});
    // [0..3] sequence stays 0 (control packets never increment — see
    // header).  [4] = 0x00 general command.  Stream port fields [5..22]
    // stay 0 = "use HPSDR default ports" (1025/1026/1027/1028/1029/
    // 1035+ — the radio's SetPort(0) fills them in).  Wideband [23..28]
    // stays 0 = off.  [37] option flags: bit 0x08 = frequencies are DDS
    // phase words — matches what the HP fields actually carry (see
    // phaseWord(); the radio's HP handler hardcodes phase-word decode
    // anyway, but declaring it keeps us honest).  No timestamps/VITA-49.
    pkt[37] = char{0x08};
    // [38] bit0 = hardware watchdog ENABLED: if this client dies, the
    // radio unkeys and idles ~1 s later instead of holding a dead
    // session — the RF-safe failure mode.
    pkt[38] = char{0x01};
    // [58] radio options = 0: PA DISABLED (Phase B never transmits),
    // no Apollo.  [59] Alex enable = 0.
    return pkt;
}

QByteArray P2Session::buildDdcSpecificPacket() const {
    // Layout per p2app protocol2_command.c P2DecodeDDCSpecificCommand:
    // [4] = ADC count (Saturn: 2), [5]/[6] = dither/random bits,
    // [7..8] = DDC enable bitmap (LITTLE-endian u16; bytes 9..16 are
    // the P2 wire bitmap for DDC16..79 and MUST stay zero — Saturn
    // rejects the packet otherwise), then per-DDC config at
    // [17 + 6*n]: +0 source (0 = ADC1), +1..2 sample rate in kHz
    // (BE u16, one of 48/96/192/384/768/1536), +5 sample size (24).
    // Interleave sync words [1363+2p] stay zero.  Disabled DDCs may
    // leave rate/size zero (unvalidated while off).
    QByteArray pkt(kDdcSpecificLen, char{0});
    pkt[4] = char{0x02};
    quint16 mask = 0;
    for (int i = 0; i < kNumDdc; ++i) {
        const auto n = static_cast<std::size_t>(i);
        if (!ddcEnabled_[n]) continue;
        mask |= static_cast<quint16>(1u << i);
        const int off = 17 + 6 * i;
        pkt[off]     = char{0x00};                     // source: ADC1
        pkt[off + 1] = static_cast<char>(ddcRateKhz_[n] >> 8);
        pkt[off + 2] = static_cast<char>(ddcRateKhz_[n] & 0xFF);
        pkt[off + 5] = char{24};                       // 24-bit samples
    }
    pkt[7] = static_cast<char>(mask & 0xFF);           // LE enable mask
    pkt[8] = static_cast<char>(mask >> 8);
    return pkt;
}

QByteArray P2Session::buildHighPriorityPacket(bool run) const {
    QByteArray pkt(kHpLen, char{0});
    // [0..3] sequence 0 (control).  [4]: bit0 run; transmit bit1 and
    // PureSignal bit7 stay 0 in Phase B — this session cannot key RF.
    pkt[4] = run ? char{0x01} : char{0x00};
    // [5] CWX off; [6..8] must be zero (hardened p2app rejects the
    // packet otherwise — protocol2_command.c validation).
    for (std::size_t i = 0; i < ddcFreqHz_.size(); ++i)
        wrBeU32(pkt.data() + 9 + i * 4,
                phaseWord(ddcFreqHz_[i]));      // DDC freq, phase word (BE)
    wrBeU32(pkt.data() + 329, phaseWord(ducFreqHz_));  // DUC (TX) phase word
    // [345] drive level 0.  [1396..99] client control word + CAT port 0.
    // [1400..03] xvtr/mute/OC/user outputs 0.
    // [1428..31] Alex1 words 0 (no second Alex; the fw>=12 TXANT
    // branch in p2app then applies Alex0 to both filter registers).
    // [1432..35] Alex0 TX + RX halfwords from the hardware profile —
    // the RF front end (BPF/LPF select + TRX antenna route).  Derived
    // from DDC0's dial every build, so band changes re-filter at the
    // 100 ms cadence.  All-zero here = disconnected front end.
    if (profile_) {
        const quint32 f = ddcFreqHz_[0];
        const quint16 txw = profile_->alexTxWord(f, trxAntenna_);
        const quint16 rxw = profile_->alexRxWord(f);
        pkt[1432] = static_cast<char>(txw >> 8);
        pkt[1433] = static_cast<char>(txw & 0xFF);
        pkt[1434] = static_cast<char>(rxw >> 8);
        pkt[1435] = static_cast<char>(rxw & 0xFF);
    }
    // [1442..43] ADC2/ADC1 step attenuation 0 dB (max legal 31).
    return pkt;
}

void P2Session::setTrxAntenna(int ant) {
    if (ant < 1 || ant > 3) return;
    trxAntenna_ = ant;
}

void P2Session::sendSpeakerAudio(const qint16 *lr, int nframes) {
    if (!open_ || nframes <= 0) return;
    // Stage big-endian L/R pairs; ship a 260 B packet per 64 frames.
    // The speaker stream is a DATA stream: its sequence INCREMENTS
    // (the hardened p2app rejects duplicate/backward speaker seq).
    const int stagedBytes = kSpkrFrames * 4;
    for (int f = 0; f < nframes; ++f) {
        const qint16 l = lr[2 * f], r = lr[2 * f + 1];
        const char quad[4] = {
            static_cast<char>((l >> 8) & 0xFF), static_cast<char>(l & 0xFF),
            static_cast<char>((r >> 8) & 0xFF), static_cast<char>(r & 0xFF),
        };
        spkrStage_.append(quad, 4);
        if (spkrStage_.size() >= stagedBytes) {
            QByteArray pkt(kSpkrPktLen, char{0});
            wrBeU32(pkt.data(), spkrSeq_++);
            memcpy(pkt.data() + 4, spkrStage_.constData(), stagedBytes);
            sock_.writeDatagram(pkt, radioAddr_, kPortSpkrToSdr);
            spkrStage_.remove(0, stagedBytes);
        }
    }
}

void P2Session::open(const QString &ip) {
    if (open_) close();

    QHostAddress target;
    if (ip.isEmpty() || !target.setAddress(ip) ||
        target.protocol() != QAbstractSocket::IPv4Protocol) {
        emit logLine(QStringLiteral("P2: invalid IPv4 '%1'").arg(ip));
        return;
    }

    // ONE socket for the whole session (see header wire model).  Bind
    // an ephemeral port on AnyIPv4 — the radio replies to whatever
    // source address:port the general packet came from, and the OS
    // routes out the right interface.
    if (!sock_.bind(QHostAddress(QHostAddress::AnyIPv4), 0)) {
        emit logLine(QStringLiteral("P2: bind failed: %1")
                     .arg(sock_.errorString()));
        return;
    }

    radioAddr_     = target;
    radioIp_       = ip;
    open_          = true;
    running_       = false;
    lastStatusSeq_ = 0;
    statusCount_   = 0;
    iqFrameCount_  = 0;
    iqSeqErrors_   = 0;
    ddcSeqStarted_.fill(false);
    spkrSeq_       = 0;
    spkrStage_.clear();

    // Handshake per p2app: general packet (claims the controller lease
    // for our source IP + registers the reply address), DDC-specific
    // config (all-disabled for Phase B — also opens the firewall UDP
    // flow toward radio:1025, see kDdcRefreshTicks), then the HP
    // run=1 cadence (StartBitReceived) → radio goes active and its
    // status stream starts.  First HP goes immediately; the timer
    // refreshes it every 100 ms which doubles as the keepalive.
    sock_.writeDatagram(buildGeneralPacket(), radioAddr_, kPortCommand);
    sock_.writeDatagram(buildDdcSpecificPacket(), radioAddr_, kPortDdcConfig);
    sock_.writeDatagram(buildHighPriorityPacket(true), radioAddr_, kPortHpToSdr);
    hpTickCount_ = 0;
    hpTimer_.start();
    emit logLine(QStringLiteral(
        "P2: session opening to %1 (general -> :%2, HP run=1 -> :%3, "
        "local port %4)")
        .arg(ip).arg(kPortCommand).arg(kPortHpToSdr)
        .arg(sock_.localPort()));
}

void P2Session::close() {
    if (!open_) return;
    hpTimer_.stop();
    // run=0 unkeys, releases the controller lease, and stops the
    // radio's outgoing streams.  Sent three times — it's UDP and this
    // is the packet we most want to arrive.
    const QByteArray stop = buildHighPriorityPacket(false);
    for (int i = 0; i < 3; ++i)
        sock_.writeDatagram(stop, radioAddr_, kPortHpToSdr);
    sock_.close();
    open_    = false;
    running_ = false;
    emit logLine(QStringLiteral(
        "P2: session closed (%1 status packets received)").arg(statusCount_));
    emit stopped();
}

void P2Session::onHpTick() {
    if (!open_) return;
    sock_.writeDatagram(buildHighPriorityPacket(true),
                        radioAddr_, kPortHpToSdr);
    // Periodic DDC-specific refresh (see kDdcRefreshTicks rationale).
    if (++hpTickCount_ % kDdcRefreshTicks == 0)
        sock_.writeDatagram(buildDdcSpecificPacket(),
                            radioAddr_, kPortDdcConfig);
}

void P2Session::onReadyRead() {
    while (sock_.hasPendingDatagrams()) {
        QByteArray buf;
        buf.resize(static_cast<int>(sock_.pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        const qint64 n = sock_.readDatagram(buf.data(), buf.size(),
                                            &sender, &senderPort);
        if (n < 0) continue;
        buf.resize(static_cast<int>(n));

        // Only traffic from OUR radio counts (the controller lease is
        // ours, but stray LAN packets can still land on the port).
        if (sender.toIPv4Address() != radioAddr_.toIPv4Address()) continue;

        // Demux by the radio's SOURCE port (the Thetis model).
        if (senderPort == kPortHpFromSdr && buf.size() == kStatusLen) {
            parseStatus(buf);
        } else if (senderPort >= kPortDdcIq0 &&
                   senderPort <  kPortDdcIq0 + kNumDdc &&
                   buf.size() == kIqFrameLen) {
            parseIqFrame(senderPort - kPortDdcIq0, buf);
        }
        // Phase D adds: 1026 mic, wideband.
    }
}

void P2Session::parseIqFrame(int ddc, const QByteArray &d) {
    const char *p = d.constData();
    const quint32 seq  = rdBeU32(p);
    const quint16 bits = rdBeU16(p + 12);
    const quint16 spp  = rdBeU16(p + 14);
    if (bits != 24 || spp != kIqSamplesPerFrame) {
        // Unexpected framing — count it as a stream error and drop.
        ++iqSeqErrors_;
        return;
    }

    const auto n = static_cast<std::size_t>(ddc);
    if (ddcSeqStarted_[n] && seq != ddcSeqNext_[n]) {
        ++iqSeqErrors_;
        emit logLine(QStringLiteral(
            "P2: DDC%1 seq gap — expected %2 got %3")
            .arg(ddc).arg(ddcSeqNext_[n]).arg(seq));
    }
    ddcSeqStarted_[n] = true;
    ddcSeqNext_[n]    = seq + 1;
    ++iqFrameCount_;

    emit iqFrameReceived(ddc, seq,
                         d.mid(kIqHeaderLen, kIqSamplesPerFrame * 6));
}

void P2Session::parseStatus(const QByteArray &d) {
    const char *p = d.constData();
    const quint32 seq = rdBeU32(p);
    const auto u = reinterpret_cast<const std::uint8_t *>(p);

    ++statusCount_;
    lastStatusSeq_ = seq;

    if (!running_) {
        // First status packet = handshake complete, radio active.
        running_ = true;
        emit logLine(QStringLiteral(
            "P2: radio ACTIVE — status stream up from %1:%2")
            .arg(radioIp_).arg(kPortHpFromSdr));
        emit started(radioIp_);
    }

    emit statusReceived(seq,
                        u[4],                 // PTT / key bits
                        u[5],                 // ADC overflow bits
                        rdBeU16(p + 6),       // exciter power (raw)
                        rdBeU16(p + 14),      // forward power (raw)
                        rdBeU16(p + 22),      // reverse power (raw)
                        rdBeU16(p + 49),      // supply volts (raw)
                        rdBeU16(p + 39),      // ADC1 peak (fw >= 27)
                        rdBeU16(p + 41),      // ADC2 peak
                        rdBeU16(p + 57),      // AIN3 — PA volts sense
                        rdBeU16(p + 55),      // AIN4 — PA current sense
                        rdBeU16(p + 37));     // speaker FIFO depth
}

} // namespace lyra::wire

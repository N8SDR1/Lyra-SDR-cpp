// Lyra — HPSDR Protocol 2 control-plane session implementation.
// See P2Session.h for the protocol summary + references.

#include "P2Session.h"

#include "P2TxCmaster.h"   // setP2TxCmasterChannelRunning — run the TXA channel with the DUC transport

#include <QtEndian>
#include <algorithm>
#include <cmath>

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
quint16 saturnAlexRxWord(quint32 hz, P2RxInput input, bool hpfBypass) {
    quint16 w = 0;
    switch (input) {
        case P2RxInput::Bypass: w |= 1u << 10; break;
        case P2RxInput::Ext1:   w |= 1u << 9;  break;
        case P2RxInput::Xvtr:   w |= 1u << 8;  break;
        case P2RxInput::Trx:                         break;
    }
    if (hpfBypass || hz < 1'500'000u) return w | (1u << 12);
    if (hz <  2'100'000u) return w | (1u << 6); // 1.5 MHz HPF
    if (hz <  5'500'000u) return w | (1u << 5); // 6.5 MHz HPF
    if (hz < 11'000'000u) return w | (1u << 4); // 9.5 MHz HPF
    if (hz < 22'000'000u) return w | (1u << 1); // 13 MHz HPF
    if (hz < 35'000'000u) return w | (1u << 2); // 20 MHz HPF
    if (hz <= 61'440'000u) return w | (1u << 3); // 6 m BPF/LNA
    return w | (1u << 12); // outside Thetis's configured BPF1 table
}

// TX halfword bits (32-bit word bits 16..31 → u16 0..15):
//   4=30/20 LPF  5=60/40 LPF  6=80 LPF  7=160 LPF  8/9/10=ANT1/2/3
//   11=TR relay (0 = RX)  13=6 LPF  14=12/10 LPF  15=17/15 LPF
// The LPF sits in the shared antenna path, so RX wants the band's
// LPF selected too (Thetis sends it continuously).  Band edges =
// Thetis's Alex LPF defaults.
quint16 saturnAlexTxWord(quint32 hz, int trxAnt) {
    quint16 w = 0;
    if      (hz <  2'500'001u) w |= 1u << 7;    // 160 m LPF
    else if (hz <  5'000'001u) w |= 1u << 6;    // 80 m
    else if (hz <  8'000'001u) w |= 1u << 5;    // 60/40 m
    else if (hz < 16'500'001u) w |= 1u << 4;    // 30/20 m
    else if (hz < 24'000'001u) w |= 1u << 15;   // 17/15 m
    else if (hz < 35'600'001u) w |= 1u << 14;   // 12/10 m
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
    "ANAN-G2", "Saturn (ANAN G2)", 10, 10, 2,
    &saturnAlexRxWord, &saturnAlexTxWord,
};

// BrickSDR (Hermes-class P2) has NO Alex front end -- its onboard LPF
// self-selects in gateware from the tuned frequency (catalog hasAlex=false).
// So the Alex0 TX+RX halfwords ([1432..1435]) stay zero -- byte-for-byte
// what the bench-confirmed Brick RX already sent while running with
// profile_==nullptr.  This is the Saturn per-band-Alex ladder the Brick
// deliberately avoids; do NOT point these at saturnAlex*Word.
//
// S-T0 = un-grey "Arm P2 TX" only (txHardwareSupported_ = profile!=nullptr).
// It is RF-inert and RX-byte-identical: adcCount=1 matches the pkt[4]
// nullptr-default, and both word functors return 0 like the skipped block.
// S-T1 replaces brickAlexTxWord with the captured fixed TX front-end
// constant (OC 0x04 / the 08 04 01 00 ... relay bytes) -- which only ever
// reaches the wire during TX.
quint16 brickAlexRxWord(quint32, P2RxInput, bool) { return 0; }
quint16 brickAlexTxWord(quint32, int) { return 0; }  // S-T1: captured constant

const P2HardwareProfile kBrickProfile = {
    "BRICK-SDR", "BrickSDR",
    1,   // boardId: bHermes (P2 discovery reply [11]); informational, unconsumed
    1,   // ddcCount: informational, unconsumed (the RX loop uses kNumDdc)
    1,   // adcCount: CONSUMED (pkt[4]) -- Hermes-class single ADC; == RX default
    &brickAlexRxWord, &brickAlexTxWord,
    true,  // fixedTxFrontEnd: assert the captured fixed TX/T-R constant on key
};
} // namespace

const P2HardwareProfile *p2ProfileForModel(const QString &modelKey) {
    if (modelKey.compare(QLatin1String("ANAN-G2"), Qt::CaseInsensitive) == 0 ||
        modelKey.compare(QLatin1String("ANAN-G2-1K"), Qt::CaseInsensitive) == 0)
        return &kSaturnProfile;
    // BRICK-SDR2 = the pre-generalization key; the catalog resolves it to
    // BRICK-SDR, but a saved profile may still carry it here.
    if (modelKey.compare(QLatin1String("BRICK-SDR"), Qt::CaseInsensitive) == 0 ||
        modelKey.compare(QLatin1String("BRICK-SDR2"), Qt::CaseInsensitive) == 0)
        return &kBrickProfile;
    return nullptr;
}

P2Session::P2Session(QObject *parent)
    // The socket + timer MUST be children of the session: P2RxBridge
    // constructs this object on the main thread and then
    // moveToThread()s it onto the session thread — only the QObject
    // CHILD TREE moves.  As parentless value members they kept
    // main-thread affinity while open()/onHpTick() ran on the session
    // thread — cross-thread QUdpSocket/QTimer use, which crashed the
    // app on the first P2 open (bench 2026-07-18, AV in lyra.exe).
    : QObject(parent), sock_(this), hpTimer_(this), txPrimeTimer_(this),
      txWriter_(this), txPump_(this) {
    // Bench-sane default: every DDC parked on 20 m until told otherwise
    // (the radio requires a plausible frequency word even for DDCs that
    // will never be enabled — zeros are legal but this keeps DDC0 useful
    // the moment Phase C turns its stream on).
    ddcFreqHz_.fill(14'100'000);

    hpTimer_.setInterval(kHpPeriodMs);
    connect(&hpTimer_, &QTimer::timeout, this, &P2Session::onHpTick);
    txPrimeTimer_.setInterval(kTxPrimePollMs);
    txPrimeTimer_.setTimerType(Qt::PreciseTimer);
    connect(&txPrimeTimer_, &QTimer::timeout,
            this, &P2Session::onTxPrimeTick);
    connect(&sock_, &QUdpSocket::readyRead, this, &P2Session::onReadyRead);
    // The CMaster producer is routed into this process-lifetime FIFO by
    // P2RxBridge while a P2 radio is open. The writer starts only after
    // radio status and FIFO priming; RF fields remain independently gated.
    txWriter_.setInputFifo(&p2TxInputFifo());
    txWriter_.setPacketSink([this](const QByteArray &packet) {
        if (open_)
            sock_.writeDatagram(packet, radioAddr_, kPortDucIqToSdr);
    });
    txWriter_.setFaultSink([this]() {
        onTxTransportCadenceFault(QStringLiteral(
            "TX-IQ pacing deadline/FIFO fault"));
    });
    txPump_.setFaultSink([this]() {
        onTxTransportCadenceFault(QStringLiteral(
            "CMaster producer deadline/input fault"));
    });
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

void P2Session::setDdcAdc(int ddc, int adc) {
    if (ddc < 0 || ddc >= kNumDdc || adc < 0 || adc > 1) return;
    ddcAdc_[static_cast<std::size_t>(ddc)] = static_cast<quint8>(adc);
    if (open_)
        sock_.writeDatagram(buildDdcSpecificPacket(), radioAddr_, kPortDdcConfig);
}

QByteArray P2Session::buildGeneralPacket() const {
    QByteArray pkt(kGeneralLen, char{0});
    const auto tx = P2TxSafetyGate::evaluate(txIntent_, txSafety_);
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
    // [58] bit0 is PA enable. The safety gate returns false unless every
    // live prerequisite is healthy and the operator bench arm is set.
    pkt[58] = tx.paEnabled ? char{0x01} : char{0x00};
    // No Apollo. [59] Alex enable remains 0.
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
    pkt[4] = static_cast<char>(profile_ ? profile_->adcCount : 1);
    quint16 mask = 0;
    for (int i = 0; i < kNumDdc; ++i) {
        const auto n = static_cast<std::size_t>(i);
        if (!ddcEnabled_[n]) continue;
        mask |= static_cast<quint16>(1u << i);
        const int off = 17 + 6 * i;
        pkt[off]     = static_cast<char>(ddcAdc_[n]); // source: ADC1/ADC2
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
    const auto tx = P2TxSafetyGate::evaluate(txIntent_, txSafety_);
    // [0..3] sequence 0 (control). [4]: bit0 run, bit1 gated transmit;
    // PureSignal bit7 remains off until its separate validation phase.
    const bool transmit = run && tx.transmit;
    pkt[4] = static_cast<char>((run ? 0x01 : 0x00) |
                               (transmit ? 0x02 : 0x00));
    // [5] CWX off; [6..8] must be zero (hardened p2app rejects the
    // packet otherwise — protocol2_command.c validation).
    for (std::size_t i = 0; i < ddcFreqHz_.size(); ++i)
        wrBeU32(pkt.data() + 9 + i * 4,
                phaseWord(ddcFreqHz_[i]));      // DDC freq, phase word (BE)
    wrBeU32(pkt.data() + 329, phaseWord(ducFreqHz_));  // DUC (TX) phase word
    pkt[345] = static_cast<char>(tx.drive);
    // [1396..99] client control word + CAT port 0.
    // [1400..03] xvtr/mute/OC/user outputs 0.
    // [1428..31] Alex1 words 0 (no second Alex; the fw>=12 TXANT
    // branch in p2app then applies Alex0 to both filter registers).
    // [1432..35] Alex0 TX + RX halfwords from the hardware profile —
    // the RF front end (BPF/LPF select + TRX antenna route).  Derived
    // from the independent DUC/DDC carriers every build, so band changes
    // re-filter at the 100 ms cadence. All-zero here = disconnected front
    // end.
    if (profile_) {
        const quint16 txw =
            profile_->alexTxWord(ducFreqHz_, trxAntenna_);
        const quint16 rxw =
            profile_->alexRxWord(ddcFreqHz_[0], rxInput_, hpfBypass_);
        pkt[1432] = static_cast<char>(txw >> 8);
        pkt[1433] = static_cast<char>(txw & 0xFF);
        pkt[1434] = static_cast<char>(rxw >> 8);
        pkt[1435] = static_cast<char>(rxw & 0xFF);
    }
    // S-T1 BrickSDR fixed TX front-end + T/R assertion.  Band-independent
    // constant captured verbatim from a working reference->Brick P2 keydown
    // (SDRProject/brick_p2_hp_diff.log, 2026-08-01): OC[1402]=0x04, Alex1
    // [1428..1430]=08 04 01, Alex0 TX halfword [1432..1433]=08 04.  The
    // Brick's onboard LPFs self-select in gateware from the DUC carrier;
    // this is the T/R + enable the radio needs to actually key.  On the wire
    // ONLY while transmitting (overrides the all-zero profile words above);
    // every byte returns to zero on unkey, so RX stays byte-identical.
    if (transmit && profile_ && profile_->fixedTxFrontEnd) {
        pkt[1402] = char{0x04};
        pkt[1428] = char{0x08};
        pkt[1429] = char{0x04};
        pkt[1430] = char{0x01};
        pkt[1432] = char{0x08};
        pkt[1433] = char{0x04};
    }
    pkt[1442] = static_cast<char>(adcAttenuation_[1]); // ADC2 ATT
    pkt[1443] = static_cast<char>(adcAttenuation_[0]); // ADC1 ATT
    return pkt;
}

void P2Session::setTrxAntenna(int ant) {
    if (ant < 1 || ant > 3) return;
    trxAntenna_ = ant;
}

void P2Session::setAdcAttenuation(int adc, int db) {
    if (adc < 0 || adc >= static_cast<int>(adcAttenuation_.size())) return;
    adcAttenuation_[static_cast<std::size_t>(adc)] =
        static_cast<quint8>(std::clamp(db, 0, 31));
}

void P2Session::setTxOperatorArmed(bool armed) {
    if (!armed)
        txIntent_ = {};
    txSafety_.operatorArmed = armed;
    if (open_)
        applyTxControlNow();
    emitTxState(armed
        ? QStringLiteral("Bench interlock armed")
        : QStringLiteral("Bench interlock disarmed"));
    emit logLine(armed
        ? QStringLiteral(
              "P2 TX: bench interlock ARMED; PA enable + MOX/PTT + "
              "non-zero limited drive can now request RF")
        : QStringLiteral(
              "P2 TX: bench interlock disarmed; transmit/PA/drive forced off"));
}

void P2Session::setTxDriveCeiling(int ceilingByte) {
    txSafety_.driveCeiling =
        static_cast<std::uint8_t>(std::clamp(ceilingByte, 0, 255));
    if (open_)
        applyTxControlNow();
}

void P2Session::setTransmitIntent(bool on, bool paRequested, int drive) {
    // RX status is only ~5 Hz and can pause during harmless Windows/Qt
    // scheduling stalls. Do not strand the receive-only transport for
    // that. A key request, however, requires fresh telemetry at the
    // instant RF is requested; stale status becomes a latched TX fault.
    if (on && (!statusAge_.isValid() || statusAge_.elapsed() > 1000)) {
        txSafety_.telemetryHealthy = false;
        latchTxFault(QStringLiteral(
            "MOX/PTT rejected because status telemetry is stale"));
        return;
    }
    if (on) {
        txIntent_.transmitRequested = true;
        txIntent_.paRequested = paRequested;
        txIntent_.drive = std::clamp(drive, 0, 255);
    } else {
        txIntent_ = {};
    }

    txSafety_.transportRunning = txWriter_.isRunning();
    const auto effective = P2TxSafetyGate::evaluate(txIntent_, txSafety_);
    if (open_)
        applyTxControlNow();
    if (on && !effective.transmit) {
        emit logLine(QStringLiteral(
            "P2 TX: MOX/PTT blocked by safety gate "
            "(arm=%1 session=%2 IQ=%3 telemetry=%4 watchdog=%5 fault=%6)")
            .arg(txSafety_.operatorArmed)
            .arg(txSafety_.sessionRunning)
            .arg(txSafety_.iqPrimed)
            .arg(txSafety_.telemetryHealthy)
            .arg(txSafety_.watchdogEnabled)
            .arg(txSafety_.faultLatched));
    }
    emitTxState(on && !effective.transmit
                    ? QStringLiteral("MOX/PTT blocked by safety gate")
                    : (on ? QStringLiteral("Transmit active")
                          : QStringLiteral("Receive")));
}

void P2Session::applyTxControlNow() {
    if (!open_)
        return;
    // Authorise the wire transmit/PA/drive bytes against the LIVE transport
    // state, never a stale prime latch. buildGeneral/HighPriority below also
    // re-evaluate the gate, so refreshing here covers them too.
    txSafety_.transportRunning = txWriter_.isRunning();
    const bool transmitting =
        P2TxSafetyGate::evaluate(txIntent_, txSafety_).transmit;

    // Keydown: authorize the PA in General before raising HP transmit.
    // Keyup/fault: clear HP transmit first, then remove PA authorization.
    // The 100 ms HP cadence reinforces the resulting state afterward.
    if (transmitting) {
        sock_.writeDatagram(buildGeneralPacket(), radioAddr_, kPortCommand);
        sock_.writeDatagram(buildHighPriorityPacket(true),
                            radioAddr_, kPortHpToSdr);
    } else {
        sock_.writeDatagram(buildHighPriorityPacket(true),
                            radioAddr_, kPortHpToSdr);
        sock_.writeDatagram(buildGeneralPacket(), radioAddr_, kPortCommand);
    }
}

void P2Session::emitTxState(const QString &detail) {
    if (!detail.isEmpty())
        txStateDetail_ = detail;
    txSafety_.transportRunning = txWriter_.isRunning();
    const auto effective = P2TxSafetyGate::evaluate(txIntent_, txSafety_);
    emit txStateChanged(
        txWriter_.isRunning() && txSafety_.iqPrimed &&
            !txSafety_.faultLatched,
        txSafety_.operatorArmed, effective.transmit, effective.paEnabled,
        effective.drive, txSafety_.faultLatched, txStateDetail_);
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
    warnedNoIq_    = false;
    ddcSeqStarted_.fill(false);
    spkrSeq_       = 0;
    spkrStage_.clear();
    // Mic FIFO + sequence trackers: a reconnect (2nd+ open in one process)
    // must not carry a prior session's fill level, primed cushion, or stale
    // samples -- otherwise the first pump tick could drain leftover mic audio
    // onto the air. Zeroing head/count and clearing primed makes the ring
    // re-prime from fresh packets; the seq trackers reset so the first mic
    // packet doesn't log a spurious seqErr.
    micFifoHead_   = 0;
    micFifoCount_  = 0;
    micPrimed_     = false;
    micSeqStarted_ = false;
    micSeqNext_    = 0;
    micSeqErrors_  = 0;
    micPktCount_   = 0;
    micPeak_       = 0.0;
    micRateTimer_.invalidate();
    lastMicPkt_.invalidate();
    warnedNoMic_   = false;
    stopTxTransport();
    txIntent_ = {};
    txSafety_ = {};
    txStateDetail_ = QStringLiteral("Waiting for radio status");
    statusAge_.invalidate();
    emitTxState();

    // Handshake per p2app: general packet (claims the controller lease
    // for our source IP + registers the reply address), safe DUC-specific
    // config (CW off, 192 kHz, maximum ADC attenuation), DDC-specific
    // config (also opens the firewall UDP
    // flow toward radio:1025, see kDdcRefreshTicks), then the HP
    // run=1 cadence (StartBitReceived) → radio goes active and its
    // status stream starts.  First HP goes immediately; the timer
    // refreshes it every 100 ms which doubles as the keepalive.
    sock_.writeDatagram(buildGeneralPacket(), radioAddr_, kPortCommand);
    // A prior unclean exit (force-kill) leaves the radio still streaming to
    // the now-dead session.  Now that the general packet has (re)claimed the
    // controller lease for our new source port, send an explicit run=0 so the
    // radio halts any lingering stream before we (re)start it fresh below —
    // this shrinks the stale-IQ flood the run() gate in parseIqFrame drops.
    // On a clean start the radio isn't streaming yet, so this is a no-op.
    sock_.writeDatagram(buildHighPriorityPacket(false), radioAddr_,
                        kPortHpToSdr);
    sock_.writeDatagram(P2TxPackets::encodeDucSpecific(ducConfig_),
                        radioAddr_, kPortDucConfig);
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
    stopTxTransport();
    txIntent_ = {};
    txSafety_ = {};
    txStateDetail_ = QStringLiteral("Disconnected");
    emitTxState();
    // run=0 unkeys, releases the controller lease, and stops the
    // radio's outgoing streams.  Sent three times — it's UDP and this
    // is the packet we most want to arrive.
    const QByteArray stop = buildHighPriorityPacket(false);
    for (int i = 0; i < 3; ++i)
        sock_.writeDatagram(stop, radioAddr_, kPortHpToSdr);
    // PA authorization lives in the General packet, not in HP Run. Send
    // an explicit safe General after HP Run=0 so a close cannot leave the
    // radio's PA-enable bit latched from the previous keyed state. This is
    // authoritative even when P2RxBridge closes before HL2Stream's delayed
    // MOX-release signal reaches the session thread.
    const QByteArray safeGeneral = buildGeneralPacket();
    for (int i = 0; i < 3; ++i)
        sock_.writeDatagram(safeGeneral, radioAddr_, kPortCommand);
    sock_.close();
    open_    = false;
    running_ = false;
    emit logLine(QStringLiteral(
        "P2: session closed (%1 status packets received)").arg(statusCount_));
    emit stopped();
}

void P2Session::onHpTick() {
    if (!open_) return;
    // Once keying is requested the radio reports status at ~1 kHz, and
    // losing it is a real TX safety failure. In RX, status is ~5 Hz and
    // an event-loop/firewall pause must not permanently fault an otherwise
    // healthy continuous port-1029 transport.
    if (txIntent_.transmitRequested && statusAge_.isValid() &&
        statusAge_.elapsed() > 1000 && !txSafety_.faultLatched) {
        txSafety_.telemetryHealthy = false;
        latchTxFault(QStringLiteral("status telemetry timeout"));
    }
    // The periodic HP packet re-derives transmit/PA/drive from the gate; keep
    // the live transport-running input fresh so a stopped writer can't be
    // reinforced into an authorised-but-dead keyed state.
    txSafety_.transportRunning = txWriter_.isRunning();
    sock_.writeDatagram(buildHighPriorityPacket(true),
                        radioAddr_, kPortHpToSdr);
    // Periodic DDC-specific refresh (see kDdcRefreshTicks rationale).
    if (++hpTickCount_ % kDdcRefreshTicks == 0)
        sock_.writeDatagram(buildDdcSpecificPacket(),
                            radioAddr_, kPortDdcConfig);

    // Low-power-bench diagnostic (~1/s while keyed): the actual wire drive
    // byte (pkt[345], 0-255) + the live DUC-IQ peak (~1.0 = full-scale).
    // Both maxed but low RF ⇒ the shortfall is radio-side, not Lyra's
    // drive/amplitude.  Also mirrored on the "P2 TX:" status line.
    if (txIntent_.transmitRequested && (hpTickCount_ % 10 == 0)) {
        const auto eff = P2TxSafetyGate::evaluate(txIntent_, txSafety_);
        qInfo("[p2tx] keyed: drive byte=%d/255 (HP pkt[345])  DUC-IQ peak=%.3f "
              "(~1.0=full-scale)  pa=%d",
              static_cast<int>(eff.drive),
              lyra::wire::p2TxCmasterLastPeak(),
              eff.paEnabled ? 1 : 0);
    }

    // Firewall-blocked-RX self-diagnosis (resolved 2026-08-26).  If the
    // control link is healthy (status flowing) but a DDC is enabled and
    // NOT ONE IQ frame has arrived, the cause is almost always Windows
    // Firewall dropping the radio's unsolicited RX-IQ (source port 1035+,
    // a port the host never sends to, so no stateful return opens for it).
    // Small status(1025)/mic(1026) get through because the host DOES send
    // to those ports.  Fires once so a "dead" panadapter says WHY.
    if (!warnedNoIq_ && running_ && iqFrameCount_ == 0 &&
        statusCount_ >= 25) {
        bool anyDdc = false;
        for (bool en : ddcEnabled_) if (en) { anyDdc = true; break; }
        if (anyDdc) {
            warnedNoIq_ = true;
            emit logLine(QStringLiteral(
                "P2: control link healthy but ZERO RX-IQ after %1 status "
                "packets — this is almost always WINDOWS FIREWALL blocking "
                "inbound UDP for this Lyra build. The radio's IQ arrives on "
                "an unsolicited port (1035+). Fix (admin PowerShell): "
                "New-NetFirewallRule -DisplayName 'Lyra SDR (UDP In)' "
                "-Direction Inbound -Program '<this lyra.exe>' -Protocol UDP "
                "-Action Allow -Profile Any").arg(statusCount_));
        }
    }

    // Jack-less-rig hint (fires once).  Keyed, but no mic packet has arrived
    // from the radio recently — a rig with a working mic jack streams mic
    // continuously while transmitting, so this means there is no usable mic
    // jack (or its mic path is dead) and "Mic In" is sending silence.  Gated
    // on transmitRequested + a stale/absent last-packet, so a jack-equipped
    // rig (mic present during TX) never trips it.  Worded for voice — a CW /
    // TUN / digital-via-VAC/TCI op can ignore the one line.
    if (!warnedNoMic_ && running_ && txIntent_.transmitRequested &&
        (!lastMicPkt_.isValid() || lastMicPkt_.elapsed() > 1500)) {
        warnedNoMic_ = true;
        emit logLine(QStringLiteral(
            "P2 TX: no mic audio is arriving from the radio. For a VOICE mode "
            "on a rig without a usable mic jack, set Settings → TX → "
            "Mic source to PC Soundcard (VAC1) or TCI (digital modes)."));
    }
}

void P2Session::startTxTransportRxState() {
    if (!open_ || !running_ || txSafety_.faultLatched ||
        txPump_.isRunning() || txWriter_.isRunning())
        return;
    if (!txPump_.hasInputSink()) {
        emit logLine(QStringLiteral(
            "P2 TX: no CMaster producer attached; port 1029 remains stopped "
            "and RF controls remain safe"));
        // Surface on-screen (the file log is dead): the producer seam never
        // attached, so nothing can ever prime.
        emitTxState(QStringLiteral("no producer seam — port 1029 stopped"));
        return;
    }

    auto &fifo = p2TxInputFifo();
    fifo.reset();
    txSafety_.iqPrimed = false;
    emitTxState(QStringLiteral("TX transport stopped"));
    // Arm the shared WDSP TXA channel BEFORE the pump feeds its first
    // block.  P2TxPump::start() SYNCHRONOUSLY seeds block 0 (its input
    // sink calls Inbound(inid(1,0),…), which releases stream 1's
    // Sem_BuffReady and wakes the high-priority cm_main(1) pump →
    // xcmaster(1) → fexchange0(chid(1,0),…)).  create_xmtr opens that
    // channel OPEN-but-NOT-STARTED (state=0) with block=1 ("block until
    // output available").  If the channel is still state=0 when that
    // first fexchange0 fires, the block-until-output wait on a stopped
    // channel never completes → the cm pump thread stalls → the producer
    // FIFO never fills → priming below never completes → port 1029 never
    // streams (the "DUC FIFO 0 samples" symptom, with no fault latched
    // because the pump's single seed returned true).  Arm first so the
    // channel's DSP worker is running before the seed lands.  RF stays
    // gated by the wire MOX bit (buildHighPriorityPacket), so arming in
    // RX state only streams zero-valued IQ — no on-air output.
    setP2TxCmasterChannelRunning(true);
    txPump_.start();
    if (!txPump_.isRunning()) {
        setP2TxCmasterChannelRunning(false);   // undo the arm on a failed start
        latchTxFault(QStringLiteral("CMaster producer failed to start"));
        return;
    }
    txPrimeTimer_.start();
    onTxPrimeTick();
    emit logLine(QStringLiteral(
        "P2 TX: priming port 1029 in RX state (transmit=0, PA=off, "
        "drive=0)"));
    qInfo("[p2tx] transport start: TXA channel chid(1,0) ARMED before the "
          "pump seed; priming producer FIFO toward the prime target");
}

void P2Session::stopTxTransport() {
    txPrimeTimer_.stop();
    // Stop the pump (no more input) BEFORE stopping the TXA channel, so the
    // non-blocking channel stop can't race the pump feeding a stopped chain.
    txPump_.stop();
    setP2TxCmasterChannelRunning(false);
    txWriter_.stop();
    txSafety_.iqPrimed = false;
}

void P2Session::restartTxTransportRxState() {
    // A controlled RX sample-rate rebuild can stall the event loop longer
    // than the TX transport's deadline. It is safe to clear/re-prime only
    // while no transmit intent exists; real TX faults stay latched.
    if (!open_ || !running_ || txIntent_.transmitRequested)
        return;
    stopTxTransport();
    txSafety_.faultLatched = false;
    txStateDetail_ = QStringLiteral("Re-priming TX transport");
    startTxTransportRxState();
}

void P2Session::latchTxFault(const QString &reason) {
    stopTxTransport();
    txIntent_ = {};
    txSafety_.faultLatched = true;
    txStateDetail_ = reason;
    // Push an immediate fail-closed HP command when a live session faults;
    // the periodic HP timer continues to reinforce the same safe state.
    if (open_)
        applyTxControlNow();
    emitTxState();
    emit logLine(QStringLiteral(
        "P2 TX: %1; transport stopped and RF controls forced safe")
        .arg(reason));
}

void P2Session::onTxTransportCadenceFault(const QString &reason) {
    // Keyed: a cadence/FIFO miss can produce a stuck or garbage carrier, so
    // this is a hard safety event — stop, force RF safe, latch, unkey.
    if (txIntent_.transmitRequested) {
        latchTxFault(reason);
        return;
    }
    // RX-idle: the P2 TX stream is RF-inert (transmit=0, PA=off, drive=0).
    // A transient host stall here must recover in place, never latch and
    // lock out the next key-up. The faulting writer/pump has already stopped
    // itself; re-prime once this call stack unwinds (deferred so we don't
    // restart timers from inside the faulting timer callback).
    if (!open_ || !running_)
        return;
    // Tear the transport down NOW (clears iqPrimed) so the safety gate refuses
    // any key-up that lands before the deferred re-prime runs; the re-prime is
    // deferred only to avoid restarting timers from inside the faulting timer
    // callback. Mirrors latchTxFault's synchronous stopTxTransport in the
    // keyed branch (same fault-sink context, proven safe).
    stopTxTransport();
    emit logLine(QStringLiteral(
        "P2 TX: %1 while RX-idle (RF-inert); re-priming transport")
        .arg(reason));
    QTimer::singleShot(0, this, [this]() { restartTxTransportRxState(); });
}

void P2Session::onTxPrimeTick() {
    if (!open_ || !running_ || txSafety_.faultLatched) {
        txPrimeTimer_.stop();
        return;
    }

    auto &fifo = p2TxInputFifo();
    if (fifo.overflowed()) {
        latchTxFault(QStringLiteral("producer FIFO overflow during priming"));
        return;
    }
    if (fifo.size() < kTxPrimeSamples) {
        // Producer-side priming progress, surfaced ON-SCREEN (the file log
        // is dead — see reference_lyra_cpp_dead_logfile).  This is the real
        // Stage-1a signal: the "DUC FIFO N samples" number in the same line
        // is a downstream RADIO-reported field, but this "prod FIFO x/y"
        // is Lyra's own producer ring off fexchange0.  If it stays pinned
        // at 0, the cm pump / fexchange0 is not producing (arm-before-feed
        // did not take, or a deeper stall); if it climbs, the producer path
        // works and any remaining "DUC 0" is the radio not buffering TX-IQ
        // in RX state.  Throttled so the poll cadence doesn't spam signals.
        static int primeEmitThrottle = 0;
        if ((primeEmitThrottle++ % 8) == 0)
            emitTxState(QStringLiteral("priming (prod FIFO %1/%2)")
                            .arg(static_cast<qulonglong>(fifo.size()))
                            .arg(static_cast<qulonglong>(kTxPrimeSamples)));
        return;
    }

    txPrimeTimer_.stop();
    txWriter_.startFromInput();
    if (!txWriter_.isRunning()) {
        latchTxFault(QStringLiteral("TX-IQ writer failed to start"));
        return;
    }
    txSafety_.iqPrimed = true;
    // On-screen (file log is dead): producer primed + writer streaming to
    // 1029.  If the operator now sees this but "DUC FIFO" stays 0, the
    // break is the radio not buffering TX-IQ in RX state — not the producer.
    emitTxState(QStringLiteral("streaming 1029 (prod FIFO %1, RF disarmed)")
                    .arg(static_cast<qulonglong>(p2TxInputFifo().size())));
    emit logLine(QStringLiteral(
        "P2 TX: port 1029 streaming in RX state (seq=%1, FIFO=%2 "
        "samples; transmit=0, PA=off, drive=0)")
        .arg(txWriter_.nextSequence())
        .arg(p2TxInputFifo().size()));
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
        } else if (senderPort == kPortMicFromSdr &&
                   buf.size() == kMicPktLen) {
            parseMic(buf);
        }
        // Phase D adds: wideband.
    }
}

void P2Session::parseMic(const QByteArray &d) {
    const char *p = d.constData();
    const quint32 seq = rdBeU32(p);
    // 64 big-endian signed 16-bit mono samples follow the 4-byte sequence.
    const auto *s = reinterpret_cast<const std::uint8_t *>(p + 4);
    // Push this datagram's 64 samples onto the tail of the elastic FIFO.
    // Overrun (a burst outran the pump) drops the oldest block so latency
    // stays bounded; the pump drains from the head on its own cadence.
    const int tail = (micFifoHead_ + micFifoCount_) % kMicFifoCap;
    auto &block = micFifo_[static_cast<std::size_t>(tail)];
    double peak = 0.0;
    for (int i = 0; i < kMicFrames; ++i) {
        const auto raw = static_cast<qint16>(
            (static_cast<quint16>(s[2 * i]) << 8) | s[2 * i + 1]);
        const double v = raw * (1.0 / 32768.0);
        peak = std::max(peak, std::abs(v));
        // {I = mic, Q = 0} — the modulator's real-input convention.
        block[static_cast<std::size_t>(2 * i)]     = v;
        block[static_cast<std::size_t>(2 * i + 1)] = 0.0;
    }
    if (micFifoCount_ < kMicFifoCap) {
        ++micFifoCount_;
    } else {
        // Full: the block we just wrote overwrote the oldest slot, so step
        // the head past it too (drop-oldest, keeping the freshest audio).
        micFifoHead_ = (micFifoHead_ + 1) % kMicFifoCap;
    }

    if (micSeqStarted_ && seq != micSeqNext_)
        ++micSeqErrors_;
    micSeqStarted_ = true;
    micSeqNext_    = seq + 1;

    ++micPktCount_;
    micPeak_ = std::max(micPeak_, peak);
    lastMicPkt_.restart();  // for the jack-less "no mic input" hint in onHpTick

    if (!micRateTimer_.isValid())
        micRateTimer_.start();
    if (micRateTimer_.elapsed() >= 1000) {
        emit logLine(QStringLiteral(
            "P2 mic: %1 pkt/s  peak=%2  seqErr=%3")
            .arg(micPktCount_)
            .arg(micPeak_, 0, 'f', 3)
            .arg(micSeqErrors_));
        micPktCount_ = 0;
        micPeak_     = 0.0;
        micRateTimer_.restart();
    }
}

bool P2Session::feedTxProducer(const double *pumpZeros, int n) {
    static_assert(2 * kMicFrames == 128,
                  "micFifo_ block literal size (2*64) must equal 2*kMicFrames");
    if (!txProducerTerminal_)
        return false;
    // Live radio mic drives the modulator when a fresh block is available;
    // otherwise the pump's zero block keeps the 48 kHz cadence (initial
    // prime before the mic stream starts, and gap-fill if it slips). RF stays
    // MOX-gated regardless — feeding mic in RX only shapes inert DUC-IQ.
    //
    // Prime once the cushion fills, then drain a block per tick. Underrun
    // (the pump briefly outran the mic) feeds zeros and re-arms priming so
    // the next drain waits for the cushion to rebuild rather than chattering
    // one block ahead of the producer. Steady state sits near the cushion
    // depth with only the small ppm drift between the two 48 kHz clocks.
    if (!micPrimed_ && micFifoCount_ >= kMicPrimeBlocks) {
        micPrimed_ = true;
    }
    if (micPrimed_ && micFifoCount_ > 0) {
        const double *block = micFifo_[static_cast<std::size_t>(micFifoHead_)].data();
        micFifoHead_ = (micFifoHead_ + 1) % kMicFifoCap;
        --micFifoCount_;
        return txProducerTerminal_(block, kMicFrames);
    }
    // Underrun (or not yet primed): keep the 48 kHz cadence with zeros.
    micPrimed_ = false;
    return txProducerTerminal_(pumpZeros, n);
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

    // Drop IQ that arrives BEFORE this session's status handshake completes
    // (running_ is set by parseStatus on the radio's first status packet for
    // THIS session, and cleared in open()).  A prior unclean exit (force-kill)
    // leaves the radio still streaming stale frames to the dead session; if
    // those are fed into a not-yet-confirmed RX pipeline they overrun WDSP's
    // buffers — the "arm-before-feed" heap corruption seen on a restart after
    // a kill.  Once the new session is confirmed active we accept IQ normally.
    // On a clean start the radio sends status before IQ, so this drops nothing.
    if (!running_) {
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
    statusAge_.restart();
    txSafety_.telemetryHealthy = true;

    if (!running_) {
        // First status packet = handshake complete, radio active.
        running_ = true;
        txSafety_.sessionRunning = true;
        emit logLine(QStringLiteral(
            "P2: radio ACTIVE — status stream up from %1:%2")
            .arg(radioIp_).arg(kPortHpFromSdr));
        emit started(radioIp_);
        startTxTransportRxState();
    }

    // Saturn status bit 2 reports the TX DUC FIFO underflow. Once the
    // writer is primed/running this is hardware confirmation that our
    // stream failed to maintain cadence, so fail closed immediately.
    if (txWriter_.isRunning() && (u[30] & 0x04u) != 0)
        latchTxFault(QStringLiteral("radio reported DUC FIFO underflow"));

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
                        rdBeU16(p + 37),      // speaker FIFO depth
                        u[30],                // FIFO status flags
                        rdBeU16(p + 35));     // TX DUC FIFO depth
}

} // namespace lyra::wire

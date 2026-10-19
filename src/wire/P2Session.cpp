// Lyra — HPSDR Protocol 2 control-plane session implementation.
// See P2Session.h for the protocol summary + references.

#include "P2Session.h"

#include "P2TxCmaster.h"   // setP2TxCmasterChannelRunning — run the TXA channel with the DUC transport
#include "ps/PsCalcThread.h"

#include <QtEndian>
#include <QAbstractSocket>
#include <QNetworkInterface>
#include <QSettings>
#include <QVariant>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

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

// deskHPSDR binds the P2 data socket to the discovery NIC's IPv4
// (new_protocol.c: interface_address). Binding AnyIPv4 lets Windows
// accept DDC0 (UDP 1035) and drop DDC1 (1036) as a separate Public-
// profile flow. Match the radio onto a same-subnet local address.
QHostAddress localIpv4OnSameSubnet(const QHostAddress &radio) {
    if (radio.protocol() != QAbstractSocket::IPv4Protocol)
        return {};
    const quint32 r = radio.toIPv4Address();
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) ||
            (flags & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &e : iface.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            const quint32 ip = e.ip().toIPv4Address();
            const quint32 mask = e.netmask().toIPv4Address();
            if (mask == 0)
                continue;
            if ((ip & mask) == (r & mask))
                return e.ip();
        }
    }
    return {};
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

// Orion MkII BPF board (ANAN-7000DLE / 8000DLE). Same band windows and
// wire bits as saturnAlexRxWord. Input routing differs: Bypass is bit
// 11, and Ext1 / Xvtr also set bit 14 (that bit connects the external
// jack on this board). Trx is the shared ladder with no route bits.
quint16 orionMkiiAlexRxWord(quint32 hz, P2RxInput input, bool hpfBypass) {
    quint16 route = 0;
    switch (input) {
        case P2RxInput::Bypass: route |= 1u << 11; break;
        case P2RxInput::Ext1:   route |= (1u << 9) | (1u << 14); break;
        case P2RxInput::Xvtr:   route |= (1u << 8) | (1u << 14); break;
        case P2RxInput::Trx: break;
    }
    return static_cast<quint16>(
        route | saturnAlexRxWord(hz, P2RxInput::Trx, hpfBypass));
}

// Classic Alex HPF (ANAN-10 / 10E / 100 / 100B / 100D / 200D). deskHPSDR
// new_protocol.c default HPF path — same wire bits as Saturn BPF, older
// band edges (1.8 / 6.5 / 9.5 / 13 / 20 / 50 MHz). Do not use on
// OrionMkII (orionMkiiAlexRxWord) or Saturn (saturnAlexRxWord).
quint16 classicAlexRxWord(quint32 hz, P2RxInput input, bool hpfBypass) {
    quint16 w = 0;
    switch (input) {
        // DeskHPSDR alex.h: Bypass = K36 bit 11, not EXT2 bit 10.
        case P2RxInput::Bypass: w |= 1u << 11; break;
        case P2RxInput::Ext1:   w |= 1u << 9;  break;
        case P2RxInput::Xvtr:   w |= 1u << 8;  break;
        case P2RxInput::Trx:                         break;
    }
    if (hpfBypass || hz < 1'800'000u) return w | (1u << 12);
    if (hz <  6'500'000u) return w | (1u << 6); // 1.5 MHz HPF
    if (hz <  9'500'000u) return w | (1u << 5); // 6.5 MHz HPF
    if (hz < 13'000'000u) return w | (1u << 4); // 9.5 MHz HPF
    if (hz < 20'000'000u) return w | (1u << 1); // 13 MHz HPF
    if (hz < 50'000'000u) return w | (1u << 2); // 20 MHz HPF
    return w | (1u << 3);                       // 6 m preamp
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

// Two BPF banks. Byte [59] = 0x03 enables both. RX IQ is DDC2/DDC3
// so the band decoder tracks the dial. TX stays dummy-load armed.
const P2HardwareProfile kOrionMkiiProfile = {
    "ANAN-7000DLE", "Orion MkII (ANAN-7000/8000)",
    5, 7, 2,
    &orionMkiiAlexRxWord, &saturnAlexTxWord,
    false, false, 0x03, true,
};

// ANAN-10 / 10E / HERMES: 1 ADC, classic Alex (they have a filter
// board — not BrickSDR2's zero-Alex fixed TX). TX keys like BrickSDR2
// (Enable PA + MOX + Drive, no dummy-load arm).
const P2HardwareProfile kHermesAlexP2 = {
    "ANAN-10", "Hermes-class ANAN-10 (classic Alex)", 1, 10, 1,
    &classicAlexRxWord, &saturnAlexTxWord,
    false, true, 0x01,  // Alex0 — PA/LPF/TR on the filter board
};
// ANAN-100 / 100B: still 1 ADC + classic Alex (not Brick3's 2-ADC
// Angelia DDC map). TX keys like Brick3SDR (on-air, no bench arm).
const P2HardwareProfile kAnan100HermesP2 = {
    "ANAN-100", "Hermes-class ANAN-100 (classic Alex)", 1, 10, 1,
    &classicAlexRxWord, &saturnAlexTxWord,
    false, true, 0x01,
};
const P2HardwareProfile kAngeliaOrionAlexP2 = {
    "ANAN-100D", "Angelia/Orion ANAN (classic Alex)", 3, 10, 2,
    &classicAlexRxWord, &saturnAlexTxWord,
    false, true, 0x01, true,  // Alex0 + DDC2/DDC3 HP; TX like Brick3SDR
};
const P2HardwareProfile kBrick3Profile = {
    "BRICK3-SDR", "Brick3SDR", 3, 10, 2,
    &classicAlexRxWord, &saturnAlexTxWord,
    false, true, 0x01, true,  // on-air TX: keys like BrickSDR2 (no bench arm)
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
    true,  // txOnAirValidated: BrickSDR2 SSB first contact on air (v0.22.0) —
           // keys like HL2 (Enable PA + MOX + Drive), no transient arm.
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
    // ANAN-10 / 10E / HERMES: BrickSDR2 TX posture, keep classic Alex.
    // Discovery "Hermes" still defaults to BrickP2 — pick these in Settings.
    if (modelKey.compare(QLatin1String("ANAN-10"), Qt::CaseInsensitive) == 0 ||
        modelKey.compare(QLatin1String("ANAN-10E"), Qt::CaseInsensitive) == 0 ||
        modelKey.compare(QLatin1String("HERMES"), Qt::CaseInsensitive) == 0)
        return &kHermesAlexP2;
    // ANAN-100 / 100B: Brick3SDR TX posture on the 1-ADC Hermes-Alex wire.
    if (modelKey.compare(QLatin1String("ANAN-100"), Qt::CaseInsensitive) == 0 ||
        modelKey.compare(QLatin1String("ANAN-100B"), Qt::CaseInsensitive) == 0)
        return &kAnan100HermesP2;
    if (modelKey.compare(QLatin1String("BRICK3-SDR"), Qt::CaseInsensitive) == 0)
        return &kBrick3Profile;
    // ANAN-100D / 200D: Brick3SDR TX + the 2-ADC Angelia DDC map.
    if (modelKey.compare(QLatin1String("ANAN-100D"), Qt::CaseInsensitive) == 0 ||
        modelKey.compare(QLatin1String("ANAN-200D"), Qt::CaseInsensitive) == 0)
        return &kAngeliaOrionAlexP2;
    // Orion MkII BPF. ORIONMKII is the discovery alias (catalog maps it
    // to ANAN-7000DLE, but a saved key may still be the board name).
    // Anvelina Pro3 is the same board. Red Pitaya shares the board id
    // and is not this filter set.
    if (modelKey.compare(QLatin1String("ANAN-7000DLE"), Qt::CaseInsensitive) == 0 ||
        modelKey.compare(QLatin1String("ANAN-8000DLE"), Qt::CaseInsensitive) == 0 ||
        modelKey.compare(QLatin1String("ORIONMKII"), Qt::CaseInsensitive) == 0 ||
        modelKey.compare(QLatin1String("ANVELINA-PRO3"), Qt::CaseInsensitive) == 0)
        return &kOrionMkiiProfile;
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

void P2Session::setDucFrequencyHz(quint32 hz) {
    const bool changed = ducFreqHz_ != hz;
    ducFreqHz_ = hz;
    // Alex LPF and the TX phase word are rebuilt from this on every HP
    // packet. A band change that waits for the 100 ms tick ships the new
    // filter in the same datagram as the first MOX, and that key produces
    // no RF. Push the new word while still in receive. An unchanged word
    // must not send: Tune's NCO push and the following MOX were landing
    // in one turn when the session thread was busy (desk attached), and
    // that burst closed the T/R relay with PA latched off.
    if (!open_ || !changed)
        return;
    const bool transmitting =
        P2TxSafetyGate::evaluate(txIntent_, txSafety_).transmit;
    sock_.writeDatagram(buildHighPriorityPacket(true), radioAddr_,
                        kPortHpToSdr);
    if (!transmitting)
        filterHpAge_.start();
}

void P2Session::setDdcFrequencyHz(int ddc, quint32 hz) {
    if (ddc < 0 || ddc >= static_cast<int>(ddcFreqHz_.size())) return;
    ddcFreqHz_[static_cast<std::size_t>(ddc)] = hz;
    // DDC0 is already on the 100 ms HP cadence. SUB DDC phase words must
    // land before we wait another tick — firmware that keys DDC1/2/3 off
    // the HP word otherwise streams silence.
    if (open_ && ddc >= 1)
        sock_.writeDatagram(buildHighPriorityPacket(true), radioAddr_,
                            kPortHpToSdr);
}

bool P2Session::angeliaRxSlots() const {
    return profile_ && profile_->angeliaRxDdcSlots;
}

int P2Session::rx1DdcIndex() const {
    if (diversityOverlayLive())
        return 0;
    return angeliaRxSlots() ? 2 : 0;
}

int P2Session::rx2DdcIndex() const {
    return angeliaRxSlots() ? 3 : 1;
}

bool P2Session::diversityOverlayLive() const {
    if (!diversityArmed_ || !angeliaRxSlots())
        return false;
    if (!profile_ || profile_->adcCount < 2)
        return false;
    if (isPureSignalLive())
        return false;
    if (P2TxSafetyGate::evaluate(txIntent_, txSafety_).transmit)
        return false;
    return true;
}

void P2Session::setDiversityArmed(bool on) {
    if (diversityArmed_ == on)
        return;
    if (on && subRx2Wanted_)
        disarmSubSecondaryDdcs();
    diversityArmed_ = on;
    if (!open_)
        return;
    sendDdcSpecificToRadio(true);
    sock_.writeDatagram(buildHighPriorityPacket(true), radioAddr_,
                        kPortHpToSdr);
    punchEnabledDdcIqFirewalls();
}

void P2Session::enablePrimaryRx(quint16 rateKhz) {
    if (angeliaRxSlots()) {
        // DDC0's IQ port is the feedback stream on this class, not RX1.
        ddcEnabled_[0] = false;
        ddcAdc_[2] = 0;
        enableDdc(2, rateKhz);
        return;
    }
    enableDdc(0, rateKhz);
}

void P2Session::enableDdc(int ddc, quint16 rateKhz) {
    if (ddc < 0 || ddc >= kNumDdc) return;
    const auto i = static_cast<std::size_t>(ddc);
    ddcEnabled_[i]    = true;
    ddcRateKhz_[i]    = rateKhz;
    ddcSeqStarted_[i] = false;   // stream (re)starts — reseed seq tracking
    if (ddc == rx2DdcIndex()) {
        warnedNoDdc1Iq_ = false;
        ddc1IqOkLogged_ = false;
        ddc1IqLogLeft_  = 30;
        unmatchedIqLogLeft_ = 12;
    }
    // Push the new config now rather than waiting for the 5 s refresh.
    if (open_) {
        sendDdcSpecificToRadio(true);
        punchEnabledDdcIqFirewalls();
        if (ddc >= 1)
            scheduleDdc1FirewallPunch();
    }
}

void P2Session::disableDdc(int ddc) {
    if (ddc < 0 || ddc >= kNumDdc) return;
    ddcEnabled_[static_cast<std::size_t>(ddc)] = false;
    if (ddc == 1) {
        warnedNoDdc1Iq_ = false;
        ddc1IqOkLogged_ = false;
        ddc1IqLogLeft_  = 0;
        unmatchedIqLogLeft_ = 0;
    }
    if (open_)
        sendDdcSpecificToRadio(true);
}

void P2Session::armSubSecondaryDdcs(quint16 rateKhz, quint32 freqHz) {
    if (diversityArmed_)
        diversityArmed_ = false;
    subRx2Wanted_ = true;
    const int rx2 = rx2DdcIndex();
    const auto slot = static_cast<std::size_t>(rx2);
    const bool alreadyArmed =
        ddcEnabled_[slot] && ddcRateKhz_[slot] == rateKhz;
    // HP phase words for the second receiver still live in slot 1.
    // On the two-ADC class that word is copied onto DDC3.
    ddcFreqHz_[1] = freqHz;
    if (angeliaRxSlots()) {
        ddcEnabled_[1] = false;
        ddcAdc_[2] = 0;
        ddcAdc_[3] = 1;
    } else {
        ddcEnabled_[2] = false;
        ddcEnabled_[3] = false;
    }
    ddcEnabled_[slot] = true;
    ddcRateKhz_[slot] = rateKhz;
    if (alreadyArmed) {
        // VFO B / SPLIT retune: phase word rides the HP packet. A full
        // general + receive_specific blast on every 1 kHz step does not
        // start the second stream and floods :1024/:1025.
        if (open_ && hpTimer_.isActive())
            sock_.writeDatagram(buildHighPriorityPacket(true), radioAddr_,
                                kPortHpToSdr);
        return;
    }
    warnedNoDdc1Iq_ = false;
    ddc1IqOkLogged_ = false;
    ddc1IqLogLeft_ = 30;
    unmatchedIqLogLeft_ = 12;
    subIqLatchDdc_ = -1;
    ddcSeqStarted_[slot] = false;
    if (!open_ || !hpTimer_.isActive())
        return;  // handshake still pending; finishOpenHandshake programs mask
    emit logLine(QStringLiteral(
        "P2: SUB arm — DDC%1, UDP %2, then 1-byte host punch "
        "(Windows UDP return path)")
                     .arg(rx2)
                     .arg(kPortDdcIq0 + rx2));
    sendDdcSpecificToRadio(true);
    sock_.writeDatagram(buildHighPriorityPacket(true), radioAddr_,
                        kPortHpToSdr);
    punchEnabledDdcIqFirewalls();
    scheduleDdc1FirewallPunch();
}

void P2Session::disarmSubSecondaryDdcs() {
    subRx2Wanted_ = false;
    subIqLatchDdc_ = -1;
    warnedNoDdc1Iq_ = false;
    ddc1IqOkLogged_ = false;
    ddc1IqLogLeft_ = 0;
    unmatchedIqLogLeft_ = 0;
    if (angeliaRxSlots()) {
        // DDC2 is RX1 on this class. Only the second receiver goes off.
        ddcEnabled_[1] = false;
        ddcEnabled_[3] = false;
    } else {
        for (int d = 1; d <= 3; ++d)
            ddcEnabled_[static_cast<std::size_t>(d)] = false;
    }
    if (open_)
        sendDdcSpecificToRadio(true);
}

bool P2Session::shouldFeedRx2(int ddc) {
    const int want = rx2DdcIndex();
    if (ddc != want || !subRx2Wanted_ ||
        !ddcEnabled_[static_cast<std::size_t>(want)])
        return false;
    if (isPureSignalLive() || diversityOverlayLive())
        return false;
    if (subIqLatchDdc_ != want) {
        subIqLatchDdc_ = want;
        emit logLine(QStringLiteral(
            "P2: SUB IQ from radio UDP %1 (DDC%2) — feeding WDSP RX2")
                         .arg(kPortDdcIq0 + ddc)
                         .arg(ddc));
    }
    return true;
}

void P2Session::punchDdcIqFirewall(int ddc) {
    if (!open_ || ddc < 0 || ddc >= kNumDdc) return;
    const char b = 0;
    sock_.writeDatagram(&b, 1, radioAddr_,
                        static_cast<quint16>(kPortDdcIq0 + ddc));
}

void P2Session::punchEnabledDdcIqFirewalls() {
    for (int d = 0; d < 4; ++d) {
        if (ddcEnabled_[static_cast<std::size_t>(d)])
            punchDdcIqFirewall(d);
    }
    // Angelia leaves DDC0 disabled (it is the feedback port). The
    // diversity overlay still needs UDP 1035 or RX1 stays silent.
    if (diversityOverlayLive())
        punchDdcIqFirewall(0);
}

void P2Session::scheduleDdc1FirewallPunch() {
    if (!open_)
        return;
    const quint32 epoch = openEpoch_;
    QTimer::singleShot(80, this, [this, epoch]() {
        if (!open_ || epoch != openEpoch_)
            return;
        punchEnabledDdcIqFirewalls();
    });
}

void P2Session::ingestRadioDatagram(quint16 senderPort, const QByteArray &buf) {
    // Protocol 2 IQ identity is the *radio source* port (1035+n). Never
    // remap a 1035 packet because it landed on a well-known dest.
    if (senderPort == kPortHpFromSdr && buf.size() == kStatusLen) {
        parseStatus(buf);
    } else if (senderPort >= kPortDdcIq0 &&
               senderPort <  kPortDdcIq0 + kNumDdc &&
               buf.size() == kIqFrameLen) {
        parseIqFrame(senderPort - kPortDdcIq0, buf);
    } else if (senderPort == kPortMicFromSdr &&
               buf.size() == kMicPktLen) {
        parseMic(buf);
    } else if (unmatchedIqLogLeft_ > 0 &&
               senderPort != kPortHpFromSdr) {
        --unmatchedIqLogLeft_;
        emit logLine(QStringLiteral(
            "P2: unmatched UDP from radio src port %1 size %2 "
            "(second-receiver IQ is 1444 bytes from src %3)")
                         .arg(senderPort)
                         .arg(buf.size())
                         .arg(kPortDdcIq0 + rx2DdcIndex()));
    }
}

void P2Session::setDdcAdc(int ddc, int adc) {
    if (ddc < 0 || ddc >= kNumDdc || adc < 0 || adc > 1) return;
    ddcAdc_[static_cast<std::size_t>(ddc)] = static_cast<quint8>(adc);
    if (open_)
        sendDdcSpecificToRadio(true);
}

QByteArray P2Session::buildGeneralPacket() const {
    QByteArray pkt(kGeneralLen, char{0});
    // [0..3] sequence stays 0 (control packets never increment — see
    // header).  [4] = 0x00 general command.
    //
    // All port fields stay 0. This session uses one ephemeral socket, so
    // filling PC-origin 1025–1029 would tell firmware to expect DDC/HP
    // from sockets we never bind. Filling radio-origin 1025/1035/1026
    // stalled DDC0 ~44 s on this Brick until status arrived; DDC1 still
    // never emitted. Firmware defaults those source ports.
    // Wideband [23..28] stays 0 = off.  [37] option flags: bit 0x08 =
    // frequencies are DDS phase words — matches what the HP fields actually
    // carry (see phaseWord(); the radio's HP handler hardcodes phase-word
    // decode anyway, but declaring it keeps us honest).  No timestamps/VITA-49.
    pkt[37] = char{0x08};
    // [38] bit0 = hardware watchdog ENABLED: if this client dies, the
    // radio unkeys and idles ~1 s later instead of holding a dead
    // session — the RF-safe failure mode.
    pkt[38] = char{0x01};
    // [58] bit0 is PA enable. It must already be set in receive: the radio
    // latches it on the rising MOX edge. Tying it to the keyed gate made
    // the first key (and the first key after a band change) send PA-enable
    // in the same burst as MOX, and that key produced no RF. A later key
    // in that same burst latches PA off until this bit has been 1 in
    // receive again. MOX and drive stay on the keyed gate. A stale status
    // packet must not clear the bit — that drop is what made the next
    // Tune / 2-tone / voice key dark until Stop.
    const bool paBit = paHold_ && txSafety_.operatorArmed &&
                       txSafety_.sessionRunning &&
                       !txSafety_.faultLatched;
    pkt[58] = paBit ? char{0x01} : char{0x00};
    // [59] = Alex-board enable. Classic-Alex profiles set bit0; BrickSDR
    // and Saturn leave 0 (no SPI Alex / onboard filters already clocked).
    pkt[59] = profile_ ? static_cast<char>(profile_->alexEnableBits) : char{0};
    return pkt;
}

QByteArray P2Session::buildDdcSpecificPacket() const {
    // Layout per p2app protocol2_command.c P2DecodeDDCSpecificCommand:
    // [4] = ADC count, [5]/[6] = dither/random bits,
    // [7..8] = DDC enable bitmap (LITTLE-endian u16; bytes 9..16 are
    // the P2 wire bitmap for DDC16..79 and MUST stay zero — Saturn
    // rejects the packet otherwise), then per-DDC config at
    // [17 + 6*n]: +0 source, +1..2 sample rate in kHz
    // (BE u16, one of 48/96/192/384/768/1536), +5 sample size (24).
    // Live PS (DeskHPSDR receive_specific, Hermes and Angelia): DDC0
    // coupler + DDC1 TX replica, 192 kHz, [1363]=0x02, enable DDC0
    // only. Same packet for Brick2 and Brick3.
    QByteArray pkt(kDdcSpecificLen, char{0});
    pkt[4] = static_cast<char>(profile_ ? profile_->adcCount : 1);
    quint16 mask = 0;
    // Program DDC0+DDC1 always (Hermes/Brick 2-receiver). Slot 1 left
    // at rate=0 while SUB is later armed is a firmware-miss we hit on
    // this Brick (DDC1 never emitted).
    const int fillSlots = 2;
    const int nAdc = profile_ ? profile_->adcCount : 1;
    const bool psTx = isPureSignalLive();
    const bool angeliaSlots =
        profile_ && profile_->angeliaRxDdcSlots;
    auto writeDdcSlot = [&](int i, quint8 adc, quint16 rate) {
        const int off = 17 + 6 * i;
        pkt[off]     = static_cast<char>(adc);
        pkt[off + 1] = static_cast<char>(rate >> 8);
        pkt[off + 2] = static_cast<char>(rate & 0xFF);
        pkt[off + 5] = char{24};                       // 24-bit samples
    };
    for (int i = 0; i < kNumDdc; ++i) {
        const auto n = static_cast<std::size_t>(i);
        if (ddcEnabled_[n])
            mask |= static_cast<quint16>(1u << i);
        if (i >= fillSlots && !ddcEnabled_[n])
            continue;
        quint8 adc = ddcAdc_[n];
        quint16 rate = ddcRateKhz_[n];
        if (psTx) {
            rate = 192;
            // DeskHPSDR: DDC0 ADC = PS_RX_FEEDBACK->adc (create default 0);
            // DDC1 ADC = n_adc (Hermes 1, Angelia/Brick3 2).
            if (i == 0)
                adc = 0;
            else
                adc = static_cast<quint8>(nAdc);
        } else if (nAdc <= 1) {
            adc = 0;
        }
        if (rate == 0)
            rate = ddcRateKhz_[0] ? ddcRateKhz_[0] : quint16{192};
        writeDdcSlot(i, adc, rate);
    }
    // DeskHPSDR receive_specific first loop (Angelia/Orion/Saturn):
    // receiver[i] programs DDC(i+2) ADC/rate/24-bit, then the PS
    // overlay ORs DDC0. Hermes/Brick2 skip this (RX is DDC0).
    if (angeliaSlots && !psTx) {
        // RX1 is DDC2 on ADC 0. RX2 is DDC3 on ADC 1. Do not copy the
        // Settings ADC onto those slots — index 2 is the feedback
        // selector, not the second converter.
        const quint16 rx1Rate = ddcRateKhz_[2] ? ddcRateKhz_[2]
                                : (ddcRateKhz_[0] ? ddcRateKhz_[0]
                                                 : quint16{192});
        writeDdcSlot(2, 0, rx1Rate);
        if (ddcEnabled_[3]) {
            const quint16 rx2Rate = ddcRateKhz_[3] ? ddcRateKhz_[3]
                                                   : rx1Rate;
            writeDdcSlot(3, 1, rx2Rate);
        }
    }
    if (psTx) {
        // DeskHPSDR: [1363]=0x02, [7] |= 1 (DDC0). Duplex can also
        // keep DDC2/DDC3; Lyra is not duplex during PS.
        mask = 0x0001;
        pkt[1363] = 0x02;
    } else if (diversityOverlayLive()) {
        // Stock diversity: DDC0 ADC0 + DDC1 ADC1, one rate, enable
        // 0x01, sync 0x02. Never 0x03 (both bits) and never 0x0F.
        // Dither and random stay the current RX1 bytes (both 0).
        const quint16 rx1Rate = ddcRateKhz_[2] ? ddcRateKhz_[2]
                                : (ddcRateKhz_[0] ? ddcRateKhz_[0]
                                                 : quint16{192});
        writeDdcSlot(0, 0, rx1Rate);
        writeDdcSlot(1, 1, rx1Rate);
        mask = 0x0001;
        pkt[1363] = 0x02;
    }
    pkt[7] = static_cast<char>(mask & 0xFF);           // LE enable mask
    pkt[8] = static_cast<char>(mask >> 8);
    return pkt;
}

void P2Session::sendDdcSpecificToRadio(bool logConfig) {
    if (!open_) return;
    QByteArray pkt = buildDdcSpecificPacket();
    const quint32 seq = ddcSpecificSeq_++;
    wrBeU32(pkt.data(), seq);
    sock_.writeDatagram(pkt, radioAddr_, kPortDdcConfig);
    if (!logConfig) return;
    const quint16 mask =
        static_cast<quint8>(pkt[7]) |
        (static_cast<quint16>(static_cast<quint8>(pkt[8])) << 8);
    const quint8 adc0 = static_cast<quint8>(pkt[17]);
    const quint8 adc1 = static_cast<quint8>(pkt[23]);
    const quint16 rate0 = (static_cast<quint8>(pkt[18]) << 8) |
                          static_cast<quint8>(pkt[19]);
    const quint16 rate1 = (static_cast<quint8>(pkt[24]) << 8) |
                          static_cast<quint8>(pkt[25]);
    const quint8 adc2 = static_cast<quint8>(pkt[29]);
    const quint8 adc3 = static_cast<quint8>(pkt[35]);
    const quint16 rate2 = (static_cast<quint8>(pkt[30]) << 8) |
                          static_cast<quint8>(pkt[31]);
    const quint16 rate3 = (static_cast<quint8>(pkt[36]) << 8) |
                          static_cast<quint8>(pkt[37]);
    const quint8 sync0 = static_cast<quint8>(pkt[1363]);
    emit logLine(
        QStringLiteral(
            "P2: DDC-specific seq=%1 mask=0x%2 adcCount=%3 "
            "DDC0 adc=%4 %5 kHz DDC1 adc=%6 %7 kHz sync0=0x%8")
            .arg(seq)
            .arg(mask, 4, 16, QLatin1Char('0'))
            .arg(profile_ ? profile_->adcCount : 0)
            .arg(adc0)
            .arg(rate0)
            .arg(adc1)
            .arg(rate1)
            .arg(sync0, 2, 16, QLatin1Char('0'))
        + QStringLiteral(" DDC2 adc=%1 %2 kHz DDC3 adc=%3 %4 kHz DDC1Hz=%5")
              .arg(adc2)
              .arg(rate2)
              .arg(adc3)
              .arg(rate3)
              .arg(ddcFreqHz_[1]));
}

QByteArray P2Session::buildHighPriorityPacket(bool run) const {
    QByteArray pkt(kHpLen, char{0});
    const auto tx = P2TxSafetyGate::evaluate(txIntent_, txSafety_);
    // [0..3] sequence 0 (control). [4]: bit0 run, bit1 MOX.
    // DeskHPSDR high_priority does not set bit7 for PureSignal.
    const bool transmit = run && tx.transmit;
    pkt[4] = static_cast<char>((run ? 0x01 : 0x00) |
                               (transmit ? 0x02 : 0x00));
    // [5] CWX off; [6..8] must be zero (hardened p2app rejects the
    // packet otherwise — protocol2_command.c validation).
    if (profile_ && profile_->angeliaRxDdcSlots) {
        const quint32 rx2 = diversityOverlayLive()
                                 ? ddcFreqHz_[0]
                                 : (subRx2Wanted_ ? ddcFreqHz_[1]
                                                  : ddcFreqHz_[0]);
        wrBeU32(pkt.data() + 9, phaseWord(ddcFreqHz_[0]));   // DDC0 STM32
        wrBeU32(pkt.data() + 13, phaseWord(rx2));            // DDC1 STM32
        wrBeU32(pkt.data() + 17, phaseWord(ddcFreqHz_[0]));  // DDC2 RX1
        wrBeU32(pkt.data() + 21, phaseWord(rx2));            // DDC3 RX2
        for (std::size_t i = 4; i < ddcFreqHz_.size(); ++i)
            wrBeU32(pkt.data() + 9 + i * 4, phaseWord(ddcFreqHz_[i]));
    } else {
        for (std::size_t i = 0; i < ddcFreqHz_.size(); ++i)
            wrBeU32(pkt.data() + 9 + i * 4,
                    phaseWord(ddcFreqHz_[i]));
    }
    wrBeU32(pkt.data() + 329, phaseWord(ducFreqHz_));  // DUC (TX) phase word
    pkt[345] = static_cast<char>(analogDriveByte_(tx));
    // [1396..99] client control word + CAT port 0.
    // [1400..03] xvtr/mute/OC/user outputs 0.
    // [1428..31] Alex1 TX/RX. Classic-Alex copies Alex0 TX here (Thetis
    // bit-inherit). BrickSDR overlay below replaces these on key.
    // [1432..35] Alex0 TX + RX halfwords from the hardware profile —
    // the RF front end (BPF/LPF select + TRX antenna route).  Derived
    // from the independent DUC/DDC carriers every build, so band changes
    // re-filter at the 100 ms cadence. All-zero here = disconnected front
    // end.
    if (profile_) {
        const quint16 txw =
            profile_->alexTxWord(ducFreqHz_, trxAntenna_);
        // While TX+PS, analog RX bits stay Trx/Internal (create-default
        // alex_antenna=0). LOCKED 2026-10-10: no Internal/EXT1/ByPass
        // picker — Thetis has none; Brick3 rear PS-A is already the
        // coupler via ALEX_PS_BIT (bit 18) below. DeskHPSDR's PS-menu
        // EXT1 is a DeskHPSDR-only analog-bit combo. Do not collapse
        // that with MAP Ext1 (Brick3 silk RX2, Alex bit 9).
        const P2RxInput analogIn =
            isPureSignalLive() ? P2RxInput::Trx : rxInput_;
        const quint16 rxw =
            profile_->alexRxWord(ddcFreqHz_[0], analogIn, hpfBypass_);
        pkt[1432] = static_cast<char>(txw >> 8);
        pkt[1433] = static_cast<char>(txw & 0xFF);
        pkt[1434] = static_cast<char>(rxw >> 8);
        pkt[1435] = static_cast<char>(rxw & 0xFF);
        // Second BPF bank (Alex1 RX, bytes 1430-31). Only when Alex1
        // is enabled. Follows the second receiver, or the main dial
        // when that receiver is not on its own frequency. A later
        // keyed ground-RX2 OR into [1430] still applies.
        if (profile_->alexEnableBits & 0x02) {
            const quint32 rx2hz = diversityOverlayLive()
                ? ddcFreqHz_[0]
                : (subRx2Wanted_ ? ddcFreqHz_[1] : ddcFreqHz_[0]);
            const quint16 rx2w =
                profile_->alexRxWord(rx2hz, analogIn, hpfBypass_);
            pkt[1430] = static_cast<char>(rx2w >> 8);
            pkt[1431] = static_cast<char>(rx2w & 0xFF);
        }
        // saturnAlexTxWord is LPF+ANT only. The T/R relay is Alex word
        // bit 27 → TX halfword bit 11 → [1432]|=0x08. Without it the
        // PA never sees the antenna (driver leak / tenths of a watt).
        // Skip Saturn/G2 (alexEnableBits==0) and BrickSDR (fixed overlay
        // below). Do NOT also set [1433]|=0x04: that is Alex bit 18, the
        // PureSignal coupler route. Forcing it on a normal key keeps the
        // PA at milliwatts while DeskHPSDR (same Brick3) puts ~15 W on
        // the dummy. PS already ORs bit 18 in the psArmed_ block below.
        if (transmit && profile_->alexEnableBits != 0) {
            pkt[1432] = static_cast<char>(
                static_cast<unsigned char>(pkt[1432]) | 0x08);
            pkt[1428] = pkt[1432];
            pkt[1429] = pkt[1433];
        }
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
        // DDC0 frequency word tracks the DUC while keyed so a cross-band
        // TX VFO still produces RF on radios whose TX path samples DDC0.
        // DDC0 stays disabled; only the phase word is overlaid.
        wrBeU32(pkt.data() + 9, phaseWord(ducFreqHz_));
    }
    // ALEX_PS_BIT (bit 18): alex1 whenever PS on; alex0 when TX.
    // Analog RX bits stay Internal. No PS-input picker (Thetis has none).
    if (psArmed_)
        pkt[1429] = static_cast<char>(
            static_cast<unsigned char>(pkt[1429]) | 0x04);
    if (psArmed_ && transmit) {
        wrBeU32(pkt.data() + 9, phaseWord(ducFreqHz_));
        wrBeU32(pkt.data() + 13, phaseWord(ducFreqHz_));
        pkt[1433] = static_cast<char>(
            static_cast<unsigned char>(pkt[1433]) | 0x04);
    }
    // Dual-ADC analog isolate: ground the RX2 jack while keyed.
    // Brick2's captured TX overlay already writes 0x01 here on
    // one-converter radios (no RX2 jack) — do not skip that path.
    // OR only when the profile actually has a second converter.
    if (transmit && rx2GroundOnTx_.load(std::memory_order_relaxed) &&
        profile_ && profile_->adcCount > 1) {
        pkt[1430] = static_cast<char>(
            static_cast<unsigned char>(pkt[1430]) | 0x01);
    }
    const bool keyedWithPa = transmit && tx.paEnabled;
    // HP ATT: DeskHPSDR PA→both 31, then PS overwrites ADC1 [1442] only.
    pkt[1442] = static_cast<char>(overlayHpAdcAttByte(1, keyedWithPa));
    pkt[1443] = static_cast<char>(overlayHpAdcAttByte(0, keyedWithPa));
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
    sendDucSpecificIfOpen();
}

void P2Session::setPsFeedbackAttn(int db) {
    psFeedbackAttnDb_ = std::clamp(db, 0, 31);
    sendDucSpecificIfOpen();
    if (!open_)
        return;
    if (P2TxSafetyGate::evaluate(txIntent_, txSafety_).transmit)
        applyTxControlNow();
}

void P2Session::setPureSignalArmed(bool on) {
    if (psArmed_ == on) return;
    psArmed_ = on;
    if (!on)
        clearPsccIq();
    applyTxControlNow();
}

bool P2Session::isPureSignalLive() const {
    return psArmed_ && P2TxSafetyGate::evaluate(txIntent_, txSafety_).transmit;
}

bool P2Session::psccUsesSeparateDdcStreams() const {
    return false;
}

void P2Session::clearPsccIq() {
    psccRx_.clear();
    psccTx_.clear();
}

void P2Session::ingestPsccInterleaved(const double *iq, int nComplex) {
    if (!iq || nComplex < 2)
        return;
    const int pairs = nComplex / 2;
    if (pairs <= 0)
        return;
    std::vector<double> rx(static_cast<std::size_t>(pairs) * 2);
    std::vector<double> tx(static_cast<std::size_t>(pairs) * 2);
    for (int i = 0; i < pairs; ++i) {
        rx[static_cast<std::size_t>(2 * i)]     = iq[4 * i];
        rx[static_cast<std::size_t>(2 * i) + 1] = iq[4 * i + 1];
        tx[static_cast<std::size_t>(2 * i)]     = iq[4 * i + 2];
        tx[static_cast<std::size_t>(2 * i) + 1] = iq[4 * i + 3];
    }
    ingestPsccIq(0, rx.data(), pairs);
    ingestPsccIq(1, tx.data(), pairs);
}

void P2Session::ingestPsccIq(int ddc, const double *iq, int nComplex) {
    if (!iq || nComplex <= 0 || (ddc != 0 && ddc != 1))
        return;
    if (!isPureSignalLive()) {
        if (!psccRx_.empty() || !psccTx_.empty())
            clearPsccIq();
        return;
    }
    auto &dst = (ddc == 0) ? psccRx_ : psccTx_;
    dst.insert(dst.end(), iq, iq + static_cast<std::size_t>(nComplex) * 2);
    constexpr std::size_t kCapDoubles = 16384;
    if (dst.size() > kCapDoubles) {
        std::size_t drop = dst.size() - kCapDoubles;
        if (drop % 2u != 0)
            ++drop;
        dst.erase(dst.begin(),
                  dst.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    const std::size_t n = std::min(psccRx_.size(), psccTx_.size()) / 2;
    if (n == 0)
        return;
    lyra::ps::PsCalcThread::instance().feed(
        static_cast<int>(n), psccRx_.data(), psccTx_.data());
    const auto used = static_cast<std::ptrdiff_t>(n * 2);
    psccRx_.erase(psccRx_.begin(), psccRx_.begin() + used);
    psccTx_.erase(psccTx_.begin(), psccTx_.begin() + used);
}

void P2Session::setAttOnTx(bool enabled, int db) {
    attOnTxEnabled_ = enabled;
    attOnTxDb_ = std::clamp(db, 0, 31);
    sendDucSpecificIfOpen();
    if (!open_)
        return;
    if (P2TxSafetyGate::evaluate(txIntent_, txSafety_).transmit)
        applyTxControlNow();
}

void P2Session::setRx2GroundOnTx(bool on) {
    bool expected = !on;
    if (!rx2GroundOnTx_.compare_exchange_strong(
            expected, on, std::memory_order_relaxed))
        return;
    if (!open_)
        return;
    if (P2TxSafetyGate::evaluate(txIntent_, txSafety_).transmit)
        applyTxControlNow();
}

quint8 P2Session::overlayAdcAttByte(int adcIndex, bool keyedWithPa) const {
    // DUC-specific (DeskHPSDR transmit_specific [59]/[58]): PA both 31,
    // then PS writes transmitter->attenuation onto ADC0 ([59]).
    if (isPureSignalLive() && adcIndex == 0)
        return static_cast<quint8>(std::clamp(psFeedbackAttnDb_, 0, 31));
    if (keyedWithPa && attOnTxEnabled_)
        return static_cast<quint8>(std::clamp(attOnTxDb_, 0, 31));
    if (diversityOverlayLive() && adcIndex == 1 && !adcAttenuation_.empty())
        return adcAttenuation_[0];
    if (adcIndex < 0 ||
        adcIndex >= static_cast<int>(adcAttenuation_.size()))
        return 0;
    return adcAttenuation_[static_cast<std::size_t>(adcIndex)];
}

quint8 P2Session::overlayHpAdcAttByte(int adcIndex, bool keyedWithPa) const {
    // HP (DeskHPSDR high_priority [1442]/[1443]): PA both 31, then PS
    // writes transmitter->attenuation onto ADC1 ([1442]) only.
    if (isPureSignalLive() && adcIndex == 1)
        return static_cast<quint8>(std::clamp(psFeedbackAttnDb_, 0, 31));
    if (keyedWithPa && attOnTxEnabled_)
        return static_cast<quint8>(std::clamp(attOnTxDb_, 0, 31));
    if (diversityOverlayLive() && adcIndex == 1 && !adcAttenuation_.empty())
        return adcAttenuation_[0];
    if (adcIndex < 0 ||
        adcIndex >= static_cast<int>(adcAttenuation_.size()))
        return 0;
    return adcAttenuation_[static_cast<std::size_t>(adcIndex)];
}

QByteArray P2Session::buildDucSpecificPacket() const {
    P2DucConfig cfg = ducConfig_;
    const auto tx = P2TxSafetyGate::evaluate(txIntent_, txSafety_);
    const bool keyedWithPa = tx.transmit && tx.paEnabled;
    const quint8 a0 = overlayAdcAttByte(0, keyedWithPa);
    const quint8 a1 = overlayAdcAttByte(1, keyedWithPa);
    cfg.txAttenuationDb[0] = a0;
    cfg.txAttenuationDb[1] = a1;
    cfg.txAttenuationDb[2] = a0;
    return P2TxPackets::encodeDucSpecific(cfg);
}

void P2Session::sendDucSpecificIfOpen() {
    if (!open_)
        return;
    sock_.writeDatagram(buildDucSpecificPacket(), radioAddr_, kPortDucConfig);
}

void P2Session::diagnosticArmHealthyTx(bool transmit, bool pa, int drive) {
    txSafety_.operatorArmed = true;
    txSafety_.sessionRunning = true;
    txSafety_.iqPrimed = true;
    txSafety_.transportRunning = true;
    txSafety_.telemetryHealthy = true;
    txSafety_.watchdogEnabled = true;
    txSafety_.faultLatched = false;
    if (transmit) {
        txIntent_.transmitRequested = true;
        txIntent_.paRequested = pa;
        txIntent_.drive = std::clamp(drive, 0, 255);
    } else {
        txIntent_ = {};
    }
}

void P2Session::setTxOperatorArmed(bool armed) {
    if (!armed) {
        txIntent_ = {};
        paHold_ = false;
    }
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

int P2Session::analogDriveByte_(const P2TxEffectiveState &tx) const {
    // Analog HP [345] is the P2 DAC scale, independent of the PA-bias bit.
    // Zeroing it whenever PA is off left a full-scale IQ carrier at ~0.9 W
    // with Drive/Tune dead (Brick3). Send the operator byte on every keyed
    // frame; [58] still gates PA bias.
    if (!tx.transmit)
        return 0;
    int drive = static_cast<int>(tx.drive);
    if (wireDriveProvider_) {
        const int emitted = std::clamp(wireDriveProvider_(), 0, 255);
        drive = std::min(emitted, static_cast<int>(txSafety_.driveCeiling));
    }
    return drive;
}

void P2Session::setWireDriveProvider(std::function<int()> provider) {
    wireDriveProvider_ = std::move(provider);
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
    // that. A *new* key request requires fresh telemetry; a drive/PA
    // refresh while already keyed must not latch (Settings paint + cap
    // fold used to re-enter here and lock Tune until Lyra restarted).
    const bool alreadyKeyed = txIntent_.transmitRequested;
    if (!on && keyDeferPending_) {
        keyDeferPending_ = false;
        ++keyDeferEpoch_;
    }
    // A receive-state frequency HP (Tune NCO, band change) and the first
    // MOX must not be the same turn. Desk traffic delays the session
    // thread so both queued calls run back to back: relay closes, PA
    // latches off, and the next host Tune is dark too.
    if (on && !alreadyKeyed && !keyDeferPending_
        && filterHpAge_.isValid() && filterHpAge_.elapsed() < 150) {
        deferredPa_ = paRequested;
        deferredDrive_ = std::clamp(drive, 0, 255);
        keyDeferPending_ = true;
        const quint32 epoch = ++keyDeferEpoch_;
        const int wait = static_cast<int>(150 - filterHpAge_.elapsed());
        QTimer::singleShot(std::max(20, wait), this, [this, epoch]() {
            if (epoch != keyDeferEpoch_ || !keyDeferPending_)
                return;
            keyDeferPending_ = false;
            filterHpAge_.invalidate();
            setTransmitIntent(true, deferredPa_, deferredDrive_);
        });
        return;
    }
    if (on && !alreadyKeyed
        && (!statusAge_.isValid() || statusAge_.elapsed() > 1000)) {
        // Do not clear telemetryHealthy here. That made the next general
        // packet drop PA enable, and the following key latched the radio
        // PA off until Stop. Refuse the key only. A sick writer is
        // re-primed below; a healthy one keeps streaming.
        emit logLine(QStringLiteral(
            "P2 TX: key refused, status telemetry is stale; "
            "re-priming in receive"));
        if (txSafety_.faultLatched || !txWriter_.isRunning() ||
            !txSafety_.iqPrimed) {
            QTimer::singleShot(0, this, [this]() {
                if (!txIntent_.transmitRequested)
                    restartTxTransportRxState();
            });
        }
        emitTxState(QStringLiteral("key refused, telemetry stale"));
        return;
    }
    const bool paWas = paHold_;
    if (on) {
        txIntent_.transmitRequested = true;
        txIntent_.paRequested = paRequested;
        txIntent_.drive = std::clamp(drive, 0, 255);
        paHold_ = paRequested;
    } else {
        txIntent_ = {};
        if (txSafety_.faultLatched) {
            // Host unkey (including a watts-cap cut) — re-prime so Tune
            // works without restarting the app. Fail-closed while keyed.
            QTimer::singleShot(0, this, [this]() {
                restartTxTransportRxState();
            });
        }
    }

    // PA enable rising in this same call as the first MOX latches the
    // radio PA off until the bit has been 1 in receive again. Leave the
    // bit in receive and refuse the key. The bridge holds the next key
    // until that settle finishes. A key whose bit is already 1 does not
    // rewrite it (skip general below).
    const bool newKey = on && !alreadyKeyed;
    if (newKey && paRequested && !paWas) {
        txIntent_ = {};
        paHold_ = paRequested;
        if (open_)
            sock_.writeDatagram(buildGeneralPacket(), radioAddr_,
                                kPortCommand);
        emitTxState(QStringLiteral(
            "key refused, PA enable was not in receive"));
        return;
    }
    txSafety_.transportRunning = txWriter_.isRunning();
    const auto effective = P2TxSafetyGate::evaluate(txIntent_, txSafety_);
    if (open_)
        applyTxControlNow(newKey && paWas == paHold_);
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

void P2Session::setPaHold(bool on) {
    paHold_ = on && txSafety_.operatorArmed;
    if (!open_)
        return;
    // General only. An HP write here would move the MOX bit.
    sock_.writeDatagram(buildGeneralPacket(), radioAddr_, kPortCommand);
}

void P2Session::applyTxControlNow(bool skipGeneralWhileKeying) {
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
    // Three copies: one lost datagram used to swallow the first MOX, or
    // leave RF up after key-up until the next 100 ms tick. Sequence on
    // these packets stays 0, same as the disconnect path.
    const QByteArray general = buildGeneralPacket();
    const QByteArray hp = buildHighPriorityPacket(true);
    if (transmitting) {
        if (!skipGeneralWhileKeying)
            sock_.writeDatagram(general, radioAddr_, kPortCommand);
        for (int i = 0; i < 3; ++i)
            sock_.writeDatagram(hp, radioAddr_, kPortHpToSdr);
    } else {
        for (int i = 0; i < 3; ++i)
            sock_.writeDatagram(hp, radioAddr_, kPortHpToSdr);
        sock_.writeDatagram(general, radioAddr_, kPortCommand);
    }
    sock_.writeDatagram(buildDucSpecificPacket(), radioAddr_, kPortDucConfig);
    sendDdcSpecificToRadio(psArmed_);
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
        analogDriveByte_(effective), txSafety_.faultLatched, txStateDetail_);
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

    // ONE socket for the whole session (see header wire model). Bind
    // an ephemeral port on the radio's NIC IPv4 so DDC0 (1035) and
    // DDC1 (1036) land on the same socket. Fall back to AnyIPv4 if
    // no same-subnet address exists (manual IP on another LAN).
    const QHostAddress nicIp = localIpv4OnSameSubnet(target);
    const QHostAddress bindAddr =
        nicIp.isNull() ? QHostAddress(QHostAddress::AnyIPv4) : nicIp;
    const auto bindFlags =
        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    // Prefer a stable host port so later P1 Start can send run=0 from
    // the same 5-tuple the Brick is still streaming to.
    constexpr quint16 kHostBindPort = 1025;
    auto tryBind = [&](const QHostAddress &addr, quint16 port) {
        sock_.abort();
        return sock_.bind(addr, port, bindFlags);
    };
    const bool bound =
        tryBind(bindAddr, kHostBindPort) ||
        tryBind(bindAddr, 0) ||
        (!nicIp.isNull() && tryBind(QHostAddress(QHostAddress::AnyIPv4),
                                    kHostBindPort)) ||
        (!nicIp.isNull() && tryBind(QHostAddress(QHostAddress::AnyIPv4), 0));
    if (!bound) {
        emit logLine(QStringLiteral("P2: bind failed: %1")
                         .arg(sock_.errorString()));
        return;
    }
    if (sock_.localPort() != kHostBindPort) {
        emit logLine(QStringLiteral(
            "P2: port 1025 unavailable — using %1:%2")
                         .arg(sock_.localAddress().toString())
                         .arg(sock_.localPort()));
    }
    sock_.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,
                          QVariant(0x80000));

    {
        QSettings s;
        QHostAddress local = sock_.localAddress();
        if (local.isNull() || local == QHostAddress::AnyIPv4)
            local = QHostAddress(QHostAddress::AnyIPv4);
        s.setValue(QStringLiteral("p2/lastBindIp"), local.toString());
        s.setValue(QStringLiteral("p2/lastBindPort"), sock_.localPort());
        s.setValue(QStringLiteral("p2/lastRadioIp"), ip);
        emit logLine(QStringLiteral("P2: remembered bind %1:%2 → %3")
                         .arg(local.toString())
                         .arg(sock_.localPort())
                         .arg(ip));
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
    warnedNoDdc1Iq_ = false;
    ddc1IqOkLogged_ = false;
    // If SUB was armed before bind, keep logging the second stream.
    const bool subArmed =
        subRx2Wanted_ &&
        ddcEnabled_[static_cast<std::size_t>(rx2DdcIndex())];
    rx1PortLogged_ = false;
    ddc1IqLogLeft_  = subArmed ? 30 : 0;
    unmatchedIqLogLeft_ = subArmed ? 12 : 0;
    ddcSpecificSeq_ = 0;
    if (!subArmed)
        subIqLatchDdc_ = -1;
    ddcIqCount_.fill(0);
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

    // Handshake: general (controller lease + reply address), then HP
    // run=0 to halt a stale stream. Receive/TX-specific and HP run=1
    // wait 100 ms so firmware can digest the lease before DDC config.
    sock_.writeDatagram(buildGeneralPacket(), radioAddr_, kPortCommand);
    sock_.writeDatagram(buildHighPriorityPacket(false), radioAddr_,
                        kPortHpToSdr);
    const quint32 epoch = ++openEpoch_;
    QTimer::singleShot(100, this, [this, epoch]() {
        if (!open_ || epoch != openEpoch_) return;
        finishOpenHandshake();
    });
    emit logLine(QStringLiteral(
        "P2: session opening to %1 (general -> :%2, HP run=1 in 100 ms -> :%3, "
        "bound %4:%5)")
        .arg(ip)
        .arg(kPortCommand)
        .arg(kPortHpToSdr)
        .arg(sock_.localAddress().toString())
        .arg(sock_.localPort()));
}

double P2Session::iqSampleScale() const {
    if (ddcRateKhz_[0] != 48)
        return 1.0;
    if (profile_ && profile_->adcCount > 1)
        return 1.0;
    return 0.035481338923357552;
}

void P2Session::finishOpenHandshake() {
    sock_.writeDatagram(buildDucSpecificPacket(), radioAddr_, kPortDucConfig);
    sendDdcSpecificToRadio(true);
    punchEnabledDdcIqFirewalls();
    scheduleDdc1FirewallPunch();
    sock_.writeDatagram(buildHighPriorityPacket(true), radioAddr_, kPortHpToSdr);
    hpTickCount_ = 0;
    hpTimer_.start();
}

void P2Session::close() {
    if (!open_) return;
    ++openEpoch_;
    wireDriveProvider_ = {};
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
    ddcSpecificSeq_ = 0;
    subIqLatchDdc_ = -1;
    emit logLine(QStringLiteral(
        "P2: session closed (%1 status packets received)").arg(statusCount_));
    emit stopped();
}

void P2Session::sendRunOff(const QHostAddress &ip) {
    if (ip.isNull() || ip.protocol() != QAbstractSocket::IPv4Protocol)
        return;
    // Firmware captures the controller from the General packet's source
    // IP/port. An unbound QUdpSocket often leaves the ham NIC (e.g.
    // 169.254 Brick APIPA) so run=0 never reaches the leftover session.
    // First reuse the last real P2 5-tuple (the port the Brick is still
    // streaming to). Then scatter from every local IPv4:ephemeral.
    QByteArray general(kGeneralLen, char{0});
    general[37] = char{0x08};
    general[38] = char{0x01};
    QByteArray hp(kHpLen, char{0});
    QByteArray ddc(kDdcSpecificLen, char{0});
    ddc[4] = char{2};

    const auto sendThree = [&](QUdpSocket &sock) {
        for (int i = 0; i < 3; ++i) {
            wrBeU32(ddc.data(), static_cast<quint32>(i + 1));
            sock.writeDatagram(general, ip, kPortCommand);
            sock.writeDatagram(ddc, ip, kPortDdcConfig);
            sock.writeDatagram(hp, ip, kPortHpToSdr);
        }
    };

    const auto bindFlags =
        QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint;

    QSettings s;
    const QString lastLip = s.value(QStringLiteral("p2/lastBindIp")).toString();
    const quint16 lastPort =
        static_cast<quint16>(s.value(QStringLiteral("p2/lastBindPort")).toUInt());
    if (lastPort != 0) {
        QHostAddress local;
        if (lastLip.isEmpty() || !local.setAddress(lastLip))
            local = QHostAddress(QHostAddress::AnyIPv4);
        QUdpSocket sock;
        if (sock.bind(local, lastPort, bindFlags) ||
            sock.bind(QHostAddress(QHostAddress::AnyIPv4), lastPort, bindFlags)) {
            sendThree(sock);
            qWarning("[wire] P2 run=0 from remembered %s:%u → %s",
                     qPrintable(sock.localAddress().toString()),
                     sock.localPort(),
                     qPrintable(ip.toString()));
        } else {
            qWarning("[wire] P2 remembered bind %s:%u failed (%s)",
                     qPrintable(local.toString()), lastPort,
                     qPrintable(sock.errorString()));
        }
    }

    constexpr quint16 kHostBindPort = 1025;
    const auto sendFrom = [&](const QHostAddress &local, quint16 port) {
        QUdpSocket sock;
        if (!sock.bind(local, port, bindFlags))
            return;
        sendThree(sock);
    };

    bool any = false;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) ||
            (flags & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &e : iface.addressEntries()) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            sendFrom(e.ip(), kHostBindPort);
            sendFrom(e.ip(), 0);
            any = true;
        }
    }
    if (!any) {
        sendFrom(QHostAddress(QHostAddress::AnyIPv4), kHostBindPort);
        sendFrom(QHostAddress(QHostAddress::AnyIPv4), 0);
    }
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
    const bool transmitting =
        P2TxSafetyGate::evaluate(txIntent_, txSafety_).transmit;
    if (transmitting)
        sock_.writeDatagram(buildDucSpecificPacket(), radioAddr_,
                            kPortDucConfig);
    ++hpTickCount_;
    if (hpTickCount_ % kDdcRefreshTicks == 0) {
        sendDdcSpecificToRadio(false);
        punchEnabledDdcIqFirewalls();
        if (!transmitting)
            sock_.writeDatagram(buildDucSpecificPacket(), radioAddr_,
                                kPortDucConfig);
    }
    if (hpTickCount_ % kGeneralRefreshTicks == 0)
        sock_.writeDatagram(buildGeneralPacket(), radioAddr_, kPortCommand);

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

    // Per-DDC IQ rates (~1 Hz). One-ADC SUB is DDC1 / UDP 1036.
    // Two-ADC SUB is DDC3 / UDP 1038, with RX1 on DDC2 / UDP 1037.
    if (hpTickCount_ % 10 == 0) {
        const bool twoAdc = angeliaRxSlots();
        const int rx2 = rx2DdcIndex();
        const bool subArmed =
            subRx2Wanted_ &&
            ddcEnabled_[static_cast<std::size_t>(rx2)];
        if (subArmed) {
            if (unmatchedIqLogLeft_ < 4)
                unmatchedIqLogLeft_ = 4;
        }
        const quint32 d0 = ddcIqCount_[0];
        const quint32 d1 = ddcIqCount_[1];
        const quint32 d2 = ddcIqCount_[2];
        const quint32 d3 = ddcIqCount_[3];
        ddcIqCount_.fill(0);
        const quint32 subPkts = twoAdc ? d3 : d1;
        const quint32 rx1Pkts = twoAdc ? d2 : d0;
        if (subArmed && subPkts > 0 && !ddc1IqOkLogged_) {
            ddc1IqOkLogged_ = true;
            ddc1IqLogLeft_ = 0;
            emit logLine(QStringLiteral(
                "P2 IQ: DDC0=%1 DDC1=%2 DDC2=%3 DDC3=%4 pkt/s")
                .arg(d0).arg(d1).arg(d2).arg(d3));
        } else if (subArmed && ddc1IqLogLeft_ > 0) {
            --ddc1IqLogLeft_;
            emit logLine(QStringLiteral(
                "P2 IQ: DDC0=%1 DDC1=%2 DDC2=%3 DDC3=%4 pkt/s "
                "(UDP 1035/1036/1037/1038) RX2Hz=%5")
                .arg(d0).arg(d1).arg(d2).arg(d3).arg(ddcFreqHz_[1]));
        }
        if (!warnedNoDdc1Iq_ && running_ && subArmed
            && subPkts == 0 && rx1Pkts > 0
            && statusCount_ >= 25) {
            warnedNoDdc1Iq_ = true;
            const int port = kPortDdcIq0 + rx2;
            emit logLine(QStringLiteral(
                "P2: SUB armed but DDC%1 = 0 pkt/s (radio src %2) while "
                "RX1 is flowing. RX2Hz=%3. The second stream is not "
                "arriving (or Windows is dropping it).")
                .arg(rx2).arg(port).arg(ddcFreqHz_[1]));
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

void P2Session::recoverIdleTx() {
    if (!open_ || !running_)
        return;
    // A rejected key can leave transmit intent set without the radio
    // ever having gone to TX. restartTxTransportRxState will not re-prime
    // while that intent remains, which is the stuck-until-restart latch.
    txIntent_ = {};
    restartTxTransportRxState();
}

void P2Session::restartTxTransportRxState() {
    // Re-prime only while no transmit intent exists (RX rate rebuild, or
    // host unkey after a latched fault). Never clear a fault while keyed.
    if (!open_ || !running_ || txIntent_.transmitRequested)
        return;
    stopTxTransport();
    txSafety_.faultLatched = false;
    txStateDetail_ = QStringLiteral("Re-priming TX transport");
    // PA enable was forced off with the fault. Put it back while MOX is
    // still clear so it can sit in receive before the bridge opens the
    // next key. A key in the same burst as this rising edge latches the
    // radio PA off for the rest of the session.
    sock_.writeDatagram(buildGeneralPacket(), radioAddr_, kPortCommand);
    emit logLine(QStringLiteral(
        "P2 TX: re-priming port 1029 in receive"));
    startTxTransportRxState();
    emitTxState();
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

        ingestRadioDatagram(senderPort, buf);
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
    const auto n = static_cast<std::size_t>(ddc);
    if (bits != 24 || spp != kIqSamplesPerFrame) {
        // Unexpected framing — count it as a stream error and drop.
        ++iqSeqErrors_;
        if (n < ddcIqCount_.size())
            ++ddcIqCount_[n];
        static std::array<bool, 10> s_hdrRejectLogged{};
        if (ddc >= 0 && ddc < 10 && !s_hdrRejectLogged[n]) {
            s_hdrRejectLogged[n] = true;
            emit logLine(QStringLiteral(
                "P2: DDC%1 IQ header reject bits=%2 spp=%3 (want 24 / %4)")
                             .arg(ddc).arg(bits).arg(spp).arg(kIqSamplesPerFrame));
        }
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
        // Count handshake-only IQ so SUB DDC1/2/3 show in the 1 Hz line.
        if (n < ddcIqCount_.size())
            ++ddcIqCount_[n];
        return;
    }
    if (angeliaRxSlots() && ddc == rx1DdcIndex() && !rx1PortLogged_) {
        rx1PortLogged_ = true;
        emit logLine(QStringLiteral(
            "P2: RX1 IQ from radio UDP %1 (DDC%2)")
                         .arg(kPortDdcIq0 + ddc)
                         .arg(ddc));
    }

    if (ddcSeqStarted_[n] && seq != ddcSeqNext_[n]) {
        ++iqSeqErrors_;
        emit logLine(QStringLiteral(
            "P2: DDC%1 seq gap — expected %2 got %3")
            .arg(ddc).arg(ddcSeqNext_[n]).arg(seq));
    }
    ddcSeqStarted_[n] = true;
    ddcSeqNext_[n]    = seq + 1;
    ++iqFrameCount_;
    if (n < ddcIqCount_.size())
        ++ddcIqCount_[n];

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
        // Some firmware ignores receive_specific until run=1/status is
        // live. Re-send with a new sequence (deskHPSDR increments this
        // counter; general/HP stay seq 0).
        sendDdcSpecificToRadio(true);
        sock_.writeDatagram(buildGeneralPacket(), radioAddr_, kPortCommand);
        punchEnabledDdcIqFirewalls();
        scheduleDdc1FirewallPunch();
        sock_.writeDatagram(buildHighPriorityPacket(true), radioAddr_,
                            kPortHpToSdr);
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

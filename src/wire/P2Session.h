// Lyra — HPSDR Protocol 2 control-plane session (Saturn / ANAN G2).
//
// Phase B of the P2 bring-up: establishes and holds a P2 session with
// the radio — general packet, high-priority run-bit keepalive, and the
// radio's 60-byte high-priority status stream back.  NO IQ / audio
// streams yet (those are Phase C/D); this class proves the handshake,
// the controller lease, and two-way traffic on the bench.
//
// References (both verified against KD4YAL's live Saturn G2 bench):
//   * Radio side: KD4YAL Saturn fork, sw_projects/P2_app —
//     protocol2_command.c gives the authoritative field offsets the
//     radio DECODES (our packets must match), OutHighPriority.c gives
//     the status packet the radio SENDS (we parse), p2app.c gives the
//     session state machine (general packet claims the controller
//     lease by source IP; HP run=1 + reply address -> SDR active;
//     run=0 releases; 1 s inactivity timeout).
//   * Client side: Thetis (KD4YAL fork), Project Files/Source/
//     ChannelMaster/network.c — the reference P2 client whose
//     behavior p2app is regression-tested against.
//
// Wire model (matches Thetis): ONE UDP socket carries the whole
// session.  All control goes out radio-bound to fixed destination
// ports (1024 general, 1027 high-priority); every radio->PC stream
// comes back to OUR source port and is demultiplexed by the RADIO's
// source port (1025 = HP status, 1026 = mic, 1035+n = DDC IQ).  The
// radio captures our address from the general packet (reply_addr in
// p2app) — so everything must be sent from this one socket.
//
// Sequence-number rule (Thetis_P2_Compatibility_Regression_Checklist):
// control packets (general, HP) are sent with sequence ALWAYS ZERO —
// only data streams increment.  The radio's control decoders must not
// (and per the checklist do not) dedupe on it.
//
// Packet layouts (offsets per protocol2_command.c / OutHighPriority.c):
//   General to radio, 60 B -> port 1024:
//     [0..3]=seq(0)  [4]=0x00 cmd  [5..22]=stream port overrides
//     (0 = radio defaults)  [23..28]=wideband (off)  [37]=option
//     flags (0 => frequencies in Hz, no timestamp/VITA49)
//     [38] bit0 = hardware watchdog enable  [58] bit0 = PA enable
//     [59] = Alex enable bits
//   High-priority to radio, 1444 B -> port 1027:
//     [0..3]=seq(0)  [4] bit0=run bit1=transmit bit7=PureSignal
//     [5]=CWX  [6..8] MUST be zero (hardened p2app validates)
//     [9+4n..]=DDC n frequency (BE u32, DDS PHASE WORD = Hz × 2^32 /
//     122.88 MHz — the radio hardcodes phase-word decode)
//     [329..332]=DUC phase word  [345]=drive  [1396..7]=client control word
//     [1398..9]=CAT port  [1400]=xvtr/mute/tune  [1401]=OC<<1
//     [1402]=user outputs  [1403]=Mercury ATT  [1428..1435]=Alex
//     TX/RX words  [1442]=ADC2 ATT  [1443]=ADC1 ATT (<= 31)
//   High-priority status from radio, 60 B <- source port 1025
//     (200 ms cadence in RX, 1 ms in TX, immediate on PTT change):
//     [0..3]=seq (increments)  [4]=PTT/key bits  [5]=ADC overflow
//     [6..7]=exciter power  [14..15]=forward power  [22..23]=reverse
//     power  [30]=FIFO over/underflow bits  [31..38]=FIFO depths
//     [39..42]=ADC1/ADC2 peak (Saturn fw >= 27)  [49..50]=supply
//     volts (raw ADC)  [55..58]=user analog  [59]=user I/O bits
//
// Bench safety: Phase B keys NOTHING — transmit bit never set, drive
// level 0, PA enable 0 in the general packet.  The session is RX-only
// control traffic; the hardware watchdog bit is set so the radio
// drops to idle ~1 s after this client dies for any reason.

#pragma once

#include "P2TxSafety.h"

#include <QObject>
#include <QString>
#include <QHostAddress>
#include <QUdpSocket>
#include <QTimer>
#include <array>
#include <cstdint>

namespace lyra::wire {

// G2/Saturn RX input routing.  The G2's BYPS connector is the former
// Alex EXT2 input (bit 10); it is distinct from HPF bypass (bit 12).
enum class P2RxInput : quint8 {
    Trx    = 0,
    Bypass = 1,
    Ext1   = 2,
    Xvtr   = 3,
};

// ---- P2 hardware profile (Thetis "HardwareSpecific.Hardware" analogue) ----
// Thetis branches per radio model at every hardware-behavior site
// (console.cs: HPSDRHW.Saturn → setBPF1ForOrionIISaturn, MkII BPF
// attenuator rules, antenna options…).  Lyra collects the same
// decisions in one struct so additional ANAN models drop in as data.
// First (and default) profile: Saturn / ANAN G2.
//
// The front-end words land in the HP packet's Alex0 fields (bytes
// 1432-33 TX halfword, 1434-35 RX halfword) — p2app forwards them to
// AlexManualTXFilters/AlexManualRXFilters, and p2app.c:1325 hardcodes
// EnableAlexManualFilterSelect(true): the CLIENT fully owns the RF
// front end.  All-zero words = no antenna routed, no filter selected
// = the "connects but only hears its own noise floor" trap (bench
// 2026-07-19).  Bit layout per Thetis network.h:263-302 rbpfilter.
struct P2HardwareProfile {
    const char *modelKey;     // selected marketed model, not board id alone
    const char *name;
    int         boardId;      // P2 discovery reply byte [11]
    int         ddcCount;
    int         adcCount;
    // Alex0 RX halfword (BPF/HPF select, ext-in routing) for a dial
    // frequency, and TX halfword (ANT select + LPF) — trxAnt 1..3.
    quint16 (*alexRxWord)(quint32 hz, P2RxInput input, bool hpfBypass);
    quint16 (*alexTxWord)(quint32 hz, int trxAnt);
};

// The selected/saved marketed model chooses runtime front-end policy.
// Discovery board id remains validation metadata; it cannot distinguish
// every product variant that shares a wire-compatible board.
const P2HardwareProfile *p2ProfileForModel(const QString &modelKey);

class P2Session : public QObject {
    Q_OBJECT

public:
    explicit P2Session(QObject *parent = nullptr);
    ~P2Session() override;

    bool isRunning() const { return running_; }

    // DDC frequency (Hz) — DDC 0..9 on Saturn.  Applied on the next
    // 100 ms HP tick (control cadence, not a hot path).
    void setDdcFrequencyHz(int ddc, quint32 hz);
    void setDucFrequencyHz(quint32 hz) { ducFreqHz_ = hz; }

    // Phase C — enable a DDC's IQ stream.  rateKhz must be one of the
    // Protocol 2 legal rates (48/96/192/384/768/1536; p2app validates
    // and rejects the whole packet otherwise).  Sample size is always
    // 24 bit.  Takes effect on the next DDC-specific send (immediate
    // if the session is open).  Source defaults to ADC1 and is selectable.
    void enableDdc(int ddc, quint16 rateKhz);
    void disableDdc(int ddc);
    void setDdcAdc(int ddc, int adc);
    quint32 iqFrameCount() const { return iqFrameCount_; }
    quint32 iqSeqErrors()  const { return iqSeqErrors_;  }

    // nullptr is an explicit no/unverified-front-end state.
    void setProfile(const P2HardwareProfile *p) { profile_ = p; }
    void setTrxAntenna(int ant);
    void setRxInput(P2RxInput input) { rxInput_ = input; }
    void setHpfBypass(bool on) { hpfBypass_ = on; }
    void setAdcAttenuation(int adc, int db);

    // Golden-packet test seam: exact production encoders, no I/O.
    QByteArray diagnosticHighPriorityPacket(bool run) const {
        return buildHighPriorityPacket(run);
    }
    QByteArray diagnosticDdcSpecificPacket() const {
        return buildDdcSpecificPacket();
    }
    QByteArray diagnosticGeneralPacket() const {
        return buildGeneralPacket();
    }

    // RX-audio return to the RADIO's speaker (G2 on-board amp):
    // 48 kHz stereo int16 → 260 B packets (4 B incrementing seq +
    // 64 L/R pairs, 16-bit BE) → radio port 1028 (p2app InSpkrAudio).
    // MUST be called on the session thread — in practice it is: the
    // engine's radio-audio sink fires on the RX feeder thread, which
    // IS this session's thread while a P2 radio is the live source.
    void sendSpeakerAudio(const qint16 *lr, int nframes);

public slots:
    // Establish the session: bind the session socket, send the
    // general packet (claims the controller lease), start the HP
    // run=1 cadence.  `started` is emitted on the first status
    // packet back from the radio — that's the proof the handshake
    // completed and the radio went active.
    void open(const QString &ip);

    // Tear down: HP run=0 (sent more than once — it's UDP), stop the
    // cadence, close the socket.  Releases the radio's controller
    // lease so another client (Thetis) can claim it immediately.
    void close();

signals:
    void started(QString ip);
    void stopped();
    // One per HP status packet from the radio (raw wire values;
    // scaling to watts/volts is a UI concern and radio-dependent).
    // ain3Raw/ain4Raw = the user-analog words at bytes 57-58 / 55-56 —
    // on MkII/Saturn hardware these carry PA VOLTS (AIN3) and PA
    // CURRENT (AIN4); Thetis converts exactly these (console.cs
    // convertToVolts/convertToAmps via getUserADC0/1).
    // spkrFifoSamples = the radio's speaker-FIFO depth (bytes 37-38)
    // — the closed-loop proof that the outbound speaker stream is
    // being accepted and drained by the codec.
    void statusReceived(quint32 seq, quint8 pttBits, quint8 adcOverflows,
                        quint16 exciterPowerRaw, quint16 fwdPowerRaw,
                        quint16 revPowerRaw, quint16 supplyRaw,
                        quint16 adc1Peak, quint16 adc2Peak,
                        quint16 ain3Raw, quint16 ain4Raw,
                        quint16 spkrFifoSamples);
    // One per DDC IQ frame (radio source port 1035+ddc).  `iqBytes` is
    // the 1428-byte payload: 238 samples x 6 bytes, I then Q, each a
    // big-endian signed 24-bit value (Thetis scales as (b0<<24 | b1<<16
    // | b2<<8) / 2^31 — network.c:591-601).  At 48 kHz this fires
    // ~201.7x/s per DDC — fine on the event loop for the control/bench
    // phase; the Phase D audio path moves consumption to a wire thread.
    void iqFrameReceived(int ddc, quint32 seq, QByteArray iqBytes);
    void logLine(QString line);

private slots:
    void onReadyRead();
    void onHpTick();

private:
    QByteArray buildGeneralPacket() const;
    QByteArray buildHighPriorityPacket(bool run) const;
    QByteArray buildDdcSpecificPacket() const;
    void parseStatus(const QByteArray &d);
    void parseIqFrame(int ddc, const QByteArray &d);

    QUdpSocket   sock_;
    QTimer       hpTimer_;
    QHostAddress radioAddr_;
    QString      radioIp_;
    bool         open_        = false;   // socket up, session being held
    bool         running_     = false;   // radio confirmed via status stream
    quint32      lastStatusSeq_ = 0;
    quint32      statusCount_   = 0;
    int          hpTickCount_   = 0;     // paces the DDC-specific refresh
    std::array<quint32, 10> ddcFreqHz_{};
    quint32      ducFreqHz_  = 14'100'000;
    // Phase C receive-stream config + per-DDC sequence tracking.
    std::array<bool,    10> ddcEnabled_{};
    std::array<quint16, 10> ddcRateKhz_{};
    std::array<quint8,  10> ddcAdc_{};
    std::array<quint32, 10> ddcSeqNext_{};
    std::array<bool,    10> ddcSeqStarted_{};
    quint32      iqFrameCount_ = 0;
    quint32      iqSeqErrors_  = 0;
    const P2HardwareProfile *profile_ = nullptr;
    int          trxAntenna_   = 1;               // ANT1..3
    P2RxInput    rxInput_      = P2RxInput::Trx;
    bool         hpfBypass_    = false;
    std::array<quint8, 2> adcAttenuation_{};
    quint32      spkrSeq_      = 0;               // speaker stream sequence
    QByteArray   spkrStage_;                      // partial-packet staging
    // TX remains wire-inert in this phase: no public API can raise any of
    // these prerequisites or intents. Keeping the gate in the production
    // packet builders makes later TX integration fail closed by default.
    P2TxIntent       txIntent_;
    P2TxSafetyInputs txSafety_;

    static constexpr quint16 kPortCommand    = 1024;  // general/discovery
    static constexpr quint16 kPortDdcConfig  = 1025;  // DDC-specific -> radio
    static constexpr quint16 kPortHpToSdr    = 1027;
    static constexpr quint16 kPortHpFromSdr  = 1025;  // radio SOURCE port
    static constexpr quint16 kPortSpkrToSdr  = 1028;  // speaker audio -> radio
    static constexpr quint16 kPortDdcIq0     = 1035;  // radio SOURCE, +ddc
    static constexpr int     kNumDdc         = 10;
    static constexpr int     kSpkrFrames     = 64;    // frames per packet
    static constexpr int     kSpkrPktLen     = 260;   // 4 seq + 64*4 bytes
    static constexpr int     kGeneralLen     = 60;
    static constexpr int     kHpLen          = 1444;
    static constexpr int     kDdcSpecificLen = 1444;
    static constexpr int     kStatusLen      = 60;
    static constexpr int     kIqFrameLen     = 1444;
    static constexpr int     kIqHeaderLen    = 16;    // seq4 + ts8 + bits2 + count2
    static constexpr int     kIqSamplesPerFrame = 238;
    // HP cadence: Thetis-like periodic refresh.  Must stay well under
    // p2app's 1 s activity timeout; 100 ms gives 10x margin.
    static constexpr int     kHpPeriodMs     = 100;
    // DDC-specific refresh every N HP ticks (5 s).  Dual purpose: the
    // config resend is Thetis-compatible control behavior, AND it
    // keeps the host firewall's UDP flow state for radio:1025 open so
    // the status stream (radio SOURCE port 1025 -> us) keeps passing
    // without an inbound firewall rule.  Windows expires UDP flow
    // state at ~60 s; 5 s is a 12x margin.  Bench-proven 2026-07-18:
    // without any traffic TO :1025, zero status packets arrive on a
    // default-firewall Windows host even though the radio is sending.
    static constexpr int     kDdcRefreshTicks = 50;
};

} // namespace lyra::wire

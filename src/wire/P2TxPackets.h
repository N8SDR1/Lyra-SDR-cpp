// Pure Protocol 2 TX packet encoders. No sockets and no radio state.
//
// References:
//   Thetis ChannelMaster/network.c: CmdTx(), sendOutbound(), WriteUDPFrame()
//   Saturn P2_app: IncomingDUCSpecific.c and InDUCIQ.c

#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <array>

namespace lyra::wire {

struct P2DucConfig {
    quint8 dacCount = 1;

    bool eer = false;
    bool cwEnabled = false;
    bool reverseKeys = false;
    bool iambic = false;
    bool sidetone = false;
    bool modeB = false;
    bool strictSpacing = false;
    bool breakIn = false;

    quint8 sidetoneLevel = 0;
    quint16 sidetoneFrequencyHz = 0;
    quint8 keyerSpeedWpm = 0;
    quint8 keyerWeight = 0;
    quint16 hangDelayMs = 0;
    quint8 rfDelayMs = 0;
    quint16 sampleRateKhz = 192;
    quint8 edgeLengthMs = 7;
    quint16 phaseShiftDegrees = 0;

    bool lineInput = false;
    bool micBoost = false;
    bool disableOrionPtt = false;
    bool swapOrionTipRing = false;
    bool micBias = false;
    bool balancedInput = false;
    quint8 lineInGain = 0;

    // ADC0, ADC1, ADC2. The safe Thetis default is maximum attenuation.
    std::array<quint8, 3> txAttenuationDb{{31, 31, 31}};
};

struct P2TxIqSample {
    double i = 0.0;
    double q = 0.0;
};

class P2TxPackets final {
public:
    static constexpr int kDucSpecificLength = 60;
    static constexpr int kIqSamplesPerPacket = 240;
    static constexpr int kIqPacketLength = 1444;

    static QByteArray encodeDucSpecific(const P2DucConfig &config);

    // Saturn's InDUCIQ swaps the two 24-bit wire components before DMA.
    // Therefore the P2 wire packet is Q then I for each logical {I,Q} sample.
    static QByteArray encodeIq(
        quint32 sequence,
        const std::array<P2TxIqSample, kIqSamplesPerPacket> &samples);
};

} // namespace lyra::wire

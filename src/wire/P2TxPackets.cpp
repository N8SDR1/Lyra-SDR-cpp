#include "P2TxPackets.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace lyra::wire {
namespace {

void putBe16(QByteArray &packet, int offset, quint16 value) {
    qToBigEndian<quint16>(
        value, reinterpret_cast<uchar *>(packet.data() + offset));
}

void putBe32(QByteArray &packet, int offset, quint32 value) {
    qToBigEndian<quint32>(
        value, reinterpret_cast<uchar *>(packet.data() + offset));
}

void putSample24(QByteArray &packet, int offset, double sample) {
    const double limited = std::clamp(sample, -1.0, 1.0);
    // Matches Thetis: +/-1.0 maps to +/-8388607 with half-away-from-zero
    // rounding. Keep the low 24 bits for the signed two's-complement wire.
    const qint32 value =
        static_cast<qint32>(std::lround(limited * 8'388'607.0));
    packet[offset]     = static_cast<char>((value >> 16) & 0xFF);
    packet[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
    packet[offset + 2] = static_cast<char>(value & 0xFF);
}

} // namespace

QByteArray P2TxPackets::encodeDucSpecific(const P2DucConfig &config) {
    QByteArray packet(kDucSpecificLength, char{0});

    packet[4] = static_cast<char>(config.dacCount);
    quint8 mode = 0;
    mode |= config.eer           ? 0x01 : 0;
    mode |= config.cwEnabled     ? 0x02 : 0;
    mode |= config.reverseKeys   ? 0x04 : 0;
    mode |= config.iambic        ? 0x08 : 0;
    mode |= config.sidetone      ? 0x10 : 0;
    mode |= config.modeB         ? 0x20 : 0;
    mode |= config.strictSpacing ? 0x40 : 0;
    mode |= config.breakIn       ? 0x80 : 0;
    packet[5] = static_cast<char>(mode);
    packet[6] = static_cast<char>(config.sidetoneLevel);
    putBe16(packet, 7, config.sidetoneFrequencyHz);
    packet[9] = static_cast<char>(config.keyerSpeedWpm);
    packet[10] = static_cast<char>(config.keyerWeight);
    putBe16(packet, 11, config.hangDelayMs);
    packet[13] = static_cast<char>(config.rfDelayMs);
    putBe16(packet, 14, config.sampleRateKhz);
    packet[17] = static_cast<char>(config.edgeLengthMs);
    putBe16(packet, 26,
            static_cast<quint16>(config.phaseShiftDegrees % 360));

    quint8 mic = 0;
    mic |= config.lineInput        ? 0x01 : 0;
    mic |= config.micBoost         ? 0x02 : 0;
    mic |= config.disableOrionPtt  ? 0x04 : 0;
    mic |= config.swapOrionTipRing ? 0x08 : 0;
    mic |= config.micBias          ? 0x10 : 0;
    mic |= config.balancedInput    ? 0x20 : 0;
    packet[50] = static_cast<char>(mic);
    packet[51] = static_cast<char>(config.lineInGain);

    packet[57] = static_cast<char>(
        std::min<quint8>(config.txAttenuationDb[2], 31));
    packet[58] = static_cast<char>(
        std::min<quint8>(config.txAttenuationDb[1], 31));
    packet[59] = static_cast<char>(
        std::min<quint8>(config.txAttenuationDb[0], 31));
    return packet;
}

QByteArray P2TxPackets::encodeIq(
    quint32 sequence,
    const std::array<P2TxIqSample, kIqSamplesPerPacket> &samples)
{
    QByteArray packet(kIqPacketLength, char{0});
    putBe32(packet, 0, sequence);
    for (int n = 0; n < kIqSamplesPerPacket; ++n) {
        const int offset = 4 + n * 6;
        putSample24(packet, offset, samples[static_cast<std::size_t>(n)].q);
        putSample24(packet, offset + 3,
                    samples[static_cast<std::size_t>(n)].i);
    }
    return packet;
}

} // namespace lyra::wire

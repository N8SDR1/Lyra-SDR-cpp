#include "wire/P2Session.h"
#include "wire/P2TxFifo.h"
#include "wire/P2TxPackets.h"
#include "wire/P2TxSafety.h"
#include "wire/P2TxWriter.h"

#include <QCoreApplication>
#include <QtEndian>

#include <array>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

bool expect(bool condition, const char *what) {
    if (condition) return true;
    std::fprintf(stderr, "FAIL: %s\n", what);
    return false;
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using namespace lyra::wire;

    bool ok = true;

    const P2TxIntent hostileIntent{true, true, 999};
    P2TxSafetyInputs safety;
    auto state = P2TxSafetyGate::evaluate(hostileIntent, safety);
    ok &= expect(!state.ready && !state.transmit && !state.paEnabled &&
                     state.drive == 0,
                 "default safety state fails closed");

    safety.operatorArmed = true;
    safety.sessionRunning = true;
    safety.iqPrimed = true;
    safety.telemetryHealthy = true;
    safety.watchdogEnabled = true;
    state = P2TxSafetyGate::evaluate(hostileIntent, safety);
    ok &= expect(state.ready && state.transmit && state.paEnabled &&
                     state.drive == 255,
                 "healthy armed state clamps and passes TX intent");

    const std::array<bool P2TxSafetyInputs::*, 5> prerequisites{{
        &P2TxSafetyInputs::operatorArmed,
        &P2TxSafetyInputs::sessionRunning,
        &P2TxSafetyInputs::iqPrimed,
        &P2TxSafetyInputs::telemetryHealthy,
        &P2TxSafetyInputs::watchdogEnabled,
    }};
    for (auto member : prerequisites) {
        P2TxSafetyInputs missing = safety;
        missing.*member = false;
        const auto blocked = P2TxSafetyGate::evaluate(hostileIntent, missing);
        ok &= expect(!blocked.transmit && !blocked.paEnabled &&
                         blocked.drive == 0,
                     "each missing prerequisite blocks RF");
    }
    safety.faultLatched = true;
    state = P2TxSafetyGate::evaluate(hostileIntent, safety);
    ok &= expect(!state.transmit && !state.paEnabled && state.drive == 0,
                 "latched fault blocks RF");
    safety.faultLatched = false;
    state = P2TxSafetyGate::evaluate(P2TxIntent{true, false, 200}, safety);
    ok &= expect(state.transmit && !state.paEnabled && state.drive == 0,
                 "PA-disabled state forces zero drive");

    P2DucConfig duc;
    duc.dacCount = 1;
    duc.eer = true;
    duc.cwEnabled = true;
    duc.reverseKeys = true;
    duc.iambic = true;
    duc.sidetone = true;
    duc.modeB = true;
    duc.strictSpacing = true;
    duc.breakIn = true;
    duc.sidetoneLevel = 0x23;
    duc.sidetoneFrequencyHz = 600;
    duc.keyerSpeedWpm = 25;
    duc.keyerWeight = 50;
    duc.hangDelayMs = 300;
    duc.rfDelayMs = 12;
    duc.sampleRateKhz = 192;
    duc.edgeLengthMs = 7;
    duc.phaseShiftDegrees = 361;
    duc.lineInput = true;
    duc.micBoost = true;
    duc.disableOrionPtt = true;
    duc.swapOrionTipRing = true;
    duc.micBias = true;
    duc.balancedInput = true;
    duc.lineInGain = 0x34;
    duc.txAttenuationDb = {{12, 20, 99}};

    QByteArray expectedDuc(P2TxPackets::kDucSpecificLength, char{0});
    expectedDuc[4] = char{1};
    expectedDuc[5] = static_cast<char>(0xFF);
    expectedDuc[6] = char{0x23};
    qToBigEndian<quint16>(
        600, reinterpret_cast<uchar *>(expectedDuc.data() + 7));
    expectedDuc[9] = char{25};
    expectedDuc[10] = char{50};
    qToBigEndian<quint16>(
        300, reinterpret_cast<uchar *>(expectedDuc.data() + 11));
    expectedDuc[13] = char{12};
    qToBigEndian<quint16>(
        192, reinterpret_cast<uchar *>(expectedDuc.data() + 14));
    expectedDuc[17] = char{7};
    qToBigEndian<quint16>(
        1, reinterpret_cast<uchar *>(expectedDuc.data() + 26));
    expectedDuc[50] = char{0x3F};
    expectedDuc[51] = char{0x34};
    expectedDuc[57] = char{31};
    expectedDuc[58] = char{20};
    expectedDuc[59] = char{12};
    ok &= expect(P2TxPackets::encodeDucSpecific(duc) == expectedDuc,
                 "DUC-specific packet matches Thetis/Saturn golden bytes");

    std::array<P2TxIqSample, P2TxPackets::kIqSamplesPerPacket> iq{};
    iq[0] = P2TxIqSample{1.0, -1.0};
    iq[1] = P2TxIqSample{0.5, -0.5};
    iq[2] = P2TxIqSample{2.0, -2.0};
    const QByteArray iqPacket = P2TxPackets::encodeIq(0x01020304u, iq);
    ok &= expect(iqPacket.size() == P2TxPackets::kIqPacketLength,
                 "TX IQ packet is exactly 1444 bytes");
    const QByteArray expectedPrefix = QByteArray::fromHex(
        "01020304"
        "8000017fffff"
        "c00000400000"
        "8000017fffff");
    ok &= expect(iqPacket.first(expectedPrefix.size()) == expectedPrefix,
                 "TX IQ sequence, Q/I order, rounding, and saturation match");
    ok &= expect(iqPacket.mid(expectedPrefix.size()) ==
                     QByteArray(iqPacket.size() - expectedPrefix.size(),
                                char{0}),
                 "unused TX IQ samples remain zero");

    QList<QByteArray> pacedPackets;
    bool pacingFault = false;
    P2TxWriter writer;
    writer.setPacketSink(
        [&pacedPackets](const QByteArray &packet) {
            pacedPackets.append(packet);
        });
    writer.setFaultSink([&pacingFault]() { pacingFault = true; });
    writer.startZeroPriming();
    ok &= expect(pacedPackets.size() == 1 && writer.nextSequence() == 1,
                 "writer primes sequence zero immediately");
    ok &= expect(writer.produceDueForElapsedNs(1'249'999) == 0,
                 "writer waits for the 1.25 ms frame boundary");
    ok &= expect(writer.produceDueForElapsedNs(1'250'000) == 1 &&
                     pacedPackets.size() == 2 &&
                     qFromBigEndian<quint32>(
                         reinterpret_cast<const uchar *>(
                             pacedPackets[1].constData())) == 1,
                 "writer emits sequence one at the next frame boundary");
    writer.stop();

    pacedPackets.clear();
    pacingFault = false;
    writer.startZeroPriming();
    ok &= expect(writer.produceDueForElapsedNs(20'000'000) == 0 &&
                     pacingFault && !writer.isRunning() &&
                     writer.droppedPackets() > 0,
                 "writer stops and faults instead of flooding after a stall");

    static_assert(P2TxWriter::kSampleRateHz ==
                  P2TxFifo::kSampleRateHz);
    ok &= expect(P2TxFifo::kPacketSamples * 800 ==
                     P2TxFifo::kSampleRateHz,
                 "192 kHz / 240 samples establishes 800 packets/s");

    P2TxFifo fifo;
    std::vector<double> twoPackets(
        2 * 2 * P2TxFifo::kPacketSamples);
    for (std::size_t n = 0; n < 2 * P2TxFifo::kPacketSamples; ++n) {
        twoPackets[2 * n] = static_cast<double>(n + 1) / 1000.0;
        twoPackets[2 * n + 1] = -static_cast<double>(n + 1) / 1000.0;
    }
    ok &= expect(fifo.pushInterleaved(
                     twoPackets.data(), 2 * P2TxFifo::kPacketSamples),
                 "FIFO accepts two complete CMaster IQ packets");

    pacedPackets.clear();
    pacingFault = false;
    writer.setInputFifo(&fifo);
    writer.startFromInput();
    std::array<P2TxIqSample, P2TxFifo::kPacketSamples> first{};
    std::array<P2TxIqSample, P2TxFifo::kPacketSamples> second{};
    for (std::size_t n = 0; n < P2TxFifo::kPacketSamples; ++n) {
        first[n] = {twoPackets[2 * n], twoPackets[2 * n + 1]};
        const std::size_t m = P2TxFifo::kPacketSamples + n;
        second[n] = {twoPackets[2 * m], twoPackets[2 * m + 1]};
    }
    ok &= expect(writer.isRunning() && pacedPackets.size() == 1 &&
                     pacedPackets[0] == P2TxPackets::encodeIq(0, first) &&
                     fifo.size() == P2TxFifo::kPacketSamples,
                 "writer consumes sequence zero with logical I/Q ordering");
    ok &= expect(writer.produceDueForElapsedNs(1'250'000) == 1 &&
                     pacedPackets.size() == 2 &&
                     pacedPackets[1] == P2TxPackets::encodeIq(1, second) &&
                     fifo.size() == 0,
                 "writer consumes sequence one at the 800 Hz cadence");
    writer.stop();

    fifo.reset();
    std::vector<double> shortBlock(
        2 * (P2TxFifo::kPacketSamples - 1), 0.125);
    ok &= expect(fifo.pushInterleaved(
                     shortBlock.data(), P2TxFifo::kPacketSamples - 1),
                 "FIFO accepts a short producer block");
    pacedPackets.clear();
    pacingFault = false;
    writer.startFromInput();
    ok &= expect(pacingFault && !writer.isRunning() &&
                     pacedPackets.isEmpty() &&
                     fifo.underflowEvents() == 1,
                 "writer faults without emitting a packet on underrun");

    fifo.reset();
    std::vector<double> fullBlock(
        2 * P2TxFifo::kCapacitySamples, 0.25);
    const double extra[2] = {0.5, -0.5};
    ok &= expect(fifo.pushInterleaved(
                     fullBlock.data(), P2TxFifo::kCapacitySamples) &&
                     !fifo.pushInterleaved(extra, 1) &&
                     fifo.size() == P2TxFifo::kCapacitySamples &&
                     fifo.overflowEvents() == 1 &&
                     fifo.droppedSamples() == 1,
                 "FIFO rejects overflow atomically and records the drop");

    // Exercise the exact raw callback shape installed into CMaster's ILV.
    auto &producerFifo = p2TxInputFifo();
    producerFifo.reset();
    setP2TxInputEnabled(false);
    p2TxOutbound(1, static_cast<int>(P2TxFifo::kPacketSamples),
                 twoPackets.data());
    setP2TxInputEnabled(true);
    p2TxOutbound(0, static_cast<int>(P2TxFifo::kPacketSamples),
                 twoPackets.data());
    p2TxOutbound(1, static_cast<int>(P2TxFifo::kPacketSamples),
                 twoPackets.data());
    std::array<P2TxIqSample, P2TxFifo::kPacketSamples> routed{};
    ok &= expect(producerFifo.size() == P2TxFifo::kPacketSamples &&
                     producerFifo.popPacket(routed) &&
                     routed[0].i == twoPackets[0] &&
                     routed[0].q == twoPackets[1],
                 "CMaster seam accepts only enabled transmitter id 1");
    setP2TxInputEnabled(false);
    producerFifo.reset();

    fifo.reset();
    const double nonFinite[2] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
    };
    std::vector<double> sanitized(
        2 * P2TxFifo::kPacketSamples, 0.0);
    sanitized[0] = nonFinite[0];
    sanitized[1] = nonFinite[1];
    fifo.pushInterleaved(sanitized.data(), P2TxFifo::kPacketSamples);
    ok &= expect(fifo.popPacket(routed) &&
                     routed[0].i == 0.0 && routed[0].q == 0.0,
                 "FIFO sanitizes non-finite DSP samples to zero");

    P2Session session;
    const QByteArray general = session.diagnosticGeneralPacket();
    const QByteArray hp = session.diagnosticHighPriorityPacket(true);
    ok &= expect(static_cast<quint8>(general[38]) == 0x01 &&
                     static_cast<quint8>(general[58]) == 0 &&
                     static_cast<quint8>(hp[4]) == 0x01 &&
                     static_cast<quint8>(hp[345]) == 0,
                 "production session keeps watchdog on and RF off");

    std::printf(ok ? "PASS: P2 TX safety and golden packets\n"
                   : "FAIL: P2 TX safety and golden packets\n");
    return ok ? 0 : 1;
}

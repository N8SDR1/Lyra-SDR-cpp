#include "wire/P2Session.h"
#include "wire/P2TxPackets.h"
#include "wire/P2TxSafety.h"

#include <QCoreApplication>
#include <QtEndian>

#include <array>
#include <cstdio>

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

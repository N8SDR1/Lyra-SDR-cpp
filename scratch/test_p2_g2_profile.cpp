#include "wire/P2Session.h"

#include <QCoreApplication>
#include <QtEndian>
#include <cstdio>

namespace {

void putBe32(QByteArray &b, int off, quint32 v) {
    qToBigEndian<quint32>(v,
        reinterpret_cast<uchar *>(b.data() + off));
}

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
    const auto *g2 = p2ProfileForModel(QStringLiteral("ANAN-G2"));
    ok &= expect(g2 != nullptr, "ANAN-G2 resolves to a P2 profile");
    ok &= expect(p2ProfileForModel(QStringLiteral("BRICK-SDR2")) == nullptr,
                 "unverified/non-Alex model does not inherit Saturn");

    P2Session session;
    session.setProfile(g2);
    session.setDdcFrequencyHz(0, 7'100'000u);
    session.setDucFrequencyHz(14'100'000u);
    session.setTrxAntenna(2);
    session.setRxInput(P2RxInput::Ext1);
    session.setAdcAttenuation(0, 12);
    session.setAdcAttenuation(1, 7);

    QByteArray expectedHp(1444, char{0});
    expectedHp[4] = char{0x01};
    putBe32(expectedHp, 9, 248'162'987u); // 7.1 MHz phase word
    for (int ddc = 1; ddc < 10; ++ddc)
        putBe32(expectedHp, 9 + 4 * ddc, 492'830'720u);
    putBe32(expectedHp, 329, 492'830'720u);
    expectedHp[1432] = char{0x02}; // TX uses DUC 14.1 MHz: ANT2 + 30/20 LPF
    expectedHp[1433] = char{0x10};
    expectedHp[1434] = char{0x02}; // EXT1 + 9.5 MHz HPF = 0x0210
    expectedHp[1435] = char{0x10};
    expectedHp[1442] = char{7};
    expectedHp[1443] = char{12};
    ok &= expect(session.diagnosticHighPriorityPacket(true) == expectedHp,
                 "G2 high-priority packet matches golden bytes");

    const auto rxWordAt = [&session](quint32 hz) {
        session.setDdcFrequencyHz(0, hz);
        const QByteArray packet = session.diagnosticHighPriorityPacket(true);
        return static_cast<quint16>(
            (static_cast<quint8>(packet[1434]) << 8) |
            static_cast<quint8>(packet[1435]));
    };
    ok &= expect(rxWordAt(1'499'999u) == 0x1200,
                 "G2 RX below 1.5 MHz uses EXT1 plus HPF bypass");
    ok &= expect(rxWordAt(1'500'000u) == 0x0240,
                 "G2 RX 1.5 MHz uses the 1.5 MHz HPF");
    ok &= expect(rxWordAt(2'100'000u) == 0x0220,
                 "G2 RX 2.1 MHz switches to the 6.5 MHz HPF");
    ok &= expect(rxWordAt(5'500'000u) == 0x0210,
                 "G2 RX 5.5 MHz switches to the 9.5 MHz HPF");
    ok &= expect(rxWordAt(11'000'000u) == 0x0202,
                 "G2 RX 11 MHz switches to the 13 MHz HPF");
    ok &= expect(rxWordAt(22'000'000u) == 0x0204,
                 "G2 RX 22 MHz switches to the 20 MHz HPF");
    ok &= expect(rxWordAt(35'000'000u) == 0x0208,
                 "G2 RX 35 MHz switches to the 6 m BPF");

    const auto txWordAt = [&session](quint32 hz) {
        session.setDucFrequencyHz(hz);
        const QByteArray packet = session.diagnosticHighPriorityPacket(true);
        return static_cast<quint16>(
            (static_cast<quint8>(packet[1432]) << 8) |
            static_cast<quint8>(packet[1433]));
    };
    ok &= expect(txWordAt(2'500'000u) == 0x0280,
                 "G2 TX 2.5 MHz uses the 160 m LPF and ANT2");
    ok &= expect(txWordAt(8'000'000u) == 0x0220,
                 "G2 TX 8 MHz uses the 60/40 m LPF and ANT2");
    ok &= expect(txWordAt(24'000'000u) == 0x8200,
                 "G2 TX 24 MHz uses the 17/15 m LPF and ANT2");
    ok &= expect(txWordAt(35'600'001u) == 0x2200,
                 "G2 TX above 35.6 MHz uses the 6 m LPF and ANT2");

    session.setDdcAdc(0, 1);
    session.enableDdc(0, 192);
    QByteArray expectedDdc(1444, char{0});
    expectedDdc[4] = char{2};
    expectedDdc[7] = char{1};
    expectedDdc[17] = char{1};
    expectedDdc[18] = char{0};
    expectedDdc[19] = static_cast<char>(192);
    expectedDdc[22] = char{24};
    ok &= expect(session.diagnosticDdcSpecificPacket() == expectedDdc,
                 "G2 DDC-specific packet matches golden bytes");

    session.setRxInput(P2RxInput::Bypass);
    session.setHpfBypass(true);
    const QByteArray bypass = session.diagnosticHighPriorityPacket(true);
    const quint16 rxWord =
        (static_cast<quint8>(bypass[1434]) << 8) |
         static_cast<quint8>(bypass[1435]);
    ok &= expect(rxWord == 0x1400,
                 "BYPS connector and HPF bypass encode independently");

    P2Session unverified;
    const QByteArray noProfile =
        unverified.diagnosticHighPriorityPacket(true);
    ok &= expect(noProfile.mid(1432, 4) == QByteArray(4, char{0}),
                 "unverified model emits no Saturn Alex words");

    std::printf(ok ? "PASS: P2 G2 golden packets\n"
                   : "FAIL: P2 G2 golden packets\n");
    return ok ? 0 : 1;
}

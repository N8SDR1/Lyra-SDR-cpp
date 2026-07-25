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
    expectedHp[1432] = char{0x02}; // ANT2 + 60/40 LPF = 0x0220
    expectedHp[1433] = char{0x20};
    expectedHp[1434] = char{0x02}; // EXT1 + 6.5 MHz HPF = 0x0220
    expectedHp[1435] = char{0x20};
    expectedHp[1442] = char{7};
    expectedHp[1443] = char{12};
    ok &= expect(session.diagnosticHighPriorityPacket(true) == expectedHp,
                 "G2 high-priority packet matches golden bytes");

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

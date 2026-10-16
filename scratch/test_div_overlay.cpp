#include "wire/P2Session.h"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

namespace {

bool expect(bool condition, const char *what) {
    if (condition) return true;
    std::fprintf(stderr, "FAIL: %s\n", what);
    return false;
}

quint16 maskOf(const QByteArray &pkt) {
    return static_cast<quint8>(pkt[7]) |
           (static_cast<quint16>(static_cast<quint8>(pkt[8])) << 8);
}

quint16 rateAt(const QByteArray &pkt, int slot) {
    const int off = 17 + 6 * slot;
    return (static_cast<quint16>(static_cast<quint8>(pkt[off + 1])) << 8) |
           static_cast<quint8>(pkt[off + 2]);
}

bool sameWord(const QByteArray &pkt, int a, int b) {
    return pkt.mid(a, 4) == pkt.mid(b, 4);
}

} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using namespace lyra::wire;

    bool ok = true;
    const auto *brick3 = p2ProfileForModel(QStringLiteral("BRICK3-SDR"));
    const auto *anan100 = p2ProfileForModel(QStringLiteral("ANAN-100D"));
    const auto *brick2 = p2ProfileForModel(QStringLiteral("BRICK-SDR"));
    ok &= expect(brick3 && brick3->adcCount == 2 && brick3->angeliaRxDdcSlots,
                 "Brick3 profile is two-ADC Angelia class");
    ok &= expect(anan100 && anan100->adcCount == 2 && anan100->angeliaRxDdcSlots,
                 "100D profile is two-ADC Angelia class");
    ok &= expect(brick2 && brick2->adcCount == 1 && !brick2->angeliaRxDdcSlots,
                 "Brick2 profile is one ADC");

    P2Session s;
    s.setProfile(brick3);
    s.enablePrimaryRx(192);
    s.setDdcFrequencyHz(0, 14000000);
    s.setDdcFrequencyHz(1, 7100000);
    s.setAdcAttenuation(0, 10);
    s.setAdcAttenuation(1, 20);

    ok &= expect(!s.diversityArmed() && !s.diversityOverlayLive(),
                 "diversity starts disarmed");
    ok &= expect(s.rx1DdcIndex() == 2, "unarmed Angelia RX1 is DDC2");
    const QByteArray stock = s.diagnosticDdcSpecificPacket();
    ok &= expect(maskOf(stock) == 0x0004, "unarmed enable is DDC2 only");
    ok &= expect(static_cast<quint8>(stock[1363]) == 0,
                 "unarmed sync byte stays 0");
    ok &= expect(stock[5] == 0 && stock[6] == 0,
                 "dither and random stay the current RX1 bytes");

    s.armSubSecondaryDdcs(192, 7100000);
    const QByteArray subHp = s.diagnosticHighPriorityPacket(false);
    ok &= expect(!sameWord(subHp, 9, 13),
                 "SUB puts a different frequency on DDC1");

    s.setDiversityArmed(true);
    ok &= expect(s.diversityArmed() && s.diversityOverlayLive(),
                 "armed RX overlay is live");
    ok &= expect(s.rx1DdcIndex() == 0, "overlay RX1 follows DDC0");
    ok &= expect(!s.shouldFeedRx2(3), "overlay does not feed a second receiver");
    const QByteArray div = s.diagnosticDdcSpecificPacket();
    ok &= expect(maskOf(div) == 0x0001, "overlay enable is DDC0 only");
    ok &= expect(maskOf(div) != 0x0003 && maskOf(div) != 0x000F,
                 "overlay is never 0x03 or 0x0F");
    ok &= expect(static_cast<quint8>(div[1363]) == 0x02, "overlay sync is 0x02");
    ok &= expect(static_cast<quint8>(div[17]) == 0, "DDC0 source is ADC0");
    ok &= expect(static_cast<quint8>(div[23]) == 1, "DDC1 source is ADC1");
    ok &= expect(rateAt(div, 0) == 192 && rateAt(div, 0) == rateAt(div, 1),
                 "both diversity DDCs use the RX1 rate");
    ok &= expect(div[5] == 0 && div[6] == 0,
                 "overlay copies the current RX1 dither and random");
    const QByteArray divHp = s.diagnosticHighPriorityPacket(false);
    ok &= expect(sameWord(divHp, 9, 13),
                 "DDC0 and DDC1 phase words are both RX1");
    ok &= expect(sameWord(divHp, 9, 17), "DDC2 phase word stays RX1");
    ok &= expect(static_cast<quint8>(divHp[1442]) == 10 &&
                     static_cast<quint8>(divHp[1443]) == 10,
                 "ADC1 attenuation follows ADC0 while the overlay is live");

    s.diagnosticArmHealthyTx(true, true, 20);
    ok &= expect(!s.diversityOverlayLive() && s.diversityArmed(),
                 "transmit drops the overlay and keeps the arm");
    ok &= expect(s.rx1DdcIndex() == 2, "transmit restores the Angelia RX1 port");
    const QByteArray tx = s.diagnosticDdcSpecificPacket();
    ok &= expect(maskOf(tx) == 0x0004 && static_cast<quint8>(tx[1363]) == 0,
                 "transmit packet is the stock RX map");

    s.diagnosticArmHealthyTx(false, false, 0);
    ok &= expect(s.diversityOverlayLive(), "unkey restores the overlay");

    s.setPureSignalArmed(true);
    s.diagnosticArmHealthyTx(true, true, 20);
    ok &= expect(s.isPureSignalLive() && !s.diversityOverlayLive(),
                 "live PureSignal drops the overlay");
    const QByteArray ps = s.diagnosticDdcSpecificPacket();
    ok &= expect(maskOf(ps) == 0x0001 && static_cast<quint8>(ps[1363]) == 0x02,
                 "PureSignal keeps its own DDC0 sync packet");
    ok &= expect(static_cast<quint8>(ps[17]) == 0 &&
                     static_cast<quint8>(ps[23]) == brick3->adcCount,
                 "PureSignal DDC1 source is the feedback ADC, not ADC1");
    ok &= expect(rateAt(ps, 0) == 192 && rateAt(ps, 1) == 192,
                 "PureSignal pair stays at 192 kHz");

    s.setPureSignalArmed(false);
    s.diagnosticArmHealthyTx(false, false, 0);
    s.setDiversityArmed(false);
    ok &= expect(!s.diversityOverlayLive() && s.rx1DdcIndex() == 2,
                 "disarm returns to the stock Angelia map");
    ok &= expect(maskOf(s.diagnosticDdcSpecificPacket()) == 0x0004,
                 "disarm enable is DDC2 again");

    s.setDiversityArmed(true);
    s.armSubSecondaryDdcs(192, 7100000);
    ok &= expect(!s.diversityArmed() && !s.diversityOverlayLive(),
                 "arming SUB clears diversity");
    ok &= expect(static_cast<quint8>(s.diagnosticDdcSpecificPacket()[1363]) == 0,
                 "SUB packet is not the diversity sync");

    P2Session one;
    one.setProfile(brick2);
    one.enablePrimaryRx(48);
    const QByteArray before = one.diagnosticDdcSpecificPacket();
    one.setDiversityArmed(true);
    ok &= expect(one.diversityArmed() && !one.diversityOverlayLive(),
                 "one-ADC arm does not go live");
    ok &= expect(one.rx1DdcIndex() == 0, "one-ADC RX1 stays DDC0");
    ok &= expect(one.diagnosticDdcSpecificPacket() == before,
                 "one-ADC arm does not change the packet");

    P2Session hundred;
    hundred.setProfile(anan100);
    hundred.enablePrimaryRx(96);
    hundred.setDiversityArmed(true);
    const QByteArray d100 = hundred.diagnosticDdcSpecificPacket();
    ok &= expect(hundred.diversityOverlayLive() && maskOf(d100) == 0x0001 &&
                     static_cast<quint8>(d100[1363]) == 0x02 &&
                     static_cast<quint8>(d100[23]) == 1 &&
                     rateAt(d100, 0) == 96 && rateAt(d100, 1) == 96,
                 "100D uses the same 0x01 overlay as Brick3");

    if (!ok) {
        std::fprintf(stderr, "DIV overlay tests failed\n");
        return 1;
    }
    std::printf("DIV overlay tests passed\n");
    return 0;
}

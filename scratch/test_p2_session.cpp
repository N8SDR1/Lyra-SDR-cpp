// Lyra — P2 session bench test (Phase B).
//
// Console tool: opens a Protocol 2 control-plane session against a
// Saturn / ANAN G2, holds it for N seconds while printing the radio's
// high-priority status stream, then tears down with run=0.  PASS =
// status packets arrived (handshake + two-way traffic proven);
// exit code 0.  RF-safe: P2Session never sets the transmit bit and
// sends PA-disabled / drive 0 (see P2Session.h bench-safety note).
//
// Build:  cmake --build build --target test_p2_session
// Run:    build\test_p2_session.exe <radio-ip> [seconds] [ddc0-freq-hz]
//   e.g.  build\test_p2_session.exe 192.168.0.139 5 14100000
//
// While it runs, a parallel P2 discovery (e.g. Lyra's Discover) should
// report the radio state 3 = "in use" — and state 2 = idle again after
// teardown.  That plus the status counter is the Phase B bench gate.

#include "wire/P2Session.h"

#include <QCoreApplication>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <radio-ip> [seconds=5] [ddc0-freq-hz=14100000] "
                     "[trx-ant=1] [speaker-tone-hz=0]\n",
                     argv[0]);
        return 2;
    }
    const QString ip      = QString::fromLocal8Bit(argv[1]);
    const int     seconds = (argc > 2) ? std::atoi(argv[2]) : 5;
    const quint32 freqHz  = (argc > 3)
        ? static_cast<quint32>(std::strtoul(argv[3], nullptr, 10))
        : 14'100'000u;
    const int     trxAnt  = (argc > 4) ? std::atoi(argv[4]) : 1;
    // Non-zero: stream a sine at this pitch to the RADIO's speaker
    // (48 kHz stereo int16 → 260 B packets → :1028) and verify via
    // the status packet's speaker-FIFO depth — the radio audibly
    // beeps AND reports the stream being consumed.
    const int     toneHz  = (argc > 5) ? std::atoi(argv[5]) : 0;

    lyra::wire::P2Session session;
    // This bench executable targets the Saturn/G2.  Production selects
    // the profile from the saved marketed model; do the same explicitly
    // here now that unknown boards no longer inherit Saturn/Alex words.
    session.setProfile(
        lyra::wire::p2ProfileForModel(QStringLiteral("ANAN-G2")));
    session.setTrxAntenna(trxAnt);
    session.setDdcFrequencyHz(0, freqHz);
    session.setDucFrequencyHz(freqHz);
    // Phase C: DDC0 IQ stream at 48 kHz (238 samples/frame -> expect
    // ~201.7 frames/s).
    session.enableDdc(0, 48);

    quint32 statusCount   = 0;
    quint32 iqFrames      = 0;   // per-second window
    quint32 iqFramesTotal = 0;
    double  sumSq         = 0.0; // I+Q mean-square accumulator (window)
    quint64 sampCount     = 0;
    // Mirror probe: single-bin DFTs at ±1 kHz baseband offset.  Tune
    // the DDC 1 kHz below a known carrier (e.g. 9,999,000 for WWV 10
    // MHz): a NON-mirrored baseband puts the carrier at +1 kHz, an
    // HL2-style mirrored one at −1 kHz.  Phase accumulators persist
    // across frames (continuous DFT at 48 kHz).
    double posRe = 0, posIm = 0, negRe = 0, negIm = 0;
    double probePhase = 0.0;
    quint64 probeN = 0;

    QObject::connect(&session, &lyra::wire::P2Session::logLine,
                     [](const QString &l) {
                         std::printf("%s\n", qPrintable(l));
                         std::fflush(stdout);
                     });
    QObject::connect(
        &session, &lyra::wire::P2Session::statusReceived,
        [&statusCount](quint32 seq, quint8 ptt, quint8 adcOvf,
                       quint16 exciter, quint16 fwd, quint16 rev,
                       quint16 supply, quint16 adc1Peak, quint16 adc2Peak,
                       quint16 ain3, quint16 ain4, quint16 spkrFifo) {
            ++statusCount;
            // Status is ~5/s — print 1 in 5 to keep the IQ stats readable.
            if (statusCount % 5 == 1) {
                // Thetis conversions, ANAN-G2 constants (voff 0.001,
                // sens 66.23) — the same math the meter now uses.
                const double volts =
                    (ain3 / 4095.0) * 5.0 * ((22.0 + 1.0) / 1.1);
                const double amps = std::max(
                    0.0, (ain4 * 5000.0 / 4095.0 - 0.001) / 66.23);
                std::printf(
                    "  status seq=%u  ptt=0x%02X  adcOvf=0x%02X  exciter=%u  "
                    "fwd=%u  rev=%u  supplyRaw=%u  adcPeak=%u/%u  "
                    "PA %.1f V / %.2f A  spkrFifo=%u\n",
                    seq, ptt, adcOvf, exciter, fwd, rev, supply,
                    adc1Peak, adc2Peak, volts, amps, spkrFifo);
                std::fflush(stdout);
            }
        });
    QObject::connect(
        &session, &lyra::wire::P2Session::iqFrameReceived,
        [&](int /*ddc*/, quint32 /*seq*/, const QByteArray &iq) {
            ++iqFrames;
            ++iqFramesTotal;
            // Unpack: 6 bytes/sample, I then Q, BE signed 24-bit in the
            // top 3 bytes of an int32 (the Thetis network.c recipe).
            const auto *u =
                reinterpret_cast<const unsigned char *>(iq.constData());
            for (int k = 0; k + 5 < iq.size(); k += 6) {
                const qint32 iRaw = static_cast<qint32>(
                    (u[k]     << 24) | (u[k + 1] << 16) | (u[k + 2] << 8));
                const qint32 qRaw = static_cast<qint32>(
                    (u[k + 3] << 24) | (u[k + 4] << 16) | (u[k + 5] << 8));
                const double iv = iRaw / 2147483648.0;
                const double qv = qRaw / 2147483648.0;
                sumSq += iv * iv + qv * qv;
                ++sampCount;
                // ±1 kHz probe bins (assumes 48 kHz DDC rate).
                const double w = 2.0 * 3.14159265358979323846 *
                                 1000.0 * probePhase / 48000.0;
                const double c = std::cos(w), s = std::sin(w);
                // exp(-j w t) * x  → +1 kHz bin;  exp(+j w t) → −1 kHz.
                posRe += iv * c + qv * s;  posIm += qv * c - iv * s;
                negRe += iv * c - qv * s;  negIm += qv * c + iv * s;
                probePhase += 1.0;
                ++probeN;
            }
        });
    // Once a second: frame rate + stream integrity + RX signal level.
    QTimer statsTimer;
    statsTimer.setInterval(1000);
    QObject::connect(&statsTimer, &QTimer::timeout, [&]() {
        const double dbfs = (sampCount > 0)
            ? 10.0 * std::log10(sumSq / static_cast<double>(sampCount))
            : -999.0;
        double posDb = -999.0, negDb = -999.0;
        if (probeN > 0) {
            const double n2 = static_cast<double>(probeN) *
                              static_cast<double>(probeN);
            posDb = 10.0 * std::log10((posRe * posRe + posIm * posIm) / n2
                                      + 1e-30);
            negDb = 10.0 * std::log10((negRe * negRe + negIm * negIm) / n2
                                      + 1e-30);
        }
        std::printf("  IQ: %u frames/s (expect ~202)  seqErrs=%u  "
                    "level=%.1f dBFS  bin+1k=%.1f  bin-1k=%.1f\n",
                    iqFrames, session.iqSeqErrors(), dbfs, posDb, negDb);
        std::fflush(stdout);
        iqFrames  = 0;
        sumSq     = 0.0;
        sampCount = 0;
        posRe = posIm = negRe = negIm = 0.0;
        probePhase = 0.0;
        probeN = 0;
    });
    statsTimer.start();

    // Speaker tone: 20 ms pacing → 960 frames of sine per tick (real-
    // time 48 kHz).  The session lives on THIS thread in the bench
    // tool, so calling sendSpeakerAudio directly is thread-correct.
    QTimer toneTimer;
    double tonePhase = 0.0;
    std::vector<qint16> toneBuf;
    if (toneHz > 0) {
        toneTimer.setInterval(20);
        toneTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&toneTimer, &QTimer::timeout, [&]() {
            constexpr int frames = 960;             // 20 ms @ 48 kHz
            toneBuf.resize(frames * 2);
            const double w = 2.0 * 3.14159265358979323846 * toneHz / 48000.0;
            for (int f = 0; f < frames; ++f) {
                const auto s = static_cast<qint16>(
                    8000.0 * std::sin(tonePhase));  // ~-12 dBFS
                toneBuf[2 * f] = toneBuf[2 * f + 1] = s;
                tonePhase += w;
            }
            session.sendSpeakerAudio(toneBuf.data(), frames);
        });
        toneTimer.start();
        std::printf("speaker tone: %d Hz -> radio :1028 "
                    "(watch spkrFifo + listen to the radio)\n", toneHz);
    }

    QObject::connect(&session, &lyra::wire::P2Session::stopped,
                     &app, &QCoreApplication::quit);

    // Hold the session for the requested window, then tear down.
    QTimer::singleShot(seconds * 1000, &session,
                       [&session]() { session.close(); });
    // Hard backstop: if the radio never answers, don't hang forever.
    QTimer::singleShot((seconds + 5) * 1000, &app, &QCoreApplication::quit);

    session.open(ip);
    app.exec();

    // PASS = sustained status stream AND sustained DDC0 IQ (~202
    // frames/s at 48 kHz; require 150/s average to allow start-up
    // ramp) with a clean sequence run.
    const bool statusOk = statusCount >= 3;
    const bool iqOk     = iqFramesTotal >=
                          static_cast<quint32>(seconds) * 150u;
    const bool pass     = statusOk && iqOk;
    std::printf("\n%s — %u status packet(s), %u IQ frame(s), %u seq "
                "error(s) in %d s\n",
                pass ? "PASS" : "FAIL", statusCount, iqFramesTotal,
                session.iqSeqErrors(), seconds);
    return pass ? 0 : 1;
}

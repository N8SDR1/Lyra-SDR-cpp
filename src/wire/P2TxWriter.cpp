#include "P2TxWriter.h"

#include <algorithm>
#include <utility>

namespace lyra::wire {

P2TxWriter::P2TxWriter(QObject *parent)
    : QObject(parent), timer_(this)
{
    timer_.setInterval(kTimerPeriodMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this]() { onTimer(); });
}

void P2TxWriter::startZeroPriming() {
    stop();
    sequence_ = 0;
    scheduledPackets_ = 0;
    droppedPackets_ = 0;
    running_ = true;
    elapsed_.start();
    produceDueForElapsedNs(0); // Prime one frame immediately.
    timer_.start();
}

void P2TxWriter::stop() {
    timer_.stop();
    running_ = false;
}

int P2TxWriter::produceDueForElapsedNs(qint64 elapsedNs) {
    if (!running_ || elapsedNs < 0)
        return 0;

    // Frame zero is immediate. Thereafter 192000 / 240 = 800 packets/s.
    const auto targetPackets = std::uint64_t{1} +
        (static_cast<std::uint64_t>(elapsedNs) *
         static_cast<std::uint64_t>(kSampleRateHz)) /
        (std::uint64_t{1'000'000'000} *
         static_cast<std::uint64_t>(P2TxPackets::kIqSamplesPerPacket));
    if (targetPackets <= scheduledPackets_)
        return 0;

    std::uint64_t due = targetPackets - scheduledPackets_;
    if (due > kMaxCatchupPackets) {
        const std::uint64_t missed = due - kMaxCatchupPackets;
        droppedPackets_ += missed;
        if (faultSink_)
            faultSink_();
        // A stalled TX producer must not flood stale samples in a catch-up
        // burst. Stop immediately; the session safety gate will unkey when
        // live TX integration exposes the writer.
        stop();
        return 0;
    }

    int produced = 0;
    while (due-- > 0) {
        if (packetSink_)
            packetSink_(P2TxPackets::encodeIq(sequence_, zeros_));
        ++sequence_;
        ++scheduledPackets_;
        ++produced;
    }
    return produced;
}

void P2TxWriter::onTimer() {
    produceDueForElapsedNs(elapsed_.nsecsElapsed());
}

} // namespace lyra::wire

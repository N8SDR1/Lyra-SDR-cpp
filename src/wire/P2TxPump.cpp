#include "P2TxPump.h"

namespace lyra::wire {

P2TxPump::P2TxPump(QObject *parent)
    : QObject(parent), timer_(this)
{
    timer_.setInterval(kTimerPeriodMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this]() { onTimer(); });
}

void P2TxPump::start() {
    stop();
    scheduledBlocks_ = 0;
    producedBlocks_ = 0;
    missedBlocks_ = 0;
    running_ = true;
    elapsed_.start();
    produceDueForElapsedNs(0); // Seed CMaster immediately.
    if (running_)
        timer_.start();
}

void P2TxPump::stop() {
    timer_.stop();
    running_ = false;
}

int P2TxPump::produceDueForElapsedNs(qint64 elapsedNs) {
    if (!running_ || elapsedNs < 0)
        return 0;

    // Block zero is immediate. Thereafter 48000 / 64 = 750 blocks/s.
    const auto targetBlocks = std::uint64_t{1} +
        (static_cast<std::uint64_t>(elapsedNs) *
         static_cast<std::uint64_t>(kSampleRateHz)) /
        (std::uint64_t{1'000'000'000} *
         static_cast<std::uint64_t>(kBlockSamples));
    if (targetBlocks <= scheduledBlocks_)
        return 0;

    std::uint64_t due = targetBlocks - scheduledBlocks_;
    if (due > kMaxCatchupBlocks) {
        missedBlocks_ += due - kMaxCatchupBlocks;
        fail();
        return 0;
    }

    int produced = 0;
    while (due-- > 0) {
        if (!inputSink_ || !inputSink_(zeros_.data(), kBlockSamples)) {
            fail();
            return produced;
        }
        ++scheduledBlocks_;
        ++producedBlocks_;
        ++produced;
    }
    return produced;
}

void P2TxPump::onTimer() {
    produceDueForElapsedNs(elapsed_.nsecsElapsed());
}

void P2TxPump::fail() {
    stop();
    if (faultSink_)
        faultSink_();
}

} // namespace lyra::wire

// Dedicated Protocol 2 TX producer clock.
//
// Protocol 1 gets its 48 kHz/64-sample CMaster input cadence from the
// EP6 receive thread. Protocol 2 has no EP6 thread, so this event-loop
// owned pump supplies zero seed blocks at the exact TXA cadence. VAC1
// or TCI audio replaces those zeros inside xcmaster(); TUN and other
// internally generated TX modes can use the same path.

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace lyra::wire {

class P2TxPump final : public QObject {
public:
    using InputSink = std::function<bool(const double *, int)>;
    using FaultSink = std::function<void()>;

    explicit P2TxPump(QObject *parent = nullptr);

    void setInputSink(InputSink sink) { inputSink_ = std::move(sink); }
    void setFaultSink(FaultSink sink) { faultSink_ = std::move(sink); }

    void start();
    void stop();
    bool isRunning() const { return running_; }
    bool hasInputSink() const { return static_cast<bool>(inputSink_); }
    std::uint64_t producedBlocks() const { return producedBlocks_; }
    std::uint64_t missedBlocks() const { return missedBlocks_; }

    // Deterministic test seam used by the real timer path too.
    int produceDueForElapsedNs(qint64 elapsedNs);

    static constexpr int kSampleRateHz = 48'000;
    static constexpr int kBlockSamples = 64;
    static constexpr int kTimerPeriodMs = 1;
    // Match the writer's bounded scheduler-jitter budget. The FIFO
    // prefill absorbs callback ordering while CMaster catches up.
    static constexpr std::uint64_t kMaxCatchupBlocks = 16;

private:
    void onTimer();
    void fail();

    QTimer timer_;
    QElapsedTimer elapsed_;
    InputSink inputSink_;
    FaultSink faultSink_;
    std::array<double, 2 * kBlockSamples> zeros_{};
    bool running_ = false;
    std::uint64_t scheduledBlocks_ = 0;
    std::uint64_t producedBlocks_ = 0;
    std::uint64_t missedBlocks_ = 0;
};

} // namespace lyra::wire

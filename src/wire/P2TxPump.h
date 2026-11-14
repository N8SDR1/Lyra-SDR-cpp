// Dedicated Protocol 2 TX producer clock.
//
// Protocol 1 gets its 48 kHz/64-sample CMaster input cadence from the
// EP6 receive thread. Protocol 2 has no EP6 thread, so this native
// thread supplies seed blocks at the TXA input cadence. VAC1 or TCI
// audio replaces those zeros inside xcmaster(); TUN and other
// internally generated TX modes use the same path.
//
// This is not a QTimer on the P2 session loop. Session RX decode must
// not starve the modulator clock.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>

namespace lyra::wire {

class P2TxPump final {
public:
    using InputSink = std::function<bool(const double *, int)>;

    P2TxPump() = default;
    ~P2TxPump() { stop(); }

    P2TxPump(const P2TxPump &) = delete;
    P2TxPump &operator=(const P2TxPump &) = delete;

    void setInputSink(InputSink sink) { inputSink_ = std::move(sink); }

    void start();
    void stop();
    bool isRunning() const { return running_.load(std::memory_order_acquire); }
    bool hasInputSink() const { return static_cast<bool>(inputSink_); }
    std::uint64_t producedBlocks() const {
        return producedBlocks_.load(std::memory_order_relaxed);
    }

    static constexpr int kSampleRateHz = 48'000;
    static constexpr int kBlockSamples = 64;

private:
    void threadMain();

    InputSink inputSink_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> producedBlocks_{0};
};

} // namespace lyra::wire

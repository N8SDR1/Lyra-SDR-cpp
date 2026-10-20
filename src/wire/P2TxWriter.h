// Dedicated Protocol 2 DUC-IQ sender (port 1029).
//
// Matches DeskHPSDR `new_protocol_txiq_thread` / Thetis ChannelMaster
// outbound: a native thread waits until 240 samples are in the FIFO,
// then sendto()s one 1444-byte packet. Cadence is the DSP producer, not
// a Qt 1 ms timer on the P2 session loop.

#pragma once

#include "P2TxFifo.h"
#include "P2TxPackets.h"

#include <QByteArray>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <utility>

namespace lyra::wire {

class P2TxWriter final {
public:
    using PacketSink = std::function<bool(const QByteArray &)>;

    P2TxWriter() = default;
    ~P2TxWriter() { stop(); }

    P2TxWriter(const P2TxWriter &) = delete;
    P2TxWriter &operator=(const P2TxWriter &) = delete;

    void setPacketSink(PacketSink sink) { packetSink_ = std::move(sink); }
    void setInputFifo(P2TxFifo *fifo) { inputFifo_ = fifo; }

    void startFromInput();
    void stop();
    bool isRunning() const { return running_.load(std::memory_order_acquire); }
    quint32 nextSequence() const {
        return sequence_.load(std::memory_order_relaxed);
    }
    std::uint64_t packetsSent() const {
        return packetsSent_.load(std::memory_order_relaxed);
    }

    static constexpr int kSampleRateHz = P2TxFifo::kSampleRateHz;

private:
    void threadMain();

    PacketSink packetSink_;
    P2TxFifo *inputFifo_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<quint32> sequence_{0};
    std::atomic<std::uint64_t> packetsSent_{0};
};

} // namespace lyra::wire

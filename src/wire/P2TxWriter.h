// Paced Protocol 2 TX-IQ packet producer.
//
// The writer owns pacing and sequence numbers but not a socket. P2Session
// supplies the packet sink so every P2 datagram continues to use the one
// session socket. This phase can produce zero-IQ priming frames internally;
// P2Session exposes no method that starts it.

#pragma once

#include "P2TxFifo.h"
#include "P2TxPackets.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <utility>

namespace lyra::wire {

class P2TxWriter final : public QObject {
public:
    using PacketSink = std::function<void(const QByteArray &)>;
    using FaultSink = std::function<void()>;

    explicit P2TxWriter(QObject *parent = nullptr);

    void setPacketSink(PacketSink sink) { packetSink_ = std::move(sink); }
    void setFaultSink(FaultSink sink) { faultSink_ = std::move(sink); }
    void setInputFifo(P2TxFifo *fifo) { inputFifo_ = fifo; }

    void startZeroPriming();
    void startFromInput();
    void stop();
    bool isRunning() const { return running_; }
    quint32 nextSequence() const { return sequence_; }
    std::uint64_t droppedPackets() const { return droppedPackets_; }

    // Deterministic test seam used by the real timer path too.
    int produceDueForElapsedNs(qint64 elapsedNs);

    static constexpr int kSampleRateHz = P2TxFifo::kSampleRateHz;
    static constexpr int kTimerPeriodMs = 1;
    // Windows commonly coalesces 1 ms timers for several milliseconds.
    // A 20 ms bounded recovery window tolerates normal scheduler jitter;
    // anything longer is treated as a transport fault, not replayed.
    static constexpr std::uint64_t kMaxCatchupPackets = 16;

private:
    void onTimer();

    QTimer timer_;
    QElapsedTimer elapsed_;
    PacketSink packetSink_;
    FaultSink faultSink_;
    P2TxFifo *inputFifo_ = nullptr;
    std::array<P2TxIqSample, P2TxPackets::kIqSamplesPerPacket> zeros_{};
    bool zeroPriming_ = true;
    bool running_ = false;
    quint32 sequence_ = 0;
    std::uint64_t scheduledPackets_ = 0;
    std::uint64_t droppedPackets_ = 0;
};

} // namespace lyra::wire

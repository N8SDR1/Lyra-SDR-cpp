// Bounded single-producer/single-consumer FIFO for Protocol 2 TX IQ.
//
// Producer: ChannelMaster's post-WDSP ILV output (cm_main TX thread).
// Consumer: dedicated P2 TX-IQ send thread (not the session Qt loop).
//
// The FIFO stores logical {I,Q} doubles. P2TxPackets encodes them to the
// wire as I-then-Q, 24-bit big-endian (encodeIq) -- the HPSDR P2 DUC-IQ
// convention, matching the RX DDC-IQ decode. (An earlier bring-up packed
// Q-then-I, which mirror-imaged the transmitted sideband; do NOT "restore"
// that.) Pushes are all-or-nothing so an overflow cannot splice a partial
// WDSP block into the TX stream.

#pragma once

#include "P2TxPackets.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace lyra::wire {

class P2TxFifo final {
public:
    static constexpr int kSampleRateHz = 192'000;
    static constexpr std::size_t kPacketSamples =
        P2TxPackets::kIqSamplesPerPacket;
    static constexpr std::size_t kCapacitySamples = 8'192;

    static_assert((kCapacitySamples & (kCapacitySamples - 1)) == 0,
                  "P2 TX FIFO capacity must be a power of two");

    bool pushInterleaved(const double *iq, std::size_t samples) noexcept;
    bool popPacket(
        std::array<P2TxIqSample, kPacketSamples> &packet) noexcept;

    // Writer wait: returns true when a packet is available or stop is set.
    bool waitForPacket(const std::atomic<bool> &stop, int timeoutMs) noexcept;
    void wakeWaiters() noexcept;

    std::size_t size() const noexcept;
    std::size_t freeSpace() const noexcept {
        return kCapacitySamples - size();
    }

    std::uint64_t overflowEvents() const noexcept {
        return overflowEvents_.load(std::memory_order_relaxed);
    }
    std::uint64_t droppedSamples() const noexcept {
        return droppedSamples_.load(std::memory_order_relaxed);
    }
    std::uint64_t underflowEvents() const noexcept {
        return underflowEvents_.load(std::memory_order_relaxed);
    }
    bool overflowed() const noexcept { return overflowEvents() != 0; }

    // Call only while the producer and consumer are quiescent.
    void reset() noexcept;

private:
    static constexpr std::size_t kMask = kCapacitySamples - 1;

    std::atomic<std::uint64_t> writeIndex_{0};
    std::atomic<std::uint64_t> readIndex_{0};
    std::array<P2TxIqSample, kCapacitySamples> samples_{};
    std::atomic<std::uint64_t> overflowEvents_{0};
    std::atomic<std::uint64_t> droppedSamples_{0};
    std::atomic<std::uint64_t> underflowEvents_{0};
    std::mutex waitMutex_;
    std::condition_variable waitCv_;
};

// Process-wide producer seam. Lyra supports one active radio/session;
// this stable-lifetime FIFO avoids a detach-vs-producer pointer race.
P2TxFifo &p2TxInputFifo() noexcept;
void setP2TxInputEnabled(bool enabled) noexcept;
bool p2TxInputEnabled() noexcept;

// Raw function-pointer shape required by CMaster::SendpOutboundTx.
// Only transmitter output id 1 is accepted.
void p2TxOutbound(int id, int nsamples, double *iq) noexcept;

} // namespace lyra::wire

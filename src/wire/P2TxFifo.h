// Bounded single-producer/single-consumer FIFO for Protocol 2 TX IQ.
//
// Producer: ChannelMaster's existing post-WDSP ILV output callback.
// Consumer: P2TxWriter on the P2 session thread.
//
// The FIFO stores logical {I,Q} doubles. P2TxPackets performs the
// Saturn-specific Q-then-I wire encoding when a complete 240-sample
// packet is consumed. Pushes are all-or-nothing so an overflow cannot
// splice a partial WDSP block into the TX stream.

#pragma once

#include "P2TxPackets.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

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

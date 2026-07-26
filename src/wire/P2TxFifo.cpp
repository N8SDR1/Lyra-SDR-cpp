#include "P2TxFifo.h"

#include <cmath>

namespace lyra::wire {
namespace {

P2TxFifo gP2TxInput;
std::atomic<bool> gP2TxInputEnabled{false};

double finiteOrZero(double value) noexcept {
    return std::isfinite(value) ? value : 0.0;
}

} // namespace

bool P2TxFifo::pushInterleaved(const double *iq,
                               std::size_t sampleCount) noexcept {
    if (!iq || sampleCount == 0 || sampleCount > kCapacitySamples) {
        if (sampleCount != 0) {
            overflowEvents_.fetch_add(1, std::memory_order_relaxed);
            droppedSamples_.fetch_add(sampleCount, std::memory_order_relaxed);
        }
        return false;
    }

    const std::uint64_t write =
        writeIndex_.load(std::memory_order_relaxed);
    const std::uint64_t read =
        readIndex_.load(std::memory_order_acquire);
    const std::uint64_t used = write - read;
    if (sampleCount > kCapacitySamples - used) {
        overflowEvents_.fetch_add(1, std::memory_order_relaxed);
        droppedSamples_.fetch_add(sampleCount, std::memory_order_relaxed);
        return false;
    }

    for (std::size_t n = 0; n < sampleCount; ++n) {
        samples_[static_cast<std::size_t>(write + n) & kMask] = {
            finiteOrZero(iq[2 * n]),
            finiteOrZero(iq[2 * n + 1]),
        };
    }
    writeIndex_.store(write + sampleCount, std::memory_order_release);
    return true;
}

bool P2TxFifo::popPacket(
    std::array<P2TxIqSample, kPacketSamples> &packet) noexcept {
    const std::uint64_t read =
        readIndex_.load(std::memory_order_relaxed);
    const std::uint64_t write =
        writeIndex_.load(std::memory_order_acquire);
    if (write - read < kPacketSamples) {
        underflowEvents_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    for (std::size_t n = 0; n < kPacketSamples; ++n)
        packet[n] = samples_[static_cast<std::size_t>(read + n) & kMask];
    readIndex_.store(read + kPacketSamples, std::memory_order_release);
    return true;
}

std::size_t P2TxFifo::size() const noexcept {
    const std::uint64_t write =
        writeIndex_.load(std::memory_order_acquire);
    const std::uint64_t read =
        readIndex_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(write - read);
}

void P2TxFifo::reset() noexcept {
    writeIndex_.store(0, std::memory_order_relaxed);
    readIndex_.store(0, std::memory_order_relaxed);
    overflowEvents_.store(0, std::memory_order_relaxed);
    droppedSamples_.store(0, std::memory_order_relaxed);
    underflowEvents_.store(0, std::memory_order_relaxed);
}

P2TxFifo &p2TxInputFifo() noexcept {
    return gP2TxInput;
}

void setP2TxInputEnabled(bool enabled) noexcept {
    gP2TxInputEnabled.store(enabled, std::memory_order_release);
}

bool p2TxInputEnabled() noexcept {
    return gP2TxInputEnabled.load(std::memory_order_acquire);
}

void p2TxOutbound(int id, int nsamples, double *iq) noexcept {
    if (id != 1 || nsamples <= 0 || !iq || !p2TxInputEnabled())
        return;
    gP2TxInput.pushInterleaved(
        iq, static_cast<std::size_t>(nsamples));
}

} // namespace lyra::wire

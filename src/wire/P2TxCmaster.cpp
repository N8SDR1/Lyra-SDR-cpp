#include "P2TxCmaster.h"

#include "P2TxFifo.h"
#include "wire/CMaster.h"
#include "wire/ObBuffs.h"
#include "wire/wdspcalls.h"

#include <array>
#include <mutex>

namespace lyra::wire {

namespace {
bool active = false;
RESAMPLE resampler = nullptr;
std::mutex resamplerMutex;
std::array<double, 2 * P2TxFifo::kCapacitySamples> resampledIq{};

void p2TxCmasterOutbound(int id, int nsamples, double *iq) noexcept {
    if (id != 1 || nsamples <= 0 || !iq || !p2TxInputEnabled())
        return;

    // The existing TXA/ILV path is intentionally left at its proven
    // 48 kHz P1 rate. P2 DUC IQ is fixed at 192 kHz, so convert at the
    // protocol boundary with WDSP's own stateful complex resampler.
    // At the normal 64-sample TXA block this produces 256 samples; the
    // FIFO absorbs the 256-vs-240 P2 packet-size mismatch.
    if (static_cast<std::size_t>(nsamples) >
        P2TxFifo::kCapacitySamples / 4)
        return;

    std::lock_guard<std::mutex> lock(resamplerMutex);
    if (!active || !resampler || !xresample)
        return;
    resampler->in = iq;
    resampler->size = nsamples;
    const int outputSamples = xresample(resampler);
    if (outputSamples > 0)
        p2TxInputFifo().pushInterleaved(
            resampledIq.data(), static_cast<std::size_t>(outputSamples));
}
}

bool activateP2TxCmasterProducer() {
    if (active)
        return true;
    if (!pcm || !pcm->xmtr[0].pilv || !create_resample ||
        !destroy_resample || !flush_resample || !xresample)
        return false;

    {
        std::lock_guard<std::mutex> lock(resamplerMutex);
        resampler = create_resample(
            1, 64, nullptr, resampledIq.data(),
            48'000, P2TxFifo::kSampleRateHz, 0.0, 0, 1.0);
        if (!resampler)
            return false;
        p2TxInputFifo().reset();
        active = true;
        setP2TxInputEnabled(true);
    }
    SendpOutboundTx(&p2TxCmasterOutbound);
    return true;
}

void deactivateP2TxCmasterProducer() {
    if (!active)
        return;

    // Stop accepting and restore P1 before releasing resampler state.
    setP2TxInputEnabled(false);
    if (pcm && pcm->xmtr[0].pilv)
        SendpOutboundTx(&OutBound);
    {
        std::lock_guard<std::mutex> lock(resamplerMutex);
        active = false;
        if (resampler && destroy_resample)
            destroy_resample(resampler);
        resampler = nullptr;
    }
    p2TxInputFifo().reset();
}

} // namespace lyra::wire

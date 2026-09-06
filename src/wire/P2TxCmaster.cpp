#include "P2TxCmaster.h"

#include "P2TxFifo.h"
#include "wire/CMaster.h"
#include "wire/CmBuffs.h"
#include "wire/ObBuffs.h"
#include "wire/cmsetup.h"
#include "wire/wdspcalls.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>

namespace lyra::wire {

namespace {
std::atomic_bool active{false};
std::atomic_bool channelRunning{false};
RESAMPLE resampler = nullptr;
std::mutex resamplerMutex;
std::array<double, 2 * P2TxFifo::kCapacitySamples> resampledIq{};
// Diagnostic: peak |I|,|Q| of the last block actually pushed to the DUC
// FIFO (== what putSample24 packs on the wire).  Surfaced on-screen (the
// file log is dead) so a low-power bench can tell "DSP output is short"
// from "drive byte / radio is short".  ~1.0 = full-scale IQ.
std::atomic<double> lastPeak{0.0};

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
    if (!active.load(std::memory_order_acquire) || !resampler || !xresample)
        return;
    resampler->in = iq;
    resampler->size = nsamples;
    const int outputSamples = xresample(resampler);
    if (outputSamples > 0) {
        double pk = 0.0;
        for (int n = 0; n < 2 * outputSamples; ++n)
            pk = std::max(pk, std::fabs(resampledIq[static_cast<std::size_t>(n)]));
        lastPeak.store(pk, std::memory_order_relaxed);
        p2TxInputFifo().pushInterleaved(
            resampledIq.data(), static_cast<std::size_t>(outputSamples));
    }
}
}

bool activateP2TxCmasterProducer() {
    if (active.load(std::memory_order_acquire))
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
        active.store(true, std::memory_order_release);
        setP2TxInputEnabled(true);
    }
    SendpOutboundTx(&p2TxCmasterOutbound);
    // DIAGNOSTIC (2026-09-06): CFIR DISABLED to test the -13 dB (peak 0.215)
    // TUN under-drive.  The CFIR here compensates a 192 kHz-out DUC CIC, but
    // Lyra's shared TXA outputs at 48 kHz and does its OWN clean 48→192
    // resample — so this CFIR is configured for the wrong rate and is
    // attenuating the output, not gently boosting the band edges.  Lyra's
    // external resampler already interpolates cleanly; the radio's own DUC
    // CIC droop across a single tune tone near passband centre is
    // negligible.  If the DUC-IQ peak jumps to ~1.0 with this off, the CFIR
    // was the shortfall.  (Was: SetTXACFIRRun(chid(1,0), 1).)
    if (SetTXACFIRRun)
        SetTXACFIRRun(chid(1, 0), 0);
    return true;
}

void deactivateP2TxCmasterProducer() {
    if (!active.load(std::memory_order_acquire))
        return;

    // Stop the TXA channel while the seam is still active (its output still
    // routes to the P2 FIFO) so we never leave the shared channel running
    // for a later P1 transmit.  Idempotent if the transport already stopped it.
    setP2TxCmasterChannelRunning(false);
    // Stop accepting and restore P1 before releasing resampler state.
    setP2TxInputEnabled(false);
    // Restore the TXA compensating FIR to its create-time default (off)
    // so a later P1/HL2 transmit on the same TXA channel is unaffected.
    if (SetTXACFIRRun)
        SetTXACFIRRun(chid(1, 0), 0);
    if (pcm && pcm->xmtr[0].pilv)
        SendpOutboundTx(&OutBound);
    {
        std::lock_guard<std::mutex> lock(resamplerMutex);
        active.store(false, std::memory_order_release);
        if (resampler && destroy_resample)
            destroy_resample(resampler);
        resampler = nullptr;
    }
    p2TxInputFifo().reset();
}

double p2TxCmasterLastPeak() {
    return lastPeak.load(std::memory_order_relaxed);
}

bool feedP2TxCmasterInput(const double *iq, int samples) {
    if (!active.load(std::memory_order_acquire) || !pcm || !iq ||
        samples <= 0)
        return false;

    // Inbound's historical API predates const-correctness. It copies the
    // block into stream 1's CMB ring and does not modify caller storage.
    Inbound(inid(1, 0), samples, const_cast<double *>(iq));
    return true;
}

void setP2TxCmasterChannelRunning(bool on) {
    // Only run the channel while the outbound seam is active, so its ILV
    // output routes to the P2 DUC FIFO (p2TxCmasterOutbound), never the P1
    // EP2 path (OutBound).  Stop is always honoured.
    const bool want = on && active.load(std::memory_order_acquire);
    if (channelRunning.load(std::memory_order_acquire) == want)
        return;
    if (!SetChannelState)
        return;
    // start: state=1 (run), non-blocking up-ramp.
    // stop:  state=0 (off),  dmode=0 non-blocking — this runs on the pump's
    //        own thread at transport teardown, so a blocking down-ramp flush
    //        (dmode=1) could self-deadlock waiting on input the stopped pump
    //        no longer feeds; RF is already MOX-gated off, so no on-air click.
    SetChannelState(chid(1, 0), want ? 1 : 0, 0);
    channelRunning.store(want, std::memory_order_release);
}

} // namespace lyra::wire

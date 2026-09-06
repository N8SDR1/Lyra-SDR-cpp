#include "P2TxCmaster.h"

#include "P2TxFifo.h"
#include "wire/CMaster.h"
#include "wire/CmBuffs.h"
#include "wire/ObBuffs.h"
#include "wire/cmsetup.h"
#include "wire/wdspcalls.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace lyra::wire {

// Defined in CMaster.cpp (Lyra-native): narrow reconfigure of the TX
// channel's DUC output rate + output-stage block sizes, WITHOUT touching the
// RX-audio AAMixer (the full reference SetXmtrChannelOutrate would clobber
// the shared channel's TX-monitor mixer state).  SetOutputSamplerate inside
// it also re-points CFIR's rate.
void SetXmtrDucOutrate(int xmtr_id, int rate);

namespace {
std::atomic_bool active{false};
std::atomic_bool channelRunning{false};
std::mutex stateMutex;
// Diagnostic: peak |I|,|Q| of the last block actually pushed to the DUC
// FIFO (== what putSample24 packs on the wire).  Surfaced on-screen (the
// file log is dead) so a low-power bench can tell "DSP output is short"
// from "drive byte / radio is short".  ~1.0 = full-scale IQ.
std::atomic<double> lastPeak{0.0};

void p2TxCmasterOutbound(int id, int nsamples, double *iq) noexcept {
    if (id != 1 || nsamples <= 0 || !iq || !p2TxInputEnabled())
        return;

    // The shared TXA channel is raised to the 192 kHz P2 DUC output rate on
    // activate (SetXmtrDucOutrate), so WDSP's own output resampler now emits
    // native 192 kHz IQ here — feed it straight to the DUC FIFO, no external
    // resample.  CFIR (enabled at 192 kHz) has already pre-corrected the
    // radio's DUC CIC interpolator droop.  The FIFO absorbs the block-size
    // vs 240-sample P2 packet mismatch.
    if (static_cast<std::size_t>(nsamples) > P2TxFifo::kCapacitySamples)
        return;

    std::lock_guard<std::mutex> lock(stateMutex);
    if (!active.load(std::memory_order_acquire))
        return;
    double pk = 0.0;
    for (int n = 0; n < 2 * nsamples; ++n)
        pk = std::max(pk, std::fabs(iq[static_cast<std::size_t>(n)]));
    lastPeak.store(pk, std::memory_order_relaxed);
    p2TxInputFifo().pushInterleaved(iq, static_cast<std::size_t>(nsamples));
}
}

bool activateP2TxCmasterProducer() {
    if (active.load(std::memory_order_acquire))
        return true;
    if (!pcm || !pcm->xmtr[0].pilv || !SetOutputSamplerate || !SetTXACFIRRun)
        return false;

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        // Raise the shared TXA channel to the 192 kHz P2 DUC output rate.
        // WDSP's own output resampler (96→192 kHz) now produces the DUC
        // stream natively — no external resampler.  The channel is restored
        // to the P1/HL2 48 kHz rate on deactivate.
        SetXmtrDucOutrate(0, P2TxFifo::kSampleRateHz);
        p2TxInputFifo().reset();
        active.store(true, std::memory_order_release);
        setP2TxInputEnabled(true);
    }
    SendpOutboundTx(&p2TxCmasterOutbound);
    // Enable the compensating FIR — valid only at the DUC output rate, where
    // it pre-corrects the radio's DUC CIC interpolator droop (reference:
    // Thetis/deskHPSDR run CFIR ON for Protocol 2, OFF for Protocol 1).
    // Restored to off (the P1 default) on deactivate.
    SetTXACFIRRun(chid(1, 0), 1);
    return true;
}

void deactivateP2TxCmasterProducer() {
    if (!active.load(std::memory_order_acquire))
        return;

    // Stop the TXA channel while the seam is still active (its output still
    // routes to the P2 FIFO) so we never leave the shared channel running
    // for a later P1 transmit.  Idempotent if the transport already stopped it.
    setP2TxCmasterChannelRunning(false);
    // Stop accepting and restore P1 before releasing state.
    setP2TxInputEnabled(false);
    // Restore the compensating FIR to off (the P1 default) and the shared
    // TXA channel to the P1/HL2 48 kHz output rate, so a later P1/HL2
    // transmit on the same channel is byte-identical to before.
    if (SetTXACFIRRun)
        SetTXACFIRRun(chid(1, 0), 0);
    SetXmtrDucOutrate(0, 48'000);
    if (pcm && pcm->xmtr[0].pilv)
        SendpOutboundTx(&OutBound);
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        active.store(false, std::memory_order_release);
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

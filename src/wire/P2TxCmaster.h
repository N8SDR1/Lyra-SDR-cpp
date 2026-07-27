// RF-inert adapter between the legacy 48 kHz ChannelMaster callback
// surface and the 192 kHz Protocol 2 TX FIFO. It uses WDSP's stateful
// complex resampler and leaves the proven P1 TXA channel configuration
// unchanged. Kept in its own translation unit because the reference
// CMaster headers carry legacy C macros that must not leak into
// Qt/application headers.

#pragma once

namespace lyra::wire {

bool activateP2TxCmasterProducer();
void deactivateP2TxCmasterProducer();
bool feedP2TxCmasterInput(const double *iq, int samples);

} // namespace lyra::wire

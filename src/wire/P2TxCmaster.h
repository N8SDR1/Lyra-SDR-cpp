// Adapter between the ChannelMaster TX callback surface and the 192 kHz
// Protocol 2 DUC TX FIFO.  On P2 TX activate it raises the shared TXA
// channel to native 192 kHz output (SetXmtrDucOutrate) and enables the
// compensating FIR (CFIR) for the radio's DUC CIC droop — reference-faithful
// P2 TX — then feeds WDSP's native 192 kHz IQ straight to the FIFO; on
// deactivate it restores the P1/HL2 48 kHz output + CFIR-off so the shared
// channel is byte-identical for a later P1 transmit.  Kept in its own
// translation unit because the reference CMaster headers carry legacy C
// macros that must not leak into Qt/application headers.

#pragma once

namespace lyra::wire {

bool activateP2TxCmasterProducer();
void deactivateP2TxCmasterProducer();
bool feedP2TxCmasterInput(const double *iq, int samples);

// Diagnostic: peak |I|,|Q| of the last DUC-IQ block pushed to port 1029
// (== what putSample24 packs on the wire; ~1.0 = full-scale).  Surfaced on
// the "P2 TX:" status line because the file log is dead — lets a low-power
// bench distinguish a short DSP output (ALC/postgen) from a short drive
// byte / radio-side limit.
double p2TxCmasterLastPeak();

// Run/stop the shared WDSP TXA channel (chid(1,0)) that feeds the P2 DUC-IQ
// producer.  create_xmtr opens that channel OPEN-but-NOT-STARTED (state=0);
// nothing processes the input ring until the channel is STARTED via
// SetChannelState.  The P1/HL2 path does this on keydown (main.cpp
// registerTxControl .start = SetChannelState(chid(1,0),1,0)); the P2 path
// was missing it entirely, so the pump's input was never modulated and the
// DUC FIFO sat at 0 (verified against deskHPSDR tx_on / Thetis console.cs
// :30345 — both SetChannelState the TX channel to run it; P2 differs from
// P1 only in output routing + CFIR, not in the channel start).  Called by
// the P2 DUC transport lifecycle (start with the pump, stop on teardown);
// edge-guarded and gated on the outbound seam being active so the channel's
// ILV output routes to the P2 DUC FIFO, never the P1 EP2 path.
void setP2TxCmasterChannelRunning(bool on);

} // namespace lyra::wire

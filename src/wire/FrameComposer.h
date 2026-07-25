// Lyra — C&C frame composer (§4a/§4b/§4c populated; §1-C Stage 4F.2
// dissolved class → free functions).
//
// Mirrors the reference's `WriteMainLoop_HL2` round-robin
// (`networkproto1.c:869-1191`) for HL2 / HL2+ dispatch.  Per the
// signed §4a parity checkpoint (`docs/architecture/
// PARITY_CHECKPOINTS.md §4a`, 2026-06-05) AND the §1-C Stage 4F.2
// follow-up (2026-06-06):
//
//   - Q1 — switch-style scheduler verbatim from the reference.
//   - Q2 — eager case presence: all 19 cases (0-18) in the switch.
//   - Q3 — per-family branches inline at each case site; HL2 path
//     implemented, non-HL2 paths `assert(false …)`.
//   - Q4 — NO concurrency guard (Round-1 audit 2026-06-06
//     correction).  Reference is single-threaded I/O — the wire-
//     egress thread owns the C&C register state and walks the
//     scheduler; control-thread setters MUST funnel through that
//     same wire-egress thread via the command-queue primitive,
//     never touching `prn->...` directly.  An earlier `cc_lock_`
//     class-member mutex / `g_cc_lock` TU-scope static was a
//     Lyra-native deviation, removed per "do as reference, period"
//     (operator-locked 2026-06-06).
//   - Q5 — cursor advance once per USB frame emit.
//
// §1-C Stage 4F.2 (sign-off 2026-06-06): the `FrameComposer`
// class was Lyra-native scaffolding with no reference counterpart
// (reference's `WriteMainLoop_HL2` is a free function in
// `networkproto1.c` using file-scope `out_control_idx` + `PreviousTXBit`
// globals).  Dissolved into namespace-scope free functions
// operating on TU-scope statics in `FrameComposer.cpp`:
//   - `out_control_idx_` member → `g_out_control_idx` TU-scope
//     (mirror of reference `networkproto1.c:27`)
//   - `previous_tx_bit_` member → `g_previous_tx_bit` TU-scope
//     (mirror of reference `networkproto1.c:29 PreviousTXBit`)
//   - `cc_lock_` mutex → REMOVED (reference single-threaded I/O;
//     control-thread setters funnel through the wire-egress
//     thread, never touching `prn->...` directly — Round-1 audit
//     2026-06-06 correction; see Q4)
//
// Sister-pattern of §1-C Stage 2A (Router) + Stage 4D (OutboundRing)
// dissolutions.

#pragma once

#include <cstdint>

namespace lyra::wire {

// ---- write_main_loop_hl2 ----
//
// MONOLITHIC reference-faithful equivalent of
// `WriteMainLoop_HL2(char* bufp)` (`networkproto1.c:869-1200`).
// Composes ALL of the wire-egress steps inside one function body
// matching the reference: 2-USB-frame sync + C&C bytes built
// into the TU-scope FPGA write buffer, LRIQ payload memcpy'd
// from the caller-supplied `out_bufp` (= reference's
// `prn->OutBufp` at the `:1262` call site) into the same FPGA
// write buffer at offsets [8..511] + [520..1023], then
// `metis_write_frame(0x02, ...)` + paired `outbound_notify_
// consumed_pair()` (the C++23-idiom equivalent of reference's
// `MetisWriteFrame + ReleaseSemaphore(hobbuffsRun[0/1])` at
// `:1198-1200`).
//
// Parameter `out_bufp` is the **LRIQ source** (matching reference
// parameter name `bufp` at `:869` — what the caller passes as
// `prn->OutBufp`), NOT the FPGA write destination.  The
// destination FPGAWriteBufp equivalent is a TU-scope static in
// FrameComposer.cpp, mirroring reference's file-scope global.
//
// Reference is single-threaded I/O — Lyra mirrors that contract:
// this function MUST be invoked from the wire-egress thread, and
// the same thread MUST own all state-mutating setter calls
// (funneled via the command-queue primitive — see Q4 above).
//
// §10.2-component-split-revert note (2026-06-08, operator
// directive "do as reference, period, NO PATCHING"): the prior
// Lyra structural split (write_main_loop_hl2 ONLY composed C&C;
// LRIQ memcpy + metis_write_frame + paired release lived in
// Ep2SendThread::process_one_pair) was a Lyra-native deviation
// from the reference's monolithic shape.  The §10.2 sign-off
// 2026-06-04 was reversed under the strict-fidelity rule; this
// function is now the full reference-faithful equivalent.
void write_main_loop_hl2(const char* out_bufp);   // P2.b: reference parameter type (WriteMainLoop_HL2(char* bufp), networkproto1.c:869); const added — the body only reads

// ===== Setter surface (§4a-scope, §1-C Stage 4F.2: free functions) =====
//
// The reference has no setter abstraction — operator code writes
// directly to `prn->rx[N].frequency` etc. from its single I/O
// thread.  Lyra adds these typed setters for compile-time
// per-family branching + bounds checking, but per Q4 they MUST
// be invoked on the wire-egress thread (control threads enqueue
// to the command-queue primitive; the egress thread drains the
// queue and calls these setters between scheduler walks — same
// single-threaded contract as the reference).

// Write the per-RX NCO frequency that case 2 (RX1) / case 3 (RX2)
// / case 5 (RX3 mirror) emits.  `rx_idx` is 0-based (0 = RX1,
// 1 = RX2, etc.); no-op if out of range or `prn` is nullptr.
void set_rx_freq(int rx_idx, int freq_hz);

// §4b-1.8 — Write the TX NCO frequency that case 1 emits
// (frame `0x02`).  Writes `prn->tx[0].frequency`.  No-op if
// `prn` is nullptr.  Must be called from the wire-egress thread
// (see Q4).
void set_tx_freq(int freq_hz);

// HL2 "Band Volts" output enable (MI0BOT / Ramdor gateware feature).
// Writes the C0=0x00 frame's C3 bit 3 — the ADC "dither" bit — which the
// gateware decodes as `band_volts_enabled` (control.v:582-584,
// `cmd_data[11]`) and then emits a per-band analog voltage on the fan-PWM
// pin for amps / tuners / antenna switches that band-follow off a band
// voltage.  Writes `prn->adc[0].dither`; caller guards `prn != nullptr`.
void set_band_volts_output(bool on);

// Frequency-calibration factor — a crystal/TCXO ppm trim applied to
// EVERY RX/TX wire frequency.  corrected_freq() is the shared P1/P2
// choke point; the P1 setters below and P2RxBridge both call it
// immediately before handing a frequency to their protocol encoder.
// Faithful mirror of the reference's `NetworkIO._freq_correction_factor`,
// which multiplies the requested freq in `VFOfreq()` (NetworkIO.cs:219)
// before it reaches the wire — so `prn->..[].frequency` holds the
// corrected value exactly as the reference's command struct does.
// Default 1.0 = no correction (exact fast-path, byte-identical output).
// Thread-safe (atomic); see freq_calibration_design.md.
int corrected_freq(int freq_hz);
void set_freq_correction(double factor);
double freq_correction();

// Write `prn->tx[0].drive_level` (case 10 C1).  Operator-axis
// input (the reference's `_tx_attenuator_data` is the same
// operator-axis value; SWR correction happens at Radio-layer
// BEFORE calling this setter).  No-op if `prn` is nullptr.
void set_drive_level(int level);

// PA enable.  Task #114 wire-layer fix (2026-06-08): drives
// the THREE bits the HL2 / HL2+ gateware reads for PA bias +
// T/R relay control:
//   - `prn->tx[0].pa`  → case-10 C3 bit 7 (LEGACY; HL2
//                        gateware IGNORES per ad9866.v RTL
//                        ground truth, but kept for non-HL2
//                        family parity)
//   - `ApolloTuner`    → case-10 C2 bit 3 (0x08, `pa_enable`
//                        per ad9866.v:209-220) — the actual
//                        PA bias enable on HL2+
//   - `ApolloFilt`     → case-10 C2 bit 2 (0x04, `tr_disable`
//                        per ad9866.v) — PA OFF sets this so
//                        the T/R relay cannot engage on a MOX
//                        edge (prevents driver-only RF
//                        leakage); PA ON clears it
//
// Operator-confirmed correct on N8SDR HL2+ via lyra-Python
// §15.26 commit `b68886d` (PA-enable reconcile that produced
// "first real RF").  See FrameComposer.cpp body for the full
// reference-citation + safety rationale.
//
// **Default-OFF safety at startup**: `create_rnet()` sets
// `ApolloFilt = 0x04` at session-open, matching the
// `set_pa_on(false)` safe state.  Operator must call
// `set_pa_on(true)` to enable RF; no accidental MOX edge
// can produce RF before that call.
void set_pa_on(bool on);

// §4b-2.5 TX step attenuator setter — **per-family branching**.
//
// For HL2 / HL2+ (`hpsdrModel == HPSDRModel::HERMESLITE`), applies
// the `(31 - signed_db) & 0x3F` inversion per the source-verified
// `console.cs:10657-10663` HL2-specific branch.  For non-HL2
// families, writes the raw value `& 0x3F`.
//
// Operator-axis input is signed dB (-28..+31 on HL2 per the
// hardware's bipolar range; 0..31 on ANAN-class).  Writes
// `prn->adc[0].tx_step_attn`; case 4 emits the 5-bit truncated
// form, case 11 emits the 6-bit + `0x40` enable form on the MOX
// branch.
//
// The Display.TXAttenuatorOffset panadapter compensation + the
// m_bATTonTX policy gate are operator-layer concerns deferred to
// Task #114.  This setter just writes the wire-encoded field.
void set_tx_step_attn_db(int signed_db);

// §4b-2.5 RX step attenuator setter — no inversion (any family).
// Per-ADC variant for ANAN multi-ADC radios; HL2 uses only
// `adc_idx = 0`.
void set_rx_step_attn_db(int signed_db, int adc_idx = 0);

}  // namespace lyra::wire

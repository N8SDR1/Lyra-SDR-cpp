// Lyra — C&C frame composer.  See FrameComposer.h.
//
// Source mirror: `networkproto1.c::WriteMainLoop_HL2`
// (lines 869-1191).  All 19 cases populated.
//
// §1-C Stage 4F.2 (sign-off 2026-06-06): the `FrameComposer`
// class was dissolved into namespace-scope free functions
// operating on TU-scope statics here (mirroring the reference's
// `WriteMainLoop_HL2` free function + file-scope
// `out_control_idx` / `PreviousTXBit` globals).
//
// Sister-pattern of §1-C Stage 2A (Router) + Stage 4D
// (OutboundRing) dissolutions.

#include "wire/FrameComposer.h"
#include "wire/MetisFrame.h"
#include "wire/RadioNet.h"
#include "wire/RbpFilter.h"

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

#include <QtGlobal>   // CW wire-probe (LYRA_CW_DEBUG) — diagnostic only

namespace lyra::wire {

// ---- TU-scope state (§1-C Stage 4F.2 + §10.2-revert 2026-06-08) ----
//
// Direct mirrors of reference file-scope globals:
//   `out_control_idx` at `networkproto1.c:27`
//   `PreviousTXBit`   at `networkproto1.c:29`
//   `FPGAWriteBufp`   at `network.h:499` (NOT in `_radionet`)
//
// ⚠ Rule 24 — no concurrency guard.  Reference is single-
// threaded I/O (one wire-egress thread invokes the entire
// `WriteMainLoop_HL2` walk + all setters happen via the same
// command surface).  Lyra mirrors that contract verbatim per
// the operator-locked "do as reference, period" rule
// (2026-06-06): the wire layer owns these registers; control-
// thread setters MUST funnel through the same wire-egress
// thread (e.g. via the command-queue primitive) — they may NOT
// touch `prn->...` directly.  An earlier `std::mutex g_cc_lock`
// was a Lyra-native deviation caught by Round-1 audit 2026-06-06
// and removed.
//
// `g_fpga_write_bufp` moved here from `Ep2SendThread.cpp` on
// 2026-06-08 per operator directive "do as reference, period,
// NO PATCHING" (Task #121).  Reference allocates `FPGAWriteBufp`
// at `networkproto1.c:428` inside the EP6 thread init (yes,
// inside the RX thread — that's where reference puts it);
// Lyra lazy-inits on first `write_main_loop_hl2` call (sub-µs
// branch in steady state).  Sister of `g_fpga_read_bufp` in
// `wire/Ep6RecvThread.cpp` + `g_metis_out_seq_num` in
// `wire/MetisFrame.cpp` — all three are reference file-scope
// globals correctly mirrored as TU-scope statics in their
// respective wire-layer translation units.
namespace {
int g_out_control_idx = 0;
int g_previous_tx_bit = 0;

// Sample-slot counts mirroring the reference per-USB-frame
// budget.  Reference uses literal `63` / `2 * 63` / `8 * 63`
// at networkproto1.c:1241 etc.; Lyra centralizes here.
constexpr int kSampleSlotsPerFrame    = 63;        // = 504/8 bytes
constexpr int kLriqBytesPerFrame      = 8 * kSampleSlotsPerFrame;  // 504
constexpr int kLriqBytesPerDatagram   = 2 * kLriqBytesPerFrame;    // 1008
constexpr std::size_t kFpgaPayloadBytes = 1024;    // 2 × 512-byte USB frames

// EP2 endpoint identifier.  Reference: `MetisWriteFrame(0x02,
// FPGAWriteBufp);` at `networkproto1.c:1198`.
constexpr int kEp2Endpoint = 0x02;

// FPGAWriteBufp equivalent — 1024-byte EP2 raw send buffer
// (8-byte HPSDR header NOT included — added by
// `metis_write_frame()` before sendto).  Sized + zeroed lazily
// on first `write_main_loop_hl2` call.
std::vector<std::uint8_t> g_fpga_write_bufp;

// Frequency-calibration factor.  Mirror of the reference file-scope
// static `NetworkIO._freq_correction_factor` (default 1.0).  Atomic so
// the operator-facing setter and the freq-write path stay race-free even
// though the reference tolerates a plain double under its single-thread
// I/O contract.  See FrameComposer.h / freq_calibration_design.md.
std::atomic<double> g_freq_correction{1.0};

// Apply the ppm trim to a frequency the instant before it is stored to
// `prn`.  Exact `== 1.0` fast path → default output is byte-identical to
// pre-calibration (a bit-exact no-op), so a fresh install / Reset tunes
// exactly as before.  Mirror of `VFOfreq`: `f_freq = f * factor`.
int corrected_freq(int freq_hz) {
    const double f = g_freq_correction.load(std::memory_order_relaxed);
    if (f == 1.0) return freq_hz;
    return static_cast<int>(std::llround(static_cast<double>(freq_hz) * f));
}
}  // namespace

// =================== §4a setters =====================================
//
// Reference has NO setter abstraction — operator code in
// `Console/radio.cs` etc. writes `prn->...` fields directly.  Lyra
// keeps thin free-function wrappers for the C++23 idiomatic call
// site but the bodies are reduced to single-line writes that mirror
// what the reference operator code does literally.  Defensive
// guards (bounds checks, null checks, extra masks at the field-
// store level) were Lyra-native additions caught by 2026-06-06
// TX-Agent-2 setter audit and removed per "do as reference,
// period."

void set_rx_freq(int rx_idx, int freq_hz) {
    prn->rx[rx_idx].frequency = corrected_freq(freq_hz);
}

// §4b-1.8 — TX NCO freq for case 1.
void set_tx_freq(int freq_hz) {
    prn->tx[0].frequency = corrected_freq(freq_hz);
}

// HL2 "Band Volts" enable → C0=0x00 frame C3 bit 3 (ADC dither bit).
// compose_case_0 reads `prn->adc[0].dither` into C3 bit 3; the gateware
// (control.v:582-584) latches it as `band_volts_enabled`.
void set_band_volts_output(bool on) {
    prn->adc[0].dither = on ? 1 : 0;
}

// Frequency-calibration factor accessors (see FrameComposer.h).  The
// factor multiplies inside the two setters above, so it covers every
// tune path uniformly and `prn->..[].frequency` always holds the
// corrected wire value — mirror of the reference command-struct state.
void set_freq_correction(double factor) {
    g_freq_correction.store(factor, std::memory_order_relaxed);
}
double freq_correction() {
    return g_freq_correction.load(std::memory_order_relaxed);
}

// =================== §4b-2.5 setters =================================

// §4b-2.5 — `prn->tx[0].drive_level` (case 10 C1).
void set_drive_level(int level) {
    prn->tx[0].drive_level = level;
}

// §4b-2.5 — PA enable.  Task #114 wire-layer fix
// (2026-06-08): drives BOTH the legacy C3-bit-7 path AND the
// Apollo C2-bit-3 (`pa_enable`) + C2-bit-2 (`tr_disable`) bits
// that the HL2/HL2+ gateware actually reads per ad9866.v RTL
// ground truth (§15.26 / HL2 wiki register `0x09[19] =
// pa_enable`, `0x09[18] = tr_disable`).
//
// Legacy C3 bit 7 (`prn->tx[0].pa`) — HL2 gateware IGNORES
// this bit per the §15.26 R1' RTL audit (`data[19]` is the
// actual enable, NOT C3 bit 7).  Kept for reference parity +
// non-HL2 (ANAN-class) family paths where the legacy bit may
// still matter.
//
// C2 bit 3 (`ApolloTuner = 0x08`) — the load-bearing PA-enable
// bit on Apollo-gated HL2+ gateware.  Mirrors reference's
// `EnableApolloTuner(bits)` at `netInterface.c:578-585` +
// `DisablePA(bit)` at `:625-636` (which on HL2 calls
// `EnableApolloTuner(!bit)`).  Operator-empirically confirmed
// on N8SDR HL2+ as the actually-functional PA enable mechanism
// per lyra-Python §15.26 commit `b68886d` ("PA-enable C2 bit 3
// (ApolloTuner) never reaching the wire" → first real RF).
//
// C2 bit 2 (`ApolloFilt = 0x04`) — `tr_disable` per gateware.
// PA OFF ⇒ tr_disable SET (T/R relay disabled even during
// MOX, prevents driver-only RF leakage onto the antenna
// terminal); PA ON ⇒ tr_disable CLEAR (relay engages
// normally for TX).  Pairing with `ApolloTuner` per the
// pihpsdr / HL2-wiki convention; matches the safe state
// `create_rnet()` initializes (ApolloFilt = 0x04 at startup
// = same as the post-`set_pa_on(false)` state).
//
// NOTE on dual-purpose bits: `ApolloFilt` and
// `ApolloFiltSelect` carry both the tr_disable / PA-relay-
// gating semantics (HL2 base hardware) AND the Apollo
// daughterboard filter operator-feature bits (Apollo-equipped
// HL2+).  Until Apollo daughterboard support lands as a
// dedicated operator-feature setter, `set_pa_on` owns
// `ApolloFilt` to keep the gateware semantic correct on
// base HL2 hardware.  When daughterboard support arrives, a
// `set_apollo_filter(...)` sibling setter coordinates with
// `set_pa_on` to manage the operator-feature aspect without
// breaking the tr_disable contract.
void set_pa_on(bool on) {
    prn->tx[0].pa = on ? 1 : 0;
    ApolloTuner   = on ? 0x08 : 0;
    ApolloFilt    = on ? 0    : 0x04;
}

// §4b-2.5 — TX step attenuator setter.  HL2 (`console.cs:
// 10657-10658`): operator code calls `SetTxAttenData(31 -
// _tx_attenuator_data)` which stores into
// `prn->adc[0].tx_step_attn`; case 4 then truncates to 5 bits
// (`networkproto1.c:1019`), case 11 to 6 bits + `0x40` enable
// (`:1099-1102`).  Lyra mirrors that storage convention at the
// setter so the wire bytes come out byte-identical to the
// reference.  Non-HL2 families (ANAN/Orion/RedPitaya) take the
// raw operator value with NO inversion per the reference C#
// path — `assert(false)` until tester hardware lands.
void set_tx_step_attn_db(int signed_db) {
    int wire_value;
    switch (hpsdrModel) {
        case HPSDRModel::HERMESLITE:
        case HPSDRModel::HPSDR:  // HL2-class
            // Reference HL2 stores `31 - x` raw; wire-site masks
            // do the truncation (case 4 `& 0x1F`, case 11
            // `& 0x3F | 0x40`).  No extra setter-level mask.
            wire_value = 31 - signed_db;
            break;
        default:
            assert(false && "non-HL2 TX step ATT encoding not yet "
                            "implemented — needs operator bench "
                            "verification");
            wire_value = signed_db;  // unreachable; compiler-quiet
            break;
    }
    prn->adc[0].tx_step_attn = wire_value;
}

// §4b-2.5 — RX step attenuator setter.  Reference writes
// `prn->adc[N].rx_step_attn` raw via operator code; wire-site
// masks at case 11 (`& 0x3F`), case 12 C1 (no mask on RX branch),
// case 12 C2 (`& 0x1F`) do the truncation.  No setter-level mask.
void set_rx_step_attn_db(int signed_db, int adc_idx) {
    prn->adc[adc_idx].rx_step_attn = signed_db;
}

// =================== §4a.3 case 0 ====================================
//
// Source: networkproto1.c:948-970, HL2 dispatch.

void compose_case_0([[maybe_unused]] unsigned char& C0,
                                   unsigned char& C1,
                                   unsigned char& C2, unsigned char& C3,
                                   unsigned char& C4) {
    // Defensive: §4a-scope is WIRE-INERT; compose_case_0 should
    // never run with null prn / prbpfilter, but assert defensively
    // so a future wire-up that forgets to allocate them fails LOUD.

    // C0 is unchanged for case 0 — it carries only the XmitBit
    // base set in the caller (no `C0 |= addr` here; addr 0 leaves
    // bits 7..1 zero).  Parameter is `[[maybe_unused]]` to express
    // this in code; reference's case-0 body @ networkproto1.c:948-
    // 970 also has no C0 write.

    // C1 — sample-rate code (networkproto1.c:949)
    C1 = static_cast<unsigned char>(SampleRateIn2Bits & 3);

    // C2 — CW EER flag (bit 0) + OC pins shifted into bits 7..1
    // (networkproto1.c:950)
    C2 = static_cast<unsigned char>(
            (prn->cw.eer & 1)
          | ((prn->oc_output << 1) & 0xFE));

    // C3 — six OR'd bitfields then a three-way conditional on the
    // RX-input bits (networkproto1.c:951-959)
    C3 = static_cast<unsigned char>(
            (prbpfilter->_10_dB_Atten & 1)
          | ((prbpfilter->_20_dB_Atten << 1) & 0b00000010)
          | ((prn->rx[0].preamp        << 2) & 0b00000100)
          | ((prn->adc[0].dither       << 3) & 0b00001000)
          | ((prn->adc[0].random       << 4) & 0b00010000)
          | ((prbpfilter->_Rx_1_Out    << 7) & 0b10000000));

    if (prbpfilter->_XVTR_Rx_In)
        C3 |= 0b01100000;
    else if (prbpfilter->_Rx_1_In)
        C3 |= 0b00100000;
    else if (prbpfilter->_Rx_2_In)
        C3 |= 0b01000000;

    // C4 — antenna-select 3-way + duplex bit + nddc-1<<3 +
    // diversity<<7 (networkproto1.c:961-969)
    if (prbpfilter->_ANT_3 == 1)
        C4 = 0b10;
    else if (prbpfilter->_ANT_2 == 1)
        C4 = 0b01;
    else
        C4 = 0b0;
    C4 |= static_cast<unsigned char>(0b00000100);              // duplex bit
    C4 |= static_cast<unsigned char>((nddc - 1) << 3);          // DDC count
    C4 |= static_cast<unsigned char>((P1_en_diversity) << 7);   // diversity lock
}

// =================== §4a.4 case 2 ====================================
//
// Source: networkproto1.c:982-993, HL2 dispatch.
// DDC0 is always RX1 frequency, except in the nddc=2 (Hermes II)
// PS-on TX state where it carries TX frequency.  On HL2 nddc=4 the
// nddc==2 branch is a structural no-op (preserved verbatim).

void compose_case_2(unsigned char& C0, unsigned char& C1,
                                   unsigned char& C2, unsigned char& C3,
                                   unsigned char& C4) {

    C0 |= 4;  // addr 2: C0 |= (addr << 1) → 0x04

    int ddc_freq;
    if ((nddc == 2) && (XmitBit == 1) && (prn->puresignal_run))
        ddc_freq = prn->tx[0].frequency;
    else
        ddc_freq = prn->rx[0].frequency;

    C1 = static_cast<unsigned char>((ddc_freq >> 24) & 0xff);
    C2 = static_cast<unsigned char>((ddc_freq >> 16) & 0xff);
    C3 = static_cast<unsigned char>((ddc_freq >>  8) & 0xff);
    C4 = static_cast<unsigned char>((ddc_freq      ) & 0xff);
}

// =================== §4a.5 case 3 ====================================
//
// Source: networkproto1.c:995-1010, HL2 dispatch.
// Three-way conditional preserved verbatim:
//   (1) Hermes-II nddc=2 + PS + TX → DDC1 = TX freq;
//   (2) Orion / ANAN P1 nddc=5 → DDC1 = RX1 freq;
//   (3) default HL2 nddc=4 → DDC1 = RX2 freq.

void compose_case_3(unsigned char& C0, unsigned char& C1,
                                   unsigned char& C2, unsigned char& C3,
                                   unsigned char& C4) {

    C0 |= 6;  // addr 3: C0 |= (addr << 1) → 0x06

    int ddc_freq;
    if ((nddc == 2) && (XmitBit == 1) && (prn->puresignal_run))
        ddc_freq = prn->tx[0].frequency;
    else if (nddc == 5)
        ddc_freq = prn->rx[0].frequency;
    else
        ddc_freq = prn->rx[1].frequency;  // default HL2 nddc=4 path

    C1 = static_cast<unsigned char>((ddc_freq >> 24) & 0xff);
    C2 = static_cast<unsigned char>((ddc_freq >> 16) & 0xff);
    C3 = static_cast<unsigned char>((ddc_freq >>  8) & 0xff);
    C4 = static_cast<unsigned char>((ddc_freq      ) & 0xff);
}

// =================== §4b-1.1 case 1 ==================================
//
// Source: networkproto1.c:974-980, HL2 dispatch.
// TX VFO — DDC2/3 mirror it on HL2 nddc=4 (see §4c case 6).

void compose_case_1(unsigned char& C0, unsigned char& C1,
                                   unsigned char& C2, unsigned char& C3,
                                   unsigned char& C4) {

    C0 |= 2;  // addr 1: C0 |= (addr << 1) → 0x02

    C1 = static_cast<unsigned char>((prn->tx[0].frequency >> 24) & 0xff);
    C2 = static_cast<unsigned char>((prn->tx[0].frequency >> 16) & 0xff);
    C3 = static_cast<unsigned char>((prn->tx[0].frequency >>  8) & 0xff);
    C4 = static_cast<unsigned char>((prn->tx[0].frequency      ) & 0xff);
}

// =================== §4b-1.2 case 4 ==================================
//
// Source: networkproto1.c:1015-1021, HL2 dispatch.
// ADC assignments (`P1_adc_cntrl`) + TX step attenuator narrow 5-bit
// form.  The wider 6-bit MOX-gated form lives in case 11 (§4b-2) —
// both read the same `prn->adc[0].tx_step_attn` field.  No `31 - x`
// inversion at this layer; the inversion is the setter's job
// (`set_tx_step_attn_db`, lands with §4b-2).
//
// Note on C2: the literal mask `0b0011111111` is 10 bits but the
// destination is `unsigned char` (8 bits) — the upper 2 bits of
// the mask are structurally unreachable.  Preserved verbatim per
// Rule 24 (don't "clean up" reference quirks).

void compose_case_4(unsigned char& C0, unsigned char& C1,
                                   unsigned char& C2, unsigned char& C3,
                                   unsigned char& C4) {

    C0 |= 0x1c;  // addr 14 (0x0e in reference comment): C0 |= 0x0e << 1 = 0x1c

    C1 = static_cast<unsigned char>(P1_adc_cntrl & 0xFF);
    C2 = static_cast<unsigned char>((P1_adc_cntrl >> 8) & 0b0011111111);  // verbatim 10-bit mask
    C3 = static_cast<unsigned char>(prn->adc[0].tx_step_attn & 0b00011111);
    C4 = 0;
}

// =================== §4b-1.3 case 13 =================================
//
// Source: networkproto1.c:1127-1133, HL2 dispatch.
// CW enable bitfield + sidetone level + RF delay.

void compose_case_13(unsigned char& C0, unsigned char& C1,
                                    unsigned char& C2, unsigned char& C3,
                                    unsigned char& C4) {

    C0 |= 0x1e;  // addr 15: C0 |= 0x0f << 1 = 0x1e

    // `prn->cw.cw_enable` is a 1-bit bitfield in `_cw.mode_control`
    // (§1.5).  Bitfield → integer assignment yields 0 or 1.
    C1 = static_cast<unsigned char>(prn->cw.cw_enable);
    C2 = static_cast<unsigned char>(prn->cw.sidetone_level);
    C3 = static_cast<unsigned char>(prn->cw.rf_delay);
    C4 = 0;
}

// =================== §4b-1.4 case 14 =================================
//
// Source: networkproto1.c:1135-1141, HL2 dispatch.
// CW hang_delay split 10-bit across C1/C2; CW sidetone_freq split
// 12-bit across C3/C4.

void compose_case_14(unsigned char& C0, unsigned char& C1,
                                    unsigned char& C2, unsigned char& C3,
                                    unsigned char& C4) {

    C0 |= 0x20;  // addr 16: C0 |= 0x10 << 1 = 0x20

    // 10-bit hang_delay: upper 8 in C1, lower 2 in C2
    C1 = static_cast<unsigned char>((prn->cw.hang_delay >> 2) & 0b11111111);
    C2 = static_cast<unsigned char>(prn->cw.hang_delay        & 0b00000011);

    // 12-bit sidetone_freq: upper 8 in C3, lower 4 in C4
    C3 = static_cast<unsigned char>((prn->cw.sidetone_freq >> 4) & 0b11111111);
    C4 = static_cast<unsigned char>(prn->cw.sidetone_freq        & 0b00001111);
}

// =================== §4b-1.5 case 15 =================================
//
// Source: networkproto1.c:1143-1149, HL2 dispatch.
// EER PWM min/max — each 10-bit, split across two bytes.

void compose_case_15(unsigned char& C0, unsigned char& C1,
                                    unsigned char& C2, unsigned char& C3,
                                    unsigned char& C4) {

    C0 |= 0x22;  // addr 17: C0 |= 0x11 << 1 = 0x22

    // 10-bit epwm_min: upper 8 in C1, lower 2 in C2
    C1 = static_cast<unsigned char>((prn->tx[0].epwm_min >> 2) & 0b11111111);
    C2 = static_cast<unsigned char>(prn->tx[0].epwm_min        & 0b00000011);

    // 10-bit epwm_max: upper 8 in C3, lower 2 in C4
    C3 = static_cast<unsigned char>((prn->tx[0].epwm_max >> 2) & 0b11111111);
    C4 = static_cast<unsigned char>(prn->tx[0].epwm_max        & 0b00000011);
}

// =================== §4b-1.6 case 17 =================================
//
// Source: networkproto1.c:1162-1168, HL2 dispatch.
// HL2 TX-latency register — the "0x2e" enhancement.  5-bit PTT hang
// in C3, 7-bit TX latency (ms) in C4.

void compose_case_17(unsigned char& C0, unsigned char& C1,
                                    unsigned char& C2, unsigned char& C3,
                                    unsigned char& C4) {

    C0 |= 0x2e;  // addr 23: C0 |= 0x17 << 1 = 0x2e

    C1 = 0;
    C2 = 0;
    C3 = static_cast<unsigned char>(prn->tx[0].ptt_hang   & 0b00011111);  // 5-bit
    C4 = static_cast<unsigned char>(prn->tx[0].tx_latency & 0b01111111);  // 7-bit
}

// =================== §4b-1.7 case 18 =================================
//
// Source: networkproto1.c:1170-1176, HL2 dispatch.
// HL2 safety bit — when non-zero the gateware auto-resets on host
// disconnect.  Per §15.26 history, defaults to 0 to avoid the wedge
// defect where a clean Lyra stop triggered gateware reset.

void compose_case_18(unsigned char& C0, unsigned char& C1,
                                    unsigned char& C2, unsigned char& C3,
                                    unsigned char& C4) {

    C0 |= 0x74;  // addr 58: C0 |= 0x3a << 1 = 0x74

    C1 = 0;
    C2 = 0;
    C3 = 0;
    C4 = static_cast<unsigned char>(prn->reset_on_disconnect);
}

// =================== §4b-2.1 case 10 =================================
//
// Source: networkproto1.c:1076-1089, HL2 dispatch.
// Drive level + Apollo PA / filter / tuner / ATU bits + mic_boost
// + line_in + per-band HPF/LPF + legacy PA bit.

void compose_case_10(unsigned char& C0, unsigned char& C1,
                                    unsigned char& C2, unsigned char& C3,
                                    unsigned char& C4) {

    C0 |= 0x12;  // addr 9: C0 |= 0x09 << 1 = 0x12

    // C1 — drive level (operator-axis; SWR correction applied at
    // Radio-layer caller, NOT here).  Source: :1078.
    C1 = static_cast<unsigned char>(prn->tx[0].drive_level);

    // C2 — mic_boost (bit 0) + line_in (bit 1) + Apollo globals
    // OR'd inline + forced bit 6 (0x40), masked to 7 bits.  Source:
    // :1079-1080.  The Apollo globals hold pre-shifted bit-values
    // (or zero) per the reference convention.
    C2 = static_cast<unsigned char>(
            ((prn->mic.mic_boost & 1)
          | ((prn->mic.line_in & 1) << 1)
          | ApolloFilt
          | ApolloTuner
          | ApolloATU
          | ApolloFiltSelect
          | 0b01000000) & 0x7f);

    // C3 — bit 0-4 = 5 HPF bands; bit 5 = Bypass; bit 6 = 6M_preamp;
    // bit 7 = legacy `prn->tx[0].pa`.  Source: :1081-1084.
    C3 = static_cast<unsigned char>(
            (prbpfilter->_13MHz_HPF  & 1)
          | ((prbpfilter->_20MHz_HPF & 1) << 1)
          | ((prbpfilter->_9_5MHz_HPF & 1) << 2)
          | ((prbpfilter->_6_5MHz_HPF & 1) << 3)
          | ((prbpfilter->_1_5MHz_HPF & 1) << 4)
          | ((prbpfilter->_Bypass     & 1) << 5)
          | ((prbpfilter->_6M_preamp  & 1) << 6)
          | ((prn->tx[0].pa           & 1) << 7));

    // C4 — 7 per-band LPF bits, bit 7 unused.  Source: :1085-1088.
    C4 = static_cast<unsigned char>(
            (prbpfilter->_30_20_LPF  & 1)
          | ((prbpfilter->_60_40_LPF & 1) << 1)
          | ((prbpfilter->_80_LPF    & 1) << 2)
          | ((prbpfilter->_160_LPF   & 1) << 3)
          | ((prbpfilter->_6_LPF     & 1) << 4)
          | ((prbpfilter->_12_10_LPF & 1) << 5)
          | ((prbpfilter->_17_15_LPF & 1) << 6));
}

// =================== §4b-2.2 case 11 =================================
//
// Source: networkproto1.c:1091-1103, HL2 dispatch.
// Preamps (4 bits, incl. the `rx[0].preamp << 3` reference quirk
// preserved verbatim) + mic_trs/bias/ptt + line_in_gain +
// puresignal_run + user_dig_out + MOX-gated 6-bit step ATT + 0x40
// enable.
//
// C4 MOX-gated is the §15.26-history load-bearing RX-ADC protection
// mechanism — during TX, the operator-policy layer ensures
// `prn->adc[0].tx_step_attn` is set to the protective value
// (Task #114 ATT-on-TX policy).

void compose_case_11(unsigned char& C0, unsigned char& C1,
                                    unsigned char& C2, unsigned char& C3,
                                    unsigned char& C4) {

    C0 |= 0x14;  // addr 10: C0 |= 0x0a << 1 = 0x14

    // C1 — bits 0/1/2 = rx[0/1/2].preamp; bit 3 = rx[0].preamp
    // AGAIN (reference quirk preserved per Rule 24); bits 4/5/6 =
    // mic_trs / mic_bias / mic_ptt.  Source: :1093-1096.
    C1 = static_cast<unsigned char>(
            (prn->rx[0].preamp & 1)
          | ((prn->rx[1].preamp & 1) << 1)
          | ((prn->rx[2].preamp & 1) << 2)
          | ((prn->rx[0].preamp & 1) << 3)   // verbatim duplicate
          | ((prn->mic.mic_trs  & 1) << 4)
          | ((prn->mic.mic_bias & 1) << 5)
          | ((prn->mic.mic_ptt  & 1) << 6));

    // C2 — line_in_gain (5-bit) + puresignal_run bit at 6.  Source:
    // :1097.
    C2 = static_cast<unsigned char>(
            (prn->mic.line_in_gain & 0b00011111)
          | ((prn->puresignal_run & 1) << 6));

    // C3 — user_dig_out 4-bit.  Source: :1098.
    C3 = static_cast<unsigned char>(prn->user_dig_out & 0b00001111);

    // C4 — MOX-gated 6-bit step ATT + 0x40 enable bit.  Source:
    // :1099-1102.  This is the §15.26-load-bearing RX-ADC
    // protection mechanism.
    if (XmitBit) {
        C4 = static_cast<unsigned char>(
                (prn->adc[0].tx_step_attn & 0b00111111) | 0b01000000);
    } else {
        C4 = static_cast<unsigned char>(
                (prn->adc[0].rx_step_attn & 0b00111111) | 0b01000000);
    }
}

// =================== §4b-2.3 case 12 =================================
//
// Source: networkproto1.c:1105-1123, HL2 dispatch.
// ADC1/ADC2 step ATT (XmitBit-force on ADC1) + CW keyer config.
// Two reference quirks preserved verbatim per Rule 24:
//   - No explicit `& 0x1F` mask on `adc[1].rx_step_attn` RX branch
//   - No `& 1` mask on `strict_spacing` (harmless — 1-bit bitfield)

void compose_case_12(unsigned char& C0, unsigned char& C1,
                                    unsigned char& C2, unsigned char& C3,
                                    unsigned char& C4) {

    C0 |= 0x16;  // addr 11: C0 |= 0x0b << 1 = 0x16

    // C1 — XmitBit force-31 on ADC1 ATT, then OR 0x20 enable bit.
    // RX branch does NOT mask `adc[1].rx_step_attn` — reference
    // quirk preserved.  Source: :1107-1111.
    if (XmitBit) {
        C1 = 0x1F;
    } else {
        C1 = static_cast<unsigned char>(prn->adc[1].rx_step_attn);
    }
    C1 |= 0b00100000;  // 0x20 enable bit

    // C2 — ADC2 5-bit step ATT + 0x20 enable + rev_paddle bit 6.
    // Source: :1112-1113.
    C2 = static_cast<unsigned char>(
            (prn->adc[2].rx_step_attn & 0b00011111)
          | 0b00100000
          | ((prn->cw.rev_paddle & 1) << 6));

    // CWMode 3-way conditional.  Source: :1115-1120.
    unsigned char CWMode;
    if (prn->cw.iambic == 0)
        CWMode = 0b00000000;
    else if (prn->cw.mode_b == 0)
        CWMode = 0b01000000;
    else
        CWMode = 0b10000000;

    // C3 — keyer_speed (6-bit) + CWMode (bits 6-7).  Source: :1121.
    C3 = static_cast<unsigned char>(
            (prn->cw.keyer_speed & 0b00111111) | CWMode);

    // C4 — keyer_weight (7-bit) + strict_spacing at bit 7.
    // `strict_spacing` has no `& 1` mask in the reference; harmless
    // because it's a 1-bit bitfield.  Source: :1122.
    C4 = static_cast<unsigned char>(
            (prn->cw.keyer_weight & 0b01111111)
          | ((prn->cw.strict_spacing) << 7));
}

// =================== §4b-2.4 case 16 =================================
//
// Source: networkproto1.c:1151-1160, HL2 dispatch.
// BPF2 (Alex1 secondary band-pass filter board) HPF/Bypass/preamp
// bits read from `prbpfilter2` + xvtr_enable + puresignal_run bit.
//
// The `_rx2_gnd << 7` has NO `& 1` mask in the reference — quirk
// preserved per Rule 24 (harmless because `_rx2_gnd` is a 1-bit
// bitfield in §2 RbpFilter2).

void compose_case_16(unsigned char& C0, unsigned char& C1,
                                    unsigned char& C2, unsigned char& C3,
                                    unsigned char& C4) {

    C0 |= 0x24;  // addr 18: C0 |= 0x12 << 1 = 0x24

    // C1 — 5 Alex1 HPF bands + Bypass + 6M_preamp + `_rx2_gnd` at
    // bit 7 (no `& 1` mask preserved verbatim).  Source: :1153-1156.
    C1 = static_cast<unsigned char>(
            (prbpfilter2->_13MHz_HPF  & 1)
          | ((prbpfilter2->_20MHz_HPF & 1) << 1)
          | ((prbpfilter2->_9_5MHz_HPF & 1) << 2)
          | ((prbpfilter2->_6_5MHz_HPF & 1) << 3)
          | ((prbpfilter2->_1_5MHz_HPF & 1) << 4)
          | ((prbpfilter2->_Bypass     & 1) << 5)
          | ((prbpfilter2->_6M_preamp  & 1) << 6)
          | ((prbpfilter2->_rx2_gnd          ) << 7));  // no `& 1` per source

    // C2 — xvtr_enable (bit 0) + puresignal_run (bit 6).  Source:
    // :1157.
    C2 = static_cast<unsigned char>(
            (xvtr_enable & 1)
          | ((prn->puresignal_run & 1) << 6));

    // C3 / C4 — unused on case 16.  Source: :1158-1159.
    C3 = 0;
    C4 = 0;
}

// =================== §4c.1 case 5 ====================================
//
// Source: networkproto1.c:1023-1034, HL2 dispatch.
// RX3 VFO / DDC2.  Per-family conditional: nddc=5 (Orion) → DDC2
// carries RX2 freq stored at `prn->rx[3].frequency` (reference
// indexing convention — array slot 3 holds Orion's RX2 freq);
// HL2 / Hermes (nddc=2/4) → DDC2 carries TX freq.

void compose_case_5(unsigned char& C0, unsigned char& C1,
                                   unsigned char& C2, unsigned char& C3,
                                   unsigned char& C4) {

    C0 |= 8;  // addr 4: C0 |= 0x04 << 1 = 0x08

    int ddc_freq;
    if (nddc == 5)
        ddc_freq = prn->rx[3].frequency;  // Orion path (verbatim rx[3])
    else
        ddc_freq = prn->tx[0].frequency;  // HL2 / Hermes path

    C1 = static_cast<unsigned char>((ddc_freq >> 24) & 0xff);
    C2 = static_cast<unsigned char>((ddc_freq >> 16) & 0xff);
    C3 = static_cast<unsigned char>((ddc_freq >>  8) & 0xff);
    C4 = static_cast<unsigned char>((ddc_freq      ) & 0xff);
}

// =================== §4c.2 case 6 ====================================
//
// Source: networkproto1.c:1036-1044, HL2 dispatch.
// RX4 VFO / DDC3 — always TX frequency on all families.

void compose_case_6(unsigned char& C0, unsigned char& C1,
                                   unsigned char& C2, unsigned char& C3,
                                   unsigned char& C4) {

    C0 |= 0x0a;  // addr 5: C0 |= 0x05 << 1 = 0x0a

    int ddc_freq = prn->tx[0].frequency;
    C1 = static_cast<unsigned char>((ddc_freq >> 24) & 0xff);
    C2 = static_cast<unsigned char>((ddc_freq >> 16) & 0xff);
    C3 = static_cast<unsigned char>((ddc_freq >>  8) & 0xff);
    C4 = static_cast<unsigned char>((ddc_freq      ) & 0xff);
}

// =================== §4c.3 case 7 ====================================
//
// Source: networkproto1.c:1046-1054, HL2 dispatch.
// RX5 VFO / DDC4 — used as TX frequency for Orion2 PS feedback;
// for other families "not used, so make TX always" per reference
// comment.

void compose_case_7(unsigned char& C0, unsigned char& C1,
                                   unsigned char& C2, unsigned char& C3,
                                   unsigned char& C4) {

    C0 |= 0x0c;  // addr 6: C0 |= 0x06 << 1 = 0x0c

    int ddc_freq = prn->tx[0].frequency;
    C1 = static_cast<unsigned char>((ddc_freq >> 24) & 0xff);
    C2 = static_cast<unsigned char>((ddc_freq >> 16) & 0xff);
    C3 = static_cast<unsigned char>((ddc_freq >>  8) & 0xff);
    C4 = static_cast<unsigned char>((ddc_freq      ) & 0xff);
}

// =================== §4c.4 case 8 ====================================
//
// Source: networkproto1.c:1056-1064, HL2 dispatch.
// RX6 VFO / DDC5 — "not used" per reference comment; emits
// `prn->rx[0].frequency` as the benign default (real freq, not
// garbage on the wire).

void compose_case_8(unsigned char& C0, unsigned char& C1,
                                   unsigned char& C2, unsigned char& C3,
                                   unsigned char& C4) {

    C0 |= 0x0e;  // addr 7: C0 |= 0x07 << 1 = 0x0e

    int ddc_freq = prn->rx[0].frequency;
    C1 = static_cast<unsigned char>((ddc_freq >> 24) & 0xff);
    C2 = static_cast<unsigned char>((ddc_freq >> 16) & 0xff);
    C3 = static_cast<unsigned char>((ddc_freq >>  8) & 0xff);
    C4 = static_cast<unsigned char>((ddc_freq      ) & 0xff);
}

// =================== §4c.5 case 9 ====================================
//
// Source: networkproto1.c:1066-1074, HL2 dispatch.
// RX7 VFO / DDC6 — "not used" per reference comment; same benign
// default pattern as case 8 (emits `prn->rx[0].frequency`).

void compose_case_9(unsigned char& C0, unsigned char& C1,
                                   unsigned char& C2, unsigned char& C3,
                                   unsigned char& C4) {

    C0 |= 0x10;  // addr 8: C0 |= 0x08 << 1 = 0x10

    int ddc_freq = prn->rx[0].frequency;
    C1 = static_cast<unsigned char>((ddc_freq >> 24) & 0xff);
    C2 = static_cast<unsigned char>((ddc_freq >> 16) & 0xff);
    C3 = static_cast<unsigned char>((ddc_freq >>  8) & 0xff);
    C4 = static_cast<unsigned char>((ddc_freq      ) & 0xff);
}

// =================== §4a.1 main scheduler ============================
//
// Source mirror: networkproto1.c::WriteMainLoop_HL2 lines 869-1191.
// Structural elements (per §4a.1 row table):
//   - Two USB frames per call (outer txframe loop)
//   - Three sync bytes 0x7f at [0..2] of each USB frame
//   - MOX-edge jump (nddc=2 only — no-op on HL2)
//   - C0 base init = XmitBit (MOX bit at bit 0)
//   - I2C-transaction overlay (overrides C0..C4 when queued)
//   - Switch dispatch (cases 0-18 compile-time present per Q2)
//   - Cursor advance INSIDE the else branch (no advance on I2C
//     overlay frames)
//   - Post-switch packet packing into txbptr[3..7]

// -------------------------------------------------------------------
// CW wire-probe (env `LYRA_CW_DEBUG`) — READ-ONLY diagnostic, #186.
//
// Logs, on-change, the exact CW bytes leaving on the wire so a paddle
// "acts like 2 keys, not iambic" report can be settled against the
// ground truth of what the HL2+ gateware keyer actually receives:
//   - addr 0x16 (case 12): C3[7:6]=CWMode (0=straight/off, 1=iambic-A,
//     2=iambic-B), C3[5:0]=speed, C2[6]=rev_paddle, C4=weight|spacing.
//   - addr 0x1e (case 13): C1=cw_enable (the firmware-keyer arm).
// Also prints the MOX bit (C0[0]) — the HL2 spec requires MOX=0 while
// sending CW.  If CWMode reads IAMBIC here but the paddle still acts
// like 2 keys → gateware register-map skew; if it reads STRAIGHT →
// an intent/population problem upstream.  Zero cost when the env var
// is unset (one-time static check).
static void cw_wire_probe(unsigned char C0, unsigned char C1,
                          unsigned char C2, unsigned char C3,
                          unsigned char C4) {
    static const bool on = qEnvironmentVariableIsSet("LYRA_CW_DEBUG");
    if (!on) return;
    const unsigned char addr = C0 & 0xFE;   // strip the MOX bit
    const int mox = C0 & 0x01;
    if (addr == 0x16) {                     // case 12 — keyer config
        static int last = -1;
        const int snap = (C2 << 16) | (C3 << 8) | C4 | (mox << 24);
        if (snap == last) return;
        last = snap;
        const int cwmode = (C3 >> 6) & 0x03;
        const char* name = cwmode == 0 ? "STRAIGHT/off"
                         : cwmode == 1 ? "IAMBIC-A"
                         : cwmode == 2 ? "IAMBIC-B" : "?";
        qInfo("[cwwire] 0x16 keyer: CWMode=%d(%s) speed=%d rev=%d "
              "weight=%d strict=%d  MOX=%d  [C2=%02x C3=%02x C4=%02x]",
              cwmode, name, C3 & 0x3F, (C2 >> 6) & 1, C4 & 0x7F,
              (C4 >> 7) & 1, mox, C2, C3, C4);
    } else if (addr == 0x1e) {              // case 13 — cw_enable
        static int last = -1;
        const int snap = ((C1 & 1) << 1) | mox;
        if (snap == last) return;
        last = snap;
        qInfo("[cwwire] 0x1e cw_enable=%d  MOX=%d  [C1=%02x]",
              C1 & 1, mox, C1);
    }
}

void write_main_loop_hl2(const char* out_bufp) {
    // No defensive `assert(prn != nullptr); assert(prbpfilter !=
    // nullptr);` — reference `WriteMainLoop_HL2` at
    // `networkproto1.c:869` has no such asserts; caller owns the
    // precondition.  Earlier Lyra-native asserts caught by
    // 2026-06-06 TX-Agent-2 S.1 audit and removed per "do as
    // reference, period."

    // Lazy-init the FPGA write buffer (mirrors reference
    // `FPGAWriteBufp = calloc(1024,…)` at networkproto1.c:428).
    // One sub-µs `size() != N` branch per call in steady state.
    if (g_fpga_write_bufp.size() != kFpgaPayloadBytes) {
        g_fpga_write_bufp.assign(kFpgaPayloadBytes, 0);
    }

    // Reference `WriteMainLoop_HL2:873` declares the C0..C4 locals
    // once at function scope and they retain values across USB-
    // frame iterations.  Per-case bodies write all 5 bytes; the
    // outer init covers the (defensive) all-zero start state for
    // the very first iteration only.
    unsigned char C0 = 0, C1 = 0, C2 = 0, C3 = 0, C4 = 0;

    // Outer loop: 2 USB frames per UDP datagram (networkproto1.c:878)
    for (int txframe = 0; txframe < 2; ++txframe) {
        // Reference `WriteMainLoop_HL2:880`:
        //     txbptr = FPGAWriteBufp + 512 * txframe;
        char* txbptr = reinterpret_cast<char*>(
            g_fpga_write_bufp.data()) + 512 * txframe;

        // Sync bytes (networkproto1.c:881-883)
        txbptr[0] = 0x7f;
        txbptr[1] = 0x7f;
        txbptr[2] = 0x7f;

        // MOX-edge jump (networkproto1.c:886-891).  On HL2 nddc=4
        // the `if (nddc == 2)` is FALSE so g_out_control_idx stays
        // put — but g_previous_tx_bit still updates.  Preserved
        // verbatim including the no-op-on-HL2 behavior so ANAN
        // tester hardware (nddc=2 Hermes II) gets the same logic
        // when its branch lands.
        if (XmitBit != g_previous_tx_bit) {
            if (nddc == 2)
                g_out_control_idx = 2;
            g_previous_tx_bit = XmitBit;
        }

        // C0 base init — MOX bit goes to C0 bit 0
        // (networkproto1.c:896).  Per-case `C0 |= (addr << 1)`
        // OR's the address bits ABOVE bit 0.
        //
        // NO per-iteration `C1=C2=C3=C4=0` re-zero — reference
        // (`:896`) only re-inits C0; C1..C4 inherit their value
        // from the previous switch case.  Every populated case
        // 0..18 writes all 4 bytes explicitly so steady-state
        // wire output is identical, but the contract is
        // reference-faithful.  Earlier Lyra-native re-zero
        // caught by 2026-06-06 TX-Agent-2 S.8 audit.
        C0 = static_cast<unsigned char>(XmitBit);

        // I2C-transaction overlay (HL2-only).  Lines 898-943.
        // First leg: decrement delay if non-zero.
        if (0 != prn->i2c.delay) {
            prn->i2c.delay--;
        }

        // Second leg: pre-decrement delay AND check the queue.
        // Preserved verbatim — the double decrement is the
        // reference's countdown pattern.  When the queue has data
        // AND delay is at-or-below-zero, fire an I2C transaction
        // and reset delay to 5.
        if ((0 >= --prn->i2c.delay) &&
            (prn->i2c.in_index != prn->i2c.out_index)) {
            prn->i2c.delay = 5;

            // I2C queue ring-buffer next-index walk (HL2 has
            // MAX_I2C_QUEUE = 32 slots; kMaxI2cQueue mirrors).
            unsigned char next = prn->i2c.out_index + 1 >= kMaxI2cQueue
                                     ? 0
                                     : prn->i2c.out_index + 1;

            // C0 — I2C addr 0x3c (bus 0) or 0x3d (bus 1) shifted
            // into bits[7:1], plus ctrl_request → bit 7 (which is
            // the "stop" bit on the wire).
            if (0 == prn->i2c.i2c_queue[next].bus) {
                C0 |= static_cast<unsigned char>((0x3c << 1)
                          | (prn->i2c.ctrl_request << 7));
            } else {
                C0 |= static_cast<unsigned char>((0x3d << 1)
                          | (prn->i2c.ctrl_request << 7));
            }

            // C2 — I2C target address.  If MSB set (7-bit notation
            // shifted), normalize by right-shift; then OR 0x80
            // (Stop request).
            unsigned char address = prn->i2c.i2c_queue[next].address;
            if (0x7f < address) {
                address = address >> 1;
            }
            C2 = static_cast<unsigned char>(0x80 | address);

            // C1 — read=0x07 / write=0x06 sub-command
            if (prn->i2c.ctrl_read) {
                C1 = 0x07;
            } else {
                C1 = 0x06;
            }

            // C3 / C4 — payload control byte + write data
            C3 = prn->i2c.i2c_queue[next].control;
            C4 = prn->i2c.i2c_queue[next].write_data;

            // Advance queue out-index
            prn->i2c.out_index = next;

            // NB: I2C-overlay frames do NOT advance g_out_control_idx.
            // The else-branch below does that on non-I2C frames.
        } else {
            // Normal switch dispatch (networkproto1.c:946-1178).
            // All 19 cases compile-time present per Q2 eager.
            // §4a-scope: cases 0 / 2 / 3 implemented.  Cases 1
            // (TX VFO) and 4-18 are `assert(false)` placeholders
            // until §4b / §4c populate.
            switch (g_out_control_idx) {
                case 0:
                    compose_case_0(C0, C1, C2, C3, C4);
                    break;

                case 1:  // §4b-1.1 — TX VFO frame 0x02
                    compose_case_1(C0, C1, C2, C3, C4);
                    break;

                case 2:
                    compose_case_2(C0, C1, C2, C3, C4);
                    break;

                case 3:
                    compose_case_3(C0, C1, C2, C3, C4);
                    break;

                case 4:  // §4b-1.2 — ADC assignments + TX ATT narrow
                    compose_case_4(C0, C1, C2, C3, C4);
                    break;

                case 5:  // §4c.1 — RX3/DDC2 (Orion RX2 or TX-mirror)
                    compose_case_5(C0, C1, C2, C3, C4);
                    break;

                case 6:  // §4c.2 — RX4/DDC3 (TX-mirror always)
                    compose_case_6(C0, C1, C2, C3, C4);
                    break;

                case 7:  // §4c.3 — RX5/DDC4 (Orion2 PS feedback)
                    compose_case_7(C0, C1, C2, C3, C4);
                    break;

                case 8:  // §4c.4 — RX6/DDC5 (unused; rx[0] benign default)
                    compose_case_8(C0, C1, C2, C3, C4);
                    break;

                case 9:  // §4c.5 — RX7/DDC6 (unused; rx[0] benign default)
                    compose_case_9(C0, C1, C2, C3, C4);
                    break;

                case 10:  // §4b-2.1 — drive level + Apollo + mic + HPF/LPF + PA
                    compose_case_10(C0, C1, C2, C3, C4);
                    break;

                case 11:  // §4b-2.2 — preamps + mic + MOX-gated step ATT
                    compose_case_11(C0, C1, C2, C3, C4);
                    break;

                case 12:  // §4b-2.3 — adc[1]/adc[2] step ATT + CW keyer
                    compose_case_12(C0, C1, C2, C3, C4);
                    break;

                case 13:  // §4b-1.3 — CW enable + sidetone + rf_delay
                    compose_case_13(C0, C1, C2, C3, C4);
                    break;

                case 14:  // §4b-1.4 — CW hang_delay + sidetone_freq
                    compose_case_14(C0, C1, C2, C3, C4);
                    break;

                case 15:  // §4b-1.5 — EER PWM min/max
                    compose_case_15(C0, C1, C2, C3, C4);
                    break;

                case 16:  // §4b-2.4 — BPF2 (Alex1) + xvtr_enable + puresignal
                    compose_case_16(C0, C1, C2, C3, C4);
                    break;

                case 17:  // §4b-1.6 — HL2 TX-latency + PTT-hang
                    compose_case_17(C0, C1, C2, C3, C4);
                    break;

                case 18:  // §4b-1.7 — reset_on_disconnect
                    compose_case_18(C0, C1, C2, C3, C4);
                    break;
            }

            // Cursor advance — networkproto1.c:1180-1183.  Stays
            // INSIDE the else-branch so I2C-overlay frames do NOT
            // advance the round-robin.
            if (g_out_control_idx < 18)
                g_out_control_idx++;
            else
                g_out_control_idx = 0;
        }

        // Post-switch packet packing — networkproto1.c:1186-1190.
        txbptr[3] = static_cast<char>(C0);
        txbptr[4] = static_cast<char>(C1);
        txbptr[5] = static_cast<char>(C2);
        txbptr[6] = static_cast<char>(C3);
        txbptr[7] = static_cast<char>(C4);

        cw_wire_probe(C0, C1, C2, C3, C4);  // #186 diag (LYRA_CW_DEBUG)
    }

    // ---- LRIQ memcpy + wire send + paired release ----
    //
    // Reference `WriteMainLoop_HL2:1192-1200` (the body that
    // follows the 2-USB-frame composition loop):
    //
    //     memcpy(FPGAWriteBufp +   8, bufp,         8 * 63);
    //     memcpy(FPGAWriteBufp + 520, bufp + 8*63,  8 * 63);
    //     MetisWriteFrame(0x02, FPGAWriteBufp);
    //     ReleaseSemaphore(prn->hobbuffsRun[0], 1, 0);
    //     ReleaseSemaphore(prn->hobbuffsRun[1], 1, 0);
    //
    // `bufp` is the function parameter (the LRIQ source =
    // `prn->OutBufp` at the `:1262` call site).  Lyra mirrors
    // verbatim under the parameter name `out_bufp`.
    //
    // §10.2-component-split-revert (2026-06-08, operator
    // directive Task #121): these calls previously lived in
    // `Ep2SendThread::process_one_pair` per the 2026-06-04
    // signed-off §10.2 split.  The strict-fidelity rule "do as
    // reference, period, NO PATCHING" overrides the prior
    // sign-off; the calls are now inside this function body
    // matching reference's monolithic shape.

    // First USB frame LRIQ payload: bytes [8..511] of the FPGA
    // write buffer ← bytes [0..503] of out_bufp.
    std::memcpy(g_fpga_write_bufp.data() + 8,
                out_bufp,
                static_cast<std::size_t>(kLriqBytesPerFrame));

    // Second USB frame LRIQ payload: bytes [520..1023] of the
    // FPGA write buffer ← bytes [504..1007] of out_bufp.
    std::memcpy(g_fpga_write_bufp.data() + 520,
                out_bufp + kLriqBytesPerFrame,
                static_cast<std::size_t>(kLriqBytesPerFrame));

    // Wire send.  Reference `:1198`:
    //     MetisWriteFrame(0x02, FPGAWriteBufp);
    // Discards return value — reference does the same.  §1-C
    // (2026-06-06): diagnostic counters removed per "do as
    // reference, period."
    (void) metis_write_frame(kEp2Endpoint, g_fpga_write_bufp.data());

    // Paired release — reference `:1199-1200` (verbatim).  P4.a
    // (2026-06-12): the cv-based `outbound_notify_consumed_pair()`
    // translation is replaced by the verbatim semaphore pair (the
    // P2.b HANDLE quartet on prn; created by P4.b's open()).
    // Dormant-safe flip: nothing calls write_main_loop_hl2 until
    // the P4.b sendProtocol1Samples thread runs — by which time
    // the quartet exists.  The OutboundRing cv translation retires
    // wholesale at P4.b.
    ReleaseSemaphore(prn->hobbuffsRun[0], 1, 0);
    ReleaseSemaphore(prn->hobbuffsRun[1], 1, 0);
}

}  // namespace lyra::wire

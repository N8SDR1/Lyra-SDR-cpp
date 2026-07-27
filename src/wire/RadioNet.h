// Lyra — radio-network state container.
//
// Mirrors the reference `radionet` struct (network.h:56-291).
// Master state container for a host↔radio session: holds the C&C
// register state (host→radio write fields) and the EP6 telemetry
// (radio→host read fields) per the locked Q1 mixed-state decision
// (2026-06-05).  Networking infrastructure (sockets, thread/event
// handles, buffer pointers, sequence counters) is EXCLUDED per the
// §10.2 component split (sign-off 2026-06-04) — those live in
// `wire/Ep6RecvThread`, `wire/Ep2SendThread`, `wire/OutboundRing`.
//
// Family-parameterized per Q2 lock — same struct serves HL2/HL2+
// today and ANAN/Hermes/Orion in v0.4 (multi-radio refactor).
// Array sizes preserved at reference maxima per Q3 lock
// (MAX_ADC=3, MAX_RX_STREAMS=12, MAX_TX_STREAMS=3,
// MAX_AUDIO_STREAMS=2).  HL2 populates adc[0]/rx[0..3]/tx[0]; the
// rest is allocated but inert.
//
// Bit-packed control bytes mirror the reference verbatim per Q4
// lock — C-style union-of-bitfield-struct + `#pragma pack(push,1)`
// around each one.  Inner type names PascalCased per Lyra
// convention (acceptable deviation per docs/architecture/
// PARITY_CHECKPOINTS.md §1).
//
// Synchronization per Q5 Option A — six `std::mutex` members
// embedded in the struct, mirroring the reference's six
// `CRITICAL_SECTION` members (network.h:99-104).  Five are LIVE
// (udpOUT, rcvpkt, sndpkt, seqErrors, rcvpktp1).  `sendOUT` is
// declared and initialized in the reference but never locked
// anywhere in the tree — included here for byte-for-byte parity
// per Rule 1; eligible for retirement post-Phase-2 if a future
// audit re-confirms dead.
//
// Phase 2 step 13 — populated per the signed §1 parity checkpoint
// (docs/architecture/PARITY_CHECKPOINTS.md).  WIRE-INERT: built
// but not wired into HL2Stream until §10.3 step 14.
//
// Reference: ChannelMaster/network.h:33-291.

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>

// §1-C (sign-off 2026-06-06): reverting the §1.1
// networking-infrastructure exclusion means RadioNet now carries
// the reference's `HANDLE`/`WSAEVENT`/`LARGE_INTEGER` fields.
// These are Win32 types; include guards keep the header
// compilable on non-Win32 hosts (HL2 Lyra is Win32-only today).
#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
#endif

namespace lyra::wire {

// Reference array sizes (network.h:33-36, 41, 43).  `inline
// constexpr` so each translation unit gets the same address +
// the linker does not duplicate.
inline constexpr int kMaxAdc          = 3;   // MAX_ADC
inline constexpr int kMaxRxStreams    = 12;  // MAX_RX_STREAMS
inline constexpr int kMaxTxStreams    = 3;   // MAX_TX_STREAMS
inline constexpr int kMaxAudioStreams = 2;   // MAX_AUDIO_STREAMS
inline constexpr int kMaxI2cQueue     = 32;  // MAX_I2C_QUEUE
inline constexpr int kMaxInSeqLog     = 40;  // MAX_IN_SEQ_LOG

// ===== §3 — HPSDR family + protocol enums + dispatch globals =====
//
// Three enums + three runtime-mutable globals mirroring the
// reference's `HPSDRHW` / `HPSDRModel` / `RadioProtocol` enums
// (network.h:446-483) and the `XmitBit` / `HPSDRModel HPSDRModel;`
// / `RadioProtocol RadioProtocol;` runtime variables.  Per the
// signed §3 parity checkpoint (PARITY_CHECKPOINTS.md, 2026-06-05),
// the architecture is:
//   - Type-level all-families NOW (verbatim enum values — ANAN /
//     Atlas / Orion / Saturn / AnvelinaPro3 / RedPitaya all
//     declared).  Behavior-level HL2-only — other family branches
//     land as `assert(false && "not yet implemented — needs "
//     "operator bench verification")` until tester hardware is
//     available (Q2 / Option A).
//   - NO Lyra-native `DispatchState` struct grouping.  Reference
//     reads these as scattered globals at use sites; Lyra does the
//     same (operator directive 2026-06-05: do as the reference does,
//     eliminates sync hazard, preserves cross-reference grep
//     parity 100%).
//   - Globals live in RadioNet.h alongside the existing `prn`
//     pointer — matches reference placement (network.h holds the
//     RadioNet equivalent + these enums + globals all in one file).
//
// `enum class` (C++23 scoped enum) is an acceptable deviation from
// the reference's C unscoped `enum` — same memory layout (default
// underlying type `int`), the scoping prevents implicit-conversion
// foot-guns the C reference's unscoped enums permit.  All numeric
// values verbatim from the reference; the `HPSDRModel_` prefix on
// HPSDRModel values is dropped (redundant under `enum class` — the
// reference needed it to avoid namespace collision in C unscoped
// enums; PureSignal cross-reference grep on the value identifier
// itself (`HERMESLITE`, `ANAN_G2`, etc.) is preserved).

// Gateware-architecture family (network.h:446-457).  Declared but
// NOT the active dispatch axis in ChannelMaster — runtime dispatch
// uses `HPSDRModel` (see below).  Kept here for type-level parity
// per Q2 / Option A.  Note: values 7..9 are reserved in the
// reference (gap between `HermesLite = 6` and `Saturn = 10`);
// preserved verbatim.
enum class HPSDRHW {
    Atlas       = 0,
    Hermes      = 1,
    HermesII    = 2,
    Angelia     = 3,
    Orion       = 4,
    OrionMKII   = 5,
    HermesLite  = 6,
    Saturn      = 10,
    SaturnMKII  = 11,
};

// Marketed-model identifier (network.h:459-477).  The enum the
// reference actually dispatches on — `==` comparisons in 8 sites
// across networkproto1.c / netInterface.c / network.c (HL2-class
// vs ANAN-class branching).  All 16 values verbatim; the C-style
// shadow-variable `HPSDRModel HPSDRModel;` becomes a distinctly-
// named variable below (`hpsdrModel`) since C++ doesn't permit the
// shadow.
enum class HPSDRModel {
    HPSDR        = 0,
    HERMES       = 1,
    ANAN10       = 2,
    ANAN10E      = 3,
    ANAN100      = 4,
    ANAN100B     = 5,
    ANAN100D     = 6,
    ANAN200D     = 7,
    ORIONMKII    = 8,
    ANAN7000D    = 9,
    ANAN8000D    = 10,
    ANAN_G2      = 11,
    ANAN_G2_1K   = 12,
    ANVELINAPRO3 = 13,
    HERMESLITE   = 14,
    REDPITAYA    = 15,
};

// Active wire protocol (network.h:479-483).  Reference's C-style
// shadow-variable `RadioProtocol RadioProtocol;` becomes a
// distinctly-named variable below (`radioProtocol`) under the same
// rationale as `hpsdrModel`.
enum class RadioProtocol {
    USB = 0,   // Protocol USB (P1)
    ETH = 1,   // Protocol ETH (P2)
};

// Forward decl — RxState's seq-error linked-list node.
struct SeqLogSnapshot;

// Per-RX seq-error snapshot (network.h:46-54 / _seqLogSnapshot_t).
// Linked-list node holding the in-seq-delta ring at the moment of
// a seq error, plus a timestamp.
struct SeqLogSnapshot {
    SeqLogSnapshot* next{nullptr};
    SeqLogSnapshot* previous{nullptr};

    int          rx_in_seq_snapshot[kMaxInSeqLog]{};
    char         dateTimeStamp[24]{};
    unsigned int received_seqnum{0};
    unsigned int last_seqnum{0};
};

// HL2 I²C queue state (network.h:113-148 / _i2c).
struct I2cState {
#pragma pack(push, 1)
#pragma warning(push)
#pragma warning(disable: 4201)  // nonstandard: nameless struct/union
                                // — Q4 byte-for-byte parity with
                                //   the reference's _radionet sub-
                                //   struct unions (network.h).
    union {
        unsigned char i2c_control{0};
        struct {
            unsigned char ctrl_read            : 1;  // bit 0
            unsigned char ctrl_stop            : 1;  // bit 1
            unsigned char ctrl_request         : 1;  // bit 2
            unsigned char ctrl_error           : 1;  // bit 3
            unsigned char ctrl_read_available  : 1;  // bit 4
            unsigned char                      : 1;  // bit 5 unused
            unsigned char                      : 1;  // bit 6 unused
            unsigned char                      : 1;  // bit 7 unused
        };
    };
#pragma warning(pop)
#pragma pack(pop)

#pragma pack(push, 1)
    struct I2cQueueEntry {
        unsigned char bus{0};
        unsigned char address{0};
        unsigned char control{0};
        unsigned char write_data{0};
    };
    I2cQueueEntry i2c_queue[kMaxI2cQueue]{};
#pragma pack(pop)

    unsigned char in_index{0};
    unsigned char out_index{0};
    signed char   delay{0};

    unsigned char returned_address{0};
    unsigned char read_data[4]{};
};

// CW configuration (network.h:185-210 / _cw).
struct CwConfig {
    int sidetone_level{0};
    int sidetone_freq{0};
    int keyer_speed{0};
    int keyer_weight{0};
    int hang_delay{0};
    int rf_delay{0};
    int edge_length{0};

#pragma pack(push, 1)
#pragma warning(push)
#pragma warning(disable: 4201)  // nonstandard: nameless struct/union
                                // — Q4 byte-for-byte parity with
                                //   the reference's _radionet sub-
                                //   struct unions (network.h).
    union {
        unsigned char mode_control{0};
        struct {
            unsigned char eer            : 1;  // bit 0
            unsigned char cw_enable      : 1;  // bit 1
            unsigned char rev_paddle     : 1;  // bit 2
            unsigned char iambic         : 1;  // bit 3
            unsigned char sidetone       : 1;  // bit 4
            unsigned char mode_b         : 1;  // bit 5
            unsigned char strict_spacing : 1;  // bit 6
            unsigned char break_in       : 1;  // bit 7
        };
    };
#pragma warning(pop)
#pragma pack(pop)
};

// Mic configuration (network.h:212-232 / _mic).
struct MicConfig {
    int line_in_gain{0};

#pragma pack(push, 1)
#pragma warning(push)
#pragma warning(disable: 4201)  // nonstandard: nameless struct/union
                                // — Q4 byte-for-byte parity with
                                //   the reference's _radionet sub-
                                //   struct unions (network.h).
    union {
        unsigned char mic_control{0};
        struct {
            unsigned char line_in   : 1;  // bit 0
            unsigned char mic_boost : 1;  // bit 1
            unsigned char mic_ptt   : 1;  // bit 2
            unsigned char mic_trs   : 1;  // bit 3
            unsigned char mic_bias  : 1;  // bit 4
            unsigned char mic_xlr   : 1;  // bit 5
            unsigned char           : 1;  // bit 6 unused
            unsigned char           : 1;  // bit 7 unused
        };
    };
#pragma warning(pop)
#pragma pack(pop)

    int spp{0};  // I-samples per network packet
};

// Per-ADC state (network.h:168-183 / _adc).
struct AdcState {
    int id{0};
    int rx_step_attn{0};
    int tx_step_attn{0};
    int previous_adc_overload{0};
    int adc_overload{0};

    // Sticky overload accumulator, set by the receive path and cleared
    // by whoever consumes it.  adc_overload above is a faithful
    // per-frame overwrite — that is the wire semantic and must stay
    // that way — but real overload is INTERMITTENT: the converter
    // clips on peaks, not on every frame.  A consumer polling at
    // control-loop rate (hundreds of ms) against a field rewritten
    // thousands of times a second therefore samples a clean frame
    // almost every time and concludes the band is quiet while the
    // front end is actually clipping.  This latch answers the question
    // a gain-control loop actually needs — "did the ADC clip AT ALL
    // since I last looked" — and survives between polls.
    std::atomic<int> adc_overload_seen{0};
    int dither{0};
    int random{0};

    // Wideband dynamic per-ADC (HL2-inert; ANAN-ready).
    int      wb_seqnum{0};
    int      wb_state{0};
    // wb_buff allocated by create_rnet() per reference
    // netInterface.c:1663 `prn->adc[i].wb_buff = malloc0(
    // 1024 * sizeof(double))`.  C++23 idiom translation
    // malloc0 → std::vector; size + zero-fill identical.
    std::vector<double> wb_buff{};
    uint16_t max_magnitude{0};
    uint16_t max_magnitude_at_overload{0};
};

// Per-RX stream state (network.h:234-256 / _rx).
struct RxState {
    int id{0};
    int rx_adc{0};
    int frequency{0};
    int enable{0};
    int sync{0};
    int sampling_rate{0};
    int bit_depth{0};
    int preamp{0};

    // Per-stream telemetry counters (not the global socket-level
    // counters that §1.1 moved to wire-layer threads).
    unsigned    rx_in_seq_no{0};
    unsigned    rx_in_seq_err{0};
    unsigned    rx_out_seq_no{0};
    std::time_t time_stamp{0};
    unsigned    bits_per_sample{0};
    int         spp{0};  // IQ-samples per network packet

    // Seq-delta ring + linked-list snapshot machinery
    // (network.h:250-255).
    int rx_in_seq_delta[kMaxInSeqLog]{};
    int rx_in_seq_delta_index{0};

    SeqLogSnapshot* snapshots_head{nullptr};
    SeqLogSnapshot* snapshots_tail{nullptr};
    int             snapshot_length{0};
    SeqLogSnapshot* snapshot{nullptr};
};

// Per-TX stream state (network.h:258-282 / _tx).
struct TxState {
    int id{0};
    int frequency{0};
    int sampling_rate{0};

    // CW state bits (cwx_ptt is the HL2 enhancement,
    // network.h:264).
    int cwx{0};
    int cwx_ptt{0};
    int dash{0};
    int dot{0};

    int ptt_out{0};
    int drive_level{0};
    int exciter_power{0};

    int fwd_power{0};
    int rev_power{0};
    int phase_shift{0};

    int epwm_max{0};
    int epwm_min{0};
    int pa{0};

    // HL2 enhancements (network.h:276-277; case-17 register).
    int tx_latency{0};
    int ptt_hang{0};

    // Per-stream telemetry counters.
    unsigned mic_in_seq_no{0};
    unsigned mic_in_seq_err{0};
    unsigned mic_out_seq_no{0};

    int spp{0};  // IQ-samples per network packet
};

// Per-audio-stream config (network.h:284-287 / _audio).
struct AudioConfig {
    int spp{0};  // LR-samples per network packet
};

// Cache-aligned per network.h:38 (CACHE_ALIGN = __declspec(
// align(16))).  C++23 spelling is `alignas`.
#pragma warning(push)
#pragma warning(disable: 4324)  // structure was padded due to
                                // alignment specifier — the
                                // explicit Q4/§1.12 design.
class alignas(16) RadioNet {
public:
    RadioNet();
    ~RadioNet();

    // Note: the embedded `std::mutex` members (§1.11) already
    // make `RadioNet` non-copyable + non-movable by the language
    // rules.  Process-lifetime single instance via the global
    // pointer `prn` per §1.12 (mirrors `RADIONET prn` at
    // network.h:291).  No explicit `= delete` lines — Rule 1
    // reference parity: the C reference is a plain struct with
    // no copy-control machinery, and Lyra's non-copyability
    // emerges from the `std::mutex` translation rather than
    // from a Lyra-native restriction.

    // ===== §1.2 — Top-level state scalars (network.h:68-110) =====

    int run{0};
    int wdt{0};
    int sendHighPriority{0};

    int num_adc{0};
    int num_dac{0};

    int ptt_in{0};
    int dot_in{0};
    int dash_in{0};

    int pll_locked{0};

    int oc_output{0};
    int oc_output_extras{0};

    int supply_volts{0};

    int user_adc0{0};
    int user_adc1{0};
    int user_adc2{0};
    int user_adc3{0};

    int user_dig_in{0};
    int user_dig_out{0};

    int hardware_LEDs{0};
    int reset_on_disconnect{0};
    int swap_audio_channels{0};

    int puresignal_run{0};

    int lr_audio_swap{0};
    int CATPort{0};

    // ===== §1.3 — Wideband scalars (network.h:155-160) =====
    // HL2-inert; ANAN-ready per Q2.

    int wb_base_dispid{0};
    int wb_samples_per_packet{0};
    int wb_sample_size{0};
    int wb_update_rate{0};
    int wb_packets_per_frame{0};

    // Reference is `volatile long` (network.h:160).  Preserved
    // verbatim per Rule 24 + the operator directive 2026-06-06
    // ("do as reference, period, NO PATCHING").  The prior
    // `std::atomic<long>` (§1.3 row sign-off 2026-06-04) was
    // reverted in §1-C alongside the precedent of the §3
    // `XmitBit` correction from `std::atomic<long>` back to
    // plain `int` — same error class.
    volatile long wb_enable{0};

    // ===== §1.4–§1.10 — Sub-structs =====

    I2cState    i2c{};
    CwConfig    cw{};
    MicConfig   mic{};

    AdcState    adc[kMaxAdc]{};
    RxState     rx[kMaxRxStreams]{};
    TxState     tx[kMaxTxStreams]{};
    AudioConfig audio[kMaxAudioStreams]{};

    // ===== §1.11 — Synchronization primitives (Q5 Option A) =====
    //
    // Six std::mutex members mirroring the reference's six
    // CRITICAL_SECTIONs.  Threads that need them reach into the
    // live global instance and lock the relevant member at the
    // same call sites the reference does.  Default-init via RAII;
    // destruction automatic.
    //
    // Live-usage status was source-verified 2026-06-05; recorded
    // in PARITY_CHECKPOINTS.md §1.11.

    std::mutex udpOUT;     // UDP sendto() serializer
                           //   (network.c:1248,1283 — wraps WSASend)

    std::mutex sendOUT;    // Declared in reference, never locked
                           //   anywhere (dead code in the reference
                           //   tree; included for byte-for-byte
                           //   parity per Rule 1)

    std::mutex rcvpkt;     // P2 RX packet processor serializer
                           //   (network.c:478,490,626)

    std::mutex sndpkt;     // P2 send-side serializer
                           //   (network.c:1387,1392)

    std::mutex seqErrors;  // Seq-error log mutator serializer
                           //   (6 sites across network.c +
                           //   netInterface.c)

    std::mutex rcvpktp1;   // P1 RX packet serializer
                           //   (networkproto1.c:153,166,196,212 —
                           //   ForceCandCFrame priming + P1 read
                           //   path)

    // Lock-order across these mutexes is NOT documented in the
    // reference.  Lyra-native lock-order discipline lands when
    // Phase-2 fills the threads that take more than one of them
    // (FrameComposer / Ep6RecvThread); see PARITY_CHECKPOINTS
    // §1.11 deferral note.

    // ===== §1-C — Networking infrastructure (network.h:58-106) =====
    //
    // §1-C Stage 4A (sign-off 2026-06-06): the §1.1
    // networking-infrastructure exclusion (signed 🔴 2026-06-04)
    // is REVERTED under operator directive "Fix everything to
    // reference — §1.1 included."  The fields the reference puts
    // INSIDE `_radionet` (lines 58-106) are added here as
    // RadioNet members so the wire-layer components can read
    // them via `prn->...` exactly as the reference's free
    // functions read from `prn->...`.
    //
    // Stage 4A (this commit) ADDS the fields — wire-inert.
    // Wire-layer components (Ep6RecvThread, Ep2SendThread,
    // OutboundRing) still own their copies for now.  Stages
    // 4B/4C/4D migrate consumers to read from these fields and
    // drop the duplicate component-class members.
    //
    // Reference field names preserved verbatim per Rule 1
    // grep-parity discipline.  Reference HANDLE / WSAEVENT /
    // LARGE_INTEGER fields preserved verbatim on Win32; on
    // non-Win32 hosts they're elided via `#if defined(_WIN32)`
    // guards (HL2 Lyra is Win32-only today; cross-platform
    // C++23 idiom translations live in the wire-layer-class
    // bodies that may continue to own them post-Stage-4D).

    // --- Networking config ports (network.h:58-59) ---
    // §1-C Stage 4F: defaults are BSS-zero per reference
    // (reference `network.h:58-59` declares as plain `int`
    // with no initializer; HL2 discovery writes 1024/1025 at
    // session start).  The prior `{1025}`/`{1024}` Lyra-native
    // convenience defaults were a doctrinal divergence — now
    // reverted.
    int p2_custom_port_base{0};
    int base_outbound_port{0};

    // --- Buffer pointers (network.h:60-66) ---
    //
    // Reference uses raw `double*` / `char*` / `unsigned char*`
    // allocated via `calloc`/`malloc` and stored in the struct.
    // Lyra mirrors as `std::vector<>` for RAII memory management
    // — same conceptual storage, no manual `free` (C++23 idiom
    // translation, no behavior change).
    std::vector<std::vector<double>> RxBuff;       // per-DDC IQ staging (sized per nddc at session start)
    std::vector<double>              RxReadBufp;   // interleave staging (twist output)

    // P4.a (2026-06-12): TxReadBufp converted from the Step-14
    // std::vector idiom translation back to the VERBATIM reference
    // field declaration (network.h:62 `double* TxReadBufp;`) —
    // P4.b's verbatim mic feed `Inbound(inid(1,0), n,
    // prn->TxReadBufp)` takes the raw pointer.  Allocation is the
    // verbatim calloc at create_rnet (netInterface.c:1604);
    // lifetime = process (create_rnet is call-once), matching the
    // reference + the P2.b OutBufp/outLRbufp/outIQbufp precedent.
    double* TxReadBufp{nullptr};                   // mic-sample staging (host→radio scratch)
    std::vector<std::uint8_t>        ReadBufp;     // P2 inbound buffer (HL2 P1 = inert — HL2 uses `FPGAReadBufp` file-scope, not `_radionet`); declared for P2-family forward-compat parity

    // P2.b (2026-06-12): the three OUTBOUND buffers converted from
    // the Step-14 std::vector idiom translation back to the
    // VERBATIM reference field declarations (network.h:64-66) —
    // required so the P2.c verbatim `sendOutbound` port compiles
    // byte-identical (`memcpy(prn->outIQbufp + 720, ...)` etc. is
    // pointer arithmetic the vector form cannot express).
    // Allocation is the verbatim calloc set at create_rnet
    // (netInterface.c:1606-1608); lifetime = process (create_rnet
    // is call-once), matching the reference.
    char* OutBufp{nullptr};
    double* outLRbufp{nullptr};
    double* outIQbufp{nullptr};

    // --- RX seq counters (network.h:86-87) ---
    unsigned int cc_seq_no{0};
    unsigned int cc_seq_err{0};

    // --- Thread handles (network.h:88-96) ---
    //
    // Reference: `HANDLE hReadThreadMain`, etc.  Lyra: `std::thread`
    // (C↔C++23 idiom translation, same C++ pattern as the §1.11
    // `CRITICAL_SECTION → std::mutex` translation).  Default-init
    // to "not-yet-started"; wire-layer initializer assigns via
    // `prn->hReadThreadMain = std::thread([] { ... });` at
    // session-open.
    std::thread hReadThreadMain;     // EP6 RX thread (Ep6RecvThread body)

    // P4.a (2026-06-12): hWriteThreadMain converted std::thread ->
    // VERBATIM reference field type (network.h:90 `HANDLE
    // hWriteThreadMain;`) — P4.b's StartAudio-equivalent assigns it
    // the verbatim `_beginthreadex(..., sendProtocol1Samples, ...)`
    // handle (netInterface.c:66).  nullptr until then (no user
    // existed on the std::thread form — declaration-only).
    HANDLE hWriteThreadMain{nullptr};// EP2 TX writer thread (sendProtocol1Samples, P4.b)
    std::thread hKeepAliveThread;    // P2 keep-alive thread (HL2 P1 = dead code on this path; declared for parity)

    // --- Init semaphores (network.h:89, 91) ---
    //
    // Reference: `HANDLE hReadThreadInitSem`, `hWriteThreadInitSem`
    // — initial-state "not yet ready"; child thread releases after
    // init completes; parent thread waits to confirm.  Lyra:
    // `std::counting_semaphore<1>` with initial count 0.
    std::counting_semaphore<1> hReadThreadInitSem{0};
    std::counting_semaphore<1> hWriteThreadInitSem{0};

    // --- Outbound producer/consumer sync (network.h:92-95) ---
    //
    // P2.b (2026-06-12): the VERBATIM reference HANDLE quartet is
    // now declared below (network.h:92-95) for the P2.c verbatim
    // `sendOutbound` port + the P4 verbatim `sendProtocol1Samples`
    // port.  DORMANT until P4's StartAudio-equivalent creates them
    // (CreateSemaphore(NULL, 0, 1, NULL) ×4, netInterface.c:68-71)
    // — nullptr until then; the verbatim consumers only run after
    // that creation, matching the reference's thread-start order.
    HANDLE hsendLRSem{nullptr};
    HANDLE hsendIQSem{nullptr};
    HANDLE hsendEventHandles[2]{nullptr, nullptr};
    HANDLE hobbuffsRun[2]{nullptr, nullptr};

#if defined(_WIN32)
    // --- Win32 waitable timer (network.h:97-98) ---
    //
    // Reference uses for the EP2 keep-alive cadence.  HL2 P1
    // path doesn't use a Win32 waitable timer (the cv-based
    // OutboundRing handshake handles cadence); fields declared
    // for parity + future P2 work.
    HANDLE        hTimer    = nullptr;
    LARGE_INTEGER liDueTime {};

    // --- WSA event for FD_READ (network.h:105-106) ---
    //
    // Used by Ep6RecvThread's §5.10 `WSAEventSelect` +
    // `WSAWaitForMultipleEvents` mechanism (wires up in
    // Stage 4B).  Initial value `WSA_INVALID_EVENT` =
    // not-yet-created; created via `WSACreateEvent()` at
    // session-open.
    WSAEVENT         hDataEvent     = WSA_INVALID_EVENT;
    WSANETWORKEVENTS wsaProcessEvents{};
#endif
};
#pragma warning(pop)

// ===== §1.12 — Global instance pointer =====
//
// Mirrors the reference's `RADIONET prn` (network.h:291) — one
// process-lifetime instance owned by the wire-layer initializer
// (Phase 2 wire-up).  Stays `nullptr` until the HL2 session is
// constructed.

extern RadioNet* prn;

// ===== §1.12 supplement — `create_rnet()` allocator =====
//
// Direct mirror of the reference's `create_rnet()` at
// `netInterface.c:1590-1763`:
//
//     prn = (RADIONET)malloc(sizeof(radionet));
//     if (prn) {
//         prn->base_outbound_port = 1024;
//         prn->p2_custom_port_base = 1025;
//         prn->RxBuff = calloc(8, sizeof(double*));
//         for (i = 0; i < 8; i++) prn->RxBuff[i] = calloc(64, 2*sizeof(double));
//         prn->RxReadBufp = calloc(1, 2*sizeof(double)*240);
//         prn->TxReadBufp = calloc(1, 2*sizeof(double)*720);
//         prn->ReadBufp   = calloc(1, sizeof(unsigned char)*1444);
//         prn->OutBufp    = calloc(1, sizeof(char)*1440);
//         prn->outLRbufp  = calloc(1, sizeof(double)*1440);
//         prn->outIQbufp  = calloc(1, sizeof(double)*1440);
//         /* …scalar + per-element struct init… */
//     }
//
// Lyra-native translations (Rule 24-class C↔C++23 idioms only;
// no behavioral deviation):
//   - `malloc(sizeof(radionet))` → `new RadioNet()`
//   - `calloc(N, S)` array buffers → `std::vector::assign(N*…, 0)`
//   - C struct-array `prn->adc[i].x = v;` → identical syntax
//     (kept as fixed-size C arrays per the §1-C reverted struct
//     layout; `adc[MAX_ADC]` / `rx[MAX_RX_STREAMS]` / etc.).
//
// Contract: **call exactly once** at app init, matching the
// reference's single call site (`create_rnet()` is called once
// from the C# console init path).  P3 (2026-06-12): the Lyra call
// site MOVED from HL2Stream::open() — which ran it on EVERY
// stop/start re-open, re-allocating prn each cycle (a leak + a
// wire-state reset that contradicted the close() "prn stays alive
// for re-open" contract) — to the main.cpp QTimer block AFTER
// create_xmtr(), the reference's init ordering.  That ordering is
// LOAD-BEARING: create_rnet's tail registration
// `SendpOutboundTx(OutBound)` derefs pcm->xmtr[0].pilv with NO
// null guard (SetILVOutputPointer, the verbatim reference shape).
// No idempotency guard — matches reference verbatim per the
// operator-locked "do as reference, period" rule (reference
// unconditionally `prn = malloc(sizeof(radionet))`; second call
// would leak the prior allocation).
//
// Lifetime: heap-allocated; never freed (reference does the same
// — `prn` lives for the lifetime of the audio driver process).
void create_rnet();

// P3 (2026-06-12) — reference netInterface.c:1836-1858 (verbatim;
// body in RadioNet.cpp): per-protocol sample sizes + the obbuffs
// ring pair (ring 0 = rx audio, ring 1 = tx I/Q).  Called per
// session-open from HL2Stream::open() at the reference's
// StartAudio position (netInterface.c:45); destroy_obbuffs(0/1)
// at close() per StopAudio (netInterface.c:112-113).
void UpdateRadioProtocolSampleSize();

// ===== §3.4 — Dispatch-relevant runtime globals =====
//
// Reference-verbatim globals consumed at use sites by the wire-
// send thread (MOX-bit emission), the EP6-recv thread's inline
// per-`nddc` DDC routing switch (matches the reference's
// `MetisReadThreadMainLoop_HL2:544-558` literal switch — no
// separate DdcMap class per the locked 2026-06-05 "do as the
// reference does" discipline), the family / wire-protocol
// dispatch (`FrameComposer` per-HPSDRModel + per-RadioProtocol
// branches), and the per-radio-class static dispatch parameters
// consumed by the C&C round-robin (`FrameComposer` case-0 +
// case-N branches).
//
// No struct wrapper — Lyra reads these the same way the
// reference does: scattered globals at use sites (signed §3
// checkpoint 2026-06-05).
//
// `XmitBit` mirrors the reference verbatim (`network.h:413` +
// `:505` — the reference has a benign duplicate declaration; Lyra
// declares once here).  Reference type is plain `int` (Rule 24
// source-verified 2026-06-05; the prior `std::atomic<long>`
// translation was a memory-vs-source error and is corrected by
// this declaration).  Synchronization posture matches the
// reference: word-sized reads on x86_64 are the operational
// guarantee; no atomic wrapper.  If a future bench discovers a
// race, atomic wrappers can be added with explicit operator sign-
// off — NOT a preemptive safety addition per the 2026-06-05
// directive.
//
// `hpsdrModel` and `radioProtocol` are renamed from the reference's
// C-style enum-name shadow (`HPSDRModel HPSDRModel;`) — C++ does
// not permit a variable named identically to its enum type.  Role
// + read-at-use-site behavior preserved.

extern int           XmitBit;

// P4.a (2026-06-12) — reference network.h:411 (verbatim): the EP2
// writer-thread run flag.  Set 1 before the thread starts (the
// reference sets it in ReadThreadMain/MetisReadThreadMain at
// session start); 0 stops the sendProtocol1Samples loop.
extern int           io_keep_running;

// P4.a — reference networkproto1.c:1204 (verbatim; body at
// wire/NetworkProto1.cpp, DORMANT until P4.b starts the thread
// per StartAudio netInterface.c:66):
DWORD WINAPI sendProtocol1Samples(LPVOID n);
extern HPSDRModel    hpsdrModel;
extern RadioProtocol radioProtocol;

// ===== §3.5 — Supplemental dispatch globals =====
//
// Additional per-radio-class static parameters declared by the
// reference at `network.h:501-506` and consumed by the C&C round-
// robin scheduler.  Set ONCE at session open by per-family init
// code (HL2 defaults below); read at use sites by FrameComposer.
//
// `nddc` is the per-family DDC count (HL2 / HL2+ = 4; ANAN G2 = 4;
// ANAN 7000DLE = 7; Hermes II = 2 — different gateware-mux
// behaviors).  Reference reads it in the case-0 `(nddc - 1) << 3`
// computation + the MOX-edge `if (nddc == 2)` jump + the PS / Orion
// per-DDC overrides in cases 2 / 3 / 5.
//
// `SampleRateIn2Bits` is the 2-bit encoding of the radio's outbound
// sample rate (48k=0, 96k=1, 192k=2, 384k=3) — written into case-0
// C1.  Set by the per-session rate setter (operator-driven).
//
// `P1_en_diversity` is the diversity-enabled flag — when true, RX1
// and RX2 VFOs lock together (case-0 C4 bit 7).  HL2 has no
// diversity feature; default 0 stays.

extern int           nddc;
extern unsigned char SampleRateIn2Bits;
extern int           P1_en_diversity;

// `P1_adc_cntrl` (`network.h:503`) — per-radio-class ADC-to-DDC
// routing control word.  Case 4 (frame `0x1c`) writes the low 8
// bits to C1 and bits 8-9 to C2.  HL2 / HL2+ uses ADC0 for all
// DDCs (default `0`); per-family init at session start overwrites
// for ANAN models.  Added 2026-06-05 per §4b-1 source-verification.
extern int           P1_adc_cntrl;

// §4b-2 supplement (added 2026-06-05 per §4b-2 source-verification).
//
// `xvtr_enable` (`network.h:419`) — transverter-enable flag.  Case
// 16 (frame `0x24`) reads `(xvtr_enable & 1)` into C2 bit 0.  HL2
// has no transverter port; default `0`.  Operator-toggleable via
// Settings → TX → Transverter (Task #114).
//
// `ApolloFilt` / `ApolloFiltSelect` / `ApolloTuner` / `ApolloATU`
// (`network.h:435-438`) — pre-shifted Apollo PA / filter / tuner /
// ATU control bits.  Case 10 (frame `0x12`) OR's all four inline
// into C2.  Per the gateware RTL ground truth (`control.v:209-220`):
//   - `data[19] = pa_enable` → C2 bit 3 → `ApolloTuner = 0x08`
//     when PA ON, `0` when PA OFF
//   - `data[18] = tr_disable` → C2 bit 2 → `ApolloFilt = 0x04`
//     (also serves as the tr_disable bit; PA-OFF should set
//     this to keep T/R relay disabled, PA-ON clears it)
//   - ApolloATU / ApolloFiltSelect are operator-feature bits for
//     the HL2 Apollo daughterboard
//
// **All four default `0` per the §15.26-locked PA-OFF-at-startup
// safety** — PA bias OFF, RF impossible until operator opt-in via
// Settings → TX → "Enable PA" (Task #114).  Per-family init code
// for HL2-with-Apollo-mod operators overwrites these at session
// start ONLY when the operator has previously ticked Enable PA in
// Settings.
extern int           xvtr_enable;
extern int           ApolloFilt;
extern int           ApolloFiltSelect;
extern int           ApolloTuner;
extern int           ApolloATU;

// §5 supplement (added 2026-06-05 per §5 Ep6RecvThread source-
// verification).
//
// `mic_decimation_factor` (`network.h:507`) — divisor for mic-
// sample harvest cadence.  Mic samples arrive at the full EP6
// IQ rate (one per sample slot); the read loop only emits a mic
// sample every `mic_decimation_factor` slots.  Default 1 = no
// decimation (all mic samples emitted).  Per-family init / operator
// rate setter overwrites for non-48k mic rates.
//
// `mic_decimation_count` (`network.h:508`) — running counter for
// the decimator state; cycles 0 .. factor-1.  Updated per sample
// inside the read loop.
extern int           mic_decimation_factor;
extern int           mic_decimation_count;

}  // namespace lyra::wire

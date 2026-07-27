// Lyra — EP6 receive thread implementation.
//
// See Ep6RecvThread.h for the design commentary + source mirror.
// This file mirrors `ChannelMaster/networkproto1.c:422-586` (the
// `MetisReadThreadMainLoop_HL2` function) verbatim per the signed
// §5 parity checkpoint, the §5-A parity-correction audit
// (2026-06-05), and the §1-C Stage 4B parity-correction sweep
// (2026-06-06).  Reference defects (dash_in/dot_in left-shift
// always-zero; HL2 single-frame adc_overload assignment vs the
// generic OR-until-cleared) are PRESERVED verbatim per Rule 24.
//
// §1-C Stage 4B: buffers (`RxBuff` / `TxReadBufp` / `ReadBufp`)
// migrated from class members to `prn->...` (per §1.1 revert);
// `ControlBytesIn[5]` / `MetisLastRecvSeq` / `SeqError` migrated
// from class members to TU-scope statics here (per reference,
// these are file-scope globals NOT in `_radionet`); the recv()
// loop replaced with `WSAEventSelect` + `WSAWaitForMultipleEvents`
// (per `:433-462` reference verbatim, using `prn->hDataEvent` +
// `prn->wsaProcessEvents` per §1.1 revert).
//
// 🔴 Round-1 audit 2026-06-06 corrections (operator-directed):
//   - `g_fpga_read_bufp` sized 1024 (the USB-frame content only),
//     NOT 1032 — matches reference `FPGAReadBufp = calloc(1024,…)`
//     at `networkproto1.c:427` verbatim.  The 8-byte HPSDR header
//     is stripped via the reference's `MetisReadDirect` pattern:
//     raw 1032-byte recv goes into `prn->ReadBufp`; `memcpy` 1024
//     from `prn->ReadBufp + 8` into `g_fpga_read_bufp`.
//   - All per-thread init (buffer alloc, seq counter reset, mic
//     decimator reset, `WSACreateEvent`, `WSAEventSelect`,
//     `ForceCandCFrame(3)` priming, MMCSS Pro Audio + priority-2)
//     runs at thread ENTRY inside `run_loop()` — matches reference
//     `MetisReadThreadMainLoop_HL2:423-430` verbatim.  `start()`
//     spawns the thread + waits on the `hReadThreadInitSem`
//     handshake; `run_loop()` releases it after init completes.
//     Matches reference `netInterface.c:60-66` thread-spawn
//     handshake pattern.
//   - `AvSetMmThreadPriority(hTask, AVRT_PRIORITY_HIGH)` (= 2)
//     added after `AvSetMmThreadCharacteristicsW("Pro Audio")` —
//     matches reference `networkproto1.c:243-244` verbatim.
//   - `WSASetEvent` on stop DROPPED.  Reference relies on the
//     `prn->wdt` timeout-based wake (`if(prn->wdt)` → 3000ms,
//     else WSA_INFINITE) for shutdown wake — `io_keep_running=0`
//     just makes the loop exit on the next iteration.  Lyra's
//     `stop_request_` flag does the same; the manual `WSASetEvent`
//     was a Lyra-native deviation now removed.

#include "wire/Ep6RecvThread.h"
#include "wire/ForceCandC.h"
#include "wire/MetisFrame.h"
#include "wire/RadioNet.h"
#include "wire/Router.h"
#include "wire/CmBuffs.h"   // P4.b — Inbound(): mic → stream-1 TX input ring
#include "wire/cmsetup.h"   // P4.b — inid()

#include <QtGlobal>   // Q_ASSERT_X (sink-registration-time contract)
#include <cstring>
#include <cmath>      // std::sqrt — #91 VOX mic-RMS accumulation

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <avrt.h>
  #pragma comment(lib, "avrt.lib")
  using socket_recv_len_t  = int;
  using socket_recv_size_t = int;
#else
  #include <sys/socket.h>
  #include <unistd.h>
  using socket_recv_len_t  = ssize_t;
  using socket_recv_size_t = size_t;
#endif

namespace lyra::wire {

// ---- constants mirroring the reference layout ----
namespace {
// One UDP datagram = 8-byte HPSDR header (4 sync 0xEF/0xFE/0x01/
// 0x06 + 4-byte BE sequence) + 2 x 512-byte USB frames.
constexpr std::size_t kHpsdrHeaderBytes = 8;
constexpr std::size_t kUsbFrameBytes    = 512;
constexpr std::size_t kEp6DatagramBytes =
    kHpsdrHeaderBytes + 2 * kUsbFrameBytes;  // 1032

// `FPGAReadBufp` size — USB-frame content only, NO 8-byte header.
// Reference: `FPGAReadBufp = (unsigned char*)calloc(1024,
// sizeof(unsigned char));` at `networkproto1.c:427`.  The
// `MetisReadDirect` pattern strips the header via memcpy:
//   `recv(listenSock, prn->ReadBufp, 1032, 0);`
//   `memcpy(FPGAReadBufp, prn->ReadBufp + 8, 1024);`
constexpr std::size_t kFpgaReadBufBytes = 2 * kUsbFrameBytes;  // 1024

// USB frame layout: 3-byte sync + 5-byte C0..C4 status + 504
// bytes of sample slots.  The reference's per-sample-slot index
// uses `k = 8 + isample * (6*nddc + 2) + iddc * 6` — that is,
// the sample area begins at byte 8 within the 512-byte USB frame
// (3 sync + 5 CC = 8).
constexpr int kFrameSampleAreaOffset = 8;
constexpr int kSampleAreaBytes       = 504;

// Reference IQ-pack divisor.  Per `networkproto1.c:533-540` each
// 24-bit BE IQ value is packed into the TOP 24 bits of an int32
// (`bptr[k+0] << 24 | bptr[k+1] << 16 | bptr[k+2] << 8`, low 8
// bits zero) and then multiplied by `1.0 / 2147483648.0`.  The
// effective normalization is `int24_value / 2^23` → ±1.0 double.
constexpr double kIqDivisor = 1.0 / 2147483648.0;

// Per-DDC stride within a sample slot: 3 bytes I + 3 bytes Q.
constexpr int kBytesPerDdc = 6;
// Mic trailer within a sample slot: 2 bytes (16-bit BE).
constexpr int kMicTrailerBytes = 2;

// Unpack one 24-bit BE IQ word from `p[0..2]` into a normalized
// double, exactly mirroring the reference's bit pattern (top-24
// pack + 1/2^31 divisor → effective /2^23).  Done via uint32
// build + signed conversion to keep the path defined under C++23
// (the reference's direct `unsigned char << 24` into a signed
// int is technically UB but works on every two's-complement
// platform; the uint32 build below is byte-identical and well-
// defined).
inline double unpack_iq_be(const uint8_t* p) {
    const uint32_t raw = (static_cast<uint32_t>(p[0]) << 24) |
                         (static_cast<uint32_t>(p[1]) << 16) |
                         (static_cast<uint32_t>(p[2]) <<  8);
    return static_cast<double>(static_cast<int32_t>(raw)) * kIqDivisor;
}

// Unpack one 16-bit BE mic word from `p[0..1]` into a normalized
// double via the SAME divisor — reference packs `bptr[k+0] << 24
// | bptr[k+1] << 16` (top 16 bits of int32) and scales by
// `1/2^31` → effective /2^15.  Result is in ±1.0.
inline double unpack_mic_be(const uint8_t* p) {
    const uint32_t raw = (static_cast<uint32_t>(p[0]) << 24) |
                         (static_cast<uint32_t>(p[1]) << 16);
    return static_cast<double>(static_cast<int32_t>(raw)) * kIqDivisor;
}

// ---- TU-scope file-scope mirrors (§1-C Stage 4B) ----
//
// Reference puts these at FILE SCOPE in `networkproto1.c`
// (NOT in `_radionet`).  Lyra mirrors as TU-scope statics in
// this translation unit — direct equivalent of reference's
// file-scope globals.  Sister-pattern to §6-B's TU-scope
// `g_metis_out_seq_num` in `wire/MetisFrame.cpp` (mirroring
// reference's TX-side `MetisOutBoundSeqNum` global at
// `networkproto1.c:30`).

// Reference: `int MetisLastRecvSeq = 0;` at
// `networkproto1.c:28`.  Last received EP6 seq number.
// Reference seq-check at `:191-194`: `if (seqnum != (1 +
// MetisLastRecvSeq)) SeqError += 1; MetisLastRecvSeq = seqnum;`
// — fires on the FIRST frame too (expects seqnum=1 since
// MetisLastRecvSeq starts at 0; HL2 gateware's first packet IS
// seqnum=1).  An earlier `g_seq_seen` first-frame-skip flag was
// a Lyra-native deviation caught by 2026-06-06 TX-Agent A.4
// audit and removed per "do as reference, period."
unsigned int g_metis_last_recv_seq = 0;

// Reference: `int SeqError = 0;` at `networkproto1.c:26`.
// Count of EP6 seq errors.  Stage 2b: promoted to atomic<int64_t>
// for safe cross-thread read by the operator-facing `ep6_seq_errors()`
// accessor.  Reference uses bare `int` with no lock; the QML reader
// on a separate thread justifies the atomic in Lyra (acceptable
// C++23 idiom translation per Rule 26).
std::atomic<std::int64_t> g_seq_error{0};

// ===== Stage 2b — Lyra-native operator UX counters =====
//
// These have NO reference equivalent (reference doesn't track
// per-session framing-error or total-datagram counts as exposed
// observables — operator UX-only).  Classified ACCEPTABLE
// LYRA-NATIVE per Rule 26.  Same TU-scope file-scope-global
// pattern as g_seq_error.  Per-process-lifetime monotonic;
// re-initialized only at thread entry in run_loop's per-thread
// init, mirroring reference's `SeqError = 0;` reset at
// `networkproto1.c:425`.
std::atomic<std::int64_t> g_total_datagrams  {0};
std::atomic<std::int64_t> g_framing_errors   {0};
std::atomic<std::int64_t> g_window_datagrams {0};

// #91 VOX — running RMS of the decoded EP6 mic samples (linear, [0,1)).
// Lyra-native observability, NOT a reference counter: the VOX gate
// (HL2Stream::onVoxPoll) reads it to decide whether to key.  Written
// ONLY by the EP6 reader thread's mic harvest (single writer),
// EWMA-smoothed per datagram; read cross-thread by the Qt main-thread
// poll → atomic.  Flows continuously in RX (the mic harvest runs every
// datagram regardless of MOX), which is exactly what VOX needs.
std::atomic<double> g_mic_rms_lin{0.0};

// Reference: `unsigned char ControlBytesIn[5];` at
// `network.h:414`.  Cached C&C-in header bytes from the most
// recent USB frame.
unsigned char g_control_bytes_in[5] = {0, 0, 0, 0, 0};

// Reference: `unsigned char* FPGAReadBufp;` at
// `network.h:498` (NOT in `_radionet`).  HL2 P1 EP6
// header-stripped USB-frame buffer (1024 bytes = 2 × 512).
// Sized at thread entry mirroring reference's
// `FPGAReadBufp = calloc(1024,…)` at `networkproto1.c:427`.
// Sister of `g_fpga_write_bufp` (Ep2SendThread Stage 4C).
//
// Stage 4B.1 (sign-off 2026-06-06): corrects a Stage 4B
// mismapping that put this buffer in `prn->ReadBufp`.  Per
// reference: `prn->ReadBufp` is the inbound 1032-byte raw
// recv buffer; HL2 P1 separately keeps the 1024-byte
// header-stripped `FPGAReadBufp` file-scope global.  Lyra
// now mirrors verbatim.
std::vector<std::uint8_t> g_fpga_read_bufp;

}  // namespace

// ---- ctor/dtor ----

Ep6RecvThread::Ep6RecvThread() = default;

Ep6RecvThread::~Ep6RecvThread() {
    stop();
}

// ---- lifecycle ----
//
// Reference spawn-side handshake (`netInterface.c:60-66`):
//
//     prn->hReadThreadInitSem = CreateSemaphore(NULL, 0, 1, NULL);
//     prn->hReadThreadMain    = (HANDLE) _beginthreadex(
//         NULL, 0, MetisReadThreadMainLoop_HL2, NULL, 0, &tid);
//     WaitForSingleObject(prn->hReadThreadInitSem, INFINITE);
//     CloseHandle(prn->hReadThreadInitSem);
//
// Lyra mirrors via `std::counting_semaphore<1>` on `prn`:
// `start()` resets it (consumes any leftover release from a prior
// session via `try_acquire()`), spawns the thread, then
// `acquire()` blocks until `run_loop()` releases after init.

void Ep6RecvThread::start(int socket_fd) {
    if (running_.load(std::memory_order_acquire)) return;

    // §1-C Stage 4B: `prn` must be a valid RadioNet by session-
    // open contract (reference's `prn` global is always valid
    // when MetisReadThreadMainLoop_HL2 runs — Lyra mirrors).
    if (prn == nullptr) return;

    // Socket binding happens ONCE at session-open in
    // `hl2_stream.cpp`'s wire-layer initializer — reference
    // binds `listenSock` once at the StartMetis discovery
    // path (`network.c` file-scope), NOT in each thread's
    // spawn.  An earlier Lyra-native idempotent re-bind here
    // was a deviation caught by 2026-06-06 TX-Agent A.3
    // audit and removed per "do as reference, period."
    (void) socket_fd;  // socket arg retained for API parity; see comment above.

    stop_request_.store(false, std::memory_order_release);
    running_.store(true,  std::memory_order_release);

    // Drain any stale release on `hReadThreadInitSem` left over
    // from a previous session (e.g. if stop() raced).  Reference
    // creates a fresh semaphore per session via `CreateSemaphore`
    // (`netInterface.c:60`); Lyra's persistent `counting_semaphore`
    // on `prn` requires this drain to match the same semantics.
    while (prn->hReadThreadInitSem.try_acquire()) { /* drain */ }

    // Spawn the reader thread; ALL per-thread init now happens
    // inside `run_loop()` at thread entry, mirroring reference
    // `MetisReadThreadMainLoop_HL2:423-430` verbatim.
    thread_ = std::make_unique<std::thread>([this] { this->run_loop(); });

    // Wait for `run_loop()` to release the init semaphore.
    // Reference: `WaitForSingleObject(prn->hReadThreadInitSem,
    // INFINITE);` at `netInterface.c:65`.
    prn->hReadThreadInitSem.acquire();
}

void Ep6RecvThread::stop() {
    stop_request_.store(true, std::memory_order_release);

    // Reference shutdown: `io_keep_running = 0;` makes the loop
    // exit on its next iteration; the loop wakes naturally via
    // either the FD_READ event firing OR the `prn->wdt` 3000ms
    // timeout (whichever the wait was using).  No manual event-
    // signal is needed — Lyra mirrors verbatim per the
    // operator-locked "do as reference, period" directive
    // (2026-06-06).  The earlier `WSASetEvent` was a Lyra-native
    // deviation caught by Round-1 audit 2026-06-06 and removed.

    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();
    running_.store(false, std::memory_order_release);

    // No `WSACloseEvent(prn->hDataEvent)` — reference NEVER
    // closes `prn->hDataEvent` anywhere in the ChannelMaster
    // tree (zero `WSACloseEvent` matches).  The handle is
    // re-created via `WSACreateEvent()` at the top of every
    // `run_loop()` (mirrors reference `:433`); the prior
    // handle is replaced (leaks one HANDLE per re-open cycle,
    // exactly as the reference does).  An earlier Lyra-native
    // cleanup was a deviation caught by 2026-06-06 TX-Agent
    // A.1 audit and removed per "do as reference, period."
}

// ---- sink registration ----

// Stage 2b — sink-registration contract (F1, operator-locked
// 2026-06-07): sinks MUST be registered BEFORE start() spawns the
// thread, and MUST NOT be reassigned at runtime.  This mirrors the
// reference's LoadRouterAll-once-at-session-open pattern (router.c +
// cmaster.cs:561 — reference reloads its router table ONLY on
// HPSDRModel change, not at runtime).  The assert() catches a
// runtime swap attempt in debug builds; release builds rely on the
// header contract + sink-registration site discipline (single caller
// per sink, registered from main.cpp wiring before the stream opens).
//
// Releasing a sink via `set_*({})` from a destructor is permitted
// ONLY after the EP6 thread is joined (i.e. running_ == false) —
// the operator-locked main.cpp aboutToQuit ordering (Stage 2b)
// guarantees `stream->close()` runs BEFORE any sink-owning object
// is destructed, so this assert never trips on the legitimate
// teardown path either.
namespace {
inline void assert_not_running(const std::atomic<bool>& running) {
    Q_ASSERT_X(!running.load(std::memory_order_acquire),
               "Ep6RecvThread::set_*_sink",
               "sinks may not be reassigned while the EP6 thread is "
               "running — register BEFORE start(); reference posture "
               "matches LoadRouterAll-once-at-session-open");
}
}  // namespace

void Ep6RecvThread::set_router(Router* router, int router_id) {
    assert_not_running(running_);
    router_    = router;
    router_id_ = router_id;
}

void Ep6RecvThread::set_telemetry_sink(Ep6TelemetrySink sink) {
    assert_not_running(running_);
    telemetry_sink_ = std::move(sink);
}

void Ep6RecvThread::set_mic_sink(Ep6MicSink sink) {
    assert_not_running(running_);
    mic_sink_ = std::move(sink);
}

void Ep6RecvThread::set_i2c_sink(Ep6I2cSink sink) {
    assert_not_running(running_);
    i2c_sink_ = std::move(sink);
}

void Ep6RecvThread::set_tx_clip_source(Ep6TxClipSource src) {
    assert_not_running(running_);
    tx_clip_source_ = std::move(src);
}

void Ep6RecvThread::set_mic_record_tap(Ep6MicRecordTap tap) {
    assert_not_running(running_);
    mic_record_tap_ = std::move(tap);
}

// Stage 2b2-fix-v2: set_hw_ptt_sink retired — see Ep6RecvThread.h
// `Ep6HwPttSink` comment.  Reference has no wire-side sink; the
// FSM consumer polls prn->ptt_in directly on its own clock.
// Lyra-side equivalent = HL2Stream::onHwPttPoll() (Qt timer).

// ---- reader thread ----
//
// Reference: `MetisReadThreadMainLoop_HL2` at
// `networkproto1.c:421-586`.  All per-thread init runs at thread
// entry; the init semaphore release signals the spawner that
// init is done; only then does the wait loop start.

void Ep6RecvThread::run_loop() {
#if defined(_WIN32)
    // MMCSS Pro Audio class — best-effort.  Reference:
    // `AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcss_task);`
    // followed by `AvSetMmThreadPriority(hTask,
    // AVRT_PRIORITY_HIGH);` at `networkproto1.c:243-244`.
    DWORD  mmcss_task   = 0;
    HANDLE mmcss_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio",
                                                        &mmcss_task);
    if (mmcss_handle != nullptr) {
        // AVRT_PRIORITY_HIGH = 2.  Matches reference verbatim.
        AvSetMmThreadPriority(mmcss_handle, AVRT_PRIORITY_HIGH);
    }
#endif

    // ---- Spawn-side handshake release (reference :249) ----
    //
    // Reference `MetisReadThreadMain` at `networkproto1.c:240-261`
    // releases `hReadThreadInitSem` IMMEDIATELY AFTER MMCSS
    // classification (line 249), BEFORE calling
    // `MetisReadThreadMainLoop_HL2()` (line 253) — so the
    // spawn-side `WaitForSingleObject(hReadThreadInitSem,
    // INFINITE)` at `netInterface.c:63` unblocks AS SOON AS the
    // thread has elevated priority, and the caller proceeds to
    // spawn the EP2 writer (`sendProtocol1Samples`,
    // `netInterface.c:66`) IN PARALLEL with this thread's
    // priming pass + WSAEventSelect setup.
    //
    // This is the LOAD-BEARING release position for PureSignal:
    // the reference's single shared `MetisOutBoundSeqNum`
    // counter (`networkproto1.c:30`) is consumed by both the
    // priming pass (via `MetisWriteFrame`, `:216-237`) and the
    // EP2 send thread (also via `MetisWriteFrame`), giving a
    // single continuous outbound seq stream that the HL2
    // gateware (and PureSignal's calcc/iqc) expect.  Releasing
    // the sem AFTER priming would imply two separate counters,
    // which breaks the reference contract.
    //
    // Step 14 Stage 2a correction (operator-directed 2026-06-07,
    // "Make like reference always — PureSignal requires it"):
    // the prior Lyra position (release AFTER priming +
    // WSAEventSelect) was a deviation introduced before the
    // §3.9 audit and is corrected here.
    // P4.b — reference `MetisReadThreadMain` at `networkproto1.c:246`
    // sets `io_keep_running = 1;` BEFORE releasing the init semaphore.
    // The EP2 writer thread (`sendProtocol1Samples`, spawned by
    // HL2Stream::open AFTER the init-sem handshake returns) loops on
    // `while (io_keep_running != 0)` — so by the reference's own
    // ordering the flag is guaranteed 1 before the writer ever tests
    // it.  Lyra mirrors verbatim.
    io_keep_running = 1;

    if (prn != nullptr) {
        prn->hReadThreadInitSem.release();
    }

    // ---- Per-thread init (reference :423-430) ----
    //
    // Reference body of `MetisReadThreadMainLoop_HL2` at
    // `networkproto1.c:422-586`, the function called by
    // `MetisReadThreadMain` AFTER the semaphore release above:
    //
    //     mic_decimation_count = 0;                       // :424
    //     SeqError = 0;                                  // :425
    //     FPGAReadBufp = (unsigned char*)calloc(1024,...);// :427
    //     ForceCandCFrame(3);                             // :430
    //     prn->hDataEvent = WSACreateEvent();             // :433
    //     WSAEventSelect(listenSock,
    //                    prn->hDataEvent, FD_READ);       // :434
    //     while (io_keep_running != 0) { ... }            // :439+
    //
    // All sized + zeroed INSIDE the thread per the operator-
    // locked "do as reference, period" directive (2026-06-06) —
    // the prior Lyra split (some init in start() before thread
    // spawn) was a deviation caught by Round-1 audit.

    // FPGA read buffer — 1024 bytes (header-stripped USB content).
    // Reference: `FPGAReadBufp = (unsigned char*)calloc(1024,
    // sizeof(unsigned char));` at `networkproto1.c:427`.
    g_fpga_read_bufp.assign(kFpgaReadBufBytes, 0);

    // Reset TU-scope seq tracking — reference `SeqError = 0;` at
    // `networkproto1.c:425` + `MetisLastRecvSeq` init to 0 (so
    // the first gateware packet, expected seqnum=1, passes the
    // `seqnum != (1 + MetisLastRecvSeq)` check at `:191-194`).
    //
    // Stage 2b: the Lyra-native total/framing/window counters
    // ALSO reset here — same per-thread-entry pattern as the
    // reference `SeqError = 0` reset.  These are operator-facing
    // observables that the QML banner pulls; resetting at thread
    // start (not at session-stop) matches the reference's
    // monotonic-across-stop, init-at-thread-entry posture.
    g_metis_last_recv_seq = 0;
    g_seq_error.store      (0, std::memory_order_relaxed);
    g_total_datagrams.store(0, std::memory_order_relaxed);
    g_framing_errors.store (0, std::memory_order_relaxed);
    g_window_datagrams.store(0, std::memory_order_relaxed);
    std::memset(g_control_bytes_in, 0, sizeof(g_control_bytes_in));

    // Reference: `mic_decimation_count = 0;` at
    // `networkproto1.c:424`.
    mic_decimation_count = 0;

    // C&C priming — reference `ForceCandCFrame(3);` at
    // `networkproto1.c:430`.  Emits 6 priming datagrams (3 TX-
    // freq + 3 RX1-freq) with inter-pass sleeps via the shared
    // `metis_write_frame()` primitive (which advances the shared
    // `g_metis_out_seq_num`).  Ensures the HL2 gateware has the
    // operator's tuned frequencies before the EP6 read loop
    // starts consuming sample datagrams.
    //
    // Note ordering vs WSAEventSelect: reference does priming
    // (:430) BEFORE WSAEventSelect (:433-434).  Priming sends
    // EP2 datagrams (host→radio); WSAEventSelect arms the FD_READ
    // notify for EP6 (radio→host).  No interaction — but match
    // reference order verbatim.
    force_candc_frame(3);

#if defined(_WIN32)
    // WSAEventSelect setup — reference `:433-434`:
    //     prn->hDataEvent = WSACreateEvent();
    //     WSAEventSelect(listenSock, prn->hDataEvent, FD_READ);
    prn->hDataEvent = WSACreateEvent();
    if (prn->hDataEvent != WSA_INVALID_EVENT) {
        WSAEventSelect(static_cast<SOCKET>(metis_socket_fd()),
                       prn->hDataEvent,
                       FD_READ);
    }
#endif

#if defined(_WIN32)
    // ---- Main wait loop (reference :439-463) ----
    //
    //   while (io_keep_running != 0) {
    //     DWORD retVal = WSAWaitForMultipleEvents(1, &prn->hDataEvent,
    //                       FALSE, prn->wdt ? 3000 : WSA_INFINITE,
    //                       FALSE);
    //     if (retVal == WSA_WAIT_FAILED || retVal == WSA_WAIT_TIMEOUT) {
    //       HaveSync = 0;  // send console LOS
    //       continue;
    //     } else {
    //       WSAEnumNetworkEvents(listenSock, prn->hDataEvent,
    //                            &prn->wsaProcessEvents);
    //       if (prn->wsaProcessEvents.lNetworkEvents & FD_READ) {
    //         if (prn->wsaProcessEvents.iErrorCode[FD_READ_BIT] != 0)
    //           break;
    //         MetisReadDirect(FPGAReadBufp);
    //         <process the datagram>
    //       }
    //     }
    //   }
    while (!stop_request_.load(std::memory_order_acquire)) {
        if (prn == nullptr || prn->hDataEvent == WSA_INVALID_EVENT) break;

        const DWORD timeout = (prn->wdt) ? 3000UL : WSA_INFINITE;
        DWORD retVal = WSAWaitForMultipleEvents(1, &prn->hDataEvent,
                                                FALSE, timeout, FALSE);

        // Check stop after wait wakes (reference exits on the
        // next loop iteration when `io_keep_running = 0`).
        if (stop_request_.load(std::memory_order_acquire)) break;

        if (retVal == WSA_WAIT_FAILED || retVal == WSA_WAIT_TIMEOUT) {
            // Reference does HaveSync = 0 + destroy_pro(prop)
            // here; Lyra-side LOS handling deferred to the
            // operator-policy commit (Task #114).  Continue.
            continue;
        }

        WSAEnumNetworkEvents(static_cast<SOCKET>(metis_socket_fd()),
                             prn->hDataEvent,
                             &prn->wsaProcessEvents);
        if (!(prn->wsaProcessEvents.lNetworkEvents & FD_READ)) continue;
        if (prn->wsaProcessEvents.iErrorCode[FD_READ_BIT] != 0) break;

        // ---- MetisReadDirect-equivalent ----
        //
        // Reference body verbatim from `networkproto1.c:141-214`:
        //
        //   int MetisReadDirect(unsigned char* bufp) {
        //     struct indgram { unsigned char readbuf[1074]; } inpacket;
        //     ...
        //     EnterCriticalSection(&prn->rcvpktp1);
        //     rc = recvfrom(listenSock, &inpacket, sizeof(inpacket), 0, ...);
        //     ...
        //     if (rc == 1032) {
        //       if (readbuf[0..2] == EF/FE/01) {
        //         endpoint = readbuf[3];
        //         seqbytep[3..0] = readbuf[4..7];   // BE pack into seqnum
        //         if (endpoint == 6) {
        //           if (readbuf[8..10] == 7F/7F/7F) HaveSync = 1; else HaveSync = 0;
        //           memcpy(bufp, readbuf + 8, 1024);
        //           xpro(prop, seqnum, bufp);       // out-of-order resequencer
        //           if (seqnum != (1 + MetisLastRecvSeq)) SeqError += 1;
        //           MetisLastRecvSeq = seqnum;
        //           LeaveCriticalSection(&prn->rcvpktp1); return 1024;
        //         }
        //       }
        //     }
        //     LeaveCriticalSection(&prn->rcvpktp1); return 0;
        //   }
        //
        // Lyra mirrors verbatim: LOCAL-stack 1074-byte buffer
        // (NOT `prn->ReadBufp` — that's the P2 inbound buffer,
        // unused by HL2 P1 in the reference), `prn->rcvpktp1`
        // lock held across the entire body, memcpy 1024 from
        // `readbuf + 8` into `g_fpga_read_bufp` (the file-scope
        // `FPGAReadBufp` equivalent), then dispatch.
        //
        // FIXME (Task #114): `xpro(prop, seqnum, bufp)` out-of-
        // order resequencer NOT YET PORTED — Lyra processes
        // datagrams in arrival order without resequencing.
        // FIXME (Task #114): `bandwidth_monitor_in(rc)` per
        // `networkproto1.c:170` NOT YET PORTED — Lyra has no
        // bandwidth-meter subsystem.
        unsigned char readbuf[1074]{};
        socket_recv_len_t n = 0;
        {
            std::lock_guard<std::mutex> lk(prn->rcvpktp1);
            n = ::recv(
                metis_socket_fd(),
                reinterpret_cast<char*>(readbuf),
                static_cast<socket_recv_size_t>(sizeof(readbuf)),
                0);
            if (n != static_cast<socket_recv_len_t>(kEp6DatagramBytes)) {
                g_framing_errors.fetch_add(1, std::memory_order_relaxed);
                continue;  // lock released by guard's destructor
            }
            // Validate header bytes 0..2 + endpoint 3 + sync 8..10
            // per reference :174, :181-184.
            if (readbuf[0] != 0xEF || readbuf[1] != 0xFE ||
                readbuf[2] != 0x01 || readbuf[3] != 6) {
                g_framing_errors.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (readbuf[8] != 0x7F || readbuf[9] != 0x7F ||
                readbuf[10] != 0x7F) {
                g_framing_errors.fetch_add(1, std::memory_order_relaxed);
                continue;  // sync fail; reference logs + sets HaveSync=0
            }
            // memcpy `readbuf + 8` (1024 bytes) into FPGAReadBufp
            // equivalent (reference :189).
            std::memcpy(g_fpga_read_bufp.data(),
                        readbuf + kHpsdrHeaderBytes,
                        kFpgaReadBufBytes);
            // Stage 2b: per-datagram count for the operator-facing
            // Hz computation (Lyra-native UX observable).  Both
            // monotonic and per-stats-tick-drained counters tick
            // BEFORE dispatch so a slow dispatcher can't lose count
            // (analog to HEAD's `totalDg_.fetch_add` at the same
            // position in OLD rxWorkerLoop).
            g_total_datagrams.fetch_add(1, std::memory_order_relaxed);
            g_window_datagrams.fetch_add(1, std::memory_order_relaxed);
            // Per-datagram seq + dispatch lives outside the lock
            // because process_datagram dispatches to operator
            // sinks which may take their own locks (Router
            // mutex, downstream consumer queues).  Reference
            // does the seq check + MetisLastRecvSeq update
            // INSIDE the lock (`:191-194`); we mirror that:
            process_datagram(readbuf, g_fpga_read_bufp.data());
        }  // rcvpktp1 released here
    }
#else
    // Non-Win32: fallback to blocking recv loop.  Reference is
    // Win32-only; Lyra's Linux/macOS support is a future-ANAN
    // concern, deferred.
    while (!stop_request_.load(std::memory_order_acquire)) {
        if (prn == nullptr) break;
        unsigned char readbuf[1074]{};
        socket_recv_len_t n = 0;
        {
            std::lock_guard<std::mutex> lk(prn->rcvpktp1);
            n = ::recv(
                metis_socket_fd(),
                reinterpret_cast<char*>(readbuf),
                static_cast<socket_recv_size_t>(sizeof(readbuf)),
                0);
            if (n != static_cast<socket_recv_len_t>(kEp6DatagramBytes)) {
                g_framing_errors.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (readbuf[0] != 0xEF || readbuf[1] != 0xFE ||
                readbuf[2] != 0x01 || readbuf[3] != 6) {
                g_framing_errors.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (readbuf[8] != 0x7F || readbuf[9] != 0x7F ||
                readbuf[10] != 0x7F) {
                g_framing_errors.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            std::memcpy(g_fpga_read_bufp.data(),
                        readbuf + kHpsdrHeaderBytes,
                        kFpgaReadBufBytes);
            g_total_datagrams.fetch_add(1, std::memory_order_relaxed);
            g_window_datagrams.fetch_add(1, std::memory_order_relaxed);
            process_datagram(readbuf, g_fpga_read_bufp.data());
        }
    }
#endif

    // No `AvRevertMmThreadCharacteristics(mmcss_handle)` —
    // reference NEVER reverts (zero `AvRevert*` matches in the
    // entire ChannelMaster tree).  OS reclaims on thread exit.
    // An earlier Lyra-native cleanup was a deviation caught by
    // 2026-06-06 TX-Agent C.5 audit and removed per "do as
    // reference, period."
}

// ---- per-datagram processing ----
//
// `raw_header` points at the start of the 1032-byte recv buffer
// (used only for BE seq tracking at offsets 4..7).  `usb_frames`
// points at the 1024-byte header-stripped buffer, identical to
// the reference's `FPGAReadBufp` post-MetisReadDirect.

void Ep6RecvThread::process_datagram(const uint8_t* raw_header,
                                     const uint8_t* usb_frames) {
    // 4-byte BE sequence at offset 4 of the HPSDR header.
    const uint32_t seq =
        (static_cast<uint32_t>(raw_header[4]) << 24) |
        (static_cast<uint32_t>(raw_header[5]) << 16) |
        (static_cast<uint32_t>(raw_header[6]) <<  8) |
        (static_cast<uint32_t>(raw_header[7]));

    // §1-C Stage 4B: seq tracking via TU-scope statics
    // mirroring reference's file-scope `MetisLastRecvSeq` /
    // `SeqError` at `networkproto1.c:26-28` (these are NOT in
    // `_radionet`).  Reference seq-check verbatim from
    // `MetisReadDirect` at `:191-194`:
    //     if (seqnum != (1 + MetisLastRecvSeq)) SeqError += 1;
    //     MetisLastRecvSeq = seqnum;
    // First frame counts: `MetisLastRecvSeq` inits to 0, expected
    // first seqnum is 1 (HL2 gateware emits seqnum=1 first), so a
    // gateware that obeys the contract logs zero seq errors.
    if (seq != (1u + g_metis_last_recv_seq)) {
        g_seq_error.fetch_add(1, std::memory_order_relaxed);
    }
    g_metis_last_recv_seq = seq;

    // Two USB frames per datagram, header already stripped.
    // Reference loops `for (frame=0; frame<2; frame++)` and runs
    // the full unpack→switch→mic harvest sequence per frame.
    process_usb_frame(usb_frames);
    process_usb_frame(usb_frames + kUsbFrameBytes);
}

// ---- USB frame parsing (mirrors `networkproto1.c:470-580`) ----

void Ep6RecvThread::process_usb_frame(const uint8_t* frame) {
    // Sync check: all three sync bytes must be 0x7F.  Reference
    // `if ((bptr[0]==0x7f) && (bptr[1]==0x7f) && (bptr[2]==0x7f))`.
    if (frame[0] != 0x7F || frame[1] != 0x7F || frame[2] != 0x7F) {
        return;
    }

    // §1-C Stage 4B: prn must be valid for all buffer access.
    if (prn == nullptr) return;

    // Cache the 5-byte C&C-in (status header) and decode.
    // §1-C Stage 4B: `g_control_bytes_in` TU-scope mirror of
    // reference's file-scope `ControlBytesIn[5]` at
    // `network.h:414` (NOT in `_radionet`).
    std::memcpy(g_control_bytes_in, frame + 3, 5);
    decode_status_header(g_control_bytes_in);

    // Dynamic per-frame layout (matches the reference exactly):
    //   stride = 6 * nddc + 2   bytes per sample slot
    //   spr    = 504 / stride   samples per DDC per frame
    const int n = nddc;
    if (n < 1) return;
    const int stride = kBytesPerDdc * n + kMicTrailerBytes;
    int spr = (stride > 0) ? (kSampleAreaBytes / stride) : 0;
    if (spr <= 0) return;
    if (spr > kMaxSprPerFrame) spr = kMaxSprPerFrame;
    const int clamped_ddc = (n <= kMaxDdc) ? n : kMaxDdc;

    // §1-C Stage 4B: `prn->RxBuff` is the §1.1-reverted host
    // for the per-DDC IQ staging.  Defensive bounds check so
    // we don't blow past a wrongly-sized RxBuff.
    if (static_cast<int>(prn->RxBuff.size()) < clamped_ddc) return;
    // P4.a: TxReadBufp is now the verbatim reference `double*`
    // (network.h:62); capacity is the fixed create_rnet calloc of
    // 2*720 doubles (netInterface.c:1604), >= 4*spr for every
    // valid nddc (worst case nddc=1: 4*63 = 252).  The old
    // vector-size guard reduces to a null check.
    if (prn->TxReadBufp == nullptr) return;

    // ---- Per-DDC IQ unpack (DDC-major, matches reference structure)
    //
    // Reference `networkproto1.c:528-542`:
    //   for (iddc = 0; iddc < nddc; iddc++)
    //     for (isample = 0; isample < spr; isample++)
    //       k = 8 + isample*(6*nddc+2) + iddc*6
    //       prn->RxBuff[iddc][2*isample+0] = const * (bptr[k+0]<<24 | bptr[k+1]<<16 | bptr[k+2]<<8)
    //       prn->RxBuff[iddc][2*isample+1] = const * (bptr[k+3]<<24 | bptr[k+4]<<16 | bptr[k+5]<<8)
    for (int iddc = 0; iddc < clamped_ddc; ++iddc) {
        double* const dst = prn->RxBuff[iddc].data();
        for (int isample = 0; isample < spr; ++isample) {
            const int k = kFrameSampleAreaOffset
                        + isample * stride
                        + iddc * kBytesPerDdc;
            dst[2 * isample + 0] = unpack_iq_be(frame + k);
            dst[2 * isample + 1] = unpack_iq_be(frame + k + 3);
        }
    }

    // ---- Per-nddc switch (`networkproto1.c:544-558`) ----
    //
    // Direct xrouter calls for unpaired DDCs; twist() (which
    // calls xrouter internally with 2*spr) for paired DDCs.
    switch (n) {
    case 2:
        if (router_) {
            twist(spr,
                  prn->RxBuff[0].data(), prn->RxBuff[1].data(),
                  prn->TxReadBufp,
                  /*source=*/0,
                  router_, router_id_);
        }
        break;
    case 4:
        if (router_) {
            // source 0 = DDC0; source 1 = twist(DDC2, DDC3);
            // source 2 = DDC1.  Verbatim from
            // `networkproto1.c:549-552`.
            xrouter(router_, router_id_, 0, spr, prn->RxBuff[0].data());
            twist(spr,
                  prn->RxBuff[2].data(), prn->RxBuff[3].data(),
                  prn->TxReadBufp,
                  /*source=*/1,
                  router_, router_id_);
            xrouter(router_, router_id_, 2, spr, prn->RxBuff[1].data());
        }
        break;
    case 5:
        // ANAN P1 — kept structurally for parity; bench-untested
        // in this Lyra build (no ANAN P1 hardware available).
        // Reference `networkproto1.c:554-557`.
        if (router_ && clamped_ddc >= 5) {
            twist(spr,
                  prn->RxBuff[0].data(), prn->RxBuff[1].data(),
                  prn->TxReadBufp,
                  /*source=*/0,
                  router_, router_id_);
            twist(spr,
                  prn->RxBuff[3].data(), prn->RxBuff[4].data(),
                  prn->TxReadBufp,
                  /*source=*/1,
                  router_, router_id_);
            xrouter(router_, router_id_, 2, spr, prn->RxBuff[2].data());
        }
        break;
    default:
        // Other nddc values are not dispatched (reference's
        // switch has no default body).  IQ has been unpacked
        // into prn->RxBuff regardless.
        break;
    }

    // ---- Mic harvest with decimation
    //      (`networkproto1.c:560-579`) ----
    //
    // `mic_decimation_count` is post-incremented and compared
    // against `mic_decimation_factor`; on equality the counter
    // resets and the mic sample is harvested.  Default factor=0
    // (reference BSS-init) yields no harvest; HL2 default
    // operating point sets factor=1 at session open for
    // every-slot harvest.
    //
    // Mic-sample byte offset within the slot is at `k + nddc*6`
    // (mic trailer immediately follows the last DDC's IQ).  IQ
    // pair output is interleaved {I=mic, Q=0.0}, matching the
    // reference's TxReadBufp layout.
    int mic_sample_count = 0;
    double mic_sumsq = 0.0;   // #91 VOX — Σ mic² over this datagram
    for (int isamp = 0; isamp < spr; ++isamp) {
        const int k = kFrameSampleAreaOffset
                    + n * kBytesPerDdc
                    + isamp * stride;
        ++mic_decimation_count;
        if (mic_decimation_count == mic_decimation_factor) {
            mic_decimation_count = 0;
            const double m = unpack_mic_be(frame + k);
            prn->TxReadBufp[2 * mic_sample_count + 0] = m;
            prn->TxReadBufp[2 * mic_sample_count + 1] = 0.0;
            mic_sumsq += m * m;
            ++mic_sample_count;
        }
    }
    // #91 VOX — EWMA-smoothed mic RMS for the gate.  ~30 ms time
    // constant (α=0.2 per ~2.6 ms datagram) so a single click doesn't
    // slam the level yet speech onset is tracked promptly.  Single
    // writer (this thread); atomic for the Qt-main-thread poll reader.
    if (mic_sample_count > 0) {
        const double rms = std::sqrt(mic_sumsq / mic_sample_count);
        const double prev = g_mic_rms_lin.load(std::memory_order_relaxed);
        g_mic_rms_lin.store(prev + 0.2 * (rms - prev),
                            std::memory_order_relaxed);
    }
    // Reference networkproto1.c:579 (verbatim, unconditional): hand the
    // harvested mic block to the stream-1 TX input ring.  Inbound
    // releases stream 1's Sem_BuffReady → cm_main pump → xcmaster(1) →
    // fexchange0 (WDSP TXA) → xilv → OutBound(1) → obbuffs ring 1 →
    // ob_main → sendOutbound → prn->outIQbufp + ReleaseSemaphore(
    // hsendIQSem).  Flows every EP6 datagram regardless of MOX (the TXA
    // channel parked at state=0 produces zeros; !XmitBit ⇒
    // sendProtocol1Samples zeroes outIQbufp anyway) — this is the §1
    // pacing leg that releases the writer's hsendIQSem.  (The OLD
    // mic_sink_ → Hl2Ep6MicSource indirection is orphaned by this direct
    // call — retired with the legacy TX path in the piece-6 sweep.)
    //
    // #89 recorder — tap the REAL harvested mic (before any injection below
    // overwrites the scratch) so a voice-message record captures the operator,
    // not a clip.  Lock-free no-op unless the operator is recording.
    if (mic_record_tap_ && mic_sample_count > 0) {
        mic_record_tap_(mic_sample_count, prn->TxReadBufp);
    }
    // #89 voice keyer — clip injection.  If a clip is transmitting, the injector
    // OVERWRITES prn->TxReadBufp IN PLACE (same {I,Q}-pair shape) with clip
    // samples so the clip flows into the TX ring INSTEAD of the live mic — the
    // reference plays a recorded clip at the mic input, so it runs the full TXA
    // chain (EQ/Comp/Combinator/PHROT/Leveler/ALC/modulator) and PureSignal taps
    // it post-gain (design §5).  Idle (the common case) → the injector's
    // lock-free gate returns false immediately and the live mic flows unchanged
    // (byte-identical to pre-#89).  VOX RMS above was already computed from the
    // REAL mic, so injection never trips VOX.  Only fires while a clip actually
    // plays, which requires the operator opt-in + a deliberate OTA trigger.
    if (tx_clip_source_ && mic_sample_count > 0) {
        tx_clip_source_(mic_sample_count, prn->TxReadBufp);
    }
    Inbound(inid(1, 0), mic_sample_count, prn->TxReadBufp);
}

// ---- C&C-in header decode (`networkproto1.c:478-525`) ----
//
// I2C-overlay gate (C0 bit 7 set) takes precedence and replaces
// the telemetry-class switch entirely; otherwise the PTT/dash/
// dot shadow bits land on RadioNet and the 5-case telemetry
// switch populates per-class RADIONET fields verbatim.

void Ep6RecvThread::decode_status_header(const uint8_t cc[5]) {
    // I2C readback overlay — `networkproto1.c:478-493`.
    if (cc[0] & 0x80) {
        if (i2c_sink_) {
            i2c_sink_(cc + 1, 4);
        }
        return;
    }

    if (auto* p = prn) {
        // ptt_in / dash_in / dot_in shadows — reference at
        // `networkproto1.c:496-498`.
        //
        // Note Rule 24 preservation of reference defects:
        // `dash_in = (cc[0] << 1) & 0x01` and
        // `dot_in  = (cc[0] << 2) & 0x01` always evaluate to 0
        // (left-shifting moves bits AWAY from the LSB; the LSB
        // mask then captures the new LSB which is 0).  This is
        // a known reference defect — preserved verbatim so a
        // downstream consumer that depends on "dash/dot always
        // 0" (e.g. as a placeholder for the per-HL2 hardware
        // CW input path that is wired elsewhere) sees the same
        // wire behaviour as the reference.
        p->ptt_in  = static_cast<int>(cc[0] & 0x01);
        p->dash_in = static_cast<int>((cc[0] << 1) & 0x01);
        p->dot_in  = static_cast<int>((cc[0] << 2) & 0x01);

        // Stage 2b2-fix-v2: hw_ptt_sink_ push-callback RETIRED.
        // The wire-thread push was a Lyra-native idiom (callback
        // → Qt QueuedConnection slot hop per datagram); reference
        // C-side consumer reads `prn->ptt_in` directly on its own
        // clock (no push, no per-datagram event-queue spam).
        // Lyra-side equivalent: `HL2Stream::onHwPttPoll()` (Qt
        // timer @ 20 Hz on the main thread) polls
        // `lyra::wire::prn->ptt_in` and edge-detects there.
        // The `p->ptt_in = cc[0] & 0x01` write above is the SAME
        // unconditional level write the reference does — the
        // consumer just reads it differently.

        // ---- 5-class telemetry switch on (cc[0] & 0xf8) ----
        //      Verbatim from `networkproto1.c:499-524`.
        switch (cc[0] & 0xf8) {
        case 0x00:  // C0 0000 0xxx
            // adc_overload + user_dig_in.
            p->adc[0].adc_overload = static_cast<int>(cc[1] & 0x01);
            if ((cc[1] & 0x01) != 0) {
                p->adc[0].adc_overload_seen.store(1, std::memory_order_relaxed);
            }
            p->user_dig_in = static_cast<int>((cc[1] >> 1) & 0x0f);
            break;
        case 0x08:  // C0 0000 1xxx
            // AIN5 exciter (drive) power + AIN1 PA coupler fwd.
            p->tx[0].exciter_power =
                static_cast<int>(((static_cast<int>(cc[1]) << 8) & 0xff00) |
                                 ( static_cast<int>(cc[2])       & 0x00ff));
            p->tx[0].fwd_power =
                static_cast<int>(((static_cast<int>(cc[3]) << 8) & 0xff00) |
                                 ( static_cast<int>(cc[4])       & 0x00ff));
            // FIXME (Task #114 TX-policy plumbing): reference
            // also calls `PeakFwdPower((float)prn->tx[0].fwd_power)`
            // here to maintain a running peak-meter state for
            // consumer-facing readouts.  Lyra-native peak
            // helper lands with the TX-policy plumbing commit;
            // raw fwd_power above is already populated for
            // direct consumers.
            break;
        case 0x10:  // C0 0001 0xxx
            // AIN2 PA reverse power + AIN3 MKII PA volts.
            p->tx[0].rev_power =
                static_cast<int>(((static_cast<int>(cc[1]) << 8) & 0xff00) |
                                 ( static_cast<int>(cc[2])       & 0x00ff));
            // FIXME (Task #114): reference calls
            // `PeakRevPower((float)prn->tx[0].rev_power)` here.
            p->user_adc0 =
                static_cast<int>(((static_cast<int>(cc[3]) << 8) & 0xff00) |
                                 ( static_cast<int>(cc[4])       & 0x00ff));
            break;
        case 0x18:  // C0 0001 1xxx
            // AIN4 MKII PA amps + AIN6 Hermes (supply) volts.
            p->user_adc1 =
                static_cast<int>(((static_cast<int>(cc[1]) << 8) & 0xff00) |
                                 ( static_cast<int>(cc[2])       & 0x00ff));
            p->supply_volts =
                static_cast<int>(((static_cast<int>(cc[3]) << 8) & 0xff00) |
                                 ( static_cast<int>(cc[4])       & 0x00ff));
            break;
        case 0x20:  // C0 0010 0xxx
            // Per-ADC overload bits.  Note the reference's
            // shift-then-write idiom for adc[1]/adc[2] is
            // preserved verbatim (the resulting values are 0 or
            // 2 for adc[1], 0 or 4 for adc[2] — non-1 truthy
            // values that consumers test as bool).
            p->adc[0].adc_overload = static_cast<int>(cc[1] & 0x01);
            p->adc[1].adc_overload = static_cast<int>((cc[2] & 0x01) << 1);
            p->adc[2].adc_overload = static_cast<int>((cc[3] & 0x01) << 2);
            // Latch each ADC's clip independently of the shift idiom
            // above (which yields 0/2 and 0/4, not 0/1).
            for (int a = 0; a < 3; ++a) {
                if ((cc[1 + a] & 0x01) != 0) {
                    p->adc[a].adc_overload_seen.store(
                        1, std::memory_order_relaxed);
                }
            }
            break;
        default:
            // Reference switch has no default body — other
            // class IDs are reserved/unused.
            break;
        }
    }

    // Lyra-native telemetry-sink forwarding (additive — not
    // present in the reference).  Operator-side consumers that
    // want raw payload bytes (independent of the RADIONET
    // shadow writes above) can register a sink to receive them.
    // The shadow writes above are the reference-faithful path;
    // this sink is a convenience layer signed off as an
    // acceptable Lyra-native addition (no impact on wire or
    // RADIONET state).
    if (telemetry_sink_) {
        Ep6Telemetry tm{};
        tm.class_id   = static_cast<uint8_t>(cc[0] & 0xf8);
        tm.control[0] = cc[1];
        tm.control[1] = cc[2];
        tm.control[2] = cc[3];
        tm.control[3] = cc[4];
        telemetry_sink_(tm);
    }
}

// ============== Stage 2b — operator-facing counter accessors ==============
//
// Free-function accessors at namespace scope; mirror MetisFrame's
// `metis_out_seq_num()` / `metis_socket_fd()` pattern.  Backing
// state lives as TU-scope `std::atomic` statics in the anonymous
// namespace above (`g_seq_error`, `g_total_datagrams`,
// `g_framing_errors`, `g_window_datagrams`).
//
// `ep6_seq_errors()` mirrors reference's `SeqError` (`networkproto1.c:26`)
// — per-process-lifetime monotonic, re-initialized only at thread entry
// in `run_loop` (matches reference's `SeqError = 0;` reset at `:425`).
// The other three are Lyra-native operator UX observables with no
// reference equivalent (acceptable per Rule 26 idiom space).

std::int64_t ep6_seq_errors() {
    return g_seq_error.load(std::memory_order_relaxed);
}

// #91 VOX — latest EWMA-smoothed mic RMS (linear, [0,1)); 0 when idle.
double ep6_mic_rms_lin() {
    return g_mic_rms_lin.load(std::memory_order_relaxed);
}

std::int64_t ep6_total_datagrams() {
    return g_total_datagrams.load(std::memory_order_relaxed);
}

std::int64_t ep6_framing_errors() {
    return g_framing_errors.load(std::memory_order_relaxed);
}

std::int64_t ep6_drain_window_datagrams() {
    // Atomic exchange-to-0 so the operator stats tick gets the
    // count since last drain (used to compute Hz over the tick
    // interval).  Matches HEAD's `windowDg_.exchange(0)` posture.
    return g_window_datagrams.exchange(0, std::memory_order_relaxed);
}

}  // namespace lyra::wire

// Lyra — HL2 I/O board (HL2IOBoard Pico, I2C bus 2 slave 0x1D) sequencer.
//
// Protocol 1 / Hermes-Lite 2 only.  Implements the deskHPSDR periodic model
// (old_protocol.c:2888-3198): one I2C frame per schedule point, every 12 EP2
// datagrams, in the SECOND USB frame of the datagram; an 11-slot cycle
// (132 datagrams = 346.5 ms without slip) that re-sends registers 0..4, 11,
// 13 and 14 every cycle from ONE snapshot latched at slot 0.  No queue, no
// dedupe, no retry: a write lost on the gateware's queue-less bus 2
// (i2c_bus2.v:177-247) is repaired by the next cycle.
//
// Two additions inside the same sequencer:
//   * slip  — an IO frame is deferred one datagram when the first USB frame
//             of its datagram, or the second USB frame of the previous
//             datagram, is round-robin case 0 (frame 0: OC / RX-ant /
//             dither) or case 10 (addr 0x09: mic boost); those frames can
//             make the gateware start its own bus-2 transaction.
//   * restart — a changed snapshot (or a fresh detection) restarts the cycle
//             at slot 0 at the next schedule point, unless the running cycle
//             is itself a restarted one (anti-starvation).
//
// Detection: PCA9536 at 0x41 read at slot 1 until present; present iff the
// EP6 reply echoes addr 0x3D with RQST and (C4 & 0x0F) == 0x01.  A cycle is
// armed only at slot 0, only when present (or forced) and the snapshot is
// valid; regs 0..4 only when the snapshot carries a TX value (never 0 Hz).
// REG 11 is written every armed cycle, value 0 (RF1) or 2 (Alt) — the
// snapshot field cannot encode 1.
//
// Threads:
//   Qt main   : publish(), init_for_open(), stop(), set_*(), pop_record()
//   EP2 writer: begin_datagram(), compose_frame(), end_datagram()
//   EP6 reader: on_i2c_reply()
// No Qt dependency; no allocation, locking or formatting on the EP2 writer.
//
// Plan: docs/architecture/HL2_IOBOARD_REMEDIATION_PLAN.md §1, §4, §11 A.

#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lyra::wire::hl2iob {

// ---- wire constants -------------------------------------------------------
inline constexpr std::uint8_t kBus2CmdAddr   = 0x3D;  // C0 addr: I2C bus 2
inline constexpr std::uint8_t kSlaveIoboard  = 0x1D;  // HL2IOBoard Pico
inline constexpr std::uint8_t kSlavePca9536  = 0x41;  // detection target
inline constexpr std::uint8_t kRegRfInputs   = 11;
inline constexpr std::uint8_t kRegFcodeRx1   = 13;
inline constexpr std::uint8_t kRegFcodeRx2   = 14;

// ---- sequencer constants --------------------------------------------------
inline constexpr int kSlots                = 11;  // slots 0..10
inline constexpr int kDatagramsPerSlot     = 12;  // 12 x 2.625 ms = 31.5 ms
inline constexpr int kProbeIntervalFast    = 5;   // cycles (~1.75 s)
inline constexpr int kProbeFastCount       = 10;  // probes before back-off
inline constexpr int kProbeIntervalSlow    = 87;  // cycles (~30 s)

// ---- snapshot word --------------------------------------------------------
//   bit 63..60 reserved (0)
//   bit 59     valid
//   bit 58     sendTx       (regs 0..4)
//   bit 57     sendRxCodes  (regs 13/14)
//   bit 56     reg11Alt     0 -> REG 11 = 0, 1 -> REG 11 = 2
//   bit 55..48 fcode14
//   bit 47..40 fcode13
//   bit 39..0  txHz
inline constexpr std::uint64_t kTxHzMask       = (std::uint64_t{1} << 40) - 1;
inline constexpr int           kFcode13Shift   = 40;
inline constexpr int           kFcode14Shift   = 48;
inline constexpr std::uint64_t kReg11AltBit    = std::uint64_t{1} << 56;
inline constexpr std::uint64_t kSendRxCodesBit = std::uint64_t{1} << 57;
inline constexpr std::uint64_t kSendTxBit      = std::uint64_t{1} << 58;
inline constexpr std::uint64_t kValidBit       = std::uint64_t{1} << 59;
inline constexpr std::uint64_t kReservedMask   = std::uint64_t{0xF} << 60;

struct Snapshot {
    bool          valid       = false;
    bool          sendTx      = false;
    bool          sendRxCodes = false;
    bool          reg11Alt    = false;
    std::uint8_t  fcode13     = 0;
    std::uint8_t  fcode14     = 0;
    std::uint64_t txHz        = 0;     // 40 bits
};

constexpr std::uint64_t pack(const Snapshot& s) noexcept
{
    return (s.txHz & kTxHzMask)
         | (std::uint64_t{s.fcode13} << kFcode13Shift)
         | (std::uint64_t{s.fcode14} << kFcode14Shift)
         | (s.reg11Alt    ? kReg11AltBit    : 0)
         | (s.sendRxCodes ? kSendRxCodesBit : 0)
         | (s.sendTx      ? kSendTxBit      : 0)
         | (s.valid       ? kValidBit       : 0);
}

constexpr Snapshot unpack(std::uint64_t w) noexcept
{
    Snapshot s;
    s.valid       = (w & kValidBit) != 0;
    s.sendTx      = (w & kSendTxBit) != 0;
    s.sendRxCodes = (w & kSendRxCodesBit) != 0;
    s.reg11Alt    = (w & kReg11AltBit) != 0;
    s.fcode13     = static_cast<std::uint8_t>((w >> kFcode13Shift) & 0xFF);
    s.fcode14     = static_cast<std::uint8_t>((w >> kFcode14Shift) & 0xFF);
    s.txHz        = w & kTxHzMask;
    return s;
}

// REG 11 value carried by a word: 0 (RF1) or 2 (Alt RX).  Never 1.
constexpr std::uint8_t reg11_value(std::uint64_t w) noexcept
{
    return (w & kReg11AltBit) ? 2 : 0;
}

// RF1/Alt port -> REG 11 Alt flag.  Port 2 = Alt RX; every other value
// (incl. 1 and >= 3) = RF1.  The 0/2 clamp of LY c416580.
constexpr bool reg11_alt_from_port(int port) noexcept { return port == 2; }

// ---- register value helpers (IOB README:288-298, frequency_code.c) --------

// 40-bit TX Hz -> regs 0..4 (BYTE4..BYTE0), register 4 last.
inline void pack_tx_freq_bytes(std::uint64_t hz, std::uint8_t out[5]) noexcept
{
    out[0] = static_cast<std::uint8_t>((hz >> 32) & 0xFFu);
    out[1] = static_cast<std::uint8_t>((hz >> 24) & 0xFFu);
    out[2] = static_cast<std::uint8_t>((hz >> 16) & 0xFFu);
    out[3] = static_cast<std::uint8_t>((hz >>  8) & 0xFFu);
    out[4] = static_cast<std::uint8_t>( hz        & 0xFFu);
}

// One-byte frequency code for REG 13/14, identical to the IOB firmware's
// hertz2fcode (n2adr_lib/frequency_code.c:10-21): 0 for 0 Hz, 1 below
// 20 kHz, 255 above 270 GHz, else (int)(0.5 + 15.47 * ln(Hz / 18748.1)).
inline std::uint8_t rx_fcode(std::uint64_t hz) noexcept
{
    if (hz == 0)
        return 0;
    if (hz < 20000ull)
        return 1;
    if (hz > 270'000'000'000ull)
        return 255;
    const int code = static_cast<int>(
        0.5 + 15.47 * std::log(static_cast<double>(hz) / 18748.1));
    if (code < 0)
        return 0;
    if (code > 255)
        return 255;
    return static_cast<std::uint8_t>(code);
}

// ---- snapshot computation (pure; called on the Qt thread) -----------------

enum class XvtrPolicy : std::uint8_t {
    HoldLastHf = 0,   // default: transverter slot -> last HF TX Hz, or nothing
    OnAirRf    = 1,   // opt-in: the RF dial, any band
};

struct Inputs {
    std::uint64_t txHz         = 0;      // effective TX dial, on-air RF Hz
    bool          txInXvtrSlot = false;  // txHz lies in a transverter slot
    std::uint64_t rx1Hz        = 0;      // RX1 dial RF
    std::uint64_t rx2Hz        = 0;      // RX2 dial RF
    bool          subEnabled   = false;  // RX2 on: REG 14 = fcode(rx2)
    int           rxPort       = 0;      // 0 RF1, 2 Alt
    XvtrPolicy    xvtrPolicy   = XvtrPolicy::HoldLastHf;
    bool          sendRxCodes  = true;   // REG 13/14 enabled
};

// Last HF TX Hz after observing `in`: txHz when it is > 0 and not in a
// transverter slot, otherwise `lastHfTxHz` unchanged.
constexpr std::uint64_t update_last_hf(const Inputs& in,
                                       std::uint64_t lastHfTxHz) noexcept
{
    return (in.txHz > 0 && !in.txInXvtrSlot) ? in.txHz : lastHfTxHz;
}

// The word one cycle sends.  Always valid.  sendTx = 0 when the policy
// yields no TX value or the value would be 0 Hz.
std::uint64_t compute_snapshot(const Inputs& in, std::uint64_t lastHfTxHz) noexcept;

// ---- Qt-thread API --------------------------------------------------------

// Store the word.  Requests a restart when the stored word changed.
void publish(std::uint64_t word) noexcept;
std::uint64_t published() noexcept;

// Session lifecycle.  init_for_open() must run before the EP6 reader and
// the EP2 writer threads start; it zeroes all sequencer state, probe
// counters and both record rings, clears presence and starts the
// sequencer.  stop() makes the sequencer and the reply parser inert.
void init_for_open(bool forcePresent) noexcept;
void stop() noexcept;
bool running() noexcept;

void set_force_present(bool on) noexcept;
bool force_present() noexcept;
bool present() noexcept;
std::uint32_t probes_sent() noexcept;
std::uint8_t  last_reply_c4() noexcept;   // C4 of the reply that set present

void set_log_verbose(bool on) noexcept;

// ---- EP2 writer thread API (write_main_loop_hl2) --------------------------

// Once per datagram, before the USB-frame loop.  f0_case = the round-robin
// case USB frame 1 will carry.  Returns true when USB frame 2 carries an
// IO-board frame.
bool begin_datagram(int f0_case) noexcept;

// For USB frame 2 when begin_datagram() returned true.  Writes C0..C4
// (MOX bit from xmit) and returns true.
bool compose_frame(int xmit, unsigned char& C0, unsigned char& C1,
                   unsigned char& C2, unsigned char& C3,
                   unsigned char& C4) noexcept;

// After the USB-frame loop.  f1_case = the round-robin case USB frame 2
// carried, or -1 when it carried an IO-board frame.
void end_datagram(int f1_case) noexcept;

// ---- EP6 reader thread API ------------------------------------------------

enum class ReplyKind : std::uint8_t { Ignored, Present, Absent };

// Classify an EP6 C0..C4 with C0 bit 7 set (pure).
constexpr ReplyKind classify_reply(const std::uint8_t c[5]) noexcept
{
    if ((c[0] & 0x80) == 0)
        return ReplyKind::Ignored;
    if (((c[0] & 0x7E) >> 1) != kBus2CmdAddr)
        return ReplyKind::Ignored;
    return ((c[4] & 0x0F) == 0x01) ? ReplyKind::Present : ReplyKind::Absent;
}

// I2C readback sink: c = EP6 C0..C4.
void on_i2c_reply(const std::uint8_t c[5]) noexcept;

// ---- record ring (logging off the EP2 writer thread) ----------------------

enum class RecKind : std::uint8_t {
    Latch   = 0,
    Write   = 1,
    Read    = 2,
    Slip    = 3,
    Restart = 4,
    Reply   = 5,
};

struct Record {
    std::uint64_t t_ns;       // steady clock on the producer, since open
    std::uint32_t datagram;   // datagram counter since open
    std::uint32_t cycle;
    std::uint8_t  slot;
    std::uint8_t  kind;       // RecKind
    std::uint8_t  reg;        // Write: register; Latch: armed (0/1)
    std::uint8_t  val;        // Write: value
    std::int8_t   f0;         // Slip: frame-1 case
    std::int8_t   pf1;        // Slip: previous frame-2 case
    std::uint8_t  pad[2];
    std::uint64_t word;       // Latch: latched word; Reply: C1..C4
};
static_assert(sizeof(Record) == 32, "Record must stay 32 bytes");

// Lock-free single-producer / single-consumer ring.  push() on the producer
// thread only; pop() on the consumer thread only.  Full -> the record is
// dropped and counted.
template <std::size_t N>
class RecordRing {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");
public:
    bool push(const Record& r) noexcept
    {
        const std::uint32_t h = head_.load(std::memory_order_relaxed);
        const std::uint32_t t = tail_.load(std::memory_order_acquire);
        if (h - t >= N) {
            drops_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        buf_[h & (N - 1)] = r;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(Record& r) noexcept
    {
        const std::uint32_t t = tail_.load(std::memory_order_relaxed);
        const std::uint32_t h = head_.load(std::memory_order_acquire);
        if (t == h)
            return false;
        r = buf_[t & (N - 1)];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    std::uint32_t take_drops() noexcept
    {
        return drops_.exchange(0, std::memory_order_relaxed);
    }
    // Only while neither side is running.
    void reset() noexcept
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        drops_.store(0, std::memory_order_relaxed);
    }
private:
    Record                     buf_[N]{};
    std::atomic<std::uint32_t> head_{0};
    std::atomic<std::uint32_t> tail_{0};
    std::atomic<std::uint32_t> drops_{0};
};

inline constexpr std::size_t kRecordRingSize = 512;  // EP2 writer producer
inline constexpr std::size_t kReplyRingSize  = 16;   // EP6 reader producer

// Qt thread: next record from the EP2 ring, then the EP6 reply ring.
bool pop_record(Record& r) noexcept;
// Qt thread: records dropped on full rings since the last call.
std::uint32_t take_dropped_records() noexcept;

}  // namespace lyra::wire::hl2iob

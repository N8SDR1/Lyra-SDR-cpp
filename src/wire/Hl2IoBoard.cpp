// Lyra — HL2 I/O board sequencer.  See Hl2IoBoard.h.
//
// Plan: docs/architecture/HL2_IOBOARD_REMEDIATION_PLAN.md §4, §11 A.
// Reference cadence: deskHPSDR old_protocol.c:2888-3198.

#include "wire/Hl2IoBoard.h"

#include <chrono>

namespace lyra::wire::hl2iob {

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "snapshot word must be lock-free");

namespace {

// ---- cross-thread state ---------------------------------------------------
std::atomic<std::uint64_t> g_snapshot{0};          // Qt -> EP2
std::atomic<bool>          g_restart_req{false};   // Qt/EP6 -> EP2
std::atomic<bool>          g_present{false};       // EP6 -> EP2/Qt
std::atomic<bool>          g_force_present{false}; // Qt -> EP2
std::atomic<bool>          g_running{false};       // Qt -> EP2/EP6
std::atomic<bool>          g_log_verbose{false};   // Qt -> EP2/EP6
std::atomic<std::uint32_t> g_probes_sent{0};       // EP2 -> Qt
std::atomic<std::uint8_t>  g_last_reply_c4{0};     // EP6 -> Qt
std::atomic<std::int64_t>  g_t0_ns{0};             // Qt (open) -> EP2/EP6

// ---- sequencer state (EP2 writer thread only) -----------------------------
int           s_slot          = 0;
int           s_due           = 0;
std::uint32_t s_cycle         = 0;
bool          s_armed         = false;
bool          s_from_restart  = false;
std::uint64_t s_latched       = 0;
int           s_pending_slot  = -1;
int           s_prev_f1_case  = -1;
std::uint32_t s_next_probe    = 0;
std::uint32_t s_datagram      = 0;

RecordRing<kRecordRingSize> g_ring;        // producer: EP2 writer
RecordRing<kReplyRingSize>  g_reply_ring;  // producer: EP6 reader

std::int64_t steady_ns() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool is_i2c_gen(int c) noexcept { return c == 0 || c == 10; }

template <std::size_t N>
void push_record(RecordRing<N>& ring, RecKind kind, std::uint8_t slot,
                 std::uint8_t reg, std::uint8_t val, int f0, int pf1,
                 std::uint64_t word) noexcept
{
    if (!g_log_verbose.load(std::memory_order_relaxed))
        return;
    Record r{};
    r.t_ns     = static_cast<std::uint64_t>(
        steady_ns() - g_t0_ns.load(std::memory_order_relaxed));
    r.datagram = s_datagram;
    r.cycle    = s_cycle;
    r.slot     = slot;
    r.kind     = static_cast<std::uint8_t>(kind);
    r.reg      = reg;
    r.val      = val;
    r.f0       = static_cast<std::int8_t>(f0);
    r.pf1      = static_cast<std::int8_t>(pf1);
    r.word     = word;
    ring.push(r);
}

// Register written by a write slot (2..9).
std::uint8_t reg_for_slot(int slot) noexcept
{
    if (slot >= 2 && slot <= 6)
        return static_cast<std::uint8_t>(slot - 2);
    if (slot == 7)
        return kRegRfInputs;
    if (slot == 8)
        return kRegFcodeRx1;
    return kRegFcodeRx2;
}

// Value written by a write slot, from the latched word.
std::uint8_t val_for_slot(std::uint64_t w, int slot) noexcept
{
    if (slot >= 2 && slot <= 6) {
        std::uint8_t b[5];
        pack_tx_freq_bytes(w & kTxHzMask, b);
        return b[slot - 2];
    }
    if (slot == 7)
        return reg11_value(w);
    if (slot == 8)
        return static_cast<std::uint8_t>((w >> kFcode13Shift) & 0xFF);
    return static_cast<std::uint8_t>((w >> kFcode14Shift) & 0xFF);
}

bool probe_due(std::uint32_t cycle) noexcept { return cycle >= s_next_probe; }

void note_probe_sent(std::uint32_t cycle) noexcept
{
    const std::uint32_t n =
        g_probes_sent.fetch_add(1, std::memory_order_relaxed) + 1;
    s_next_probe = cycle + static_cast<std::uint32_t>(
        n < static_cast<std::uint32_t>(kProbeFastCount) ? kProbeIntervalFast
                                                        : kProbeIntervalSlow);
}

}  // namespace

// ---- snapshot -------------------------------------------------------------

std::uint64_t compute_snapshot(const Inputs& in, std::uint64_t lastHfTxHz) noexcept
{
    Snapshot s;
    s.valid = true;

    // No TX dial (0 Hz) sends nothing under either policy.
    std::uint64_t tx = in.txHz;
    if (tx > 0 && in.xvtrPolicy == XvtrPolicy::HoldLastHf && in.txInXvtrSlot)
        tx = lastHfTxHz;
    tx &= kTxHzMask;
    s.sendTx = tx > 0;
    s.txHz   = s.sendTx ? tx : 0;

    s.sendRxCodes = in.sendRxCodes && in.rx1Hz > 0;
    if (s.sendRxCodes) {
        s.fcode13 = rx_fcode(in.rx1Hz);
        s.fcode14 = in.subEnabled ? rx_fcode(in.rx2Hz) : s.fcode13;
    }

    s.reg11Alt = reg11_alt_from_port(in.rxPort);
    return pack(s);
}

void publish(std::uint64_t word) noexcept
{
    if (g_snapshot.exchange(word, std::memory_order_acq_rel) != word)
        g_restart_req.store(true, std::memory_order_release);
}

std::uint64_t published() noexcept
{
    return g_snapshot.load(std::memory_order_acquire);
}

// ---- lifecycle ------------------------------------------------------------

void init_for_open(bool forcePresent) noexcept
{
    s_slot         = 0;
    s_due          = 0;
    s_cycle        = 0;
    s_armed        = false;
    s_from_restart = false;
    s_latched      = 0;
    s_pending_slot = -1;
    s_prev_f1_case = -1;
    s_next_probe   = 0;
    s_datagram     = 0;
    g_ring.reset();
    g_reply_ring.reset();
    g_probes_sent.store(0, std::memory_order_relaxed);
    g_last_reply_c4.store(0, std::memory_order_relaxed);
    g_t0_ns.store(steady_ns(), std::memory_order_relaxed);
    g_present.store(false, std::memory_order_relaxed);
    g_restart_req.store(false, std::memory_order_relaxed);
    g_force_present.store(forcePresent, std::memory_order_relaxed);
    g_running.store(true, std::memory_order_release);
}

void stop() noexcept
{
    g_running.store(false, std::memory_order_release);
}

bool running() noexcept { return g_running.load(std::memory_order_acquire); }

void set_force_present(bool on) noexcept
{
    g_force_present.store(on, std::memory_order_relaxed);
    if (on)
        g_restart_req.store(true, std::memory_order_release);
}

bool force_present() noexcept
{
    return g_force_present.load(std::memory_order_relaxed);
}

bool present() noexcept { return g_present.load(std::memory_order_acquire); }

std::uint32_t probes_sent() noexcept
{
    return g_probes_sent.load(std::memory_order_relaxed);
}

std::uint8_t last_reply_c4() noexcept
{
    return g_last_reply_c4.load(std::memory_order_relaxed);
}

void set_log_verbose(bool on) noexcept
{
    g_log_verbose.store(on, std::memory_order_relaxed);
}

// ---- EP2 writer thread ----------------------------------------------------

bool begin_datagram(int f0_case) noexcept
{
    if (!g_running.load(std::memory_order_acquire))
        return false;
    ++s_datagram;
    if (s_due != 0) {
        --s_due;
        return false;                                  // not a schedule point
    }

    // --- schedule point ---
    if (g_restart_req.load(std::memory_order_acquire)
        && s_slot != 0 && !s_from_restart) {
        g_restart_req.store(false, std::memory_order_relaxed);
        push_record(g_ring, RecKind::Restart,
                    static_cast<std::uint8_t>(s_slot), 0, 0, -1, -1, 0);
        s_slot = 0;
        s_from_restart = true;
    }

    bool emit = false;
    switch (s_slot) {
    case 0:
        g_restart_req.store(false, std::memory_order_relaxed);  // newest word
        s_latched = g_snapshot.load(std::memory_order_acquire);
        s_armed = (g_present.load(std::memory_order_acquire)
                   || g_force_present.load(std::memory_order_relaxed))
                  && (s_latched & kValidBit) != 0;
        push_record(g_ring, RecKind::Latch, 0,
                    static_cast<std::uint8_t>(s_armed ? 1 : 0), 0, -1, -1,
                    s_latched);
        break;
    case 1:
        emit = !s_armed
               && !g_force_present.load(std::memory_order_relaxed)
               && !g_present.load(std::memory_order_acquire)
               && probe_due(s_cycle);
        break;
    case 2: case 3: case 4: case 5: case 6:
        emit = s_armed && (s_latched & kSendTxBit) != 0;
        break;
    case 7:
        emit = s_armed;                                // REG 11 every cycle
        break;
    case 8: case 9:
        emit = s_armed && (s_latched & kSendRxCodesBit) != 0;
        break;
    default:
        emit = false;
        break;
    }

    if (emit && (is_i2c_gen(f0_case) || is_i2c_gen(s_prev_f1_case))) {
        push_record(g_ring, RecKind::Slip, static_cast<std::uint8_t>(s_slot),
                    0, 0, f0_case, s_prev_f1_case, 0);
        return false;                    // due stays 0, slot unchanged
    }

    s_pending_slot = emit ? s_slot : -1;
    if (emit) {
        if (s_slot == 1) {
            push_record(g_ring, RecKind::Read, 1, 0, 0, -1, -1, 0);
            note_probe_sent(s_cycle);
        } else {
            push_record(g_ring, RecKind::Write,
                        static_cast<std::uint8_t>(s_slot),
                        reg_for_slot(s_slot), val_for_slot(s_latched, s_slot),
                        -1, -1, 0);
        }
    }
    if (++s_slot == kSlots) {
        s_slot = 0;
        ++s_cycle;
        s_from_restart = false;
    }
    s_due = kDatagramsPerSlot - 1;
    return emit;
}

bool compose_frame(int xmit, unsigned char& C0, unsigned char& C1,
                   unsigned char& C2, unsigned char& C3,
                   unsigned char& C4) noexcept
{
    const int slot = s_pending_slot;
    if (slot < 0)
        return false;
    const unsigned char mox = static_cast<unsigned char>(xmit & 1);
    if (slot == 1) {
        C0 = static_cast<unsigned char>(0x80 | (kBus2CmdAddr << 1) | mox);
        C1 = 0x07;                                     // read
        C2 = static_cast<unsigned char>(0x80 | kSlavePca9536);
        C3 = 0;
        C4 = 0;
    } else {
        C0 = static_cast<unsigned char>((kBus2CmdAddr << 1) | mox);
        C1 = 0x06;                                     // write
        C2 = static_cast<unsigned char>(0x80 | kSlaveIoboard);
        C3 = reg_for_slot(slot);
        C4 = val_for_slot(s_latched, slot);
    }
    return true;
}

void end_datagram(int f1_case) noexcept
{
    s_prev_f1_case = f1_case;
    s_pending_slot = -1;
}

// ---- EP6 reader thread ----------------------------------------------------

void on_i2c_reply(const std::uint8_t c[5]) noexcept
{
    if (!g_running.load(std::memory_order_acquire))
        return;
    const ReplyKind k = classify_reply(c);
    if (k == ReplyKind::Ignored)
        return;
    if (g_log_verbose.load(std::memory_order_relaxed)) {
        Record r{};
        r.t_ns = static_cast<std::uint64_t>(
            steady_ns() - g_t0_ns.load(std::memory_order_relaxed));
        r.kind = static_cast<std::uint8_t>(RecKind::Reply);
        r.val  = (k == ReplyKind::Present) ? 1 : 0;
        r.word = (std::uint64_t{c[1]} << 24) | (std::uint64_t{c[2]} << 16)
               | (std::uint64_t{c[3]} << 8)  |  std::uint64_t{c[4]};
        g_reply_ring.push(r);
    }
    if (k == ReplyKind::Present && !g_present.load(std::memory_order_acquire)) {
        g_last_reply_c4.store(c[4], std::memory_order_relaxed);
        g_present.store(true, std::memory_order_release);
        g_restart_req.store(true, std::memory_order_release);
    }
}

// ---- record drain (Qt thread) ---------------------------------------------

bool pop_record(Record& r) noexcept
{
    return g_ring.pop(r) || g_reply_ring.pop(r);
}

std::uint32_t take_dropped_records() noexcept
{
    return g_ring.take_drops() + g_reply_ring.take_drops();
}

}  // namespace lyra::wire::hl2iob

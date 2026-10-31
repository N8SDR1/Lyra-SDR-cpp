// HL2 I/O board sequencer — frame-level harness.
//
// Plan: docs/architecture/HL2_IOBOARD_REMEDIATION_PLAN.md §5.
//
// Links the real src/wire/Hl2IoBoard.cpp and src/wire/FrameComposer.cpp.
// The datagram stream comes from one of two drivers: SimDriver calls the
// sequencer the way write_main_loop_hl2 does (begin_datagram before the two
// USB frames, compose_frame for USB frame 2, end_datagram after) with
// synthetic round-robin frames; ComposerDriver runs the real
// write_main_loop_hl2 and captures metis_write_frame
// (hl2_ioboard_test_stubs.cpp).  Either stream is consumed by a gateware model
// (queue-less bus 2, 100 us per transaction, frame-0 / mic-boost
// transactions, optional UDP loss) feeding a Pico model (Registers[],
// commit on register 4, FT-817 J4 table).
//
// cmake --build build --target test_hl2_ioboard_frames
// build\test_hl2_ioboard_frames.exe

#include "wire/FrameComposer.h"
#include "wire/Hl2IoBoard.h"
#include "wire/RadioNet.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace iob = lyra::wire::hl2iob;

// hl2_ioboard_test_stubs.cpp
extern std::uint8_t g_hl2iob_test_dg[1024];
extern long g_hl2iob_test_dg_count;
void hl2iob_test_wire_setup();

// ---------------------------------------------------------------------------
// check plumbing
// ---------------------------------------------------------------------------
static int g_fail = 0;
static int g_checks = 0;
static const char* g_test = "";

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL [%s] %s:%d  %s\n", g_test, __FILE__,  \
                         __LINE__, #cond);                                   \
            ++g_fail;                                                        \
        }                                                                    \
    } while (0)

#define CHECKF(cond, ...)                                                    \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL [%s] %s:%d  %s  -- ", g_test,         \
                         __FILE__, __LINE__, #cond);                         \
            std::fprintf(stderr, __VA_ARGS__);                               \
            std::fprintf(stderr, "\n");                                      \
            ++g_fail;                                                        \
        }                                                                    \
    } while (0)

// ---------------------------------------------------------------------------
// IOB firmware references (n2adr_lib/frequency_code.c, fcode2bcode.c,
// ft817_band_volts.c) — independent of the module under test.
// ---------------------------------------------------------------------------
static int iob_hertz2fcode(std::uint64_t hz)
{
    if (hz == 0) return 0;
    if (hz < 20000ull) return 1;
    if (hz > 270000000000ull) return 255;
    return (int)(0.5 + 15.47 * std::log((double)hz / 18748.1));
}

// Band code by fcode (plan Appendix B ranges); 0 = no band.
static int fcode_band(int fc)
{
    struct R { int lo, hi, band; };
    static const R t[] = {
        {71, 72, 160}, {81, 83, 80}, {87, 88, 60}, {92, 92, 40},
        {97, 97, 30}, {102, 103, 20}, {106, 106, 17}, {109, 109, 15},
        {111, 111, 12}, {113, 114, 10}, {122, 123, 6}, {127, 127, 4},
        {138, 139, 2}, {155, 156, 70},
    };
    for (const R& r : t)
        if (fc >= r.lo && fc <= r.hi) return r.band;
    return 0;
}

// FT-817 J4 table (ft817_band_volts.c:23-69); anything else 0 V.
static double ft817_volts(int band)
{
    switch (band) {
    case 160: return 0.33; case 80: return 0.67; case 60: return 0.83;
    case 40: return 1.00;  case 30: return 1.33; case 20: return 1.67;
    case 17: return 2.00;  case 15: return 2.33; case 12: return 2.67;
    case 10: return 3.00;  case 6: return 3.33;  case 2: return 3.67;
    case 70: return 4.00;
    default: return 0.0;
    }
}

static double j4_for_hz(std::uint64_t hz)
{
    if (hz == 0) return 0.0;                         // tx_band 0 = reset
    return ft817_volts(fcode_band(iob_hertz2fcode(hz)));
}

// ---------------------------------------------------------------------------
// round-robin C0 address per case (FrameComposer compose_case_N)
// ---------------------------------------------------------------------------
static const std::uint8_t kCaseC0[19] = {
    0x00, 0x02, 0x04, 0x06, 0x1c, 0x08, 0x0a, 0x0c, 0x0e, 0x10,
    0x12, 0x14, 0x16, 0x1e, 0x20, 0x22, 0x24, 0x2e, 0x74,
};

static bool is_io_frame(const std::uint8_t c[5])
{
    return ((c[0] >> 1) & 0x3F) == iob::kBus2CmdAddr;
}

// ---------------------------------------------------------------------------
// datagram + drivers
// ---------------------------------------------------------------------------
struct Frame {
    std::uint8_t c[5]{};
    int rr_case = -1;            // -1 = IO-board frame
};
struct Dg { Frame f[2]; };

struct Driver {
    virtual ~Driver() = default;
    virtual const char* name() const = 0;
    virtual void reset() = 0;     // round robin to case 0, inputs to rest
    virtual Dg step() = 0;        // one datagram
    // inputs read by frame 0 / case 10
    int oc = 0;
    int dither = 0;
    int micboost = 0;
    int xmit = 0;
};

// Mirrors the write_main_loop_hl2 integration (plan §4.4): frame 1 always
// takes round-robin case idx; frame 2 is the IO frame when begin_datagram
// said so, else the next round-robin case.  An IO frame does not advance
// the round robin.  fixed_case >= 0 pins every round-robin frame to one
// case (slip-free phase for the exact-period check).
struct SimDriver : Driver {
    int idx = 0;
    int fixed_case = -1;
    const char* name() const override { return "sim"; }
    void reset() override { idx = 0; oc = dither = micboost = xmit = 0; }

    Frame synth(int c) const
    {
        Frame f;
        f.rr_case = c;
        f.c[0] = static_cast<std::uint8_t>(kCaseC0[c] | (xmit & 1));
        if (c == 0) {
            f.c[2] = static_cast<std::uint8_t>((oc << 1) & 0xFE);
            f.c[3] = static_cast<std::uint8_t>((dither & 1) << 3);
            f.c[4] = 0x1C;
        } else if (c == 10) {
            f.c[2] = static_cast<std::uint8_t>(0x40 | (micboost & 1));
        } else {
            f.c[1] = static_cast<std::uint8_t>(c);
        }
        return f;
    }
    int take()
    {
        if (fixed_case >= 0) return fixed_case;
        const int c = idx;
        idx = (idx < 18) ? idx + 1 : 0;
        return c;
    }
    int peek() const { return fixed_case >= 0 ? fixed_case : idx; }

    Dg step() override
    {
        Dg d;
        const bool io = iob::begin_datagram(peek());
        d.f[0] = synth(take());
        int f1_case = -1;
        if (io) {
            unsigned char C0 = static_cast<unsigned char>(xmit & 1), C1 = 0,
                          C2 = 0, C3 = 0, C4 = 0;
            const bool ok = iob::compose_frame(xmit, C0, C1, C2, C3, C4);
            CHECK(ok);
            d.f[1].c[0] = C0; d.f[1].c[1] = C1; d.f[1].c[2] = C2;
            d.f[1].c[3] = C3; d.f[1].c[4] = C4;
            d.f[1].rr_case = -1;
        } else {
            f1_case = peek();
            d.f[1] = synth(take());
        }
        iob::end_datagram(f1_case);
        return d;
    }
};

// Round-robin case from a frame's C0 address; -1 = not a round-robin frame.
static int case_from_c0(std::uint8_t c0)
{
    const std::uint8_t a = static_cast<std::uint8_t>(c0 & 0xFE);
    for (int i = 0; i < 19; ++i)
        if (kCaseC0[i] == a) return i;
    return -1;
}

// Drives the real write_main_loop_hl2 (src/wire/FrameComposer.cpp);
// metis_write_frame is captured by hl2_ioboard_test_stubs.cpp.  Inputs go
// through the fields compose_case_0 / compose_case_10 read.  The composer
// round robin is never reset (as in the application across sessions).
struct ComposerDriver : Driver {
    const char* name() const override { return "composer"; }
    void reset() override
    {
        hl2iob_test_wire_setup();
        oc = dither = micboost = xmit = 0;
    }
    Dg step() override
    {
        using namespace lyra::wire;
        prn->oc_output = oc;
        prn->adc[0].dither = dither;
        prn->mic.mic_boost = static_cast<unsigned char>(micboost & 1);
        XmitBit = xmit;
        static const char lriq[1008] = {};
        const long before = g_hl2iob_test_dg_count;
        write_main_loop_hl2(lriq);
        CHECK(g_hl2iob_test_dg_count == before + 1);
        Dg d;
        for (int k = 0; k < 2; ++k) {
            const std::uint8_t* p = g_hl2iob_test_dg + 512 * k;
            CHECK(p[0] == 0x7F && p[1] == 0x7F && p[2] == 0x7F);
            for (int i = 0; i < 5; ++i) d.f[k].c[i] = p[3 + i];
            d.f[k].rr_case = is_io_frame(d.f[k].c) ? -1 : case_from_c0(d.f[k].c[0]);
            CHECK(is_io_frame(d.f[k].c) || d.f[k].rr_case >= 0);
        }
        CHECK(!is_io_frame(d.f[0].c));
        return d;
    }
};

// ---------------------------------------------------------------------------
// Pico + gateware models
// ---------------------------------------------------------------------------
struct Pico {
    std::uint8_t R[256]{};
    std::uint64_t commit = 0;
    std::vector<std::uint64_t> commits;
    std::vector<long> commit_dg;
    double j4 = 0.0;
    int j4_changes = 0;
    std::vector<long> j4_change_dg;

    void write(std::uint8_t reg, std::uint8_t v, long dg)
    {
        R[reg] = v;
        if (reg == 4) {
            commit = (std::uint64_t{R[0]} << 32) | (std::uint64_t{R[1]} << 24)
                   | (std::uint64_t{R[2]} << 16) | (std::uint64_t{R[3]} << 8)
                   | std::uint64_t{R[4]};
            commits.push_back(commit);
            commit_dg.push_back(dg);
            const double nv = j4_for_hz(commit);
            if (nv != j4) {
                j4 = nv;
                ++j4_changes;
                j4_change_dg.push_back(dg);
            }
        }
    }
};

struct Gateware {
    bool ak4951 = true;
    double p_drop = 0.0;
    std::mt19937 rng{1};
    int filter = -1, rxant = -1, spon = -1, micboost = -1;
    double busy_until = -1.0;
    long host_lost_busy = 0, host_lost_udp = 0, host_ok = 0, f0_refused = 0;
    // what holds the bus when a host frame is dropped
    long lost_by_io = 0, lost_by_f0 = 0, lost_by_mb = 0;
    int busy_kind = 0;           // 1 IO, 2 frame 0, 3 mic boost
    long busy_dg = 0;
    long lost_by_f0_same_or_prev_dg = 0;
    Pico* pico = nullptr;

    void start(double t_us, int kind, long dg)
    {
        busy_until = t_us + 100.0;
        busy_kind = kind;
        busy_dg = dg;
    }

    void frame(const Frame& f, double t_us, long dg)
    {
        const int addr = (f.c[0] >> 1) & 0x3F;
        const bool busy = t_us < busy_until;
        if (addr == iob::kBus2CmdAddr) {
            if (p_drop > 0.0
                && std::uniform_real_distribution<double>(0, 1)(rng) < p_drop) {
                ++host_lost_udp;
                return;
            }
            if (busy) {
                ++host_lost_busy;
                if (busy_kind == 1) ++lost_by_io;
                if (busy_kind == 2) {
                    ++lost_by_f0;
                    if (dg - busy_dg <= 1) ++lost_by_f0_same_or_prev_dg;
                }
                if (busy_kind == 3) ++lost_by_mb;
                return;
            }
            start(t_us, 1, dg);
            ++host_ok;
            if (f.c[1] == 0x06 && (f.c[2] & 0x7F) == iob::kSlaveIoboard && pico)
                pico->write(f.c[3], f.c[4], dg);
            return;
        }
        if (addr == 0) {
            const int o  = (f.c[2] >> 1) & 0x7F;
            const int ra = (f.c[3] >> 5) & 1;
            const int d  = (f.c[3] >> 3) & 1;
            const bool fchg = (o != filter) || (ra != rxant);
            const bool dchg = ak4951 && d != spon;
            if (fchg || dchg) {
                if (busy) { ++f0_refused; return; }
                start(t_us, 2, dg);
                if (fchg) { filter = o; rxant = ra; }
                if (dchg) spon = d;
            }
            return;
        }
        if (addr == 9 && ak4951) {
            const int mb = f.c[2] & 1;
            if (mb != micboost) {
                if (busy) { ++f0_refused; return; }
                start(t_us, 3, dg);
                micboost = mb;
            }
        }
    }
};

// ---------------------------------------------------------------------------
// environment: one driver + models + captured stream + drained records
// ---------------------------------------------------------------------------
struct IoEvent {
    long dg;                 // datagram index (1-based since open)
    std::uint8_t c[5];
};

static long g_reg11_bad = 0;    // reg11_never_1_wire, across every run
static long g_frames_seen = 0;

struct Env {
    Driver* drv;
    Gateware gw;
    Pico pico;
    long dg = 0;                // datagrams stepped since open
    bool burst = false;
    std::vector<Dg> dgs;        // index dg-1
    std::vector<IoEvent> io;
    std::vector<iob::Record> recs;
    bool keep_dgs = true;

    explicit Env(Driver* d) : drv(d) { gw.pico = &pico; }

    void open(bool force)
    {
        drv->reset();
        iob::set_log_verbose(true);
        iob::init_for_open(force);
        dg = 0;
        burst_left = 0;
        gw.busy_until = -1.0;             // new session: clock restarts at 0
        dgs.clear(); io.clear(); recs.clear();
    }

    // Paced: one datagram per 2.625 ms.  Burst: the writer stalls and then
    // releases the backlog back-to-back (8 us apart) — groups of 1..8
    // datagrams, the last of a group on the paced grid.
    std::mt19937 trng{99};
    int burst_left = 0, burst_idx = 0;
    double burst_base = 0.0;
    double t_us(long n)
    {
        if (!burst) return n * 2625.0;
        if (burst_left == 0) {
            burst_left = 1 + (int)(trng() % 8);
            burst_idx = 0;
            burst_base = (n + burst_left - 1) * 2625.0 - (burst_left - 1) * 8.0;
        }
        --burst_left;
        return burst_base + 8.0 * burst_idx++;
    }

    void drain()
    {
        iob::Record r;
        while (iob::pop_record(r)) recs.push_back(r);
    }

    void step()
    {
        ++dg;
        const Dg d = drv->step();
        const double t = t_us(dg);
        for (int k = 0; k < 2; ++k) {
            ++g_frames_seen;
            if (is_io_frame(d.f[k].c)) {
                IoEvent e{dg, {}};
                for (int i = 0; i < 5; ++i) e.c[i] = d.f[k].c[i];
                io.push_back(e);
                if (d.f[k].c[1] == 0x06 && d.f[k].c[3] == iob::kRegRfInputs
                    && d.f[k].c[4] != 0 && d.f[k].c[4] != 2)
                    ++g_reg11_bad;
            }
            gw.frame(d.f[k], t + k * 5.0, dg);
        }
        if (keep_dgs) dgs.push_back(d);
        drain();
    }
    void run(long n) { for (long i = 0; i < n; ++i) step(); }

    // IO events filtered
    std::vector<IoEvent> writes() const
    {
        std::vector<IoEvent> v;
        for (const auto& e : io) if (e.c[1] == 0x06) v.push_back(e);
        return v;
    }
    std::vector<IoEvent> reads() const
    {
        std::vector<IoEvent> v;
        for (const auto& e : io) if (e.c[1] == 0x07) v.push_back(e);
        return v;
    }
    std::vector<iob::Record> recs_of(iob::RecKind k) const
    {
        std::vector<iob::Record> v;
        for (const auto& r : recs)
            if (r.kind == static_cast<std::uint8_t>(k)) v.push_back(r);
        return v;
    }
};

static void inject_reply(std::uint8_t c4)
{
    const std::uint8_t c[5] = {0xFA, 0xF1, 0xF1, 0xF1, c4};
    iob::on_i2c_reply(c);
}

// A valid word: TX hz, RX1 hz, port, RX codes on.
static std::uint64_t word_for(std::uint64_t tx, std::uint64_t rx1 = 0,
                              int port = 0, bool rxcodes = true,
                              bool sub = false, std::uint64_t rx2 = 0)
{
    iob::Inputs in;
    in.txHz = tx;
    in.rx1Hz = rx1 ? rx1 : tx;
    in.rx2Hz = rx2;
    in.subEnabled = sub;
    in.rxPort = port;
    in.sendRxCodes = rxcodes;
    return iob::compute_snapshot(in, 0);
}

// Expected write sequence for one armed cycle of word w.
static std::vector<std::pair<int, int>> expected_writes(std::uint64_t w)
{
    std::vector<std::pair<int, int>> v;
    const iob::Snapshot s = iob::unpack(w);
    if (s.sendTx) {
        std::uint8_t b[5];
        iob::pack_tx_freq_bytes(s.txHz, b);
        for (int i = 0; i < 5; ++i) v.push_back({i, b[i]});
    }
    v.push_back({11, iob::reg11_value(w)});
    if (s.sendRxCodes) {
        v.push_back({13, s.fcode13});
        v.push_back({14, s.fcode14});
    }
    return v;
}

// Writes grouped by latch record (cycle as seen on the wire).
struct CycleWrites {
    long latch_dg;
    std::uint64_t word;
    bool armed;
    std::vector<IoEvent> w;
};
static std::vector<CycleWrites> group_cycles(const Env& e)
{
    std::vector<CycleWrites> out;
    const auto latches = e.recs_of(iob::RecKind::Latch);
    const auto ws = e.writes();
    for (std::size_t i = 0; i < latches.size(); ++i) {
        CycleWrites c;
        c.latch_dg = latches[i].datagram;
        c.word = latches[i].word;
        c.armed = latches[i].reg != 0;
        const long end = (i + 1 < latches.size()) ? (long)latches[i + 1].datagram
                                                  : e.dg + 1;
        for (const auto& w : ws)
            if (w.dg > c.latch_dg && w.dg < end) c.w.push_back(w);
        out.push_back(c);
    }
    return out;
}

// ===========================================================================
// C1 gate — module only
// ===========================================================================
static void t_snapshot_layout()
{
    g_test = "snapshot_layout";
    std::mt19937_64 rng(7);
    for (int i = 0; i < 20000; ++i) {
        iob::Snapshot s;
        const std::uint64_t r = rng();
        s.valid = r & 1; s.sendTx = (r >> 1) & 1; s.sendRxCodes = (r >> 2) & 1;
        s.reg11Alt = (r >> 3) & 1;
        s.fcode13 = static_cast<std::uint8_t>(r >> 8);
        s.fcode14 = static_cast<std::uint8_t>(r >> 16);
        s.txHz = (r >> 20) & iob::kTxHzMask;
        const std::uint64_t w = iob::pack(s);
        const iob::Snapshot u = iob::unpack(w);
        CHECK((w & iob::kReservedMask) == 0);
        CHECK(u.valid == s.valid && u.sendTx == s.sendTx
              && u.sendRxCodes == s.sendRxCodes && u.reg11Alt == s.reg11Alt
              && u.fcode13 == s.fcode13 && u.fcode14 == s.fcode14
              && u.txHz == s.txHz);
        CHECK(iob::reg11_value(w) == (s.reg11Alt ? 2 : 0));
    }
    // out-of-range Hz cannot leak into the flag bits
    iob::Snapshot big;
    big.txHz = ~std::uint64_t{0};
    CHECK(iob::pack(big) == iob::kTxHzMask);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    // compute_snapshot output never sets reserved bits
    iob::Inputs in;
    in.txHz = ~std::uint64_t{0};
    in.rx1Hz = ~std::uint64_t{0};
    in.rxPort = 2;
    CHECK((iob::compute_snapshot(in, 0) & iob::kReservedMask) == 0);
}

static void t_frame_bytes_exact()
{
    g_test = "frame_bytes_exact";
    SimDriver sd;
    Env e(&sd);
    // writes, RX and MOX
    for (int mox = 0; mox <= 1; ++mox) {
        e.open(true);
        iob::publish(word_for(14074000));
        sd.xmit = mox;
        e.run(140);
        const auto ws = e.writes();
        CHECK(!ws.empty());
        for (const auto& w : ws) {
            CHECK(w.c[0] == (mox ? 0x7B : 0x7A));
            CHECK(w.c[1] == 0x06);
            CHECK(w.c[2] == 0x9D);
        }
        const auto exp = expected_writes(word_for(14074000));
        CHECK(ws.size() >= exp.size());
        for (std::size_t i = 0; i < exp.size() && i < ws.size(); ++i) {
            CHECK(ws[i].c[3] == exp[i].first);
            CHECK(ws[i].c[4] == exp[i].second);
        }
    }
    // detection read, RX and MOX
    for (int mox = 0; mox <= 1; ++mox) {
        e.open(false);
        iob::publish(word_for(7074000));
        sd.xmit = mox;
        e.run(30);
        const auto rs = e.reads();
        CHECK(rs.size() == 1);
        for (const auto& r : rs) {
            CHECK(r.c[0] == (mox ? 0xFB : 0xFA));
            CHECK(r.c[1] == 0x07);
            CHECK(r.c[2] == 0xC1);
            CHECK(r.c[3] == 0);
            CHECK(r.c[4] == 0);
        }
        CHECK(e.writes().empty());
    }
    // 14,074,000 = 00 00 D6 C0 90; 144,200,000 = 00 08 98 51 40
    std::uint8_t b[5];
    iob::pack_tx_freq_bytes(14074000ull, b);
    CHECK(b[0] == 0x00 && b[1] == 0x00 && b[2] == 0xD6 && b[3] == 0xC0 && b[4] == 0x90);
    iob::pack_tx_freq_bytes(144200000ull, b);
    CHECK(b[0] == 0x00 && b[1] == 0x08 && b[2] == 0x98 && b[3] == 0x51 && b[4] == 0x40);
}

static void t_detect_reply_parse()
{
    g_test = "detect_reply_parse";
    auto k = [](std::uint8_t c0, std::uint8_t c1, std::uint8_t c2,
                std::uint8_t c3, std::uint8_t c4) {
        const std::uint8_t c[5] = {c0, c1, c2, c3, c4};
        return iob::classify_reply(c);
    };
    CHECK(k(0xFA, 0xF1, 0xF1, 0xF1, 0xF1) == iob::ReplyKind::Present);
    CHECK(k(0xFB, 0xF1, 0xF1, 0xF1, 0xF1) == iob::ReplyKind::Present);  // PTT bit
    CHECK(k(0xFA, 0x00, 0x00, 0x00, 0x01) == iob::ReplyKind::Present);
    CHECK(k(0xFA, 0x00, 0x00, 0x00, 0x31) == iob::ReplyKind::Present);
    CHECK(k(0xFA, 0xF1, 0xF1, 0xF1, 0x00) == iob::ReplyKind::Absent);
    CHECK(k(0xFA, 0x00, 0x00, 0x00, 0xF0) == iob::ReplyKind::Absent);
    CHECK(k(0xF8, 0xF1, 0xF1, 0xF1, 0xF1) == iob::ReplyKind::Ignored);  // addr 0x3C
    CHECK(k(0x7A, 0xF1, 0xF1, 0xF1, 0xF1) == iob::ReplyKind::Ignored);  // no RQST

    // on_i2c_reply: present latches and only a Present reply sets it
    SimDriver sd;
    Env e(&sd);
    e.open(false);
    const std::uint8_t absent[5] = {0xFA, 0xF1, 0xF1, 0xF1, 0x00};
    iob::on_i2c_reply(absent);
    CHECK(!iob::present());
    const std::uint8_t other[5] = {0xF8, 0xF1, 0xF1, 0xF1, 0xF1};
    iob::on_i2c_reply(other);
    CHECK(!iob::present());
    inject_reply(0xF1);
    CHECK(iob::present());
    CHECK(iob::last_reply_c4() == 0xF1);
    iob::stop();
    e.open(false);
    CHECK(!iob::present());
    iob::stop();
    inject_reply(0xF1);                  // stopped: ignored
    CHECK(!iob::present());
}

static void t_fcode_matches_iob()
{
    g_test = "fcode_matches_iob";
    const std::uint64_t hz[] = {
        0ull, 1ull, 19999ull, 20000ull,
        1800000ull, 2000000ull, 3500000ull, 4000000ull, 5330500ull,
        5406500ull, 7000000ull, 7300000ull, 10100000ull, 10150000ull,
        14000000ull, 14350000ull, 18068000ull, 18168000ull, 21000000ull,
        21450000ull, 24890000ull, 24990000ull, 28000000ull, 29700000ull,
        50000000ull, 54000000ull, 70000000ull, 71000000ull, 144000000ull,
        148000000ull, 420000000ull, 450000000ull, 1240000000ull,
        1300000000ull, 270000000000ull, 270000000001ull,
        (std::uint64_t{1} << 40) - 1,
    };
    for (std::uint64_t h : hz)
        CHECKF(iob::rx_fcode(h) == iob_hertz2fcode(h),
               "hz=%llu lyra=%d iob=%d", (unsigned long long)h,
               (int)iob::rx_fcode(h), iob_hertz2fcode(h));
    CHECK(iob::rx_fcode(7074000ull) == 92);
    CHECK(iob::rx_fcode(14074000ull) == 102);
    CHECK(iob::rx_fcode(50000000ull) == 122);
    CHECK(iob::rx_fcode(144200000ull) == 138);
    // dense sweep 20 kHz .. 3 GHz
    for (double f = 20000.0; f < 3e9; f *= 1.0007)
        CHECK(iob::rx_fcode((std::uint64_t)f) == iob_hertz2fcode((std::uint64_t)f));
}

static void t_reg11_never_1_pack()
{
    g_test = "reg11_never_1_pack";
    const int ports[] = {-1, 0, 1, 2, 3, 255};
    for (int p : ports) {
        iob::Inputs in;
        in.txHz = 7074000;
        in.rx1Hz = 7074000;
        in.rxPort = p;
        const std::uint8_t v = iob::reg11_value(iob::compute_snapshot(in, 0));
        CHECK(v == 0 || v == 2);
        CHECK(v == (p == 2 ? 2 : 0));
    }
}

// ===========================================================================
// sequencer — run on each driver
// ===========================================================================
static void t_period(Driver* d, bool exact)
{
    g_test = "period_is_132_to_140_datagrams";
    Env e(d);
    e.open(true);
    iob::publish(word_for(7074000));
    e.run(132 * 30);
    const auto L = e.recs_of(iob::RecKind::Latch);
    CHECK(L.size() >= 28);
    long mn = 1 << 30, mx = 0;
    for (std::size_t i = 1; i < L.size(); ++i) {
        const long p = (long)L[i].datagram - (long)L[i - 1].datagram;
        mn = std::min(mn, p);
        mx = std::max(mx, p);
    }
    if (exact) {
        CHECKF(mn == 132 && mx == 132, "min=%ld max=%ld", mn, mx);
        // 11 slots x 12 datagrams x 2.625 ms = 346.5 ms
        CHECK(std::fabs(132 * 2.625 - 346.5) < 1e-9);
        // latch record timestamps in the paced model are not meaningful;
        // the cycle in datagrams is the contract
    } else {
        CHECKF(mn >= 132 && mx <= 140, "min=%ld max=%ld", mn, mx);
    }
    // first latch at datagram 1 (connect), slot 1 at datagram 13
    CHECK(!L.empty() && L[0].datagram == 1);
}

static void t_order_spacing_frame2_slip(Driver* d)
{
    Env e(d);
    e.open(true);
    std::uint64_t w = word_for(7074000, 7074000, 2);
    iob::publish(w);
    std::mt19937 rng(3);
    // change inputs at random phases so restarts happen too
    for (int k = 0; k < 40; ++k) {
        e.run(100 + rng() % 400);
        d->oc = rng() & 0x7F;           // frame-0 changes
        d->micboost = rng() & 1;
        if (k % 3 == 0) {
            w = word_for(1800000 + (rng() % 28000000), 0, (k & 1) ? 2 : 0);
            iob::publish(w);
        }
    }
    e.run(132 * 5);

    g_test = "order_per_cycle";
    const auto cyc = group_cycles(e);
    int full = 0;
    for (const auto& c : cyc) {
        if (!c.armed) continue;
        const auto exp = expected_writes(c.word);
        // a cycle abandoned by a restart is a prefix of the expected order
        CHECK(c.w.size() <= exp.size());
        for (std::size_t i = 0; i < c.w.size() && i < exp.size(); ++i)
            CHECK(c.w[i].c[3] == exp[i].first);
        if (c.w.size() == exp.size()) ++full;
    }
    CHECK(full > 20);

    g_test = "slot_spacing_ge_12_datagrams";
    for (std::size_t i = 1; i < e.io.size(); ++i)
        CHECKF(e.io[i].dg - e.io[i - 1].dg >= 12, "gap=%ld",
               e.io[i].dg - e.io[i - 1].dg);

    g_test = "second_frame_only";
    for (const auto& dg : e.dgs) {
        CHECK(!is_io_frame(dg.f[0].c));
        CHECK(dg.f[0].rr_case >= 0);
    }

    g_test = "slip_never_adjacent";
    for (std::size_t i = 0; i < e.dgs.size(); ++i) {
        if (!is_io_frame(e.dgs[i].f[1].c)) continue;
        const int f0 = e.dgs[i].f[0].rr_case;
        CHECK(f0 != 0 && f0 != 10);
        if (i > 0) {
            const int pf1 = e.dgs[i - 1].f[1].rr_case;
            CHECK(pf1 != 0 && pf1 != 10);
        }
    }
    CHECK(!e.recs_of(iob::RecKind::Slip).empty());

    g_test = "round_robin_not_skipped";
    std::vector<int> seq;
    for (const auto& dg : e.dgs)
        for (int k = 0; k < 2; ++k)
            if (!is_io_frame(dg.f[k].c)) seq.push_back(dg.f[k].rr_case);
    std::set<int> seen(seq.begin(), seq.end());
    CHECK(seen.size() == 19);
    for (std::size_t i = 1; i < seq.size(); ++i)
        CHECKF(seq[i] == (seq[i - 1] + 1) % 19, "i=%zu %d->%d", i,
               seq[i - 1], seq[i]);
}

static void t_identical_value_resent(Driver* d)
{
    g_test = "identical_value_resent";
    Env e(d);
    e.open(true);
    const std::uint64_t w = word_for(14074000, 14074000, 0);
    iob::publish(w);
    e.run(140 * 21);
    const auto cyc = group_cycles(e);
    const auto exp = expected_writes(w);
    int n = 0;
    for (std::size_t i = 0; i + 1 < cyc.size(); ++i) {
        CHECK(cyc[i].armed);
        CHECK(cyc[i].word == w);
        CHECK(cyc[i].w.size() == exp.size());
        for (std::size_t k = 0; k < exp.size() && k < cyc[i].w.size(); ++k) {
            CHECK(cyc[i].w[k].c[3] == exp[k].first);
            CHECK(cyc[i].w[k].c[4] == exp[k].second);
        }
        ++n;
    }
    CHECK(n >= 20);
    CHECK(e.recs_of(iob::RecKind::Restart).empty());
}

static void t_arm_only_at_slot0(Driver* d)
{
    g_test = "arm_only_at_slot0";
    Env e(d);
    e.open(false);
    iob::publish(word_for(7074000));
    // run to the probe (slot 1 of cycle 0)
    while (e.reads().empty() && e.dg < 200) e.step();
    CHECK(e.reads().size() == 1);
    const long probe_dg = e.reads()[0].dg;
    e.step();                                   // between slot 1 and slot 2
    inject_reply(0xF1);
    const long reply_dg = e.dg;
    e.run(140 * 3);
    const auto ws = e.writes();
    CHECK(!ws.empty());
    // no write before an armed latch that follows the reply via a restart
    long armed_latch = -1;
    for (const auto& r : e.recs) {
        if (r.kind == (std::uint8_t)iob::RecKind::Latch && r.reg == 1) {
            armed_latch = r.datagram;
            break;
        }
    }
    CHECK(armed_latch > reply_dg);
    CHECK(!ws.empty() && ws.front().dg > armed_latch);
    bool restart_before = false;
    for (const auto& r : e.recs)
        if (r.kind == (std::uint8_t)iob::RecKind::Restart
            && (long)r.datagram == armed_latch)
            restart_before = true;
    CHECK(restart_before);
    // restart serviced at the next schedule point (slot 2 position)
    CHECK(armed_latch - probe_dg <= 12 + 5);
    for (std::uint64_t c : e.pico.commits) CHECK(c != 0);
    CHECK(!e.pico.commits.empty());

    // present but nothing ever published (word 0 = invalid): never armed
    iob::publish(0);
    e.open(false);
    e.run(30);
    inject_reply(0xF1);
    e.run(140 * 3);
    CHECK(e.writes().empty());
    for (const auto& r : e.recs_of(iob::RecKind::Latch)) CHECK(r.reg == 0);
}

static void t_never_commit_zero(Driver* d)
{
    g_test = "never_commit_zero";
    Env e(d);
    e.open(true);
    iob::Inputs in;
    in.txHz = 144200000;
    in.txInXvtrSlot = true;
    in.rx1Hz = 144200000;
    in.xvtrPolicy = iob::XvtrPolicy::HoldLastHf;
    const std::uint64_t w = iob::compute_snapshot(in, 0);   // no HF history
    CHECK(!iob::unpack(w).sendTx);
    iob::publish(w);
    e.run(140 * 50);
    for (const auto& x : e.writes()) CHECK(x.c[3] > 4);
    CHECK(e.pico.commits.empty());
    int r11 = 0;
    for (const auto& x : e.writes()) if (x.c[3] == 11) ++r11;
    CHECK(r11 >= 49);
    // txHz = 0 under On-air RF as well
    in.txHz = 0;
    in.xvtrPolicy = iob::XvtrPolicy::OnAirRf;
    CHECK(!iob::unpack(iob::compute_snapshot(in, 14074000)).sendTx);
}

static void t_restart_latency(Driver* d)
{
    g_test = "restart_latency";
    const std::uint64_t A = 7074000, B = 14074000;
    long worst = 0;
    for (int off = 0; off < 140; ++off) {
        Env e(d);
        e.keep_dgs = false;
        e.open(true);
        iob::publish(word_for(A));
        e.run(132 * 4 + off);
        CHECK(e.pico.commit == A);
        iob::publish(word_for(B));
        const long pub = e.dg;
        long got = -1;
        for (int i = 0; i < 400 && got < 0; ++i) {
            e.step();
            if (e.pico.commit == B) got = e.dg;
        }
        CHECK(got > 0);
        worst = std::max(worst, got - pub);
        CHECKF(got - pub <= 89, "off=%d latency=%ld dg", off, got - pub);
    }
    std::printf("  [%s] restart_latency worst = %ld datagrams (%.1f ms)\n",
                d->name(), worst, worst * 2.625);
}

static void t_restart_not_starved(Driver* d)
{
    g_test = "restart_not_starved";
    Env e(d);
    e.keep_dgs = false;
    e.open(true);
    std::set<std::uint64_t> published;
    std::uint64_t tx = 7000000;
    const long total = (long)(30.0 / 0.002625);
    for (long i = 0; i < total; ++i) {
        if (i % 10 == 0) {
            tx += 1000;
            iob::publish(word_for(tx));
            published.insert(tx);
        }
        e.step();
    }
    CHECK(!e.pico.commits.empty());
    for (std::uint64_t c : e.pico.commits) CHECK(published.count(c) == 1);
    long maxgap = 0;
    for (std::size_t i = 1; i < e.pico.commit_dg.size(); ++i)
        maxgap = std::max(maxgap, e.pico.commit_dg[i] - e.pico.commit_dg[i - 1]);
    CHECKF(maxgap <= 2 * 140, "max commit gap %ld dg", maxgap);
    CHECK(e.pico.commit_dg.front() <= 2 * 140);
}

static void t_restart_no_partial_commit(Driver* d)
{
    g_test = "restart_no_partial_commit";
    const std::uint64_t A = 0x0111223344ull, B = 0x0255667788ull;
    for (int off = 0; off < 140; ++off) {
        Env e(d);
        e.keep_dgs = false;
        e.open(true);
        iob::publish(word_for(A, 7074000));
        e.run(132 * 3 + off);
        iob::publish(word_for(B, 7074000));
        e.run(140 * 3);
        for (std::uint64_t c : e.pico.commits) CHECK(c == A || c == B);
        CHECK(e.pico.commit == B);
    }
}

static void t_snapshot_single_source(Driver* d)
{
    g_test = "snapshot_single_source";
    Env e(d);
    e.open(true);
    std::mt19937 rng(11);
    for (int k = 0; k < 200; ++k) {
        iob::publish(word_for(100000 + rng() % 60000000, 0, (rng() & 1) ? 2 : 0,
                              rng() % 4 != 0, rng() & 1, 3000000 + rng() % 1000000));
        e.run(20 + rng() % 200);
    }
    e.run(140 * 2);
    for (const auto& c : group_cycles(e)) {
        if (!c.armed) { CHECK(c.w.empty()); continue; }
        const auto exp = expected_writes(c.word);
        for (std::size_t i = 0; i < c.w.size() && i < exp.size(); ++i) {
            CHECK(c.w[i].c[3] == exp[i].first);
            CHECK(c.w[i].c[4] == exp[i].second);
        }
    }
}

static void t_reg11_every_cycle(Driver* d)
{
    g_test = "reg11_every_cycle";
    Env e(d);
    e.open(true);
    iob::publish(word_for(7074000, 0, 0));
    e.run(140 * 20);
    std::mt19937 rng(5);
    int port = 0;
    std::vector<std::pair<long, int>> changes;   // publish dg, new value
    for (int k = 0; k < 60; ++k) {
        e.run(rng() % 300);
        port = port ? 0 : 2;
        const int mode = k % 3;   // vary sendTx / sendRxCodes too
        std::uint64_t w;
        if (mode == 0) {
            w = word_for(7074000, 0, port);
        } else if (mode == 1) {
            w = word_for(7074000, 0, port, false);          // no RX codes
        } else {
            iob::Inputs in;
            in.txHz = 144200000; in.txInXvtrSlot = true; in.rx1Hz = 144200000;
            in.rxPort = port;
            w = iob::compute_snapshot(in, 0);               // sendTx = 0
        }
        iob::publish(w);
        changes.push_back({e.dg, port});
    }
    e.run(140 * 3);
    const auto cyc = group_cycles(e);
    int complete = 0;
    for (std::size_t i = 0; i + 1 < cyc.size(); ++i) {
        const auto& c = cyc[i];
        CHECK(c.armed);
        int n11 = 0;
        for (const auto& w : c.w)
            if (w.c[3] == 11) {
                ++n11;
                CHECK(w.c[4] == iob::reg11_value(c.word));
            }
        // a cycle abandoned before slot 7 has none; a completed one has one
        const long len = cyc[i + 1].latch_dg - c.latch_dg;
        if (len >= 132) { CHECK(n11 == 1); ++complete; }
        else CHECK(n11 <= 1);
    }
    CHECK(complete > 50);
    // an RF1 <-> Alt publish is carried by the REG 11 write of the next
    // latched cycle (checked where no later publish lands before that
    // cycle's slot 7)
    int reflected = 0;
    for (std::size_t i = 0; i < changes.size(); ++i) {
        const CycleWrites* next = nullptr;
        for (const auto& c : cyc)
            if (c.latch_dg > changes[i].first) { next = &c; break; }
        if (!next) continue;
        const long next_pub = (i + 1 < changes.size()) ? changes[i + 1].first
                                                       : (1L << 30);
        if (next_pub <= next->latch_dg + 140) continue;
        int v = -1;
        for (const auto& w : next->w) if (w.c[3] == 11) v = w.c[4];
        CHECKF(v == changes[i].second, "change %zu: REG 11 %d want %d", i, v,
               changes[i].second);
        CHECK(next->latch_dg - changes[i].first <= 12 + 5 + 140);
        ++reflected;
    }
    CHECK(reflected > 10);
    // final state
    const auto ws = e.writes();
    int last11 = -1;
    for (const auto& w : ws) if (w.c[3] == 11) last11 = w.c[4];
    CHECK(last11 == port);
    CHECK(e.pico.R[11] == port);
}

static void t_rx2_off_reg14(Driver* d)
{
    g_test = "rx2_off_reg14_equals_reg13";
    Env e(d);
    e.open(true);
    iob::publish(word_for(14074000, 14074000, 0, true, false, 3573000));
    e.run(140 * 10);
    int n = 0;
    for (const auto& c : group_cycles(e)) {
        int r13 = -1, r14 = -1;
        for (const auto& w : c.w) {
            if (w.c[3] == 13) r13 = w.c[4];
            if (w.c[3] == 14) r14 = w.c[4];
        }
        if (r13 >= 0 && r14 >= 0) { CHECK(r13 == r14); CHECK(r13 == 102); ++n; }
    }
    CHECK(n >= 9);
    // SUB on: REG 14 = fcode(RX2)
    e.open(true);
    iob::publish(word_for(14074000, 14074000, 0, true, true, 3573000));
    e.run(140 * 3);
    CHECK(e.pico.R[13] == 102);
    CHECKF(e.pico.R[14] == iob::rx_fcode(3573000), "R14=%d want %d word=%llx",
           e.pico.R[14], iob::rx_fcode(3573000),
           (unsigned long long)iob::published());
}

static void t_probe_schedule(Driver* d)
{
    Env e(d);
    e.keep_dgs = false;
    e.open(false);
    iob::publish(word_for(7074000));
    e.run(132 * 330);

    g_test = "no_writes_until_armed";
    CHECK(e.writes().empty());
    CHECK(e.pico.commits.empty());

    g_test = "probe_backoff";
    std::vector<std::uint32_t> got;
    for (const auto& r : e.recs_of(iob::RecKind::Read)) got.push_back(r.cycle);
    std::vector<std::uint32_t> want;
    for (std::uint32_t c = 0; c <= 45; c += 5) want.push_back(c);
    for (std::uint32_t c = 45 + 87; c < 330; c += 87) want.push_back(c);
    CHECKF(got == want, "got %zu probes, want %zu", got.size(), want.size());
    CHECK(iob::probes_sent() == want.size());
    CHECK(e.reads().size() == want.size());
    // an absent reply changes nothing
    const std::uint8_t absent[5] = {0xFA, 0x00, 0x00, 0x00, 0x00};
    iob::on_i2c_reply(absent);
    e.run(140 * 2);
    CHECK(e.writes().empty());
}

static void t_force_present_no_probe(Driver* d)
{
    g_test = "force_present_no_probe";
    Env e(d);
    e.open(true);
    iob::publish(word_for(7074000));
    e.run(132 * 60);
    CHECK(e.reads().empty());
    const auto L = e.recs_of(iob::RecKind::Latch);
    CHECK(!L.empty() && L[0].reg == 1 && L[0].datagram == 1);
    CHECK(!e.writes().empty());
    CHECK(e.writes().front().dg <= 12 * 2 + 1 + 5);
}

static void t_no_traffic_when_stopped(Driver* d)
{
    g_test = "no_traffic_when_stopped";
    Env e(d);
    e.open(true);
    iob::publish(word_for(7074000));
    e.run(300);
    iob::stop();
    const std::size_t before = e.io.size();
    e.run(132 * 20);
    CHECK(e.io.size() == before);
    inject_reply(0xF1);
    e.run(132 * 2);
    CHECK(e.io.size() == before);
}

static void t_connect_reconnect(Driver* d)
{
    g_test = "connect_reconnect";
    Env e(d);
    const std::uint64_t w = word_for(21074000, 21074000, 2);
    for (int session = 0; session < 3; ++session) {
        e.open(false);
        CHECK(iob::probes_sent() == 0);
        CHECK(!iob::present());
        iob::publish(w);
        e.run(14);
        CHECK(e.reads().size() == 1);
        CHECK(iob::probes_sent() == 1);
        inject_reply(0xF1);
        e.run(140 * 2);
        const auto exp = expected_writes(w);
        const auto ws = e.writes();
        CHECK(ws.size() >= exp.size());
        for (std::size_t i = 0; i < exp.size() && i < ws.size(); ++i) {
            CHECK(ws[i].c[3] == exp[i].first);
            CHECK(ws[i].c[4] == exp[i].second);
        }
        CHECK(e.pico.commit == 21074000);
        iob::stop();
        const std::size_t n = e.io.size();
        e.run(500);
        CHECK(e.io.size() == n);
    }
}

// N2ADR-style OC pattern: LPF bit per band + bit 6 (3 MHz HPF) on RX above
// 3 MHz; keydown drops bit 6.
static int oc_rx(int band_idx) { return (1 << (band_idx % 6)) | (band_idx > 0 ? 0x40 : 0); }

static void t_hop_never_tears(Driver* d)
{
    g_test = "hop_never_tears";
    const std::uint64_t bands[] = {1840000, 3573000, 7074000, 10136000,
                                   14074000, 18100000, 21074000, 24915000,
                                   28074000, 50313000};
    for (int burst = 0; burst <= 1; ++burst)
        for (int ak = 0; ak <= 1; ++ak)
        for (int seed = 0; seed < 10; ++seed) {
            Env e(d);
            e.keep_dgs = false;
            e.burst = burst != 0;
            e.gw.ak4951 = ak != 0;
            e.trng.seed(1000 + seed);
            e.open(true);
            std::mt19937 rng(100 + burst * 2 + ak + 16 * seed);
            std::set<std::uint64_t> pub;
            int b = 2;
            d->oc = oc_rx(b);
            iob::publish(word_for(bands[b]));
            pub.insert(bands[b]);
            e.run(132 * 3);
            for (int h = 0; h < 200; ++h) {
                b = rng() % 10;
                d->oc = oc_rx(b);
                iob::publish(word_for(bands[b]));
                pub.insert(bands[b]);
                e.run(rng() % 400);
            }
            e.run(140 * 3);
            long torn = 0, unrepaired = 0;
            const auto& cm = e.pico.commits;
            for (std::size_t i = 0; i < cm.size(); ++i) {
                if (pub.count(cm[i]) == 1) continue;
                ++torn;
                // repaired: the next commit is a published value, by the next
                // cycle or the next restart (L2: <= 0.6 s = 229 datagrams)
                if (i + 1 >= cm.size() || pub.count(cm[i + 1]) != 1
                    || e.pico.commit_dg[i + 1] - e.pico.commit_dg[i] > 229) {
                    ++unrepaired;
                    std::printf("    torn at dg %ld; next %s at +%ld dg\n",
                                e.pico.commit_dg[i],
                                i + 1 >= cm.size() ? "none"
                                : pub.count(cm[i + 1]) ? "ok" : "TORN",
                                i + 1 >= cm.size() ? -1L
                                : e.pico.commit_dg[i + 1] - e.pico.commit_dg[i]);
                }
            }
            // IO frames never block each other; a frame 0 never blocks an IO
            // frame from the adjacent position (T4a)
            CHECK(e.gw.lost_by_io == 0);
            CHECK(e.pico.commit == bands[b]);
            CHECK(e.gw.filter == d->oc);
            std::printf("  [%s] hop_never_tears %s ak=%d seed=%d: %ld torn of "
                        "%zu commits, host IO lost to busy %ld (frame 0 %ld)\n",
                        d->name(), burst ? "burst" : "paced", ak, seed, torn,
                        cm.size(), e.gw.host_lost_busy, e.gw.lost_by_f0);
            if (burst) {
                // Back-to-back arrival puts frames 2..7 positions before the
                // IO frame inside the 100 us transaction a changed frame 0
                // starts; slip (adjacent frames only) cannot see them.
                // Guarantee here: every tear is repaired by the next commit
                // (plan §1.2 residual causes).
                CHECKF(unrepaired == 0, "%ld unrepaired torn commits", unrepaired);
                continue;
            }
            CHECK(e.gw.host_lost_busy == 0);
            CHECKF(torn == 0, "%ld torn of %zu commits burst=%d ak=%d "
                   "(host lost busy %ld [io %ld f0 %ld (adjacent %ld) mb %ld], "
                   "frame0 refused %ld)", torn,
                   e.pico.commits.size(), burst, ak, e.gw.host_lost_busy,
                   e.gw.lost_by_io, e.gw.lost_by_f0,
                   e.gw.lost_by_f0_same_or_prev_dg, e.gw.lost_by_mb,
                   e.gw.f0_refused);
        }
}

static void t_hop_then_key(Driver* d)
{
    g_test = "hop_then_key_never_tears";
    for (int ms = 0; ms <= 200; ms += 25) {
        for (int rep = 0; rep < 5; ++rep) {
            Env e(d);
            e.keep_dgs = false;
            e.open(true);
            const bool up = (rep & 1) == 0;
            const std::uint64_t from = up ? 7074000 : 14074000;
            const std::uint64_t to   = up ? 14074000 : 7074000;
            const int bfrom = up ? 2 : 4, bto = up ? 4 : 2;
            d->oc = oc_rx(bfrom);
            iob::publish(word_for(from));
            e.run(132 * 3 + rep * 17);
            const double vnew = j4_for_hz(to);
            CHECK(e.pico.j4 == j4_for_hz(from));
            // hop
            d->oc = oc_rx(bto);
            iob::publish(word_for(to));
            const long hop = e.dg;
            e.run((long)(ms / 2.625));
            // key down: TX pattern drops bit 6, MOX set
            d->oc = oc_rx(bto) & ~0x40;
            d->xmit = 1;
            long reached = -1;
            for (long i = 0; i < (long)(5000 / 2.625); ++i) {
                e.step();
                if (reached < 0 && e.pico.j4 == vnew) reached = e.dg;
            }
            // key up
            d->oc = oc_rx(bto);
            d->xmit = 0;
            for (long i = 0; i < (long)(5000 / 2.625); ++i) {
                e.step();
                if (reached < 0 && e.pico.j4 == vnew) reached = e.dg;
            }
            CHECK(reached > 0);
            CHECKF(reached - hop <= 89, "d=%d rep=%d J4 after %ld dg", ms, rep,
                   reached - hop);
            // J4: old band -> new band, then constant
            CHECKF(e.pico.j4_changes == 2, "d=%d rep=%d j4 changes=%d", ms,
                   rep, e.pico.j4_changes);
            CHECK(e.pico.j4 == vnew);
            for (std::uint64_t c : e.pico.commits) CHECK(c == from || c == to);
        }
    }
    d->xmit = 0;
}

static void t_drop_injection_converges(Driver* d)
{
    g_test = "drop_injection_converges";
    for (int seed = 0; seed < 100; ++seed) {
        Env e(d);
        e.keep_dgs = false;
        e.gw.rng.seed(seed);
        e.open(true);
        std::mt19937 rng(seed * 7 + 1);
        e.gw.p_drop = 0.3;
        std::uint64_t w = 0;
        for (int k = 0; k < 5; ++k) {
            w = word_for(1800000 + rng() % 50000000, 1800000 + rng() % 30000000,
                         (rng() & 1) ? 2 : 0, true, rng() & 1,
                         1800000 + rng() % 30000000);
            iob::publish(w);
            e.run(132 * 10);
        }
        e.gw.p_drop = 0.0;
        e.run(141);
        const iob::Snapshot s = iob::unpack(w);
        std::uint8_t b[5];
        iob::pack_tx_freq_bytes(s.txHz, b);
        bool ok = true;
        for (int i = 0; i < 5; ++i) ok = ok && e.pico.R[i] == b[i];
        ok = ok && e.pico.R[11] == iob::reg11_value(w);
        ok = ok && e.pico.R[13] == s.fcode13 && e.pico.R[14] == s.fcode14;
        ok = ok && e.pico.commit == s.txHz;
        CHECKF(ok, "seed=%d", seed);
    }
}

static void t_steady_state(Driver* d)
{
    g_test = "steady_state_no_commit_change";
    Env e(d);
    e.keep_dgs = false;
    e.gw.rng.seed(42);
    e.open(true);
    iob::publish(word_for(7074000));
    e.run(140 * 2);
    e.gw.p_drop = 0.05;
    const int ch0 = e.pico.j4_changes;
    const double v0 = e.pico.j4;
    e.run((long)(30.0 * 60.0 / 0.002625));
    CHECK(e.pico.j4_changes == ch0);
    CHECK(e.pico.j4 == v0);
    for (std::uint64_t c : e.pico.commits) CHECK(c == 7074000);
    CHECK(e.gw.host_lost_udp > 0);
}

// ===========================================================================
// record ring
// ===========================================================================
static void t_record_ring_spsc()
{
    g_test = "record_ring_spsc";
    static iob::RecordRing<512> ring;
    ring.reset();
    const std::uint32_t N = 1000000;
    std::atomic<bool> done{false};
    std::uint64_t pops = 0, torn = 0, order = 0;
    std::thread consumer([&]() {
        iob::Record r;
        std::int64_t last = -1;
        for (;;) {
            if (ring.pop(r)) {
                ++pops;
                const std::uint32_t s = r.datagram;
                if (r.cycle != ~s || r.word != (std::uint64_t{s} * 0x9E3779B97F4A7C15ull)
                    || r.t_ns != s || r.slot != (std::uint8_t)s
                    || r.val != (std::uint8_t)(s >> 8))
                    ++torn;
                if ((std::int64_t)s <= last) ++order;
                last = s;
            } else if (done.load(std::memory_order_acquire)) {
                if (!ring.pop(r)) break;
                ++pops;
            }
        }
    });
    for (std::uint32_t s = 0; s < N; ++s) {
        iob::Record r{};
        r.datagram = s;
        r.cycle = ~s;
        r.word = std::uint64_t{s} * 0x9E3779B97F4A7C15ull;
        r.t_ns = s;
        r.slot = (std::uint8_t)s;
        r.val = (std::uint8_t)(s >> 8);
        ring.push(r);
    }
    done.store(true, std::memory_order_release);
    consumer.join();
    const std::uint32_t drops = ring.take_drops();
    CHECK(torn == 0);
    CHECK(order == 0);
    CHECKF(pops + drops == N, "pops=%llu drops=%u", (unsigned long long)pops, drops);
    // full ring with no consumer
    ring.reset();
    iob::Record r{};
    for (int i = 0; i < 600; ++i) ring.push(r);
    CHECK(ring.take_drops() == 600 - 512);
    int n = 0;
    while (ring.pop(r)) ++n;
    CHECK(n == 512);
}

// ===========================================================================
static void run_sequencer_suite(Driver* d, bool exact_period_possible)
{
    std::printf("-- driver: %s\n", d->name());
    t_period(d, false);
    if (exact_period_possible) {
        auto* sd = static_cast<SimDriver*>(d);
        sd->fixed_case = 5;
        t_period(d, true);
        sd->fixed_case = -1;
    }
    t_order_spacing_frame2_slip(d);
    t_identical_value_resent(d);
    t_arm_only_at_slot0(d);
    t_never_commit_zero(d);
    t_restart_latency(d);
    t_restart_not_starved(d);
    t_restart_no_partial_commit(d);
    t_snapshot_single_source(d);
    t_reg11_every_cycle(d);
    t_rx2_off_reg14(d);
    t_probe_schedule(d);
    t_force_present_no_probe(d);
    t_no_traffic_when_stopped(d);
    t_connect_reconnect(d);
    t_hop_never_tears(d);
    t_hop_then_key(d);
    t_drop_injection_converges(d);
    t_steady_state(d);
}

int main()
{
    t_snapshot_layout();
    t_frame_bytes_exact();
    t_detect_reply_parse();
    t_fcode_matches_iob();
    t_reg11_never_1_pack();

    SimDriver sim;
    run_sequencer_suite(&sim, true);
    ComposerDriver composer;
    run_sequencer_suite(&composer, false);

    t_record_ring_spsc();

    g_test = "reg11_never_1_wire";
    CHECKF(g_reg11_bad == 0, "%ld REG 11 frames outside {0,2} of %ld frames",
           g_reg11_bad, g_frames_seen);

    iob::stop();
    if (g_fail) {
        std::fprintf(stderr, "test_hl2_ioboard_frames: %d of %d checks FAILED\n",
                     g_fail, g_checks);
        return 1;
    }
    std::printf("test_hl2_ioboard_frames: ok (%d checks, %ld frames)\n",
                g_checks, g_frames_seen);
    return 0;
}

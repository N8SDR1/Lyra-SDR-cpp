// Lyra — real-time scheduling for the wire threads.
//
// The HL2 Protocol-1 wire path is cadence-critical: the EP2 writer must
// hand the radio a datagram roughly every 2.6 ms, and the EP6 reader must
// drain inbound datagrams before the socket buffer overruns.  Missing
// those windows under load is what testers report as "PA chatter" or
// distorted TX.
//
// Windows buys that headroom with MMCSS ("Pro Audio") plus a raised
// process priority class (see src/win_perf.h).  macOS has no MMCSS; the
// equivalent is the Mach time-constraint scheduling policy, which tells
// the kernel "this thread needs `computation` ns of CPU within every
// `period` ns" and schedules it ahead of ordinary timeshare threads.
//
// Call promoteCurrentThreadToRealTime() from inside the thread itself,
// once, after it starts.  Failure is non-fatal and reported by the return
// value: the thread keeps running at normal priority, which degrades
// cadence under load but is not a correctness problem.

#pragma once

#include <cstdint>

namespace lyra::platform {

// Raise the CALLING thread to real-time scheduling.
//
// `periodNanoseconds` is how often the thread expects to run (for the EP2
// writer: the ~2.6 ms datagram interval).  Pass 0 if the cadence is not
// strictly periodic; a sensible audio-thread default is used.
//
// Returns true if the thread was promoted.
bool promoteCurrentThreadToRealTime(std::uint64_t periodNanoseconds = 0);

// Human-readable description of what the last promotion attempt did, for
// the diagnostics log.  Never empty.
const char *realTimeSchedulingStatus();

}  // namespace lyra::platform

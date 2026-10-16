// Lyra — real-time scheduling for the wire threads.  See thread_priority.h.

#include "platform/thread_priority.h"

#if defined(__APPLE__)
#  include <mach/mach_init.h>
#  include <mach/mach_time.h>
#  include <mach/thread_act.h>
#  include <mach/thread_policy.h>
#  include <pthread.h>
#elif !defined(_WIN32)
#  include <pthread.h>
#  include <sched.h>
#endif

namespace lyra::platform {

namespace {

// Set by the last promotion attempt so the caller can log what happened.
const char *g_status = "real-time scheduling not attempted";

#if defined(__APPLE__)

// Convert nanoseconds to the absolute-time units thread_policy_set wants.
std::uint32_t nanosecondsToAbsolute(std::uint64_t nanoseconds)
{
    static mach_timebase_info_data_t timebase{};
    if (timebase.denom == 0) mach_timebase_info(&timebase);
    if (timebase.numer == 0) return static_cast<std::uint32_t>(nanoseconds);
    return static_cast<std::uint32_t>(nanoseconds * timebase.denom / timebase.numer);
}

#endif

}  // namespace

bool promoteCurrentThreadToRealTime(std::uint64_t periodNanoseconds)
{
#if defined(__APPLE__)
    // Default to a 2.6 ms period — the HL2 EP2 datagram cadence.
    if (periodNanoseconds == 0) periodNanoseconds = 2'600'000ULL;

    // Ask for a modest slice of each period.  Over-requesting makes the
    // kernel refuse the policy outright, and the wire threads do very
    // little work per wakeup (a memcpy and a sendto) — they need to run
    // PROMPTLY, not for long.
    const std::uint64_t computation = periodNanoseconds / 8;
    const std::uint64_t constraint  = periodNanoseconds / 2;

    thread_time_constraint_policy_data_t policy{};
    policy.period      = nanosecondsToAbsolute(periodNanoseconds);
    policy.computation = nanosecondsToAbsolute(computation);
    policy.constraint  = nanosecondsToAbsolute(constraint);
    // false => the thread is not preemptible mid-computation, which is
    // what a fixed-cadence wire thread wants.
    policy.preemptible = 0;

    const kern_return_t result = thread_policy_set(
        pthread_mach_thread_np(pthread_self()),
        THREAD_TIME_CONSTRAINT_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_TIME_CONSTRAINT_POLICY_COUNT);

    if (result == KERN_SUCCESS) {
        g_status = "Mach time-constraint policy applied";
        return true;
    }
    g_status = "Mach time-constraint policy refused; running timeshare";
    return false;

#elif defined(_WIN32)
    (void)periodNanoseconds;
    // On Windows the wire threads use MMCSS directly at their call sites
    // (AvSetMmThreadCharacteristics), matching the Thetis reference.
    g_status = "Windows uses MMCSS at the call site";
    return true;

#else
    (void)periodNanoseconds;
    // Linux: SCHED_FIFO needs RLIMIT_RTPRIO or CAP_SYS_NICE, which a
    // desktop app usually lacks.  Try, and report honestly if refused.
    sched_param parameters{};
    parameters.sched_priority = (sched_get_priority_min(SCHED_FIFO) +
                                 sched_get_priority_max(SCHED_FIFO)) / 2;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &parameters) == 0) {
        g_status = "SCHED_FIFO applied";
        return true;
    }
    g_status = "SCHED_FIFO refused (needs RLIMIT_RTPRIO); running SCHED_OTHER";
    return false;
#endif
}

const char *realTimeSchedulingStatus() { return g_status; }

}  // namespace lyra::platform

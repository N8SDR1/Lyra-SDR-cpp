// Lyra — keep macOS from throttling Lyra while a radio is streaming.
//
// macOS App Nap slows timers and lowers the scheduling priority of an app
// whose windows are hidden or in the background, and idle sleep stops it
// outright.  Either one breaks a live radio stream: the wire cadence and
// the TX pump are timer-driven, and unattended receiving (overnight FT8
// monitoring, skimming) must keep running with nobody at the keyboard.
//
// setStreamingActivity(true) declares a user-initiated, latency-critical
// activity that disables App Nap and prevents idle system sleep (the
// display may still sleep); setStreamingActivity(false) ends it.  Calls
// are idempotent — repeating the current state does nothing.  Call from
// the GUI thread.
//
// No-op on platforms without App Nap.

#pragma once

namespace lyra::platform {

#if defined(__APPLE__)
void setStreamingActivity(bool streaming);
#else
inline void setStreamingActivity(bool) {}
#endif

}  // namespace lyra::platform

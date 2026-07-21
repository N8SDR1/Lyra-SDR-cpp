// Lyra — Windows performance controls (process priority + network throttle).
//
// Two operator-facing knobs that help Lyra hold its HL2 wire cadence when
// the machine is under load from other apps (a common tester report:
// "Chrome running alongside Lyra causes PA chatter / distorted TX" — the
// EP2 writer thread misses its ~2.6 ms send window under contention on a
// weak laptop).  Both are per-PC/machine-local, not per-rig.
//
//   * Process priority — SetPriorityClass on the current process so
//     Lyra's threads compete better with a busy foreground app.  Normal
//     (Windows default) / Above Normal / High.  High is the useful ceiling;
//     Realtime is deliberately NOT offered (starves the OS).  No elevation
//     needed for these three classes.
//
//   * Network throttle — Windows' multimedia NetworkThrottlingIndex caps
//     non-multimedia UDP to ~10 packets/ms, which can starve the HL2
//     EP6/EP2 stream.  Setting it to 0xFFFFFFFF disables the cap (the
//     classic HL2/SDR "gaming" fix).  The value lives in HKLM, so writing
//     it needs elevation — setNetworkThrottleDisabled() launches an
//     elevated one-shot reg.exe (a single UAC prompt); reading the current
//     state does not need elevation.  Takes effect after a reboot.
//
// Windows-only (the project is Windows-only); the POSIX build gets inert
// stubs so the callers compile.

#pragma once

namespace lyra::perf {

// Operator process-priority selection.  Ordinal is persisted (hw/processPriority)
// — do NOT renumber.
enum class ProcessPriority {
    Normal      = 0,   // Windows default
    AboveNormal = 1,
    High        = 2,
};

// Apply <level> (a ProcessPriority ordinal, 0..2) to the current process
// via SetPriorityClass.  Out-of-range clamps to Normal.  Best-effort:
// failure is logged and ignored (a lost priority bump is never fatal).
void applyProcessPriority(int level);

// True if NetworkThrottlingIndex == 0xFFFFFFFF (throttle disabled) in HKLM.
// Reads only — no elevation required.  False if the value is absent, any
// other value, or unreadable.
bool networkThrottleDisabled();

// Enable (disable=false → restore Windows default 0x0000000A) or disable
// (disable=true → 0xFFFFFFFF) the multimedia network throttle in HKLM via
// an elevated one-shot reg.exe (raises a UAC prompt).  Returns true if the
// elevated helper was launched and reported success; false if the user
// declined UAC or the write failed.  Effect applies after reboot.
bool setNetworkThrottleDisabled(bool disable);

} // namespace lyra::perf

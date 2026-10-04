// Lyra — CW keyer element pump (#105 CW-3a, host software keyer).
//
// Owns a dedicated worker thread that walks a CwMorse element list on
// a monotonic absolute-deadline schedule (locked decision B,
// cw3_software_keyer_design.md §4) and drives two injected callbacks:
//   - keyFn(on):  the per-element CW key  → wire `tx[0].cwx`
//   - pttFn(on):  the message-level hold  → wire `tx[0].cwx_ptt`
// The gateware keys the carrier on `cwx` (cwx_keydown) and holds TX
// between elements via `cwx_ptt` + its 500-unit spacing hang
// (hl2_rtl_dsopenhpsdr1.v:422); the host need only toggle these bits.
//
// Callbacks are injected (std::function) so the keyer has no hard
// dependency on HL2Stream and is unit-testable with capture lambdas.
// The keyer thread calls them; the callee (HL2Stream::setCwx*) is
// responsible for its own prn-field synchronisation vs the EP2 writer.
//
// Absolute-deadline scheduling: each element holds its bit until a
// steady_clock deadline that accumulates element durations, so
// per-element scheduler jitter does not drift the overall WPM. The
// wait is condvar-interruptible so abort() / shutdown break a
// long dah immediately.
//
// Reference model: the CWX engine's element-rate timer-driven
// state machine (cwx.cs:257-272 timer, :1805-1808 / :2195-2276
// process_element; :313-323 quitshut abort). Lyra-native realisation.

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <vector>

#include "CwMorse.h"

namespace lyra::tx {

class CwKeyer {
public:
    using BitFn   = std::function<void(bool)>;
    using StateFn = std::function<void(bool)>;
    // Type-ahead display hook (committed = already keyed / locked,
    // pending = still-editable tail).  Fired whenever either changes
    // (a keystroke, a backspace, a commit, a clear).  Callee marshals
    // to its own thread — this may fire from the caller's thread
    // (pushChar/backspace/clearPending) OR the keyer pump thread (a
    // commit / run end).  See the type-ahead block below.
    using TextFn  = std::function<void(const std::string& committed,
                                       const std::string& pending)>;

    // keyFn → cwx, pttFn → cwx_ptt. onState(true) fires just before
    // the first key of a message, onState(false) after the last
    // element + the cwx_ptt drop (the FSM/TX-state hook — break-in
    // policy lives in the callee). Spawns the worker thread.
    CwKeyer(BitFn keyFn, BitFn pttFn, StateFn onState = {}, TextFn onText = {});
    ~CwKeyer();

    CwKeyer(const CwKeyer&)            = delete;
    CwKeyer& operator=(const CwKeyer&) = delete;

    // Translate `text` at `wpm` / `weightPct` and enqueue its
    // elements. If a message is already playing, the new elements are
    // appended (continuous send, like a logger queuing). Thread-safe.
    void send(const std::string& text, int wpm, int weightPct = 50);

    // ── Type-ahead (CWX kbufnew/kbufold model, faithful to Thetis
    // cwx.cs) ──────────────────────────────────────────────────────
    // pushChar appends ONE character to the editable staging tail;
    // the pump commits characters to the element list one at a time
    // as the previous drains, holding cwx_ptt across the run (a hang
    // bridges inter-character typing pauses so the run does not chop).
    // A committed character is on the air and cannot be recalled —
    // backspacePending() only edits the not-yet-committed tail. All
    // three are thread-safe and drive the TextFn (committed, pending).
    void pushChar(char c, int wpm, int weightPct = 50);
    bool backspacePending();     // drop the last staged (uncommitted) char; false if none
    void clearPending();         // clear the editable tail (does not stop in-flight keying)

    // Terminal mode (default off). While on, a finished message keeps
    // cwx_ptt up so the TR relay stays in TX between letters. Turning
    // it off mid-message finishes the letters already queued, then
    // drops the line — it does not chop a dit. A 30 s idle with no
    // new text drops the line on its own.
    void setHoldAfter(bool on);
    using DrainFn = std::function<void()>;
    using CallFn  = std::function<void(const std::string&)>;
    void setDrainFn(DrainFn fn);          // queue just drained into the hold
    void setHoldTimeoutFn(DrainFn fn);    // 30 s idle dropped the hold
    void setCallsignFn(CallFn fn);        // last callsign letter just started

    // One staged message: prefix and suffix are ordinary text, the
    // callsign stays editable until its letters leave the tail.
    // A 1-arg correction replaces only the not-yet-keyed callsign.
    // Returns false when that callsign is already on the air.
    void queueMessage(const std::string& prefix, const std::string& call,
                      const std::string& suffix, int wpm, int weightPct = 50);
    bool correctCall(const std::string& call, int wpm, int weightPct = 50);

    // Immediately abort: flush the queue + the staging tail, drop key
    // + cwx_ptt, end the message (CWX Esc). Safe to call from any
    // thread (a paddle interrupt or operator Stop). Thread-safe.
    void abort();

    bool busy() const noexcept { return busy_.load(std::memory_order_relaxed); }

private:
    void run();
    // Snapshot committedText_ + the staging tail and fire onTextFn_.
    // MUST be called with m_ held (onTextFn_ only marshals — it never
    // re-enters the keyer — so calling it under the lock is safe and
    // keeps the display snapshots strictly in mutation order).
    void emitTextLocked();
    struct PendingChar {
        std::string glyph;
        int  wpm = 20;
        int  weight = 50;
        bool callsign = false;
        bool prosign = false;
    };
    std::vector<PendingChar> parseAtoms(const std::string& text, bool callsign,
                                        int wpm, int weight) const;

    BitFn   keyFn_;
    BitFn   pttFn_;
    StateFn onStateFn_;
    TextFn  onTextFn_;
    DrainFn onDrainFn_;
    DrainFn onHoldTimeoutFn_;
    CallFn  onCallFn_;

    std::mutex              m_;
    std::condition_variable cv_;
    std::vector<CwElement>  queue_;         // pending elements (guarded by m_)
    std::deque<PendingChar> pending_;       // editable type-ahead tail (guarded by m_)
    std::string             committedText_; // chars keyed this run, for display (guarded by m_)
    std::string             callSent_;       // callsign letters already committed
    bool                    callWindowDone_ = true;
    bool                    holdAfter_ = false;
    bool                    holdAnnounced_ = false;
    bool                    abort_ = false;
    bool                    stop_  = false;  // thread-exit request
    std::atomic<bool>       busy_{false};

    std::thread th_;
};

}  // namespace lyra::tx

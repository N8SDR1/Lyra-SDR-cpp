// Lyra — CW keyer element pump (#105 CW-3a). See CwKeyer.h.

#include "CwKeyer.h"

#include <chrono>

namespace lyra::tx {

using clock = std::chrono::steady_clock;

CwKeyer::CwKeyer(BitFn keyFn, BitFn pttFn, StateFn onState, TextFn onText)
    : keyFn_(std::move(keyFn)),
      pttFn_(std::move(pttFn)),
      onStateFn_(std::move(onState)),
      onTextFn_(std::move(onText)) {
    th_ = std::thread([this] { run(); });
}

CwKeyer::~CwKeyer() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_  = true;
        abort_ = true;
        queue_.clear();
        pending_.clear();
    }
    cv_.notify_all();
    if (th_.joinable())
        th_.join();
}

void CwKeyer::emitTextLocked() {
    if (!onTextFn_)
        return;
    std::string pend;
    pend.reserve(pending_.size());
    for (const auto& pc : pending_)
        pend.push_back(pc.c);
    onTextFn_(committedText_, pend);
}

void CwKeyer::send(const std::string& text, int wpm, int weightPct) {
    auto elems = cwTextToElements(text, wpm, weightPct);
    if (elems.empty())
        return;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (stop_)
            return;
        queue_.insert(queue_.end(), elems.begin(), elems.end());
    }
    cv_.notify_all();
}

void CwKeyer::pushChar(char c, int wpm, int weightPct) {
    {
        std::lock_guard<std::mutex> lk(m_);
        if (stop_)
            return;
        pending_.push_back({c, wpm, weightPct});
        emitTextLocked();
    }
    cv_.notify_all();
}

bool CwKeyer::backspacePending() {
    std::lock_guard<std::mutex> lk(m_);
    if (pending_.empty())
        return false;                       // nothing editable — the tail is on the air
    pending_.pop_back();
    emitTextLocked();
    return true;
}

void CwKeyer::clearPending() {
    std::lock_guard<std::mutex> lk(m_);
    if (pending_.empty())
        return;
    pending_.clear();
    emitTextLocked();
}

void CwKeyer::abort() {
    {
        std::lock_guard<std::mutex> lk(m_);
        abort_ = true;
        queue_.clear();
        pending_.clear();
        if (!committedText_.empty()) {
            committedText_.clear();
            emitTextLocked();               // CWX Esc: clear the line
        }
    }
    cv_.notify_all();
}

void CwKeyer::run() {
    std::unique_lock<std::mutex> lk(m_);
    for (;;) {
        // Idle: wait for work (or shutdown).
        cv_.wait(lk, [this] { return stop_ || !queue_.empty() || !pending_.empty(); });
        if (stop_)
            return;

        // A fresh run begins. abort_ may be stale-true from a prior
        // aborted run; clear it for this one.
        abort_   = false;
        committedText_.clear();
        busy_.store(true, std::memory_order_relaxed);
        bool typeAhead = false;             // ≥1 staged char committed this run → bridge applies
        bool runFirst  = true;              // no leading gap before the run's first mark
        bool wordGap   = false;             // next committed char uses inter-word (7·dit) gap
        int  lastWpm   = 20;                // WPM of the last committed char (bridge length)

        lk.unlock();
        if (onStateFn_) onStateFn_(true);
        pttFn_(true);                       // hold cwx_ptt for the run
        lk.lock();

        // Absolute-deadline element walk (jitter does not accumulate).
        auto deadline = clock::now();
        for (;;) {
            if (abort_ || stop_)
                break;

            if (queue_.empty()) {
                // The element list has no trailing gap, so the last element
                // played was a MARK — release the key before any inter-
                // character space / bridge so the carrier is never held
                // keyed-down while we wait (that was a stuck-tone tail).
                lk.unlock();
                keyFn_(false);
                lk.lock();
                if (abort_ || stop_)
                    break;

                if (!pending_.empty()) {
                    // Commit one staged character → elements.  This is
                    // the point a character moves from editable to
                    // on-the-air (CWX kbufnew → element fifo).
                    PendingChar pc = pending_.front();
                    pending_.pop_front();
                    typeAhead = true;
                    lastWpm   = pc.wpm;
                    committedText_.push_back(pc.c);
                    emitTextLocked();

                    if (pc.c == ' ' || pc.c == '\t') {
                        wordGap = true;     // a typed space → next letter gets the 7·dit gap
                        continue;           // the space itself keys nothing
                    }

                    const int ditUs = cwDitUs(pc.wpm);
                    const int gapUs = runFirst ? 0 : (wordGap ? 7 : 3) * ditUs;
                    wordGap  = false;
                    runFirst = false;
                    if (gapUs > 0)
                        queue_.push_back({false, gapUs});   // key already released above
                    auto elems = cwTextToElements(std::string(1, pc.c), pc.wpm, pc.weight);
                    queue_.insert(queue_.end(), elems.begin(), elems.end());
                    continue;
                }

                // Nothing pending. A plain send()/macro message ends the
                // instant it drains (unchanged — cwx_ptt drops now, the
                // gateware spacing hang covers the carrier tail). A type-
                // ahead run holds cwx_ptt open for just ONE inter-character
                // space (WPM-adaptive): a keystroke within it continues the
                // run seamlessly, otherwise the over ends promptly. The key
                // is already released, so the wait is silent — no long tail.
                if (!typeAhead)
                    break;
                const auto bridge = std::chrono::microseconds(3 * cwDitUs(lastWpm));
                const bool got = cv_.wait_for(lk, bridge, [this] {
                    return abort_ || stop_ || !pending_.empty();
                });
                if (abort_ || stop_ || !got)
                    break;
                // Keep `deadline` continuous so the inter-character gap the
                // next commit inserts stays exactly 3 dits from the last mark.
                continue;
            }

            CwElement e = queue_.front();
            queue_.erase(queue_.begin());

            lk.unlock();
            keyFn_(e.key);                  // cwx = mark/space
            lk.lock();

            deadline += std::chrono::microseconds(e.durationUs);
            // Interruptible hold until this element's deadline.
            cv_.wait_until(lk, deadline, [this] { return abort_ || stop_; });
        }

        const bool aborted = abort_ || stop_;
        lk.unlock();
        keyFn_(false);                      // ensure key released
        pttFn_(false);                      // drop cwx_ptt (gateware hang covers tail)
        if (onStateFn_) onStateFn_(false);
        lk.lock();

        busy_.store(false, std::memory_order_relaxed);
        abort_ = false;
        if (aborted) {
            // abort() already flushed queue_/pending_/committedText_ and
            // emitted the empty line; just make sure nothing lingers.
            queue_.clear();
            pending_.clear();
            committedText_.clear();
        } else if (pending_.empty()) {
            // Clean drain, nothing waiting — the type-ahead line is spent
            // (all on the air). Reset the display for the next over.
            if (!committedText_.empty()) {
                committedText_.clear();
                emitTextLocked();
            }
        }
        // Else: the operator started typing again during teardown — leave
        // pending_ intact; the outer loop restarts a fresh run (which
        // clears committedText_ at the top) so no keystroke is lost.
        if (stop_)
            return;
    }
}

}  // namespace lyra::tx

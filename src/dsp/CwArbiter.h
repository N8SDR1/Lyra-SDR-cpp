// Lyra — CW "Auto" arbiter.
//
// Supervises the two RX CW decoders (Classic fldigi + DeepFist neural) when the
// operator selects the "Auto" engine.  It is deliberately engine-agnostic: it
// sees only committed characters tagged by source and a scalar fade confidence
// (DeepFist's keyingRatio), and hands DISPLAY OWNERSHIP to exactly one engine at
// a time.  DeepFist is preferred (cleaner copy); Classic is the fade safety net.
//
// Only the owner's characters are forwarded (onOutput); the muted engine keeps
// running but its output is dropped.  Ownership flips only at a keying GAP (a
// space), so no character is ever cut mid-symbol and there is no de-duplication
// problem by construction.  Asymmetric hysteresis biases toward "never silent":
// fall back fast (nFall), return to DeepFist only when solidly recovered (nRise).
//
// Threading: fed from two threads (Classic on the audio thread, DeepFist on its
// worker thread) — every method takes mx_; onOutput fires OUTSIDE the lock.
#pragma once

#include <functional>
#include <mutex>
#include <string>

namespace lyra::dsp {

class CwArbiter {
public:
    enum class Source { DeepFist, Classic };

    struct Config {
        float tLow  = 12.0f;   // ratio below this => fading (== NeuralCwDecoder kKeyingMin)
        float tHigh = 20.0f;   // ratio above this => solidly recovered
        int   nFall = 1;       // consecutive sub-tLow ticks to fall back (fast)
        int   nRise = 3;       // consecutive above-tHigh ticks to return (slow)
    };

    explicit CwArbiter(Config cfg = {});

    void updateKeying(float ratio);            // fade-confidence feed (~2.5 Hz)
    void pushDeepFist(const std::string& text);
    void pushClassic(const std::string& text);
    void reset();                              // seeds owner = DeepFist

    std::function<void(const std::string& text, bool fallback)> onOutput;

    Source owner() const;

private:
    static bool isGap(const std::string& text); // a space == safe switch point

    Config             cfg_;
    mutable std::mutex mx_;
    Source             owner_      = Source::DeepFist;
    Source             desired_    = Source::DeepFist;
    int                fadeCount_  = 0;
    int                solidCount_ = 0;
};

}  // namespace lyra::dsp

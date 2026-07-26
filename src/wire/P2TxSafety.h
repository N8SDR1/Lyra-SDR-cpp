// Protocol 2 TX safety gate.
//
// This is deliberately a pure evaluator. Every high-priority packet derives
// its transmit, PA, and drive fields from current safety inputs rather than
// from a stale "armed" latch. Missing or unhealthy input fails closed.

#pragma once

#include <cstdint>

namespace lyra::wire {

struct P2TxIntent {
    bool transmitRequested = false;
    bool paRequested = false;
    int  drive = 0;
};

struct P2TxSafetyInputs {
    bool operatorArmed = false;
    bool sessionRunning = false;
    bool iqPrimed = false;
    bool telemetryHealthy = false;
    bool watchdogEnabled = true;
    bool faultLatched = false;
};

struct P2TxEffectiveState {
    bool ready = false;
    bool transmit = false;
    bool paEnabled = false;
    std::uint8_t drive = 0;
};

class P2TxSafetyGate final {
public:
    static P2TxEffectiveState evaluate(const P2TxIntent &intent,
                                       const P2TxSafetyInputs &inputs);
};

} // namespace lyra::wire

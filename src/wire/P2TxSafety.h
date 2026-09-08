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
    // The DUC-IQ writer is actually running RIGHT NOW. iqPrimed is a latched
    // "was primed" flag; a cadence fault can stop the writer without clearing
    // it (e.g. RX-idle re-prime in flight). Sourced live from the writer at
    // each evaluation so the wire can never authorise transmit/PA/drive into
    // a dead transport (empty/garbage keyed carrier). Fails closed.
    bool transportRunning = false;
    bool telemetryHealthy = false;
    bool watchdogEnabled = true;
    bool faultLatched = false;
    // Hard drive ceiling (0..255 byte). The gate clamps the effective
    // drive to this, so the operator drive-limit is enforced structurally
    // here rather than only in the caller that builds the intent. 255 =
    // no additional ceiling (default until a limit is pushed in).
    std::uint8_t driveCeiling = 255;
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

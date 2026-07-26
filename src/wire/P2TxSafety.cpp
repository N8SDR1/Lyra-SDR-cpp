#include "P2TxSafety.h"

#include <algorithm>

namespace lyra::wire {

P2TxEffectiveState P2TxSafetyGate::evaluate(
    const P2TxIntent &intent, const P2TxSafetyInputs &inputs)
{
    P2TxEffectiveState state;
    state.ready = inputs.operatorArmed &&
                  inputs.sessionRunning &&
                  inputs.iqPrimed &&
                  inputs.telemetryHealthy &&
                  inputs.watchdogEnabled &&
                  !inputs.faultLatched;
    if (!state.ready || !intent.transmitRequested)
        return state;

    state.transmit = true;
    state.paEnabled = intent.paRequested;
    state.drive = state.paEnabled
        ? static_cast<std::uint8_t>(std::clamp(intent.drive, 0, 255))
        : std::uint8_t{0};
    return state;
}

} // namespace lyra::wire

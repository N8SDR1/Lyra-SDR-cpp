#pragma once

#include <QtGlobal>

namespace lyra::wire {

// Protocol-2 DDC0 RF (before xvtr / freq-cal) matching P1
// HL2Stream::pushEffectiveRxFreq hardware write:
//   CTUN on  (ctuneCenterHz != 0) → DDC locked at the pan centre.
//                                 RIT lives in the WDSP RXA shift
//                                 (dial(+RIT) − centre), not the NCO.
//   CTUN off                      → DDC = dial + RIT (clamped ≥ 0).
// DUC / TX is a separate path and always tracks the heard/LED VFO
// (txFreqHz + CW/TUN offset), never the frozen pan centre.
inline quint32 p2Ddc0RfHz(quint32 dialHz, bool ritEnabled, int ritOffsetHz,
                          quint32 ctuneCenterHz)
{
    if (ctuneCenterHz != 0)
        return ctuneCenterHz;
    const qint64 eff = static_cast<qint64>(dialHz)
        + (ritEnabled ? static_cast<qint64>(ritOffsetHz) : 0);
    return (eff < 0) ? 0u : static_cast<quint32>(eff);
}

} // namespace lyra::wire

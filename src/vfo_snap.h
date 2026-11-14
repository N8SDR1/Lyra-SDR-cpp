// Sticky VFO wheel: first notch snaps to the step grid in that
// direction, further notches add the step.  14_228_200 Hz + 500 Hz
// → 14_228_500 (up) or 14_228_000 (down), not +500 onto 14_228_700.
// Digit-place wheels and 1 Hz stay additive.  freqHz is >= 0.

#pragma once

#include <cstdlib>

namespace lyra::ui {

inline int nextWheelHz(int freqHz, int stepHz, int ticks, bool sticky)
{
    constexpr int kMax = 2'147'483'647;
    if (freqHz < 0)
        freqHz = 0;
    if (ticks == 0)
        return freqHz;
    const int step = stepHz > 1 ? stepHz : 1;
    auto clamp = [](long long v) -> int {
        if (v < 0)
            return 0;
        if (v > kMax)
            return kMax;
        return static_cast<int>(v);
    };
    if (!sticky || step <= 1)
        return clamp(static_cast<long long>(freqHz)
                     + static_cast<long long>(ticks) * step);
    const int dir = ticks > 0 ? 1 : -1;
    const int n = std::abs(ticks);
    const int rem = freqHz % step;
    int first;
    if (rem == 0)
        first = freqHz + dir * step;
    else if (dir > 0)
        first = freqHz - rem + step;
    else
        first = freqHz - rem;
    return clamp(static_cast<long long>(first)
                 + static_cast<long long>(dir) * step * (n - 1));
}

} // namespace lyra::ui

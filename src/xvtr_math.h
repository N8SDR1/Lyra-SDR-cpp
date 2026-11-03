// RF (operator dial) → radio IF / DDS for a transverter slot.
#pragma once

#include <cstdint>
#include <limits>

namespace lyra::xvtr {

// Number of named transverter slots.  SINGLE SOURCE OF TRUTH — both
// lyra::ui::XvtrSlots::kCount (Band dock chips, xvtr/N/* QSettings) and
// lyra::oc::OcControl::kXvtrSlots (J16 per-chip masks, oc/xvtrRx|Tx/N)
// alias this so the two can never drift.  Raised 4 -> 8 for multi-band
// transverter stacks (K7MDL: 2 m / 1.25 m / 70 cm / 33 cm / 23 cm).
// Slots beyond the first three default to disabled / empty, exactly as
// slot 4 always has.
inline constexpr int kSlotCount = 8;

inline int ddsHz(std::int64_t rfHz, std::int64_t loHz, std::int64_t errorHz) {
    std::int64_t dds = rfHz - loHz - errorHz;
    if (dds < 0) return 0;
    if (dds > std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();
    return static_cast<int>(dds);
}

} // namespace lyra::xvtr

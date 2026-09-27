// RF (operator dial) → radio IF / DDS for a transverter slot.
#pragma once

#include <cstdint>
#include <limits>

namespace lyra::xvtr {

inline int ddsHz(std::int64_t rfHz, std::int64_t loHz, std::int64_t errorHz) {
    std::int64_t dds = rfHz - loHz - errorHz;
    if (dds < 0) return 0;
    if (dds > std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();
    return static_cast<int>(dds);
}

} // namespace lyra::xvtr

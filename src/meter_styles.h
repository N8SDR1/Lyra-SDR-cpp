// Lyra — meter face style indices (MeterModel::style / txStyle).
//
// The index is persisted (meter/style, meter/txStyle), so the list is
// append-only: a retired face keeps its number forever and keptStyle()
// maps it to the nearest face that is still there.  A new face takes the
// next free number — never a retired one, or an old saved index would
// silently become the new face.
//
// MeterPanel.qml (style list + Loader) and SettingsDialog (style combos)
// list the same indices; keep all three in step with this file.

#pragma once

#include <algorithm>

namespace lyra::ui::meterstyle {

enum Style : int {
    HorizonArc     = 0,
    PlasmaBar      = 1,
    VerticalLadder = 2,
    // 3 Porthole (retired)
    // 4 Fader (retired)
    // 5 Nixie (retired)
    // 6 Numitron (retired)
    Vfd            = 7,
    // 8 Dekatron, 9 Panaplex (retired)
    Drum           = 10,
    Iris           = 11,
    Sonar          = 12,
    // 13 Ingot (retired)
    // 14 Sunburst, 15 Lamps (retired)
    Phosphor       = 16,
    Cluster        = 17,
};

// Highest index in use.  Anything above it (a newer build's face) clamps.
inline constexpr int kStyleMax = Cluster;

// Ladder and Cluster both draw MeterModel::ladderRows.
constexpr bool usesLadderRows(int style)
{
    return style == VerticalLadder || style == Cluster;
}

// A saved index for a retired face becomes the nearest face still there.
constexpr int keptStyle(int s)
{
    switch (s) {
    case 3:                        // Porthole
    case 4:  return Iris;          // Fader — the remaining round face
    case 5:                        // Nixie
    case 6:                        // Numitron
    case 8:                        // Dekatron
    case 9:  return Vfd;           // Panaplex — the remaining tube face
    case 13: return PlasmaBar;     // Ingot — the closest bar face
    case 14:                       // Sunburst
    case 15: return Phosphor;      // Lamps
    default: return std::clamp(s, 0, kStyleMax);
    }
}

} // namespace lyra::ui::meterstyle

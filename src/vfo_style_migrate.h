// Lyra — saved VFO-style id migration (Prefs ui/vfo_style).
//
// A style that was removed keeps its id in the operator's settings.  Map it
// to its successor on load so the right-click "VFO style" menu shows a
// checkmark.  Unknown ids pass through unchanged: VfoStyles.resolve() falls
// back to the default style for them.  The mapping is applied in memory
// only; the stored value keeps the old id until the operator picks a
// different style.

#pragma once

#include <QString>

namespace lyra::ui {

inline QString migrateVfoStyleId(const QString &id)
{
    // Aluminum was removed; Lyra (Mac) is the closest successor.
    if (id == QLatin1String("aluminum"))
        return QStringLiteral("lyraMac");
    return id;
}

} // namespace lyra::ui

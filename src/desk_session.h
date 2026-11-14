#pragma once

// Desk-originated filter and rack applies update the live shack chain
// and skip QSettings. The shack's saved profile stays as it was.

namespace lyra {

inline bool &deskApplyQuietFlag()
{
    static bool quiet = false;
    return quiet;
}

inline void setDeskApplyQuiet(bool on) { deskApplyQuietFlag() = on; }
inline bool deskApplyQuiet() { return deskApplyQuietFlag(); }

}  // namespace lyra

// Host PureSignal uses the existing wdspcalls.h pscc / SetPS* table.
// Do not add SetTXAiqc* — deskHPSDR never calls them; WDSP 2.0 dropped
// the public iqc wrappers. iqc runs inside the TXA chain from pscc.

#pragma once

namespace lyra::ps {
}  // namespace lyra::ps

// Link stubs for golden P2 packet tests. Production Lyra uses P2TxCmaster.cpp
// and the real PureSignal feed thread.
#include "ps/PsCalcThread.h"
#include "wire/P2TxCmaster.h"

namespace lyra::ps {

PsCalcThread &PsCalcThread::instance() {
    static PsCalcThread g;
    return g;
}

void PsCalcThread::setRun(bool) {}

void PsCalcThread::feed(int, const double *, const double *) {}

} // namespace lyra::ps

namespace lyra::wire {

double p2TxCmasterLastPeak() { return 0.0; }

void p2TxCmasterResetPeak() {}

void setP2TxCmasterChannelRunning(bool) {}

void reassertP2TxCmasterProducer() {}

} // namespace lyra::wire

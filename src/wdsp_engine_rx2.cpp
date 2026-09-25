// Second receiver (WDSP channel 2 = xrouter source 2 / DDC1).
// Channel 1 is reserved for PureSignal (DDC2/DDC3). Do not steal it.
//
// openRx2/closeRx2 must not take channelMtx_ — setSampleRate already
// holds it and reopens RX2 when SUB is wanted.

#include "wdsp_engine.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include <QSettings>
#include <QtGlobal>

namespace lyra {
namespace dsp {

namespace {

int modeToWdsp(const QString &m)
{
    if (m == QLatin1String("LSB"))  return 0;
    if (m == QLatin1String("USB"))  return 1;
    if (m == QLatin1String("DSB"))  return 2;
    if (m == QLatin1String("CWL"))  return 3;
    if (m == QLatin1String("CWU"))  return 4;
    if (m == QLatin1String("FM"))   return 5;
    if (m == QLatin1String("AM"))   return 6;
    if (m == QLatin1String("DIGU")) return 7;
    if (m == QLatin1String("DIGL")) return 9;
    if (m == QLatin1String("SAM"))  return 10;
    return 1;
}

} // namespace

bool WdspEngine::openRx2()
{
    if (rx2Opened_)
        return true;
    if (!opened_ || !wdsp_ || !wdsp_->isLoaded())
        return false;

    const WdspApi &api = wdsp_->api();
    if (!api.OpenChannel || !api.SetChannelState || !api.SetRXAMode ||
        !api.RXASetPassband || !api.SetRXAAGCMode ||
        !api.SetRXAPanelBinaural) {
        emitLog(QStringLiteral(
            "[wdsp] engine: cannot open RX2 — required symbols not resolved"));
        return false;
    }

    api.OpenChannel(rx2Channel_, cfg_.inSize, cfg_.dspSize,
                    cfg_.inRate, cfg_.dspRate, cfg_.outRate,
                    0, 0,
                    cfg_.tDelayUp, cfg_.tSlewUp,
                    cfg_.tDelayDown, cfg_.tSlewDown,
                    cfg_.block);

    {
        std::lock_guard<std::mutex> lk(rx2Mtx_);
        rx2Opened_ = true;
        rx2OutBuf_.assign(static_cast<size_t>(2 * outSize_), 0.0);
        rx2Accum_.clear();
        rx2NbBuf_.assign(static_cast<size_t>(2 * cfg_.inSize), 0.0);
    }

    if (api.SetRXAPanelBinaural)
        api.SetRXAPanelBinaural(rx2Channel_, 0);

    applyModeFilterRx2();
    applyDspFilterTypes();
    pushAgcMode();
    pushAgcThresh();
    if (api.SetRXAPanelGain1)
        api.SetRXAPanelGain1(rx2Channel_, std::pow(10.0, afGainDb_ / 20.0));
    pushNrState();
    pushAnfState();
    pushLmsState();
    pushSquelchState();

    if (api.create_nobEXT && !nbCreatedRx2_) {
        api.create_nobEXT(rx2Channel_, 0, 0, cfg_.inSize,
                          static_cast<double>(cfg_.inRate),
                          0.0001, 0.0001, 0.0001, 0.020, 20.0);
        nbCreatedRx2_ = true;
    }
    pushNbState();
    pushApfState();

    api.SetChannelState(rx2Channel_, 1, 0);
    haveRx2_.store(true, std::memory_order_release);
    subMixActive_.store(true, std::memory_order_release);
    recomputePassbandRx2();
    emitLog(QStringLiteral("[wdsp] channel 2 opened (SUB / RX2)"));
    return true;
}

void WdspEngine::closeRx2()
{
    haveRx2_.store(false, std::memory_order_release);
    subMixActive_.store(false, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lk(rx2Mtx_);
    if (!rx2Opened_)
        return;

    const WdspApi &api = wdsp_->api();
    if (api.SetChannelState)
        api.SetChannelState(rx2Channel_, 0, 1);
    if (nbCreatedRx2_ && api.destroy_nobEXT) {
        api.destroy_nobEXT(rx2Channel_);
        nbCreatedRx2_ = false;
    }
    if (api.CloseChannel)
        api.CloseChannel(rx2Channel_);

    rx2Opened_ = false;
    rx2Accum_.clear();
    rx2OutBuf_.clear();
    rx2NbBuf_.clear();
    emitLog(QStringLiteral("[wdsp] channel 2 closed"));
}

void WdspEngine::setSubEnabled(bool on)
{
    subWanted_ = on;
    std::lock_guard<std::mutex> lk(channelMtx_);
    if (!opened_)
        return;
    if (on)
        openRx2();
    else
        closeRx2();
}

void WdspEngine::feedIqRx2(const double *iq, int nframes)
{
    if (!haveRx2_.load(std::memory_order_acquire) || nframes <= 0)
        return;
    const WdspApi &api = wdsp_->api();
    if (!api.fexchange0)
        return;

    std::lock_guard<std::mutex> lk(rx2Mtx_);
    if (!rx2Opened_ || !running_)
        return;

    rx2Accum_.insert(rx2Accum_.end(), iq,
                     iq + static_cast<size_t>(2 * nframes));
    const size_t blockDoubles = static_cast<size_t>(2 * cfg_.inSize);
    if (rx2OutBuf_.size() < static_cast<size_t>(2 * outSize_))
        rx2OutBuf_.assign(static_cast<size_t>(2 * outSize_), 0.0);

    while (rx2Accum_.size() >= blockDoubles) {
        double *blockPtr = rx2Accum_.data();
        if (nbEnabled_ && nbCreatedRx2_ && api.xnobEXT &&
            rx2NbBuf_.size() >= blockDoubles) {
            api.xnobEXT(rx2Channel_, rx2Accum_.data(), rx2NbBuf_.data());
            blockPtr = rx2NbBuf_.data();
        }
        api.fexchange0(rx2Channel_, blockPtr, rx2OutBuf_.data(), &fexErrRx2_);
        rx2Accum_.erase(rx2Accum_.begin(),
                        rx2Accum_.begin() + static_cast<std::ptrdiff_t>(blockDoubles));
    }
}

void WdspEngine::applyModeFilterRx2()
{
    if (!rx2Opened_)
        return;
    const WdspApi &api = wdsp_->api();
    if (api.SetRXAMode)
        api.SetRXAMode(rx2Channel_, modeToWdsp(modeRx2_));
    double lo = 0.0, hi = 0.0;
    computePassband(modeRx2_, bwRx2_, &lo, &hi);
    if (api.RXASetPassband)
        api.RXASetPassband(rx2Channel_, lo, hi);
}

void WdspEngine::recomputePassbandRx2()
{
    double lo = 0.0, hi = 0.0;
    computePassband(modeRx2_, bwRx2_, &lo, &hi);
    if (lo != passbandLowHzRx2_ || hi != passbandHighHzRx2_) {
        passbandLowHzRx2_  = lo;
        passbandHighHzRx2_ = hi;
        emit passbandRx2Changed();
    }
}

void WdspEngine::setModeRx2(const QString &m)
{
    if (m.isEmpty() || m == modeRx2_)
        return;
    modeRx2_ = m;
    recomputePassbandRx2();
    applyModeFilterRx2();
    applyDspFilterTypes();
    pushSquelchState();
    pushApfState();
    emit modeRx2Changed();
}

void WdspEngine::setBandwidthRx2(int hz)
{
    hz = std::clamp(hz, 10, 20000);
    if (hz == bwRx2_)
        return;
    bwRx2_ = hz;
    recomputePassbandRx2();
    applyModeFilterRx2();
    emit bandwidthRx2Changed();
}

} // namespace dsp
} // namespace lyra

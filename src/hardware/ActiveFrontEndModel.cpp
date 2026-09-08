#include "ActiveFrontEndModel.h"

#include "hl2_stream.h"
#include "wire/P2RxBridge.h"

namespace lyra::hardware {

ActiveFrontEndModel::ActiveFrontEndModel(lyra::ipc::HL2Stream *p1,
                                         lyra::wire::P2RxBridge *p2,
                                         QObject *parent)
    : QObject(parent), p1_(p1), p2_(p2) {
    if (p1_) {
        connect(p1_, &lyra::ipc::HL2Stream::lnaGainChanged,
                this, &ActiveFrontEndModel::changed);
        connect(p1_, &lyra::ipc::HL2Stream::autoLnaChanged,
                this, &ActiveFrontEndModel::changed);
        connect(p1_, &lyra::ipc::HL2Stream::adcOverloadChanged,
                this, &ActiveFrontEndModel::changed);
        connect(p1_, &lyra::ipc::HL2Stream::runningChanged,
                this, &ActiveFrontEndModel::changed);
    }
    if (p2_) {
        connect(p2_, &lyra::wire::P2RxBridge::runningChanged,
                this, &ActiveFrontEndModel::changed);
        connect(p2_, &lyra::wire::P2RxBridge::frontEndChanged,
                this, &ActiveFrontEndModel::changed);
        connect(p2_, &lyra::wire::P2RxBridge::telemetryChanged,
                this, &ActiveFrontEndModel::changed);
    }
}

bool ActiveFrontEndModel::p2Active() const {
    return p2_ && p2_->isRunning();
}

QString ActiveFrontEndModel::label() const {
    return p2Active() ? QStringLiteral("ATT") : QStringLiteral("LNA");
}

int ActiveFrontEndModel::value() const {
    return p2Active() ? p2_->rxAttenuationDb()
                      : (p1_ ? p1_->lnaGainDb() : 0);
}

int ActiveFrontEndModel::minimum() const { return p2Active() ? 0 : -12; }
int ActiveFrontEndModel::maximum() const { return p2Active() ? 31 : 48; }
bool ActiveFrontEndModel::autoAvailable() const { return !p2Active(); }
bool ActiveFrontEndModel::autoEnabled() const {
    return !p2Active() && p1_ && p1_->autoLna();
}
int ActiveFrontEndModel::overloadTier() const {
    return p2Active() ? p2_->adcOverloadTier()
                      : (p1_ ? p1_->adcOverloadTier() : 0);
}
int ActiveFrontEndModel::overloadMask() const {
    return p2Active() ? p2_->adcOverloadMask() : 0;
}
int ActiveFrontEndModel::adcPeak() const {
    if (!p2Active()) return 0;
    return p2_->ddcAdc() == 0 ? p2_->adc1Peak() : p2_->adc2Peak();
}
int ActiveFrontEndModel::adcIndex() const {
    return p2Active() ? p2_->ddcAdc() : 0;
}

void ActiveFrontEndModel::setValue(int value) {
    if (p2Active()) p2_->setRxAttenuationDb(value);
    else if (p1_) p1_->setLnaGainDb(value);
}

void ActiveFrontEndModel::setAutoEnabled(bool on) {
    if (!p2Active() && p1_) p1_->setAutoLna(on);
}

void ActiveFrontEndModel::setAdcIndex(int index) {
    if (p2Active()) p2_->setDdcAdc(index);
}

} // namespace lyra::hardware

// Lyra — OC Control model + emit choke (Stage 1).  See OcControl.h.
//
// Lyra-native port of Thetis Console/HPSDR/Penny.cs UpdateExtCtrl /
// adjustForTXAction / adjustForRX / getGroup (provenance-only; no code
// copied).  Pure + wire-inert; the Stage-2 wire sink lives in HL2Stream.

#include "oc/OcControl.h"

#include "bands.h"   // amateurBands() — single source of the HF band count

namespace lyra::oc {

OcControl::OcControl(QObject *parent)
    : QObject(parent),
      nBands_(static_cast<int>(lyra::amateurBands().size())) {
    rxA_.fill(0, nBands_);
    txA_.fill(0, nBands_);
    rxB_.fill(0, nBands_);
    txB_.fill(0, nBands_);
    rxXvtr_.fill(0, kXvtrSlots);
    txXvtr_.fill(0, kXvtrSlots);
    // Thetis defaults (Penny ctor): every pin action = MOX_TUNE_TWOTONE,
    // every xPA gate off.
    for (int g = 0; g < kGroups; ++g) {
        for (int p = 0; p < kPins; ++p) {
            txPinAction_[g][p] = TxPinAction::MoxTuneTwoTone;
            txPinPa_[g][p]     = false;
            rxPinPa_[g][p]     = false;
        }
    }
}

// ---- per-band masks --------------------------------------------------------

void OcControl::setRxMaskA(int band, quint8 mask) {
    if (band >= 0 && band < nBands_) rxA_[band] = static_cast<quint8>(mask & 0x7F);
}
void OcControl::setTxMaskA(int band, quint8 mask) {
    if (band >= 0 && band < nBands_) txA_[band] = static_cast<quint8>(mask & 0x7F);
}
void OcControl::setRxMaskB(int band, quint8 mask) {
    if (band >= 0 && band < nBands_) rxB_[band] = static_cast<quint8>(mask & 0x7F);
}
void OcControl::setTxMaskB(int band, quint8 mask) {
    if (band >= 0 && band < nBands_) txB_[band] = static_cast<quint8>(mask & 0x7F);
}
quint8 OcControl::rxMaskA(int band) const {
    return (band >= 0 && band < nBands_) ? rxA_[band] : quint8(0);
}
quint8 OcControl::txMaskA(int band) const {
    return (band >= 0 && band < nBands_) ? txA_[band] : quint8(0);
}
quint8 OcControl::rxMaskB(int band) const {
    return (band >= 0 && band < nBands_) ? rxB_[band] : quint8(0);
}
quint8 OcControl::txMaskB(int band) const {
    return (band >= 0 && band < nBands_) ? txB_[band] : quint8(0);
}

void OcControl::setRxMaskXvtr(int slot, quint8 mask) {
    if (slot >= 0 && slot < kXvtrSlots)
        rxXvtr_[slot] = static_cast<quint8>(mask & 0x7F);
}
void OcControl::setTxMaskXvtr(int slot, quint8 mask) {
    if (slot >= 0 && slot < kXvtrSlots)
        txXvtr_[slot] = static_cast<quint8>(mask & 0x7F);
}
quint8 OcControl::rxMaskXvtr(int slot) const {
    return (slot >= 0 && slot < kXvtrSlots) ? rxXvtr_[slot] : quint8(0);
}
quint8 OcControl::txMaskXvtr(int slot) const {
    return (slot >= 0 && slot < kXvtrSlots) ? txXvtr_[slot] : quint8(0);
}

// ---- pin-action / xPA (pin1 is 1-based, matching Thetis) -------------------

void OcControl::setTxPinAction(int group, int pin1, TxPinAction action) {
    if (group < 0 || group >= kGroups || pin1 < 1 || pin1 > kPins) return;
    txPinAction_[group][pin1 - 1] = action;
}
void OcControl::setTxPinPa(int group, int pin1, bool pa) {
    if (group < 0 || group >= kGroups || pin1 < 1 || pin1 > kPins) return;
    txPinPa_[group][pin1 - 1] = pa;
}
void OcControl::setRxPinPa(int group, int pin1, bool pa) {
    if (group < 0 || group >= kGroups || pin1 < 1 || pin1 > kPins) return;
    rxPinPa_[group][pin1 - 1] = pa;
}
TxPinAction OcControl::txPinAction(int group, int pin0) const {
    if (group < 0 || group >= kGroups || pin0 < 0 || pin0 >= kPins)
        return TxPinAction::MoxTuneTwoTone;
    return txPinAction_[group][pin0];
}
bool OcControl::txPinPa(int group, int pin0) const {
    if (group < 0 || group >= kGroups || pin0 < 0 || pin0 >= kPins) return false;
    return txPinPa_[group][pin0];
}
bool OcControl::rxPinPa(int group, int pin0) const {
    if (group < 0 || group >= kGroups || pin0 < 0 || pin0 >= kPins) return false;
    return rxPinPa_[group][pin0];
}

// ---- scalars ---------------------------------------------------------------

void OcControl::setSplitPins(bool on)     { splitPins_ = on; }
void OcControl::setRxABitMask(int mask)   { rxABitMask_ = mask; }
void OcControl::setVfoBTx(bool on)        { vfoBTx_ = on; }
void OcControl::setRx2Enabled(bool on)    { rx2Enabled_ = on; }
void OcControl::setFamily(Family f)       { family_ = f; }
void OcControl::setAllowHotSwitch(bool on){ allowHotSwitch_ = on; }
void OcControl::setEnabled(bool on) {
    enabled_ = on;
    resetChangeGate();   // §7.5.1 — re-arm so the next emit is never swallowed
}

// ---- group lookup ----------------------------------------------------------

int OcControl::groupForBand(int band) const {
    // v1: the amateur-band index space is entirely HF (Thetis group 0).
    // VHF (group 1) + SWL (group 2) are RESERVED until Lyra adds those band
    // enumerations (§7.4); the [3][7] pin-action/xPA rows already exist so
    // no rework is needed when the band axes arrive.
    return (band >= 0 && band < nBands_) ? 0 : -1;
}

// ---- the pure compute (port of UpdateExtCtrl) ------------------------------

quint8 OcControl::compute(int idx, int idxb, bool tx, bool tune,
                          bool twoTone, bool pa) const {
    const bool idxOk  = (idx  >= 0 && idx  < nBands_);
    const bool idxbOk = (idxb >= 0 && idxb < nBands_);

    quint8 bits;
    if (!idxOk || (splitPins_ && !idxbOk)) {
        bits = 0;                                   // out of range -> no pins
    } else if (splitPins_) {
        bits = tx ? static_cast<quint8>((txA_[idx] & rxABitMask_) | txB_[idxb])
                  : static_cast<quint8>((rxA_[idx] & rxABitMask_) | rxB_[idxb]);
    } else if (family_ == Family::HermesLite) {
        // HL2 has two receivers: pick the correct LPF (MI0BOT).  The idxb
        // reads are guarded (Thetis relies on the caller; Lyra falls back to
        // idx when idxb is invalid — safe deviation, same result on-band).
        if (tx) {
            bits = (vfoBTx_ && idxbOk) ? txA_[idxb] : txA_[idx];
        } else {
            bits = (rx2Enabled_ && idxbOk && idxb > idx) ? rxA_[idxb] : rxA_[idx];
        }
    } else {
        if (tx && vfoBTx_ && idxbOk) bits = txA_[idxb];
        else if (tx)                 bits = txA_[idx];
        else                         bits = rxA_[idx];
    }

    bits = tx ? adjustForTxAction(idx, idxb, bits, tx, tune, twoTone, pa)
              : adjustForRx(idx, idxb, bits, pa);
    return static_cast<quint8>(bits & 0x7F);
}

quint8 OcControl::computeXvtr(int slot, bool tx, bool tune, bool twoTone,
                              bool pa) const {
    if (slot < 0 || slot >= kXvtrSlots)
        return 0;
    quint8 bits = tx ? txXvtr_[slot] : rxXvtr_[slot];
    // Pin-action / xPA gates are the HF (group 0) row — same as DeskHPSDR's
    // single OC byte. Band index 0 is only used to pick group 0.
    bits = tx ? adjustForTxAction(0, 0, bits, tx, tune, twoTone, pa)
              : adjustForRx(0, 0, bits, pa);
    return static_cast<quint8>(bits & 0x7F);
}

quint8 OcControl::adjustForTxAction(int bandA, int bandB, quint8 bits,
                                    bool tx, bool tune, bool twoTone,
                                    bool pa) const {
    int mask = 0;
    for (int pin = 0; pin < kPins; ++pin) {
        int group;
        if (splitPins_)
            group = ((1 << pin) & rxABitMask_) ? groupForBand(bandA)
                                               : groupForBand(bandB);
        else
            group = vfoBTx_ ? groupForBand(bandB) : groupForBand(bandA);
        if (group < 0) continue;                     // §7.5.6 — pin forced off

        const TxPinAction action    = txPinAction_[group][pin];
        const bool        considerPa = txPinPa_[group][pin];
        int nBit = 0;
        if (!considerPa || pa) {
            switch (action) {
            case TxPinAction::Mox:            nBit = (tx && !tune && !twoTone) ? 1 : 0; break;
            case TxPinAction::Tune:           nBit = tune ? 1 : 0; break;
            case TxPinAction::TwoTone:        nBit = twoTone ? 1 : 0; break;
            case TxPinAction::MoxTune:        nBit = ((tx || tune) && !twoTone) ? 1 : 0; break;
            case TxPinAction::MoxTwoTone:     nBit = ((tx || twoTone) && !tune) ? 1 : 0; break;
            case TxPinAction::TuneTwoTone:    nBit = (tune || twoTone) ? 1 : 0; break;
            case TxPinAction::MoxTuneTwoTone: nBit = (tx || tune || twoTone) ? 1 : 0; break;
            }
        }
        mask |= nBit << pin;
    }
    return static_cast<quint8>(bits & mask);
}

quint8 OcControl::adjustForRx(int bandA, int bandB, quint8 bits, bool pa) const {
    int mask = 0;
    for (int pin = 0; pin < kPins; ++pin) {
        int group;
        if (splitPins_)
            group = ((1 << pin) & rxABitMask_) ? groupForBand(bandA)
                                               : groupForBand(bandB);
        else
            group = vfoBTx_ ? groupForBand(bandB) : groupForBand(bandA);
        if (group < 0) continue;

        const bool considerPa = rxPinPa_[group][pin];
        const int  nBit       = (!considerPa || pa) ? 1 : 0;
        mask |= nBit << pin;
    }
    return static_cast<quint8>(bits & mask);
}

} // namespace lyra::oc

// Lyra — OC Control model + emit choke (Stage 1).  See OcControl.h.
//
// Lyra-native port of Thetis Console/HPSDR/Penny.cs UpdateExtCtrl /
// adjustForTXAction / adjustForRX / getGroup (provenance-only; no code
// copied).  Pure + wire-inert; the Stage-2 wire sink lives in HL2Stream.

#include "oc/OcControl.h"

#include "bands.h"   // amateurBands() / broadcastBands() — the two row tables

namespace lyra::oc {

namespace {
// Amateur row count, usable from the static helpers.
int amateurRows() { return static_cast<int>(lyra::amateurBands().size()); }
int broadcastRows() { return static_cast<int>(lyra::broadcastBands().size()); }
} // namespace

OcControl::OcControl(QObject *parent)
    : QObject(parent),
      nBands_(amateurRows()),
      nBc_(broadcastRows()) {
    const int rows = nBands_ + nBc_;
    rxA_.fill(0, rows);
    txA_.fill(0, rows);
    rxB_.fill(0, rows);
    txB_.fill(0, rows);
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

// ---- row index space -------------------------------------------------------

int OcControl::bcRow(int bc) const {
    return (bc >= 0 && bc < nBc_) ? nBands_ + bc : -1;
}
bool OcControl::isBcRow(int row) {
    return row >= amateurRows() && row < amateurRows() + broadcastRows();
}
int OcControl::bcIndexOfRow(int row) {
    return isBcRow(row) ? row - amateurRows() : -1;
}

int OcControl::rowForHz(int hz, int xvtrSlot) {
    // Precedence: Xvtr slot -> amateur -> broadcast -> none.  Amateur is
    // asked first so every ham/BC overlap keeps today's ham row (75 m BC
    // inside 80 m, 41 m BC 7.2-7.3 under 40 m, 13 m BC's 21.45 edge on
    // 15 m).  CB / GEN / anything else -> -1 -> compute() emits 0.
    if (xvtrSlot >= 0)
        return -1;
    const int ham = lyra::bandIndexForFreq(hz);
    if (ham >= 0)
        return ham;
    const int bc = lyra::broadcastBandIndexForFreq(hz);
    if (bc >= 0)
        return amateurRows() + bc;
    return -1;
}

quint8 OcControl::n2adrBroadcastSeed(int bc, bool tx) {
    // The N2ADR board has six LPFs, each shared by the amateur bands it
    // covers (bands.cpp n2adrOcPattern): pin 1 = 160 m, 2 = 80 m,
    // 3 = 60+40 m, 4 = 30+20 m, 5 = 17+15 m, 6 = 12+10 m, pin 7 = 3 MHz
    // HPF (RX only).  The seed code carries no explicit cutoffs, so each
    // LPF's usable passband is taken as "up to the top edge of the highest
    // amateur band on that pin".  A broadcast band therefore borrows the
    // mask of the LOWEST amateur band whose upper edge is >= the BC band's
    // upper edge — the nearest LPF that passes the whole band, i.e. the
    // ham band directly above it:
    //
    //   BC band   MHz            ham row  pins (RX / TX)
    //   120 m     2.300- 2.495   80 m     2    / 2    (HPF dropped, <3 MHz)
    //    90 m     3.200- 3.400   80 m     2.7  / 2
    //    75 m     3.900- 4.000   80 m     2.7  / 2    (ham 80 m wins live)
    //    60 m     4.750- 5.060   60 m     3.7  / 3
    //    49 m     5.900- 6.200   40 m     3.7  / 3
    //    41 m     7.200- 7.450   30 m     4.7  / 4    (straddles 40 m top)
    //    31 m     9.400- 9.900   30 m     4.7  / 4
    //    25 m    11.600-12.100   20 m     4.7  / 4
    //    22 m    13.570-13.870   20 m     4.7  / 4
    //    19 m    15.100-15.830   17 m     5.7  / 5
    //    16 m    17.480-17.900   17 m     5.7  / 5
    //    13 m    21.450-21.850   12 m     6.7  / 6    (straddles 15 m top)
    //
    // Pin 7 (3 MHz HPF) is only kept when the BC band sits entirely above
    // 3 MHz — on 120 m it would attenuate the band itself.  Anything whose
    // top edge is below 160 m (LW / MW) has no LPF and seeds empty.
    const auto &bcs = lyra::broadcastBands();
    if (bc < 0 || bc >= static_cast<int>(bcs.size()))
        return 0;
    const lyra::Band &b = bcs[static_cast<size_t>(bc)];
    const auto &hams = lyra::amateurBands();
    if (hams.empty() || b.high < hams.front().low)
        return 0;                                   // below 160 m: no LPF
    int mask = 0;
    for (size_t a = 0; a < hams.size(); ++a) {
        if (hams[a].high >= b.high) {
            mask = lyra::n2adrOcPattern(static_cast<int>(a), tx);
            break;
        }
    }
    constexpr int kHpfPin7 = 0x40;
    constexpr int kHpfCornerHz = 3'000'000;
    if (!tx && b.low < kHpfCornerHz)
        mask &= ~kHpfPin7;
    return static_cast<quint8>(mask & 0x7F);
}

// ---- per-row masks ---------------------------------------------------------

void OcControl::setRxMaskA(int band, quint8 mask) {
    if (band >= 0 && band < nRows()) rxA_[band] = static_cast<quint8>(mask & 0x7F);
}
void OcControl::setTxMaskA(int band, quint8 mask) {
    if (band >= 0 && band < nRows()) txA_[band] = static_cast<quint8>(mask & 0x7F);
}
void OcControl::setRxMaskB(int band, quint8 mask) {
    if (band >= 0 && band < nRows()) rxB_[band] = static_cast<quint8>(mask & 0x7F);
}
void OcControl::setTxMaskB(int band, quint8 mask) {
    if (band >= 0 && band < nRows()) txB_[band] = static_cast<quint8>(mask & 0x7F);
}
quint8 OcControl::rxMaskA(int band) const {
    return (band >= 0 && band < nRows()) ? rxA_[band] : quint8(0);
}
quint8 OcControl::txMaskA(int band) const {
    return (band >= 0 && band < nRows()) ? txA_[band] : quint8(0);
}
quint8 OcControl::rxMaskB(int band) const {
    return (band >= 0 && band < nRows()) ? rxB_[band] : quint8(0);
}
quint8 OcControl::txMaskB(int band) const {
    return (band >= 0 && band < nRows()) ? txB_[band] : quint8(0);
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
    // Amateur rows -> Thetis group 0 (HF).  Broadcast rows -> group 2
    // (SWL), whose [7] pin-action / xPA set has been persisted all along.
    // VHF (group 1) stays RESERVED (§7.4).
    if (band < 0) return -1;
    if (band < nBands_) return 0;
    if (band < nBands_ + nBc_) return 2;
    return -1;
}

int OcControl::rowLowHz(int row) {
    const auto &hams = lyra::amateurBands();
    if (row >= 0 && row < static_cast<int>(hams.size()))
        return hams[static_cast<size_t>(row)].low;
    const int bc = bcIndexOfRow(row);
    if (bc >= 0)
        return lyra::broadcastBands()[static_cast<size_t>(bc)].low;
    return -1;
}

// ---- the pure compute (port of UpdateExtCtrl) ------------------------------

quint8 OcControl::compute(int idx, int idxb, bool tx, bool tune,
                          bool twoTone, bool pa) const {
    const int  rows   = nRows();
    const bool idxOk  = (idx  >= 0 && idx  < rows);
    const bool idxbOk = (idxb >= 0 && idxb < rows);

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
        // "Higher band" is compared by frequency, not row number, because
        // the broadcast rows sit after the amateur rows in the index space.
        if (tx) {
            bits = (vfoBTx_ && idxbOk) ? txA_[idxb] : txA_[idx];
        } else {
            const bool bHigher = idxbOk && rowLowHz(idxb) > rowLowHz(idx);
            bits = (rx2Enabled_ && bHigher) ? rxA_[idxb] : rxA_[idx];
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

// Lyra — OC Control: editable per-band J16 open-collector band-filtering.
//
// Single owner of the OC table + the emit choke.  This is a Lyra-native
// port of the Thetis Penny.UpdateExtCtrl model (per-band RX/TX 7-bit J16
// masks, transmit-pin actions, external-PA gating, split pins) driving the
// generic HPSDR open-collector outputs — the mechanism that band-follows
// external amps / tuners / filter boards (N2ADR on HL2, and ANAN/Brick
// once their protocol paths land).  Design: docs/architecture/
// oc_control_design.md §7 (LOCKED v2).  Provenance-only reference:
// Thetis 2.10.3.13 Console/HPSDR/Penny.cs.
//
// Stage 1 (this file): the data model + the pure family-branched compute()
// + the −1-seeded change gate.  NO wire I/O, NO radio-state reads — those
// are Stage 2 (HL2Stream::setOcOutput + call-site migration).  Everything
// here is Qt-main-thread only: the wire publication (Stage 2) hands the
// 7-bit result to the EP2 writer via the existing atomic; refresh() is
// never called off the Qt-main thread (§7.5.2).

#pragma once

#include <QObject>
#include <QVector>

#include <cstdint>

namespace lyra::oc {

// Transmit-pin action combo — faithful port of Thetis Penny.TXPinActions
// (Console/enums.cs:443).  The 0..6 combo index is preserved so it maps
// directly onto the UI dropdown order + the QSettings value.
enum class TxPinAction : int {
    Mox            = 0,  // TX only (not tune, not two-tone)
    Tune           = 1,  // tune only
    TwoTone        = 2,  // two-tone only
    MoxTune        = 3,  // TX or tune
    MoxTwoTone     = 4,  // TX or two-tone
    TuneTwoTone    = 5,  // tune or two-tone
    MoxTuneTwoTone = 6,  // TX or tune or two-tone  (default)
};

// Which hardware family compute() emulates.  Stage 2 sets this from the
// radio's HPSDRHW (HPSDRHW::HermesLite=6 -> HermesLite; every other P1
// model -> GenericP1).  It is the ONLY branch in compute()'s raw byte
// select (§3.6): HermesLite has the RX2 "pick the higher band" + the
// VFO-B-TX picks; the generic branch does not.
enum class Family { HermesLite, GenericP1 };

// Operator-facing accessory (Settings Filters / BCD). Not the DeskHPSDR
// Alex/Apollo/Charly list — Alex is the P2 hardware profile, not J16.
enum class OcAccessory { Off = 0, N2adr = 1, Custom = 2 };

class OcControl : public QObject {
    Q_OBJECT
public:
    static constexpr int kPins      = 7;   // J16 open-collector pins 1..7
    static constexpr int kGroups    = 3;   // 0=HF (live v1), 1=VHF, 2=SWL (reserved)
    static constexpr int kXvtrSlots = 4;   // matches XvtrSlots::kCount — DeskHPSDR appends named xvtr rows

    explicit OcControl(QObject *parent = nullptr);

    // ---- per-band 7-bit J16 masks (band = amateur-band index 0..nBands-1) ----
    // A = VFO-A path; B = VFO-B / split path (reserved until RX2, §7.3).
    void   setRxMaskA(int band, quint8 mask);
    void   setTxMaskA(int band, quint8 mask);
    void   setRxMaskB(int band, quint8 mask);
    void   setTxMaskB(int band, quint8 mask);
    quint8 rxMaskA(int band) const;
    quint8 txMaskA(int band) const;
    quint8 rxMaskB(int band) const;
    quint8 txMaskB(int band) const;

    // Named Xvtr chips (DeskHPSDR / piHPSDR). Default 0. N2ADR preset does
    // not fill these. Empty = J16 idle while that chip is selected so an
    // HF amp on J16 does not follow the 10 m IF.
    void   setRxMaskXvtr(int slot, quint8 mask);
    void   setTxMaskXvtr(int slot, quint8 mask);
    quint8 rxMaskXvtr(int slot) const;
    quint8 txMaskXvtr(int slot) const;

    // ---- per-(group,pin) transmit action + external-PA gating ----
    // group 0..2 (v1: only 0=HF is reached; 1/2 reserved).  pin1 is 1-based
    // (1..7) to match Thetis's setter API; the model stores pin0 = pin1-1.
    void        setTxPinAction(int group, int pin1, TxPinAction action);
    void        setTxPinPa(int group, int pin1, bool pa);
    void        setRxPinPa(int group, int pin1, bool pa);
    TxPinAction txPinAction(int group, int pin0) const;
    bool        txPinPa(int group, int pin0) const;
    bool        rxPinPa(int group, int pin0) const;

    // ---- scalars ----
    void setSplitPins(bool on);
    void setRxABitMask(int mask);   // 0x0f = 4x3 split, 0x07 = 3x4 split
    void setVfoBTx(bool on);
    void setRx2Enabled(bool on);
    void setFamily(Family f);
    void setAllowHotSwitch(bool on);
    void setEnabled(bool on);       // master gate; re-arms the change gate (§7.5.1)

    bool    enabled() const        { return enabled_; }
    bool    allowHotSwitch() const { return allowHotSwitch_; }
    bool    splitPins() const      { return splitPins_; }
    int     rxABitMask() const     { return rxABitMask_; }
    Family  family() const         { return family_; }
    int     nBands() const         { return nBands_; }

    // ---- the emit choke ----------------------------------------------------
    // Pure port of Penny.UpdateExtCtrl (raw byte select + adjustForTX/RX +
    // per-pin group + PA gating).  NO enable gate, NO change gate, NO I/O —
    // fully unit-testable.  Returns the raw 7-bit J16 pattern.
    quint8 compute(int bandA, int bandB, bool tx, bool tune,
                   bool twoTone, bool pa) const;

    // J16 bits for an Xvtr chip. Uses that chip's RX/TX masks (default 0)
    // and the HF (group 0) pin-action / xPA gates. No analog / REG 11.
    quint8 computeXvtr(int slot, bool tx, bool tune, bool twoTone,
                       bool pa) const;

    // The gated target: enabled ? compute(...) : 0  (Thetis ExtCtrlEnable).
    quint8 targetBits(int bandA, int bandB, bool tx, bool tune,
                      bool twoTone, bool pa) const {
        return enabled_ ? compute(bandA, bandB, tx, tune, twoTone, pa)
                        : static_cast<quint8>(0);
    }

    // Change gate — Thetis m_nOldBits, seeded to −1 so the FIRST emit
    // (including a legitimate "all pins off" = 0) is NOT swallowed and the
    // board is driven out of its power-up state (§7.5.1).
    void resetChangeGate()        { oldBits_ = -1; }
    bool wouldChange(quint8 bits) const { return static_cast<int>(bits) != oldBits_; }
    // Commit the gate; returns true iff the value changed (Stage 2 caller
    // pushes to the wire only on a true return).
    bool takeIfChanged(quint8 bits) {
        if (static_cast<int>(bits) == oldBits_) return false;
        oldBits_ = static_cast<int>(bits);
        return true;
    }

signals:
    // Last emitted bits, for the live "Hardware Pin State" readout (Stage 4).
    void ocBitsChanged(quint8 bits);

private:
    int    groupForBand(int band) const;   // valid HF band -> 0, else -1 (§7.4)
    quint8 adjustForTxAction(int bandA, int bandB, quint8 bits,
                             bool tx, bool tune, bool twoTone, bool pa) const;
    quint8 adjustForRx(int bandA, int bandB, quint8 bits, bool pa) const;

    int             nBands_;
    QVector<quint8> rxA_, txA_, rxB_, txB_;
    QVector<quint8> rxXvtr_, txXvtr_;
    TxPinAction     txPinAction_[kGroups][kPins];
    bool            txPinPa_[kGroups][kPins];
    bool            rxPinPa_[kGroups][kPins];
    bool            splitPins_     = false;
    int             rxABitMask_    = 0x0f;
    bool            vfoBTx_        = false;
    bool            rx2Enabled_    = false;
    bool            enabled_       = false;
    bool            allowHotSwitch_= false;
    Family          family_        = Family::HermesLite;
    int             oldBits_       = -1;   // −1 seed (§7.5.1)
};

} // namespace lyra::oc

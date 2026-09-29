// Lyra — transverter (Xvtr) slots: display RF, radio IF = RF − LO − error.
// Four chips on the Band dock. Hardware NCO/TX writers subtract LO; VFO,
// TCI, memory, panadapter stay in RF.

#pragma once

#include <QObject>
#include <QString>
#include <cstdint>

namespace lyra::ipc { class HL2Stream; }

namespace lyra::ui {

class XvtrSlots : public QObject {
    Q_OBJECT
    Q_PROPERTY(int slotCount READ slotCount CONSTANT)
    Q_PROPERTY(int activeSlot READ activeSlot NOTIFY slotsChanged)
    Q_PROPERTY(int activeSlotRx2 READ activeSlotRx2 NOTIFY slotsChanged)
public:
    static constexpr int kCount = 4;

    explicit XvtrSlots(lyra::ipc::HL2Stream *stream, QObject *parent = nullptr);

    int slotCount() const { return kCount; }
    int activeSlot() const;
    int activeSlotRx2() const;

    Q_INVOKABLE bool    slotEnabled(int i) const;
    Q_INVOKABLE QString slotName(int i) const;
    Q_INVOKABLE double  slotRfLoHz(int i) const;
    Q_INVOKABLE double  slotRfHiHz(int i) const;
    Q_INVOKABLE double  slotLoHz(int i) const;
    Q_INVOKABLE double  slotErrorHz(int i) const;
    Q_INVOKABLE bool    slotDisablePa(int i) const;
    Q_INVOKABLE bool    slotRxOnly(int i) const;
    Q_INVOKABLE double  slotLastHz(int i) const;

    Q_INVOKABLE void setSlot(int i, bool enabled, const QString &name,
                             double rfLoHz, double rfHiHz, double loHz,
                             double errorHz, bool disablePa, bool rxOnly);

    Q_INVOKABLE void tune(int i);
    Q_INVOKABLE void tuneSub(int i);

    // Radio DDS / DUC / DDC Hz for a displayed RF. Identity when no
    // enabled slot contains rfHz. Negative IF is clamped to 0.
    int ddsHz(qint64 rfHz) const;
    bool disablePaForRf(quint32 rfHz) const;
    bool rxOnlyForRf(quint32 rfHz) const;
    Q_INVOKABLE int matchingSlot(qint64 rfHz) const;

signals:
    void slotsChanged();

private:
    struct Slot {
        bool    enabled   = false;
        QString name;
        qint64  rfLoHz    = 0;
        qint64  rfHiHz    = 0;
        qint64  loHz      = 0;
        qint64  errorHz   = 0;
        bool    disablePa = true;
        bool    rxOnly    = false;
        qint64  lastHz    = 0;
    };

    static bool valid(int i) { return i >= 0 && i < kCount; }
    void load();
    void persist(int i) const;
    void applyDefaults();
    qint64 tuneHzFor(int i) const;
    void rememberRx1();

    lyra::ipc::HL2Stream *stream_ = nullptr;
    Slot slots_[kCount];
};

} // namespace lyra::ui

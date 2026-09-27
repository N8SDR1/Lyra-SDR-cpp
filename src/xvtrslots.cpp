// Lyra — transverter slots.  See xvtrslots.h.

#include "xvtrslots.h"

#include "hl2_stream.h"
#include "xvtr_math.h"

#include <QSettings>
#include <algorithm>
#include <limits>

namespace lyra::ui {

namespace {
constexpr auto kPfx = "xvtr/";

qint64 clampHz(double v) {
    if (v < 0.0) return 0;
    if (v > static_cast<double>(std::numeric_limits<qint64>::max()))
        return std::numeric_limits<qint64>::max();
    return static_cast<qint64>(v);
}
} // namespace

XvtrSlots::XvtrSlots(lyra::ipc::HL2Stream *stream, QObject *parent)
    : QObject(parent), stream_(stream) {
    applyDefaults();
    load();
    if (stream_) {
        stream_->setXvtrSlots(this);
        connect(stream_, &lyra::ipc::HL2Stream::rx1FreqChanged,
                this, [this]() { rememberRx1(); emit slotsChanged(); });
        connect(stream_, &lyra::ipc::HL2Stream::rx2FreqChanged,
                this, &XvtrSlots::slotsChanged);
        connect(stream_, &lyra::ipc::HL2Stream::subEnabledChanged,
                this, &XvtrSlots::slotsChanged);
    }
}

void XvtrSlots::applyDefaults() {
    // Common IF-offset transverters: 2 m / 70 cm / 23 cm land near 10 m.
    slots_[0] = {true, QStringLiteral("2m"),
                 144000000, 148000000, 116000000, 0, true, false, 144300000};
    slots_[1] = {true, QStringLiteral("70cm"),
                 420000000, 450000000, 392000000, 0, true, false, 432200000};
    slots_[2] = {true, QStringLiteral("23cm"),
                 1240000000, 1300000000, 1212000000, 0, true, false, 1296000000};
    slots_[3] = {false, QStringLiteral("4"),
                 0, 0, 0, 0, true, false, 0};
}

void XvtrSlots::load() {
    QSettings s;
    for (int i = 0; i < kCount; ++i) {
        const QString p = QString::fromLatin1(kPfx) + QString::number(i) + QLatin1Char('/');
        if (!s.contains(p + QStringLiteral("name")))
            continue;
        slots_[i].enabled   = s.value(p + QStringLiteral("enabled"), slots_[i].enabled).toBool();
        slots_[i].name      = s.value(p + QStringLiteral("name"), slots_[i].name).toString();
        slots_[i].rfLoHz    = s.value(p + QStringLiteral("rfLoHz"), QVariant::fromValue(slots_[i].rfLoHz)).toLongLong();
        slots_[i].rfHiHz    = s.value(p + QStringLiteral("rfHiHz"), QVariant::fromValue(slots_[i].rfHiHz)).toLongLong();
        slots_[i].loHz      = s.value(p + QStringLiteral("loHz"), QVariant::fromValue(slots_[i].loHz)).toLongLong();
        slots_[i].errorHz   = s.value(p + QStringLiteral("errorHz"), QVariant::fromValue(slots_[i].errorHz)).toLongLong();
        slots_[i].disablePa = s.value(p + QStringLiteral("disablePa"), true).toBool();
        slots_[i].rxOnly    = s.value(p + QStringLiteral("rxOnly"), false).toBool();
        slots_[i].lastHz    = s.value(p + QStringLiteral("lastHz"), QVariant::fromValue(slots_[i].lastHz)).toLongLong();
    }
}

void XvtrSlots::persist(int i) const {
    if (!valid(i)) return;
    QSettings s;
    const QString p = QString::fromLatin1(kPfx) + QString::number(i) + QLatin1Char('/');
    s.setValue(p + QStringLiteral("enabled"),   slots_[i].enabled);
    s.setValue(p + QStringLiteral("name"),      slots_[i].name);
    s.setValue(p + QStringLiteral("rfLoHz"),    QVariant::fromValue(slots_[i].rfLoHz));
    s.setValue(p + QStringLiteral("rfHiHz"),    QVariant::fromValue(slots_[i].rfHiHz));
    s.setValue(p + QStringLiteral("loHz"),      QVariant::fromValue(slots_[i].loHz));
    s.setValue(p + QStringLiteral("errorHz"),   QVariant::fromValue(slots_[i].errorHz));
    s.setValue(p + QStringLiteral("disablePa"), slots_[i].disablePa);
    s.setValue(p + QStringLiteral("rxOnly"),    slots_[i].rxOnly);
    s.setValue(p + QStringLiteral("lastHz"),    QVariant::fromValue(slots_[i].lastHz));
}

int XvtrSlots::matchingSlot(qint64 rfHz) const {
    for (int i = 0; i < kCount; ++i) {
        if (!slots_[i].enabled) continue;
        if (slots_[i].rfHiHz <= slots_[i].rfLoHz) continue;
        if (rfHz >= slots_[i].rfLoHz && rfHz <= slots_[i].rfHiHz)
            return i;
    }
    return -1;
}

int XvtrSlots::activeSlot() const {
    if (!stream_) return -1;
    return matchingSlot(static_cast<qint64>(stream_->rx1FreqHz()));
}

int XvtrSlots::activeSlotRx2() const {
    if (!stream_ || !stream_->subEnabled()) return -1;
    return matchingSlot(static_cast<qint64>(stream_->rx2FreqHz()));
}

bool    XvtrSlots::slotEnabled(int i) const   { return valid(i) && slots_[i].enabled; }
QString XvtrSlots::slotName(int i) const      { return valid(i) ? slots_[i].name : QString(); }
double  XvtrSlots::slotRfLoHz(int i) const    { return valid(i) ? double(slots_[i].rfLoHz) : 0.0; }
double  XvtrSlots::slotRfHiHz(int i) const    { return valid(i) ? double(slots_[i].rfHiHz) : 0.0; }
double  XvtrSlots::slotLoHz(int i) const      { return valid(i) ? double(slots_[i].loHz) : 0.0; }
double  XvtrSlots::slotErrorHz(int i) const   { return valid(i) ? double(slots_[i].errorHz) : 0.0; }
bool    XvtrSlots::slotDisablePa(int i) const { return valid(i) && slots_[i].disablePa; }
bool    XvtrSlots::slotRxOnly(int i) const    { return valid(i) && slots_[i].rxOnly; }
double  XvtrSlots::slotLastHz(int i) const    { return valid(i) ? double(slots_[i].lastHz) : 0.0; }

void XvtrSlots::setSlot(int i, bool enabled, const QString &name,
                        double rfLoHz, double rfHiHz, double loHz,
                        double errorHz, bool disablePa, bool rxOnly) {
    if (!valid(i)) return;
    Slot &sl = slots_[i];
    sl.enabled   = enabled;
    sl.name      = name.left(12);
    sl.rfLoHz    = clampHz(rfLoHz);
    sl.rfHiHz    = clampHz(rfHiHz);
    if (sl.rfHiHz < sl.rfLoHz)
        std::swap(sl.rfHiHz, sl.rfLoHz);
    sl.loHz      = clampHz(loHz);
    sl.errorHz   = static_cast<qint64>(errorHz);
    sl.disablePa = disablePa;
    sl.rxOnly    = rxOnly;
    if (sl.lastHz < sl.rfLoHz || sl.lastHz > sl.rfHiHz) {
        sl.lastHz = (sl.rfLoHz + sl.rfHiHz) / 2;
    }
    persist(i);
    emit slotsChanged();
}

qint64 XvtrSlots::tuneHzFor(int i) const {
    const Slot &sl = slots_[i];
    if (sl.lastHz >= sl.rfLoHz && sl.lastHz <= sl.rfHiHz)
        return sl.lastHz;
    if (sl.rfHiHz > sl.rfLoHz)
        return (sl.rfLoHz + sl.rfHiHz) / 2;
    return sl.rfLoHz;
}

void XvtrSlots::tune(int i) {
    if (!valid(i) || !slots_[i].enabled || !stream_) return;
    const quint32 hz = static_cast<quint32>(
        std::clamp(tuneHzFor(i), qint64(0), qint64(std::numeric_limits<quint32>::max())));
    slots_[i].lastHz = hz;
    persist(i);
    stream_->setRx1FreqHz(hz);
    emit slotsChanged();
}

void XvtrSlots::tuneSub(int i) {
    if (!valid(i) || !slots_[i].enabled || !stream_) return;
    const quint32 hz = static_cast<quint32>(
        std::clamp(tuneHzFor(i), qint64(0), qint64(std::numeric_limits<quint32>::max())));
    slots_[i].lastHz = hz;
    persist(i);
    stream_->setSubEnabled(true);
    stream_->setRx2FreqHz(hz);
    emit slotsChanged();
}

int XvtrSlots::ddsHz(qint64 rfHz) const {
    const int i = matchingSlot(rfHz);
    if (i < 0) {
        if (rfHz < 0) return 0;
        if (rfHz > std::numeric_limits<int>::max())
            return std::numeric_limits<int>::max();
        return static_cast<int>(rfHz);
    }
    return lyra::xvtr::ddsHz(rfHz, slots_[i].loHz, slots_[i].errorHz);
}

bool XvtrSlots::disablePaForRf(quint32 rfHz) const {
    const int i = matchingSlot(static_cast<qint64>(rfHz));
    return i >= 0 && slots_[i].disablePa;
}

void XvtrSlots::rememberRx1() {
    if (!stream_) return;
    const qint64 hz = static_cast<qint64>(stream_->rx1FreqHz());
    const int i = matchingSlot(hz);
    if (i < 0) return;
    if (slots_[i].lastHz == hz) return;
    slots_[i].lastHz = hz;
    persist(i);
}

bool XvtrSlots::rxOnlyForRf(quint32 rfHz) const {
    const int i = matchingSlot(static_cast<qint64>(rfHz));
    return i >= 0 && slots_[i].rxOnly;
}

} // namespace lyra::ui

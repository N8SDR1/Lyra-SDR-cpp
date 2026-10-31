// Lyra — GEN general-coverage slots.  See genslots.h.

#include "genslots.h"

#include "hl2_stream.h"
#include "prefs.h"

#include <QSettings>

#include <algorithm>

namespace lyra::ui {

namespace {
constexpr auto kPfx = "gen/";   // gen/<n>/freq|mode|label|rxPort|lnaDb|autoLna
struct Default { int freq; const char *mode; };
// GEN1 WWV 10 MHz, GEN2 WWV 15 MHz, GEN3 1 MHz MW — all AM (old Lyra).
const Default kDefaults[4] = {
    {0, ""}, {10000000, "AM"}, {15000000, "AM"}, {1000000, "AM"}};
}

GenSlots::GenSlots(Prefs *prefs, lyra::ipc::HL2Stream *stream, QObject *parent)
    : QObject(parent), prefs_(prefs), stream_(stream) {
    QSettings s;
    for (int n = 1; n <= 3; ++n) {
        const QString p = QString::fromLatin1(kPfx) + QString::number(n) + QLatin1Char('/');
        slots_[n].freq  = s.value(p + QStringLiteral("freq"),
                                  kDefaults[n].freq).toInt();
        slots_[n].mode  = s.value(p + QStringLiteral("mode"),
                                  QString::fromLatin1(kDefaults[n].mode)).toString();
        slots_[n].label = s.value(p + QStringLiteral("label")).toString();
        slots_[n].rxPort = s.value(p + QStringLiteral("rxPort"), 0).toInt();
        if (slots_[n].rxPort != 2)
            slots_[n].rxPort = 0;
        if (s.contains(p + QStringLiteral("lnaDb")))
            slots_[n].lnaDb = s.value(p + QStringLiteral("lnaDb")).toInt();
        if (s.contains(p + QStringLiteral("autoLna")))
            slots_[n].autoLna = s.value(p + QStringLiteral("autoLna")).toBool();
    }
    if (stream_) {
        connect(stream_, &lyra::ipc::HL2Stream::rx1FreqChanged,
                this, &GenSlots::onFreqChanged);
        connect(stream_, &lyra::ipc::HL2Stream::hl2RxPortChanged,
                this, &GenSlots::onHl2RxPortChanged);
        connect(stream_, &lyra::ipc::HL2Stream::lnaSetByOperator,
                this, &GenSlots::onLnaSetByOperator);
        connect(stream_, &lyra::ipc::HL2Stream::autoLnaChanged,
                this, &GenSlots::onAutoLnaChanged);
    }
    if (prefs_)
        connect(prefs_, &Prefs::modeChanged, this, &GenSlots::onModeChanged);
}

double GenSlots::slotFreq(int n) const  { return valid(n) ? double(slots_[n].freq) : 0.0; }
QString GenSlots::slotMode(int n) const { return valid(n) ? slots_[n].mode  : QString(); }
QString GenSlots::slotLabel(int n) const{ return valid(n) ? slots_[n].label : QString(); }

void GenSlots::recall(int n) {
    if (!valid(n)) return;
    active_ = n;
    // applying_ spans the whole recall: the freq write below may land
    // inside a ham/BC/CB band, in which case BandMemory::applyBand pushes
    // THAT band's RF1/Alt + LNA synchronously from rx1FreqChanged — the
    // note handlers must not record those into this slot.
    applying_ = true;
    // Freq first, then mode (see TimeStations::tune) — so leaving the
    // current band doesn't clobber its saved mode with this slot's mode.
    if (stream_) stream_->setRx1FreqHz(quint32(slots_[n].freq));
    if (prefs_ && !slots_[n].mode.isEmpty()) prefs_->setMode(slots_[n].mode);
    // Last, so the slot's stored RF1/Alt + LNA win over anything
    // BandMemory applied in response to the freq write (#24).
    applyStoredGain(n);
    applying_ = false;
    emit activeChanged();
}

void GenSlots::applyStoredGain(int n) {
    if (!valid(n) || !stream_) return;
    // Auto before LNA (as BandMemory / XvtrSlots do) so an Auto-off
    // restore does not immediately walk the stored number.  Unlike Xvtr
    // chips, an unset Auto / LNA leaves the live state alone: GEN slots
    // are HF, so there is no "HF Auto must not follow you" reason to
    // force Auto off on a slot the operator has never adjusted.
    if (slots_[n].autoLna.has_value()) {
        const bool autoOn = *slots_[n].autoLna;
        stream_->setAutoLna(autoOn, /*armAcquire=*/autoOn);
    }
    if (slots_[n].lnaDb.has_value()) {
        stream_->setLnaGainDb(std::clamp(*slots_[n].lnaDb,
                                         lyra::ipc::HL2Stream::kLnaMinDb,
                                         lyra::ipc::HL2Stream::kLnaMaxDb));
    }
    // Unset = RF1, same default as BandMemory / XvtrSlots.  REG 11 on the
    // wire stays 0 or 2 only — setHl2RxPort clamps.
    stream_->setHl2RxPort(slots_[n].rxPort == 2 ? 2 : 0);
}

void GenSlots::deactivate() {
    if (active_ != 0) { active_ = 0; emit activeChanged(); }
}

void GenSlots::onFreqChanged() {
    if (active_ == 0 || !stream_) return;
    slots_[active_].freq = int(stream_->rx1FreqHz());   // remember where you are
    persist(active_);
}

void GenSlots::onModeChanged() {
    if (active_ == 0 || !prefs_) return;
    slots_[active_].mode = prefs_->mode();
    persist(active_);
}

// While a GEN slot is active it owns the VFO — tuning around updates the
// slot's freq (onFreqChanged), so a live RF1/Alt or LNA change belongs to
// the slot too.  Leaving via a band / TIME / Mem / Xvtr button calls
// deactivate() first, so those never land here.
void GenSlots::onHl2RxPortChanged() {
    if (applying_ || active_ == 0 || !stream_) return;
    const int p = stream_->hl2RxPort() == 2 ? 2 : 0;
    if (slots_[active_].rxPort == p) return;
    slots_[active_].rxPort = p;
    persist(active_);
}

void GenSlots::onLnaSetByOperator(int db) {
    if (applying_ || active_ == 0 || !stream_) return;
    db = std::clamp(db, lyra::ipc::HL2Stream::kLnaMinDb,
                    lyra::ipc::HL2Stream::kLnaMaxDb);
    if (slots_[active_].lnaDb == db) return;
    slots_[active_].lnaDb = db;
    persist(active_);
}

void GenSlots::onAutoLnaChanged() {
    if (applying_ || active_ == 0 || !stream_) return;
    const bool on = stream_->autoLna();
    if (slots_[active_].autoLna == on) return;
    slots_[active_].autoLna = on;
    persist(active_);
}

void GenSlots::resetSlot(int n) {
    if (!valid(n)) return;
    slots_[n].freq  = kDefaults[n].freq;
    slots_[n].mode  = QString::fromLatin1(kDefaults[n].mode);
    slots_[n].label.clear();
    slots_[n].rxPort = 0;
    slots_[n].lnaDb.reset();
    slots_[n].autoLna.reset();
    {
        QSettings s;
        const QString p = QString::fromLatin1(kPfx) + QString::number(n) + QLatin1Char('/');
        s.remove(p + QStringLiteral("lnaDb"));
        s.remove(p + QStringLiteral("autoLna"));
    }
    persist(n);
    emit slotChanged(n);
    if (active_ == n) recall(n);   // re-tune to the restored default
}

void GenSlots::setLabel(int n, const QString &label) {
    if (!valid(n)) return;
    slots_[n].label = label.left(30);
    persist(n);
    emit slotChanged(n);
}

void GenSlots::persist(int n) const {
    QSettings s;
    const QString p = QString::fromLatin1(kPfx) + QString::number(n) + QLatin1Char('/');
    s.setValue(p + QStringLiteral("freq"),  slots_[n].freq);
    s.setValue(p + QStringLiteral("mode"),  slots_[n].mode);
    s.setValue(p + QStringLiteral("label"), slots_[n].label);
    s.setValue(p + QStringLiteral("rxPort"), slots_[n].rxPort == 2 ? 2 : 0);
    if (slots_[n].lnaDb.has_value())
        s.setValue(p + QStringLiteral("lnaDb"), *slots_[n].lnaDb);
    if (slots_[n].autoLna.has_value())
        s.setValue(p + QStringLiteral("autoLna"), *slots_[n].autoLna);
}

} // namespace lyra::ui

// Lyra — GEN general-coverage slots.  See genslots.h.

#include "genslots.h"

#include "hl2_stream.h"
#include "prefs.h"

#include <QSettings>

namespace lyra::ui {

namespace {
constexpr auto kPfx = "gen/";   // gen/<n>/freq|mode|label
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
    }
    if (stream_)
        connect(stream_, &lyra::ipc::HL2Stream::rx1FreqChanged,
                this, &GenSlots::onFreqChanged);
    if (prefs_)
        connect(prefs_, &Prefs::modeChanged, this, &GenSlots::onModeChanged);
}

double GenSlots::slotFreq(int n) const  { return valid(n) ? double(slots_[n].freq) : 0.0; }
QString GenSlots::slotMode(int n) const { return valid(n) ? slots_[n].mode  : QString(); }
QString GenSlots::slotLabel(int n) const{ return valid(n) ? slots_[n].label : QString(); }

void GenSlots::recall(int n) {
    if (!valid(n)) return;
    if (stream_ && stream_->txBlocksBandJump(quint32(slots_[n].freq))) return;
    active_ = n;
    // Freq first, then mode (see TimeStations::tune) — so leaving the
    // current band doesn't clobber its saved mode with this slot's mode.
    if (stream_) stream_->setRx1FreqHz(quint32(slots_[n].freq));
    if (prefs_ && !slots_[n].mode.isEmpty()) prefs_->setMode(slots_[n].mode);
    emit activeChanged();
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

void GenSlots::resetSlot(int n) {
    if (!valid(n)) return;
    slots_[n].freq  = kDefaults[n].freq;
    slots_[n].mode  = QString::fromLatin1(kDefaults[n].mode);
    slots_[n].label.clear();
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
}

} // namespace lyra::ui

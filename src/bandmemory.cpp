// Lyra — per-band settings memory.  See bandmemory.h.

#include "bandmemory.h"

#include "bands.h"
#include "hl2_stream.h"
#include "prefs.h"
#include "rig/RigScope.h"   // multi-rig Stage 4 — per-rig key routing (band_mem/)

#include <QSettings>

namespace lyra::ui {

namespace {
// Per-rig scoped prefix: rig/<activeId>/band_mem/ (or legacy flat
// "band_mem/" when no rig is active).  Keys: band_mem/<band>/<field>.
QString bandMemPfx() {
    return lyra::rig::scope::rigKey(QStringLiteral("band_mem/"));
}
}

BandMemory::BandMemory(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                       QObject *parent)
    : QObject(parent), prefs_(prefs), stream_(stream) {
    if (stream_) {
        connect(stream_, &lyra::ipc::HL2Stream::rx1FreqChanged,
                this, &BandMemory::onFreqChanged);
        // Save the operator's MANUAL LNA set point to the current band.
        // Fires only on manual sets (not Auto-LNA roaming); applying_
        // guards out the set we make while restoring a band.
        connect(stream_, &lyra::ipc::HL2Stream::lnaSetByOperator, this,
                [this](int db) {
                    if (applying_ || currentBand_.isEmpty()) return;
                    QSettings().setValue(
                        bandMemPfx() + currentBand_ +
                        QStringLiteral("/lna"), db);
                });
        // Task #27 — per-band TX drive memory.  Save the current band's
        // drive whenever the operator changes it (UI tx panel) so a
        // future return to this band restores their per-band setting.
        // applying_ guards out the change WE make while restoring.  No
        // lnaSetByOperator-equivalent distinction is needed here because
        // there's no Auto-drive that roams; every set is the operator.
        connect(stream_, &lyra::ipc::HL2Stream::txDriveLevelChanged, this,
                [this](int level) {
                    if (applying_ || currentBand_.isEmpty()) return;
                    QSettings().setValue(
                        bandMemPfx() + currentBand_ +
                        QStringLiteral("/txDrive"), level);
                });
    }
    if (prefs_) {
        // Live-save the current band whenever a remembered setting changes
        // (so values are kept even if the operator never leaves the band).
        // applying_ guards out the changes WE make while restoring a band.
        auto live = [this]() { saveCurrent(); };
        connect(prefs_, &Prefs::modeChanged,           this, live);
        connect(prefs_, &Prefs::dbMinChanged,          this, live);
        connect(prefs_, &Prefs::dbMaxChanged,          this, live);
        connect(prefs_, &Prefs::waterfallDbMinChanged, this, live);
        connect(prefs_, &Prefs::waterfallDbMaxChanged, this, live);
        // Task #74 follow-up — per-band TUN drive memory.  Same pattern
        // as txDriveLevel (live-save on change, restore on band entry):
        // tune-into-amp-at-200W on 80m differs from tune-into-tuner-at-
        // 5W on 10m, so each band gets its own remembered value.
        // Focused lambda writes only the tuneDrive key (the saveCurrent
        // path would re-write the 4 panadapter keys unnecessarily on
        // every drive nudge).
        connect(prefs_, &Prefs::tuneDrivePctChanged, this,
                [this]() {
                    if (applying_ || currentBand_.isEmpty()) return;
                    QSettings().setValue(
                        bandMemPfx() + currentBand_ +
                        QStringLiteral("/tuneDrive"),
                        prefs_->tuneDrivePct());
                });
    }
    // Apply the current band's saved settings ONCE at startup.  The
    // stream restores its RX1 frequency in its own ctor via an atomic
    // store that does NOT emit rx1FreqChanged, so onFreqChanged never
    // fires for the initial frequency.  Without this, each band's saved
    // mode / panadapter dB / waterfall dB only restore after the first
    // band-edge crossing — so on restart they appeared not to persist.
    if (stream_) {
        currentBand_ = bandNameFor(int(stream_->rx1FreqHz()));
        if (!currentBand_.isEmpty())
            applyBand(currentBand_);
    }
}

QString BandMemory::bandNameFor(int hz) {
    // Amateur first (priority on overlaps), then broadcast, then CB — each
    // with a unique memory-key prefix so e.g. ham "60m" and bc "60m" don't
    // collide.
    const int a = lyra::bandIndexForFreq(hz);
    if (a >= 0) return QString::fromLatin1(lyra::amateurBands()[std::size_t(a)].name);
    const int b = lyra::broadcastBandIndexForFreq(hz);
    if (b >= 0)
        return QStringLiteral("bc_")
               + QString::fromLatin1(lyra::broadcastBands()[std::size_t(b)].name);
    const int c = lyra::cbBandIndexForFreq(hz);
    if (c >= 0)
        return QStringLiteral("cb_")
               + QString::fromLatin1(lyra::cbBands()[std::size_t(c)].name);
    return QString();
}

QString BandMemory::defaultModeFor(const QString &band) {
    if (band.startsWith(QLatin1String("bc_"))) {
        const QString n = band.mid(3);
        for (const auto &b : lyra::broadcastBands())
            if (n == QString::fromLatin1(b.name)) return QString::fromLatin1(b.mode);
    } else if (band.startsWith(QLatin1String("cb_"))) {
        for (const auto &b : lyra::cbBands()) return QString::fromLatin1(b.mode);
    } else {
        for (const auto &b : lyra::amateurBands())
            if (band == QString::fromLatin1(b.name)) return QString::fromLatin1(b.mode);
    }
    return QString();
}

void BandMemory::onFreqChanged() {
    if (applying_ || !stream_) return;
    const int hz = int(stream_->rx1FreqHz());
    const QString name = bandNameFor(hz);
    if (name != currentBand_) {           // crossed a band edge
        currentBand_ = name;
        if (!name.isEmpty()) applyBand(name);   // restore mode/dB ranges
    }
    // Remember this band's last frequency so the Band buttons can return
    // to where you were (see freqFor()).
    if (!name.isEmpty())
        QSettings().setValue(bandMemPfx() + name +
                             QStringLiteral("/freq"), hz);
}

int BandMemory::freqFor(const QString &band) const {
    return QSettings().value(bandMemPfx() + band +
                             QStringLiteral("/freq"), 0).toInt();
}

void BandMemory::saveCurrent() {
    if (applying_ || !prefs_ || currentBand_.isEmpty()) return;
    QSettings s;
    const QString p = bandMemPfx() + currentBand_ + QLatin1Char('/');
    s.setValue(p + QStringLiteral("mode"),  prefs_->mode());
    // Task #44 Phase 1 — always persist the RX pair as the band's
    // panadapter range.  Prefs::dbMin/dbMax return the LIVE pair
    // (TX while moxActive); reading rxDbMin/rxDbMax directly keeps
    // a TX-side drag from polluting the band's RX scale memory.
    s.setValue(p + QStringLiteral("dbMin"), prefs_->rxDbMin());
    s.setValue(p + QStringLiteral("dbMax"), prefs_->rxDbMax());
    s.setValue(p + QStringLiteral("wfMin"), prefs_->waterfallDbMin());
    s.setValue(p + QStringLiteral("wfMax"), prefs_->waterfallDbMax());
}

void BandMemory::applyBand(const QString &band) {
    if (!prefs_) return;
    // Arm Auto-LNA's fast acquire.  The reference sets `_band_change` in
    // its band-select handler (console.cs:43776) so the auto-attenuator
    // climbs quickly to the new band's overload edge instead of waiting
    // out a hold interval per dB.
    if (stream_) stream_->noteBandChange();
    QSettings s;
    const QString p = bandMemPfx() + band + QLatin1Char('/');
    // Per-band manual LNA — restored independently of the mode/dB block
    // below (a band may have an LNA set point but no stored mode).  The
    // applying_ guard keeps the resulting lnaSetByOperator from re-saving.
    if (stream_ && s.contains(p + QStringLiteral("lna"))) {
        applying_ = true;
        stream_->setLnaGainDb(s.value(p + QStringLiteral("lna")).toInt());
        applying_ = false;
    }
    // Task #27 — per-band TX drive memory + fail-safe.
    //   Stored value present  → restore it (operator's per-band choice).
    //   Stored value absent   → FORCE drive to 0 (never carry a previous
    //                           band's setting into a band the operator
    //                           hasn't yet validated — accidental full-
    //                           blast keying is the explicit failure case
    //                           this task addresses).
    // Either way we go through setTxDriveLevel under the applying_ guard
    // so the save callback above doesn't re-write what we just restored.
    if (stream_) {
        applying_ = true;
        const int restored = s.contains(p + QStringLiteral("txDrive"))
            ? s.value(p + QStringLiteral("txDrive")).toInt()
            : 0;
        stream_->setTxDriveLevel(restored);
        applying_ = false;
    }
    // Task #74 follow-up — per-band TUN drive restore.  Stored value
    // present → restore (the operator's per-band-tuned amp / tuner
    // setting).  Stored value absent → fall back to the global Prefs
    // default (currently 25 %) rather than the txDrive=0 fail-safe;
    // tuneDrive only takes effect when the operator opts into separate
    // tune drive AND presses TUN, both deliberate gestures, so the
    // unattended-keying failure mode the txDrive=0 fail-safe addresses
    // doesn't apply here.  applying_ guards the live-save callback.
    if (prefs_) {
        applying_ = true;
        const int restoredTune = s.contains(p + QStringLiteral("tuneDrive"))
            ? s.value(p + QStringLiteral("tuneDrive")).toInt()
            : prefs_->tuneDrivePct();
        prefs_->setTuneDrivePct(restoredTune);
        applying_ = false;
    }
    if (!s.contains(p + QStringLiteral("mode"))) {
        // Never configured: apply the band's default mode (band-appropriate
        // — LSB on 40m, AM on broadcast/CB), leaving dB ranges global.
        const QString dm = defaultModeFor(band);
        if (!dm.isEmpty() && prefs_) {
            applying_ = true;
            prefs_->setMode(dm);
            applying_ = false;
        }
        return;
    }
    applying_ = true;                     // don't re-save these as user edits
    QString mode = s.value(p + QStringLiteral("mode")).toString();
    // SSB sideband always follows the band's convention (from the band
    // table — so 160/80/40 = LSB, 60/20…6 = USB) even if a different
    // sideband was last stored.  The operator can still flip it manually
    // while on the band; it just re-defaults per band on the next entry.
    if (mode == QLatin1String("USB") || mode == QLatin1String("LSB")) {
        const QString bandSsb = defaultModeFor(band);
        if (bandSsb == QLatin1String("USB") || bandSsb == QLatin1String("LSB"))
            mode = bandSsb;
    }
    prefs_->setMode(mode);
    // Task #44 Phase 1 — band recall ALWAYS targets the RX pair,
    // even during MOX (the band's persisted range IS the RX scale).
    // setRxDbMin/Max writes the RX backing directly without polluting
    // the live TX pair currently bound to the panadapter.
    prefs_->setRxDbMin(s.value(p + QStringLiteral("dbMin")).toDouble());
    prefs_->setRxDbMax(s.value(p + QStringLiteral("dbMax")).toDouble());
    prefs_->setWaterfallDbMin(s.value(p + QStringLiteral("wfMin")).toDouble());
    prefs_->setWaterfallDbMax(s.value(p + QStringLiteral("wfMax")).toDouble());
    applying_ = false;
}

} // namespace lyra::ui

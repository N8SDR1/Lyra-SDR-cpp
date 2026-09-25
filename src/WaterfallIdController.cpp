// Lyra — #175 TX Waterfall callsign ID orchestrator.  See WaterfallIdController.h.

#include "WaterfallIdController.h"

#include "prefs.h"
#include "hl2_stream.h"
#include "wdsp_engine.h"
#include "bandplan.h"            // amateurBandContains — region-aware ham-band lockout
#include "wire/CMaster.h"        // SetTXTCIAudio / SetTXVacAudio / SetTxRackBypass
#include "tci/TciTxBridge.h"     // clear() the bridge backlog on restore

#include <QTimer>

using lyra::ui::Prefs;
using lyra::ipc::HL2Stream;
using lyra::dsp::WdspEngine;

WaterfallIdController::WaterfallIdController(Prefs *prefs, HL2Stream *stream,
                                            WdspEngine *engine, QObject *parent)
    : QObject(parent), prefs_(prefs), stream_(stream), engine_(engine) {
    cadence_ = new QTimer(this);
    cadence_->setSingleShot(false);
    connect(cadence_, &QTimer::timeout, this, &WaterfallIdController::onCadence);

    // Un-key timer: when the burst audio has played out, drop MOX.  If the
    // burst never actually keyed (e.g. external inhibit blocked it), restore
    // here directly so the flat state can't get stuck on.
    unkey_ = new QTimer(this);
    unkey_->setSingleShot(true);
    connect(unkey_, &QTimer::timeout, this, [this]() {
        if (!burstActive_) return;
        if (stream_->moxActive()) {
            stream_->requestMox(false);   // exitFlat() runs on the mox-false edge
        } else {
            burstActive_ = false;
            exitFlat();
        }
    });

    connect(prefs_, &Prefs::wfIdEnabledChanged,
            this, &WaterfallIdController::onArmedChanged);
    connect(prefs_, &Prefs::wfIdIntervalMinChanged,
            this, &WaterfallIdController::onIntervalChanged);
    connect(stream_, &HL2Stream::moxActiveChanged,
            this, &WaterfallIdController::onMoxChanged);
}

bool WaterfallIdController::ssbOk() const {
    const QString m = engine_->mode().toUpper();
    return m == QLatin1String("USB") || m == QLatin1String("LSB");
}

int WaterfallIdController::wdspTxModeFor(const QString &uiMode) const {
    const QString m = uiMode.toUpper();   // mirror of main.cpp's wdspTxModeFor
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
    return 1;                              // USB default
}

void WaterfallIdController::onArmedChanged() {
    if (prefs_->wfIdEnabled()) {
        fireOnce();                         // send once immediately on arm
        const int mins = prefs_->wfIdIntervalMin();
        if (mins > 0) cadence_->start(mins * 60 * 1000);   // then repeat
        else          cadence_->stop();                    // 0 = once only
    } else {
        cadence_->stop();                   // a live burst still finishes + restores
    }
}

void WaterfallIdController::onIntervalChanged() {
    if (!prefs_->wfIdEnabled()) return;
    const int mins = prefs_->wfIdIntervalMin();
    if (mins > 0) cadence_->start(mins * 60 * 1000);
    else          cadence_->stop();
}

void WaterfallIdController::onCadence() {
    fireOnce();
}

void WaterfallIdController::onMoxChanged(bool on) {
    // Restore once OUR burst's key fully drops (wire MOX clears after the keyup
    // sequence).  Guarded by burstActive_ so a normal operator transmission is
    // never touched.  Fires for ANY drop (normal / safety-timeout / inhibit).
    if (!on && burstActive_) {
        burstActive_ = false;
        unkey_->stop();
        exitFlat();
    }
}

void WaterfallIdController::fireOnce() {
    if (burstActive_) return;               // one burst at a time
    if (!prefs_->wfIdEnabled()) return;
    if (!ssbOk()) return;                   // USB/LSB voice only (digital carries the call already)
    // HAM BANDS ONLY (operator-locked, legal/public-airwaves): a waterfall
    // callsign image is a Part-97 courtesy — illegal anywhere outside an
    // amateur allocation (11m/CB, a SW-broadcast slot at 7.310, etc.).  The
    // check is region-aware (Settings → Hardware band plan).  Hard guard on
    // the live RX1 freq so NO tune source (panadapter/TCI spot/keypad/memory/
    // band) can ever fire an ID out of band, even if the chip is armed.
    if (!lyra::ui::amateurBandContains(prefs_->bandPlanRegion(),
                                       prefs_->bandPlanCountry(),
                                       static_cast<double>(stream_->rx1FreqHz())))
        return;
    if (stream_->moxActive()) return;       // defer while the operator is keyed
    const QString call = prefs_->callsign().trimmed();
    if (call.isEmpty()) return;

    enterFlat();
    // LSB pre-mirrors the raster (enterFlat forced DIGL, which flips audio↔RF).
    const bool lsb = engine_->mode().toUpper() == QLatin1String("LSB");
    const int ms = stream_->pushWaterfallIdAudio(call, prefs_->wfIdLevel(), lsb);
    if (ms <= 0) { exitFlat(); return; }    // nothing rendered → undo the flat switch
    burstActive_ = true;
    stream_->requestMox(true);              // key — the TX pump paints the bridge burst
    unkey_->start(ms + 250);                // burst length + a short keyup tail
}

void WaterfallIdController::enterFlat() {
    // Transient, NON-persisted reproduction of the validated DIGU flat state.
    // Touches only live wire/stream state — never Prefs/QSettings.
    savedLeveler_ = stream_->levelerOn();
    lyra::wire::SetTXTCIAudio(0, 1);        // mic source → the WaterfallId TCI bridge
    lyra::wire::SetTXVacAudio(0, 0);
    lyra::wire::SetTxRackBypass(1);         // whole native rack OFF (EQ / Comp / Plate / Speech)
    const bool lsb = engine_->mode().toUpper() == QLatin1String("LSB");
    stream_->setTxMode(lsb ? 9 : 7);        // DIGL : DIGU — flat WDSP TXA + PHROT auto-off, matched sideband
    if (savedLeveler_) stream_->setLevelerOn(false);   // leveler off → clean raster
}

void WaterfallIdController::exitFlat() {
    // Put everything back from the operator's UNTOUCHED selections.
    // Same TX-source rule as main.cpp applyTxAudioSource: TCI exclusive;
    // VAC TX if micpc OR (auto-digital DIG + live VAC input), never both.
    const QString src = prefs_->micSource();
    const bool tci = (src == QLatin1String("tci"));
    const QString vacIn = engine_->vac1InputDeviceName();
    const bool vacInLive = !vacIn.isEmpty()
        && vacIn != QLatin1String("(none)");
    const bool autoDigVacTx =
        !tci && vacInLive
        && engine_->vac1AutoDigital()
        && engine_->mode().startsWith(QLatin1String("DIG"),
                                      Qt::CaseInsensitive);
    const bool vac = !tci
        && (src == QLatin1String("micpc") || autoDigVacTx);
    lyra::wire::SetTXTCIAudio(0, tci ? 1 : 0);
    lyra::wire::SetTXVacAudio(0, vac ? 1 : 0);
    if (!tci)
        lyra::tci::TciTxBridge::instance().clear();

    const QString m = engine_->mode();
    const QString mu = m.toUpper();
    stream_->setTxMode(wdspTxModeFor(m));
    lyra::wire::SetTxRackBypass(
        (mu == QLatin1String("DIGU") || mu == QLatin1String("DIGL")) ? 1 : 0);
    stream_->setLevelerOn(savedLeveler_);
}

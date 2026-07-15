// Lyra — DX-cluster spot store (TCI spots).
//
// Holds the spots pushed by a TCI client (SDRLogger+, cluster apps):
// each { callsign, mode, freqHz, ARGB colour, text }.  The panadapter
// draws them as colored callsign markers; clicking one tunes there and
// echoes `spot_activated` back to the TCI clients (so the logger can
// log the QSO).  Spots optionally age out after a lifetime, and the
// bank is capped (oldest dropped) to keep the display uncluttered.

#pragma once

#include "dxcc.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

class QTimer;

namespace lyra::ipc { class HL2Stream; }
namespace lyra::dsp { class WdspEngine; }

namespace lyra::ui {

class Prefs;

class SpotStore : public QObject {
    Q_OBJECT
public:
    SpotStore(Prefs *prefs, lyra::ipc::HL2Stream *stream,
              lyra::dsp::WdspEngine *engine, QObject *parent = nullptr);

    // --- spot bus (source-agnostic) ---
    // `source` tags which feeder produced the spot ("tci" | "spothole" |
    // "telnet:<host>") so a source can be toggled off and its spots cleared
    // without touching the others.  TCI callers keep the default.
    void addSpot(const QString &call, const QString &mode, qint64 freqHz,
                 quint32 argb, const QString &text,
                 const QString &source = QStringLiteral("tci"),
                 const QString &continent = QString());
    void deleteSpot(const QString &call);
    void clearAll();
    void clearSource(const QString &source);   // drop every spot from <source>

    // --- QML / panadapter ---
    // Spots within [center±span/2]: { call, freqHz, color (#AARRGGBB),
    // mode, text }.  Empty when display is disabled.
    Q_INVOKABLE QVariantList spotsInSpan(double centerHz, double spanHz) const;
    // True if <call> currently has a live spot (RBN / cluster / TCI), used to
    // highlight a decoded callsign the moment it's a real, spotted station.
    Q_INVOKABLE bool isSpotted(const QString &call) const;
    // Click-to-tune: tune to <call>'s spot (mode + freq) and emit
    // spotActivated so the TCI layer can echo it to clients.
    Q_INVOKABLE void activate(const QString &call);

    // --- settings (persisted "spots/...") ---
    bool showSpots() const   { return show_; }
    int  maxSpots() const    { return max_; }
    int  lifetimeSec() const { return lifetimeSec_; }
    bool highlightOwn() const     { return highlightOwn_; }
    QString highlightColor() const { return highlightColor_; }
    bool notifyOwn() const        { return notifyOwn_; }
    int  notifyCooldownMin() const { return notifyCooldownMin_; }
    bool flashNew() const         { return flashNew_; }
    QString flashColor() const    { return flashColor_; }
    int  flashSec() const         { return flashSec_; }
    // Mode display filter: comma-separated whitelist (blank = all).  "CW" →
    // CW/CWU/CWL, "SSB" → USB/LSB, "DIGI" → FT8/FT4/RTTY/… etc.  Display-only;
    // the bus keeps every spot, the overlay just hides filtered modes.
    QString modeFilter() const    { return modeFilter_.join(QLatin1Char(',')); }
    // Region display filter: comma-separated continent codes (NA/EU/AS/SA/AF/OC)
    // and/or ISO-2 country codes (US/CA/DE…, the codes shown on the markers).
    // Blank = all.  A spot is shown if any token matches its continent or country.
    QString regionFilter() const  { return regionFilter_.join(QLatin1Char(',')); }
    // Marker colouring: 0 = source/client colour (default), 1 = single custom
    // colour, 2 = by mode, 3 = by region (continent), 4 = by country (DXCC).
    int     colorMode() const     { return colorMode_; }
    QString singleColor() const   { return singleColor_; }
    bool    legendOn() const      { return legendOn_; }
    // [{label,color}] for the active colour mode (empty when no legend applies).
    Q_INVOKABLE QVariantList legendEntries() const;
    // Clutter caps (§5.4): max spots drawn at once (0 = unlimited); max spots
    // per frequency bucket before collapsing to a "+K more" badge (0 = off);
    // bucket width in Hz.
    int  displayMax() const { return displayMax_; }
    int  bucketMax() const  { return bucketMax_; }
    int  bucketHz() const   { return bucketHz_; }
    void setDisplayMax(int n);
    void setBucketMax(int n);
    void setBucketHz(int hz);
    void setShowSpots(bool on);
    void setMaxSpots(int n);
    void setLifetimeSec(int s);
    void setHighlightOwn(bool on);
    void setHighlightColor(const QString &hex);
    void setNotifyOwn(bool on);
    void setNotifyCooldownMin(int min);
    void setFlashNew(bool on);
    void setFlashColor(const QString &hex);
    void setFlashSec(int s);
    void setModeFilter(const QString &csv);
    void setRegionFilter(const QString &csv);
    void setColorMode(int m);
    void setSingleColor(const QString &hex);
    void setLegendOn(bool on);

signals:
    void changed();   // bank changed → panadapter re-queries
    void spotActivated(const QString &call, const QString &mode,
                       qint64 freqHz, quint32 argb);
    // The operator's own callsign was just spotted (rate-limited by the
    // re-notify cooldown).  MainWindow turns this into a toast.
    void ownCallSpotted(const QString &call, const QString &mode,
                        qint64 freqHz, const QString &text);

private:
    struct Spot {
        QString call, mode, text, country;   // country = ISO-2 (DXCC), "" if unknown
        QString continent;                   // "NA"/"EU"/... ("" if unknown)
        QString source;                      // feeder tag ("tci" | "spothole" | ...)
        qint64  freqHz = 0;
        quint32 argb   = 0xFFFFD700;   // gold default
        qint64  added  = 0;            // ms epoch of LAST spot (drives age/freshness)
        qint64  firstSeen = 0;         // ms epoch first seen (drives flash; survives re-spot)
    };
    int  indexOf(const QString &call) const;
    void enforceCap();
    void onAgeTick();
    void onFlashTick();              // repaint pulse while a spot is flashing
    bool anyFlashing(qint64 nowMs) const;
    bool modeShown(const QString &mode) const;   // mode-filter test (overlay)
    bool regionShown(const Spot &sp) const;      // region (continent/country) test
    QString colorFor(const Spot &sp) const;      // marker colour for the active mode
    static QString lyraModeFor(const QString &tciMode, qint64 freqHz);

    Prefs                 *prefs_  = nullptr;
    lyra::ipc::HL2Stream  *stream_ = nullptr;
    lyra::dsp::WdspEngine *engine_ = nullptr;
    DxccLookup             dxcc_;
    QVector<Spot>         spots_;
    QTimer               *ageTimer_ = nullptr;
    QTimer               *flashTimer_ = nullptr;
    qint64                tStart_ = 0;

    bool show_        = true;
    int  max_         = 200;
    int  lifetimeSec_ = 0;     // 0 = never expire
    bool    highlightOwn_   = true;
    QString highlightColor_ = QStringLiteral("#ff4081");   // magenta-pink
    bool    notifyOwn_         = false;   // toast when my call is spotted
    int     notifyCooldownMin_ = 10;      // re-notify no sooner than this
    qint64  lastNotify_        = 0;       // ms epoch of the last toast
    // #172 — "flash new spots": a freshly-arrived spot renders in
    // flashColor_ for flashSec_ seconds to draw the eye, then settles to
    // its normal cluster colour.  flashTimer_ pulses changed() while any
    // spot is inside the window so the panadapter re-queries and the
    // flash decays without waiting for the next spot / age event.
    bool    flashNew_   = false;
    QString flashColor_ = QStringLiteral("#ffeb3b");   // amber-yellow
    int     flashSec_   = 8;
    QStringList modeFilter_;   // uppercased whitelist; empty = show all modes
    QStringList regionFilter_; // continent + ISO-country tokens; empty = all
    int     colorMode_   = 0;                          // 0 = source/client colour
    QString singleColor_ = QStringLiteral("#66bb6a");  // green (Single mode)
    bool    legendOn_    = false;
    int     displayMax_  = 25;     // max spots drawn at once (0 = unlimited)
    int     bucketMax_   = 3;      // max per frequency bucket (0 = off)
    int     bucketHz_    = 500;    // bucket width (Hz)
};

} // namespace lyra::ui

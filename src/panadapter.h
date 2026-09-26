// Lyra — panadapter spectrum display (Step 5, scene-graph renderer).
//
// A QQuickItem rendered on the GPU via the Qt Quick scene graph (RHI /
// Vulkan).  Pulls the WDSP analyzer's display-width dB array from
// WdspEngine and draws a smooth "liquid ribbon" spectrum on a glassy
// background — built to scale to 60→144 fps (the look the operator
// wants, done right the first time rather than a painter we'd redo).
//
// Rendering model (no custom shaders in v1 — QSGVertexColorMaterial
// does gradients via per-vertex RGBA):
//   * Glass background  : a gradient quad.
//   * Fluid ribbon+fill : ONE gradient triangle-strip whose TOP edge
//     rides a Catmull-Rom spline through the data (bright trace colour)
//     and whose BOTTOM edge sits at the baseline (transparent) — that
//     single strip is the smooth line AND the liquid fill body.
//   * Accuracy: 2048 analyzer points are reduced to screen columns by
//     PEAK-per-column (never drops a narrow CW/FT8 signal), then the
//     spline only smooths the connection — it passes through the peaks.
//
// Frame pump: window afterAnimating (vsync-locked) with an fps throttle
// — fetch the latest spectrum + schedule a repaint.  The throttle is
// the operator frame-rate control (added later).
//
// Behaviour (click-to-tune, marker, passband, snap, zoom, notch) is
// ported from the Python Lyra spectrum widget in later sub-steps.

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QQuickItem>
#include <QVariantList>

#include "autoscale.h"

#include <vector>

class QTimer;

namespace lyra::dsp { class WdspEngine; }

namespace lyra::ui {

class Panadapter : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject *engine READ engineObj WRITE setEngineObj
               NOTIFY engineChanged)
    // dB display window (vertical scale).  Operator-tunable later.
    Q_PROPERTY(double dbMin READ dbMin WRITE setDbMin NOTIFY rangeChanged)
    Q_PROPERTY(double dbMax READ dbMax WRITE setDbMax NOTIFY rangeChanged)
    // Auto dB range: when true the floor/ceiling track the band
    // automatically and dbMin/dbMax are ignored for rendering.  The
    // EFFECTIVE range actually in use (auto OR manual) is exposed via
    // effDbMin/effDbMax so the dB-scale labels follow it.
    Q_PROPERTY(bool autoScale READ autoScale WRITE setAutoScale
               NOTIFY autoScaleChanged)
    Q_PROPERTY(double effDbMin READ effDbMin NOTIFY effRangeChanged)
    Q_PROPERTY(double effDbMax READ effDbMax NOTIFY effRangeChanged)
    // Target display fps (throttle of the vsync pump).  0 = every frame.
    Q_PROPERTY(int targetFps READ targetFps WRITE setTargetFps
               NOTIFY targetFpsChanged)
    // Temporal smoothing 0..10 (display-only EWMA across frames, like
    // old Lyra).  0 = off / raw FFT.  Higher = smoother + slower
    // response (alpha = 1 - strength/11).  Smooths visual flicker over
    // TIME (not across frequency — that was the rejected box-blur);
    // higher values trade in a little response lag.
    Q_PROPERTY(int smoothing READ smoothing WRITE setSmoothing
               NOTIFY smoothingChanged)
    // Gridline brightness 0..100.  0 = off, 100 = bright.
    Q_PROPERTY(int gridLevel READ gridLevel WRITE setGridLevel
               NOTIFY gridLevelChanged)
    // Glass sheen 0..100: a subtle diagonal light reflection over the
    // glass panel (drawn under the grid/trace).  0 = off.
    Q_PROPERTY(int glassSheen READ glassSheen WRITE setGlassSheen
               NOTIFY glassSheenChanged)
    // Trace colour MODE: 0 = Solid (one picked colour), 1 = By strength
    // (colour changes with signal level via the chosen palette LUT —
    // the intensity gradient old Lyra couldn't pull off).
    Q_PROPERTY(int traceMode READ traceMode WRITE setTraceMode
               NOTIFY traceModeChanged)
    // Solid-mode trace colour (default light cyan, like old Lyra).
    Q_PROPERTY(QColor traceColor READ traceColor WRITE setTraceColor
               NOTIFY traceColorChanged)
    // By-strength-mode palette index (lyra::palettes order).  An index
    // past the preset palettes selects the operator's CUSTOM strength
    // colour (strengthColor) — a dark->hue->bright ramp, like Amber.
    Q_PROPERTY(int palette READ palette WRITE setPalette
               NOTIFY paletteChanged)
    // Base colour for the custom by-strength ramp.
    Q_PROPERTY(QColor strengthColor READ strengthColor WRITE setStrengthColor
               NOTIFY strengthColorChanged)
    // Fill on/off.  Off = clean bright line only (ExpertSDR3 look);
    // on = translucent gradient fill beneath the line (deskHPSDR look).
    Q_PROPERTY(bool fillEnabled READ fillEnabled WRITE setFillEnabled
               NOTIFY fillEnabledChanged)
    // Fill colour, used in SOLID trace mode so the fill can be a DIFFERENT
    // colour than the trace line (old-Lyra parity).  In by-strength mode
    // the fill stays intensity-coloured (the heat-map look) and ignores
    // this.
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor
               NOTIFY fillColorChanged)
    // ---- Peak-hold markers (old-Lyra parity) ----
    // peakHoldSecs encodes the mode: 0 = Off, -1 = Hold (infinite),
    // -2 = Live (rides the current spectrum), >0 = hold-then-decay
    // seconds.  Held per analyzer bin, decayed at peakDecayDbps, drawn
    // as a separate overlay in the chosen style.
    Q_PROPERTY(bool peakEnabled READ peakEnabled WRITE setPeakEnabled
               NOTIFY peakEnabledChanged)
    Q_PROPERTY(double peakHoldSecs READ peakHoldSecs WRITE setPeakHoldSecs
               NOTIFY peakHoldSecsChanged)
    Q_PROPERTY(double peakDecayDbps READ peakDecayDbps WRITE setPeakDecayDbps
               NOTIFY peakDecayDbpsChanged)
    // 0 = Line, 1 = Dots, 2 = Triangles.
    Q_PROPERTY(int peakStyle READ peakStyle WRITE setPeakStyle
               NOTIFY peakStyleChanged)
    Q_PROPERTY(QColor peakColor READ peakColor WRITE setPeakColor
               NOTIFY peakColorChanged)
    Q_PROPERTY(bool peakShowDb READ peakShowDb WRITE setPeakShowDb
               NOTIFY peakShowDbChanged)
    // Numeric dB labels for the strongest in-view peaks — each entry is
    // {frac: 0..1 across width, db: value}.  Rendered as QML Text in
    // PanadapterPanel (text in the scene graph is impractical here).
    Q_PROPERTY(QVariantList peakLabels READ peakLabels NOTIFY peakLabelsChanged)
    // Rolling noise-floor estimate (~20th percentile of the displayed
    // spectrum, EWMA-smoothed) in dBFS.  Drives the panadapter's
    // noise-floor reference line; the line's colour + on/off are Prefs
    // (Settings → Visuals) and the line itself is drawn in QML.
    Q_PROPERTY(double noiseFloorDb READ noiseFloorDb NOTIFY noiseFloorChanged)

public:
    explicit Panadapter(QQuickItem *parent = nullptr);

    // Clear the peak-hold buffer (the Display panel "Clear" button).
    Q_INVOKABLE void clearPeaks();

    QObject *engineObj() const;
    void     setEngineObj(QObject *o);

    double dbMin() const { return dbMin_; }
    void   setDbMin(double v);
    double dbMax() const { return dbMax_; }
    void   setDbMax(double v);

    bool autoScale() const { return autoScale_; }
    void setAutoScale(bool v);
    // Effective range currently rendered (auto-fit when on, else manual).
    double effDbMin() const { return autoScale_ ? autoScaler_.floorDb() : dbMin_; }
    double effDbMax() const { return autoScale_ ? autoScaler_.ceilDb()  : dbMax_; }

    int  targetFps() const { return targetFps_; }
    void setTargetFps(int v);

    int  smoothing() const { return smoothing_; }
    void setSmoothing(int v);

    int  gridLevel() const { return gridLevel_; }
    void setGridLevel(int v);

    int  glassSheen() const { return glassSheen_; }
    void setGlassSheen(int v);

    int  traceMode() const { return traceMode_; }
    void setTraceMode(int v);

    QColor traceColor() const { return traceColor_; }
    void   setTraceColor(const QColor &c);

    int  palette() const { return paletteIndex_; }
    void setPalette(int v);

    QColor strengthColor() const { return strengthColor_; }
    void   setStrengthColor(const QColor &c);

    bool fillEnabled() const { return fillEnabled_; }
    void setFillEnabled(bool v);

    QColor fillColor() const { return fillColor_; }
    void   setFillColor(const QColor &c);

    bool peakEnabled() const { return peakEnabled_; }
    void setPeakEnabled(bool v);
    double peakHoldSecs() const { return peakHoldSecs_; }
    void   setPeakHoldSecs(double v);
    double peakDecayDbps() const { return peakDecayDbps_; }
    void   setPeakDecayDbps(double v);
    int  peakStyle() const { return peakStyle_; }
    void setPeakStyle(int v);
    QColor peakColor() const { return peakColor_; }
    void   setPeakColor(const QColor &c);
    bool peakShowDb() const { return peakShowDb_; }
    void setPeakShowDb(bool v);
    QVariantList peakLabels() const { return peakLabels_; }
    double noiseFloorDb() const { return noiseFloorDb_; }

signals:
    void engineChanged();
    void rangeChanged();
    void autoScaleChanged();
    void effRangeChanged();   // effective (auto or manual) range changed
    void targetFpsChanged();
    void smoothingChanged();
    void gridLevelChanged();
    void glassSheenChanged();
    void traceModeChanged();
    void traceColorChanged();
    void paletteChanged();
    void strengthColorChanged();
    void fillEnabledChanged();
    void fillColorChanged();
    void peakEnabledChanged();
    void peakHoldSecsChanged();
    void peakDecayDbpsChanged();
    void peakStyleChanged();
    void peakColorChanged();
    void peakShowDbChanged();
    void peakLabelsChanged();
    void noiseFloorChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode,
                             UpdatePaintNodeData *data) override;
    void itemChange(ItemChange change,
                    const ItemChangeData &value) override;

private slots:
    void onFrame();   // timer-paced: fetch spectrum + schedule repaint

private:
    // Passband pixel range [lo,hi) across the item width, from the
    // engine's passband offsets + span (full width if no engine / no
    // span).  Peak markers + dB labels render only inside it (old-Lyra
    // parity: the peak overlay is passband-only).
    void passbandPx(int W, int *lo, int *hi) const;

    lyra::dsp::WdspEngine *engine_ = nullptr;
    std::vector<float>     pix_;        // dB pixels (post-EWMA, what renders)
    std::vector<float>     rawPix_;     // latest raw frame from the analyzer
    bool                   smoothInit_ = false;  // pix_ seeded yet?
    double                 dbMin_ = -130.0;
    double                 dbMax_ = -20.0;
    bool                   autoScale_ = false;   // auto-fit the dB range?
    AutoScaler             autoScaler_;          // computes the auto range
    QElapsedTimer          autoClock_;           // throttles the auto feed
    qint64                 lastAutoMs_ = -1;
    qint64                 lastFrameMs_ = -1;    // skip PreciseTimer catch-up bursts
    int                    targetFps_ = 60;
    int                    smoothing_ = 0;     // 0..100 spatial smoothing (off = Lyra-style raw trace)
    int                    gridLevel_ = 35;    // 0..100 gridline brightness
    int                    glassSheen_ = 20;   // 0..100 glass reflection
    int                    traceMode_ = 0;     // 0 solid, 1 by-strength
    QColor                 traceColor_ = QColor(0x5e, 0xc8, 0xff);  // solid trace colour
    int                    paletteIndex_ = 0;  // by-strength palette
    QColor                 strengthColor_ = QColor(0xff, 0x9b, 0x30);  // custom ramp base
    bool                   fillEnabled_ = true; // fill beneath the line
    QColor                 fillColor_ = QColor(0x5e, 0xc8, 0xff);  // solid-mode fill
    QTimer                *frameTimer_ = nullptr;  // fixed-cadence repaint pump

    // ---- peak-hold markers ----
    bool   peakEnabled_   = false;
    double peakHoldSecs_  = 0.0;     // 0 Off / -1 Hold / -2 Live / >0 timed
    double peakDecayDbps_ = 10.0;
    int    peakStyle_     = 1;       // 0 line / 1 dots / 2 triangles
    QColor peakColor_     = QColor(0xff, 0xbe, 0x5a);
    bool   peakShowDb_    = false;
    QVariantList           peakLabels_;
    std::vector<float>     peakDb_;       // per-bin hold buffer (size of pix_)
    std::vector<double>    peakUpdated_;  // per-bin last-updated (seconds)
    double                 peakLastS_ = -1.0;  // last hold/decay tick (seconds)
    std::vector<float>     peakColDb_;    // per-column reduced peak curve (scratch)

    // ---- rolling noise-floor estimate (for the reference line) ----
    double                 noiseFloorDb_ = -150.0;
    bool                   noiseFloorSeeded_ = false;
    std::vector<float>     nfScratch_;

    // Per-frame scratch (reused to avoid per-frame allocation): the
    // peak-per-column curve, then the spatially-smoothed curve.
    std::vector<float>     colDb_;
    std::vector<float>     smoothDb_;
};

} // namespace lyra::ui

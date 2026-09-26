// Lyra — waterfall (rolling spectrogram) display.
//
// A QQuickItem rendered on the GPU via the Qt Quick scene graph (RHI /
// Vulkan), the time-history companion to the Panadapter.  It pulls the
// SAME WDSP analyzer dB array (engine->copySpectrum) on the same
// timer-paced pump, maps each bin through the operator's palette LUT
// (normalized by the shared dbMin/dbMax window), and scrolls one new
// row in at the top each frame — newest at top, history falling down.
//
// Render model: an internal QImage history buffer (binCount × kHistory)
// is scrolled by one row per frame and uploaded as a QSGImageNode
// texture, which the scene graph scales (linear-filtered) to fill the
// item — so the waterfall stays smooth at any panel size and shares the
// panadapter's exact frequency extent (same bin count).
//
// Palette: index into lyra::palettes (the 8 presets + Amber); an index
// past the presets selects the operator's custom strengthColor ramp
// (dark → hue → bright), matching the by-strength trace colouring so
// the spectrum and waterfall read as one instrument.

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QQuickItem>

#include "autoscale.h"
#include "palettes.h"

#include <vector>

class QTimer;
class QSGTexture;

namespace lyra::dsp { class WdspEngine; }

namespace lyra::ui {

class Waterfall : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject *engine READ engineObj WRITE setEngineObj
               NOTIFY engineChanged)
    Q_PROPERTY(double dbMin READ dbMin WRITE setDbMin NOTIFY rangeChanged)
    Q_PROPERTY(double dbMax READ dbMax WRITE setDbMax NOTIFY rangeChanged)
    Q_PROPERTY(int targetFps READ targetFps WRITE setTargetFps
               NOTIFY targetFpsChanged)
    Q_PROPERTY(int palette READ palette WRITE setPalette NOTIFY paletteChanged)
    Q_PROPERTY(QColor strengthColor READ strengthColor WRITE setStrengthColor
               NOTIFY strengthColorChanged)
    // Scroll speed in rows (new history lines) per second.  The render
    // pump still runs at targetFps; between row pushes incoming frames
    // are peak-held so brief signals survive even at slow speeds.
    Q_PROPERTY(int speed READ speed WRITE setSpeed NOTIFY speedChanged)
    // Auto dB range: when true, the heat-map floor/ceiling track the
    // band automatically (noise-floor − 15 dB .. peak + 15 dB, ≥50 dB
    // span, smoothed) and dbMin/dbMax are ignored.
    Q_PROPERTY(bool autoScale READ autoScale WRITE setAutoScale
               NOTIFY autoScaleChanged)

public:
    explicit Waterfall(QQuickItem *parent = nullptr);

    QObject *engineObj() const;
    void     setEngineObj(QObject *o);

    double dbMin() const { return dbMin_; }
    void   setDbMin(double v);
    double dbMax() const { return dbMax_; }
    void   setDbMax(double v);

    int  targetFps() const { return targetFps_; }
    void setTargetFps(int v);

    int  palette() const { return paletteIndex_; }
    void setPalette(int v);

    QColor strengthColor() const { return strengthColor_; }
    void   setStrengthColor(const QColor &c);

    int  speed() const { return speed_; }
    void setSpeed(int v);

    bool autoScale() const { return autoScale_; }
    void setAutoScale(bool v);

signals:
    void engineChanged();
    void rangeChanged();
    void targetFpsChanged();
    void paletteChanged();
    void strengthColorChanged();
    void speedChanged();
    void autoScaleChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode,
                             UpdatePaintNodeData *data) override;
    void itemChange(ItemChange change,
                    const ItemChangeData &value) override;

private slots:
    void onFrame();   // timer-paced: pull spectrum, scroll a new row in

private:
    void rebuildLut();   // refresh lut_ from paletteIndex_ / strengthColor_

    lyra::dsp::WdspEngine *engine_ = nullptr;
    QImage               img_;            // history buffer (binCount × kHistory)
    std::vector<float>   row_;            // scratch: latest dB frame
    std::vector<float>   pendingMax_;     // peak-hold accumulator between rows
    QElapsedTimer        rowClock_;       // paces row pushes by speed_
    qint64               lastRowMs_ = -1; // ms of the last row push
    qint64               lastFrameMs_ = -1; // skip PreciseTimer catch-up bursts
    bool                 dirty_ = false;  // new row since last paint?
    double               dbMin_ = -130.0;
    double               dbMax_ = -20.0;
    bool                 autoScale_ = false;  // auto-fit the dB range?
    AutoScaler           autoScaler_;         // computes the auto range
    qint64               lastAutoMs_ = -1;    // throttle auto-fit feed (~5 Hz)
    int                  targetFps_ = 60;
    int                  speed_ = 20;     // history rows per second
    int                  paletteIndex_ = 0;
    QColor               strengthColor_ = QColor(0xff, 0x9b, 0x30);
    lyra::palettes::Lut  lut_;            // active 256-entry colour LUT
    QTimer              *frameTimer_ = nullptr;

    static constexpr int kHistory = 512;  // rows of time history retained
};

} // namespace lyra::ui

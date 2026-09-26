// Lyra — panadapter scene-graph renderer (Step 5).  See panadapter.h.

#include "panadapter.h"

#include "palettes.h"
#include "wdsp_engine.h"

#include <QQuickWindow>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

namespace lyra::ui {

namespace {
// Glassy background — a 4-stop vertical gradient that reads as a
// dark glass panel: a faint top SHEEN highlight, a quick falloff,
// a deep mid body, and a near-black floor.  Stops are (fraction of
// height, R, G, B).  Palette tinting lands in step 7; these neutral
// cool-dark values keep an amber/any trace popping on top.
struct BgStop { float f; int r, g, b; };
constexpr BgStop kBgStops[4] = {
    { 0.00f, 30, 40, 54 },   // top sheen highlight
    { 0.07f, 17, 23, 33 },   // sheen falloff
    { 0.55f,  9, 12, 18 },   // deep body
    { 1.00f,  3,  4,  7 },   // near-black floor
};
// Gridlines: cool blue-grey, alpha scaled by the operator brightness.
constexpr int kGridR = 90, kGridG = 120, kGridB = 150;
constexpr int kGridMaxA = 150;     // alpha at gridLevel == 100
constexpr int kGridHLines = 8;     // horizontal (dB) divisions
constexpr int kGridVLines = 10;    // vertical (frequency) divisions
// Trace = a bright thin STROKE (line) on top of a dimmer translucent
// FILL — the "gradient fill + AA stroke" look (step 2 notes).  The
// stroke is near-opaque so it reads as a crisp line; the fill is
// translucent so the line clearly pops above it, fading to nothing at
// the baseline.  All premultiplied (QSGVertexColorMaterial blends
// premultiplied).
constexpr int kFillTopA  = 110;   // translucent fill body at the curve
constexpr int kFillBotA  = 0;     // fill fades to nothing at baseline
constexpr int kLineA     = 245;   // bright trace stroke
constexpr float kLineWidth = 1.6f;
// Peak-hold overlay alpha (old-Lyra used 230 so it stays distinct over
// any hue).  Premultiplied like the rest of the scene.
constexpr int kPeakA = 230;

// Build a dark -> hue -> bright ramp colour from a base colour at
// normalized strength t (0..1) — the generic form of what the Amber
// palette does, so a custom by-strength colour "works like Amber".
inline void rampColor(const QColor &c, double t, int &r, int &g, int &b) {
    if (t <= 0.65) {
        const double f = t / 0.65;          // black -> full colour
        r = static_cast<int>(c.red()   * f);
        g = static_cast<int>(c.green() * f);
        b = static_cast<int>(c.blue()  * f);
    } else {
        const double f = (t - 0.65) / 0.35; // colour -> toward white
        r = static_cast<int>(c.red()   + (255 - c.red())   * f * 0.7);
        g = static_cast<int>(c.green() + (255 - c.green()) * f * 0.7);
        b = static_cast<int>(c.blue()  + (255 - c.blue())  * f * 0.7);
    }
}

} // namespace

Panadapter::Panadapter(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);

    // Fixed-cadence repaint pump.  We DON'T use the window's
    // afterAnimating (per-vsync) signal: it doesn't fire continuously
    // inside a QQuickWidget (only when the scene otherwise re-renders),
    // and it can burst after a stall.  A plain QTimer at the target fps
    // gives steady frame pacing on the GUI thread regardless of how the
    // scene is hosted.  Started/stopped as the item enters/leaves a
    // scene (itemChange); guarded on engine + visibility in onFrame.
    frameTimer_ = new QTimer(this);
    frameTimer_->setTimerType(Qt::PreciseTimer);
    frameTimer_->setInterval(1000 / std::max(1, targetFps_));
    connect(frameTimer_, &QTimer::timeout, this, &Panadapter::onFrame);

    autoClock_.start();   // paces the auto-fit feed (throttled in onFrame)
}

QObject *Panadapter::engineObj() const {
    return reinterpret_cast<QObject *>(engine_);
}

void Panadapter::setEngineObj(QObject *o) {
    auto *e = qobject_cast<lyra::dsp::WdspEngine *>(o);
    if (e == engine_) {
        return;
    }
    engine_ = e;
    if (engine_) {
        pix_.assign(static_cast<size_t>(engine_->spectrumPixelCount()),
                    -200.0f);
    } else {
        pix_.clear();
    }
    emit engineChanged();
}

void Panadapter::setDbMin(double v) {
    if (v != dbMin_) {
        dbMin_ = v;
        emit rangeChanged();
        if (!autoScale_) emit effRangeChanged();   // manual = effective
        update();
    }
}

void Panadapter::setDbMax(double v) {
    if (v != dbMax_) {
        dbMax_ = v;
        emit rangeChanged();
        if (!autoScale_) emit effRangeChanged();
        update();
    }
}

void Panadapter::setAutoScale(bool v) {
    if (v != autoScale_) {
        autoScale_ = v;
        if (v) { autoScaler_.reset(); lastAutoMs_ = -1; }   // fresh fit
        emit autoScaleChanged();
        emit effRangeChanged();   // effective source switched
        update();
    }
}

void Panadapter::setTargetFps(int v) {
    v = std::clamp(v, 1, 1000);
    if (v != targetFps_) {
        targetFps_ = v;
        if (frameTimer_) {
            frameTimer_->setInterval(1000 / targetFps_);
        }
        emit targetFpsChanged();
    }
}

void Panadapter::setSmoothing(int v) {
    v = std::clamp(v, 0, 10);
    if (v != smoothing_) { smoothing_ = v; emit smoothingChanged(); update(); }
}

void Panadapter::setGridLevel(int v) {
    v = std::clamp(v, 0, 100);
    if (v != gridLevel_) { gridLevel_ = v; emit gridLevelChanged(); update(); }
}

void Panadapter::setGlassSheen(int v) {
    v = std::clamp(v, 0, 100);
    if (v != glassSheen_) { glassSheen_ = v; emit glassSheenChanged(); update(); }
}

void Panadapter::setTraceMode(int v) {
    v = std::clamp(v, 0, 1);
    if (v != traceMode_) { traceMode_ = v; emit traceModeChanged(); update(); }
}

void Panadapter::setTraceColor(const QColor &c) {
    if (c != traceColor_) { traceColor_ = c; emit traceColorChanged(); update(); }
}

void Panadapter::setPalette(int v) {
    if (v != paletteIndex_) { paletteIndex_ = v; emit paletteChanged(); update(); }
}

void Panadapter::setStrengthColor(const QColor &c) {
    if (c != strengthColor_) { strengthColor_ = c; emit strengthColorChanged(); update(); }
}

void Panadapter::setFillEnabled(bool v) {
    if (v != fillEnabled_) { fillEnabled_ = v; emit fillEnabledChanged(); update(); }
}

void Panadapter::setFillColor(const QColor &c) {
    if (c != fillColor_) { fillColor_ = c; emit fillColorChanged(); update(); }
}

void Panadapter::setPeakEnabled(bool v) {
    if (v != peakEnabled_) {
        peakEnabled_ = v;
        if (!v) {
            peakDb_.clear();
            peakUpdated_.clear();
            if (!peakLabels_.isEmpty()) { peakLabels_.clear(); emit peakLabelsChanged(); }
        }
        emit peakEnabledChanged();
        update();
    }
}

void Panadapter::setPeakHoldSecs(double v) {
    if (v != peakHoldSecs_) {
        peakHoldSecs_ = v;
        // Mode change → start the new mode from a clean buffer.
        peakDb_.clear();
        peakUpdated_.clear();
        emit peakHoldSecsChanged();
        update();
    }
}

void Panadapter::setPeakDecayDbps(double v) {
    if (v != peakDecayDbps_) { peakDecayDbps_ = v; emit peakDecayDbpsChanged(); }
}

void Panadapter::setPeakStyle(int v) {
    v = std::clamp(v, 0, 2);
    if (v != peakStyle_) { peakStyle_ = v; emit peakStyleChanged(); update(); }
}

void Panadapter::setPeakColor(const QColor &c) {
    if (c != peakColor_) { peakColor_ = c; emit peakColorChanged(); update(); }
}

void Panadapter::setPeakShowDb(bool v) {
    if (v != peakShowDb_) {
        peakShowDb_ = v;
        if (!v && !peakLabels_.isEmpty()) { peakLabels_.clear(); emit peakLabelsChanged(); }
        emit peakShowDbChanged();
    }
}

void Panadapter::clearPeaks() {
    peakDb_.clear();
    peakUpdated_.clear();
    if (!peakLabels_.isEmpty()) { peakLabels_.clear(); emit peakLabelsChanged(); }
    update();
}

void Panadapter::itemChange(ItemChange change, const ItemChangeData &value) {
    if (change == ItemSceneChange && frameTimer_) {
        // Run the repaint pump only while the item belongs to a scene.
        if (value.window) {
            frameTimer_->start();
        } else {
            frameTimer_->stop();
        }
    }
    QQuickItem::itemChange(change, value);
}

void Panadapter::passbandPx(int W, int *lo, int *hi) const {
    int l = 0, h = W;
    if (engine_) {
        const double span = static_cast<double>(engine_->spanHz());
        if (span > 0.0 && W > 0) {
            double xl = W * (0.5 + engine_->passbandLowHz()  / span);
            double xh = W * (0.5 + engine_->passbandHighHz() / span);
            if (xh < xl) std::swap(xl, xh);
            l = std::clamp(static_cast<int>(xl), 0, W);
            h = std::clamp(static_cast<int>(xh), 0, W);
        }
    }
    *lo = l;
    *hi = h;
}

void Panadapter::onFrame() {
    // GUI-thread timer tick: pull the latest spectrum, apply optional
    // temporal EWMA smoothing, and schedule a scene-graph rebuild.
    // Cheap no-op when there's no engine or the item isn't visible.
    if (!engine_ || pix_.empty() || !isVisible()) {
        return;
    }
    // PreciseTimer will dump a burst of queued ticks after a GUI stall.
    // Doing GetPixels + a full scene-graph rebuild for each one wedges
    // the QQuickWidget host.  Keep at most one real frame per interval.
    {
        const qint64 now = autoClock_.elapsed();
        const qint64 minMs = 1000 / std::max(1, targetFps_);
        if (lastFrameMs_ >= 0 && (now - lastFrameMs_) < (minMs / 2))
            return;
        lastFrameMs_ = now;
    }
    const int n = static_cast<int>(pix_.size());
    if (static_cast<int>(rawPix_.size()) != n) {
        rawPix_.assign(static_cast<size_t>(n), -200.0f);
    }
    engine_->copySpectrum(rawPix_.data(), n);

    // Auto-fit the dB range from the raw spectrum (throttled ~5 Hz).
    // effRangeChanged drives the dB-scale labels to follow the live fit.
    if (autoScale_) {
        const qint64 nowA = autoClock_.elapsed();
        if (lastAutoMs_ < 0 || (nowA - lastAutoMs_) >= 200) {
            lastAutoMs_ = nowA;
            autoScaler_.feed(rawPix_.data(), n);
            emit effRangeChanged();
        }
    }

    if (smoothing_ > 0 && smoothInit_) {
        // Display-only EWMA across frames (old-Lyra smoothing): blend
        // each new frame into the rendered buffer.  Map strength 1..10
        // to the blend factor GEOMETRICALLY (time constant grows by a
        // constant ~1.3x per step) so each step is an even, gentle
        // increment — a plain linear map jumps hard at the top end.
        constexpr double kTauMin = 1.2;   // frames, gentlest (strength 1)
        constexpr double kTauMax = 12.0;  // frames, heaviest (strength 10)
        const double tau = kTauMin *
            std::pow(kTauMax / kTauMin,
                     (static_cast<double>(smoothing_) - 1.0) / 9.0);
        const float alpha = static_cast<float>(1.0 - std::exp(-1.0 / tau));
        for (int i = 0; i < n; ++i) {
            pix_[static_cast<size_t>(i)] +=
                alpha * (rawPix_[static_cast<size_t>(i)] -
                         pix_[static_cast<size_t>(i)]);
        }
    } else {
        // Off (or first frame): show the raw frame directly.
        std::copy(rawPix_.begin(), rawPix_.end(), pix_.begin());
        smoothInit_ = true;
    }

    // ---- peak-hold markers (old-Lyra port) ----
    // Maintain the per-bin hold buffer over the DISPLAYED spectrum (pix_):
    //   holdSecs == -2  Live   — buffer = live spectrum each tick.
    //   holdSecs <   0  Hold   — infinite; only clamp up to live.
    //   holdSecs >   0  timed  — hold N sec then decay at decay_dbps;
    //                            always clamp up; reset the per-bin timer
    //                            only when a fresh higher peak hits a bin
    //                            already past its hold window.
    if (peakEnabled_ && peakHoldSecs_ != 0.0 && n > 0) {
        const double now = autoClock_.elapsed() / 1000.0;
        if (peakHoldSecs_ == -2.0) {                       // Live
            peakDb_.assign(pix_.begin(), pix_.end());
            peakLastS_ = now;
        } else if (static_cast<int>(peakDb_.size()) != n) { // (re)seed
            peakDb_.assign(pix_.begin(), pix_.end());
            peakUpdated_.assign(static_cast<size_t>(n), now);
            peakLastS_ = now;
        } else {
            if (static_cast<int>(peakUpdated_.size()) != n)
                peakUpdated_.assign(static_cast<size_t>(n), now);
            const double dt = std::max(0.005, now - peakLastS_);
            peakLastS_ = now;
            if (peakHoldSecs_ < 0.0) {                     // Hold (infinite)
                for (int i = 0; i < n; ++i) {
                    const size_t si = static_cast<size_t>(i);
                    if (pix_[si] > peakDb_[si]) {
                        peakDb_[si] = pix_[si];
                        peakUpdated_[si] = now;
                    }
                }
            } else {                                       // hold-then-decay
                const float dec = static_cast<float>(peakDecayDbps_ * dt);
                for (int i = 0; i < n; ++i) {
                    const size_t si = static_cast<size_t>(i);
                    const bool decaying = (now - peakUpdated_[si]) > peakHoldSecs_;
                    if (decaying) peakDb_[si] -= dec;
                    if (pix_[si] > peakDb_[si]) {
                        peakDb_[si] = pix_[si];
                        if (decaying) peakUpdated_[si] = now;
                    }
                }
            }
        }
    } else if (!peakDb_.empty()) {
        peakDb_.clear();
        peakUpdated_.clear();
    }

    // dB labels: top-3 in-view peaks ≥ 6 dB over the in-view minimum,
    // separated by ≥ 16 px.  Computed here (GUI thread) and exposed for
    // the QML Text overlay; each entry is {frac: 0..1, db: value}.
    if (peakEnabled_ && peakShowDb_ && !peakDb_.empty()) {
        const int W = static_cast<int>(width());
        QVariantList labels;
        int pbLo = 0, pbHi = W;
        passbandPx(W, &pbLo, &pbHi);
        // Only inside the RX passband (old-Lyra parity), like the overlay.
        if (W >= 12 && (pbHi - pbLo) > 10) {
            std::vector<float> col(static_cast<size_t>(W), -300.0f);
            for (int x = pbLo; x < pbHi; ++x) {
                int i0 = static_cast<int>(static_cast<qint64>(x) * n / W);
                int i1 = static_cast<int>(static_cast<qint64>(x + 1) * n / W);
                if (i1 <= i0) i1 = i0 + 1;
                if (i1 > n) i1 = n;
                float db = -300.0f;
                for (int i = i0; i < i1; ++i)
                    if (peakDb_[static_cast<size_t>(i)] > db)
                        db = peakDb_[static_cast<size_t>(i)];
                col[static_cast<size_t>(x)] = db;
            }
            float mn = col[static_cast<size_t>(pbLo)];
            for (int x = pbLo; x < pbHi; ++x)
                mn = std::min(mn, col[static_cast<size_t>(x)]);
            const float thr = mn + 6.0f;
            const int lo = std::max(pbLo + 1, 1);
            const int hi = std::min(pbHi - 1, W - 1);
            std::vector<std::pair<float, int>> cand;
            for (int x = lo; x < hi; ++x) {
                const float v = col[static_cast<size_t>(x)];
                if (v > col[static_cast<size_t>(x - 1)] &&
                    v >= col[static_cast<size_t>(x + 1)] && v >= thr)
                    cand.emplace_back(v, x);
            }
            std::sort(cand.begin(), cand.end(),
                      [](const auto &a, const auto &b) { return a.first > b.first; });
            std::vector<int> chosen;
            for (const auto &c : cand) {
                bool far = true;
                for (int cx : chosen)
                    if (std::abs(c.second - cx) < 16) { far = false; break; }
                if (far) {
                    chosen.push_back(c.second);
                    QVariantMap m;
                    m[QStringLiteral("frac")] =
                        static_cast<double>(c.second) / static_cast<double>(W);
                    m[QStringLiteral("db")] = static_cast<double>(c.first);
                    labels.push_back(m);
                }
                if (chosen.size() >= 3) break;
            }
        }
        if (labels != peakLabels_) { peakLabels_ = labels; emit peakLabelsChanged(); }
    } else if (!peakLabels_.isEmpty()) {
        peakLabels_.clear();
        emit peakLabelsChanged();
    }

    // ---- rolling noise-floor estimate (old-Lyra reference line) ----
    // ~20th percentile of the displayed spectrum, EWMA-smoothed so the
    // line tracks the band without jittering.  Exposed as noiseFloorDb;
    // the line + label are drawn in QML (Settings → Visuals colour/toggle).
    if (n >= 4) {
        nfScratch_.assign(pix_.begin(), pix_.end());
        const int k = std::clamp(static_cast<int>(n * 0.20), 1, n - 1);
        std::nth_element(nfScratch_.begin(), nfScratch_.begin() + k,
                         nfScratch_.end());
        const double nf = static_cast<double>(nfScratch_[static_cast<size_t>(k)]);
        double v;
        if (!noiseFloorSeeded_) { v = nf; noiseFloorSeeded_ = true; }
        else                    { v = noiseFloorDb_ + 0.10 * (nf - noiseFloorDb_); }
        if (std::abs(v - noiseFloorDb_) > 0.05) {
            noiseFloorDb_ = v;
            emit noiseFloorChanged();
        } else {
            noiseFloorDb_ = v;
        }
    }

    update();
}

QSGNode *Panadapter::updatePaintNode(QSGNode *oldNode,
                                     UpdatePaintNodeData *) {
    const int W = static_cast<int>(width());
    const int H = static_cast<int>(height());
    if (W < 2 || H < 2) {
        delete oldNode;
        return nullptr;
    }

    QSGNode          *root      = oldNode;
    QSGGeometryNode  *bgNode    = nullptr;
    QSGGeometryNode  *sheenNode = nullptr;
    QSGGeometryNode  *gridNode  = nullptr;
    QSGGeometryNode  *ribNode   = nullptr;
    QSGGeometryNode  *lineNode  = nullptr;
    QSGGeometryNode  *peakNode  = nullptr;

    if (!root) {
        root = new QSGNode;

        // Glass background: 8-vertex multi-stop vertical gradient strip.
        bgNode = new QSGGeometryNode;
        auto *bgGeo = new QSGGeometry(
            QSGGeometry::defaultAttributes_ColoredPoint2D(), 8);
        bgGeo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        bgNode->setGeometry(bgGeo);
        bgNode->setFlag(QSGNode::OwnsGeometry);
        bgNode->setMaterial(new QSGVertexColorMaterial);
        bgNode->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(bgNode);

        // Glass sheen: a 4-vertex diagonal highlight quad over the panel
        // (premultiplied white, brightest top-left -> transparent).
        sheenNode = new QSGGeometryNode;
        auto *sheenGeo = new QSGGeometry(
            QSGGeometry::defaultAttributes_ColoredPoint2D(), 4);
        sheenGeo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        sheenNode->setGeometry(sheenGeo);
        sheenNode->setFlag(QSGNode::OwnsGeometry);
        sheenNode->setMaterial(new QSGVertexColorMaterial);
        sheenNode->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(sheenNode);

        // Gridlines: line list (sized below); per-vertex alpha carries
        // the operator brightness so the same material blends them.
        gridNode = new QSGGeometryNode;
        auto *gridGeo = new QSGGeometry(
            QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        gridGeo->setDrawingMode(QSGGeometry::DrawLines);
        gridGeo->setLineWidth(1.0f);
        gridNode->setGeometry(gridGeo);
        gridNode->setFlag(QSGNode::OwnsGeometry);
        gridNode->setMaterial(new QSGVertexColorMaterial);
        gridNode->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(gridNode);

        // Fluid ribbon + fill: gradient triangle strip (sized below).
        ribNode = new QSGGeometryNode;
        auto *ribGeo = new QSGGeometry(
            QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        ribGeo->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        ribNode->setGeometry(ribGeo);
        ribNode->setFlag(QSGNode::OwnsGeometry);
        ribNode->setMaterial(new QSGVertexColorMaterial);
        ribNode->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(ribNode);

        // Trace stroke: the bright thin line ON TOP of the fill — a
        // connected line strip riding the curve top (sized below).
        lineNode = new QSGGeometryNode;
        auto *lineGeo = new QSGGeometry(
            QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        lineGeo->setDrawingMode(QSGGeometry::DrawLineStrip);
        lineGeo->setLineWidth(kLineWidth);
        lineNode->setGeometry(lineGeo);
        lineNode->setFlag(QSGNode::OwnsGeometry);
        lineNode->setMaterial(new QSGVertexColorMaterial);
        lineNode->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(lineNode);

        // Peak-hold overlay: drawn ON TOP of the trace.  Drawing mode +
        // vertex count are (re)set each frame from the chosen style
        // (line strip / dot quads / triangles).
        peakNode = new QSGGeometryNode;
        auto *peakGeo = new QSGGeometry(
            QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        peakGeo->setDrawingMode(QSGGeometry::DrawLineStrip);
        peakGeo->setLineWidth(1.4f);
        peakNode->setGeometry(peakGeo);
        peakNode->setFlag(QSGNode::OwnsGeometry);
        peakNode->setMaterial(new QSGVertexColorMaterial);
        peakNode->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(peakNode);
    } else {
        bgNode    = static_cast<QSGGeometryNode *>(root->childAtIndex(0));
        sheenNode = static_cast<QSGGeometryNode *>(root->childAtIndex(1));
        gridNode  = static_cast<QSGGeometryNode *>(root->childAtIndex(2));
        ribNode   = static_cast<QSGGeometryNode *>(root->childAtIndex(3));
        lineNode  = static_cast<QSGGeometryNode *>(root->childAtIndex(4));
        peakNode  = static_cast<QSGGeometryNode *>(root->childAtIndex(5));
    }

    // ---- glass background: multi-stop vertical gradient ----
    {
        auto *v = bgNode->geometry()->vertexDataAsColoredPoint2D();
        for (int s = 0; s < 4; ++s) {
            const float y = kBgStops[s].f * static_cast<float>(H);
            v[2 * s + 0].set(0.0f, y,
                             kBgStops[s].r, kBgStops[s].g, kBgStops[s].b, 255);
            v[2 * s + 1].set(static_cast<float>(W), y,
                             kBgStops[s].r, kBgStops[s].g, kBgStops[s].b, 255);
        }
        bgNode->markDirty(QSGNode::DirtyGeometry);
    }

    // ---- glass sheen: diagonal reflection highlight ----
    // Premultiplied white quad, brightest top-left, fading to nothing —
    // a subtle "light on glass" reflection.  alpha scales with the
    // operator glassSheen setting (0 = invisible).
    {
        auto *v = sheenNode->geometry()->vertexDataAsColoredPoint2D();
        const double aF = (glassSheen_ / 100.0) * 40.0;   // max alpha ~40
        auto A = [](double a) { return std::clamp(static_cast<int>(a + 0.5), 0, 255); };
        const int aTL = A(aF);          // top-left brightest
        const int aTR = A(aF * 0.35);
        const int aBL = A(aF * 0.15);
        const int aBR = 0;              // bottom-right transparent
        const float Wf = static_cast<float>(W), Hf = static_cast<float>(H);
        v[0].set(0.0f, 0.0f, aTL, aTL, aTL, aTL);   // TL (premult white)
        v[1].set(0.0f, Hf,   aBL, aBL, aBL, aBL);   // BL
        v[2].set(Wf,   0.0f, aTR, aTR, aTR, aTR);   // TR
        v[3].set(Wf,   Hf,   aBR, aBR, aBR, aBR);   // BR
        sheenNode->markDirty(QSGNode::DirtyGeometry);
    }

    // ---- gridlines (brightness = operator gridLevel) ----
    // QSGVertexColorMaterial blends with PREMULTIPLIED alpha, so the
    // RGB must be scaled by the alpha fraction or the line shows at full
    // colour for any non-zero alpha (only the background show-through
    // varies) — i.e. an on/off toggle instead of a brightness gradient.
    // Premultiplying makes the slider a true 0..bright fade.
    {
        const double aF = (gridLevel_ / 100.0) * (kGridMaxA / 255.0);
        const int A = static_cast<int>(aF * 255.0 + 0.5);
        QSGGeometry *gg = gridNode->geometry();
        if (A <= 0) {
            if (gg->vertexCount() != 0) {
                gg->allocate(0);
                gridNode->markDirty(QSGNode::DirtyGeometry);
            }
        } else {
            const int R = static_cast<int>(kGridR * aF + 0.5);
            const int G = static_cast<int>(kGridG * aF + 0.5);
            const int B = static_cast<int>(kGridB * aF + 0.5);
            const int nLines = (kGridHLines - 1) + (kGridVLines - 1);
            const int vcount = 2 * nLines;
            if (gg->vertexCount() != vcount) {
                gg->allocate(vcount);
            }
            auto *v = gg->vertexDataAsColoredPoint2D();
            int idx = 0;
            for (int j = 1; j < kGridHLines; ++j) {     // horizontal (dB)
                const float y = static_cast<float>(j) * H / kGridHLines;
                v[idx++].set(0.0f, y, R, G, B, A);
                v[idx++].set(static_cast<float>(W), y, R, G, B, A);
            }
            for (int k = 1; k < kGridVLines; ++k) {     // vertical (freq)
                const float x = static_cast<float>(k) * W / kGridVLines;
                v[idx++].set(x, 0.0f, R, G, B, A);
                v[idx++].set(x, static_cast<float>(H), R, G, B, A);
            }
            gridNode->markDirty(QSGNode::DirtyGeometry);
        }
    }

    // ---- trace ribbon + fill ----
    // 1) Peak-per-screen-column downsample (never drops a narrow signal).
    //    (Temporal EWMA smoothing, if enabled, is already applied to
    //    pix_ in onFrame — it operates over TIME, not frequency.)
    // 2) Build the translucent FILL + the bright trace STROKE.
    {
        const int n = static_cast<int>(pix_.size());

        // Step 1: per-column peak dB into scratch.
        colDb_.assign(static_cast<size_t>(W), -300.0f);
        for (int x = 0; x < W; ++x) {
            float db = -300.0f;
            if (n > 0) {
                int i0 = static_cast<int>(static_cast<qint64>(x) * n / W);
                int i1 = static_cast<int>(static_cast<qint64>(x + 1) * n / W);
                if (i1 <= i0) i1 = i0 + 1;
                if (i1 > n)   i1 = n;
                for (int i = i0; i < i1; ++i) {
                    if (pix_[static_cast<size_t>(i)] > db) {
                        db = pix_[static_cast<size_t>(i)];
                    }
                }
            }
            colDb_[static_cast<size_t>(x)] = db;
        }
        // Notch cut: carve the trace down to the noise floor
        // under each active manual notch (display-only — runs AFTER the
        // noise-floor estimate above so a deep notch can't fool it).
        if (engine_)
            engine_->carveNotches(colDb_.data(), W, noiseFloorDb_);
        const float *curve = colDb_.data();

        // Step 2: build the translucent FILL (triangle strip) AND the
        // bright trace STROKE (line strip) from the curve.  Fill is
        // skipped (0 verts) when disabled — leaving just the clean line.
        QSGGeometry *fillGeo = ribNode->geometry();
        const int wantFill = fillEnabled_ ? (2 * W) : 0;
        if (fillGeo->vertexCount() != wantFill) {
            fillGeo->allocate(wantFill);
        }
        QSGGeometry *lineGeo = lineNode->geometry();
        if (lineGeo->vertexCount() != W) {
            lineGeo->allocate(W);
        }
        auto *fv = fillEnabled_ ? fillGeo->vertexDataAsColoredPoint2D()
                                : nullptr;
        auto *lv = lineGeo->vertexDataAsColoredPoint2D();
        // Render with the EFFECTIVE range (auto-fit when on, else manual).
        const double effMin = autoScale_ ? autoScaler_.floorDb() : dbMin_;
        const double effMax = autoScale_ ? autoScaler_.ceilDb()  : dbMax_;
        const double range = (effMax - effMin);
        const double invRange = (range != 0.0) ? 1.0 / range : 1.0;
        // Trace colour: SOLID (one picked colour) or BY-STRENGTH (colour
        // tracks signal level via the palette LUT — the intensity
        // gradient old Lyra couldn't achieve).  Premultiplied alpha:
        // a BRIGHT line (kLineA) over a TRANSLUCENT fill (kFillTopA)
        // that fades to a transparent baseline, so the trace clearly
        // pops above its fill.
        const bool byStrength = (traceMode_ == 1);
        // palette index past the presets == operator's custom ramp colour.
        const int palCount = lyra::palettes::names().size();
        const bool customStrength = byStrength && paletteIndex_ >= palCount;
        const lyra::palettes::Lut &pal = lyra::palettes::lut(paletteIndex_);
        for (int x = 0; x < W; ++x) {
            double t = (static_cast<double>(curve[x]) - effMin) * invRange;
            t = std::clamp(t, 0.0, 1.0);
            int rr = traceColor_.red();
            int gg = traceColor_.green();
            int bb = traceColor_.blue();
            if (customStrength) {
                rampColor(strengthColor_, t, rr, gg, bb);   // dark->hue->bright
            } else if (byStrength) {
                const int li = std::clamp(static_cast<int>(t * 255.0 + 0.5), 0, 255);
                const auto &pc = pal[static_cast<size_t>(li)];
                rr = pc[0]; gg = pc[1]; bb = pc[2];
            }
            const float xf = static_cast<float>(x);
            const float yTop = static_cast<float>(H - t * H);
            // Translucent fill body (only when enabled).  In SOLID trace
            // mode the fill uses its OWN colour (fillColor_) so it can
            // differ from the line; in by-strength mode it follows the
            // intensity LUT (rr/gg/bb) for the heat-map look.
            if (fv) {
                int fr = rr, fgc = gg, fbl = bb;
                if (!byStrength) {
                    fr = fillColor_.red();
                    fgc = fillColor_.green();
                    fbl = fillColor_.blue();
                }
                fv[2 * x + 0].set(xf, yTop,
                                  fr * kFillTopA / 255, fgc * kFillTopA / 255,
                                  fbl * kFillTopA / 255, kFillTopA);
                fv[2 * x + 1].set(xf, static_cast<float>(H), 0, 0, 0, kFillBotA);
            }
            // Bright stroke on the curve.
            lv[x].set(xf, yTop,
                      rr * kLineA / 255, gg * kLineA / 255,
                      bb * kLineA / 255, kLineA);
        }
        ribNode->markDirty(QSGNode::DirtyGeometry);
        lineNode->markDirty(QSGNode::DirtyGeometry);
    }

    // ---- peak-hold overlay (line / dots / triangles) ----
    // Same per-column peak reduction + dB->y mapping as the trace, drawn
    // in the operator's chosen style with a fixed-alpha peak colour.  The
    // hold buffer (peakDb_) is maintained on the GUI thread in onFrame;
    // Qt blocks the GUI thread during this sync, so reading it is safe.
    {
        QSGGeometry *pg = peakNode->geometry();
        const int pn = static_cast<int>(peakDb_.size());
        // Clip to the RX passband (old-Lyra parity) — peaks only render
        // for the columns inside the tuned filter window.
        int pbLo = 0, pbHi = W;
        passbandPx(W, &pbLo, &pbHi);
        const int pw = pbHi - pbLo;
        const bool show = peakEnabled_ && peakHoldSecs_ != 0.0 && pn > 0 && pw >= 2;
        if (!show) {
            if (pg->vertexCount() != 0) {
                pg->allocate(0);
                peakNode->markDirty(QSGNode::DirtyGeometry);
            }
        } else {
            peakColDb_.assign(static_cast<size_t>(W), -300.0f);
            for (int x = pbLo; x < pbHi; ++x) {
                int i0 = static_cast<int>(static_cast<qint64>(x) * pn / W);
                int i1 = static_cast<int>(static_cast<qint64>(x + 1) * pn / W);
                if (i1 <= i0) i1 = i0 + 1;
                if (i1 > pn) i1 = pn;
                float db = -300.0f;
                for (int i = i0; i < i1; ++i)
                    if (peakDb_[static_cast<size_t>(i)] > db)
                        db = peakDb_[static_cast<size_t>(i)];
                peakColDb_[static_cast<size_t>(x)] = db;
            }
            const double effMin = autoScale_ ? autoScaler_.floorDb() : dbMin_;
            const double effMax = autoScale_ ? autoScaler_.ceilDb()  : dbMax_;
            const double range = (effMax - effMin);
            const double invRange = (range != 0.0) ? 1.0 / range : 1.0;
            const int A  = kPeakA;
            const int pr = peakColor_.red()   * A / 255;
            const int pgc = peakColor_.green() * A / 255;
            const int pb = peakColor_.blue()  * A / 255;
            auto yOf = [&](int x) {
                double t = (static_cast<double>(peakColDb_[static_cast<size_t>(x)])
                            - effMin) * invRange;
                t = std::clamp(t, 0.0, 1.0);
                return static_cast<float>(H - t * H);
            };
            if (peakStyle_ == 0) {                     // Line (passband span)
                pg->setDrawingMode(QSGGeometry::DrawLineStrip);
                pg->setLineWidth(1.4f);
                if (pg->vertexCount() != pw) pg->allocate(pw);
                auto *v = pg->vertexDataAsColoredPoint2D();
                int k = 0;
                for (int x = pbLo; x < pbHi; ++x)
                    v[k++].set(static_cast<float>(x), yOf(x), pr, pgc, pb, A);
            } else if (peakStyle_ == 2) {              // Triangles (every 4 px)
                pg->setDrawingMode(QSGGeometry::DrawTriangles);
                const int step = 4;
                const int cnt = (pw + step - 1) / step;
                const int vc = cnt * 3;
                if (pg->vertexCount() != vc) pg->allocate(vc);
                auto *v = pg->vertexDataAsColoredPoint2D();
                int k = 0;
                for (int x = pbLo; x < pbHi; x += step) {
                    const float xf = static_cast<float>(x);
                    const float y = yOf(x);
                    v[k++].set(xf - 3.0f, y - 4.0f, pr, pgc, pb, A);
                    v[k++].set(xf + 3.0f, y - 4.0f, pr, pgc, pb, A);
                    v[k++].set(xf,        y,        pr, pgc, pb, A);
                }
            } else {                                   // Dots (small quads, every 2 px)
                pg->setDrawingMode(QSGGeometry::DrawTriangles);
                const int step = 2;
                const int cnt = (pw + step - 1) / step;
                const int vc = cnt * 6;
                if (pg->vertexCount() != vc) pg->allocate(vc);
                auto *v = pg->vertexDataAsColoredPoint2D();
                const float r = 1.8f;
                int k = 0;
                for (int x = pbLo; x < pbHi; x += step) {
                    const float xf = static_cast<float>(x);
                    const float y = yOf(x);
                    v[k++].set(xf - r, y - r, pr, pgc, pb, A);
                    v[k++].set(xf - r, y + r, pr, pgc, pb, A);
                    v[k++].set(xf + r, y - r, pr, pgc, pb, A);
                    v[k++].set(xf + r, y - r, pr, pgc, pb, A);
                    v[k++].set(xf - r, y + r, pr, pgc, pb, A);
                    v[k++].set(xf + r, y + r, pr, pgc, pb, A);
                }
            }
            peakNode->markDirty(QSGNode::DirtyGeometry);
        }
    }

    return root;
}

} // namespace lyra::ui

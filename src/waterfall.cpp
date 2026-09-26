// Lyra — waterfall (rolling spectrogram).  See waterfall.h.

#include "waterfall.h"

#include "wdsp_engine.h"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
#include <QTimer>

#include <algorithm>
#include <cstring>

namespace lyra::ui {

Waterfall::Waterfall(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    rebuildLut();

    // Fixed-cadence pump, same model as the panadapter (a plain QTimer,
    // NOT afterAnimating — that doesn't fire continuously inside a
    // QQuickWidget).  Started/stopped with scene membership.
    frameTimer_ = new QTimer(this);
    frameTimer_->setTimerType(Qt::PreciseTimer);
    frameTimer_->setInterval(1000 / std::max(1, targetFps_));
    connect(frameTimer_, &QTimer::timeout, this, &Waterfall::onFrame);

    rowClock_.start();
}

QObject *Waterfall::engineObj() const {
    return reinterpret_cast<QObject *>(engine_);
}

void Waterfall::setEngineObj(QObject *o) {
    auto *e = qobject_cast<lyra::dsp::WdspEngine *>(o);
    if (e == engine_) {
        return;
    }
    engine_ = e;
    img_ = QImage();   // re-sized on the next frame to the bin count
    emit engineChanged();
}

void Waterfall::setDbMin(double v) {
    if (v != dbMin_) { dbMin_ = v; emit rangeChanged(); }
}

void Waterfall::setDbMax(double v) {
    if (v != dbMax_) { dbMax_ = v; emit rangeChanged(); }
}

void Waterfall::setTargetFps(int v) {
    v = std::clamp(v, 1, 1000);
    if (v != targetFps_) {
        targetFps_ = v;
        if (frameTimer_) frameTimer_->setInterval(1000 / targetFps_);
        emit targetFpsChanged();
    }
}

void Waterfall::setSpeed(int v) {
    v = std::clamp(v, 1, 120);
    if (v != speed_) { speed_ = v; emit speedChanged(); }
}

void Waterfall::setAutoScale(bool v) {
    if (v != autoScale_) {
        autoScale_ = v;
        if (v) { autoScaler_.reset(); lastAutoMs_ = -1; }   // fresh fit
        emit autoScaleChanged();
    }
}

void Waterfall::setPalette(int v) {
    if (v < 0) v = 0;
    if (v != paletteIndex_) {
        paletteIndex_ = v;
        rebuildLut();
        emit paletteChanged();
    }
}

void Waterfall::setStrengthColor(const QColor &c) {
    if (c != strengthColor_) {
        strengthColor_ = c;
        rebuildLut();
        emit strengthColorChanged();
    }
}

void Waterfall::rebuildLut() {
    // Preset palette index -> its LUT.  An index past the presets selects
    // the operator's custom dark->hue->bright ramp (same convention +
    // ramp maths as the by-strength trace, so the two displays match).
    const int presetCount = lyra::palettes::names().count();
    if (paletteIndex_ < presetCount) {
        lut_ = lyra::palettes::lut(paletteIndex_);
        return;
    }
    const QColor &b = strengthColor_;
    for (int i = 0; i < 256; ++i) {
        const double t = i / 255.0;
        int r, g, bl;
        if (t <= 0.65) {                       // black -> base hue
            const double f = t / 0.65;
            r  = int(b.red()   * f);
            g  = int(b.green() * f);
            bl = int(b.blue()  * f);
        } else {                               // base -> toward white
            const double f = (t - 0.65) / 0.35;
            r  = int(b.red()   + (255 - b.red())   * f * 0.7);
            g  = int(b.green() + (255 - b.green()) * f * 0.7);
            bl = int(b.blue()  + (255 - b.blue())  * f * 0.7);
        }
        lut_[static_cast<size_t>(i)] = {
            static_cast<unsigned char>(std::clamp(r, 0, 255)),
            static_cast<unsigned char>(std::clamp(g, 0, 255)),
            static_cast<unsigned char>(std::clamp(bl, 0, 255))};
    }
}

void Waterfall::itemChange(ItemChange change, const ItemChangeData &value) {
    if (change == ItemSceneChange && frameTimer_) {
        if (value.window) frameTimer_->start();
        else              frameTimer_->stop();
    }
    QQuickItem::itemChange(change, value);
}

void Waterfall::onFrame() {
    if (!engine_ || !isVisible()) {
        return;
    }
    {
        const qint64 now = rowClock_.elapsed();
        const qint64 minMs = 1000 / std::max(1, targetFps_);
        if (lastFrameMs_ >= 0 && (now - lastFrameMs_) < (minMs / 2))
            return;
        lastFrameMs_ = now;
    }
    const int n = engine_->spectrumPixelCount();
    if (n < 2) {
        return;
    }
    // (Re)allocate the history buffer to the analyzer's bin count.
    if (img_.width() != n || img_.height() != kHistory) {
        img_ = QImage(n, kHistory, QImage::Format_RGBX8888);
        img_.fill(Qt::black);
    }
    if (static_cast<int>(row_.size()) != n) {
        row_.assign(static_cast<size_t>(n), -200.0f);
    }
    if (static_cast<int>(pendingMax_.size()) != n) {
        pendingMax_.assign(static_cast<size_t>(n), -200.0f);
    }
    // §15.29 C1 — switched copySpectrum (pixout=0, panadapter's 30 ms
    // IIR cache) → copyWaterfallSpectrum (pixout=1, waterfall's
    // 120 ms IIR cache).  Effective only during TX state — WdspEngine
    // falls through to pixout=0 in RX (current shared behaviour) per
    // §15.29 C2 deferral.
    engine_->copyWaterfallSpectrum(row_.data(), n);

    // Auto-fit the dB range from the live spectrum (throttled to ~5 Hz),
    // independent of scroll speed / fps.  Runs every frame (not just on a
    // row push) so the range keeps tracking between rows.
    if (autoScale_) {
        const qint64 nowA = rowClock_.elapsed();
        if (lastAutoMs_ < 0 || (nowA - lastAutoMs_) >= 200) {
            lastAutoMs_ = nowA;
            autoScaler_.feed(row_.data(), n);
        }
    }

    // Peak-hold every incoming frame into the pending row so that, at
    // slow scroll speeds, a brief signal between row pushes still shows.
    for (int x = 0; x < n; ++x) {
        pendingMax_[static_cast<size_t>(x)] =
            std::max(pendingMax_[static_cast<size_t>(x)],
                     row_[static_cast<size_t>(x)]);
    }

    // Push history rows at the operator's scroll rate.  Scroll speed is
    // DECOUPLED from the render frame rate: if more than one row-interval
    // has elapsed since the last push (e.g. 120 rows/s while rendering at
    // 60 fps), push that many rows this frame (capped) so the waterfall
    // can scroll faster than the frame rate — old-Lyra "multi-emit".
    const qint64 now = rowClock_.elapsed();
    const qint64 rowIntervalMs =
        std::max<qint64>(1, 1000 / std::max(1, speed_));
    if (lastRowMs_ < 0) {
        lastRowMs_ = now - rowIntervalMs;   // push one on the first frame
    }
    int rowsToPush = static_cast<int>((now - lastRowMs_) / rowIntervalMs);
    if (rowsToPush < 1) {
        return;   // accumulate; not time for a new row yet
    }
    // Cap: never more than the history depth, and clamp per-frame so a
    // stall (minimised window, etc.) can't dump a huge burst at once.
    rowsToPush = std::min(rowsToPush, std::min(kHistory, 8));
    lastRowMs_ += static_cast<qint64>(rowsToPush) * rowIntervalMs;

    // Scroll the whole history DOWN by rowsToPush rows.
    const int bpl = img_.bytesPerLine();
    uchar *base = img_.bits();
    if (rowsToPush < kHistory) {
        std::memmove(base + static_cast<size_t>(bpl) * rowsToPush, base,
                     static_cast<size_t>(bpl) * (kHistory - rowsToPush));
    }

    // Paint the peak-held frame into row 0 via the palette LUT, using
    // the auto-fit range when enabled (else the operator's manual one).
    const double effMin = autoScale_ ? autoScaler_.floorDb() : dbMin_;
    const double effMax = autoScale_ ? autoScaler_.ceilDb()  : dbMax_;
    const double span = (effMax > effMin) ? (effMax - effMin) : 1.0;
    uchar *row0 = base;   // scanLine(0)
    // Notch cut: drop the notched columns to the bottom of
    // the colour scale so the cut shows as a dark vertical stripe in the
    // waterfall history (display-only; auto-scale fed the un-carved row).
    if (engine_)
        engine_->carveNotches(pendingMax_.data(), n, effMin);
    for (int x = 0; x < n; ++x) {
        double t = (static_cast<double>(pendingMax_[static_cast<size_t>(x)])
                    - effMin) / span;
        t = std::clamp(t, 0.0, 1.0);
        const auto &c = lut_[static_cast<size_t>(int(t * 255.0 + 0.5))];
        uchar *px = row0 + x * 4;
        px[0] = c[0];
        px[1] = c[1];
        px[2] = c[2];
        px[3] = 255;
    }
    // Duplicate row 0 into the other freshly-scrolled rows (a fast scroll
    // pushes multiple rows from the same accumulated frame).
    for (int r = 1; r < rowsToPush; ++r) {
        std::memcpy(base + static_cast<size_t>(bpl) * r, row0,
                    static_cast<size_t>(bpl));
    }
    // Reset the accumulator for the next row interval.
    std::fill(pendingMax_.begin(), pendingMax_.end(), -200.0f);
    dirty_ = true;
    update();
}

QSGNode *Waterfall::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
    if (img_.isNull() || width() < 2 || height() < 2) {
        delete oldNode;
        return nullptr;
    }
    auto *node = static_cast<QSGImageNode *>(oldNode);
    if (!node) {
        node = window()->createImageNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        // Mipmaps so the tall history image (kHistory rows) squashed into a
        // short pane — e.g. after dragging the splitter down — minifies
        // smoothly instead of aliasing into shifting horizontal lines.
        node->setMipmapFiltering(QSGTexture::Linear);
        dirty_ = true;   // force the first upload
    }
    // Re-upload the history texture ONLY when its content actually changed
    // (a new row was pushed).  A pure resize — dragging the splitter —
    // leaves dirty_ false, so we just restretch the existing texture via
    // setRect instead of re-uploading the whole n×kHistory image on every
    // geometry event.  That's what made the drag jerk.
    if (dirty_ || !node->texture()) {
        QSGTexture *tex = window()->createTextureFromImage(
            img_, QQuickWindow::TextureHasMipmaps);
        // The history image is wide (bins) but gets crushed VERTICALLY when
        // the pane is short — an asymmetric squash that isotropic mipmaps
        // resolve poorly (residual horizontal banding).  Anisotropic
        // filtering samples along the crushed axis, so a small waterfall
        // stays clean.  Cost is negligible for one textured quad.
        tex->setFiltering(QSGTexture::Linear);
        tex->setMipmapFiltering(QSGTexture::Linear);
        tex->setAnisotropyLevel(QSGTexture::Anisotropy16x);
        node->setTexture(tex);   // ownsTexture(true) deletes the previous one
        dirty_ = false;
    }
    node->setRect(boundingRect());
    return node;
}

} // namespace lyra::ui

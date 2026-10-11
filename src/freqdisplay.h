// Lyra — VFO tuning core (RX1 / VFO B readout input).
//
// Started as a C++ port of old Lyra's led_freq.py and used to paint the
// amber readout itself.  Painting now belongs to the selectable VFO styles
// (src/qml/VfoStyle*.qml); this item is laid transparently over the style's
// tune area by VfoBox.qml and keeps ALL the tuning behaviour so every style
// tunes identically: click a digit to select its place, wheel over a digit
// tunes THAT place (10^place), wheel elsewhere in the area tunes by the Step
// combo's value (externalStepHz), ←/→ move the selection, ↑/↓ nudge it, and
// double-click requests typed entry (the host pops a field and calls
// setFreqHz with parseFreqInput's result).
//
// The style reports where its digits are (digitRects, index = place); the
// style draws the selection from selectedDigit.  freqHz mirrors the
// operator's tuned frequency; user tuning emits freqEdited(hz), which the
// host wires to Stream.  setFreqHz() does NOT emit freqEdited, so there's no
// set→edit→set feedback loop.

#pragma once

#include <QQuickItem>
#include <QRectF>
#include <QVariantList>

#include <vector>

namespace lyra::ui {

class FreqDisplay : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int freqHz READ freqHz WRITE setFreqHz NOTIFY freqHzChanged)
    // Mouse-wheel step (Hz) used when the cursor isn't over a specific
    // digit — driven by the panel's Step combo.  0 = per-digit only.
    Q_PROPERTY(int externalStepHz READ externalStepHz WRITE setExternalStepHz
               NOTIFY externalStepHzChanged)
    // Per-digit hit rects from the active VFO style, index = place
    // (0 = Hz units … 9 = 1 GHz), in this item's coordinates.  An empty or
    // missing rect = that digit isn't shown and can't be clicked.
    Q_PROPERTY(QVariantList digitRects READ digitRects WRITE setDigitRects
               NOTIFY digitRectsChanged)
    // Selected place, drawn by the style; click / wheel / ←→ change it.
    Q_PROPERTY(int selectedDigit READ selectedDigit NOTIFY selectedDigitChanged)

public:
    explicit FreqDisplay(QQuickItem *parent = nullptr);

    int  freqHz() const { return freqHz_; }
    void setFreqHz(int hz);              // external set (no freqEdited)
    int  externalStepHz() const { return externalStepHz_; }
    void setExternalStepHz(int hz);
    QVariantList digitRects() const { return digitRectsVar_; }
    void setDigitRects(const QVariantList &rects);
    int  selectedDigit() const { return selected_; }

    // Parse a free-form frequency entry (MHz decimal / Hz with
    // separators / bare) into Hz, or -1 if invalid.  Mirrors old Lyra's
    // parse_freq_input.  Q_INVOKABLE so the QML typed-entry field uses it.
    Q_INVOKABLE int parseFreqInput(const QString &text) const;

signals:
    void freqHzChanged();
    void externalStepHzChanged();
    void digitRectsChanged();
    void selectedDigitChanged();
    void freqEdited(int hz);     // user tuned (wheel / keys) — wire to Stream
    void editRequested();        // double-click — host shows a typed-entry field

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void changeFreq(int deltaHz);        // tune + emit freqEdited
    void setSelected(int place);

    static constexpr int kNDigits = 10;
    // Display/RF ceiling (VFO LED + wheel).  HF native is ~55 MHz; Xvtr
    // RF (2 m / 70 cm / 23 cm) sits well above that.  Cap is signed-int
    // max so freqHz_ / Q_PROPERTY(int) stay valid (~2.147 GHz).
    static constexpr int kMaxHz   = 2'147'483'647;

    int  freqHz_         = 7'074'000;
    int  selected_       = 3;            // default = 1 kHz place
    int  externalStepHz_ = 0;
    int  wheelAccum_     = 0;            // partial wheel delta (wheel_steps.h)
    QVariantList        digitRectsVar_;  // as set from QML (change detection)
    std::vector<QRectF> rects_;          // parsed, index = place (vfo_digit_hit.h)
};

} // namespace lyra::ui

// Lyra — LED-style frequency display (RX1 VFO readout + tuner).
//
// A faithful C++/Qt-Quick port of old Lyra's led_freq.py: large amber
// digits on black in MMMM.kkk.hhh format (10 digits — HF through 23 cm
// transverter RF; signed-int ceiling ~2.147 GHz), with a ghost "8"
// behind each digit (unlit-segment look) and MHz/kHz/Hz group labels.
// Click a digit to select its place (cyan underline); mouse-wheel over
// a digit tunes THAT place (10^place), or — away from a digit — by the
// Step combo's value (externalStepHz); arrow keys nudge the selected
// place / move the selection.  Double-click requests direct typed entry
// (the host QML pops a field and calls setFreqHz with the parsed value).
//
// freqHz mirrors the operator's tuned frequency (set externally from
// Stream.rx1FreqHz); user tuning emits freqEdited(hz) which the host
// wires to Stream.setRx1FreqHz.  setFreqHz() does NOT emit freqEdited,
// so there's no set→edit→set feedback loop.

#pragma once

#include <QQuickPaintedItem>
#include <QRectF>

#include <utility>
#include <vector>

namespace lyra::ui {

class FreqDisplay : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(int freqHz READ freqHz WRITE setFreqHz NOTIFY freqHzChanged)
    // Mouse-wheel step (Hz) used when the cursor isn't over a specific
    // digit — driven by the panel's Step combo.  0 = per-digit only.
    Q_PROPERTY(int externalStepHz READ externalStepHz WRITE setExternalStepHz
               NOTIFY externalStepHzChanged)

public:
    explicit FreqDisplay(QQuickItem *parent = nullptr);

    int  freqHz() const { return freqHz_; }
    void setFreqHz(int hz);              // external set (no freqEdited)
    int  externalStepHz() const { return externalStepHz_; }
    void setExternalStepHz(int hz);

    // Parse a free-form frequency entry (MHz decimal / Hz with
    // separators / bare) into Hz, or -1 if invalid.  Mirrors old Lyra's
    // parse_freq_input.  Q_INVOKABLE so the QML typed-entry field uses it.
    Q_INVOKABLE int parseFreqInput(const QString &text) const;

    void paint(QPainter *p) override;

signals:
    void freqHzChanged();
    void externalStepHzChanged();
    void freqEdited(int hz);     // user tuned (wheel / keys) — wire to Stream
    void editRequested();        // double-click — host shows a typed-entry field

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void changeFreq(int deltaHz);        // tune + emit freqEdited

    static constexpr int kNDigits = 10;
    // Display/RF ceiling (VFO LED + wheel).  HF native is ~55 MHz; Xvtr
    // RF (2 m / 70 cm / 23 cm) sits well above that.  Cap is signed-int
    // max so freqHz_ / Q_PROPERTY(int) stay valid (~2.147 GHz).
    static constexpr int kMaxHz   = 2'147'483'647;

    int  freqHz_         = 7'074'000;
    int  selected_       = 3;            // default = 1 kHz place
    int  externalStepHz_ = 0;
    // (placeValue, on-screen rect) per painted digit, for hit-testing.
    std::vector<std::pair<int, QRectF>> digitRects_;
};

} // namespace lyra::ui

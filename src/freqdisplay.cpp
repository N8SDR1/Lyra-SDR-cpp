// Lyra — LED frequency display.  See freqdisplay.h.  Ported from old
// Lyra's led_freq.py (FrequencyDisplay) — same look + interactions.

#include "freqdisplay.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QWheelEvent>

#include <algorithm>

namespace lyra::ui {

namespace {
const QColor kBg      {4, 4, 6};
const QColor kAmber   {255, 171, 71};
const QColor kAmberDim {60, 40, 15};      // unlit-segment ghost
const QColor kSelect  {0, 229, 255};      // cyan selected-digit
const QColor kFaint   {185, 202, 220};    // unit labels (MHz/kHz/Hz) — bright + readable

int placeStep(int idx) {   // 10^idx, idx in [0, 9]
    int s = 1;
    for (int i = 0; i < idx; ++i) s *= 10;
    return s;
}

int wheelTicks(int angleDeltaY, int pixelDeltaY) {
    if (angleDeltaY <= -120 || angleDeltaY >= 120)
        return angleDeltaY / 120;
    if (angleDeltaY != 0)
        return angleDeltaY > 0 ? 1 : -1;
    if (pixelDeltaY != 0)
        return pixelDeltaY > 0 ? 1 : -1;
    return 0;
}

FreqDisplay *g_ledUnderMouse = nullptr;
} // namespace

FreqDisplay *FreqDisplay::ledUnderMouse() {
    return g_ledUnderMouse;
}

FreqDisplay::FreqDisplay(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, false);
    setAntialiasing(true);
}

FreqDisplay::~FreqDisplay() {
    if (g_ledUnderMouse == this)
        g_ledUnderMouse = nullptr;
}

void FreqDisplay::setFreqHz(int hz) {
    hz = std::clamp(hz, 0, kMaxHz);
    if (hz == freqHz_) {
        return;
    }
    freqHz_ = hz;
    update();
    emit freqHzChanged();           // NOTE: no freqEdited — external set
}

void FreqDisplay::setExternalStepHz(int hz) {
    hz = std::max(0, hz);
    if (hz == externalStepHz_) {
        return;
    }
    externalStepHz_ = hz;
    emit externalStepHzChanged();
}

void FreqDisplay::setStickyWheel(bool on) {
    if (on == stickyWheel_)
        return;
    stickyWheel_ = on;
    emit stickyWheelChanged();
}

void FreqDisplay::changeFreq(int deltaHz) {
    const int nf = std::clamp(freqHz_ + deltaHz, 0, kMaxHz);
    if (nf == freqHz_) {
        return;
    }
    freqHz_ = nf;
    update();
    emit freqHzChanged();
    emit freqEdited(nf);            // user tuned — host pushes to Stream
}

int FreqDisplay::parseFreqInput(const QString &text) const {
    QString s = text;
    s.remove(' ');
    if (s.isEmpty()) {
        return -1;
    }
    // 2+ separators (any mix of , and .) = thousands separators → Hz.
    if ((s.count(QLatin1Char(',')) + s.count(QLatin1Char('.'))) > 1) {
        QString digits = s;
        digits.remove(QLatin1Char(',')).remove(QLatin1Char('.'));
        bool ok = false;
        const qlonglong v = digits.toLongLong(&ok);
        if (!ok) return -1;
        return static_cast<int>(std::clamp(v, qlonglong(0),
                                           qlonglong(FreqDisplay::kMaxHz)));
    }
    // Single separator = decimal point (MHz); comma is the Euro decimal.
    s.replace(QLatin1Char(','), QLatin1Char('.'));
    if (s.contains(QLatin1Char('.'))) {
        bool ok = false;
        const double mhz = s.toDouble(&ok);
        if (!ok) return -1;
        const qlonglong hz = qRound64(mhz * 1.0e6);
        return static_cast<int>(std::clamp(hz, qlonglong(0),
                                           qlonglong(FreqDisplay::kMaxHz)));
    }
    bool ok = false;
    const qlonglong n = s.toLongLong(&ok);
    if (!ok) {
        return -1;
    }
    qlonglong hz = n;
    if (n < 100)        hz = n * 1'000'000;   // MHz
    else if (n < 100'000) hz = n * 1'000;     // kHz
    return static_cast<int>(std::clamp(hz, qlonglong(0),
                                       qlonglong(FreqDisplay::kMaxHz)));
}

void FreqDisplay::paint(QPainter *p) {
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::TextAntialiasing, true);

    const qreal w = width();
    const qreal h = height();
    p->fillRect(QRectF(0, 0, w, h), kBg);

    QFont font;
    font.setFamilies({QStringLiteral("Share Tech Mono"),
                      QStringLiteral("Consolas"),
                      QStringLiteral("Courier New")});
    font.setPixelSize(std::max(24, static_cast<int>(h * 0.64)));
    font.setWeight(QFont::Bold);
    p->setFont(font);
    const QFontMetricsF fm(font);

    const qreal digitW = fm.horizontalAdvance(QLatin1Char('0'));
    const qreal dotW   = fm.horizontalAdvance(QLatin1Char('.'));
    const qreal totalW = kNDigits * digitW + 2 * dotW;
    qreal x = (w - totalW) / 2.0;
    const qreal baseY = (h + fm.ascent() - fm.descent()) / 2.0;

    const QString freqStr =
        QStringLiteral("%1").arg(freqHz_, kNDigits, 10, QLatin1Char('0'));
    digitRects_.clear();

    for (int i = 0; i < kNDigits; ++i) {
        const QChar ch = freqStr[i];
        const int placeIdx = kNDigits - 1 - i;   // leftmost char = highest place
        // Include the cyan underline and a little slop so wheel/click on
        // the mark still counts as that digit.
        digitRects_.emplace_back(
            placeIdx,
            QRectF(x - 1, baseY - fm.ascent() - 2, digitW + 2, fm.height() + 8));

        // Ghost "8" behind the digit.
        p->setPen(QPen(kAmberDim, 1));
        p->drawText(QPointF(x, baseY), QStringLiteral("8"));

        // Leading-zero suppression on the high (MHz) digits.
        bool allZero = true;
        for (int j = 0; j <= i; ++j) {
            if (freqStr[j] != QLatin1Char('0')) { allZero = false; break; }
        }
        const QColor pen =
            (placeIdx >= 6 && allZero) ? kAmberDim : kAmber;
        p->setPen(QPen(pen, 1));
        p->drawText(QPointF(x, baseY), QString(ch));

        // Selected-digit cyan underline.
        if (placeIdx == selected_) {
            p->setPen(QPen(kSelect, 3));
            p->drawLine(QPointF(x + 1, baseY + 3),
                        QPointF(x + digitW - 1, baseY + 3));
        }

        x += digitW;
        if (i == 3 || i == 6) {                  // MMMM.kkk.hhh separators
            p->setPen(QPen(kAmber, 1));
            p->drawText(QPointF(x, baseY), QStringLiteral("."));
            x += dotW;
        }
    }

    // Group unit labels (MHz / kHz / Hz).  MHz group is four digits.
    QFont unitFont;
    unitFont.setFamilies({QStringLiteral("Consolas")});
    unitFont.setPixelSize(std::max(11, static_cast<int>(h * 0.19)));
    unitFont.setBold(true);
    p->setFont(unitFont);
    const QFontMetricsF ufm(unitFont);
    const qreal unitY = h - 4;
    const qreal xs = (w - totalW) / 2.0;
    const qreal groupStarts[3] = {
        xs,
        xs + 4 * digitW + dotW,
        xs + 7 * digitW + 2 * dotW,
    };
    const qreal groupWidths[3] = {4 * digitW, 3 * digitW, 3 * digitW};
    const char *labels[3] = {"MHz", "kHz", "Hz"};
    p->setPen(QPen(kFaint, 1));
    for (int g = 0; g < 3; ++g) {
        const qreal center = groupStarts[g] + groupWidths[g] / 2.0;
        const QString lab = QString::fromLatin1(labels[g]);
        const qreal tw = ufm.horizontalAdvance(lab);
        p->drawText(QPointF(center - tw / 2.0, unitY), lab);
    }
}

void FreqDisplay::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) {
        e->ignore();
        return;
    }
    g_ledUnderMouse = this;
    for (const auto &[idx, rect] : digitRects_) {
        if (rect.contains(e->position())) {
            selected_ = idx;
            update();
            forceActiveFocus();
            e->accept();
            return;
        }
    }
    forceActiveFocus();
    e->accept();
}

void FreqDisplay::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        emit editRequested();
        e->accept();
        return;
    }
    e->ignore();
}

void FreqDisplay::hoverEnterEvent(QHoverEvent *e) {
    g_ledUnderMouse = this;
    QQuickPaintedItem::hoverEnterEvent(e);
}

void FreqDisplay::hoverLeaveEvent(QHoverEvent *e) {
    if (g_ledUnderMouse == this)
        g_ledUnderMouse = nullptr;
    QQuickPaintedItem::hoverLeaveEvent(e);
}

void FreqDisplay::handleWheel(int angleDeltaY, int pixelDeltaY, qreal x, qreal y) {
    g_ledUnderMouse = this;
    const int ticks = wheelTicks(angleDeltaY, pixelDeltaY);
    if (ticks == 0)
        return;
    applyWheelAt(ticks, QPointF(x, y));
}

void FreqDisplay::nudgeByTicks(int ticks) {
    if (ticks == 0)
        return;
    applyWheelAt(ticks, QPointF(-1, -1));
}

void FreqDisplay::wheelEvent(QWheelEvent *e) {
    g_ledUnderMouse = this;
    const int ticks = wheelTicks(e->angleDelta().y(), e->pixelDelta().y());
    if (ticks == 0) {
        e->ignore();
        return;
    }
    applyWheelAt(ticks, e->position());
    e->accept();
}

void FreqDisplay::applyWheelAt(int ticks, QPointF pos) {
    int hover = -1;
    for (const auto &[idx, rect] : digitRects_) {
        if (rect.contains(pos)) {
            hover = idx;
            selected_ = idx;
            update();
            break;
        }
    }
    const int place = hover >= 0 ? hover : std::max(0, selected_);
    const int step = placeStep(place);
    changeFreq(ticks * step);
}

void FreqDisplay::keyPressEvent(QKeyEvent *e) {
    const int step = placeStep(std::max(0, selected_));
    switch (e->key()) {
    case Qt::Key_Up:    changeFreq(step);  break;
    case Qt::Key_Down:  changeFreq(-step); break;
    case Qt::Key_Left:  selected_ = std::min(kNDigits - 1, selected_ + 1);
                        update(); break;
    case Qt::Key_Right: selected_ = std::max(0, selected_ - 1);
                        update(); break;
    default:            QQuickPaintedItem::keyPressEvent(e); return;
    }
    e->accept();
}

} // namespace lyra::ui

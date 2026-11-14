#include "helpcard.h"
#include "helphint.h"
#include "mainwindow.h"
#include "prefs.h"

#include <QDockWidget>
#include <QEvent>
#include <QHash>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

namespace lyra::ui {

namespace {
constexpr int kCardWidth = 280;
constexpr int kMaxBodyH = 140;
constexpr int kTailH = 16;
constexpr int kTailW = 22;
constexpr int kRadius = 18;
constexpr int kStroke = 3;
const char *kPosX = "ui/helpCardX";
const char *kPosY = "ui/helpCardY";
const char *kParked = "ui/helpCardParked";
}

HelpCard::HelpCard(MainWindow *host, HelpHint *hint, Prefs *prefs,
                   QHash<QString, QDockWidget *> *docks)
    : QFrame(host),
      host_(host), hint_(hint), prefs_(prefs), docks_(docks) {
    setObjectName(QStringLiteral("LyraHelpCard"));
    // Child overlay of the main window — NOT Qt::Tool.  A Tool window
    // sat above QWidget QToolTips (Solar / Propagation chips) and ate
    // the old “tip just below the heat-map chip” popups.
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setCursor(Qt::SizeAllCursor);
    setMinimumWidth(kCardWidth);
    setMaximumWidth(kCardWidth);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 12, 14, 10 + kTailH);
    lay->setSpacing(4);

    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("helpCardTitle"));
    title_->setText(tr("Help"));
    title_->setWordWrap(false);
    title_->setAttribute(Qt::WA_TranslucentBackground);
    title_->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffffff; font-weight: 700; font-size: 14px; "
        "background: transparent; }"));
    lay->addWidget(title_);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setMaximumHeight(kMaxBodyH);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setAttribute(Qt::WA_TranslucentBackground);
    scroll_->viewport()->setAutoFillBackground(false);
    scroll_->setStyleSheet(QStringLiteral(
        "QScrollArea { border: none; background: transparent; }"));

    body_ = new QLabel;
    body_->setObjectName(QStringLiteral("helpCardBody"));
    body_->setWordWrap(true);
    body_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    body_->setTextInteractionFlags(Qt::NoTextInteraction);
    body_->setAttribute(Qt::WA_TranslucentBackground);
    body_->setStyleSheet(QStringLiteral(
        "QLabel { color: #f4f8fc; font-size: 13px; background: transparent; }"));
    body_->setText({});
    scroll_->setWidget(body_);
    lay->addWidget(scroll_);

    QSettings s;
    parked_ = s.value(kParked, false).toBool();
    if (parked_) {
        localPos_ = QPoint(s.value(kPosX, 16).toInt(),
                           s.value(kPosY, 80).toInt());
    }

    if (hint_) {
        connect(hint_, &HelpHint::titleChanged, this, &HelpCard::refreshText);
        connect(hint_, &HelpHint::bodyChanged, this, &HelpCard::refreshText);
        connect(hint_, &HelpHint::filledChanged, this, &HelpCard::refreshText);
    }
    if (prefs_) {
        connect(prefs_, &Prefs::tooltipsEnabledChanged,
                this, &HelpCard::syncVisibility);
    }
    if (host_) {
        host_->installEventFilter(this);
        connect(host_, &MainWindow::docksReady, this, [this]() {
            if (!parked_)
                localPos_ = defaultLocalPos();
            applyDefaultOrSaved();
            syncVisibility();
        });
    }

    refreshText();
    relayout();
}

void HelpCard::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal inset = kStroke / 2.0;
    const QRectF body = QRectF(rect()).adjusted(
        inset, inset, -inset, -inset - kTailH);

    QPainterPath path;
    path.addRoundedRect(body, kRadius, kRadius);

    QPainterPath tail;
    const QPointF a(body.left() + 20, body.bottom() - 1);
    const QPointF b(body.left() + 20 + kTailW, body.bottom() - 1);
    const QPointF tip(body.left() + 6, rect().bottom() - inset);
    tail.moveTo(a);
    tail.lineTo(tip);
    tail.lineTo(b);
    tail.closeSubpath();
    path = path.united(tail);

    p.setPen(QPen(QColor(255, 255, 255, 210), 2, Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor(12, 18, 26, 180));
    p.drawPath(path);
}

void HelpCard::syncVisibility() {
    const bool on = prefs_ && prefs_->tooltipsEnabled()
                    && hint_ && hint_->filled();
    if (on) {
        show();
        raise();
        followHost();
    } else {
        hide();
    }
}

void HelpCard::applyDefaultOrSaved() {
    if (!parked_)
        localPos_ = defaultLocalPos();
    localPos_ = clampLocal(localPos_);
    followHost();
}

QPoint HelpCard::defaultLocalPos() const {
    // Lower-left of the panadapter dock (waterfall lives in the bottom
    // half).  Falls back to the left of the central display.
    QRect pan;
    if (docks_) {
        if (QDockWidget *d = docks_->value(QStringLiteral("panadapter"))) {
            if (d->isVisible())
                pan = d->geometry();
        }
    }
    if (!pan.isValid() && host_) {
        pan = QRect(12, 48, qMax(200, host_->width() - 24),
                    qMax(120, host_->height() / 2));
    }
    const int x = pan.left() + 12;
    const int y = pan.bottom() - height() - 18;
    return QPoint(x, qMax(pan.top() + 36, y));
}

QPoint HelpCard::clampLocal(QPoint local) const {
    if (!host_)
        return local;
    const int maxX = qMax(0, host_->width() - width() - 4);
    const int maxY = qMax(0, host_->height() - height() - 4);
    local.setX(qBound(4, local.x(), maxX));
    local.setY(qBound(4, local.y(), maxY));
    return local;
}

void HelpCard::followHost() {
    if (!host_ || !isVisible())
        return;
    localPos_ = clampLocal(localPos_);
    move(localPos_);
    raise();
}

void HelpCard::persistPos() {
    QSettings s;
    s.setValue(kPosX, localPos_.x());
    s.setValue(kPosY, localPos_.y());
    s.setValue(kParked, parked_);
}

void HelpCard::refreshText() {
    if (!(hint_ && hint_->filled())) {
        title_->clear();
        body_->clear();
        syncVisibility();
        return;
    }
    title_->setText(hint_->title());
    body_->setText(hint_->body());
    relayout();
    syncVisibility();
    raise();
}

void HelpCard::relayout() {
    body_->setFixedWidth(kCardWidth - 28);
    body_->adjustSize();
    const int bodyH = qMin(kMaxBodyH, qMax(32, body_->sizeHint().height() + 4));
    scroll_->setFixedHeight(bodyH);
    adjustSize();
    if (!parked_ && host_ && host_->isVisible())
        localPos_ = defaultLocalPos();
    followHost();
}

void HelpCard::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        dragOffset_ = event->globalPosition().toPoint()
                      - mapToGlobal(QPoint(0, 0));
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void HelpCard::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_ && (event->buttons() & Qt::LeftButton) && host_) {
        const QPoint glob = event->globalPosition().toPoint() - dragOffset_;
        localPos_ = clampLocal(host_->mapFromGlobal(glob));
        parked_ = true;
        followHost();
        event->accept();
        return;
    }
    QFrame::mouseMoveEvent(event);
}

void HelpCard::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        persistPos();
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

bool HelpCard::eventFilter(QObject *watched, QEvent *event) {
    if (watched == host_) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::WindowActivate:
        case QEvent::Show:
            syncVisibility();
            break;
        case QEvent::Hide:
            hide();
            break;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

} // namespace lyra::ui

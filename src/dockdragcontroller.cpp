// Lyra — drag-to-dock controller.  See dockdragcontroller.h.
#include "dockdragcontroller.h"

#include <QApplication>
#include <QCursor>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QFont>
#include <QPainter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>

namespace lyra::ui {

// ── translucent cyan drop-zone overlay ──────────────────────────────────────
// A mouse-transparent child of the main window, raised above the docks during
// a drag, painting the candidate drop rectangle.  It never receives input —
// the controller drives it from the drag's mouse-move events.
class DropOverlay : public QWidget {
public:
    explicit DropOverlay(QWidget *parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        hide();
    }
    void setHighlight(const QRect &r, const QString &label = QString()) {
        if (r == rect_ && label == label_) return;
        rect_  = r;
        label_ = label;
        update();
    }
    void clearHighlight() {
        if (rect_.isNull() && label_.isEmpty()) return;
        rect_ = QRect();
        label_.clear();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (rect_.isNull()) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor cyan(0x00, 0xe5, 0xff);
        QColor fill = cyan;
        fill.setAlpha(64);
        p.fillRect(rect_, fill);
        p.setPen(QPen(cyan, 2.0));
        p.drawRect(rect_.adjusted(1, 1, -1, -1));

        // Name the action ("Dock left" / "Stack as tab" / "Split right") on a
        // dark pill centred in the target.  Not knowing what a drop will do is
        // the docking friction — worse on a small screen where every drop
        // reflows the layout.
        if (label_.isEmpty()) return;
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QRect tb = p.fontMetrics().boundingRect(label_);
        QRect pill(0, 0, tb.width() + 20, tb.height() + 12);
        pill.moveCenter(rect_.center());
        // Keep the pill on-screen even for a thin edge strip.
        if (pill.left()   < 2)            pill.moveLeft(2);
        if (pill.top()    < 2)            pill.moveTop(2);
        if (pill.right()  > width()  - 2) pill.moveRight(width()  - 2);
        if (pill.bottom() > height() - 2) pill.moveBottom(height() - 2);
        QColor pillBg(0x0a, 0x12, 0x18);
        pillBg.setAlpha(225);
        p.setPen(QPen(cyan, 1.0));
        p.setBrush(pillBg);
        p.drawRoundedRect(pill, 6, 6);
        p.setPen(QColor(0xe6, 0xf7, 0xff));
        p.drawText(pill, Qt::AlignCenter, label_);
    }

private:
    QRect   rect_;
    QString label_;
};

// ── controller ──────────────────────────────────────────────────────────────
DockDragController::DockDragController(
    QMainWindow *win, const QHash<QString, QDockWidget *> *docks,
    QObject *parent)
    : QObject(parent), win_(win), docks_(docks) {}

DockDragController::~DockDragController() = default;

QDockWidget *DockDragController::dockOf(QWidget *w) const {
    for (QWidget *p = w; p; p = p->parentWidget())
        if (auto *d = qobject_cast<QDockWidget *>(p)) return d;
    return nullptr;
}

bool DockDragController::isFloatOnly(QDockWidget *d) const {
    return d && floatOnly_ && floatOnly_(d->objectName());
}

QRect DockDragController::dockRegionLocal() const {
    QRect r = win_->rect();   // client area, win_-local
    int top = 0, bottom = 0;
    if (auto *mb = win_->menuBar(); mb && mb->isVisible())
        top += mb->height();
    // Top-docked toolbars sit just under the menu bar.
    const auto bars = win_->findChildren<QToolBar *>();
    for (auto *tb : bars) {
        if (!tb || !tb->isVisible() || tb->isFloating()) continue;
        if (tb->mapTo(win_, QPoint(0, 0)).y() <= top + 4)
            top += tb->height();
    }
    if (auto *sb = win_->statusBar(); sb && sb->isVisible())
        bottom += sb->height();
    return r.adjusted(0, top, 0, -bottom);
}

DockDragController::Zone
DockDragController::hitTest(const QPoint &globalPos) const {
    Zone z;
    const QRect region = dockRegionLocal();
    const QPoint p = win_->mapFromGlobal(globalPos);
    if (!region.contains(p))
        return z;   // off the dock region → float

    // Edge bands first so the far edges stay reachable past the panels.
    const int bandW = qMin(48, region.width() / 6);
    const int bandH = qMin(48, region.height() / 6);
    const int w3 = region.width() / 3;
    const int h3 = region.height() / 3;
    if (p.x() - region.left() < bandW) {
        z.type = ZoneType::Edge;
        z.edge = Qt::LeftDockWidgetArea;
        z.highlight = QRect(region.left(), region.top(), w3, region.height());
        return z;
    }
    if (region.right() - p.x() < bandW) {
        z.type = ZoneType::Edge;
        z.edge = Qt::RightDockWidgetArea;
        z.highlight = QRect(region.right() - w3, region.top(), w3, region.height());
        return z;
    }
    if (p.y() - region.top() < bandH) {
        z.type = ZoneType::Edge;
        z.edge = Qt::TopDockWidgetArea;
        z.highlight = QRect(region.left(), region.top(), region.width(), h3);
        return z;
    }
    if (region.bottom() - p.y() < bandH) {
        z.type = ZoneType::Edge;
        z.edge = Qt::BottomDockWidgetArea;
        z.highlight = QRect(region.left(), region.bottom() - h3, region.width(), h3);
        return z;
    }

    // Interior → split/tabify relative to the docked neighbor under the cursor.
    for (QDockWidget *t : *docks_) {
        if (!t || t == dragDock_) continue;
        if (!t->isVisible() || t->isFloating()) continue;
        const QRect tr(t->mapTo(win_, QPoint(0, 0)), t->size());
        if (!tr.contains(p)) continue;
        const int lx = p.x() - tr.left();
        const int ly = p.y() - tr.top();
        z.neighbor = t;
        // Outer QUARTERS split (drop near a panel's edge to sit beside it); the
        // generous center half stacks as a TAB — the space-saving arrangement a
        // small screen needs, and previously a hard-to-hit center third.
        if (lx < tr.width() / 4) {
            z.type = ZoneType::Split; z.orient = Qt::Horizontal; z.before = true;
            z.highlight = QRect(tr.left(), tr.top(), tr.width() / 2, tr.height());
        } else if (lx > 3 * tr.width() / 4) {
            z.type = ZoneType::Split; z.orient = Qt::Horizontal; z.before = false;
            z.highlight = QRect(tr.center().x(), tr.top(), tr.width() / 2, tr.height());
        } else if (ly < tr.height() / 4) {
            z.type = ZoneType::Split; z.orient = Qt::Vertical; z.before = true;
            z.highlight = QRect(tr.left(), tr.top(), tr.width(), tr.height() / 2);
        } else if (ly > 3 * tr.height() / 4) {
            z.type = ZoneType::Split; z.orient = Qt::Vertical; z.before = false;
            z.highlight = QRect(tr.left(), tr.center().y(), tr.width(), tr.height() / 2);
        } else {
            z.type = ZoneType::Tabify;
            z.highlight = tr;
        }
        return z;
    }
    return z;   // inside the region but over no neighbor → float
}

void DockDragController::commit(const Zone &z) {
    QDockWidget *d = dragDock_;
    if (!d) return;
    switch (z.type) {
    case ZoneType::Edge:
        win_->addDockWidget(z.edge, d);   // re-docks (un-floats) automatically
        break;
    case ZoneType::Tabify:
        if (z.neighbor) win_->tabifyDockWidget(z.neighbor, d);
        break;
    case ZoneType::Split:
        if (z.neighbor) {
            // splitDockWidget(first, second, orient) puts `second` AFTER
            // `first`.  For a "before" drop, split once (dragged after the
            // neighbor) then split again with the neighbor as `second` so it
            // lands after the dragged dock → dragged ends up before it.
            win_->splitDockWidget(z.neighbor, d, z.orient);
            if (z.before) win_->splitDockWidget(d, z.neighbor, z.orient);
        }
        break;
    case ZoneType::None:
        // Dropped off the dock region → leave floating.  A dock that was
        // already floating followed the cursor during the drag; a docked one
        // tears out here, under the cursor — UNLESS this is a no-tear-out host
        // (a rack window), where a drop in empty space just stays put.
        if (!noTearOut_ && !dragWasFloating_) {
            d->setFloating(true);
            d->move(QCursor::pos() - QPoint(40, 10));
            d->activateWindow();
        }
        break;
    }
    d->show();
    d->raise();
    emit layoutChanged();
}

void DockDragController::endDrag() {
    if (overlay_) {
        overlay_->hide();
        overlay_->clearHighlight();
    }
    state_ = State::Idle;
    dragDock_ = nullptr;
}

bool DockDragController::eventFilter(QObject *obj, QEvent *event) {
    auto *bar = qobject_cast<QWidget *>(obj);
    if (!bar || bar->objectName() != QStringLiteral("dockTitleBar"))
        return QObject::eventFilter(obj, event);

    switch (event->type()) {
    case QEvent::MouseButtonDblClick: {
        // Standard QDockWidget gesture, lost when we replaced the title bar.
        if (QDockWidget *d = dockOf(bar)) {
            if (d->features() & QDockWidget::DockWidgetFloatable) {
                d->setFloating(!d->isFloating());
                if (d->isFloating()) { d->raise(); d->activateWindow(); }
            }
            return true;
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::LeftButton) break;
        QDockWidget *d = dockOf(bar);
        if (!d) break;
        // Locked panel → not draggable (applyPanelLock clears Movable).
        if (!(d->features() & QDockWidget::DockWidgetMovable)) break;
        // Never start a drag on a title-bar button (?/float/close).
        if (qobject_cast<QToolButton *>(bar->childAt(me->position().toPoint())))
            break;
        dragDock_        = d;
        pressGlobal_     = me->globalPosition().toPoint();
        dragWasFloating_ = d->isFloating();
        floatGrabOffset_ = pressGlobal_ - d->frameGeometry().topLeft();
        state_           = State::Armed;
        return true;
    }
    case QEvent::MouseMove: {
        if (state_ == State::Idle || !dragDock_) break;
        auto *me = static_cast<QMouseEvent *>(event);
        const QPoint g = me->globalPosition().toPoint();
        const bool floatOnly = isFloatOnly(dragDock_);
        if (state_ == State::Armed) {
            if ((g - pressGlobal_).manhattanLength()
                < QApplication::startDragDistance())
                return true;   // not past threshold yet
            state_ = State::Dragging;
            if (floatOnly) {
                // Chip-summoned tool window (TX/RX DSP racks, CW console,
                // CW decoder): a pure free-move that NEVER docks — no drop
                // overlay, no hit-testing.  Self-heal one a prior build may
                // have left docked so it can be dragged out again.
                if (!dragDock_->isFloating()) {
                    dragDock_->setFloating(true);
                    dragWasFloating_ = true;
                    floatGrabOffset_ = QPoint(40, 10);
                }
            } else {
                if (!overlay_) overlay_ = new DropOverlay(win_);
                overlay_->setGeometry(QRect(QPoint(0, 0), win_->size()));
                overlay_->clearHighlight();
                overlay_->show();
                overlay_->raise();
            }
        }
        if (floatOnly) {
            // Free-follow the cursor only — no drop zones, no dock commit.
            dragDock_->move(g - floatGrabOffset_);
            return true;
        }
        // An already-floating dock free-follows the cursor (preserves the old
        // floating-drag feel); a docked one stays put and only the overlay
        // tracks (no mid-drag reparent → no lost mouse grab).
        if (dragWasFloating_)
            dragDock_->move(g - floatGrabOffset_);
        const Zone z = hitTest(g);
        if (z.type == ZoneType::None) {
            overlay_->clearHighlight();   // off the dock region → release to float
            return true;
        }
        QString label;
        switch (z.type) {
        case ZoneType::Edge:
            label = z.edge == Qt::LeftDockWidgetArea  ? tr("Dock left")
                  : z.edge == Qt::RightDockWidgetArea ? tr("Dock right")
                  : z.edge == Qt::TopDockWidgetArea   ? tr("Dock top")
                                                      : tr("Dock bottom");
            break;
        case ZoneType::Tabify:
            label = tr("Stack as tab");
            break;
        case ZoneType::Split:
            label = z.orient == Qt::Horizontal
                        ? (z.before ? tr("Split left") : tr("Split right"))
                        : (z.before ? tr("Split top")  : tr("Split bottom"));
            break;
        default:
            break;
        }
        overlay_->setHighlight(z.highlight, label);
        return true;
    }
    case QEvent::MouseButtonRelease: {
        if (state_ == State::Armed) {   // pressed but never dragged → click
            state_ = State::Idle;
            dragDock_ = nullptr;
            return true;
        }
        if (state_ != State::Dragging) break;
        auto *me = static_cast<QMouseEvent *>(event);
        const QPoint gpos = me->globalPosition().toPoint();
        if (isFloatOnly(dragDock_)) {
            // Float-only tool window: leave it floating where dropped, never
            // dock.  (endDrag hides the overlay if a previous non-float drag
            // created it.)
            QDockWidget *dropped = dragDock_;
            dropped->raise();
            dropped->activateWindow();
            emit layoutChanged();   // persist the new floating position
            endDrag();
            return true;
        }
        const Zone z = hitTest(gpos);
        commit(z);
        endDrag();
        return true;
    }
    default:
        break;
    }
    return QObject::eventFilter(obj, event);
}

} // namespace lyra::ui

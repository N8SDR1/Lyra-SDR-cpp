// Lyra — drag-to-dock controller for the custom dock title bars.
//
// Why this exists: every panel's QDockWidget title bar is replaced with a
// custom cyan bar (MainWindow::makeDockTitleBar) for the "?"/float/close
// glyphs.  Replacing a QDockWidget's title bar REMOVES Qt's built-in
// title-bar drag — the gesture that drives Qt's native drag → drop-indicator
// → snap-to-edge re-dock machinery.  Without it, a docked panel could not be
// dragged at all (only double-click float-toggle + the ⧉ button worked), and
// a floated panel could never be dragged back in.  Three testers reported
// "everything floats, nothing docks, drop-against-an-edge does nothing."
//
// This controller restores it Lyra-native, using only PUBLIC QMainWindow API
// (no private Qt drag internals): it is installed as the event filter on each
// custom title bar and runs a press → threshold → drag → drop state machine.
// A translucent cyan overlay highlights the target zone as the cursor moves;
// on release it commits the placement via addDockWidget / splitDockWidget /
// tabifyDockWidget / setFloating.  A committed drag emits layoutChanged() so
// the host can persist immediately (save-on-commit).
//
// Threading: GUI thread only (event filter on widgets).  Lock-aware: a dock
// whose DockWidgetMovable feature is cleared (MainWindow::applyPanelLock) is
// never draggable.
#pragma once

#include <QHash>
#include <QObject>   // also pulls in the Qt:: namespace enums (qnamespace.h)
#include <QPoint>
#include <QRect>

#include <functional>

class QMainWindow;
class QDockWidget;
class QWidget;
class QEvent;
class QString;

namespace lyra::ui {

class DropOverlay;

class DockDragController : public QObject {
    Q_OBJECT
public:
    // `docks` is borrowed (owned by MainWindow); read live during hit-testing.
    DockDragController(QMainWindow *win,
                       const QHash<QString, QDockWidget *> *docks,
                       QObject *parent = nullptr);
    ~DockDragController() override;

    // Float-only panels (the header-chip-summoned TX/RX DSP + CW tool windows)
    // must NEVER dock: dragging them is a pure free-move with no drop zones and
    // no commit-to-dock on release.  MainWindow sets this to its
    // isChipSummonedPanel() test so the two stay in lockstep.
    void setFloatOnlyPredicate(std::function<bool(const QString &)> pred) {
        floatOnly_ = std::move(pred);
    }

    // No-tear-out mode (a rack window): members re-tile / tab freely INSIDE the
    // host, but a drop into empty space is a no-op instead of floating the
    // member out — so a fixed-membership rack can't spill panels onto the
    // screen.  Set true on a PanelRack's own controller.
    void setNoTearOut(bool v) { noTearOut_ = v; }

signals:
    // A drag committed a new dock placement (or float) — persist the layout.
    void layoutChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    enum class State { Idle, Armed, Dragging };
    enum class ZoneType { None, Edge, Split, Tabify };
    struct Zone {
        ZoneType           type     = ZoneType::None;
        Qt::DockWidgetArea edge     = Qt::NoDockWidgetArea;  // Edge
        QDockWidget       *neighbor = nullptr;               // Split / Tabify
        Qt::Orientation    orient   = Qt::Horizontal;        // Split
        bool               before   = false;                 // Split: drag side
        QRect              highlight;   // win_-local, for the overlay
    };

    QDockWidget *dockOf(QWidget *w) const;
    bool         isFloatOnly(QDockWidget *d) const;   // chip-summoned tool window?
    QRect        dockRegionLocal() const;     // win_-local content rect
    Zone         hitTest(const QPoint &globalPos) const;
    void         commit(const Zone &z);
    void         endDrag();

    QMainWindow                         *win_     = nullptr;
    const QHash<QString, QDockWidget *> *docks_   = nullptr;
    DropOverlay                         *overlay_ = nullptr;
    std::function<bool(const QString &)> floatOnly_;   // chip-summoned → float-only
    bool                                 noTearOut_ = false;   // rack: None = no-op

    State        state_           = State::Idle;
    QDockWidget *dragDock_        = nullptr;
    QPoint       pressGlobal_;
    QPoint       floatGrabOffset_;          // free-move of an already-floating dock
    bool         dragWasFloating_ = false;
};

} // namespace lyra::ui

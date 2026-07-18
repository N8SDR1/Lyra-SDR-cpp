// Lyra — fixed-membership tool-panel "rack" window.
//
// The optional grouped-panel mode (Settings → Visuals → Panel layout) houses a
// whole set of tool panels — the DSP set (TX Speech / TX EQ / TX Combinator /
// TX Plating / RX EQ) or the Options set (Tuner / CW / CW Dec / Voice Keyer) —
// inside ONE window instead of each floating individually from its own header
// chip.  A rack is a nested QMainWindow dock-host: members are tiled, resizable
// panes, with a top strip of show/hide toggles (one per member) so the operator
// can click off the modules they don't use.
//
// Membership is fixed — a member cannot be dragged or floated OUT of the rack
// (that is what made the old drag-to-group container spill panels across the
// screen).  Closing the rack HIDES the whole window; the members go with it and
// nothing spills.  The rack persists its geometry, which members are shown, and
// whether it was open, so it recalls on the next launch.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QString>

class QAction;
class QDockWidget;
class QMenu;
class QTimer;
class QToolBar;
class QToolButton;

namespace lyra::ui {

class DockDragController;

class PanelRack : public QMainWindow {
    Q_OBJECT
public:
    PanelRack(const QString &objName, const QString &title,
              QWidget *parent = nullptr);

    // Adopt a member dock (already removeDockWidget()'d from its old host).
    // `toggleAction` is the member's toggleViewAction() — reused as the rack's
    // internal show/hide toggle for that member.
    void addMember(QDockWidget *dock, QAction *toggleAction);

    // Geometry + member arrangement + open-state persistence (QSettings under
    // "panelRack/<objName>/").
    void store() const;                 // write geometry / state / open flag
    void restore();                     // read geometry / state (call after members added)
    bool openByDefault() const { return openByDefault_; }

    // Saved layout slots + undo, mirroring the main window's per-display layout
    // slots.  Each numbered slot persists the rack's member arrangement (which
    // are shown, how they are tiled / tabbed / sized) and the rack window
    // geometry; recall restores it; undo reverts the last arrangement change
    // (a recall OR a manual in-rack drag).  Reached from the rack's "Layouts"
    // toolbar dropdown; persisted under "panelRack/<objName>/slotN/".
    static constexpr int kSlots = 3;
    void saveSlot(int n, const QString &name);   // snapshot the arrangement + name it
    void recallSlot(int n);             // restore slot n (no-op if empty)
    bool slotFilled(int n) const;       // does slot n hold a saved arrangement?
    QString slotName(int n) const;      // operator-given name (or "Layout N" default)
    void undoArrangement();             // pop + restore one step
    bool canUndo() const { return !undoStack_.isEmpty(); }
    int  undoDepth() const { return undoStack_.size(); }

signals:
    // The operator closed the rack window (X) — the host un-lights its chip.
    void closed();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // Undo plumbing (same shape as MainWindow's): a drag commit (re)arms the
    // debounce; on timeout the PRE-change arrangement is pushed, capped at
    // kUndoMax.  `restoring_` guards our own restoreState from snapshotting.
    void onArrangementMaybeChanged();   // drag committed — (re)arm the debounce
    void commitUndoSnapshot();          // push the pre-change state if it moved
    void pushUndo(const QByteArray &state);
    void persistUndoStack();            // write the undo stack to QSettings
    void reassertMemberFeatures();      // re-apply member features after restoreState
    void buildLayoutMenu();             // (re)populate the Layouts dropdown on show
    void promptSaveSlot(int n);         // ask for a name, then saveSlot()

    static constexpr int kUndoMax = 5;

    QString                        objName_;
    QToolBar                      *toggleBar_  = nullptr;
    QToolButton                   *layoutBtn_  = nullptr;   // "Layouts" dropdown
    QMenu                         *layoutMenu_ = nullptr;
    QTimer                        *snapTimer_  = nullptr;   // undo-snapshot debounce
    QDockWidget                   *lastMember_ = nullptr;
    bool                           openByDefault_ = false;
    bool                           restoring_  = false;     // in our own restoreState
    // The rack's OWN drag controller: members re-tile / tab INSIDE the rack
    // (no-tear-out), just like the main window's dockable panels.
    DockDragController            *drag_ = nullptr;
    QHash<QString, QDockWidget *>  members_;   // objectName → member (for hit-test)
    QList<QByteArray>              undoStack_; // pre-change arrangements (cap kUndoMax)
    QByteArray                     lastState_; // baseline for change detection
};

}  // namespace lyra::ui

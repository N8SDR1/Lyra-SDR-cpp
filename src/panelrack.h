// Lyra — fixed-membership tool-panel "rack" window.
//
// The optional grouped-panel mode (Settings → Visuals → Panel layout) houses a
// whole set of tool panels — the DSP set (TX Speech / TX EQ / TX Combinator /
// TX Plating / RX EQ) or the Options set (Tuner / CW / CW Dec /
// Voice Keyer) —
// inside ONE window instead of each floating individually from its own header
// chip.  A rack is a nested QMainWindow dock-host: members are tiled, resizable
// panes, with a top strip of show/hide toggles (one per member) so the operator
// can click off the modules they don't use.
//
// The Options rack is fixed membership.  The DSP rack allows a member to pop
// out and dock into the main window.  Closing the rack HIDES the whole window.
// The rack persists its geometry, which members are shown, and whether it was
// open, so it recalls on the next launch.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QMainWindow>
#include <QString>

#include <functional>

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

    // DSP rack: members may pop out (⧉, double-click, or drag off) and the
    // host then adopts them so they can dock into the main window.  The
    // Options rack stays fixed-membership.
    void setAllowTearOut(bool allow);
    void setTearOutHandler(std::function<void(QDockWidget *, const QPoint &globalPos,
                                              const QPoint &grabOffset)> fn);
    bool allowTearOut() const { return allowTearOut_; }
    // This member never pops out, even in a tear-out rack (call before addMember).
    void setFixedMember(const QString &objectName) { fixedMembers_.insert(objectName); }
    bool hasMember(const QDockWidget *dock) const;
    // Remove from this rack (and its toggle strip), leave the dock alive.
    void detachMember(QDockWidget *dock);
    // Lock panels: members still re-tile inside the rack, but nothing pops out.
    void setLocked(bool locked);
    // Lyra is quitting: the next close is accepted and does NOT store (the
    // host stored while everything was still on screen).  Cleared if the
    // quit is cancelled.
    void setQuitting(bool quitting) { quitting_ = quitting; }

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
    // A member popped out (float button, title-bar double-click, or a drag
    // off the rack).  The host adopts it onto the main window.
    void memberFloated(QDockWidget *dock);

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
    bool canTearOut() const { return allowTearOut_ && !locked_; }
    bool canTearOut(const QDockWidget *dock) const;   // + not a fixed member
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
    bool                           allowTearOut_ = false;   // pop out + dock to main window
    QSet<QString>                  fixedMembers_;           // never pop out (setFixedMember)
    bool                           locked_     = false;     // Lock panels: no pop-out
    bool                           quitting_   = false;     // see setQuitting()
    bool                           restoring_  = false;     // in our own restoreState
    // The rack's OWN drag controller: members re-tile / tab INSIDE the rack
    // (no-tear-out), just like the main window's dockable panels.
    DockDragController            *drag_ = nullptr;
    QHash<QString, QDockWidget *>  members_;   // objectName → member (for hit-test)
    QHash<QString, QAction *>      toggles_;   // objectName → its toggle-strip action
    QList<QByteArray>              undoStack_; // pre-change arrangements (cap kUndoMax)
    QByteArray                     lastState_; // baseline for change detection
};

}  // namespace lyra::ui

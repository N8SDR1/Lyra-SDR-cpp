// Lyra — fixed-membership tool-panel rack window.  See panelrack.h.
#include "panelrack.h"

#include "dockdragcontroller.h"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVariant>
#include <QWidget>

#include <utility>   // std::as_const

namespace lyra::ui {

static QString keyBase(const QString &obj) {
    return QStringLiteral("panelRack/") + obj + QLatin1Char('/');
}

PanelRack::PanelRack(const QString &objName, const QString &title,
                     QWidget *parent)
    : QMainWindow(parent), objName_(objName) {
    // A real top-level frame (own title bar / taskbar entry) though parented to
    // the main window for ownership/cleanup.
    setWindowFlag(Qt::Window, true);
    setWindowTitle(title);
    setObjectName(QStringLiteral("panelRack_") + objName);
    setAttribute(Qt::WA_DeleteOnClose, false);   // MainWindow owns it

    // Same dock behaviour as the main shell: resizable tiled + tabbed panes.
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
    setDockNestingEnabled(true);

    // The rack's OWN drag controller — installed (in addMember) as the event
    // filter on each member's custom title bar so dragging re-tiles / tabs the
    // members INSIDE the rack, exactly like the main window.  Members are NOT
    // float-only (they dock) and NEVER tear out (no-tear-out), so a rearrange
    // can't spill a panel onto the screen.  A committed rearrange persists.
    drag_ = new DockDragController(this, &members_, this);
    drag_->setFloatOnlyPredicate([](const QString &) { return false; });
    drag_->setNoTearOut(true);
    // A committed drag persists the live layout AND may feed the undo history.
    connect(drag_, &DockDragController::layoutChanged, this, [this]() {
        store();
        onArrangementMaybeChanged();
    });

    // Undo snapshot debounce: one drag fires several layoutChanged signals; take
    // a single snapshot ~400 ms after they settle (same idiom as MainWindow).
    snapTimer_ = new QTimer(this);
    snapTimer_->setSingleShot(true);
    snapTimer_->setInterval(400);
    connect(snapTimer_, &QTimer::timeout, this, &PanelRack::commitUndoSnapshot);

    // Top strip: one show/hide toggle per member (added in addMember()).
    toggleBar_ = addToolBar(tr("Modules"));
    toggleBar_->setObjectName(QStringLiteral("rackToggleBar_") + objName);
    toggleBar_->setMovable(false);
    toggleBar_->setFloatable(false);

    // "Layouts" dropdown (leftmost, before the module toggles): save the current
    // arrangement into one of kSlots slots, recall a slot, or undo the last
    // change — the rack's own version of the main window's layout slots + undo.
    // Rebuilt on show so slot-filled state + undo depth are always current.
    layoutBtn_ = new QToolButton(this);
    layoutBtn_->setText(tr("Layouts"));
    layoutBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    layoutBtn_->setPopupMode(QToolButton::InstantPopup);
    layoutBtn_->setToolTip(
        tr("Save, recall, or undo this rack's panel arrangement"));
    layoutMenu_ = new QMenu(layoutBtn_);
    connect(layoutMenu_, &QMenu::aboutToShow, this, &PanelRack::buildLayoutMenu);
    layoutBtn_->setMenu(layoutMenu_);
    toggleBar_->addWidget(layoutBtn_);
    toggleBar_->addSeparator();

    // No central widget — a pure dock host.  A zero-size spacer collapses the
    // reserved central region so member panes fill the whole window.
    auto *spacer = new QWidget(this);
    spacer->setFixedSize(0, 0);
    setCentralWidget(spacer);

    resize(520, 680);
    openByDefault_ = QSettings().value(keyBase(objName_) + QStringLiteral("open"),
                                       false).toBool();
}

void PanelRack::addMember(QDockWidget *dock, QAction *toggleAction) {
    if (!dock) return;
    members_.insert(dock->objectName(), dock);
    // A rack member can be dragged to re-tile / tab INSIDE the rack (Movable)
    // and closed via its title-bar X (Closable, just hides it — reflected on
    // the toggle), but is NOT floatable — it can never tear out to a separate
    // window and spill onto the screen.  The rack's own no-tear-out drag
    // controller drives the in-rack rearrange.
    dock->setFeatures(QDockWidget::DockWidgetMovable
                      | QDockWidget::DockWidgetClosable);
    if (QWidget *tb = dock->titleBarWidget()) {
        if (auto *fb = tb->findChild<QToolButton *>(QStringLiteral("dockFloat")))
            fb->setVisible(false);   // no float-out from a rack
        tb->installEventFilter(drag_);   // dragging re-tiles / tabs inside the rack
    }
    if (!lastMember_)
        addDockWidget(Qt::TopDockWidgetArea, dock);
    else
        splitDockWidget(lastMember_, dock, Qt::Vertical);   // tile below the last
    lastMember_ = dock;
    dock->setFloating(false);
    dock->show();                     // shown by default; restore() hides the off ones
    if (toggleAction) toggleBar_->addAction(toggleAction);
}

void PanelRack::store() const {
    // Safe boot (--safe / LYRA_SAFE) treats the whole layout store as READ-ONLY,
    // same as MainWindow::saveLayout(): the session comes up in the factory
    // arrangement and must NOT overwrite the operator's real rack layout on
    // close (this runs from closeEvent + the rack-chip toggle, both automatic).
    if (qEnvironmentVariableIsSet("LYRA_SAFE")) return;
    QSettings s;
    const QString b = keyBase(objName_);
    s.setValue(b + QStringLiteral("geometry"), saveGeometry());
    s.setValue(b + QStringLiteral("state"), saveState());
    s.setValue(b + QStringLiteral("open"), isVisible());
}

void PanelRack::restore() {
    QSettings s;
    const QString b = keyBase(objName_);
    const QByteArray g = s.value(b + QStringLiteral("geometry")).toByteArray();
    if (!g.isEmpty()) restoreGeometry(g);
    const QByteArray st = s.value(b + QStringLiteral("state")).toByteArray();
    if (!st.isEmpty()) {
        restoreState(st);              // members + toolbar already added
        reassertMemberFeatures();      // restoreState resets dock features
    }
    openByDefault_ = s.value(b + QStringLiteral("open"), false).toBool();

    // Undo history from this rack's previous sessions (survives restart).
    undoStack_.clear();
    const QVariantList uv = s.value(b + QStringLiteral("undo")).toList();
    for (const QVariant &e : uv) {
        const QByteArray a = e.toByteArray();
        if (!a.isEmpty()) undoStack_.append(a);
    }
    while (undoStack_.size() > kUndoMax) undoStack_.removeFirst();
    lastState_ = saveState();          // baseline = the just-restored arrangement
}

void PanelRack::closeEvent(QCloseEvent *event) {
    // Close = hide the whole rack (members go with it — nothing spills onto the
    // screen).  Persist first, then tell the host to un-light its chip.
    event->ignore();
    hide();
    store();
    emit closed();
}

// ── Saved layout slots + undo ────────────────────────────────────────────────

static QString slotKey(const QString &obj, int n) {
    return keyBase(obj) + QStringLiteral("slot%1/").arg(n);
}

bool PanelRack::slotFilled(int n) const {
    if (n < 1 || n > kSlots) return false;
    return !QSettings().value(slotKey(objName_, n) + QStringLiteral("state"))
                .toByteArray().isEmpty();
}

QString PanelRack::slotName(int n) const {
    if (n < 1 || n > kSlots) return QString();
    const QString nm = QSettings().value(slotKey(objName_, n)
                                         + QStringLiteral("name")).toString();
    return nm.isEmpty() ? tr("Layout %1").arg(n) : nm;
}

void PanelRack::saveSlot(int n, const QString &name) {
    if (n < 1 || n > kSlots) return;
    QSettings s;
    const QString b = slotKey(objName_, n);
    s.setValue(b + QStringLiteral("geometry"), saveGeometry());
    s.setValue(b + QStringLiteral("state"), saveState());
    s.setValue(b + QStringLiteral("name"), name.trimmed());
}

void PanelRack::promptSaveSlot(int n) {
    if (n < 1 || n > kSlots) return;
    const QString cur = slotFilled(n) ? slotName(n) : tr("Layout %1").arg(n);
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Save panel layout"), tr("Name for this layout:"),
        QLineEdit::Normal, cur, &ok);
    if (!ok) return;                                   // cancelled
    saveSlot(n, name.trimmed().isEmpty() ? cur : name.trimmed());
}

void PanelRack::recallSlot(int n) {
    if (!slotFilled(n)) return;
    QSettings s;
    const QString b = slotKey(objName_, n);
    pushUndo(saveState());             // so Undo reverts this recall
    restoring_ = true;
    snapTimer_->stop();                // cancel any pending drag snapshot
    const QByteArray geo = s.value(b + QStringLiteral("geometry")).toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);
    const QByteArray st = s.value(b + QStringLiteral("state")).toByteArray();
    if (!st.isEmpty()) restoreState(st);
    reassertMemberFeatures();
    lastState_ = saveState();
    store();                           // the recalled arrangement is now the live one
    // Drop the guard AFTER restoreState's settling signals drain (they'd
    // otherwise re-arm the debounce and push a spurious snapshot).  600 ms >
    // the 400 ms debounce, so any deferred dock signals are ignored.
    QTimer::singleShot(600, this, [this]() { restoring_ = false; });
}

void PanelRack::undoArrangement() {
    if (undoStack_.isEmpty()) return;
    restoring_ = true;
    snapTimer_->stop();
    const QByteArray prev = undoStack_.takeLast();
    restoreState(prev);                // arrangement only, like the main window
    reassertMemberFeatures();
    lastState_ = prev;
    persistUndoStack();                // the popped stack survives a restart
    store();
    QTimer::singleShot(600, this, [this]() { restoring_ = false; });
}

void PanelRack::onArrangementMaybeChanged() {
    if (restoring_) return;
    snapTimer_->start();               // (re)arm the debounce
}

void PanelRack::commitUndoSnapshot() {
    if (restoring_) return;
    const QByteArray now = saveState();
    if (now == lastState_) return;     // nothing actually moved
    pushUndo(lastState_);              // the PRE-change arrangement
    lastState_ = now;
}

void PanelRack::pushUndo(const QByteArray &state) {
    if (state.isEmpty()) return;
    undoStack_.append(state);
    while (undoStack_.size() > kUndoMax) undoStack_.removeFirst();
    persistUndoStack();
}

void PanelRack::persistUndoStack() {
    QVariantList v;
    for (const QByteArray &b : std::as_const(undoStack_)) v.append(b);
    QSettings().setValue(keyBase(objName_) + QStringLiteral("undo"), v);
}

void PanelRack::reassertMemberFeatures() {
    // restoreState() resets each dock's features + can re-show the (hidden)
    // float button, so re-apply the rack's no-float-out contract afterwards.
    for (QDockWidget *d : std::as_const(members_)) {
        d->setFeatures(QDockWidget::DockWidgetMovable
                       | QDockWidget::DockWidgetClosable);
        if (QWidget *tb = d->titleBarWidget())
            if (auto *fb = tb->findChild<QToolButton *>(QStringLiteral("dockFloat")))
                fb->setVisible(false);
    }
}

void PanelRack::buildLayoutMenu() {
    layoutMenu_->clear();
    for (int n = 1; n <= kSlots; ++n) {
        const bool filled = slotFilled(n);
        QAction *a = layoutMenu_->addAction(
            filled ? tr("Recall '%1'").arg(slotName(n))
                   : tr("Layout %1  (empty)").arg(n));
        a->setEnabled(filled);
        connect(a, &QAction::triggered, this, [this, n]() { recallSlot(n); });
    }
    layoutMenu_->addSeparator();
    for (int n = 1; n <= kSlots; ++n) {
        QAction *a = layoutMenu_->addAction(
            slotFilled(n) ? tr("Save current to '%1'...").arg(slotName(n))
                          : tr("Save current to slot %1...").arg(n));
        connect(a, &QAction::triggered, this, [this, n]() { promptSaveSlot(n); });
    }
    layoutMenu_->addSeparator();
    QAction *u = layoutMenu_->addAction(
        canUndo() ? tr("Undo arrangement change  (%1)").arg(undoDepth())
                  : tr("Undo arrangement change"));
    u->setEnabled(canUndo());
    connect(u, &QAction::triggered, this, &PanelRack::undoArrangement);
}

}  // namespace lyra::ui

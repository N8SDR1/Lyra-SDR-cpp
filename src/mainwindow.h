// Lyra — QMainWindow dock shell (UI framing).
//
// The app's top-level window is a QtWidgets QMainWindow so we get the
// proven Qt docking system (movable / floatable / snappable / tabbed
// panels + saveState/restoreState layout persistence), matching the
// Python-Lyra UX.  All native C++ — Qt Widgets is a C++ module
// compiled into the binary; there is no Python and no GIL anywhere.
//
// The Vulkan scene-graph content (panadapter now; waterfall + more
// later) renders through the Qt Quick scene graph embedded INSIDE
// docks via QQuickWidget.  Control panels currently still live in the
// central QQuickWidget (Main.qml) and get peeled into their own docks
// in later steps.

#pragma once

#include <QHash>
#include <QMainWindow>
#include <QList>
#include <QPoint>

class QAction;
class QDockWidget;
class QLabel;
class QMenu;
class QToolButton;
class QQuickWidget;
class QTimer;
class QSystemTrayIcon;

namespace lyra::wx { class WxService; }
namespace lyra::solar { class SolarService; }
namespace lyra::profile { class ProfileManager; class CompanionLauncher; }
namespace lyra::cat { class SerialPtt; class SerialCwKey; class CatServer; }
namespace lyra::tx { class ClipBank; class VoiceKeyer; }
namespace lyra::recorder { class RecorderEngine; class SessionConverter; }
namespace lyra::ui {

class ProfileUi;
class CwMacroModel;
class EqModel;
class SpeechModel;
class CombinatorModel;
class PlateModel;
class TunerMemory;

class Prefs;
class SettingsDialog;
class Help;
class HelpDialog;
class LogDialog;
class Bands;
class BandPlan;
class StatusBus;
class TimeSync;
class BandMemory;
class GenSlots;
class TimeStations;
class MemoryStore;
class EibiStore;
class SpotStore;
class SpotHoleFeeder;
class DxClusterFeeder;
class TciServer;
class MeterModel;
class UsbBcd;
class UpdateChecker;
class WxIndicator;
class NcdxfFollow;
class DockDragController;
class PanelRack;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // The four C++ service objects are exposed to every embedded QML
    // surface as context properties (Discovery / Stream / Wdsp /
    // WdspEngine), so each panel binds whatever it needs.
    MainWindow(QObject *discovery, QObject *stream,
               QObject *wdsp, QObject *wdspEngine,
               Prefs *prefs, lyra::wx::WxService *wx,
               lyra::profile::ProfileManager *profiles,
               QWidget *parent = nullptr);

    // TX-rip Phase 1 (Q2): setTciMicSource / setTxDspWorker removed —
    // the TX DSP subsystem is being rebuilt from empty files per the
    // signed Phase 0 architectural mapping (docs/TX_ARCHITECTURAL_MAPPING.md
    // §10.3).  Re-introduced when the new TX path lands.

    // Connect orchestration (also called from the main.cpp startup
    // auto-connect): probe <preferIp> first and open it ONLY if the
    // radio actually answers; otherwise fall back to a scan and open
    // the first radio found.  preferIp empty -> scan directly.  Prevents
    // the blind open of a stale saved IP that left the UI stuck
    // "Connecting…" to a radio that moved / changed lease / is off.
    void beginConnect(const QString &preferIp);

    // Share JUST the panel layout as a small .lyralayout file (driven from
    // Settings → Backup & Restore → "Share a layout").  Arrangement only —
    // no window size/position, so a layout is monitor-independent and safe to
    // swap between users.  Import applies it live (panels rearrange) — no
    // restart.  Public so the Settings dialog can invoke them.
    void exportLayoutToFile();
    void importLayoutFromFile();

protected:
    void closeEvent(QCloseEvent *event) override;
    // While panels are locked, swallow dock-separator resize presses so the
    // layout can't be re-proportioned by accident (the point of Lock).  Done
    // by event interception — NOT by clamping dock sizes (that fought the
    // window layout and collapsed panels on a locked restart).
    bool event(QEvent *event) override;
    // Custom dock title bars (makeDockTitleBar) replace QDockWidget's built-in
    // title bar — and with it Qt's native drag-to-dock.  DockDragController
    // (installed as the title bars' event filter) restores drag/float/dock/
    // snap/tabify; double-click float-toggle lives there too.
    // #176 CW-macro / #89 voice-keyer F1..F12 global accelerators.  Space-bar
    // PTT is NOT here — a focused QComboBox / QPushButton consumes Space
    // (opens its popup / clicks) before keyPressEvent ever runs, so it goes
    // through the app-wide eventFilter below (previews the key like Thetis's
    // KeyPreview) instead.  F-keys stay here: widgets don't consume F1..F12,
    // so they propagate up regardless of focus.
    void keyPressEvent(QKeyEvent *event) override;

    // App-wide key preview.  Installed on qApp so space-bar PTT is seen
    // BEFORE the focused control (combo/button) can swallow it — Press routes
    // to stream.requestMox(true), release to requestMox(false).  Suppressed
    // while typing (QLineEdit/QSpinBox/…), while a modal dialog or a combo
    // popup is open, and when space-bar PTT is disabled; auto-repeat ignored.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Build a QQuickWidget that hosts <qmlFile> from the Lyra QML
    // module, with the four service objects set as context properties
    // BEFORE the source loads.
    QQuickWidget *makeQuick(const QString &qmlFile);
    // #201 — grab the panadapter+waterfall to a PNG in the active session
    // folder (driven by RecorderEngine::snapshotDue while recording).
    void captureRecorderSnapshot();
    // Wrap a QML panel in a movable/floatable/closable QDockWidget,
    // register it in docks_, and dock it into <area>.  <topic> drives
    // the title-bar "?" badge (Help guide / Settings).
    // resizable: add a bottom-right QSizeGrip (visible while floating) so the
    // panel is easy to grab + resize without hunting the thin window border.
    QDockWidget *addQuickDock(const QString &objectName,
                              const QString &title,
                              const QString &qmlFile,
                              const QString &topic,
                              Qt::DockWidgetArea area,
                              bool resizable = false);
    // Same as addQuickDock but hosts a plain QtWidgets widget (e.g. the
    // solar PROP strip) instead of a QML surface.
    QDockWidget *addWidgetDock(const QString &objectName,
                               const QString &title,
                               QWidget *content,
                               const QString &topic,
                               Qt::DockWidgetArea area);
    // Build a custom dock title bar: title (far left), "?" + float +
    // close (far right).  Replaces the default title bar so the help
    // badge lives in the title row, outside the panel content.
    QWidget *makeDockTitleBar(QDockWidget *dock, const QString &title,
                              const QString &topic);

    void buildDocks();
    void buildMenus();
    void buildToolbar();
    // Header Start/Stop: connect to the saved radio (or scan + auto-open
    // the first found) when stopped; close the stream when running.
    void onStartStop();
    // Broadcast-scan and open the first radio reported; resets the
    // connecting state (no longer sits on "Scanning…") if none found.
    void scanAndOpenFirst();
    void updateConnState();   // reflect stream running state into the UI
    void updateTciStatus();   // header TCI connected/client-count indicator
    void tickClocks();        // 1 Hz: refresh the header local + UTC clocks
    void showClockMenu(const QPoint &global);   // right-click clocks → drift check
    // GitHub update notification.  manual=true (Help → Check for Updates)
    // always reports a result; manual=false (startup) is quiet unless an
    // unseen, non-skipped newer release exists (then a once-per-version
    // modal + a persistent toolbar indicator).
    void checkForUpdates(bool manual);
    void onUpdateAvailable(const QString &tag, const QString &url,
                           const QString &body);
    void onNoUpdate();
    void onUpdateCheckFailed(const QString &reason);
    void applyPanelLock(bool locked);
    void refuseLayoutWrite();          // LYRA_TEST_SIZE run: say no, out loud
    // The session layout is stored per DISPLAY SETUP (see layoutSlotKey() in
    // mainwindow.cpp): each monitor configuration keeps its own geometry, dock
    // arrangement, panadapter split and undo history, so a session on a smaller
    // screen can never overwrite the layout built for a bigger one.
    void saveLayout();                 // session auto-save (on close)
    void restoreLayout();              // session auto-restore (on launch)
    void fitWindowToScreen();          // clamp a restored window onto its screen
    void dumpDockMinimums();           // LYRA_DUMP_MINS=1 → lyra_min_sizes.txt
    void flushLayoutToSettings();      // persist live geometry+dock state now
                                       // (so export/snapshot reflect the screen)
    // Operator-driven layout management (View menu), mirroring old Lyra:
    void saveUserLayout();             // snapshot current as "my default"
    void restoreUserLayout();          // jump back to the saved default
    void applyDefaultLayout();         // factory arrangement (built-in)
    // Four operator-designed, named layout slots (+ the Lyra factory default
    // = 5 recallable arrangements).  Save snapshots the current dock state
    // into slot 1-4 under a chosen name; recall restores it.  Persisted under
    // QSettings "layouts/<slot>/{name,geometry,windowState,panadapterSplit}".
    void saveNamedLayout(int slot);
    void recallNamedLayout(int slot);
    void refreshLayoutMenus();         // refresh slot labels on menu open
    // Multi-rig: the menu-bar "Rig" dropdown — rebuilt from the rig
    // registry on open; switch = set active rig + restart-to-load.
    void rebuildRigMenu();
    void switchRig(const QString &rigId);
    void addRigInteractive();
    void renameActiveRig();
    QMenu *rigMenu_ = nullptr;
    // Layout undo (randol request): snapshot the dock arrangement before each
    // change so a bad drag-drop can be walked back a step or two (View →
    // Layouts → Undo layout change).  A dock move / float / dock arms a debounce
    // that coalesces one drag's several signals into a single snapshot; undo
    // restores the previous arrangement, so a mis-dropped panel goes back where
    // it came from.  The stack is PERSISTED per display setup: an operator
    // usually notices the damage the next morning, not before they close.
    void initLayoutUndo();             // wire signals + baseline (after restore)
    void onLayoutMaybeChanged();       // a dock moved/floated → arm the debounce
    void commitLayoutSnapshot();       // debounce fired → push the pre-change state
    void undoLayoutChange();           // pop + restore one step
    void refreshLayoutUndoAction();    // enable/relabel the menu action
    void loadLayoutUndoStack();        // this setup's history, from QSettings
    void saveLayoutUndoStack();        // persist it
    // Export/import the full settings profile (layout + all prefs) to a
    // portable .lyra file — backup, transfer to another machine, or
    // instant recovery after layout tinkering.  Machine-specific keys
    // (graphics backend) are deliberately NOT carried.
    void exportSettings();
    void importSettings();
    void ensureSettingsDialog();   // construct settingsDlg_ if needed (no show)
    void openSettings();
    // Per-panel "?" badge targets (driven by the Help bridge).
    void showHelp(const QString &topic);        // open the User Guide at topic
    void openSettingsTopic(const QString &topic);// Settings tab, or guide fallback
    // Right-click → guide-section help for header toggle chips with no dock.
    void attachChipHelp(class QToolButton *chip, const QString &topic);

    // Collapsible-panel docks (EqPanel/SpeechPanel etc.): when the QML root's
    // `collapsed` property toggles, shrink the host dock to the title strip
    // (and release it on expand) so a collapsed panel isn't a strip floating
    // in a full-size dock.  SizeRootObjectToView ignores the QML
    // implicitHeight, so the dock size has to be driven from C++.
private slots:
    void syncCollapsibleDock();
private:
    // TX-0c-pa-debug — refresh the HL2 telemetry strip on the right
    // side of the status bar (T / V / PA current).  Driven by
    // HL2Stream::statsChanged (~5 Hz).  PA current goes red+bold when
    // ≥50 mA (the operator-visible "RF on the air" indicator for the
    // first-RF bench and beyond).
    void refreshHl2TelemetryStrip();

    QObject *discovery_  = nullptr;
    QObject *stream_     = nullptr;
    // Session recorder (#201): engine + the always-visible status-bar "● REC"
    // chip (shown only while recording; click-to-stop).
    lyra::recorder::RecorderEngine *recorder_ = nullptr;
    lyra::recorder::SessionConverter *converter_ = nullptr;   // #201 offline MP4
    QToolButton                    *recChip_  = nullptr;
    QObject *wdsp_       = nullptr;
    QObject *wdspEngine_ = nullptr;
    Prefs   *prefs_      = nullptr;
    TunerMemory *tuner_  = nullptr;   // manual-ATU memory (Tuner panel)
    lyra::wx::WxService *wx_ = nullptr;
    lyra::solar::SolarService *solar_ = nullptr;   // HamQSL solar/propagation
    NcdxfFollow         *ncdxfFollow_ = nullptr;   // NCDXF beacon auto-follow
    WxIndicator         *wxIndicator_ = nullptr;   // header alert badges
    QSystemTrayIcon     *tray_ = nullptr;          // lazy, for wx toasts
    // TX-0c-pa-debug — HL2 telemetry strip on the right of the status
    // bar.  Permanent widget (never displaced by transient
    // showMessage); renders "HL2: T 24.7°C  V 12.3 V  PA 0.00 A" from
    // the existing TX-0a Q_PROPERTYs.  PA current ≥50 mA → bold red
    // (operator-visible RF indicator + B-pa bench observable + the
    // §15.20 kill-test PA-bias-drop signal).
    QLabel              *hl2TelemLabel_ = nullptr;

    QHash<QString, QDockWidget *> docks_;
    QAction                    *lockAction_ = nullptr;
    bool                        panelsLocked_ = false;   // gates event() separator block
    bool                        spaceConsumedPress_ = false; // space-PTT: swallow paired release/repeats
    QAction                    *layoutRecallActs_[4]{};   // named-layout recall slots
    QAction                    *layoutSaveActs_[4]{};      // named-layout save slots
    QString                     layoutSource_;   // which layout this session came up in
    // Layout-undo state (persisted per display setup; see initLayoutUndo).
    QAction                    *layoutUndoAct_ = nullptr;   // View → Layouts → Undo
    QList<QByteArray>           layoutUndoStack_;            // pre-change states, newest last
    QByteArray                  layoutCurrent_;              // last settled arrangement
    QTimer                     *layoutSnapTimer_ = nullptr;  // debounce dock-change bursts
    bool                        layoutRestoring_ = false;    // guard our own restoreState
    QAction                    *startStopAction_ = nullptr;   // header Start/Stop
    QToolButton                *startStopBtn_ = nullptr;      // its backing button (green/red)
    QLabel                     *connStatus_ = nullptr;        // header conn status
    QLabel                     *tciStatus_  = nullptr;        // header TCI client indicator
    QLabel                     *spottedBadge_ = nullptr;      // "you've been spotted" header badge
    QTimer                     *spottedClearTimer_ = nullptr; // auto-clears the badge
    QLabel                     *clockLocal_ = nullptr;        // header local clock
    QLabel                     *clockUtc_   = nullptr;        // header UTC/Zulu clock
    QTimer                     *clockTimer_ = nullptr;        // 1 Hz clock tick
    UpdateChecker              *updateChecker_ = nullptr;     // GitHub release check
    QAction                    *updateAction_ = nullptr;      // toolbar "update available"
    QString                     pendingUpdateUrl_;            // release page to open
    bool                        updateCheckManual_ = false;   // manual vs startup check
    QMetaObject::Connection     scanConn_;                    // one-shot scan→open
    QMetaObject::Connection     scanDoneConn_;                // one-shot scan-finished (no-radio reset)
    QMetaObject::Connection     probeConn_;                   // one-shot probe→open/scan-fallback
    // Drives drag-to-dock for the custom title bars (see makeDockTitleBar);
    // restores tear-out / move / snap-to-edge re-dock / tabify that the
    // replaced title bar removed.  Installed as each title bar's event filter.
    DockDragController         *dragController_ = nullptr;

    // ── Optional grouped panel racks (Settings → Visuals → Panel layout) ─────
    // Instead of each DSP / Options tool panel floating individually from its
    // own header chip, the operator can opt (per set) to house the whole set in
    // one saved "rack" window with an internal show/hide toggle per module.
    // The mode is read at startup from Prefs (dspPanelsGrouped /
    // optionsPanelsGrouped); switching it takes effect on the next launch.
    // Closing a rack HIDES it (its members go with it — nothing spills onto the
    // screen); the rack chip re-summons it.  Individual mode is byte-identical
    // to the classic chip-per-panel behaviour.
    PanelRack   *dspRack_          = nullptr;
    PanelRack   *optionsRack_      = nullptr;
    QToolButton *dspRackChip_      = nullptr;   // header chip toggling the DSP rack
    QToolButton *optionsRackChip_  = nullptr;   // header chip toggling the Options rack

    // Build a rack from a member objectName list; reparent those docks into it,
    // wire its close→hide + a header chip.  Returns the rack (owned by `this`).
    PanelRack *buildRack(const QString &objName, const QString &title,
                         const QStringList &members);

    SettingsDialog             *settingsDlg_ = nullptr;
    Help                       *help_    = nullptr;   // QML→C++ help bridge
    HelpDialog                 *helpDlg_ = nullptr;   // in-app User Guide
    LogDialog                  *logDlg_  = nullptr;   // in-app diagnostic log
    Bands                      *bands_   = nullptr;   // amateur band table (QML)
    BandPlan                   *bandPlan_ = nullptr;  // band-plan overlay data (QML)
    StatusBus                  *statusBus_ = nullptr; // transient status messages (QML+C++)
    int                         lastBandState_ = -1;  // -1 unknown / 0 out-of-band / 1 in-band
    TimeSync                   *timeSync_ = nullptr;  // NTP clock-drift check
    BandMemory                 *bandMemory_ = nullptr;// per-band mode/dB-range memory
    GenSlots                   *gen_ = nullptr;       // GEN1/2/3 general-coverage slots
    TimeStations               *time_ = nullptr;      // HF time-station TIME cycle
    MemoryStore                *memory_ = nullptr;    // frequency memory bank
    EibiStore                  *eibi_  = nullptr;     // EiBi shortwave overlay
    SpotStore                  *spots_ = nullptr;     // DX-cluster spots (TCI)
    SpotHoleFeeder             *spotHole_ = nullptr;  // SpotHole REST spot source
    DxClusterFeeder            *dxCluster_ = nullptr; // DX-cluster telnet spot source
    TciServer                  *tci_   = nullptr;     // TCI server (logger/cluster)
    lyra::cat::SerialPtt       *serialPtt_ = nullptr; // serial PTT input (WSJT-X/VarAC keys Lyra)
    lyra::cat::SerialCwKey     *serialCwKey_ = nullptr; // #171 serial CW key input
    QList<lyra::cat::CatServer *> catServers_;        // Kenwood CAT serial servers (cat1..catN)
    MeterModel                 *meter_ = nullptr;     // RX S-meter (Horizon Arc / Plasma Bar)
    EqModel                    *eqModel_ = nullptr;   // #50 TX parametric EQ (EqPanel.qml)
    EqModel                    *rxEqModel_ = nullptr; // #59 RX parametric EQ (RxEqPanel.qml)
    CwMacroModel               *cwMacros_ = nullptr;  // #176 CW macro bank (CwConsolePanel.qml + F-keys)
    lyra::tx::ClipBank         *clipBank_ = nullptr;  // #89 voice-keyer clip bank (Clips ctx prop)
    lyra::tx::VoiceKeyer       *voiceKeyer_ = nullptr;// #89 voice keyer controller (VoiceKeyerPanel.qml + F-keys)
    SpeechModel                *speechModel_ = nullptr; // #88 speech rack (SpeechPanel.qml)
    CombinatorModel            *combinatorModel_ = nullptr; // #51 combinator (CombinatorPanel.qml)
    PlateModel                 *plateModel_ = nullptr;     // #52 plate reverb (PlatePanel.qml)
    lyra::profile::ProfileManager *profiles_ = nullptr; // TX/RX profile engine (Settings→Profiles)
    lyra::profile::CompanionLauncher *companion_ = nullptr; // #193 launch digital app on profile pick
    ProfileUi                  *profileUi_ = nullptr;  // native Save-Profile dialog (front panel)
    int                         driftSeverity_ = 0;   // 0 unknown/ok .. 2 warn .. 3 bad
    UsbBcd                     *usbBcd_  = nullptr;   // USB-BCD amp band output
};

} // namespace lyra::ui

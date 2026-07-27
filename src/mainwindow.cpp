// Lyra — QMainWindow dock shell.  See mainwindow.h.

#include "mainwindow.h"

#include "backup.h"
#include "bands.h"
#include "bandplan.h"
#include "statusbus.h"
#include "timesync.h"
#include "bandmemory.h"
#include "genslots.h"
#include "time_stations.h"
#include "memorystore.h"
#include "eibistore.h"
#include "spotstore.h"
#include "spothole_feeder.h"
#include "dxcluster_feeder.h"
// TX-rip Phase 1 (Q2): tci_mic_source.h removed; the TciMicSource type
// is being ripped along with the rest of the TX DSP subsystem and
// rebuilt from empty files per docs/TX_ARCHITECTURAL_MAPPING.md §10.3.
#include "tci_server.h"
#include "cat/SerialPtt.h"
#include "cat/SerialCwKey.h" // #171 serial CW key input
#include "cat/CatServer.h"
#include "metermodel.h"
#include "tunermemory.h"
#include "profile/ProfileManager.h"  // complete type for setContextProperty(QObject*)
#include "profile/CompanionLauncher.h" // #193 launch digital app on explicit profile pick
#include "eqmodel.h"                  // complete type for new EqModel + context property
#include "speechmodel.h"              // complete type for new SpeechModel + context property
#include "combinatormodel.h"          // complete type for new CombinatorModel + context property
#include "platemodel.h"               // complete type for new PlateModel + context property
#include "profileui.h"                // native Save-Profile dialog (front panel)
#include "wdsp_engine.h"
#include "help.h"
#include "helpdialog.h"
#include "logdialog.h"
#include "hl2_discovery.h"
#include "hl2_stream.h"
#include "rig/RigRegistry.h"   // multi-rig — the Rig menu (switch active rig)
#include "wdsp_engine.h"
#include "prefs.h"
#include "CwMacroModel.h"
#include "tx/ClipBank.h"
#include "tx/ClipRecorder.h"         // #89 C1 — recorder() mic-record tap feed
#include "tx/ClipRecorderPlayer.h"   // #89 B1 — player() seams (KeyFn/BlockedFn/fillBlock)
#include "recorder/RecorderEngine.h" // #201 — session recorder engine
#include "recorder/SessionConverter.h" // #201 — offline MP4 converter
#include <QImage>                    // #201 — panadapter snapshot grab
#include "tx/VoiceKeyer.h"
#include "wire/Ep6RecvThread.h"       // #89 B1 — ep6Thread().set_tx_clip_source(...)
#include "settingsdialog.h"
#include "dockdragcontroller.h"
#include "panelrack.h"
#include "usb_bcd.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QEvent>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDockWidget>
#include <QGridLayout>
#include <QGuiApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSizeGrip>
#include <QVBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTextEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QUuid>
#include <QCursor>
#include <QPixmap>
#include <QQmlContext>
#include <QQuickWidget>
#include <QQuickItem>
#include <QSettings>
#include <QVariant>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStyle>
#include <QToolBar>
#include <QStatusBar>
#include <QToolButton>
#include <QDateTime>
#include <QTimer>
#include <QDesktopServices>
#include <QPushButton>
#include <QUrl>
#include <QFileDialog>
#include <QStandardPaths>
#include <QProcess>
#include <QCoreApplication>

#include "updatechecker.h"
#include "wxservice.h"
#include "wxindicator.h"
#include "solarservice.h"
#include "solarpanel.h"
#include "ncdxffollow.h"

#include <QApplication>
#include <QDialog>
#include <QSystemTrayIcon>

#include <utility>

namespace lyra::ui {

namespace {
// Standard feature set for an UNlocked dock: drag, float, close.
constexpr QDockWidget::DockWidgetFeatures kUnlockedFeatures =
    QDockWidget::DockWidgetMovable |
    QDockWidget::DockWidgetFloatable |
    QDockWidget::DockWidgetClosable;

// A clearly-visible bottom-right resize handle for floating panels.  The
// native QSizeGrip paints faint diagonal lines that disappear on the dark
// theme, so we draw our own cyan corner and resize the top-level window
// directly on drag (works for a floating QDockWidget = a top-level window).
class ResizeGrip : public QWidget {
public:
    explicit ResizeGrip(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(20, 20);
        setCursor(Qt::SizeFDiagCursor);
        setToolTip(QObject::tr("Drag to resize this panel"));
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0x00, 0xe5, 0xff), 2.0));
        const int w = width(), h = height();
        for (int i = 0; i < 3; ++i) {
            const int o = 5 + i * 5;
            p.drawLine(w - o, h - 3, w - 3, h - o);
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            dragging_  = true;
            startG_    = e->globalPosition().toPoint();
            startSize_ = window()->size();
            e->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!dragging_) return;
        const QPoint d = e->globalPosition().toPoint() - startG_;
        QWidget *top = window();
        top->resize(qMax(top->minimumWidth(),  startSize_.width()  + d.x()),
                    qMax(top->minimumHeight(), startSize_.height() + d.y()));
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent *) override { dragging_ = false; }
private:
    bool   dragging_ = false;
    QPoint startG_;
    QSize  startSize_;
};

// Compact "move" cursor for the draggable dock title bars.  The stock
// Qt::SizeAllCursor uses the OS scheme's full-size four-arrow (chunky); we
// paint our own ~20 px glyph (white core + dark halo so it reads on any
// background) so it's a touch smaller and on-theme.  Built once, shared.
inline const QCursor &moveCursor() {
    static const QCursor c = [] {
        constexpr int s = 20;
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const int cx = s / 2, cy = s / 2, a = 7, h = 3;
        auto glyph = [&](const QColor &col, double w) {
            p.setPen(QPen(col, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(cx, cy - a, cx, cy + a);          // vertical
            p.drawLine(cx - a, cy, cx + a, cy);          // horizontal
            p.drawLine(cx, cy - a, cx - h, cy - a + h);  // up arrowhead
            p.drawLine(cx, cy - a, cx + h, cy - a + h);
            p.drawLine(cx, cy + a, cx - h, cy + a - h);  // down
            p.drawLine(cx, cy + a, cx + h, cy + a - h);
            p.drawLine(cx - a, cy, cx - a + h, cy - h);  // left
            p.drawLine(cx - a, cy, cx - a + h, cy + h);
            p.drawLine(cx + a, cy, cx + a - h, cy - h);  // right
            p.drawLine(cx + a, cy, cx + a - h, cy + h);
        };
        glyph(QColor(10, 16, 22), 3.4);   // dark halo
        glyph(Qt::white, 1.5);            // white core
        p.end();
        return QCursor(pm, cx, cy);
    }();
    return c;
}

// Panels that are hidden-by-default floating tool windows summoned by the
// header chip-strip (TX DSP rack + RX EQ + CW console/decoder) — NOT part of
// the always-visible main layout.  A layout recall / reset must NOT force
// these visible; their saved (usually hidden) state from restoreState()
// governs, so the operator's closed rack panels stay closed.
inline bool isChipSummonedPanel(const QString &objectName) {
    return objectName == QLatin1String("txspeech")
        || objectName == QLatin1String("txeq")
        || objectName == QLatin1String("txcombinator")
        || objectName == QLatin1String("txplate")
        || objectName == QLatin1String("rxeq")
        || objectName == QLatin1String("cwconsole")
        || objectName == QLatin1String("cwdecoder")
        || objectName == QLatin1String("voicekeyer")
        || objectName == QLatin1String("tuner")
        || objectName == QLatin1String("freqcal")
        || objectName == QLatin1String("recorder");
}

// Diagnostic: LYRA_TEST_SIZE=WxH opens the window at exactly that LOGICAL size.
// Qt lays panels out in logical pixels, so a 1366x768 window on any dev machine
// reproduces, number for number, what a minimum-spec operator sees full-screen —
// the dev box's own resolution and DPI scaling drop out.
//
// While it is set the session is READ-ONLY with respect to layout: saveLayout()
// and flushLayoutToSettings() both bail out.  Without that, closing the test
// window would write the squeezed arrangement over the operator's real
// ui/geometry + ui/windowState — and because the dev build shares
// HKCU\Software\N8SDR\Lyra-cpp with the installed one, it would take their
// installed layout down with it.  The diagnostic must not inflict the very
// injury it exists to measure.
//
// Returns an invalid QSize when unset or unparseable, which disables all of it.
inline QSize testWindowSize() {
    static const QSize sz = []() -> QSize {
        const QString v = QString::fromLatin1(qgetenv("LYRA_TEST_SIZE"))
                              .trimmed().toLower();
        if (v.isEmpty()) return {};
        const QStringList wh = v.split(QLatin1Char('x'), Qt::SkipEmptyParts);
        if (wh.size() != 2) return {};
        bool okW = false, okH = false;
        const int w = wh.at(0).toInt(&okW);
        const int h = wh.at(1).toInt(&okH);
        if (!okW || !okH || w < 320 || h < 240) return {};
        return QSize(w, h);
    }();
    return sz;
}

// Safe boot (--safe / LYRA_SAFE, set in main()): a GUARANTEED, NON-DESTRUCTIVE
// way in.  If the operator's saved layout is itself what crashes the restore,
// replaying it under safe boot would defeat the hatch — so restoreLayout() comes
// up in the factory arrangement for this session, and (exactly like the
// LYRA_TEST_SIZE diagnostic) the layout store is READ-ONLY: saveLayout() bails,
// so the operator's real slot is left untouched and restores normally on their
// next NORMAL launch.  Distinct from the ui/uiSafeReset crash-recovery path,
// which deliberately PURGES a slot proven bad by repeated crashes.
inline bool safeBootLayout() {
    return qEnvironmentVariableIsSet("LYRA_SAFE");
}

// The session layout is stored per DISPLAY SETUP, not in one global slot.
//
// A dock layout is only meaningful on the screen it was built for: panel widths
// SUM across a dock row, so the arrangement that fits a 2560-wide desktop cannot
// fit a 1366-wide laptop, and Qt will happily crush the panels to make it "fit".
// With a single global slot, closing that crushed session overwrites the good
// desktop arrangement and it is gone for good — one RDP session, one docking-
// station unplug, one resolution change, and the operator has lost a layout they
// spent an evening building.  That is the bug this key exists to make impossible.
//
// The key identifies the SETUP (every attached screen's available size, sorted),
// not the window, so it is known at startup before anything is restored, and it
// is stable across a reboot.  Each setup gets its own geometry + dock state +
// panadapter split + undo history, and no setup can write over another's.
//
// Under LYRA_TEST_SIZE the key is the test size instead, so the diagnostic looks
// like a fresh minimum-spec machine (falling back to the factory layout when
// that setup has never been seen) rather than the dev box's own arrangement.
inline QString layoutSlotKey() {
    if (const QSize ts = testWindowSize(); ts.isValid())
        return QStringLiteral("%1x%2").arg(ts.width()).arg(ts.height());
    QStringList parts;
    const auto screens = QGuiApplication::screens();
    parts.reserve(screens.size());
    for (const QScreen *sc : screens) {
        // geometry(), NOT availableGeometry() — the latter shrinks when the
        // taskbar is shown, so auto-hiding it would look like a brand-new
        // display setup and drop the operator into the factory layout.  Screen
        // resolution is the stable identity; the taskbar is not.
        const QSize g = sc->geometry().size();
        parts << QStringLiteral("%1x%2").arg(g.width()).arg(g.height());
    }
    parts.sort();   // screen enumeration order is not stable; the set is
    return parts.isEmpty() ? QStringLiteral("unknown") : parts.join(QLatin1Char('_'));
}

// QSettings prefix for this setup's session layout, e.g. "session/2560x1400/".
inline QString layoutSlotPrefix() {
    return QStringLiteral("session/") + layoutSlotKey() + QLatin1Char('/');
}

// Remove every persisted window-layout key so a crash-recovery launch can come
// up in the factory arrangement: this display's whole session slot subtree
// (geometry / windowState / panadapterSplit / named slots / undo), the legacy
// single-slot keys (which restoreLayout() would otherwise re-migrate into the
// slot and re-crash on), the user-default-layout keys, and the grouped-panel
// rack windows.  Non-layout settings — station, radio, DSP/TX profiles,
// calibration, colours — are deliberately left untouched.
inline void purgeSavedLayoutKeys() {
    QSettings s;
    s.beginGroup(QStringLiteral("session/") + layoutSlotKey());
    s.remove(QString());   // empty key clears every key in the current group
    s.endGroup();
    for (const char *k :
         {"ui/geometry", "ui/windowState", "ui/panadapterSplit",
          "ui/userGeometry", "ui/userWindowState", "ui/userPanadapterSplit"})
        s.remove(QLatin1String(k));
    s.remove(QStringLiteral("panelRack"));   // grouped-panel rack windows subtree
    s.sync();
}

// Global tooltip gate — swallows QWidget tooltip events app-wide when the
// operator turns tooltips off (Settings → Visuals).  QML ToolTips gate
// themselves on Prefs.tooltipsEnabled; this covers the QtWidgets side
// (Settings dialog, menus) so the one toggle is genuinely global.
class TooltipGate : public QObject {
public:
    explicit TooltipGate(Prefs *p, QObject *parent)
        : QObject(parent), prefs_(p) {}
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::ToolTip && prefs_ && !prefs_->tooltipsEnabled())
            return true;   // consume → no tooltip shown
        return QObject::eventFilter(o, e);
    }
private:
    Prefs *prefs_;
};
} // namespace

MainWindow::MainWindow(QObject *discovery, QObject *stream,
                       QObject *wdsp, QObject *wdspEngine,
                       Prefs *prefs, lyra::wx::WxService *wx,
                       lyra::profile::ProfileManager *profiles,
                       QWidget *parent)
    : QMainWindow(parent),
      discovery_(discovery), stream_(stream),
      wdsp_(wdsp), wdspEngine_(wdspEngine), prefs_(prefs), wx_(wx),
      profiles_(profiles) {
    setWindowTitle(QStringLiteral(
        "Lyra — Hermes Lite 2 / 2+ — v" LYRA_VERSION " (C++23 / Qt 6)"));
    setObjectName(QStringLiteral("LyraMainWindow"));
    resize(1100, 760);

    // Global tooltip gate (Settings → Visuals → Show tooltips).  Swallows
    // QWidget tooltip events app-wide when disabled; QML ToolTips honour
    // the same Prefs flag via their visible bindings.
    qApp->installEventFilter(new TooltipGate(prefs_, this));

    // App-wide key preview for space-bar PTT (Thetis KeyPreview equivalent).
    // MainWindow::eventFilter sees Key_Space BEFORE the focused control, so a
    // Mode/Step combo or a band button can no longer swallow it (Pierre
    // HS0ZRT: space sometimes popped the Mode dropdown instead of keying).
    qApp->installEventFilter(this);

    // Full QDockWidget feature set: nested + tabbed docks, animated,
    // grouped-drag handle — the panel-arranging UX the operator wants.
    setDockOptions(QMainWindow::AnimatedDocks |
                   QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);

    // Make the resize affordances grabbable on the dark theme.  The default
    // QMainWindow dock separators are ~3 px and unstyled (invisible here), so
    // operators were "hunting an invisible line" to resize docked panels —
    // widen them to 6 px with a cyan hover.  Scoped sub-control selectors
    // only (separator + the tabified-dock tab bar); nothing else is themed by
    // QSS so the palette-based look is untouched.
    setStyleSheet(QStringLiteral(
        "QMainWindow::separator{background:#0d141b;width:6px;height:6px;}"
        "QMainWindow::separator:hover{background:#00e5ff;}"
        "QTabBar::tab{background:#0d141b;color:#9fb3c8;"
        "padding:3px 12px;border:1px solid #1b2a36;border-bottom:none;}"
        "QTabBar::tab:selected{color:#00e5ff;border-color:#00e5ff;}"
        "QTabBar::tab:hover{color:#7ff7ff;}"));

    // Help bridge — exposed to every QML panel's "?" badge.  Created
    // before any makeQuick() so it can be set as a context property.
    help_ = new Help(this);
    connect(help_, &Help::guideRequested, this, &MainWindow::showHelp);
    connect(help_, &Help::settingsRequested,
            this, &MainWindow::openSettingsTopic);

    // Amateur band table — exposed to QML for the Band panel.
    bands_ = new Bands(this);

    // Band-plan data (sub-band segments / edges / landmarks per region)
    // — exposed to QML for the panadapter's top-strip overlay.  Reads
    // the region + layer toggles from Prefs.
    bandPlan_ = new BandPlan(prefs_, this);

    // Transient status messages (out-of-band advisory, "Tuned to …").
    // QML posts via the `Status` context property; C++ posts via show().
    statusBus_ = new StatusBus(this);
    statusBar()->setStyleSheet(QStringLiteral(
        "QStatusBar{background:#0c141c;color:#cdd9e5;}"
        "QStatusBar::item{border:0;}"));
    statusBar()->setSizeGripEnabled(false);
    connect(statusBus_, &StatusBus::message, this,
            [this](const QString &t, int ms) {
                statusBar()->showMessage(t, ms);
            });

    // TX-0c-pa-debug — HL2 telemetry strip on the right of the status
    // bar.  Permanent widget so transient `showMessage` (on the left)
    // never displaces it.  T/V are RX-anytime useful; PA current is
    // the LOAD-BEARING bench observable for B-pa first-RF bench + the
    // §15.20/§15.24-C kill-test (PA bias must drop within N sec of
    // taskkill).  At-rest text uses "—" so the strip always reads as
    // intentional UI even pre-Start (telemetry getters return NaN).
    hl2TelemLabel_ = new QLabel(this);
    hl2TelemLabel_->setText(tr("HL2: T —  V —  PA —"));
    hl2TelemLabel_->setStyleSheet(QStringLiteral(
        "QLabel{color:#8fa6ba;font-family:Consolas,Menlo,monospace;"
        "padding:0 8px;}"));
    hl2TelemLabel_->setToolTip(tr(
        "HL2 telemetry: temperature, supply voltage, PA bias current.  "
        "PA turns red ≥50 mA — when red, the gateware is producing RF.  "
        "Kill-test (§15.20): taskkill /F /IM lyra.exe mid-key; PA must "
        "drop to zero within ~13 sec (gateware EP2 watchdog)."));
    statusBar()->addPermanentWidget(hl2TelemLabel_);

    // ── Session recorder (#201) — engine + always-visible "● REC" chip ──
    // The engine is headless; the audio feed + Recorder control panel land in
    // a later stage.  The live "● REC hh:mm:ss" safety indicator is built on
    // the header toolbar (between the RX DSP and Options groups) so it's in
    // plain view while recording — see the toolbar section below.  Here we
    // create the engine, wire snapshots/errors, and expose it to QML.
    recorder_ = new lyra::recorder::RecorderEngine(this);
    connect(recorder_, &lyra::recorder::RecorderEngine::error, this,
            [this](const QString &m) { statusBar()->showMessage(m, 8000); });
    connect(recorder_, &lyra::recorder::RecorderEngine::snapshotDue, this,
            [this] { captureRecorderSnapshot(); });
    // Panel ⚙ shortcut → open Settings → Recording.
    connect(recorder_, &lyra::recorder::RecorderEngine::settingsRequested, this,
            [this] { openSettingsTopic(QStringLiteral("recorder")); });
    // Supply the engine the CURRENT (freqHz, mode) at start time so the session
    // folder + manifest are stamped without the engine reaching into the radio.
    recorder_->setContextProvider([this]() -> QPair<qint64, QString> {
        const qint64 f = stream_ ? stream_->property("rx1FreqHz").toLongLong() : 0;
        return qMakePair(f, prefs_ ? prefs_->mode() : QString());
    });
    // Refuse to record while the stream is stopped (would just capture silence).
    recorder_->setCanRecordProbe([this]() -> bool {
        auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
        return st && st->isRunning();
    });

    // #201 Stage 5 — offline MP4 converter.  Runs a below-normal-priority
    // ffmpeg as a separate process; the TX ⇄ convert mutual lock-out is
    // enforced here (probe = don't start while keyed; lockout = block keying
    // while converting) via HL2Stream, keeping the engine decoupled.
    converter_ = new lyra::recorder::SessionConverter(this);
    if (auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_)) {
        converter_->setTxActiveProbe([st] { return st->moxActive(); });
        converter_->setTxLockout([st](bool on) { st->setConvertLockout(on); });
    }
    connect(converter_, &lyra::recorder::SessionConverter::finished, this,
            [this](const QString &, const QString &out) {
                statusBar()->showMessage(tr("Saved %1").arg(out), 8000);
            });
    connect(converter_, &lyra::recorder::SessionConverter::error, this,
            [this](const QString &m) { statusBar()->showMessage(m, 8000); });

    if (auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_)) {
        connect(st, &lyra::ipc::HL2Stream::statsChanged, this,
                &MainWindow::refreshHl2TelemetryStrip);
        // Initial paint (will show — until first stats tick).
        refreshHl2TelemetryStrip();
    }

    // PC clock-drift check (NCDXF beacon timing is UTC-locked).  On-demand
    // via the header clock right-click menu; result drives the ⚠ on the
    // UTC clock + an advisory dialog.
    timeSync_ = new TimeSync(this);
    connect(timeSync_, &TimeSync::result, this,
            [this](double, const QString &srv, int sev, const QString &sum) {
        driftSeverity_ = sev;
        QString body = tr("Network time (%1):\n%2").arg(srv, sum);
        if (sev == TimeSync::Warn)
            body += tr("\n\nYour clock is drifting. NCDXF beacon markers and "
                       "Follow may show the wrong station near the 10-second "
                       "slot boundaries.");
        else if (sev == TimeSync::Bad)
            body += tr("\n\nYour clock is significantly off — NCDXF beacon "
                       "tracking will identify the wrong stations. Right-click "
                       "the clock and choose \"Sync time\".");
        statusBus_->show(sum, 5000);
        QMessageBox::information(this, tr("PC clock drift"), body);
    });
    connect(timeSync_, &TimeSync::failed, this, [this](const QString &why) {
        statusBus_->show(why, 5000);
        QMessageBox::warning(this, tr("PC clock drift"), why);
    });
    connect(timeSync_, &TimeSync::resyncDone, this,
            [this](bool ok, const QString &msg) {
        statusBus_->show(msg, 5000);
        if (ok) QTimer::singleShot(1500, this,
                                   [this]() { timeSync_->checkDrift(); });
        else QMessageBox::warning(this, tr("Sync time"), msg);
    });

    // Per-band memory: restores each band's mode + panadapter/waterfall dB
    // ranges as you move across band edges (saved live while on a band).
    bandMemory_ = new BandMemory(
        prefs_, qobject_cast<lyra::ipc::HL2Stream *>(stream_), this);

    // GEN1/2/3 general-coverage slots (shortwave / MW quick-tune).
    gen_ = new GenSlots(
        prefs_, qobject_cast<lyra::ipc::HL2Stream *>(stream_), this);

    // HF time-station TIME cycle (TIME button on the GEN row).
    time_ = new TimeStations(
        prefs_, qobject_cast<lyra::ipc::HL2Stream *>(stream_), this);

    // Frequency memory bank (Mem button + Settings → Bands → Memory).
    memory_ = new MemoryStore(
        prefs_, qobject_cast<lyra::ipc::HL2Stream *>(stream_), this);

    // EiBi shortwave-broadcaster overlay (Settings → Bands → SW Database).
    eibi_ = new EibiStore(prefs_, bands_, this);

    // RX signal-strength meter (Horizon Arc / Plasma Bar panel).
    // Source = WDSP RXA_S_PK (in-passband), standard HF S-unit scale.
    meter_ = new MeterModel(qobject_cast<lyra::ipc::HL2Stream *>(stream_),
                            qobject_cast<lyra::dsp::WdspEngine *>(wdspEngine_),
                            this);

    // Tuner panel — manual-ATU tuning memory (tracks the dial vs stored
    // Input/Output/Inductor points per antenna).  Pure UI + QSettings.
    tuner_ = new TunerMemory(qobject_cast<lyra::ipc::HL2Stream *>(stream_), this);

    // #50 TX parametric EQ model (drives EqPanel.qml, "Eq" context property).
    // Routes the TX mic rack through eqModel_->engine() (CMaster TX hook).
    eqModel_ = new EqModel(EqModel::Side::Tx, this);
    // TX-rack standalone persistence (bug fix 2026-07-17).  The TX EQ (and the
    // speech / combinator / plate stages below) are #49 profile fields, but —
    // unlike the RX EQ further down and EVERY Prefs-backed field (bandwidth,
    // mode, gains …) — they had NO independent QSettings persistence.  So on
    // restart the Prefs fields restored while the TX rack reset to flat/default,
    // leaving the active profile HALF-applied and the "● modified" light falsely
    // lit (live flat EQ != the active profile's stored curve).  Field report
    // (John S Williams): saved AM 9K profile, restart -> bandwidth restores, TX
    // EQ reverts flat, orange on; re-loading the profile fixes it.  Mirror the
    // RX EQ idiom: restore the live TX rack from QSettings at construction +
    // persist on every edit, so the rack survives restart exactly like the
    // bandwidth does.  A profile load still overwrites the rack as a unit (and
    // writes back here), and the startup default-apply (main.cpp) still wins
    // when a default profile is set.
    {
        QSettings s;
        const QString js = s.value(QStringLiteral("txeq/state")).toString();
        if (!js.isEmpty()) {
            const auto doc = QJsonDocument::fromJson(js.toUtf8());
            if (doc.isObject()) eqModel_->loadState(doc.object());
        }
        auto save = [this]() {
            QSettings st;
            st.setValue(QStringLiteral("txeq/state"),
                        QString::fromUtf8(QJsonDocument(eqModel_->saveState())
                                              .toJson(QJsonDocument::Compact)));
        };
        connect(eqModel_, &EqModel::bandsChanged, this, save);
        connect(eqModel_, &EqModel::bypassChanged, this, save);
        connect(eqModel_, &EqModel::makeupDbChanged, this, save);
    }

    // #176 CW macro bank (drives CwConsolePanel.qml, "CwMacros" context
    // property + the F1-F12 global accelerators in keyPressEvent).  Owns the
    // macros + the "current contact" row + token expansion + send-via-keyer.
    cwMacros_ = new CwMacroModel(
        prefs_, qobject_cast<lyra::ipc::HL2Stream *>(stream_), this);

    // #89 Voice keyer (Build 1) — the clip bank (Clips) + the panel controller
    // (VoiceKeyer) behind VoiceKeyerPanel.qml + the voice-mode F1-F12 keys.
    // Management (import / rename / gain / F-key / delete / folder) is live now;
    // Transmit / Review / Record light up when B1 wires the injector into the
    // live mic funnel (voiceKeyer_->setLive(true)).
    clipBank_   = new lyra::tx::ClipBank(this);
    voiceKeyer_ = new lyra::tx::VoiceKeyer(clipBank_, this);

    // #89 B1 — wire the injector into the live TX path.  Done ONCE here, in the
    // ctor, which runs before any connect starts the Ep6 reader thread, so
    // Ep6RecvThread::set_tx_clip_source satisfies its set-once-before-start
    // contract (the sink persists across reconnects; never cleared).  All three
    // seams are ALWAYS installed — but nothing keys until the operator opts in
    // (VoiceKeyer.live, default OFF) AND deliberately triggers OTA / an F-key.
    if (auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_)) {
        auto *player = voiceKeyer_->player();
        using PttSource = lyra::ipc::HL2Stream::PttSource;
        // Key via PttSource::Keyer — own-key discipline (same posture as VOX):
        // OTA asserts/releases only ITS key; a manual key / Stop aborts.
        player->setKeyFn([st](bool on) { st->requestMox(on, PttSource::Keyer); });
        // Never key over — and never drop — a higher-priority (Manual / HW-PTT /
        // TCI / Serial) key: blocked when the radio is already keyed by a
        // non-Keyer source.
        player->setBlockedFn([st]() {
            return st->moxActive() && st->pttSource() != PttSource::Keyer;
        });
        // Mic-funnel injection: while a clip transmits, feed its samples in
        // place of the live mic at the reference's mic→TX hand-off, so the clip
        // runs the full TXA chain (PS-safe).  Idle → the lock-free gate returns
        // false and the live mic flows unchanged (RX byte-identical to pre-#89).
        st->ep6Thread().set_tx_clip_source(
            [player](int n, double *buf) -> bool {
                if (!player->active()) return false;   // no lock on the idle path
                return player->fillBlock(n, buf);
            });
        // #89 Stage C — mic-record tap: read-only observer of the real mic
        // (before injection), feeds the recorder while a voice message records.
        auto *recorder = voiceKeyer_->recorder();
        st->ep6Thread().set_mic_record_tap(
            [recorder](int n, const double *iq) { recorder->feedMicPairs(iq, n); });
        // #89 C2 — RX-record tap: the post-RX-DSP "what you heard" audio feeds
        // the voice-keyer clip recorder while an RX clip records AND the #201
        // session recorder while a session records (both lock-free no-ops
        // otherwise — each gates on its own atomic).  One tap, two consumers.
        if (auto *we = qobject_cast<lyra::dsp::WdspEngine *>(wdspEngine_)) {
            auto *sessionRec = recorder_;
            we->setRxRecordTap(
                [recorder, sessionRec](const double *audio, int n) {
                    recorder->feedRxStereoDup(audio, n);
                    if (sessionRec) sessionRec->feedAudioDoubles(audio, n, 2);
                });
        }
    }

    // #59 RX parametric EQ model (drives RxEqPanel.qml, "RxEq" context
    // property).  The RX twin of the TX EQ — same engine, shaped on the
    // post-RXA receive audio instead of the mic.  R-2 wired
    // WdspEngine::setRxEqEngine to run engine()+analyzer() at the top of
    // dispatchAudioFrame (pre-tap; auto-bypassed in DIGU/DIGL; manual ON/OFF
    // any mode).  Persists standalone (QSettings "rxeq/*"), NOT in the TX
    // Profile (#49) — it's an RX listening setting like the RX bandwidth.
    rxEqModel_ = new EqModel(EqModel::Side::Rx, this);
    if (auto *we = qobject_cast<lyra::dsp::WdspEngine *>(wdspEngine_))
        we->setRxEqEngine(rxEqModel_->engine(), rxEqModel_->analyzer());
    {
        // Restore the saved RX EQ chain (bypass + makeup + all bands).
        QSettings s;
        const QString js = s.value(QStringLiteral("rxeq/state")).toString();
        if (!js.isEmpty()) {
            const auto doc = QJsonDocument::fromJson(js.toUtf8());
            if (doc.isObject()) rxEqModel_->loadState(doc.object());
        }
        // Persist on every edit (band / bypass / makeup).  Display-only
        // analyzer options are excluded by saveState(), as on the TX side.
        auto save = [this]() {
            QSettings st;
            st.setValue(QStringLiteral("rxeq/state"),
                        QString::fromUtf8(QJsonDocument(rxEqModel_->saveState())
                                              .toJson(QJsonDocument::Compact)));
        };
        connect(rxEqModel_, &EqModel::bandsChanged, this, save);
        connect(rxEqModel_, &EqModel::bypassChanged, this, save);
        connect(rxEqModel_, &EqModel::makeupDbChanged, this, save);
    }

    // #88 speech-rack model (drives SpeechPanel.qml) — Auto-AGC + De-esser,
    // pre-EQ in the mic rack (wired via SendpTxSpeechProcessor in main.cpp).
    speechModel_ = new SpeechModel(this);
    // Standalone persistence (see the TX EQ note above) — survive restart.
    {
        QSettings s;
        const QString js = s.value(QStringLiteral("txspeech/state")).toString();
        if (!js.isEmpty()) {
            const auto doc = QJsonDocument::fromJson(js.toUtf8());
            if (doc.isObject()) speechModel_->loadState(doc.object());
        }
        connect(speechModel_, &SpeechModel::changed, this, [this]() {
            QSettings st;
            st.setValue(QStringLiteral("txspeech/state"),
                        QString::fromUtf8(QJsonDocument(speechModel_->saveState())
                                              .toJson(QJsonDocument::Compact)));
        });
    }

    // #51 combinator model (drives CombinatorPanel.qml) — 5-band multiband
    // comp + SBC, after the EQ in the mic rack.  Wire-INERT until Stage 3
    // routes the rack through it (SendpTxCombinatorProcessor).
    combinatorModel_ = new CombinatorModel(this);
    // Standalone persistence (see the TX EQ note above) — survive restart.
    // Only the real state edits persist (bypass / params); metersChanged is a
    // poll tick and selectedBandChanged is UI-only, so neither is wired.
    {
        QSettings s;
        const QString js = s.value(QStringLiteral("txcombinator/state")).toString();
        if (!js.isEmpty()) {
            const auto doc = QJsonDocument::fromJson(js.toUtf8());
            if (doc.isObject()) combinatorModel_->loadState(doc.object());
        }
        auto save = [this]() {
            QSettings st;
            st.setValue(QStringLiteral("txcombinator/state"),
                        QString::fromUtf8(QJsonDocument(combinatorModel_->saveState())
                                              .toJson(QJsonDocument::Compact)));
        };
        connect(combinatorModel_, &CombinatorModel::bypassChanged, this, save);
        connect(combinatorModel_, &CombinatorModel::paramsChanged, this, save);
    }

    // #52 plate-reverb model (drives PlatePanel.qml) — last native rack
    // stage, after the Combinator.  Wire-INERT until Stage 3 registers
    // SendpTxPlateProcessor.
    plateModel_ = new PlateModel(this);
    // Standalone persistence (see the TX EQ note above) — survive restart.
    {
        QSettings s;
        const QString js = s.value(QStringLiteral("txplate/state")).toString();
        if (!js.isEmpty()) {
            const auto doc = QJsonDocument::fromJson(js.toUtf8());
            if (doc.isObject()) plateModel_->loadState(doc.object());
        }
        auto save = [this]() {
            QSettings st;
            st.setValue(QStringLiteral("txplate/state"),
                        QString::fromUtf8(QJsonDocument(plateModel_->saveState())
                                              .toJson(QJsonDocument::Compact)));
        };
        connect(plateModel_, &PlateModel::bypassChanged, this, save);
        connect(plateModel_, &PlateModel::paramsChanged, this, save);
    }

    // DX-cluster spots (pushed over TCI; drawn on the panadapter).
    spots_ = new SpotStore(prefs_, qobject_cast<lyra::ipc::HL2Stream *>(stream_),
                           qobject_cast<lyra::dsp::WdspEngine *>(wdspEngine_), this);
    // SpotHole REST feeder — a standalone DX-spot source (no SDRLogger+ /
    // cluster node needed) that pours into the same SpotStore bus.
    spotHole_ = new SpotHoleFeeder(spots_, prefs_,
                                   qobject_cast<lyra::ipc::HL2Stream *>(stream_), this);
    // DX-cluster telnet feeder — operator-chosen node into the same bus.
    dxCluster_ = new DxClusterFeeder(spots_, prefs_, this);
    // Toast when the operator's own callsign gets spotted (opt-in, with a
    // cooldown set in Settings → Network).  Tray balloon + status line.
    connect(spots_, &SpotStore::ownCallSpotted, this,
            [this](const QString &call, const QString &mode, qint64 hz,
                   const QString &text) {
        const QString mhz = QString::number(hz / 1.0e6, 'f', 3);
        const QString body = tr("%1 spotted on %2 MHz %3%4")
                                 .arg(call, mhz, mode,
                                      text.isEmpty() ? QString()
                                                     : QStringLiteral(" — ") + text);
        if (!tray_) { tray_ = new QSystemTrayIcon(windowIcon(), this); tray_->show(); }
        tray_->showMessage(tr("You've been spotted!"), body,
                           QSystemTrayIcon::Information, 8000);
        if (statusBus_) statusBus_->show(tr("★ ") + body, 8000);
        // Light the header badge (before the clocks) for a couple of minutes.
        if (spottedBadge_) {
            spottedBadge_->setText(tr("★ Spotted!"));
            spottedBadge_->setStyleSheet(
                QStringLiteral("QLabel{color:%1;font-weight:bold;}")
                    .arg(spots_ ? spots_->highlightColor()
                                : QStringLiteral("#ff4081")));
            spottedBadge_->setToolTip(body);
            if (spottedClearTimer_) spottedClearTimer_->start();
        }
    });

    // TCI server — lets loggers/cluster apps drive + read Lyra over a
    // WebSocket (Settings → Network).  START/STOP from a client route
    // through onStartStop, guarded so they only act when state differs.
    tci_ = new TciServer(prefs_, qobject_cast<lyra::ipc::HL2Stream *>(stream_),
                         qobject_cast<lyra::dsp::WdspEngine *>(wdspEngine_),
                         spots_, this);
    // The TCI rx_channel_sensors S-meter broadcast reads its dBm from the
    // MeterModel (created above) so the wire matches the on-screen meter's
    // single calibration.  MeterModel outlives TciServer (both parented to
    // this window), so the raw pointer is safe for the app lifetime.
    tci_->setMeterModel(meter_);
    // Lyra ↔ SDRLogger+ Combo link: the TCI server shares the CW Console
    // contact row (CwMacros, created above) with a linked logger.  Same
    // lifetime story — both parented to this window.
    tci_->setCwMacros(cwMacros_);
    // Serial PTT input — a digital app (WSJT-X / VarAC / fldigi) asserts
    // RTS/DTR on a (virtual) COM port → crosses to our CTS/DSR → keys Lyra
    // (which keys the rig downstream).  Default-OFF; configured in
    // Settings → CAT / Serial.  See docs/architecture/com_port_design.md.
    serialPtt_ = new lyra::cat::SerialPtt(
        qobject_cast<lyra::ipc::HL2Stream *>(stream_), this);
    connect(serialPtt_, &lyra::cat::SerialPtt::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });

    // #171 — serial CW key input: a straight key / bug / external keyer's KEY
    // output wired to a COM port's CTS/DSR → keys Lyra's CW (host drives cwx).
    // Default-OFF; configured in Settings → CW.  (The HL2 KEY jack stays the
    // lower-jitter route for a paddle — gateware iambic.)
    serialCwKey_ = new lyra::cat::SerialCwKey(
        qobject_cast<lyra::ipc::HL2Stream *>(stream_), this);
    connect(serialCwKey_, &lyra::cat::SerialCwKey::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });

    // Kenwood CAT serial servers (logger / digital-mode rig control over
    // virtual COM ports).  Several independent instances so different apps
    // each get their own CAT port to Lyra.  PS1/PS0 drive the connect flow
    // like TciServer.  Three instances → two even columns in Settings
    // (Serial PTT + CAT 1 left, CAT 2 + CAT 3 right).
    for (int i = 1; i <= 3; ++i) {
        auto *cat = new lyra::cat::CatServer(
            QStringLiteral("cat%1").arg(i), prefs_,
            qobject_cast<lyra::ipc::HL2Stream *>(stream_),
            qobject_cast<lyra::dsp::WdspEngine *>(wdspEngine_), this);
        connect(cat, &lyra::cat::CatServer::statusMessage, this,
                [this](const QString &msg) { statusBar()->showMessage(msg, 4000); });
        connect(cat, &lyra::cat::CatServer::startRequested, this, [this]() {
            auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
            if (st && !st->isRunning()) onStartStop();
        });
        connect(cat, &lyra::cat::CatServer::stopRequested, this, [this]() {
            auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
            if (st && st->isRunning()) onStartStop();
        });
        catServers_.append(cat);
    }
    connect(tci_, &TciServer::startRequested, this, [this]() {
        auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
        if (st && !st->isRunning()) onStartStop();
    });
    connect(tci_, &TciServer::stopRequested, this, [this]() {
        auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
        if (st && st->isRunning()) onStartStop();
    });
    if (tci_->enabled()) tci_->start();

    // Solar / HF-propagation service — fetches HamQSL, derives per-band
    // ratings for the operator's day/night.  Drives the PROP dock.  It
    // re-arms itself on a Prefs location change internally.
    solar_ = new lyra::solar::SolarService(prefs_, this);

    // #193 Companion-app launcher.  When the operator EXPLICITLY picks a profile
    // (Settings "Load" or the front quick-recall dock → ProfileManager
    // ::userLoaded), optionally launch that profile's digital-mode app
    // (VarAC/MSHV/…) after a short delay so Lyra's CAT server + VAC come up
    // first.  Per-machine binding; never fires on the automatic mode-change
    // auto-recall or the startup default; never auto-closes the app.
    companion_ = new lyra::profile::CompanionLauncher(this);
    if (profiles_)
        connect(profiles_, &lyra::profile::ProfileManager::userLoaded,
                companion_, &lyra::profile::CompanionLauncher::launchFor);
    connect(companion_, &lyra::profile::CompanionLauncher::statusMessage,
            this, [this](const QString &m) { statusBar()->showMessage(m, 5000); });

    // NCDXF beacon auto-follow engine (drives the Solar panel's Follow
    // dropdown).  Needs the stream to QSY + Prefs to set CWU.
    ncdxfFollow_ = new NcdxfFollow(
        prefs_, qobject_cast<lyra::ipc::HL2Stream *>(stream_), this);

    // USB-BCD amp band output — follows the band off the stream's freq.
    usbBcd_ = new UsbBcd(this);
    if (auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_)) {
        connect(st, &lyra::ipc::HL2Stream::rx1FreqChanged, usbBcd_,
                [this, st]() { usbBcd_->applyForFreq(st->rx1FreqHz()); });
        usbBcd_->applyForFreq(st->rx1FreqHz());   // assert current band now

        // Band-plan in/out-of-band advisory: on a band-state transition
        // post a status message (gated like old Lyra on the edge-warning
        // layer + a real region).  The red edge lines are the visual half.
        connect(st, &lyra::ipc::HL2Stream::rx1FreqChanged, this,
                [this, st]() {
            if (!prefs_->bandPlanEdges()
                || prefs_->bandPlanRegion() == QLatin1String("NONE")) {
                lastBandState_ = -1;
                return;
            }
            const double f = double(st->rx1FreqHz());
            const QString name = bandPlan_->bandContaining(f);
            const int inb = name.isEmpty() ? 0 : 1;
            if (inb == lastBandState_) return;
            lastBandState_ = inb;
            if (inb)
                statusBus_->show(tr("In band: %1  (%2)")
                                     .arg(name, prefs_->bandPlanRegion()), 2500);
            else
                statusBus_->show(tr("⚠ Out of band — %1 MHz is outside the "
                                    "%2 amateur allocations")
                                     .arg(f / 1.0e6, 0, 'f', 3)
                                     .arg(prefs_->bandPlanRegion()), 5000);
        });

        // NOTE: the TX-time out-of-band advisory is rendered as a prominent,
        // live-updating banner on the panadapter (PanadapterPanel.qml
        // `txWarn` + banner), NOT a corner status message — it is emission-
        // aware (occupied bandwidth, not just the dial carrier) and its
        // reactive bindings re-check on every dial/bandwidth change WHILE
        // keyed, so tuning past the edge mid-transmit warns immediately.
        // Advisory only; nothing inhibits TX.
    }
    connect(usbBcd_, &UsbBcd::statusMessage, this, [](const QString &) {});

    // No central widget content: the old Main.qml dev scaffold (Log +
    // wire-stats banner + Found-radios) is retired — discovery moved to
    // Settings → Hardware, the log lives in the launching console, and
    // the wire-stats banner is past its usefulness.  A zero-height
    // placeholder collapses the central area so the docks (panadapter on
    // top, control panels below) fill the whole window — reclaiming the
    // real estate the scaffold used to waste.
    auto *placeholder = new QWidget(this);
    placeholder->setMaximumHeight(0);
    setCentralWidget(placeholder);

    // Native Save-Profile dialog helper — must exist before buildDocks()
    // (makeQuick exposes it to the ProfilePanel as the ProfileUi context
    // property).  `this` is the dialog parent.
    if (profiles_)
        profileUi_ = new ProfileUi(profiles_, prefs_, this);

    // Drag-to-dock controller — installed as the event filter on every custom
    // dock title bar (makeDockTitleBar), so it MUST exist before buildDocks().
    // A committed drag persists the layout immediately (save-on-commit).
    dragController_ = new DockDragController(this, &docks_, this);
    // Chip-summoned tool windows (TX/RX DSP racks, CW console, CW decoder) are
    // float-only: the controller must free-move them and NEVER dock them.
    dragController_->setFloatOnlyPredicate(&isChipSummonedPanel);
    connect(dragController_, &DockDragController::layoutChanged,
            this, &MainWindow::saveLayout);

    buildDocks();      // populate docks_ (so the View menu can list them)
    buildMenus();      // File / View (dock toggles + Lock) / Help
    buildToolbar();
    restoreLayout();   // geometry + dock state + lock state
    if (const QSize ts = testWindowSize(); ts.isValid()) {
        // Un-maximize first: restoreLayout() may have set WindowMaximized, and
        // resize() on a maximized window is ignored.
        setWindowState(windowState() & ~Qt::WindowMaximized);
        resize(ts);
        // Say WHICH layout this session came up in, right in the title bar —
        // a screenshot then answers "did the factory layout actually apply?"
        // without anyone having to infer it from the pixels.
        setWindowTitle(windowTitle()
                       + tr("  —  TEST %1×%2  ·  LAYOUT SAVING DISABLED  ·  %3")
                             .arg(ts.width()).arg(ts.height()).arg(layoutSource_));
        qWarning("[TEST] window forced to %dx%d logical px; saveLayout() and "
                 "flushLayoutToSettings() are suppressed for this session",
                 ts.width(), ts.height());
    }
    initLayoutUndo();  // baseline snapshot + hook dock move/float signals
    if (!qEnvironmentVariableIsEmpty("LYRA_DUMP_MINS"))
        QTimer::singleShot(1200, this, &MainWindow::dumpDockMinimums);
    // Backup & Restore — roll an automatic settings snapshot every N launches
    // (Settings → Backup & Restore).  Reads last session's persisted config,
    // so this is a genuine "last known good" you can fall back to.
    lyra::backup::autoSnapshotOnLaunch();

    // One-time gentle hint at the panel-arranging UX (answers tester
    // "what is an edge?" confusion).  Shown once ever, then the QSettings
    // flag suppresses it; the View menu's layout actions remain the
    // permanent discovery path.
    if (!QSettings().value(QStringLiteral("ui/dockHintShown"), false).toBool()) {
        statusBar()->showMessage(
            tr("Tip: drag a panel's title bar to move or re-dock it (a cyan "
               "guide shows where it will land) • double-click the title to "
               "float it • drag the bar between panels to resize • "
               "View ▸ Reset to default layout if things get messy"),
            16000);
        QSettings().setValue(QStringLiteral("ui/dockHintShown"), true);
    }
}

// LYRA_DUMP_MINS=1 — write out what every dock ACTUALLY needs, and what the
// window as a whole therefore needs.  This is how the published minimum screen
// size gets decided: by measuring the panels rather than estimating them.
//
// Read it like Qt does.  Panel widths SUM across a dock row; panel heights take
// the MAX of the row.  So the window's minimum height is the sum of its rows'
// tallest panels plus the panadapter plus the menu/toolbar/status chrome — and
// that sum is what has to fit inside the screen's available height (i.e. what's
// left after the taskbar).
void MainWindow::dumpDockMinimums() {
    const QString path = QCoreApplication::applicationDirPath()
                         + QStringLiteral("/lyra_min_sizes.txt");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&f);

    ts << "Lyra dock minimum sizes (logical px)\n";
    ts << "====================================\n\n";
    ts << "qtMinW/qtMinH = what Qt currently enforces (dock, incl. title bar)\n"
          "declW/declH   = the panel's own honest floor (QML lyraMinWidth/Height)\n"
          "The width Qt reports is meaningless until declW is pushed as a real\n"
          "minimum — SizeRootObjectToView throws the QML root's size away.\n\n";
    ts << QStringLiteral("%1 %2 %3 %4 %5   %6\n")
              .arg(QStringLiteral("dock"), -14)
              .arg(QStringLiteral("qtMinW"), 7)
              .arg(QStringLiteral("qtMinH"), 7)
              .arg(QStringLiteral("declW"), 7)
              .arg(QStringLiteral("declH"), 7)
              .arg(QStringLiteral("state"));
    ts << QString(62, QLatin1Char('-')) << "\n";

    QStringList names = docks_.keys();
    names.sort();
    for (const QString &n : std::as_const(names)) {
        QDockWidget *d = docks_.value(n);
        if (!d) continue;
        const QSize m = d->minimumSizeHint();
        int dw = -1, dh = -1;
        if (auto *qw = qobject_cast<QQuickWidget *>(d->widget())) {
            if (QObject *r = qw->rootObject()) {
                const QVariant vw = r->property("lyraMinWidth");
                const QVariant vh = r->property("lyraMinHeight");
                if (vw.isValid()) dw = vw.toInt();
                if (vh.isValid()) dh = vh.toInt();
            }
        }
        ts << QStringLiteral("%1 %2 %3 %4 %5   %6\n")
                  .arg(n, -14)
                  .arg(m.width(), 7)
                  .arg(m.height(), 7)
                  .arg(dw < 0 ? QStringLiteral("-") : QString::number(dw), 7)
                  .arg(dh < 0 ? QStringLiteral("-") : QString::number(dh), 7)
                  .arg(!d->isVisible()  ? QStringLiteral("closed")
                       : d->isFloating() ? QStringLiteral("floating")
                                         : QStringLiteral("docked"));
    }

    const QSize win = minimumSizeHint();
    ts << "\nwindow minimumSizeHint : " << win.width() << " x " << win.height()
       << "\nwindow size now        : " << width() << " x " << height() << "\n";
    if (menuBar())   ts << "menu bar height        : " << menuBar()->height() << "\n";
    if (statusBar()) ts << "status bar height      : " << statusBar()->height() << "\n";
    for (const QScreen *sc : QGuiApplication::screens()) {
        ts << "screen " << sc->geometry().width() << "x" << sc->geometry().height()
           << "  available " << sc->availableGeometry().width() << "x"
           << sc->availableGeometry().height() << "\n";
    }
    statusBar()->showMessage(tr("Wrote dock minimums to %1").arg(path), 10000);
    qInfo("[mins] wrote %s", qUtf8Printable(path));
}

QQuickWidget *MainWindow::makeQuick(const QString &qmlFile) {
    auto *qw = new QQuickWidget(this);
    qw->setResizeMode(QQuickWidget::SizeRootObjectToView);
    // Expose all four services to every panel; each binds what it uses.
    qw->rootContext()->setContextProperty(
        QStringLiteral("Discovery"), discovery_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Stream"), stream_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Wdsp"), wdsp_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("WdspEngine"), wdspEngine_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Prefs"), prefs_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Help"), help_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Bands"), bands_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("BandPlan"), bandPlan_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Status"), statusBus_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("BandMemory"), bandMemory_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Gen"), gen_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Time"), time_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Memory"), memory_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Eibi"), eibi_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Spots"), spots_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Meter"), meter_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Tuner"), tuner_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Profiles"), profiles_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("ProfileUi"), profileUi_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Eq"), eqModel_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("RxEq"), rxEqModel_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Speech"), speechModel_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Combinator"), combinatorModel_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Plate"), plateModel_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("CwMacros"), cwMacros_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Clips"), clipBank_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("VoiceKeyer"), voiceKeyer_);
    qw->rootContext()->setContextProperty(
        QStringLiteral("Recorder"), recorder_);   // #201 session recorder
    qw->rootContext()->setContextProperty(
        QStringLiteral("Converter"), converter_); // #201 offline MP4 converter
    qw->setSource(QUrl(QStringLiteral("qrc:/qt/qml/Lyra/src/qml/") + qmlFile));
    // Diagnostic: if a panel's QML fails to load, the QQuickWidget goes
    // blank — dump the errors so we don't have to guess.
    if (qw->status() == QQuickWidget::Error) {
        QFile f(QCoreApplication::applicationDirPath()
                + QStringLiteral("/lyra_qml_err.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << "== " << qmlFile << " ==\n";
            for (const auto &e : qw->errors()) {
                ts << e.toString() << "\n";
            }
        }
    }
    // Collapsible panels (EqPanel/SpeechPanel) expose a `collapsed` bool.
    // Drive the host dock's height off it so collapse shrinks the WHOLE dock
    // to the title strip (SizeRootObjectToView ignores QML implicitHeight, so
    // C++ has to clamp the widget).  Stash the host widget on the root so the
    // slot can find it via sender(); panels start expanded (no-op until used).
    QObject *root = qw->rootObject();
    const bool collapsible = root && root->property("collapsed").isValid();
    if (collapsible) {
        root->setProperty("_lyraHostQw", QVariant::fromValue<QObject *>(qw));
        connect(root, SIGNAL(collapsedChanged()),
                this, SLOT(syncCollapsibleDock()));
    }

    // Give Qt an honest minimum HEIGHT for the panel.
    //
    // SizeRootObjectToView stretches the QML root to the widget and DISCARDS the
    // root's implicit size — so Qt is told this panel's minimum is essentially
    // zero, and a dock row will happily come up smaller than the content needs.
    // The QQuickWidget then simply CLIPS what doesn't fit: the panel silently
    // loses controls off its edge, with no scrollbar and no cue that anything is
    // missing (the Tuning panel's RIT/XIT row, DisplayPanel's Spec FPS and WF
    // sliders).  Pushing a real minimum back to Qt makes that impossible.
    //
    // Prefer the panel's declared `lyraMinHeight` — its honest FLOOR, measured
    // from the content — over implicitHeight, which is only its PREFERRED size.
    // Conflating the two overstates what a panel needs (the panadapter prefers
    // 360px but is perfectly usable at 200), and an overstated minimum is how
    // you end up with a window that refuses to fit the screen.
    //
    // HEIGHT only, deliberately.  Minimum heights take the MAX across a dock
    // row, so they cost at most the tallest panel in it.  Minimum WIDTHS SUM
    // across a row, so they are only safe once we know the whole budget adds up.
    //
    // Skipped for collapsible panels: syncCollapsibleDock() drives their height
    // from C++ and calls setMinimumHeight(0) on expand, which would delete this.
    if (root && !collapsible) {
        const QVariant declared = root->property("lyraMinHeight");
        const int h = declared.isValid()
                          ? declared.toInt()
                          : qRound(root->property("implicitHeight").toReal());
        if (h > 0) qw->setMinimumHeight(h);
    }
    return qw;
}

void MainWindow::captureRecorderSnapshot() {
    // #201 — fired by RecorderEngine::snapshotDue while recording.  Grabs the
    // panadapter dock's QQuickWidget framebuffer (the spectrum + waterfall as
    // displayed — RX signal on receive, TX on transmit) to a PNG in the
    // session folder, then records the manifest entry.  A no-op if the panel
    // is hidden / not yet rendered (grabFramebuffer returns a null image).
    if (!recorder_ || !recorder_->isRecording()) return;
    QDockWidget *dock = docks_.value(QStringLiteral("panadapter"));
    if (!dock) return;
    auto *qw = dock->findChild<QQuickWidget *>();
    if (!qw) return;
    const QImage img = qw->grabFramebuffer();
    if (img.isNull()) return;
    const QString path = recorder_->reserveSnapshotFile();
    if (!img.save(path, "PNG")) return;
    const qint64 freq = stream_ ? stream_->property("rx1FreqHz").toLongLong() : 0;
    recorder_->noteSnapshot(path, freq, prefs_ ? prefs_->mode() : QString());
}

void MainWindow::syncCollapsibleDock() {
    QObject *root = sender();
    if (!root) return;
    auto *qw = qobject_cast<QQuickWidget *>(
        root->property("_lyraHostQw").value<QObject *>());
    if (!qw) return;
    const bool collapsed = root->property("collapsed").toBool();
    if (collapsed) {
        // Remember the expanded height so we can spring back, then pin the
        // widget to the collapsed strip height → the dock shrinks to fit.
        root->setProperty("_lyraExpandH", qw->height());
        const int strip =
            qMax(28, int(root->property("implicitHeight").toReal()));
        qw->setMinimumHeight(strip);
        qw->setMaximumHeight(strip);
    } else {
        // Expand: a bare resize() on the QQuickWidget does NOT grow the host
        // dock (the dock/splitter owns the size), so the panel would stay at
        // the strip until dragged.  Force it: release the max, pin the MINIMUM
        // to the target height (the dock layout grows the dock to satisfy it),
        // then drop the floor next tick so the operator can resize freely.
        qw->setMaximumHeight(QWIDGETSIZE_MAX);
        int eh = root->property("_lyraExpandH").toInt();
        const int impl = int(root->property("implicitHeight").toReal());
        if (eh < impl) eh = impl;       // collapsed-flag now false → expanded implicit
        if (eh < 60)   eh = 200;        // sanity floor
        qw->setMinimumHeight(eh);
        QTimer::singleShot(0, qw, [qw]() { qw->setMinimumHeight(0); });
    }
}

QDockWidget *MainWindow::addQuickDock(const QString &objectName,
                                      const QString &title,
                                      const QString &qmlFile,
                                      const QString &topic,
                                      Qt::DockWidgetArea area,
                                      bool resizable) {
    auto *dock = new QDockWidget(title, this);
    dock->setObjectName(objectName);   // load-bearing for saveState/restoreState
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(kUnlockedFeatures);
    QWidget *content = makeQuick(qmlFile);
    if (resizable) {
        // Wrap the QML surface so a clearly-visible custom resize handle sits
        // in its OWN thin strip BELOW the QML, not overlapping it.  (The native
        // QSizeGrip paints faint diagonal lines that vanish on the dark theme
        // AND a grip overlapping a QQuickWidget is hidden by the QML's
        // composited surface — so we draw our own cyan corner here.)  The grip
        // resizes the top-level window, so the strip shows only while the dock
        // floats (docked, it would resize the main window).
        auto *wrap = new QWidget(this);
        auto *vbox = new QVBoxLayout(wrap);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(0);
        vbox->addWidget(content, /*stretch*/ 1);
        auto *gripRow = new QWidget(wrap);
        gripRow->setStyleSheet(QStringLiteral("background:#0d141b;"));
        auto *gh = new QHBoxLayout(gripRow);
        gh->setContentsMargins(0, 0, 2, 2);
        gh->addStretch(1);
        auto *grip = new ResizeGrip(gripRow);
        gh->addWidget(grip, 0, Qt::AlignRight | Qt::AlignBottom);
        vbox->addWidget(gripRow);
        // Float by default for these panels, so start visible; the handler
        // hides the grip strip if the operator docks the panel.
        gripRow->setVisible(true);
        connect(dock, &QDockWidget::topLevelChanged, gripRow,
                [gripRow](bool floating) { gripRow->setVisible(floating); });
        content = wrap;
    }
    dock->setWidget(content);
    // Custom title bar: title (left) + "?" / float / close (right), so
    // the help badge sits in the title row OUTSIDE the panel content.
    dock->setTitleBarWidget(makeDockTitleBar(dock, title, topic));
    addDockWidget(area, dock);
    docks_.insert(objectName, dock);
    return dock;
}

QDockWidget *MainWindow::addWidgetDock(const QString &objectName,
                                       const QString &title,
                                       QWidget *content,
                                       const QString &topic,
                                       Qt::DockWidgetArea area) {
    auto *dock = new QDockWidget(title, this);
    dock->setObjectName(objectName);   // load-bearing for saveState/restoreState
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(kUnlockedFeatures);
    dock->setWidget(content);
    dock->setTitleBarWidget(makeDockTitleBar(dock, title, topic));
    addDockWidget(area, dock);
    docks_.insert(objectName, dock);
    return dock;
}

QWidget *MainWindow::makeDockTitleBar(QDockWidget *dock,
                                      const QString &title,
                                      const QString &topic) {
    auto *bar = new QWidget(dock);
    bar->setObjectName(QStringLiteral("dockTitleBar"));
    // Move cursor over the title strip → signals "drag me to move/dock"
    // (the gesture was previously undiscoverable).  The ?/float/close buttons
    // set their own pointing-hand cursor, so only the bar's empty area + the
    // title label (which inherits the bar's cursor) show the move cursor.
    // applyPanelLock swaps this to a plain arrow while panels are locked.
    bar->setCursor(moveCursor());
    // DockDragController restores the drag/double-click-float gestures the
    // custom title bar removes (drag-to-dock, snap-to-edge, tabify, tear-out).
    bar->installEventFilter(dragController_);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(8, 2, 4, 2);
    row->setSpacing(2);

    // Title — far left, cyan accent, matching the old-Lyra dock header.
    auto *titleLbl = new QLabel(title.toUpper(), bar);
    titleLbl->setStyleSheet(QStringLiteral(
        "QLabel{color:#00e5ff;font-weight:700;letter-spacing:1px;}"));
    row->addWidget(titleLbl);
    row->addSpacing(8);   // small gap before the "?"

    // "?" help badge — right AFTER the title text.  Click pops the Help
    // guide / Settings menu (same two-item choice as before).
    auto *helpBtn = new QToolButton(bar);
    helpBtn->setObjectName(QStringLiteral("dockHelp"));
    helpBtn->setText(QStringLiteral("?"));
    helpBtn->setCursor(Qt::PointingHandCursor);
    helpBtn->setAutoRaise(true);
    helpBtn->setToolTip(tr("Help / Settings for this panel"));
    helpBtn->setStyleSheet(QStringLiteral(
        "QToolButton{color:#00e5ff;border:1px solid #00e5ff;"
        "border-radius:8px;min-width:16px;max-width:16px;"
        "min-height:16px;max-height:16px;font-weight:700;padding:0;}"
        "QToolButton:hover{background:rgba(0,229,255,40);color:#7ff7ff;}"));
    auto *menu = new QMenu(helpBtn);
    QObject::connect(menu->addAction(tr("Help guide")), &QAction::triggered,
                     this, [this, topic]() { showHelp(topic); });
    QObject::connect(menu->addAction(tr("Settings…")), &QAction::triggered,
                     this, [this, topic]() { openSettingsTopic(topic); });
    connect(helpBtn, &QToolButton::clicked, bar, [helpBtn, menu]() {
        menu->exec(helpBtn->mapToGlobal(QPoint(0, helpBtn->height())));
    });
    row->addWidget(helpBtn);
    row->addStretch(1);   // push float/close to the far right

    // Float + close — styled like the "?" badge (cyan, visible on the
    // dark title bar; the old dark system icons were invisible).  Text
    // glyphs instead of QStyle standard icons so they actually show.
    const QString btnStyle = QStringLiteral(
        "QToolButton{color:#00e5ff;border:1px solid #00e5ff;"
        "border-radius:3px;min-width:16px;max-width:16px;"
        "min-height:16px;max-height:16px;font-weight:700;padding:0;}"
        "QToolButton:hover{background:rgba(0,229,255,40);color:#7ff7ff;}");

    auto *floatBtn = new QToolButton(bar);
    floatBtn->setObjectName(QStringLiteral("dockFloat"));
    floatBtn->setText(QStringLiteral("⧉"));   // ⧉ float/overlay glyph
    floatBtn->setCursor(Qt::PointingHandCursor);
    floatBtn->setStyleSheet(btnStyle);
    floatBtn->setToolTip(tr("Float this panel (rest it on top) / re-dock "
                            "— or double-click the title bar"));
    connect(floatBtn, &QToolButton::clicked, dock, [dock]() {
        const bool toFloat = !dock->isFloating();
        dock->setFloating(toFloat);
        if (toFloat) {           // make the popped-out window visible
            dock->raise();
            dock->activateWindow();
        }
    });
    row->addWidget(floatBtn);

    auto *closeBtn = new QToolButton(bar);
    closeBtn->setObjectName(QStringLiteral("dockClose"));
    closeBtn->setText(QStringLiteral("✕"));   // ✕ close glyph
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(btnStyle);
    closeBtn->setToolTip(tr("Close this panel (View menu re-opens it)"));
    connect(closeBtn, &QToolButton::clicked, dock, &QDockWidget::close);
    row->addWidget(closeBtn);

    return bar;
}

void MainWindow::buildDocks() {
    // Panadapter — Vulkan scene-graph spectrum (top by default).
    addQuickDock(QStringLiteral("panadapter"), tr("Panadapter"),
                 QStringLiteral("PanadapterPanel.qml"),
                 QStringLiteral("panadapter"), Qt::TopDockWidgetArea);
    // Tuning + Audio — ordinary QML control panels (bottom by default).
    // Proves the dock pattern works for non-Vulkan panels too; the
    // operator can drag/float/snap/tab any of them anywhere.
    addQuickDock(QStringLiteral("tuning"), tr("Tuning"),
                 QStringLiteral("TuningPanel.qml"),
                 QStringLiteral("tuning"), Qt::BottomDockWidgetArea);
    // Filters — RX/TX filter bandwidth (per-mode memory) + rate + BW lock.
    // Mode moved to the Tuning dock (under the VFO) in the VFO-cluster
    // layout; this panel is filters-only now.  Dock id stays "modefilter"
    // so saved window layouts (QSettings dock state) keep restoring it.
    addQuickDock(QStringLiteral("modefilter"), tr("Filters"),
                 QStringLiteral("ModeFilterPanel.qml"),
                 QStringLiteral("modes-filters"), Qt::BottomDockWidgetArea);
    addQuickDock(QStringLiteral("audio"), tr("Audio"),
                 QStringLiteral("AudioPanel.qml"),
                 QStringLiteral("audio"), Qt::BottomDockWidgetArea);
    // TX — front-facing TX controls (Drive %, PA Enable, MOX).  Moved
    // out of Settings (Drive + PA) and out of TuningPanel (MOX) so the
    // operator's TX surface is a single coherent dock that's docked /
    // floated / tabbed like every other panel.  Settings → Hardware →
    // Transmit keeps only the safety-timeout config (a set-once knob).
    addQuickDock(QStringLiteral("tx"), tr("TX"),
                 QStringLiteral("TxPanel.qml"),
                 QStringLiteral("tx"), Qt::BottomDockWidgetArea);
    // TX Speech (#88) — Noise Gate + Auto-AGC + De-esser, the pre-EQ mic
    // rack stage.  Its OWN dock so it moves / resizes / floats / collapses
    // like every Lyra panel; drag it onto the TX EQ dock to tab the two into
    // a "TX DSP" group, or place them side-by-side / float either.
    addQuickDock(QStringLiteral("txspeech"), tr("TX Speech"),
                 QStringLiteral("SpeechPanel.qml"),
                 QStringLiteral("txspeech"), Qt::BottomDockWidgetArea,
                 /*resizable=*/true);
    // TX EQ (#50) — 10-band parametric EQ.  Own dock (see TX Speech) — comes
    // after Speech in the chain (mic → Speech → EQ → WDSP TXA).
    addQuickDock(QStringLiteral("txeq"), tr("TX EQ"),
                 QStringLiteral("EqPanel.qml"),
                 QStringLiteral("txeq"), Qt::BottomDockWidgetArea,
                 /*resizable=*/true);
    // RX EQ (#59) — the RX twin of the TX EQ.  Reuses EqPanel.qml via the thin
    // RxEqPanel.qml wrapper (binds the RxEq model + the RX bandwidth as the
    // graph's band-edge source + the DIGU/DIGL auto-bypass set).  Own dock,
    // chip-summoned like the TX DSP rack (floating + hidden by default below).
    addQuickDock(QStringLiteral("rxeq"), tr("RX EQ"),
                 QStringLiteral("RxEqPanel.qml"),
                 QStringLiteral("rxeq"), Qt::BottomDockWidgetArea,
                 /*resizable=*/true);
    // TX Combinator (#51) — 5-band multiband comp + SBC.  Own dock (see TX
    // Speech) — after the EQ in the chain (mic → Speech → EQ → Combinator).
    addQuickDock(QStringLiteral("txcombinator"), tr("TX Combinator"),
                 QStringLiteral("CombinatorPanel.qml"),
                 QStringLiteral("txcombinator"), Qt::BottomDockWidgetArea,
                 /*resizable=*/true);
    // TX Plate (#52) — Schroeder-Moorer reverb.  Own dock — last in the
    // chain (mic → Speech → EQ → Combinator → Plate).
    addQuickDock(QStringLiteral("txplate"), tr("TX Plating"),
                 QStringLiteral("PlatePanel.qml"),
                 QStringLiteral("txplate"), Qt::BottomDockWidgetArea,
                 /*resizable=*/true);
    // CW Console (#105 CW-3b) — chip-launched floating panel for keyboard
    // CW send (+ the CW-5 decoder pane).  Its OWN dock; floats by default
    // (hidden until its header chip summons it) so the front panel stays
    // clean for the majority who never keyboard-send / decode CW.
    addQuickDock(QStringLiteral("cwconsole"), tr("CW"),
                 QStringLiteral("CwConsolePanel.qml"),
                 QStringLiteral("cwconsole"), Qt::BottomDockWidgetArea,
                 /*resizable=*/true);
    if (QDockWidget *d = docks_.value(QStringLiteral("cwconsole"))) {
        d->setFloating(true);
        d->hide();
    }
    // CW Decoder (#173 CW-5b) — separate from the CW console so a user can run
    // the decoder alone, the keyer alone, or both.  Own floating dock + "CW
    // Dec" chip; RX-only (decoded text + WPM + AFC-lock + knobs).  Binds the
    // WdspEngine cwDecode* surface.
    addQuickDock(QStringLiteral("cwdecoder"), tr("CW Decoder"),
                 QStringLiteral("CwDecoderPanel.qml"),
                 QStringLiteral("cwdecoder"), Qt::BottomDockWidgetArea,
                 /*resizable=*/true);
    if (QDockWidget *d = docks_.value(QStringLiteral("cwdecoder"))) {
        d->setFloating(true);
        d->hide();
    }
    // Voice Keyer (#89 Build 1) — floating clip-message panel (labelled clips,
    // ▶ OTA / ▶ Review / F-keys), chip-summoned like the CW console.  Own dock;
    // floats + hidden by default so the front panel stays clean.
    addQuickDock(QStringLiteral("voicekeyer"), tr("Voice Keyer"),
                 QStringLiteral("VoiceKeyerPanel.qml"),
                 QStringLiteral("voicekeyer"), Qt::BottomDockWidgetArea,
                 /*resizable=*/true);
    if (QDockWidget *d = docks_.value(QStringLiteral("voicekeyer"))) {
        d->setFloating(true);
        d->hide();
    }
    // Tuner — native manual-ATU memory (the "Tuner Reminder" idea built in):
    // tracks the dial vs the active antenna's stored Input/Output/Inductor
    // points + live colour-coded SWR.  Own floating dock, summoned by its
    // header "Tuner" chip; hidden by default so the front panel stays clean.
    addQuickDock(QStringLiteral("tuner"), tr("Tuner"),
                 QStringLiteral("TunerPanel.qml"),
                 QStringLiteral("tuner"), Qt::BottomDockWidgetArea,
                 /*resizable=*/true);
    if (QDockWidget *d = docks_.value(QStringLiteral("tuner"))) {
        d->setFloating(true);
        d->hide();
    }
    // Frequency Calibration — chip-launched floating instrument: tap a time
    // station, it auto-tunes USB + measures the carrier offset and suggests a
    // ppm correction (freq_calibration_design.md Stage 4).  Own floating dock,
    // hidden by default so the front panel stays clean.
    addQuickDock(QStringLiteral("freqcal"), tr("Freq Cal"),
                 QStringLiteral("CalibrationPanel.qml"),
                 QStringLiteral("freqcal"), Qt::BottomDockWidgetArea,
                 /*resizable=*/true);
    if (QDockWidget *d = docks_.value(QStringLiteral("freqcal"))) {
        d->setFloating(true);
        d->hide();
    }
    // #201 Session recorder — opt-in floating tool window (View → Recorder),
    // hidden by default so it never clutters the front panel.  REC/⏹ + timer
    // + snapshot toggle; the "● REC" status-bar chip is the always-on safety
    // light.  Chip-summoned so a layout reset can't force it open.
    addQuickDock(QStringLiteral("recorder"), tr("Recorder"),
                 QStringLiteral("RecorderPanel.qml"),
                 QStringLiteral("recorder"), Qt::RightDockWidgetArea,
                 /*resizable=*/true);
    if (QDockWidget *d = docks_.value(QStringLiteral("recorder"))) {
        d->setFloating(true);
        d->hide();
    }
    // (#175 Waterfall ID controls live inline on the TX panel under a
    // "Waterfall ID" section — no separate dock/chip.)
    // TX DSP launcher default: start each rack stage as a HIDDEN FLOATING
    // window so the front panel stays clean — the header "TX DSP" chip-strip
    // summons one (it pops as a movable/resizable float), and Save-my-layout
    // remembers what's open + where.  restoreLayout() below overrides this
    // with the operator's saved arrangement when one exists.
    for (const char *nm : {"txspeech", "txeq", "txcombinator", "txplate",
                           "rxeq"}) {
        if (QDockWidget *d = docks_.value(QString::fromLatin1(nm))) {
            d->setFloating(true);
            d->hide();
        }
    }
    // Profiles — front-facing quick recall of a saved TX/RX profile
    // (dropdown + Save + ● modified).  Full editor (create / rename /
    // delete / set-default / per-mode bind) is Settings → Profiles.
    addQuickDock(QStringLiteral("profiles"), tr("Profiles"),
                 QStringLiteral("ProfilePanel.qml"),
                 QStringLiteral("profiles"), Qt::BottomDockWidgetArea);
    // Display — front-facing spectrum/waterfall controls (palette,
    // waterfall speed, smoothing, glow, grid) binding the shared Prefs.
    addQuickDock(QStringLiteral("display"), tr("Display"),
                 QStringLiteral("DisplayPanel.qml"),
                 QStringLiteral("display"), Qt::BottomDockWidgetArea);
    // Band — HF/6m amateur band switching (tunes to each band default).
    addQuickDock(QStringLiteral("band"), tr("Band"),
                 QStringLiteral("BandPanel.qml"),
                 QStringLiteral("band"), Qt::BottomDockWidgetArea);
    // Meter — RX signal-strength S-meter (Horizon Arc / Plasma Bar).
    // Docks on the right by default (squarish); operator can drag/float it.
    addQuickDock(QStringLiteral("meter"), tr("Meter"),
                 QStringLiteral("MeterPanel.qml"),
                 QStringLiteral("meter"), Qt::RightDockWidgetArea);
    // Solar / Propagation — HamQSL SFI/A/K + 10-band day/night heat-map.
    // A plain QtWidgets strip (not QML); docks at the bottom like the
    // other control panels.
    addWidgetDock(QStringLiteral("propagation"), tr("Solar / Propagation"),
                  new SolarPanel(solar_, ncdxfFollow_, this),
                  QStringLiteral("propagation"), Qt::BottomDockWidgetArea);
}

// ── Optional grouped-panel rack (Settings → Visuals → Panel layout) ──────────
// Reparent a set of member docks OUT of the main window and into a fixed-
// membership PanelRack window; strip their main-window drag filter (a rack
// member doesn't move / float out) and restore the rack's saved geometry +
// which members are shown.  The rack is owned by `this`.
lyra::ui::PanelRack *MainWindow::buildRack(const QString &objName,
                                           const QString &title,
                                           const QStringList &members) {
    auto *rack = new lyra::ui::PanelRack(objName, title, this);
    for (const QString &m : members) {
        QDockWidget *d = docks_.value(m);
        if (!d) continue;
        removeDockWidget(d);                        // detach from the main window
        if (QWidget *tbar = d->titleBarWidget())
            tbar->removeEventFilter(dragController_);  // not draggable in a rack
        rack->addMember(d, d->toggleViewAction());
    }
    rack->restore();                                // geometry + member visibility
    return rack;
}

void MainWindow::buildMenus() {
    // File — Settings (stub) + Exit.
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Settings…"), QKeySequence(tr("Ctrl+,")),
                        this, &MainWindow::openSettings);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Export settings…"),
                        this, &MainWindow::exportSettings);
    fileMenu->addAction(tr("&Import settings…"),
                        this, &MainWindow::importSettings);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence(QKeySequence::Quit),
                        this, &QWidget::close);

    // Rig — switch between saved radio profiles (multi-rig).  Rebuilt from
    // the rig registry each time it opens; the active rig is checked.
    // Switching sets the active rig and offers a restart to load its
    // settings (restart-to-load MVP; live hot-reload is a later refinement).
    rigMenu_ = menuBar()->addMenu(tr("&Rig"));
    connect(rigMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRigMenu);

    // View — one show/hide toggle per dock + Lock panels.
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    for (auto it = docks_.cbegin(); it != docks_.cend(); ++it) {
        viewMenu->addAction(it.value()->toggleViewAction());
    }
    viewMenu->addSeparator();
    lockAction_ = viewMenu->addAction(tr("&Lock panels"));
    lockAction_->setCheckable(true);
    lockAction_->setShortcut(QKeySequence(tr("Ctrl+L")));
    connect(lockAction_, &QAction::toggled, this, [this](bool on) {
        applyPanelLock(on);
        QSettings().setValue(QStringLiteral("ui/panelsLocked"), on);
    });


    // Layout management: four operator-designed, named layout slots plus the
    // built-in Lyra factory default = five recallable arrangements.  Save
    // snapshots the current dock arrangement into a slot under a chosen name;
    // recall restores it.  Slot labels refresh (names / empty state) each time
    // the menu opens.
    viewMenu->addSeparator();
    QMenu *layoutsMenu = viewMenu->addMenu(tr("&Layouts"));

    // Layout undo — first item so it's the obvious "oops, put that back".
    // Disabled until the first dock move creates a snapshot.
    layoutUndoAct_ = layoutsMenu->addAction(
        tr("&Undo layout change"), this, &MainWindow::undoLayoutChange);
    layoutUndoAct_->setEnabled(false);
    layoutUndoAct_->setToolTip(
        tr("Move the last dragged panel(s) back where they came from "
           "(up to a few steps)."));
    layoutsMenu->addSeparator();

    QMenu *recallMenu = layoutsMenu->addMenu(tr("&Recall layout"));
    for (int i = 0; i < 4; ++i) {
        const int slot = i + 1;
        layoutRecallActs_[i] = recallMenu->addAction(
            tr("Slot %1").arg(slot), this,
            [this, slot]() { recallNamedLayout(slot); });
    }
    recallMenu->addSeparator();
    recallMenu->addAction(tr("Lyra &default (factory)"),
                          this, &MainWindow::applyDefaultLayout);

    QMenu *saveMenu = layoutsMenu->addMenu(tr("&Save current layout to"));
    for (int i = 0; i < 4; ++i) {
        const int slot = i + 1;
        layoutSaveActs_[i] = saveMenu->addAction(
            tr("Slot %1").arg(slot), this,
            [this, slot]() { saveNamedLayout(slot); });
    }
    // Refresh slot labels (names + empty/enabled state) whenever the menu opens.
    connect(layoutsMenu, &QMenu::aboutToShow,
            this, &MainWindow::refreshLayoutMenus);

    // Help — Quick Basics + User Guide + About.
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    // New-operator on-ramp: jumps the guide straight to the "Start here" section.
    helpMenu->addAction(tr("&Quick Basics (Start here)…"), this,
                        [this]() { showHelp(QStringLiteral("start-here--quick-basics")); });
    helpMenu->addAction(tr("&User Guide…"), QKeySequence(QKeySequence::HelpContents),
                        this, [this]() { showHelp(QString()); });
    helpMenu->addAction(tr("Check for &Updates…"), this,
                        [this]() { checkForUpdates(true); });
    helpMenu->addAction(tr("Show &Log…"), this, [this]() {
        if (!logDlg_) logDlg_ = new LogDialog(prefs_, this);
        logDlg_->show();
        logDlg_->raise();
        logDlg_->activateWindow();
    });
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About Lyra…"), this, [this]() {
        const QString ver = QStringLiteral(LYRA_VERSION);
        // Verbatim PayPal donate target from old Lyra (item_name text shows
        // on the PayPal page — keep the encoding byte-for-byte).  Kept out
        // of the tr()/.arg() body so its % escapes aren't treated as args.
        const QString payPal = QStringLiteral(
            "https://www.paypal.com/donate/?business=NP2ZQS4LR454L"
            "&no_recurring=0&item_name=Built+by+a+fellow+ham%2C+for+the+"
            "community.++Free+to+use%2C+free+to+share.+A+small+donation+"
            "keeps+the+code+flowing.+73+de+N8SDR&currency_code=USD");
        QMessageBox box(this);
        box.setWindowTitle(tr("About Lyra"));
        box.setTextFormat(Qt::RichText);
        box.setIcon(QMessageBox::Information);
        box.setText(
            tr("<h2 style='margin-bottom:2px'>Lyra "
               "<span style='color:#00e5ff'>v%1</span></h2>"
               "<p style='color:#8a9aac;margin-top:0'>"
               "Hermes Lite 2 / 2+ — native C++23 / Qt 6 rebuild</p>"
               "<p>A desktop SDR transceiver for the Hermes Lite 2 / 2+, "
               "rebuilt in native C++ (Qt Quick + Vulkan/RHI) — no Python, "
               "no GIL anywhere.</p>"
               "<p style='color:#8a9aac'><i>Named for Apollo's lyre and "
               "the constellation Lyra — home of Vega. (See the User "
               "Guide for the full story.)</i></p>"
               "<p>Author: <b>Rick Langford (N8SDR)</b><br>"
               "With <b>Brent Crier (N9BC)</b> and "
               "<b>Timmy Davis (KC8TYK)</b><br>"
               "Repository: <a href='https://github.com/N8SDR1/Lyra-SDR-cpp'>"
               "github.com/N8SDR1/Lyra-SDR-cpp</a><br>"
               "License: <b>GPL v3 or later</b></p>"
               "<p>Lyra is <b>free</b> and ad-free, built by a fellow ham "
               "for the community. If it's useful to you, consider buying "
               "the developer a coffee — 73 de N8SDR. ☕</p>"
               "<hr style='border-color:#1a2632'>"
               "<p style='color:#8a9aac;font-size:11px'>"
               "<b>Inspiration and references</b> — these projects were "
               "studied for conventions, ballistics, and protocol "
               "structure while building Lyra.  No source code was "
               "copied; each was a reference for \"how the standard "
               "idiom works.\":<br>"
               "Thetis SDR (openHPSDR) · PowerSDR · HermesLite 2 wiki "
               "and ak4951v4 gateware · pihpsdr · Quisk · linHPSDR · "
               "EESDR V3 · Behringer X-Air mixer series · SparkSDR.<br>"
               "Full credits and reference notes: see the "
               "<i>Credits and References</i> section in the User Guide.</p>"
               "<p style='color:#8a9aac;font-size:11px'>"
               "<b>Licensed components (GPL v3+)</b><br>"
               "• DSP engine: <b>WDSP</b> by Warren Pratt (NR0V).  Lyra "
               "links to the WDSP shared library and calls its public "
               "API; no source modifications.<br>"
               "• RX CW decoder: faithful source port of <b>fldigi</b>'s "
               "CW receive chain by Dave Freese (W1HKJ), with adaptive "
               "speed tracking by Lawrence Glaister (VE7IT).<br>"
               "• TCI server protocol — Expert Electronics public "
               "specification (EESDR).  Lyra implements a TCI server "
               "compatible with the v1.9 / v2.0 spec."
               "</p>"
               "<hr style='border-color:#1a2632'>"
               "<p style='color:#8a9aac;font-size:11px'>"
               "<b>Disclaimer — use at your own risk.</b>  Lyra is provided "
               "<b>“as is”</b>, with <b>no warranty</b> of any kind, express "
               "or implied (see the GPL v3+ for the full terms).  It "
               "controls a radio transmitter and drives external equipment; "
               "<b>you, the licensed operator, are solely responsible</b> "
               "for legal, correct, and safe operation — including staying "
               "within your licence privileges, band and power limits, and "
               "RF-exposure rules, and for protecting your amplifier, "
               "antenna, and other gear.  The authors accept "
               "<b>no liability</b> for any damage, interference, injury, "
               "loss, or violation arising from the use, misuse, "
               "misconfiguration, or malfunction of this software or any "
               "equipment connected to it.  Amplifier-protection features "
               "(watts cap, SWR fold, TX timeout, etc.) are aids, "
               "<b>not guarantees</b> — always verify with a dummy load and "
               "your own instruments before trusting them on the air."
               "</p>").arg(ver));
        QPushButton *donate =
            box.addButton(tr("☕ Donate via PayPal"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Close);
        box.setDefaultButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() == donate)
            QDesktopServices::openUrl(QUrl(payPal));
    });
}

void MainWindow::ensureSettingsDialog() {
    if (!settingsDlg_) {
        settingsDlg_ = new SettingsDialog(
            prefs_, qobject_cast<lyra::ipc::HL2Stream *>(stream_),
            qobject_cast<lyra::ipc::HL2Discovery *>(discovery_),
            usbBcd_, qobject_cast<lyra::dsp::WdspEngine *>(wdspEngine_),
            wx_, memory_, eibi_, tci_, spots_, spotHole_, dxCluster_, meter_, tuner_,
            voiceKeyer_, recorder_, converter_,
            profiles_, companion_, serialPtt_, serialCwKey_, catServers_, this);
    }
}

void MainWindow::openSettings() {
    ensureSettingsDialog();
    // Flush the live layout so an export/snapshot from the Backup & Restore
    // tab captures the current arrangement (not just the last close-save).
    flushLayoutToSettings();
    settingsDlg_->show();
    settingsDlg_->raise();
    settingsDlg_->activateWindow();
}

void MainWindow::showHelp(const QString &topic) {
    if (!helpDlg_) {
        helpDlg_ = new HelpDialog(this);
    }
    helpDlg_->showTopic(topic);
}

// ── Multi-rig: the "Rig" menu ──────────────────────────────────────────────

void MainWindow::rebuildRigMenu() {
    rigMenu_->clear();
    const QString active = lyra::rig::registry::activeRigId();
    const auto rigs = lyra::rig::registry::rigs();
    if (rigs.isEmpty()) {
        QAction *none = rigMenu_->addAction(tr("(no rigs yet)"));
        none->setEnabled(false);
    }
    for (const auto &r : rigs) {
        const QString label = r.label.isEmpty() ? r.rigId : r.label;
        QAction *a = rigMenu_->addAction(label);
        a->setCheckable(true);
        a->setChecked(r.rigId == active);
        const QString id = r.rigId;
        connect(a, &QAction::triggered, this, [this, id]() { switchRig(id); });
    }
    rigMenu_->addSeparator();
    rigMenu_->addAction(tr("Add rig…"), this, &MainWindow::addRigInteractive);
    if (!active.isEmpty())
        rigMenu_->addAction(tr("Rename active rig…"), this,
                            &MainWindow::renameActiveRig);
    rigMenu_->addAction(tr("Manage rigs (Settings → Hardware)…"),
                        this, &MainWindow::openSettings);
}

void MainWindow::switchRig(const QString &rigId) {
    if (rigId.isEmpty() || rigId == lyra::rig::registry::activeRigId())
        return;   // already active — nothing to do

    // Mid-TX guard — never yank the config or restart while keyed.
    //
    // Read the transmit state through the meta-object rather than casting
    // to a concrete stream class.  The rig registry now admits families
    // whose stream is not HL2Stream, and a cast-based guard FAILS OPEN on
    // every one of them: the cast returns null, the check is skipped, and
    // the switch (and its restart) is permitted mid-transmit.  A TX safety
    // interlock must fail CLOSED — if we cannot read a transmit state, we
    // refuse rather than assume the radio is receiving.
    //
    // txDisplayActive is (moxActive || cwKeyingActive), so this is exactly
    // the condition the cast-based guard tested — unchanged for HL2, and
    // now correct for any stream that publishes the property.
    if (stream_) {
        const QVariant tx = stream_->property("txDisplayActive");
        if (!tx.isValid()) {
            QMessageBox::warning(this, tr("Switch rig"),
                tr("Can't switch rigs — this radio doesn't report its "
                   "transmit state, so Lyra can't confirm it's safe to "
                   "restart."));
            return;
        }
        if (tx.toBool()) {
            QMessageBox::warning(this, tr("Switch rig"),
                tr("Can't switch rigs while transmitting — unkey first."));
            return;
        }
    }

    const auto r = lyra::rig::registry::rig(rigId);
    const QString label = r.label.isEmpty() ? rigId : r.label;

    // A Protocol-2 rig (Brick / ANAN G2) is registered for identity, but
    // Lyra can't bring it up yet — the P2 receive engine is deferred, and
    // the launch auto-connect would try to open it on the P1 stream.  Block
    // making it active until that engine lands, with an honest message.
    if (lyra::rig::capabilitiesFor(r.family).protocol == 2) {
        QMessageBox::information(this, tr("Switch rig"),
            tr("\"%1\" is a Protocol-2 radio — it's registered, but receive "
               "support is still in progress, so it can't be made the active "
               "rig yet.").arg(label));
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("Switch rig"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("Switch to \"%1\"?").arg(label));
    box.setInformativeText(
        tr("Lyra loads a rig's settings at startup, so it will restart to "
           "switch. Restart now, or switch on the next launch?"));
    QAbstractButton *restartBtn =
        box.addButton(tr("Restart now"), QMessageBox::AcceptRole);
    QAbstractButton *laterBtn =
        box.addButton(tr("Later"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    QAbstractButton *clicked = box.clickedButton();
    if (clicked != restartBtn && clicked != laterBtn)
        return;   // Cancel / closed — leave the active rig unchanged

    lyra::rig::registry::setActiveRigId(rigId);
    if (clicked == restartBtn) {
        // Relaunch a fresh instance, then close cleanly.  The new instance
        // seeds/loads the now-active rig and auto-connects to its lastIp.
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QCoreApplication::arguments().mid(1));
        close();
    }
    // "Later" — active rig recorded; loads on the next manual restart.
}

void MainWindow::addRigInteractive() {
    bool ok = false;
    const QString label = QInputDialog::getText(
        this, tr("Add rig"), tr("Rig name:"),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || label.isEmpty())
        return;
    const QString mac = QInputDialog::getText(
        this, tr("Add rig"),
        tr("MAC address (optional — e.g. aa:bb:cc:dd:ee:ff).\n"
           "Leave blank to pre-stage a rig before its hardware is present."),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok)
        return;

    QString rigId;
    if (!mac.isEmpty())
        rigId = lyra::rig::registry::ensureRig(
            mac, lyra::rig::RadioFamily::Unknown, label);
    if (rigId.isEmpty()) {
        // MAC-less (or unparseable MAC): synthesise a stable profile id.
        rigId = QStringLiteral("rig_")
                + QUuid::createUuid().toString(QUuid::Id128);
        lyra::rig::RigProfile p;
        p.rigId = rigId;
        p.label = label;
        p.mac   = mac;   // may be empty
        lyra::rig::registry::upsertRig(p);
    }
    QMessageBox::information(this, tr("Add rig"),
        tr("Added rig \"%1\". Pick it from the Rig menu to switch "
           "(Lyra restarts to load it).").arg(label));
}

void MainWindow::renameActiveRig() {
    const QString id = lyra::rig::registry::activeRigId();
    if (id.isEmpty())
        return;
    auto r = lyra::rig::registry::rig(id);
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Rename rig"), tr("Rig name:"),
        QLineEdit::Normal, r.label, &ok).trimmed();
    if (!ok || name.isEmpty() || name == r.label)
        return;
    r.label = name;
    lyra::rig::registry::upsertRig(r);
}

void MainWindow::openSettingsTopic(const QString &topic) {
    // A panel's "?" → Settings.  If a Settings tab owns this panel, jump
    // there.  If not (Filters, Tuning, the TX DSP-rack docks, CW decoder,
    // Solar — front-panel-only controls with no Settings home), fall back
    // to that panel's User-Guide section so the "?" always lands somewhere
    // useful instead of a dead / wrong tab.
    ensureSettingsDialog();
    if (settingsDlg_->selectTopic(topic)) {
        settingsDlg_->show();
        settingsDlg_->raise();
        settingsDlg_->activateWindow();
    } else {
        showHelp(topic);
    }
}

void MainWindow::attachChipHelp(QToolButton *chip, const QString &topic) {
    // Header toggle chips (CTUN / WF-ID) have no dock title bar, so no "?"
    // badge.  Give them the same reach: right-click → that feature's
    // User-Guide section.  Left-click stays the chip's toggle.
    chip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(chip, &QWidget::customContextMenuRequested, chip,
            [this, chip, topic](const QPoint &pos) {
                auto *menu = new QMenu(chip);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                connect(menu->addAction(tr("Help — what does this do?")),
                        &QAction::triggered, this,
                        [this, topic]() { showHelp(topic); });
                menu->popup(chip->mapToGlobal(pos));
            });
}

void MainWindow::buildToolbar() {
    // Header action strip — old Lyra's top section between the menu and
    // the panels: Start/Stop first, then a connection-status readout.
    // (Time + Lightning/Wind notifications slot in here later, matching
    // old Lyra's layout.)
    QToolBar *tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("main_toolbar"));
    tb->setMovable(true);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);

    startStopAction_ = tb->addAction(tr("▶  Start"));
    startStopAction_->setToolTip(
        tr("Connect to the radio and start streaming (and stop it)."));
    connect(startStopAction_, &QAction::triggered,
            this, &MainWindow::onStartStop);
    // Grab the backing toolbar button so updateConnState() can tint it
    // green (Start / stopped) vs red (Stop / running).
    startStopBtn_ = qobject_cast<QToolButton *>(
        tb->widgetForAction(startStopAction_));

    // Update-available indicator — hidden until a newer release is found.
    updateAction_ = tb->addAction(tr("⬆  Update"));
    updateAction_->setVisible(false);
    updateAction_->setToolTip(tr("A newer Lyra release is available — "
                                 "click to open the release page."));
    connect(updateAction_, &QAction::triggered, this, [this]() {
        if (!pendingUpdateUrl_.isEmpty())
            QDesktopServices::openUrl(QUrl(pendingUpdateUrl_));
    });

    tb->addSeparator();
    connStatus_ = new QLabel(tr("Disconnected"), tb);
    connStatus_->setContentsMargins(8, 0, 8, 0);
    tb->addWidget(connStatus_);

    // TCI client indicator — sits right after the radio connection status.
    // Hidden when the TCI server is off; amber dot = listening (no clients);
    // green dot + count = clients connected (loggers / cluster apps).
    tciStatus_ = new QLabel(tb);
    tciStatus_->setContentsMargins(4, 0, 8, 0);
    tb->addWidget(tciStatus_);
    if (tci_) {
        connect(tci_, &TciServer::runningChanged,
                this, &MainWindow::updateTciStatus);
        connect(tci_, &TciServer::clientCountChanged,
                this, [this](int) { updateTciStatus(); });
    }
    updateTciStatus();

    // ---- TX DSP launcher chip-strip ----
    // One checkable chip per native mic-rack stage; each chip IS that dock's
    // toggleViewAction, so a click summons the panel (floating, movable /
    // resizable) and a click (or closing it) tucks it away — the chip stays
    // in sync.  What's open + where rides Save-my-layout (saveState keys on
    // each dock's objectName).  #51 Combinator / #52 Plate add their names
    // here when their docks land.  In grouped mode (Settings → Visuals) the
    // whole DSP / Options set is housed in one saved "rack" window instead,
    // reached from a single chip.
    {
        // Boxed-chip stylesheet shared by every chip in this strip.
        static const char *kTxDspChipQss =
            "QToolButton{border:1px solid #3a4750;border-radius:4px;"
            "padding:2px 9px;margin:0 2px;color:#cfd8dc;background:#1c252b;}"
            "QToolButton:hover{border-color:#5b6b76;background:#243038;}"
            "QToolButton:checked{background:#2e7d9a;border-color:#7fd6ef;"
            "color:#ffffff;font-weight:600;}"
            "QToolButton:checked:hover{background:#3690ad;}";
        const bool dspGrouped = prefs_ && prefs_->dspPanelsGrouped();
        const bool optGrouped = prefs_ && prefs_->optionsPanelsGrouped();
        // Reparent a set of member docks into a PanelRack + add one bespoke
        // checkable chip that shows / hides that rack window.
        auto makeRackChip = [&](const QString &objName, const QString &label,
                                const QStringList &members)
                            -> QPair<lyra::ui::PanelRack *, QToolButton *> {
            lyra::ui::PanelRack *rack = buildRack(objName, label, members);
            auto *chip = new QToolButton(tb);
            chip->setText(label);
            chip->setCheckable(true);
            chip->setObjectName(QStringLiteral("txDspChip"));
            chip->setStyleSheet(QString::fromLatin1(kTxDspChipQss));
            chip->setToolTip(tr("Show / hide the %1 window (grouped panels).")
                                 .arg(label));
            chip->setChecked(rack->openByDefault());
            tb->addWidget(chip);
            connect(chip, &QToolButton::toggled, this, [rack](bool on) {
                if (on) { rack->show(); rack->raise(); rack->activateWindow(); }
                else    { rack->hide(); rack->store(); }
            });
            connect(rack, &lyra::ui::PanelRack::closed, chip, [chip]() {
                QSignalBlocker b(chip); chip->setChecked(false);
            });
            if (rack->openByDefault()) { rack->show(); rack->raise(); }
            return { rack, chip };
        };

        // ---- DSP panels: individual chips, OR one "DSP Rack" chip ----
        if (!dspGrouped) {
        tb->addSeparator();
        auto *txDspLabel = new QLabel(tr("TX DSP:"), tb);
        txDspLabel->setContentsMargins(8, 0, 4, 0);
        txDspLabel->setToolTip(tr("Mic-rack panels — click to open one as a "
                                  "movable window; layout is remembered."));
        tb->addWidget(txDspLabel);
        for (const char *nm : {"txspeech", "txeq", "txcombinator", "txplate"}) {
            if (QDockWidget *d = docks_.value(QString::fromLatin1(nm))) {
                QAction *act = d->toggleViewAction();
                tb->addAction(act);
                if (auto *btn = qobject_cast<QToolButton *>(
                        tb->widgetForAction(act))) {
                    btn->setObjectName(QStringLiteral("txDspChip"));
                    btn->setStyleSheet(QString::fromLatin1(kTxDspChipQss));
                }
            }
        }
        // (CW / CW Dec / Tuner chips live in the Options group below — they
        // are operating launchers, not part of the mic rack.)
        // RX DSP launcher (#59) — the RX EQ pops as a floating window, same
        // chip idiom as the TX DSP strip (the chip IS the dock's
        // toggleViewAction, so open/close stays in sync + rides Save-layout).
        // Its own labelled group since it shapes RX audio, not the mic rack.
        tb->addSeparator();
        auto *rxDspLabel = new QLabel(tr("RX DSP:"), tb);
        rxDspLabel->setContentsMargins(8, 0, 4, 0);
        rxDspLabel->setToolTip(tr("Receive-side panels — click to open one as "
                                  "a movable window; layout is remembered."));
        tb->addWidget(rxDspLabel);
        if (QDockWidget *d = docks_.value(QStringLiteral("rxeq"))) {
            QAction *act = d->toggleViewAction();
            act->setText(tr("RX EQ"));
            tb->addAction(act);
            if (auto *btn = qobject_cast<QToolButton *>(
                    tb->widgetForAction(act))) {
                btn->setObjectName(QStringLiteral("txDspChip"));
                btn->setStyleSheet(QString::fromLatin1(kTxDspChipQss));
            }
        }
        } else {
            // Grouped: one chip opens the DSP Rack window holding the whole set.
            tb->addSeparator();
            auto *dspLabel = new QLabel(tr("DSP:"), tb);
            dspLabel->setContentsMargins(8, 0, 4, 0);
            dspLabel->setToolTip(tr("DSP panels grouped into one window "
                                    "(Settings → Visuals → Panel layout)."));
            tb->addWidget(dspLabel);
            auto dr = makeRackChip(QStringLiteral("dsprack"), tr("DSP Rack"),
                                   QStringList{ QStringLiteral("txspeech"),
                                                QStringLiteral("txeq"),
                                                QStringLiteral("txcombinator"),
                                                QStringLiteral("txplate"),
                                                QStringLiteral("rxeq") });
            dspRack_ = dr.first; dspRackChip_ = dr.second;
        }
        // #201 Session recorder — ONE always-visible header chip between RX DSP
        // and Options.  Idle: a "Recorder" launcher chip (click pops the panel
        // out / hides it, same idiom as the DSP chips).  While recording: it
        // morphs into the live red "● REC hh:mm:ss" indicator (still opens the
        // panel — Stop lives there).
        if (recorder_) {
            const QString recIdleQss = QString::fromLatin1(kTxDspChipQss);
            const QString recRecQss = QStringLiteral(
                "QToolButton{color:#ffffff;background:#b3261e;"
                "border:1px solid #ff6b5e;border-radius:4px;padding:2px 9px;"
                "margin:0 4px;font-weight:700;"
                "font-family:Consolas,Menlo,monospace;}");
            recChip_ = new QToolButton(tb);
            recChip_->setCursor(Qt::PointingHandCursor);
            recChip_->setText(tr("⏺ Recorder"));
            recChip_->setStyleSheet(recIdleQss);
            recChip_->setToolTip(tr("Open the Session Recorder panel.  Turns into "
                                    "a live REC timer while recording."));
            // Pop the Recorder panel out (or hide it) — it's a float-only dock.
            connect(recChip_, &QToolButton::clicked, this, [this] {
                if (QDockWidget *d = docks_.value(QStringLiteral("recorder")))
                    d->toggleViewAction()->trigger();
            });
            tb->addWidget(recChip_);   // always visible
            connect(recorder_, &lyra::recorder::RecorderEngine::recordingChanged,
                    recChip_, [this, recIdleQss, recRecQss](bool on) {
                        recChip_->setStyleSheet(on ? recRecQss : recIdleQss);
                        recChip_->setText(on ? tr("● REC 00:00:00")
                                             : tr("⏺ Recorder"));
                    });
            connect(recorder_, &lyra::recorder::RecorderEngine::elapsed, this,
                    [this](qint64 ms) {
                        const qint64 s = ms / 1000;
                        recChip_->setText(QStringLiteral("● REC %1:%2:%3")
                            .arg(s / 3600,      2, 10, QLatin1Char('0'))
                            .arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
                            .arg(s % 60,        2, 10, QLatin1Char('0')));
                    });
        }

        // Operating toggles that are NOT part of a DSP rack — CTUN (a
        // tuning/display lock) and WF-ID (a TX courtesy ID).  Their own
        // labelled group with a leading separator so they don't read as
        // "RX DSP" functions sitting under that label.
        tb->addSeparator();
        auto *optsLabel = new QLabel(tr("Options:"), tb);
        optsLabel->setContentsMargins(8, 0, 4, 0);
        optsLabel->setToolTip(tr("Operating tools — center-tune lock (CTUN), the "
                                 "Tuner memory, the CW console / decoder, and the "
                                 "waterfall callsign ID (WF-ID)."));
        tb->addWidget(optsLabel);
        // CTUN (#174) — Center-tune lock.  NOT a dock; a state toggle on the
        // stream (lock the panadapter/DDC centre, tune the VFO within the
        // captured span — no waterfall scroll).  Same boxed chip as the row
        // but a GREEN "on" accent so it reads distinctly from the dock chips.
        // Engage snaps the centre to the current dial; the panadapter handles
        // marker-slide + tune-past-edge re-centre.
        if (auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_)) {
            static const char *kCtuneChipQss =
                "QToolButton{border:1px solid #3a4750;border-radius:4px;"
                "padding:2px 9px;margin:0 2px;color:#cfd8dc;background:#1c252b;}"
                "QToolButton:hover{border-color:#5b6b76;background:#243038;}"
                "QToolButton:checked{background:#2e7d4a;border-color:#7fefa0;"
                "color:#ffffff;font-weight:600;}"
                "QToolButton:checked:hover{background:#369055;}";
            auto *ctun = new QToolButton(tb);
            ctun->setText(tr("CTUN"));
            ctun->setCheckable(true);
            ctun->setChecked(st->ctuneEnabled());
            ctun->setObjectName(QStringLiteral("txDspChip"));
            ctun->setStyleSheet(QString::fromLatin1(kCtuneChipQss));
            ctun->setToolTip(tr("Center-tune lock — freeze the panadapter "
                                "centre and tune the VFO within the captured "
                                "span (no waterfall scroll). Click to engage "
                                "at the current dial.  Right-click for help."));
            tb->addWidget(ctun);
            attachChipHelp(ctun, QStringLiteral("ctun"));
            // chip -> stream (engage snaps the locked centre to the dial)
            connect(ctun, &QToolButton::toggled, st,
                    [st](bool on) { st->setCtuneEnabled(on); });
            // stream -> chip (auto-re-centre / external toggle keeps it synced)
            // + remember the on/off across restarts (main.cpp restores it once
            // the dial is at the persisted frequency).
            connect(st, &lyra::ipc::HL2Stream::ctuneChanged, ctun,
                    [st, ctun]() {
                        const bool on = st->ctuneEnabled();
                        QSignalBlocker block(ctun);
                        ctun->setChecked(on);
                        QSettings().setValue(QStringLiteral("ui/ctunEnabled"), on);
                    });
        }
        // ---- Options panels: individual chips, OR one "Options" chip ----
        // (Tuner / CW / CW Dec / Voice Keyer.  CTUN, Freq Cal and WF-ID always
        // stay individual — they are not part of the grouped set.)
        if (!optGrouped) {
        // Tuner launcher — manual-ATU memory panel (operating tool, not a DSP
        // rack), so it sits in Options.  The chip IS the dock's toggleViewAction.
        if (QDockWidget *d = docks_.value(QStringLiteral("tuner"))) {
            QAction *act = d->toggleViewAction();
            act->setText(tr("Tuner"));
            tb->addAction(act);
            if (auto *btn = qobject_cast<QToolButton *>(
                    tb->widgetForAction(act))) {
                btn->setObjectName(QStringLiteral("txDspChip"));
                btn->setStyleSheet(QString::fromLatin1(kTxDspChipQss));
            }
        }
        // CW console launcher (#105 CW-3b) — floating CW Console (keyboard send).
        if (QDockWidget *d = docks_.value(QStringLiteral("cwconsole"))) {
            QAction *act = d->toggleViewAction();
            act->setText(tr("CW"));
            tb->addAction(act);
            if (auto *btn = qobject_cast<QToolButton *>(
                    tb->widgetForAction(act))) {
                btn->setObjectName(QStringLiteral("txDspChip"));
                btn->setStyleSheet(QString::fromLatin1(kTxDspChipQss));
            }
        }
        // CW Decoder launcher (#173 CW-5b) — floating CW Decoder panel (RX-only).
        if (QDockWidget *d = docks_.value(QStringLiteral("cwdecoder"))) {
            QAction *act = d->toggleViewAction();
            act->setText(tr("CW Dec"));
            tb->addAction(act);
            if (auto *btn = qobject_cast<QToolButton *>(
                    tb->widgetForAction(act))) {
                btn->setObjectName(QStringLiteral("txDspChip"));
                btn->setStyleSheet(QString::fromLatin1(kTxDspChipQss));
            }
        }
        // Voice Keyer launcher (#89 Build 1) — floating clip-message panel.
        if (QDockWidget *d = docks_.value(QStringLiteral("voicekeyer"))) {
            QAction *act = d->toggleViewAction();
            act->setText(tr("Voice Keyer"));
            tb->addAction(act);
            if (auto *btn = qobject_cast<QToolButton *>(
                    tb->widgetForAction(act))) {
                btn->setObjectName(QStringLiteral("txDspChip"));
                btn->setStyleSheet(QString::fromLatin1(kTxDspChipQss));
            }
        }
        } else {
            // Grouped: one chip opens the Options Rack window.
            auto orr = makeRackChip(QStringLiteral("optionsrack"), tr("Options"),
                                    QStringList{ QStringLiteral("tuner"),
                                                 QStringLiteral("cwconsole"),
                                                 QStringLiteral("cwdecoder"),
                                                 QStringLiteral("voicekeyer") });
            optionsRack_ = orr.first; optionsRackChip_ = orr.second;
        }
        // Frequency Calibration launcher — floating WWV/time-station instrument.
        // Explicit QToolButton (like WF-ID) rather than the dock's
        // toggleViewAction: for a brand-new dock the toggle action can be inert
        // when a saved layout that predates the dock is restored, so drive the
        // dock's visibility directly.
        if (QDockWidget *d = docks_.value(QStringLiteral("freqcal"))) {
            auto *fc = new QToolButton(tb);
            fc->setText(tr("Freq Cal"));
            fc->setCheckable(true);
            fc->setChecked(d->isVisible());
            fc->setObjectName(QStringLiteral("txDspChip"));
            fc->setStyleSheet(QString::fromLatin1(kTxDspChipQss));
            fc->setToolTip(tr("Frequency calibration — tap a time station "
                              "(WWV / CHU) and measure your radio's ppm error.  "
                              "Right-click for help."));
            tb->addWidget(fc);
            connect(fc, &QToolButton::toggled, this, [d](bool on) {
                if (on) {
                    d->setFloating(true);
                    d->show();
                    d->raise();
                    d->activateWindow();
                } else {
                    d->hide();
                }
            });
            connect(d, &QDockWidget::visibilityChanged, fc, [fc](bool vis) {
                if (fc->isChecked() != vis)
                    fc->setChecked(vis);
            });
            attachChipHelp(fc, QStringLiteral("freqcal"));  // right-click → guide
        }
        // Waterfall ID (#175) ARM chip — NOT a dock, NOT a popup; a state
        // toggle on Prefs.wfIdEnabled.  Armed = the auto-ID is live (fires one
        // ID now + every N min); the Level / interval / Send controls + the
        // over-drive notice live inline on the TX panel.  Kept off the TX panel
        // so a fat-finger can't key it; amber "armed" accent so an armed
        // station reads at a glance.  Re-arms OFF every session (wfIdEnabled is
        // non-persistent) so it can never auto-key on a fresh launch.
        if (prefs_) {
            static const char *kWfIdChipQss =
                "QToolButton{border:1px solid #3a4750;border-radius:4px;"
                "padding:2px 9px;margin:0 2px;color:#cfd8dc;background:#1c252b;}"
                "QToolButton:hover{border-color:#5b6b76;background:#243038;}"
                "QToolButton:checked{background:#b07000;border-color:#ffc14d;"
                "color:#ffffff;font-weight:600;}"
                "QToolButton:checked:hover{background:#c98300;}";
            auto *wfid = new QToolButton(tb);
            wfid->setText(tr("WF-ID"));
            wfid->setCheckable(true);
            wfid->setChecked(prefs_->wfIdEnabled());
            wfid->setObjectName(QStringLiteral("txDspChip"));
            wfid->setStyleSheet(QString::fromLatin1(kWfIdChipQss));
            wfid->setToolTip(tr("Arm the waterfall callsign ID.  Armed = sends "
                                "your call as a waterfall image now + every N "
                                "min (set interval / Level on the TX panel).  "
                                "USB/LSB voice only — in digital your call is "
                                "already in the payload.  Re-arms OFF each "
                                "session.  Courtesy ID only.  Right-click for help."));
            tb->addWidget(wfid);
            attachChipHelp(wfid, QStringLiteral("wfid"));
            // chip -> Prefs (arm / disarm the auto-ID cadence)
            connect(wfid, &QToolButton::toggled, prefs_,
                    [this](bool on) { prefs_->setWfIdEnabled(on); });
            // Prefs -> chip (panel Send / external change keeps it synced)
            connect(prefs_, &Prefs::wfIdEnabledChanged, wfid,
                    [this, wfid]() {
                        QSignalBlocker block(wfid);
                        wfid->setChecked(prefs_->wfIdEnabled());
                    });
            // SSB-only (#175 operator rule): a waterfall ID only makes sense on
            // USB/LSB voice.  Disable the chip off-SSB; leaving SSB while armed
            // also disarms (can't / shouldn't ID in digital — the call is
            // already in the digital payload there).
            auto isSsbMode = [this]() {
                const QString u = prefs_->mode().toUpper();
                return u == QLatin1String("USB") || u == QLatin1String("LSB");
            };
            // SAFE arming (operator-locked): "you want it you must arm it."
            // ANY band-edge crossing OR mode change DISARMS the auto-ID, so a
            // courtesy ID can never carry over to a context the operator didn't
            // deliberately arm.  HAM-BANDS-ONLY (legal, region-aware): the chip
            // is dead anywhere outside an amateur allocation for the operator's
            // band-plan region — 11m / CB and every other out-of-band segment
            // (e.g. a US ham who spins onto 7.310 in the 41m broadcast band)
            // can never raster a callsign.  The hard fire-time guard lives in
            // WaterfallIdController; this greys + disarms the chip.
            auto *wfStream = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
            auto inHamBand = [this, wfStream]() {
                return wfStream && lyra::ui::amateurBandContains(
                           prefs_->bandPlanRegion(), prefs_->bandPlanCountry(),
                           static_cast<double>(wfStream->rx1FreqHz()));
            };
            auto armable = [isSsbMode, inHamBand]() {
                return isSsbMode() && inHamBand();
            };
            auto disarm = [this]() {
                if (prefs_->wfIdEnabled())
                    prefs_->setWfIdEnabled(false);
            };
            wfid->setEnabled(armable());
            // mode change → always disarm + re-grey
            connect(prefs_, &Prefs::modeChanged, wfid,
                    [wfid, armable, disarm]() {
                        disarm();
                        wfid->setEnabled(armable());
                    });
            // freq change → disarm only on a band-edge crossing (not every
            // in-band nudge), + re-grey.  Last band stashed on the button.
            wfid->setProperty("wfLastBand",
                wfStream ? lyra::bandIndexForFreq(
                               static_cast<int>(wfStream->rx1FreqHz())) : -1);
            if (wfStream)
                connect(wfStream, &lyra::ipc::HL2Stream::rx1FreqChanged, wfid,
                        [wfStream, wfid, armable, disarm]() {
                            const int b = lyra::bandIndexForFreq(
                                static_cast<int>(wfStream->rx1FreqHz()));
                            if (b != wfid->property("wfLastBand").toInt()) {
                                wfid->setProperty("wfLastBand", b);
                                disarm();
                            }
                            wfid->setEnabled(armable());
                        });
            // band-plan region / country change → the ham-band lockout is
            // region-aware, so a plan switch can move the current freq
            // out of band: re-grey + disarm if so.
            auto onPlanChanged = [wfid, armable, disarm]() {
                if (!armable()) disarm();
                wfid->setEnabled(armable());
            };
            connect(prefs_, &Prefs::bandPlanRegionChanged, wfid, onPlanChanged);
            connect(prefs_, &Prefs::bandPlanCountryChanged, wfid, onPlanChanged);
        }
    }

    // #94 External TX Inhibit — prominent always-visible indicator.  Hidden
    // unless the lockout is engaged; when on it shows a red "TX INHIBIT" chip
    // so the operator always knows why keying is dead (Settings → Hardware).
    if (auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_)) {
        auto *txInh = new QLabel(tr("⛔ TX INHIBIT"), tb);
        txInh->setContentsMargins(8, 0, 8, 0);
        txInh->setStyleSheet(QStringLiteral(
            "QLabel{color:#ffffff;background:#b3261e;border:1px solid #ff6b5e;"
            "border-radius:4px;padding:2px 9px;margin:0 4px;font-weight:700;}"));
        txInh->setToolTip(tr("Transmit is locked out (Settings → TX → "
                             "External TX Inhibit). The radio cannot key."));
        // Toggle the QAction the toolbar wraps the widget in — a hidden child
        // widget added via addWidget() does NOT reliably re-show on
        // widget->setVisible(true) inside a QToolBar.
        QAction *txInhAct = tb->addWidget(txInh);
        txInhAct->setVisible(st->txInhibit());
        connect(st, &lyra::ipc::HL2Stream::txInhibitChanged, txInhAct,
                [txInhAct](bool on) { txInhAct->setVisible(on); });
    }

    // ---- Local + UTC clocks (old-Lyra header layout) ----
    // A flexible spacer pushes the clocks toward the right of the strip,
    // matching old Lyra's "menu/notifications … [Local] [UTC]" placement.
    auto *clockSpacer = new QWidget(tb);
    clockSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(clockSpacer);

    // "You've been spotted" header badge — just BEFORE the clocks, with a
    // fixed width + right alignment so it never nudges the clocks (even if
    // a spot lands on the same tick the clocks update).  Cleared after a
    // short while by spottedClearTimer_.
    spottedBadge_ = new QLabel(tb);
    spottedBadge_->setContentsMargins(8, 0, 10, 0);
    spottedBadge_->setFixedWidth(120);
    spottedBadge_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    tb->addWidget(spottedBadge_);
    spottedClearTimer_ = new QTimer(this);
    spottedClearTimer_->setSingleShot(true);
    spottedClearTimer_->setInterval(120000);   // badge lingers ~2 min
    connect(spottedClearTimer_, &QTimer::timeout, spottedBadge_, [this]() {
        spottedBadge_->clear();
        spottedBadge_->setToolTip(QString());
    });

    clockLocal_ = new QLabel(QStringLiteral("--:--:--"), tb);
    clockLocal_->setContentsMargins(8, 0, 4, 0);
    clockLocal_->setStyleSheet(QStringLiteral(
        "QLabel{color:#ffd54f;font-family:Consolas,monospace;"
        "font-weight:700;font-size:22px;}"));     // amber = PC local
    clockLocal_->setToolTip(tr("PC local time"));
    tb->addWidget(clockLocal_);

    clockUtc_ = new QLabel(QStringLiteral("--:--:--Z"), tb);
    clockUtc_->setContentsMargins(4, 0, 8, 0);
    clockUtc_->setStyleSheet(QStringLiteral(
        "QLabel{color:#80d8ff;font-family:Consolas,monospace;"
        "font-weight:700;font-size:22px;}"));      // cyan = UTC/Zulu
    clockUtc_->setToolTip(tr("UTC / Zulu time — right-click to check PC "
                             "clock drift (matters for NCDXF beacon timing)"));
    tb->addWidget(clockUtc_);

    // Right-click either clock → NTP drift check / Windows time resync.
    for (QLabel *clk : {clockLocal_, clockUtc_}) {
        clk->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(clk, &QWidget::customContextMenuRequested, clk,
                [this, clk](const QPoint &p) {
                    showClockMenu(clk->mapToGlobal(p));
                });
    }
    clockLocal_->setToolTip(tr("PC local time — right-click to check PC "
                               "clock drift against network time"));

    // Trailing flexible spacer — balances clockSpacer on the left so the
    // clocks sit centered in the header strip rather than hard against
    // the right edge (old-Lyra placement).
    auto *clockSpacerR = new QWidget(tb);
    clockSpacerR->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(clockSpacerR);

    // Weather-alert badges — hard right of the clocks, toward the right
    // edge of the header (empty/invisible until an alert is active).
    if (wx_) {
        wxIndicator_ = new WxIndicator(wx_, tb);
        tb->addWidget(wxIndicator_);
        connect(wx_, &lyra::wx::WxService::toast, this,
                [this](const QString &title, const QString &body) {
            if (!tray_) {   // lazily create a tray icon for OS notifications
                tray_ = new QSystemTrayIcon(windowIcon(), this);
                tray_->show();
            }
            // Prefix so the OPERATOR can always tell a Lyra alert apart from
            // other weather notifiers (Ambient app, Windows widget, stale
            // Action-Center items) — and log it so the in-app Log records
            // exactly what Lyra emitted, when.
            const QString t = QStringLiteral("Lyra — ") + title;
            tray_->showMessage(t, body, QSystemTrayIcon::Warning, 8000);
            qWarning().noquote() << "[wx] DESKTOP NOTICE FIRED:" << t << "—" << body;
        });
        connect(wx_, &lyra::wx::WxService::chime, this,
                []() { QApplication::beep(); });
    }

    clockTimer_ = new QTimer(this);
    clockTimer_->setInterval(1000);
    connect(clockTimer_, &QTimer::timeout, this, &MainWindow::tickClocks);
    clockTimer_->start();
    tickClocks();   // paint immediately, don't wait 1 s

    // Reflect the live stream state into the button + status label.
    if (auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_)) {
        connect(st, &lyra::ipc::HL2Stream::runningChanged,
                this, &MainWindow::updateConnState);
    }
    updateConnState();

    // ---- GitHub update checker ----
    updateChecker_ = new UpdateChecker(this);
    connect(updateChecker_, &UpdateChecker::updateAvailable,
            this, &MainWindow::onUpdateAvailable);
    connect(updateChecker_, &UpdateChecker::noUpdate,
            this, &MainWindow::onNoUpdate);
    connect(updateChecker_, &UpdateChecker::checkFailed,
            this, &MainWindow::onUpdateCheckFailed);
    // Quiet startup check ~5 s after launch (operator opt-out via the
    // update_check/check_on_startup setting), matching old Lyra.
    QSettings st2;
    if (st2.value(QStringLiteral("update_check/check_on_startup"), true).toBool()) {
        QTimer::singleShot(5000, this, [this]() { checkForUpdates(false); });
    }
}

void MainWindow::checkForUpdates(bool manual) {
    if (!updateChecker_) return;
    updateCheckManual_ = manual;
    updateChecker_->check();
}

void MainWindow::onUpdateAvailable(const QString &tag, const QString &url,
                                   const QString &body) {
    pendingUpdateUrl_ = url;
    if (updateAction_) {
        updateAction_->setText(tr("⬆  %1").arg(tag));
        updateAction_->setVisible(true);
    }
    QSettings s;
    s.setValue(QStringLiteral("update_check/latest_tag"), tag);
    s.setValue(QStringLiteral("update_check/latest_url"), url);

    // Manual check → always show the result.  Startup check → modal only
    // the FIRST time per version, and never for a version the operator
    // chose to skip.
    if (!updateCheckManual_) {
        const QStringList skipped =
            s.value(QStringLiteral("update_check/skipped_versions"))
                .toStringList();
        if (skipped.contains(tag)) return;          // silenced this version
        const QStringList seen =
            s.value(QStringLiteral("update_check/modal_seen_versions"))
                .toStringList();
        if (seen.contains(tag)) return;             // already modalled; indicator only
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("Lyra update available"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<b>Lyra %1</b> is available "
                   "(you're running %2).")
                    .arg(tag, QStringLiteral(LYRA_VERSION)));
    if (!body.trimmed().isEmpty()) {
        QString notes = body.trimmed();
        if (notes.size() > 600) notes = notes.left(600) + QStringLiteral("…");
        box.setInformativeText(notes);
    }
    QPushButton *openBtn = box.addButton(tr("Open release page"),
                                         QMessageBox::AcceptRole);
    box.addButton(tr("Remind me later"), QMessageBox::RejectRole);
    QPushButton *skipBtn = box.addButton(tr("Skip this version"),
                                         QMessageBox::DestructiveRole);
    box.exec();

    if (box.clickedButton() == openBtn) {
        QDesktopServices::openUrl(QUrl(url));
    } else if (box.clickedButton() == skipBtn) {
        QStringList skipped =
            s.value(QStringLiteral("update_check/skipped_versions"))
                .toStringList();
        if (!skipped.contains(tag)) skipped << tag;
        s.setValue(QStringLiteral("update_check/skipped_versions"), skipped);
    }
    // Record that we've shown the modal for this version (so a later
    // startup check shows only the quiet toolbar indicator).
    QStringList seen =
        s.value(QStringLiteral("update_check/modal_seen_versions"))
            .toStringList();
    if (!seen.contains(tag)) seen << tag;
    s.setValue(QStringLiteral("update_check/modal_seen_versions"), seen);
}

void MainWindow::onNoUpdate() {
    if (updateCheckManual_) {
        QMessageBox::information(
            this, tr("Check for Updates"),
            tr("You're running the latest Lyra (%1).")
                .arg(QStringLiteral(LYRA_VERSION)));
    }
}

void MainWindow::onUpdateCheckFailed(const QString &reason) {
    if (updateCheckManual_) {
        QMessageBox::warning(
            this, tr("Check for Updates"),
            tr("Couldn't check for updates:\n%1").arg(reason));
    }
}

void MainWindow::tickClocks() {
    // Re-read the system clock each tick (never increment) so the
    // display can't drift from the OS time.
    const QDateTime now = QDateTime::currentDateTime();
    if (clockLocal_)
        clockLocal_->setText(now.toString(QStringLiteral("HH:mm:ss")));
    if (clockUtc_) {
        QString utc = now.toUTC().toString(QStringLiteral("HH:mm:ss")) +
                      QStringLiteral("Z");
        // ⚠ prefix when the last drift check found the clock off (warn/bad).
        if (driftSeverity_ == TimeSync::Warn || driftSeverity_ == TimeSync::Bad)
            utc.prepend(QStringLiteral("⚠ "));
        clockUtc_->setText(utc);
    }
}

void MainWindow::showClockMenu(const QPoint &global) {
    QMenu m;
    m.addAction(tr("Check clock drift now…"), this, [this]() {
        statusBus_->show(tr("Checking PC clock against network time…"), 0);
        timeSync_->checkDrift();
    });
    m.addAction(tr("Sync time (w32tm /resync)"), this, [this]() {
        statusBus_->show(tr("Requesting Windows time re-sync…"), 0);
        timeSync_->windowsResync();
    });
    m.exec(global);
}

void MainWindow::onStartStop() {
    auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
    if (!st) {
        return;
    }
    if (st->isRunning()) {
        st->close();
        return;
    }
    // Stopped → connect.  Prefer the remembered radio (probe-verified by
    // beginConnect, NOT opened blind); if none / not there, scan.
    QString ip;
    if (auto *disc = qobject_cast<lyra::ipc::HL2Discovery *>(discovery_))
        ip = disc->savedRadio().value(QStringLiteral("ip")).toString();
    beginConnect(ip);
}

void MainWindow::beginConnect(const QString &preferIp) {
    auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
    if (!st || st->isRunning()) return;
    // Leaving Disconnected — show the connect attempt in amber (not the
    // resting red); once the stream is up, updateConnState() flips it green.
    if (connStatus_)
        connStatus_->setStyleSheet(
            QStringLiteral("QLabel{color:#f0c040;font-weight:bold;}"));
    auto *disc = qobject_cast<lyra::ipc::HL2Discovery *>(discovery_);
    if (!disc) {
        // No discovery service — best effort: open the saved IP directly.
        if (!preferIp.isEmpty()) {
            if (connStatus_) connStatus_->setText(tr("Connecting to %1…").arg(preferIp));
            st->open(preferIp);
        } else if (connStatus_) {
            connStatus_->setText(tr("No saved radio"));
        }
        return;
    }
    if (preferIp.isEmpty()) { scanAndOpenFirst(); return; }
    // Probe the remembered IP first (fast unicast, ~1 s).  Open it only on
    // a reply; on no reply the radio isn't there (DHCP lease changed, moved
    // subnets, powered off) → scan and self-heal to its real address instead
    // of opening a dead IP and sitting frozen on "Connecting…".
    if (connStatus_) connStatus_->setText(tr("Locating %1…").arg(preferIp));
    QObject::disconnect(probeConn_);
    probeConn_ = connect(
        disc, &lyra::ipc::HL2Discovery::probeFinished, this,
        [this, st, preferIp](bool found, const QString &ip) {
            if (ip != preferIp) return;           // a different probe
            QObject::disconnect(probeConn_);
            if (st->isRunning()) return;          // raced with another open
            if (found) {
                if (connStatus_) connStatus_->setText(tr("Connecting to %1…").arg(preferIp));
                st->open(preferIp);
            } else {
                if (connStatus_)
                    connStatus_->setText(tr("Saved radio not at %1 — scanning…").arg(preferIp));
                scanAndOpenFirst();
            }
        });
    disc->probe(preferIp, 1.5);
}

void MainWindow::scanAndOpenFirst() {
    auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
    auto *disc = qobject_cast<lyra::ipc::HL2Discovery *>(discovery_);
    if (!st || !disc) return;
    if (connStatus_) connStatus_->setText(tr("Scanning…"));
    QObject::disconnect(scanConn_);
    QObject::disconnect(scanDoneConn_);
    // One-shot: open the first radio the sweep reports, then disarm both.
    scanConn_ = connect(
        disc, &lyra::ipc::HL2Discovery::radioFound, this,
        [this, st, disc](const QString &fip, const QString &mac,
                         const QString &board, int code, int beta,
                         bool busy, int numRxs, int proto) {
            // Auto-connect drives the P1 HL2Stream.  A P2 radio (Brick /
            // ANAN G2) is reported by discovery but can't be opened here
            // yet — the P2 receive engine is separate, deferred work.
            // Leave the one-shot armed so a subsequent P1 reply in the
            // same sweep still auto-connects.
            if (proto != 1) return;
            QObject::disconnect(scanConn_);
            QObject::disconnect(scanDoneConn_);
            disc->rememberRadio(fip, mac, board, code, beta, busy, numRxs, proto);
            if (connStatus_)
                connStatus_->setText(tr("Connecting to %1…").arg(fip));
            st->open(fip);
        });
    // No radio found → reset the connecting state so the UI doesn't sit on
    // "Scanning…" forever (it flips back to the resting red "No radio found").
    scanDoneConn_ = connect(
        disc, &lyra::ipc::HL2Discovery::scanFinished, this,
        [this, st](int count) {
            if (count > 0 || st->isRunning()) return;  // radioFound handled it
            QObject::disconnect(scanConn_);
            QObject::disconnect(scanDoneConn_);
            if (connStatus_) {
                connStatus_->setStyleSheet(
                    QStringLiteral("QLabel{color:#e05050;font-weight:bold;}"));
                connStatus_->setText(tr("No radio found"));
            }
        });
    disc->scan(1.5, 2);
}

void MainWindow::updateConnState() {
    auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
    const bool running = st && st->isRunning();
    if (startStopAction_) {
        startStopAction_->setText(running ? tr("■  Stop")
                                          : tr("▶  Start"));
    }
    if (startStopBtn_) {
        // Green when stopped (the button says "Start"); red when running
        // (it says "Stop") — the colour signals the ACTION the click does.
        startStopBtn_->setStyleSheet(
            running
                ? QStringLiteral("QToolButton{color:#e53935;font-weight:bold;}")
                : QStringLiteral("QToolButton{color:#4caf50;font-weight:bold;}"));
    }
    if (connStatus_) {
        connStatus_->setText(running && st
                                 ? tr("Connected to %1").arg(st->targetIp())
                                 : tr("Disconnected"));
        // Green = connected, red = disconnected.
        connStatus_->setStyleSheet(
            running
                ? QStringLiteral("QLabel{color:#4caf50;font-weight:bold;}")
                : QStringLiteral("QLabel{color:#e53935;font-weight:bold;}"));
    }
}

void MainWindow::updateTciStatus() {
    if (!tciStatus_) return;
    if (!tci_ || !tci_->running()) {
        tciStatus_->clear();
        tciStatus_->setVisible(false);
        return;
    }
    const int n = tci_->clientCount();
    tciStatus_->setVisible(true);
    if (n > 0) {
        tciStatus_->setText(tr("● TCI: %1").arg(n));
        tciStatus_->setStyleSheet(QStringLiteral("color:#4caf50;font-weight:bold;"));
        tciStatus_->setToolTip(tr("%n TCI client(s) connected", "", n));
    } else {
        tciStatus_->setText(tr("● TCI"));
        tciStatus_->setStyleSheet(QStringLiteral("color:#f0c040;"));
        tciStatus_->setToolTip(tr("TCI server listening — no clients connected"));
    }
}

bool MainWindow::event(QEvent *e) {
    // Block dock-separator resizing while panels are locked.  QMainWindow sets
    // a split cursor on the main window when the pointer is over a draggable
    // dock separator (its own hit-test); if a press lands there while locked,
    // swallow it so the separator-drag never starts.  This locks resizing
    // WITHOUT clamping dock sizes (which fought the layout on a locked
    // restart).  Narrow guard — only a locked left-press on a split cursor.
    if (panelsLocked_ && e->type() == QEvent::MouseButtonPress) {
        const Qt::CursorShape cs = cursor().shape();
        if (cs == Qt::SplitHCursor || cs == Qt::SplitVCursor) {
            auto *me = static_cast<QMouseEvent *>(e);
            if (me->button() == Qt::LeftButton) return true;   // eat it
        }
    }
    return QMainWindow::event(e);
}

void MainWindow::applyPanelLock(bool locked) {
    // Locked = panels can't be dragged / floated / closed (no title-bar
    // grip) AND can't be resized via the dock separators (see event()).
    // Unlocked = full feature set.  Mirrors the Python-Lyra "Lock panels".
    panelsLocked_ = locked;   // gates the separator-resize block in event()
    const QDockWidget::DockWidgetFeatures f =
        locked ? QDockWidget::NoDockWidgetFeatures : kUnlockedFeatures;
    for (auto *dock : std::as_const(docks_)) {
        // The chip-summoned tool windows (TX DSP rack: Speech / EQ /
        // Combinator / Plating, RX EQ, CW console, CW decoder) are floating
        // tool windows reached from the header chip-strip — they are NOT part
        // of the lockable main layout.  Locking them removed their features
        // (so the CW console + decoder became unreachable) AND froze their
        // size, so the chips could no longer open them.  Always keep them
        // fully featured + unfrozen, regardless of the lock, so the launchers
        // keep working.  Mirrors DockDragController's float-only predicate.
        const QString on = dock->objectName();
        if (isChipSummonedPanel(on)) {
            // A chip panel currently housed in a grouped rack re-tiles / tabs
            // INSIDE the rack (Movable + Closable) but never floats OUT of it —
            // the rack's own no-tear-out controller manages that, so leave its
            // features alone regardless of the main-window lock.
            if (qobject_cast<lyra::ui::PanelRack *>(dock->window())) {
                dock->setFeatures(QDockWidget::DockWidgetMovable
                                  | QDockWidget::DockWidgetClosable);
                continue;
            }
            dock->setFeatures(kUnlockedFeatures);
            if (QWidget *tb = dock->titleBarWidget()) {
                tb->setCursor(moveCursor());   // rack docks always draggable
                if (auto *fb = tb->findChild<QToolButton *>(QStringLiteral("dockFloat")))
                    fb->setVisible(true);
                if (auto *cb = tb->findChild<QToolButton *>(QStringLiteral("dockClose")))
                    cb->setVisible(true);
            }
            continue;
        }
        dock->setFeatures(f);
        // Our custom title bar's float/close buttons aren't governed by
        // the dock feature flags, so toggle them by hand to match the
        // locked state.  The "?" help badge stays available either way.
        if (QWidget *tb = dock->titleBarWidget()) {
            // Locked → plain arrow (not draggable); unlocked → move cursor.
            tb->setCursor(locked ? QCursor(Qt::ArrowCursor) : moveCursor());
            if (auto *fb = tb->findChild<QToolButton *>(
                    QStringLiteral("dockFloat")))
                fb->setVisible(!locked);
            if (auto *cb = tb->findChild<QToolButton *>(
                    QStringLiteral("dockClose")))
                cb->setVisible(!locked);
        }
        // Lock disables move / float / close (and the drag-to-dock gesture via
        // DockWidgetMovable) — it deliberately does NOT clamp dock sizes.  An
        // earlier setFixedSize()-on-lock froze each dock and, on a locked
        // restart, fought the maximize/layout — collapsing the window to title
        // strips, or stranding the panels at a sub-window size with a big void.
        // Resizing via the (now clearly visible) dock separators stays allowed
        // while locked; that's a fair trade for a layout that always restores
        // correctly.
    }
}

// Explicit operator-driven layout saves (View ▸ Save my layout / Save to slot N)
// are refused out loud during a LYRA_TEST_SIZE run rather than silently no-op'd —
// the title bar says LAYOUT SAVING DISABLED, and that has to be the whole truth.
void MainWindow::refuseLayoutWrite() {
    statusBar()->showMessage(
        tr("Layout saving is disabled for this session (safe boot, or a "
           "LYRA_TEST_SIZE diagnostic) so it can't overwrite your real "
           "layout."), 6000);
}

// Clamp the window into the screen it landed on.  A geometry saved on a screen
// that no longer exists (or is now smaller) otherwise restores a window that is
// partly or wholly off the desktop — unreachable title bar, panels laid out for
// pixels that aren't there.
void MainWindow::fitWindowToScreen() {
    // frameGeometry() only knows the window decoration once the window exists.
    // restoreLayout() runs from the constructor, so defer until it's on screen —
    // measuring before that gives an answer that is confidently wrong.
    if (!isVisible()) {
        QTimer::singleShot(0, this, &MainWindow::fitWindowToScreen);
        return;
    }
    const QScreen *sc = screen();
    if (!sc) sc = QGuiApplication::primaryScreen();
    if (!sc) return;
    const QRect avail = sc->availableGeometry();
    if (isMaximized() || isFullScreen()) return;   // the WM already fits those

    QRect g = frameGeometry();
    if (avail.contains(g)) return;                 // already fine — leave it alone

    g.setSize(g.size().boundedTo(avail.size()));
    if (g.right()  > avail.right())  g.moveRight(avail.right());
    if (g.bottom() > avail.bottom()) g.moveBottom(avail.bottom());
    if (g.left()   < avail.left())   g.moveLeft(avail.left());
    if (g.top()    < avail.top())    g.moveTop(avail.top());

    // frameGeometry includes the WM decoration; setGeometry works in client
    // coordinates, so carry the frame margins across.
    const QMargins deco(geometry().left()   - frameGeometry().left(),
                        geometry().top()    - frameGeometry().top(),
                        frameGeometry().right()  - geometry().right(),
                        frameGeometry().bottom() - geometry().bottom());
    setGeometry(g.marginsRemoved(deco));
    qInfo("[layout] restored window did not fit %dx%d — clamped to the screen",
          avail.width(), avail.height());
}

void MainWindow::saveLayout() {
    if (testWindowSize().isValid()) return;   // diagnostic run — see testWindowSize()
    if (safeBootLayout()) return;             // safe boot is READ-ONLY — see safeBootLayout()
    QSettings s;
    const QString p = layoutSlotPrefix();     // this display setup's own slot
    // Grouped-rack panels are children of their rack window, not the main
    // window, so main-window saveState() naturally omits them; each rack
    // persists its own geometry/state separately.
    s.setValue(p + QStringLiteral("geometry"), saveGeometry());
    s.setValue(p + QStringLiteral("windowState"), saveState());
    if (prefs_)
        s.setValue(p + QStringLiteral("panadapterSplit"), prefs_->panadapterSplit());
    saveLayoutUndoStack();
}

void MainWindow::restoreLayout() {
    QSettings s;
    const QString p = layoutSlotPrefix();

    // A LYRA_TEST_SIZE session shows what a FRESH machine of that size gets.  So
    // it builds the factory layout outright and never reads a saved slot — not
    // this setup's, not a migrated one, none.  Reading the store here made the
    // diagnostic depend on whatever happened to be in QSettings, which is the one
    // thing it must NOT depend on: the question is "what does a new operator
    // see?", and a new operator has nothing saved.
    if (testWindowSize().isValid()) {
        applyDefaultLayout();
        layoutSource_ = QStringLiteral("FACTORY (diagnostic run — no slot read)");
        qInfo("[layout] %s", qUtf8Printable(layoutSource_));
        applyPanelLock(s.value(QStringLiteral("ui/panelsLocked"), false).toBool());
        return;
    }

    // Safe boot (--safe / LYRA_SAFE): come up in the factory arrangement without
    // touching the saved slot, so the hatch is a guaranteed way in even when the
    // operator's remembered layout is what crashes the restore.  saveLayout()
    // bails under safe boot (see safeBootLayout()), so this is fully
    // non-destructive — their layout restores normally next normal launch.
    if (safeBootLayout()) {
        applyDefaultLayout();
        layoutSource_ =
            QStringLiteral("FACTORY (safe boot — saved layout left untouched)");
        qWarning("[layout] %s", qUtf8Printable(layoutSource_));
        applyPanelLock(s.value(QStringLiteral("ui/panelsLocked"), false).toBool());
        return;
    }

    // Repeated-startup-crash recovery (see the RHI safe-mode ladder in main()).
    // If a prior launch crashed during UI build even in the software renderer,
    // the fault is very likely a persisted window layout this build can't
    // restore (e.g. one saved by an older Lyra).  Purge the saved layout and
    // come up in the factory arrangement so a bad remembered layout can never
    // lock a tester out with no on-screen way back — every other setting is
    // kept.  One-shot: the flag is cleared here so a normal restore resumes
    // next launch.
    if (s.value(QStringLiteral("ui/uiSafeReset"), false).toBool()) {
        s.remove(QStringLiteral("ui/uiSafeReset"));
        s.sync();
        purgeSavedLayoutKeys();
        applyDefaultLayout();
        layoutSource_ =
            QStringLiteral("FACTORY (layout reset after repeated startup crash)");
        qWarning("[layout] %s", qUtf8Printable(layoutSource_));
        applyPanelLock(s.value(QStringLiteral("ui/panelsLocked"), false).toBool());
        return;
    }

    // One-time migration: an operator upgrading from a build with the single
    // global slot keeps the layout they have.  Their current setup adopts it;
    // the legacy keys are left in place (harmless, and a downgrade still works).
    if (!s.contains(p + QStringLiteral("windowState"))
        && s.contains(QStringLiteral("ui/windowState"))) {
        s.setValue(p + QStringLiteral("geometry"),
                   s.value(QStringLiteral("ui/geometry")));
        s.setValue(p + QStringLiteral("windowState"),
                   s.value(QStringLiteral("ui/windowState")));
        s.setValue(p + QStringLiteral("panadapterSplit"),
                   s.value(QStringLiteral("ui/panadapterSplit")));
        qInfo("[layout] adopted the pre-existing layout into slot '%s'",
              qUtf8Printable(layoutSlotKey()));
    }

    const QByteArray geo = s.value(p + QStringLiteral("geometry")).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);   // includes the maximized/normal flag
        fitWindowToScreen();
    } else {
        // This display setup is new to us: open maximized / full screen — the
        // way the operator runs it (and old Lyra defaulted).  Once they size or
        // maximize and close, saveGeometry() persists it for THIS setup.
        setWindowState(windowState() | Qt::WindowMaximized);
    }
    const QByteArray st = s.value(p + QStringLiteral("windowState")).toByteArray();
    if (!st.isEmpty()) {
        restoreState(st);
        layoutSource_ = QStringLiteral("restored slot '%1'").arg(layoutSlotKey());
    } else {
        // No session for this display setup: come up in the factory layout
        // rather than the raw dock-creation order.  Built, not restored from a
        // snapshot — see applyDefaultLayout().
        applyDefaultLayout();
        layoutSource_ = QStringLiteral("FACTORY (slot '%1' was empty)")
                            .arg(layoutSlotKey());
    }
    qInfo("[layout] %s", qUtf8Printable(layoutSource_));
    // The panadapter/waterfall divider lives inside the panadapter dock's QML,
    // so QMainWindow::saveState() knows nothing about it — carry it in the slot
    // alongside.  Absent (new setup) leaves whatever Prefs loaded.
    if (prefs_) {
        const QVariant sp = s.value(p + QStringLiteral("panadapterSplit"));
        if (sp.isValid()) prefs_->setPanadapterSplit(sp);
    }
    // Apply the persisted lock state (default unlocked).
    const bool locked =
        s.value(QStringLiteral("ui/panelsLocked"), false).toBool();
    if (lockAction_) {
        lockAction_->setChecked(locked);   // fires toggled -> applyPanelLock
    } else {
        applyPanelLock(locked);
    }
}

void MainWindow::saveUserLayout() {
    // Snapshot the CURRENT arrangement into dedicated "user default"
    // keys — separate from the ui/geometry+windowState session auto-save
    // so "Restore my saved layout" always returns to THIS deliberate
    // arrangement regardless of how the panels drift between sessions.
    if (testWindowSize().isValid() || safeBootLayout()) return refuseLayoutWrite();
    QSettings s;
    s.setValue(QStringLiteral("ui/userGeometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/userWindowState"), saveState());
    // The panadapter/waterfall divider isn't part of saveState() — it's
    // a QML SplitView inside the dock — so snapshot it alongside.
    if (prefs_) {
        s.setValue(QStringLiteral("ui/userPanadapterSplit"),
                   prefs_->panadapterSplit());
    }
    QMessageBox::information(
        this, tr("Layout saved"),
        tr("Current panel arrangement saved as your default.\n\n"
           "View → \"Restore my saved layout\" returns to this any time. "
           "\"Reset to default layout\" always goes to the built-in "
           "arrangement."));
}

void MainWindow::restoreUserLayout() {
    QSettings s;
    const QByteArray st =
        s.value(QStringLiteral("ui/userWindowState")).toByteArray();
    if (st.isEmpty()) {
        QMessageBox::information(
            this, tr("No saved layout"),
            tr("You haven't saved a layout yet.\n\nArrange the panels how "
               "you want them, then View → \"Save current layout as my "
               "default\"."));
        return;
    }
    const QByteArray geo =
        s.value(QStringLiteral("ui/userGeometry")).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
        fitWindowToScreen();   // saved on a bigger screen? don't go off-desktop
    }
    restoreState(st);
    // Trust restoreState() for each dock's saved visibility — do NOT force-show
    // (that re-opened panels the operator had closed when saving this layout;
    // see #189).
    // Re-apply the saved divider position (QML re-applies on the
    // panadapterSplit change).
    if (prefs_) {
        const QVariant sp =
            s.value(QStringLiteral("ui/userPanadapterSplit"));
        if (sp.isValid()) {
            prefs_->setPanadapterSplit(sp);
        }
    }
}

// The machine-specific-key filter + the export/restore engine now live in
// lyra::backup (src/backup.{h,cpp}); the interactive section-picker restore
// dialog is lyra::ui::restoreBackupInteractive (settingsdialog.cpp).  Both the
// File menu here and the Settings → Backup & Restore tab route through them.

void MainWindow::flushLayoutToSettings() {
    // Persist the live dock arrangement + window geometry so an export or
    // snapshot taken mid-session reflects the current layout (it's otherwise
    // only written on close).  Cheap; safe to call anytime.
    saveLayout();
}

void MainWindow::exportSettings() {
    flushLayoutToSettings();   // export reflects what's on screen now
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Lyra settings"),
        docs + QStringLiteral("/lyra-profile.lyra"),
        tr("Lyra profile (*.lyra)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".lyra"), Qt::CaseInsensitive))
        path += QStringLiteral(".lyra");
    if (!lyra::backup::exportToFile(path)) {
        QMessageBox::warning(this, tr("Export failed"),
            tr("Couldn't write the profile to:\n%1").arg(path));
        return;
    }
    QMessageBox::information(this, tr("Settings exported"),
        tr("Saved your settings (layout + preferences) to:\n%1\n\n"
           "Your radio address and the graphics backend are left out, so the "
           "file is safe to share or move to another PC.").arg(path));
}

void MainWindow::importSettings() {
    // File-menu Import → the same selective restore the Backup & Restore tab
    // uses (choose which sections to bring back), then an offered restart.
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Lyra settings"), docs,
        tr("Lyra profile (*.lyra);;All files (*)"));
    if (path.isEmpty()) return;
    lyra::ui::restoreBackupInteractive(this, path);
}

// ── Share a single layout as a small .lyralayout file ──────────────────────
// A "layout" here is just the panel arrangement (saveState()) + the
// panadapter/waterfall split — NOT window geometry, so it drops onto any
// monitor.  Export writes a tiny JSON; import applies it live.

void MainWindow::exportLayoutToFile() {
    flushLayoutToSettings();   // capture the arrangement on screen right now

    bool ok = false;
    QString name = QInputDialog::getText(
        this, tr("Export layout"),
        tr("Name this layout (shown to whoever imports it):"),
        QLineEdit::Normal, tr("My Lyra layout"), &ok).trimmed();
    if (!ok) return;
    if (name.isEmpty()) name = tr("Lyra layout");

    QString slug;
    for (const QChar c : name)
        slug += (c.isLetterOrNumber() || c == QLatin1Char(' ') ||
                 c == QLatin1Char('-') || c == QLatin1Char('_'))
                    ? c : QLatin1Char('_');
    if (slug.trimmed().isEmpty()) slug = QStringLiteral("lyra-layout");

    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Lyra layout"),
        docs + QStringLiteral("/") + slug + QStringLiteral(".lyralayout"),
        tr("Lyra layout (*.lyralayout)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".lyralayout"), Qt::CaseInsensitive))
        path += QStringLiteral(".lyralayout");

    QJsonObject o;
    o[QStringLiteral("lyraLayout")] = 1;                       // format version
    o[QStringLiteral("app")]        = QStringLiteral("lyra-cpp");
    o[QStringLiteral("name")]       = name;
    o[QStringLiteral("created")]    =
        QDateTime::currentDateTime().toString(Qt::ISODate);
    // Dock positions + open/closed + sizes, base64 so it rides in JSON.
    o[QStringLiteral("windowState")] =
        QString::fromLatin1(saveState().toBase64());
    if (prefs_)
        o[QStringLiteral("panadapterSplit")] =
            QJsonValue::fromVariant(prefs_->panadapterSplit());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export failed"),
            tr("Couldn't write the layout file:\n%1").arg(path));
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.close();
    QMessageBox::information(this, tr("Layout exported"),
        tr("Saved the layout \"%1\" to:\n%2\n\n"
           "Share this file (Discord, email, …) — whoever gets it opens "
           "Settings → Backup & Restore → \"Import layout\" to use it. It "
           "carries only the panel arrangement, so it drops onto any screen "
           "size.").arg(name, path));
}

void MainWindow::importLayoutFromFile() {
    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Lyra layout"), docs,
        tr("Lyra layout (*.lyralayout);;All files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import failed"),
            tr("Couldn't read the file:\n%1").arg(path));
        return;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, tr("Not a Lyra layout"),
            tr("That file isn't a valid Lyra layout file."));
        return;
    }
    const QJsonObject o = doc.object();
    const QByteArray st = QByteArray::fromBase64(
        o.value(QStringLiteral("windowState")).toString().toLatin1());
    if (!o.contains(QStringLiteral("lyraLayout")) || st.isEmpty()) {
        QMessageBox::warning(this, tr("Not a Lyra layout"),
            tr("That file doesn't contain a Lyra panel layout.\n\n"
               "(A full backup is a .lyra file — restore it under "
               "\"Restore\" instead.)"));
        return;
    }
    QString name = o.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) name = tr("(unnamed)");

    if (QMessageBox::question(this, tr("Apply this layout?"),
            tr("Apply the layout \"%1\"?\n\nYour panels will rearrange to "
               "match it. Your settings, frequency and audio are untouched.")
                .arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes)
        != QMessageBox::Yes)
        return;

    if (!restoreState(st)) {
        QMessageBox::warning(this, tr("Import failed"),
            tr("Lyra couldn't apply that layout (it may be from a much "
               "newer or older version)."));
        return;
    }
    if (prefs_) {
        const QJsonValue sp = o.value(QStringLiteral("panadapterSplit"));
        if (!sp.isUndefined() && !sp.isNull())
            prefs_->setPanadapterSplit(sp.toVariant());
    }
    // Re-apply the current lock state to the restored arrangement + persist,
    // exactly as recallNamedLayout() does.
    applyPanelLock(
        QSettings().value(QStringLiteral("ui/panelsLocked"), false).toBool());
    flushLayoutToSettings();
    QMessageBox::information(this, tr("Layout applied"),
        tr("Now using the layout \"%1\".\n\nTo keep it in a slot for quick "
           "recall later, use View → Layouts → \"Save current layout to\".")
            .arg(name));
}

void MainWindow::refreshLayoutMenus() {
    // Refresh each slot's menu label to its saved name (or "(empty)"), and
    // disable recall for an empty slot.  Called on Layouts-menu aboutToShow.
    QSettings s;
    for (int i = 0; i < 4; ++i) {
        const int slot = i + 1;
        const QString base = QStringLiteral("layouts/%1/").arg(slot);
        QString name = s.value(base + QStringLiteral("name")).toString();
        const bool empty =
            s.value(base + QStringLiteral("windowState")).toByteArray().isEmpty();
        if (name.isEmpty()) name = tr("Layout %1").arg(slot);
        if (layoutSaveActs_[i]) {
            layoutSaveActs_[i]->setText(
                empty ? tr("Slot %1  —  (empty)").arg(slot)
                      : tr("Slot %1  —  %2").arg(slot).arg(name));
        }
        if (layoutRecallActs_[i]) {
            layoutRecallActs_[i]->setText(
                empty ? tr("Slot %1  —  (empty)").arg(slot)
                      : tr("%1  (slot %2)").arg(name).arg(slot));
            layoutRecallActs_[i]->setEnabled(!empty);
        }
    }
    refreshLayoutUndoAction();
}

void MainWindow::saveNamedLayout(int slot) {
    if (testWindowSize().isValid() || safeBootLayout()) return refuseLayoutWrite();
    QSettings s;
    const QString base = QStringLiteral("layouts/%1/").arg(slot);
    QString cur = s.value(base + QStringLiteral("name")).toString();
    if (cur.isEmpty()) cur = tr("Layout %1").arg(slot);
    bool ok = false;
    QString name = QInputDialog::getText(
        this, tr("Save layout — slot %1").arg(slot),
        tr("Name this layout:"), QLineEdit::Normal, cur, &ok).trimmed();
    if (!ok) return;
    if (name.isEmpty()) name = tr("Layout %1").arg(slot);
    s.setValue(base + QStringLiteral("name"), name);
    s.setValue(base + QStringLiteral("geometry"), saveGeometry());
    s.setValue(base + QStringLiteral("windowState"), saveState());
    // The panadapter/waterfall divider is a QML SplitView inside the dock —
    // not part of saveState() — so snapshot it alongside.
    if (prefs_)
        s.setValue(base + QStringLiteral("panadapterSplit"),
                   prefs_->panadapterSplit());
    statusBar()->showMessage(
        tr("Saved layout \"%1\" to slot %2.").arg(name).arg(slot), 4000);
}

void MainWindow::recallNamedLayout(int slot) {
    QSettings s;
    const QString base = QStringLiteral("layouts/%1/").arg(slot);
    const QByteArray st =
        s.value(base + QStringLiteral("windowState")).toByteArray();
    if (st.isEmpty()) {
        QMessageBox::information(
            this, tr("Empty layout slot"),
            tr("Slot %1 is empty.\n\nArrange the panels how you like, then "
               "View → Layouts → \"Save current layout to\" → slot %1.")
                .arg(slot));
        return;
    }
    const QByteArray geo =
        s.value(base + QStringLiteral("geometry")).toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
        fitWindowToScreen();   // saved on a bigger screen? don't go off-desktop
    }
    restoreState(st);
    // Trust restoreState() for each dock's saved open/closed state.  An earlier
    // force-show of every non-chip panel here re-opened ANY panel the operator
    // had closed when saving this slot — the Solar panel was the visible victim
    // (#189), but it hit meter/audio/tx/band equally.  A faithful recall must
    // reproduce exactly what was saved, so no force-show.
    if (prefs_) {
        const QVariant sp = s.value(base + QStringLiteral("panadapterSplit"));
        if (sp.isValid()) prefs_->setPanadapterSplit(sp);
    }
    // Re-apply the current lock state to the freshly-restored arrangement
    // (restoreState resets dock features the same way restoreLayout handles).
    applyPanelLock(
        QSettings().value(QStringLiteral("ui/panelsLocked"), false).toBool());
    statusBar()->showMessage(tr("Recalled layout slot %1.").arg(slot), 4000);
}

void MainWindow::applyDefaultLayout() {
    // The factory arrangement — BUILT, not restored from a captured blob.
    //
    // It used to be a QMainWindow::saveState() snapshot embedded in the source.
    // A snapshot is a photograph of one machine's screen: that one was 2560px
    // wide, taken on the author's desktop, with the floating tool windows parked
    // at x = 2580..4518 — i.e. on his second monitor.  Every new operator
    // inherited it, and on a smaller screen Qt crushed the panels to make it fit.
    // Placing the docks in code instead makes the default correct at ANY window
    // size, and it stays correct when the panel set changes, with no capture-and-
    // re-embed ritual.
    //
    // The arrangement is the one that fits the published 1600x900 minimum:
    //
    //   +---------------------------------------------------------+
    //   |  PANADAPTER  +  waterfall                               |
    //   +--------------------+----------+-------------------------+
    //   |  TUNING            |  METER   |  FILTERS                |
    //   +--------------------+----------+-------------------------+
    //   |  AUDIO                                                  |
    //   +---------------------------------------------------------+
    //   |  BAND                                                   |
    //   +---------------------------------------------------------+
    //   |  TX                                                     |
    //   +---------------------------------------------------------+
    //
    // Audio (needs ~1258px) and TX (~1056px) are wide enough that nothing fits
    // beside them at 1600, so they each take a full row.  Display, Profiles and
    // Solar/Propagation are CLOSED: they don't fit alongside the rest at the
    // minimum, and the published spec says so plainly.  One click in the View
    // menu opens any of them on a bigger screen.
    auto dock = [this](const char *n) {
        return docks_.value(QLatin1String(n));
    };
    QDockWidget *pan = dock("panadapter");
    QDockWidget *tun = dock("tuning");
    QDockWidget *met = dock("meter");
    QDockWidget *flt = dock("modefilter");
    QDockWidget *aud = dock("audio");
    QDockWidget *bnd = dock("band");
    QDockWidget *tx  = dock("tx");

    // Clean slate — but only for the always-on panels.  Chip-summoned rack/CW
    // tool windows keep whatever state they're in, so a reset doesn't pop all
    // of them open in the operator's face.
    for (auto *d : std::as_const(docks_)) {
        if (isChipSummonedPanel(d->objectName())) continue;
        d->setFloating(false);
        removeDockWidget(d);   // also hides it; re-shown below if it's in the set
    }

    if (pan) { addDockWidget(Qt::TopDockWidgetArea, pan);    pan->show(); }
    if (tun) { addDockWidget(Qt::BottomDockWidgetArea, tun); tun->show(); }

    // Do the VERTICAL splits first, so each row spans the full width.  Splitting
    // row 1 horizontally first would nest the later rows INSIDE the Tuning
    // column instead of below it.
    if (tun && aud) { splitDockWidget(tun, aud, Qt::Vertical);   aud->show(); }
    if (aud && bnd) { splitDockWidget(aud, bnd, Qt::Vertical);   bnd->show(); }
    if (bnd && tx)  { splitDockWidget(bnd, tx,  Qt::Vertical);   tx->show();  }
    // Now subdivide row 1.
    if (tun && met) { splitDockWidget(tun, met, Qt::Horizontal); met->show(); }
    if (met && flt) { splitDockWidget(met, flt, Qt::Horizontal); flt->show(); }

    // Proportions.  resizeDocks normalises, so these read as ratios rather than
    // pixels — but they're the panels' honest widths and heights, so on a
    // 1600x900 screen they land at close to their natural size.  Row 1's Tuning
    // share is sized for a SPLIT VFO pair, not just simplex, so turning SPLIT on
    // doesn't shove the row out of shape.
    // Filters needs 539px for its TX-BW combo to be reachable, so give it a
    // little headroom rather than exactly its floor — the separators and frame
    // eat a few pixels, and a panel that is one combo short of usable is no
    // better than one that's half missing.  Tuning takes the slack: it has the
    // most give (its VFO-B slot only claims space when SPLIT is on).
    if (tun && met && flt)
        resizeDocks({tun, met, flt}, {800, 220, 580}, Qt::Horizontal);
    if (pan && tun && aud && bnd && tx)
        resizeDocks({pan, tun, aud, bnd, tx}, {300, 224, 122, 112, 64},
                    Qt::Vertical);

    // Factory split (invalid QVariant → the QML restores its 60/40 default,
    // proportional rather than the absolute pixel height a snapshot would bake
    // in).
    // Under safe boot the layout store is read-only — setPanadapterSplit(QVariant())
    // would remove the persisted ui/panadapterSplit key, so skip it (the operator's
    // real slot keeps its own split, restored on the next normal launch).
    if (prefs_ && !safeBootLayout()) prefs_->setPanadapterSplit(QVariant());
}

// ── Layout undo (randol request) ─────────────────────────────────────────
// Snapshot the whole dock arrangement (saveState) before each change so a
// mis-dropped panel can be walked back.  Only the dock ARRANGEMENT is
// captured — not the panadapter/waterfall QML divider (a separate
// prefs_->panadapterSplit, which restoreState doesn't disturb) — so an undo
// puts the panels back without touching the spectrum split.
//
// The stack is PERSISTED, per display setup, alongside that setup's layout.  An
// undo that evaporates on exit is no use to an operator who only notices the
// damage the next morning — which is exactly when they notice it.
static constexpr int kLayoutUndoMax = 5;   // a few steps of headroom

void MainWindow::loadLayoutUndoStack() {
    layoutUndoStack_.clear();
    const QVariantList v =
        QSettings().value(layoutSlotPrefix() + QStringLiteral("undo")).toList();
    for (const QVariant &e : v) {
        const QByteArray b = e.toByteArray();
        if (!b.isEmpty()) layoutUndoStack_.append(b);
    }
    while (layoutUndoStack_.size() > kLayoutUndoMax)
        layoutUndoStack_.removeFirst();
}

void MainWindow::saveLayoutUndoStack() {
    if (testWindowSize().isValid() || safeBootLayout()) return;   // read-only session — writes nothing
    QVariantList v;
    v.reserve(layoutUndoStack_.size());
    for (const QByteArray &b : std::as_const(layoutUndoStack_)) v.append(b);
    QSettings().setValue(layoutSlotPrefix() + QStringLiteral("undo"), v);
}

void MainWindow::initLayoutUndo() {
    layoutSnapTimer_ = new QTimer(this);
    layoutSnapTimer_->setSingleShot(true);
    layoutSnapTimer_->setInterval(400);   // coalesce one drag's several signals
    connect(layoutSnapTimer_, &QTimer::timeout,
            this, &MainWindow::commitLayoutSnapshot);
    // Hook the two signals a drag-drop actually fires: dockLocationChanged
    // (moved to a new area / tabified) and topLevelChanged (floated/re-docked).
    // NOT visibilityChanged — that also fires on tab-clicks and would pollute
    // the stack with noise that isn't an arrangement change.
    for (auto *d : std::as_const(docks_)) {
        connect(d, &QDockWidget::dockLocationChanged,
                this, &MainWindow::onLayoutMaybeChanged);
        connect(d, &QDockWidget::topLevelChanged,
                this, &MainWindow::onLayoutMaybeChanged);
    }
    layoutCurrent_ = saveState();   // baseline = the just-restored arrangement
    loadLayoutUndoStack();          // history from this setup's previous sessions
    refreshLayoutUndoAction();
}

void MainWindow::onLayoutMaybeChanged() {
    if (layoutRestoring_) return;   // ignore signals from our own restoreState
    layoutSnapTimer_->start();      // (re)arm the debounce
}

void MainWindow::commitLayoutSnapshot() {
    if (layoutRestoring_) return;
    const QByteArray now = saveState();
    if (now == layoutCurrent_) return;             // nothing actually moved
    layoutUndoStack_.append(layoutCurrent_);       // the PRE-change arrangement
    while (layoutUndoStack_.size() > kLayoutUndoMax)
        layoutUndoStack_.removeFirst();            // drop the oldest
    layoutCurrent_ = now;
    saveLayoutUndoStack();                         // survives a restart
    refreshLayoutUndoAction();
}

void MainWindow::undoLayoutChange() {
    if (layoutUndoStack_.isEmpty()) {
        statusBar()->showMessage(
            tr("Nothing to undo — the panel layout hasn't changed."), 3000);
        return;
    }
    layoutRestoring_ = true;
    layoutSnapTimer_->stop();                       // cancel any pending commit
    const QByteArray prev = layoutUndoStack_.takeLast();
    restoreState(prev);
    layoutCurrent_ = prev;
    // restoreState resets each dock's features — re-apply the lock state, as
    // recallNamedLayout does.
    applyPanelLock(
        QSettings().value(QStringLiteral("ui/panelsLocked"), false).toBool());
    saveLayoutUndoStack();
    refreshLayoutUndoAction();
    const int left = layoutUndoStack_.size();
    statusBar()->showMessage(
        left > 0
            ? tr("Layout undone — %1 more step%2 available.")
                  .arg(left).arg(left == 1 ? QString() : QStringLiteral("s"))
            : tr("Layout undone."), 3000);
    // Clear the guard only AFTER restoreState's settling signals have drained
    // (they'd otherwise re-arm the debounce and push a spurious snapshot).
    // 600 ms > the 400 ms debounce, so any deferred dock signals are ignored.
    QTimer::singleShot(600, this, [this]() { layoutRestoring_ = false; });
}

void MainWindow::refreshLayoutUndoAction() {
    if (!layoutUndoAct_) return;
    const int n = layoutUndoStack_.size();
    layoutUndoAct_->setEnabled(n > 0);
    layoutUndoAct_->setText(n > 0 ? tr("&Undo layout change  (%1)").arg(n)
                                  : tr("&Undo layout change"));
}

// TX-rip Phase 1 (Q2): setTciMicSource / setTxDspWorker method bodies
// removed; declarations dropped in mainwindow.h.  TX wiring rebuilt per
// docs/TX_ARCHITECTURAL_MAPPING.md §10.3.

void MainWindow::closeEvent(QCloseEvent *event) {
    saveLayout();
    // Persist each grouped rack's geometry + open state so it recalls next launch.
    if (dspRack_)     dspRack_->store();
    if (optionsRack_) optionsRack_->store();

    // Task #47 — QML null-safety on shutdown teardown.
    //
    // Every QML panel binds against the C++ service-object context
    // properties exposed by makeQuick() (Stream, Wdsp, WdspEngine,
    // Prefs, Bands, BandPlan, Status, BandMemory, Gen, Time, Memory,
    // Eibi, Spots, Meter, Help, Discovery).  Those services are
    // parented to QCoreApplication and are destroyed by Qt's child
    // cleanup AFTER aboutToQuit returns — i.e. AFTER this MainWindow
    // is gone but possibly BEFORE every QQuickWidget has finished
    // tearing its scene-graph down on a deferred event.  Any binding
    // that re-evaluates in that window (e.g. a deleteLater on a
    // ListView delegate touching Stream.rx1FreqHz) reads a dangling
    // pointer.  No crash — Qt's qmlEngine warns ("Cannot read
    // property ... of null") and the noise drowns out real warnings.
    //
    // Fix: drop every QQuickWidget's QML scene synchronously HERE,
    // while every service is still alive and reachable.  setSource()
    // with an empty URL unloads the root component, releases all
    // bindings, and disconnects the scene graph — by the time this
    // returns no QML expression can fire against any service object,
    // regardless of teardown order downstream.
    //
    // Cheap (a handful of widgets), idempotent (a re-shown window
    // would have to call setSource() again anyway), and contained to
    // one path — the eventual feature owner (Profile Manager v0.2.x
    // dialog, etc.) gets the same protection for free since every
    // QQuickWidget is reached via findChildren<>.
    const auto qws = findChildren<QQuickWidget *>();
    for (auto *qw : qws) {
        if (qw) qw->setSource(QUrl());
    }

    QMainWindow::closeEvent(event);
}

// ----------------------------------------------------------------
// TX-0c-pa-debug — HL2 telemetry strip refresh.
//
// Reads the TX-0a Q_PROPERTYs (hl2TempC / hl2SupplyV / paCurrentA),
// formats "HL2: T 24.7°C  V 12.3 V  PA 0.00 A".  NaN getters (no
// telemetry frames yet, e.g. pre-Start) render as "—" so the strip
// is always coherent.  PA current goes BOLD RED at ≥50 mA — the
// operator's at-a-glance "RF on the air" indicator.  Threshold is
// safely above the worst-case noise on a NaN/zero idle reading and
// well below any keyed-bias level (HL2+ idle PA bias ≈ 200 mA on
// the operator's unit; full tune draws ~1.8 A).
//
// Updates fire from HL2Stream::statsChanged (~5 Hz via statsTimer_),
// the same signal that already drives the existing stats UI — no
// new timer, no new wake-ups, no impact on the EP2/wire cadence.
void MainWindow::refreshHl2TelemetryStrip() {
    if (!hl2TelemLabel_) return;
    auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_);
    if (!st) return;

    auto fmt = [](double v, int prec, const QString &unit) -> QString {
        if (std::isnan(v)) return QStringLiteral("—");
        return QStringLiteral("%1 %2").arg(v, 0, 'f', prec).arg(unit);
    };

    const double t   = st->hl2TempC();
    const double v   = st->hl2SupplyV();
    const double pa  = st->paCurrentA();

    hl2TelemLabel_->setText(QStringLiteral("HL2: T %1  V %2  PA %3")
        .arg(fmt(t,  1, QStringLiteral("°C")))
        .arg(fmt(v,  2, QStringLiteral("V")))
        .arg(fmt(pa, 2, QStringLiteral("A"))));

    // RF-on indicator: PA current red+bold ≥50 mA.  Stays muted blue
    // when PA is idle (gateware off, MOX off, or PA-enable unchecked)
    // so the operator sees a clean transition on each keydown/keyup.
    const bool paLive = !std::isnan(pa) && pa >= 0.05;
    hl2TelemLabel_->setStyleSheet(paLive
        ? QStringLiteral("QLabel{color:#ff4040;font-family:Consolas,"
                         "Menlo,monospace;padding:0 8px;font-weight:bold;}")
        : QStringLiteral("QLabel{color:#8fa6ba;font-family:Consolas,"
                         "Menlo,monospace;padding:0 8px;}"));
}

// ----------------------------------------------------------------
// TX-0c-fsm — space-bar TOGGLE PTT (Thetis / conventional-rig behaviour).
//
// Press space once → TX on; press again → TX off.  This mirrors Thetis
// (space flips chkMOX.Checked on KeyDown, no KeyUp handling) and every
// rig with a PTT-lock — so operators need no retraining.  It flips the
// SAME shared MOX truth the TxPanel MOX button toggles
// (`Stream.requestMox(!Stream.moxActive)`), so the two are identical:
// space can un-key a MOX-button transmission and vice-versa.
//
// Why toggle over the old momentary press/hold: momentary depended on
// reliably receiving the KeyRelease — a lost release (window loses
// focus mid-transmit, a popup opens, focus moves to a field) stranded
// the radio transmitting.  A toggle has no release dependency, so that
// whole stuck-carrier class is gone.
//
// Auto-repeat presses (held key) are ignored so a held space doesn't
// cycle TX/RX.  Turning ON honours the typing/dialog/popup guards so a
// space still types into a freq overlay / QLineEdit / Settings; turning
// OFF is UNCONDITIONAL (never strand TX on).  We consume the whole space
// press/release burst we act on so a focused combo/button never sees it.
//
// Limitation (unchanged): QML TextField focus inside the embedded
// QQuickWidgets is NOT detected (Qt reports the QQuickWidget as the
// focus widget).  The only such surface is TuningPanel's freq-entry
// overlay, hidden until an explicit click — operator-controlled.

static bool isEditableFocus(QWidget *fw) {
    if (!fw) return false;
    if (qobject_cast<QLineEdit *>(fw))       return true;
    if (qobject_cast<QSpinBox *>(fw))        return true;
    if (qobject_cast<QPlainTextEdit *>(fw))  return true;
    if (qobject_cast<QTextEdit *>(fw))       return true;
    // QQuickWidget hosts QML; for TX-0c-fsm we route space through.
    // The only QML focused text-entry today is the freq overlay (see
    // commentary above); refine later if a tester reports keying MOX
    // mid-typing.
    return false;
}

// App-wide key preview (installed on qApp).  The single home for space-bar
// PTT: it runs BEFORE the focused widget's own key handling, so a focused
// QComboBox (Mode/Step) or QPushButton (band button) can't consume Space to
// open its popup / click itself the way keyPressEvent-after-focus allowed
// (Pierre HS0ZRT report).  This is the Qt equivalent of Thetis's KeyPreview.
bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    const QEvent::Type t = event->type();
    if ((t == QEvent::KeyPress || t == QEvent::KeyRelease)
        && prefs_ && prefs_->spaceBarPttEnabled()) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Space) {
            // Swallow the paired release + any auto-repeat presses for a space
            // press we acted on, so a focused button/combo never reacts to the
            // space at all.  If we passed the initiating press through (typing
            // into a field), these flow through the same way.
            if (t == QEvent::KeyRelease) {
                const bool consume = spaceConsumedPress_;
                spaceConsumedPress_ = false;
                if (consume) return true;
                return QMainWindow::eventFilter(watched, event);
            }
            if (ke->isAutoRepeat()) {
                if (spaceConsumedPress_) return true;  // swallow held-key repeats
                return QMainWindow::eventFilter(watched, event);
            }
            // First (non-repeat) press: toggle MOX exactly as the MOX button
            // does — flip the one shared wire-MOX truth.
            if (auto *st = qobject_cast<lyra::ipc::HL2Stream *>(stream_)) {
                if (st->moxActive()) {
                    st->requestMox(false);       // un-key: ALWAYS allowed
                    spaceConsumedPress_ = true;
                    return true;
                }
                // Keying ON honours the guards so a space still types into a
                // field / behaves in a dialog / while a popup is open.  A
                // floating dock's window is a QDockWidget (not QDialog), so
                // PTT still keys from a torn-off panel.
                QWidget *fw = QApplication::focusWidget();
                const bool inDialog = fw && qobject_cast<QDialog *>(fw->window());
                if (!isEditableFocus(fw) && !inDialog
                    && !QApplication::activePopupWidget()) {
                    st->requestMox(true);        // key ON
                    spaceConsumedPress_ = true;
                    return true;
                }
                // MOX off + typing context → let the space type a character.
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // #176 — F1..F12 fire the assigned CW macro (global accelerator, like a
    // real contest keyer: works regardless of which dock has focus).  Gated to
    // CW modes (the keyer no-ops elsewhere) + not while typing in a field (so
    // editing a macro's text doesn't trip a send).  The model expands tokens
    // + drives Stream.sendCw + the live "sending" highlight.
    if (cwMacros_ && prefs_ && !event->isAutoRepeat()
        && event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F12) {
        const QString m = prefs_->mode().toUpper();
        const int fn = event->key() - Qt::Key_F1 + 1;
        if ((m == QLatin1String("CWU") || m == QLatin1String("CWL"))
            && !isEditableFocus(QApplication::focusWidget())) {
            cwMacros_->sendByFkey(fn);
            event->accept();
            return;
        }
        // #89 — in a VOICE mode, F1..F12 fire the assigned voice-keyer clip
        // (same global-accelerator idiom as the CW macros; no collision since
        // it's mode-gated).  Only when the keyer is live (B1 wired the injector)
        // and not while typing in a field.
        const bool voiceMode = (m == QLatin1String("USB") || m == QLatin1String("LSB")
            || m == QLatin1String("AM") || m == QLatin1String("SAM")
            || m == QLatin1String("DSB") || m == QLatin1String("FM"));
        if (voiceKeyer_ && voiceMode && voiceKeyer_->live()
            && !isEditableFocus(QApplication::focusWidget())) {
            voiceKeyer_->playByFkey(fn);
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

} // namespace lyra::ui

// Lyra — Settings dialog.  See settingsdialog.h.

#include "settingsdialog.h"

#include "bands.h"
#include "hl2_discovery.h"
#include "mainwindow.h"   // Share-a-layout buttons call MainWindow export/import
#include "hl2_stream.h"
#include "palettes.h"
#include "prefs.h"
#include "time_stations.h"
#include "logdialog.h"

#include <functional>
#include "usb_bcd.h"
#include "wdsp_engine.h"
#include "wxservice.h"

#include "backup.h"
#include "wdsp_native.h"

#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QProgressBar>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QScreen>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QFontMetrics>
#include <QInputDialog>
#include <QUrl>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPainter>      // #91 VOX — colored mic-level meter bar
#include <QPaintEvent>
#include <QGridLayout>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSignalBlocker>
#include <QVector>
#include <QFrame>
#include "oc/OcControl.h"   // #199 — editable OC table (Filters / BCD tab)
#include <QHash>
#include <QHBoxLayout>
#include <QSettings>
#include <QStandardItemModel>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QHostAddress>
#include <QColor>
#include <QFont>
#include <QBrush>
#include <QAbstractSocket>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVector>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QDir>
#include <QSignalBlocker>
#include "memorystore.h"
#include "eibistore.h"
#include "tci_server.h"
#include "cat/SerialPtt.h"
#include "cat/SerialCwKey.h"   // #171
#include "cat/CatServer.h"
#include <QSerialPortInfo>
#include "spotstore.h"
#include "spothole_feeder.h"
#include "dxcluster_feeder.h"
#include "metermodel.h"
#include "tunermemory.h"
#include "tx/VoiceKeyer.h"   // #89 recording options
#include "recorder/RecorderEngine.h"   // #201 session recorder
#include "recorder/SessionConverter.h" // #201 offline MP4 converter
#include "profile/ProfileManager.h"
#include "profile/CompanionLauncher.h"
#include <QDesktopServices>
#include <memory>
#include <QVBoxLayout>
#include <QWidget>
#include <QSettings>
#include <QMap>

#include <algorithm>
#include <utility>
#include <cmath>
#include <iterator>

namespace lyra::ui {

namespace {
// "Pick one from a list" delegate — draws a radio-button indicator (○ empty /
// ● filled) in a left gutter plus a strong row highlight, so the selected item
// is unmistakable on the dark theme (the default selection was a faint blue
// line).  Honours the item's own ForegroundRole colour (e.g. the Hardware
// tab's green connected / grey disconnected radios) and its enabled state.
class PickDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override {
        const bool sel     = opt.state & QStyle::State_Selected;
        const bool hover   = opt.state & QStyle::State_MouseOver;
        const bool enabled = opt.state & QStyle::State_Enabled;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (sel)        p->fillRect(opt.rect, QColor(0x18, 0x3a, 0x49));
        else if (hover) p->fillRect(opt.rect, QColor(0x20, 0x2a, 0x31));
        // Radio indicator.
        const int d  = 13;
        const int cx = opt.rect.left() + 8 + d / 2;
        const int cy = opt.rect.center().y();
        const QColor ring = sel ? QColor(0x4f, 0xd0, 0xff)
                                : QColor(0x62, 0x72, 0x7e);
        p->setPen(QPen(ring, 1.6));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(QPointF(cx, cy), d / 2.0, d / 2.0);
        if (sel) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0x4f, 0xd0, 0xff));
            p->drawEllipse(QPointF(cx, cy), d / 4.0, d / 4.0);
        }
        // Text — keep the item's own colour if it set one.
        QColor fg = !enabled ? QColor(0x6b, 0x76, 0x7e)
                             : (sel ? QColor(0xff, 0xff, 0xff)
                                    : QColor(0xcf, 0xd8, 0xdc));
        const QVariant fgv = idx.data(Qt::ForegroundRole);
        if (fgv.canConvert<QBrush>()) fg = fgv.value<QBrush>().color();
        p->setPen(fg);
        p->setFont(opt.font);
        const QRect tr = opt.rect.adjusted(8 + d + 8, 0, -6, 0);
        const QFontMetrics fm(opt.font);
        p->drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                    fm.elidedText(idx.data(Qt::DisplayRole).toString(),
                                  Qt::ElideRight, tr.width()));
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &opt,
                   const QModelIndex &idx) const override {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        s.setHeight(qMax(s.height(), 24));
        return s;
    }
};

// Profile-list variant: same cyan radio-mark + text as PickDelegate, plus a
// filled bright-green "ACTIVE" pill at the right edge for the on-air profile
// (flagged via Qt::UserRole + 1 == true).  A solid badge reads far more
// strongly than coloured text on the selected row.
class ProfilePickDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override {
        const bool sel     = opt.state & QStyle::State_Selected;
        const bool hover   = opt.state & QStyle::State_MouseOver;
        const bool enabled = opt.state & QStyle::State_Enabled;
        const bool active  = idx.data(Qt::UserRole + 1).toBool();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (sel)        p->fillRect(opt.rect, QColor(0x18, 0x3a, 0x49));
        else if (hover) p->fillRect(opt.rect, QColor(0x20, 0x2a, 0x31));
        // Radio indicator.
        const int d  = 13;
        const int cx = opt.rect.left() + 8 + d / 2;
        const int cy = opt.rect.center().y();
        const QColor ring = sel ? QColor(0x4f, 0xd0, 0xff)
                                : QColor(0x62, 0x72, 0x7e);
        p->setPen(QPen(ring, 1.6));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(QPointF(cx, cy), d / 2.0, d / 2.0);
        if (sel) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0x4f, 0xd0, 0xff));
            p->drawEllipse(QPointF(cx, cy), d / 4.0, d / 4.0);
        }
        // Pill metrics (drawn right AFTER the profile name, not at the far
        // right edge, so it sits close to the name).
        QFont bf = opt.font;
        bf.setBold(true);
        bf.setPointSizeF(bf.pointSizeF() * 0.85);
        const QFontMetrics pm(bf);
        const QString badge = tr("ACTIVE");
        const int pillW = pm.horizontalAdvance(badge) + 26;  // roomier
        const int pillH = pm.height() + 4;
        const int pillGap = 10;

        // Text.
        QColor fg = !enabled ? QColor(0x6b, 0x76, 0x7e)
                             : (sel ? QColor(0xff, 0xff, 0xff)
                                    : QColor(0xcf, 0xd8, 0xdc));
        const QVariant fgv = idx.data(Qt::ForegroundRole);
        if (fgv.canConvert<QBrush>()) fg = fgv.value<QBrush>().color();
        p->setFont(opt.font);
        const QFontMetrics fm(opt.font);
        const int textLeft = opt.rect.left() + 8 + d + 8;
        // Leave room for the pill (+ gaps) after the name when active.
        const int textRight = active ? (opt.rect.right() - 6 - pillW - pillGap)
                                     : (opt.rect.right() - 6);
        const int availW = qMax(0, textRight - textLeft);
        const QString text = idx.data(Qt::DisplayRole).toString();
        const QString elided = fm.elidedText(text, Qt::ElideRight, availW);
        p->setPen(fg);
        p->drawText(QRect(textLeft, opt.rect.top(), availW, opt.rect.height()),
                    Qt::AlignVCenter | Qt::AlignLeft, elided);

        // Pill immediately after the name.
        if (active) {
            const int px = textLeft + fm.horizontalAdvance(elided) + pillGap;
            const QRect pill(px, opt.rect.center().y() - pillH / 2,
                             pillW, pillH);
            p->setFont(bf);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0x1e, 0xd7, 0x60));   // bright green
            p->drawRoundedRect(pill, pillH / 2.0, pillH / 2.0);
            p->setPen(QColor(0x06, 0x2a, 0x12));      // dark text on the chip
            p->drawText(pill, Qt::AlignCenter, badge);
        }
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem &opt,
                   const QModelIndex &idx) const override {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        s.setHeight(qMax(s.height(), 24));
        return s;
    }
};
}  // namespace

SettingsDialog::SettingsDialog(Prefs *prefs, lyra::ipc::HL2Stream *stream,
                               lyra::ipc::HL2Discovery *discovery,
                               UsbBcd *bcd, lyra::dsp::WdspEngine *engine,
                               lyra::wx::WxService *wx, MemoryStore *memory,
                               EibiStore *eibi, TciServer *tci,
                               SpotStore *spots, SpotHoleFeeder *spotHole,
                               DxClusterFeeder *dxCluster, MeterModel *meter,
                               TunerMemory *tuner,
                               lyra::tx::VoiceKeyer *voiceKeyer,
                               lyra::recorder::RecorderEngine *recorder,
                               lyra::recorder::SessionConverter *converter,
                               lyra::profile::ProfileManager *profiles,
                               lyra::profile::CompanionLauncher *companion,
                               lyra::cat::SerialPtt *serialPtt,
                               lyra::cat::SerialCwKey *serialCwKey,
                               const QList<lyra::cat::CatServer *> &catServers,
                               QWidget *parent)
    : QDialog(parent), prefs_(prefs), stream_(stream),
      discovery_(discovery), bcd_(bcd), engine_(engine), wx_(wx),
      memory_(memory), eibi_(eibi), tci_(tci), spots_(spots),
      spotHole_(spotHole), dxCluster_(dxCluster),
      meter_(meter), tuner_(tuner), voiceKeyer_(voiceKeyer),
      recorder_(recorder), converter_(converter),
      profiles_(profiles), companion_(companion),
      serialCwKey_(serialCwKey),
      serialPtt_(serialPtt), catServers_(catServers) {
    setWindowTitle(tr("Lyra — Settings"));
    // Open big enough to show a full tab — the widest tabs are 2-column
    // (Visuals / TX) and were forcing both scrollbars at the old 700×640.
    // Target a roomy size, capped to ~92% of the screen so it never opens
    // larger than the display on a small laptop (the scroll areas still cover
    // any overflow if a tab is taller than even this).
    {
        QSize want(1240, 900);
        if (QScreen *sc = screen()) {
            const QSize avail = sc->availableGeometry().size() * 0.95;
            want = QSize(qMin(want.width(),  avail.width()),
                         qMin(want.height(), avail.height()));
        }
        resize(want);
    }

    tabs_ = new QTabWidget(this);

    // Wrap every tab page in a scroll area so a tab whose content is taller
    // than the dialog is fully reachable instead of being clipped at the
    // bottom (operator-reported: long tabs — TX / Network / Weather / etc. —
    // were cutting off controls for some window sizes).  widgetResizable keeps
    // the page filling the viewport width (forms/2-col layouts stay full-
    // width); a vertical scrollbar only appears when the content overflows.
    auto wrapScroll = [this](QWidget *page) -> QWidget * {
        auto *sa = new QScrollArea(this);
        sa->setWidget(page);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // The theme leaves scrollbars unstyled (near-invisible), so a tab with
        // content below the fold gave no clue there was more.  Give the tab
        // scroll area a clearly-visible Lyra-cyan scrollbar — when it appears
        // it obviously signals "scroll down for more" (operator ask 2026-07-03).
        sa->setStyleSheet(QStringLiteral(
            "QScrollBar:vertical{background:rgb(17,22,32);width:13px;margin:0;}"
            "QScrollBar::handle:vertical{background:rgba(0,229,255,150);"
            "min-height:36px;border-radius:5px;margin:2px;}"
            "QScrollBar::handle:vertical:hover{background:rgb(0,229,255);}"
            "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
            "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{"
            "background:transparent;}"));
        return sa;
    };

    // Tabs are ordered by operating flow, not by the order features were
    // built: station setup -> receive -> Filters/BCD (get the band-correct
    // filters set BEFORE you key) -> transmit + accessories -> integrations
    // -> set-once utility/cosmetic.  selectTopic() finds tabs by TITLE, so
    // this order is free to change without touching anything else.

    // --- Station setup (do these first) ---
    if (stream_ || discovery_ || bcd_) {
        tabs_->addTab(wrapScroll(buildHardwareTab()), tr("Hardware"));
    }
    if (memory_ || eibi_) {
        tabs_->addTab(wrapScroll(buildBandsTab()), tr("Bands"));
    }

    // --- Receive ---
    if (engine_) {
        tabs_->addTab(wrapScroll(buildAudioTab()), tr("Audio"));
        tabs_->addTab(wrapScroll(buildDspTab()), tr("DSP"));  // #159 filter type
        tabs_->addTab(wrapScroll(buildNoiseTab()), tr("Noise"));
    }
    if (recorder_) {  // #201 session recorder — config + Sessions list
        tabs_->addTab(wrapScroll(buildRecordingTab()), tr("Recording"));
    }
    // Visuals (panadapter / display appearance) sits with the RX-display group.
    tabs_->addTab(wrapScroll(buildVisualsTab()), tr("Visuals"));
    if (meter_) {
        tabs_->addTab(wrapScroll(buildMeterTab()), tr("Meter"));
    }

    // --- Filters BEFORE transmit (#199): band-correct filters must be right
    // before you key — wrong filter = out-of-band emissions / blown LPF relay.
    if (stream_ || bcd_) {
        tabs_->addTab(wrapScroll(buildFiltersBcdTab()), tr("Filters / BCD"));
    }

    // --- Transmit + accessories ---
    if (stream_) {
        tabs_->addTab(wrapScroll(buildTxTab()), tr("TX"));
        // TX power model Stage 3 — per-band PA Gain table.
        tabs_->addTab(wrapScroll(buildPaGainTab()), tr("PA Gain"));
    }
    if (tuner_) {
        tabs_->addTab(wrapScroll(buildTunerTab()), tr("Tuner"));
    }
    if (stream_) {
        // #91 — VOX its own tab (the TX tab was getting cluttered).
        tabs_->addTab(wrapScroll(buildVoxTab()), tr("VOX"));
        // #105 — CW is its own operating mode (RX pitch + TX keyer).
        tabs_->addTab(wrapScroll(buildCwTab()), tr("CW"));
        // Frequency calibration (WWV/time-station ppm trim).
        tabs_->addTab(wrapScroll(buildCalibrationTab()), tr("Calibration"));
    }

    // --- Integrations ---
    if (tci_) {
        tabs_->addTab(wrapScroll(buildNetworkTab()), tr("Network"));
    }
    if (serialPtt_) {
        tabs_->addTab(wrapScroll(buildCatSerialTab()), tr("CAT / Serial"));
    }

    // --- Set-once utility / cosmetic (rarely revisited) ---
    if (profiles_) {
        tabs_->addTab(wrapScroll(buildProfilesTab()), tr("Profiles"));
    }
    tabs_->addTab(wrapScroll(buildBackupRestoreTab()), tr("Backup & Restore"));
    if (wx_) {
        tabs_->addTab(wrapScroll(buildWeatherTab()), tr("Weather"));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *root = new QVBoxLayout(this);
    root->addWidget(tabs_);
    root->addWidget(buttons);
}

QWidget *SettingsDialog::buildAudioTab() {
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    // RX audio output device chooser — moved here from the Audio panel
    // (old-Lyra layout: the device picker lives in Settings; the panel
    // keeps mute + volume).  Index 0 = HL2 onboard codec (AK4951);
    // 1..N = the operator's PC sound devices.
    auto *outCombo = new QComboBox(page);
    outCombo->addItems(engine_->audioOutputDevices());
    outCombo->setCurrentIndex(engine_->audioDeviceIndex());
    connect(outCombo, &QComboBox::activated, engine_,
            [this](int idx) { engine_->setAudioOutputDevice(idx); });
    // Reflect an external change back into the combo.
    connect(engine_, &lyra::dsp::WdspEngine::audioDeviceChanged, outCombo,
            [this, outCombo]() {
                const int i = engine_->audioDeviceIndex();
                if (outCombo->currentIndex() != i) {
                    outCombo->blockSignals(true);
                    outCombo->setCurrentIndex(i);
                    outCombo->blockSignals(false);
                }
            });
    form->addRow(tr("Output"), outCombo);

    auto *note = new QLabel(
        tr("“HL2 audio jack (AK4951)” plays out the radio's "
           "headphone jack (single-crystal, lowest latency).  Choose a PC "
           "device to use the computer's sound card instead.  Volume and "
           "mute stay on the Audio panel."), page);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
    form->addRow(note);

    // ── Task #53 — Shared RX+TX Filter Low edge ─────────────────
    // Single global filter-low setting applied to BOTH the WDSP RX
    // bandpass (replaces the previously-hardcoded 0 Hz low cut for
    // SSB/DIG modes) AND the HL2Stream TX bandpass (replaces the
    // previously-hardcoded 200 Hz low cut).  Interim until the TX
    // Profile Manager (Task #49) ships per-profile (lo, hi) pairs
    // that override this default on profile load.  Tooltip carries
    // the 50/60 Hz mains-coupling warning.
    {
        auto *flSpin = new QSpinBox(page);
        flSpin->setRange(0, 500);
        flSpin->setSingleStep(10);
        flSpin->setSuffix(tr(" Hz"));
        flSpin->setValue(prefs_->filterLow());
        flSpin->setToolTip(
            tr("Low cutoff frequency for the RX and TX audio bandpass "
               "(shared).  Higher values (100-200 Hz) suppress 50 / 60 Hz "
               "mains coupling on the microphone path and reduce low-end "
               "rumble on receive.  Lower values (50-70 Hz) preserve "
               "chest-resonance / low-end body for ESSB-style wide "
               "audio — verify your station isn't picking up mains hum "
               "at very low cuts.\n\n"
               "Applies to SSB / DIG modes only (asymmetric passband, "
               "low edge offset from carrier).  CW is pitch-centred and "
               "AM / DSB / FM are symmetric around DC — those modes "
               "ignore this setting.\n\n"
               "Interim setting: when the TX Profile Manager ships, "
               "each named profile will carry its own RX + TX (low, "
               "high) pair that overrides this default on load."));
        connect(flSpin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int v) { prefs_->setFilterLow(v); });
        connect(prefs_, &Prefs::filterLowChanged, flSpin, [this, flSpin]() {
            if (flSpin->value() != prefs_->filterLow()) {
                flSpin->setValue(prefs_->filterLow());
            }
        });
        form->addRow(tr("Filter Low edge (RX + TX):"), flSpin);
    }

    // ── #158 — Virtual Audio Cable (VAC1) ───────────────────────────
    // Feed RX audio to a PC output device (a virtual cable) so a digital-
    // mode app (WSJT-X / MSHV / etc.) can decode it WHILE the normal
    // monitor keeps playing — an independent path, not the main output
    // re-routed.  Enable + output device + RX gain (dB), mirroring the
    // Thetis VAC dialog.  All applied live + persisted by the engine.
    {
        auto *grp = new QGroupBox(tr("Virtual Audio Cable (VAC1)"), page);
        auto *vf  = new QFormLayout(grp);

        auto *vacEnable = new QCheckBox(tr("Enable VAC1 (RX→PC and PC→TX)"), grp);
        vacEnable->setChecked(engine_->vac1Enabled());
        vacEnable->setToolTip(tr(
            "Master switch for VAC1 — powers BOTH directions:\n"
            "  • RX→PC: receiver audio to the Output device (for a "
            "digital-mode app to decode), and\n"
            "  • PC→TX: capture from the Input device (to transmit from a "
            "soundcard / virtual cable / mic).\n\n"
            "Required to transmit through VAC — tick this even if you only "
            "use the TX (mic-in) direction.  Without it the VAC engine never "
            "starts and there is no TX audio."));
        vf->addRow(vacEnable);

        auto *vacAuto = new QCheckBox(
            tr("Auto-enable for digital modes (disable for others)"), grp);
        vacAuto->setChecked(engine_->vac1AutoDigital());
        vacAuto->setToolTip(tr(
            "When ON, VAC1 turns on automatically whenever you switch to a "
            "digital mode (DIGU / DIGL) and off for all other modes — the "
            "Enable checkbox above is then the moot baseline.  Leave OFF if "
            "you drive digital modes a different way (e.g. TCI)."));
        vf->addRow(vacAuto);

        // #158 DL-3 — "Driver" (PortAudio host API) picker, Thetis-faithful.
        // Selecting a driver repopulates the Output/Input device combos with
        // that host API's devices (PA index stored as item userData).
        auto *vacDriver = new QComboBox(grp);
        {
            const QStringList apis = engine_->vac1HostApiNames();
            const QList<int>  idxs = engine_->vac1HostApiPaIndices();
            for (int i = 0; i < apis.size(); ++i)
                vacDriver->addItem(apis[i], i < idxs.size() ? idxs[i] : -1);
            const int sel = vacDriver->findText(engine_->vac1HostApiName());
            vacDriver->setCurrentIndex(sel >= 0 ? sel : 0);
        }
        vacDriver->setToolTip(tr(
            "Audio backend (PortAudio host API) the VAC devices live under — "
            "WASAPI for virtual cables (VB-Audio / VAC).  Changing it "
            "repopulates the Output and Input device lists below."));
        vf->addRow(tr("Driver"), vacDriver);

        const int curApi0 = vacDriver->currentData().toInt();

        auto *vacDev = new QComboBox(grp);
        vacDev->addItem(tr("(none)"));
        vacDev->addItems(engine_->vac1OutputDevicesFor(curApi0));
        {
            const int i = vacDev->findText(engine_->vac1OutputDeviceName());
            vacDev->setCurrentIndex(i >= 0 ? i : 0);
        }
        vacDev->setToolTip(tr(
            "PC output device the RX audio is sent to (the cable's playback "
            "endpoint, for a digital-mode app to decode).\n\n"
            "Set to “(none)” if you only TRANSMIT through VAC (mic / DAW → "
            "TX) and don't need RX audio on the PC.  This path plays "
            "independent of the monitor Mute, so pointing it at your real "
            "speakers makes RX audio play there uncontrollably — choose a "
            "virtual cable or “(none)”, not your speakers."));
        vf->addRow(tr("Output device"), vacDev);

        auto *vacGain = new QSpinBox(grp);
        vacGain->setRange(-60, 20);
        vacGain->setSingleStep(1);
        vacGain->setSuffix(tr(" dB"));
        vacGain->setValue(qRound(engine_->vac1RxGainDb()));
        vacGain->setToolTip(tr(
            "RX gain into the cable (reference default 0 dB).  Lower if the "
            "digital app's input is overdriven, raise if too quiet.  "
            "Independent of your monitor Volume."));
        vf->addRow(tr("RX gain"), vacGain);

        // VAC-in (PC → TX): input device + TX gain.  The captured audio
        // reaches TX only when the mic source is "PC Soundcard (VAC1)".
        auto *vacInDev = new QComboBox(grp);
        vacInDev->addItem(tr("(none)"));
        vacInDev->addItems(engine_->vac1InputDevicesFor(curApi0));
        {
            const int i = vacInDev->findText(engine_->vac1InputDeviceName());
            vacInDev->setCurrentIndex(i >= 0 ? i : 0);
        }
        vacInDev->setToolTip(tr("PC input device used as the TX mic when the "
            "mic source is “PC Soundcard (VAC1)” — the virtual "
            "cable's recording endpoint for a digital app, or a USB mic for "
            "voice."));
        vf->addRow(tr("Input device"), vacInDev);

        auto *vacTxGain = new QSpinBox(grp);
        vacTxGain->setRange(-60, 20);
        vacTxGain->setSingleStep(1);
        vacTxGain->setSuffix(tr(" dB"));
        vacTxGain->setValue(qRound(engine_->vac1TxGainDb()));
        vacTxGain->setToolTip(tr("TX gain (preamp) on the PC mic audio before "
            "the transmitter (reference default +3 dB)."));
        vf->addRow(tr("TX gain"), vacTxGain);

        // ── Latency posture (#158 follow-up) — squeeze ARQ turnaround ──
        // Buffer size = the PortAudio block (fixed ms/block); latency = the
        // rmatchV ring depth. Both ride per-profile (schema v5), so a VarAC
        // profile can carry tight rings while SSB stays fat & safe.
        auto *vacBuf = new QComboBox(grp);
        struct BufChoice { int frames; const char *label; };
        static const BufChoice kBufChoices[] = {
            {128,  "128 (~3 ms)"},   {256,  "256 (~5 ms)"},
            {512,  "512 (~11 ms)"},  {1024, "1024 (~21 ms)"},
            {2048, "2048 (~43 ms)"}, {4096, "4096 (~85 ms)"},
            {8192, "8192 (~171 ms)"},
        };
        for (const auto &c : kBufChoices)
            vacBuf->addItem(QString::fromLatin1(c.label), c.frames);
        {
            const int i = vacBuf->findData(engine_->vac1VacSize());
            vacBuf->setCurrentIndex(i >= 0 ? i : 4);   // default 2048
        }
        vacBuf->setToolTip(tr(
            "PortAudio buffer block (at 48 kHz, ms shown).  Smaller = lower "
            "TX↔RX turnaround (better for VarAC / ARQ digital), but more CPU "
            "and less dropout margin.  2048 is the safe default for SSB/voice."));
        vf->addRow(tr("Buffer size"), vacBuf);

        auto *vacLat = new QSpinBox(grp);
        vacLat->setRange(5, 500);
        vacLat->setSingleStep(5);
        vacLat->setSuffix(tr(" ms"));
        vacLat->setValue(engine_->vac1LatencyMs());
        vacLat->setToolTip(tr(
            "VAC ring-buffer latency (each direction).  Lower it to shave "
            "turnaround for VarAC — but keep it above ~2× the buffer-block "
            "time, or the ring underflows (watch the monitor below).  120 ms "
            "is the safe default."));
        vf->addRow(tr("Latency"), vacLat);

        // Live ring monitor (reference VAC1 Monitor): drop the latency until the
        // overflow/underflow counters just start to twitch, then back off.
        auto *vacMon = new QLabel(grp);
        vacMon->setWordWrap(true);
        vacMon->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
        vf->addRow(tr("Monitor"), vacMon);
        auto refreshMon = [this, vacMon]() {
            const QVariantMap d = engine_->vac1Diags();
            if (!d.value(QStringLiteral("active")).toBool()) {
                vacMon->setText(tr("VAC not running — enable it to see ring fill / "
                                   "overflow / underflow."));
                return;
            }
            vacMon->setText(tr("TO VAC: %1%% full · %2 ovf / %3 unf      "
                               "FROM VAC: %4%% full · %5 ovf / %6 unf")
                .arg(d.value(QStringLiteral("outPct")).toInt())
                .arg(d.value(QStringLiteral("outOver")).toInt())
                .arg(d.value(QStringLiteral("outUnder")).toInt())
                .arg(d.value(QStringLiteral("inPct")).toInt())
                .arg(d.value(QStringLiteral("inOver")).toInt())
                .arg(d.value(QStringLiteral("inUnder")).toInt()));
        };
        refreshMon();
        auto *vacMonTimer = new QTimer(grp);
        vacMonTimer->setInterval(500);
        connect(vacMonTimer, &QTimer::timeout, vacMon, refreshMon);
        vacMonTimer->start();

        auto *vacCombine = new QCheckBox(tr("Combine input (mono)"), grp);
        vacCombine->setChecked(engine_->vac1CombineInput());
        vacCombine->setToolTip(tr("Sum the captured left + right channels to "
            "mono before the transmitter, so a mic or app feeding either "
            "channel reaches the modulator. Leave ON for a voice mic or any "
            "mono source (recommended). Turn OFF only if you intentionally "
            "feed true stereo I/Q over the cable."));
        vf->addRow(QString(), vacCombine);

        auto *vacMuteVac = new QCheckBox(tr("Mute will mute VAC"), grp);
        vacMuteVac->setChecked(engine_->muteWillMuteVac());
        vacMuteVac->setToolTip(tr("When ON, the main Mute button also silences "
            "the VAC RX feed (matching the reference) — right when you monitor "
            "or record RX through VAC. The monitor Volume always rides the VAC "
            "feed regardless. Turn OFF for digital modes where the decoder "
            "(WSJT-X, etc.) must keep receiving while you mute the room."));
        vf->addRow(QString(), vacMuteVac);

        // → engine (each applies live + persists)
        connect(vacEnable, &QCheckBox::toggled, engine_,
                [this](bool on) { engine_->setVac1Enabled(on); });
        connect(vacAuto, &QCheckBox::toggled, engine_,
                [this](bool on) { engine_->setVac1AutoDigital(on); });
        // #158 DL-3 — Driver (host-API) change → persist + repopulate the
        // device combos for the new host API (preserve selection by name).
        connect(vacDriver, &QComboBox::activated, engine_,
                [this, vacDriver, vacDev, vacInDev](int) {
                    const int api = vacDriver->currentData().toInt();
                    engine_->setVac1HostApi(vacDriver->currentText());
                    auto repop = [this](QComboBox *c, const QStringList &devs) {
                        const QString keep = c->currentText();
                        c->blockSignals(true);
                        c->clear();
                        c->addItem(tr("(none)"));
                        c->addItems(devs);
                        const int i = c->findText(keep);
                        c->setCurrentIndex(i >= 0 ? i : 0);
                        c->blockSignals(false);
                    };
                    repop(vacDev,   engine_->vac1OutputDevicesFor(api));
                    repop(vacInDev, engine_->vac1InputDevicesFor(api));
                    // Selection may have changed/reset — push resolved names.
                    engine_->setVac1OutputDeviceName(
                        vacDev->currentIndex() <= 0 ? QString() : vacDev->currentText());
                    engine_->setVac1InputDeviceName(
                        vacInDev->currentIndex() <= 0 ? QString() : vacInDev->currentText());
                });
        connect(vacDev, &QComboBox::activated, engine_,
                [this, vacDev](int idx) {
                    // index 0 = "(none)" → clear → no RX-out direction opened
                    engine_->setVac1OutputDeviceName(
                        idx <= 0 ? QString() : vacDev->currentText());
                });
        connect(vacGain, qOverload<int>(&QSpinBox::valueChanged), engine_,
                [this](int db) { engine_->setVac1RxGainDb(db); });
        connect(vacInDev, &QComboBox::activated, engine_,
                [this, vacInDev](int idx) {
                    // index 0 = "(none)" → clear the input device
                    engine_->setVac1InputDeviceName(
                        idx <= 0 ? QString() : vacInDev->currentText());
                });
        connect(vacTxGain, qOverload<int>(&QSpinBox::valueChanged), engine_,
                [this](int db) { engine_->setVac1TxGainDb(db); });
        connect(vacBuf, qOverload<int>(&QComboBox::activated), engine_,
                [this, vacBuf](int) {
                    engine_->setVac1VacSize(vacBuf->currentData().toInt());
                });
        connect(vacLat, qOverload<int>(&QSpinBox::valueChanged), engine_,
                [this](int ms) { engine_->setVac1LatencyMs(ms); });
        connect(vacCombine, &QCheckBox::toggled, engine_,
                [this](bool on) { engine_->setVac1CombineInput(on); });
        connect(vacMuteVac, &QCheckBox::toggled, engine_,
                [this](bool on) { engine_->setMuteWillMuteVac(on); });

        // ← engine (reflect programmatic/external changes)
        connect(engine_, &lyra::dsp::WdspEngine::vac1Changed, grp,
                [this, vacEnable, vacAuto, vacGain, vacTxGain, vacCombine,
                 vacBuf, vacLat]() {
                    // Latency posture (profile load / external change) → reflect.
                    if (const int bi = vacBuf->findData(engine_->vac1VacSize());
                        bi >= 0 && vacBuf->currentIndex() != bi) {
                        vacBuf->blockSignals(true);
                        vacBuf->setCurrentIndex(bi);
                        vacBuf->blockSignals(false);
                    }
                    if (vacLat->value() != engine_->vac1LatencyMs()) {
                        vacLat->blockSignals(true);
                        vacLat->setValue(engine_->vac1LatencyMs());
                        vacLat->blockSignals(false);
                    }
                    if (vacEnable->isChecked() != engine_->vac1Enabled()) {
                        vacEnable->blockSignals(true);
                        vacEnable->setChecked(engine_->vac1Enabled());
                        vacEnable->blockSignals(false);
                    }
                    if (vacAuto->isChecked() != engine_->vac1AutoDigital()) {
                        vacAuto->blockSignals(true);
                        vacAuto->setChecked(engine_->vac1AutoDigital());
                        vacAuto->blockSignals(false);
                    }
                    const int gi = qRound(engine_->vac1RxGainDb());
                    if (vacGain->value() != gi) {
                        vacGain->blockSignals(true);
                        vacGain->setValue(gi);
                        vacGain->blockSignals(false);
                    }
                    const int ti = qRound(engine_->vac1TxGainDb());
                    if (vacTxGain->value() != ti) {
                        vacTxGain->blockSignals(true);
                        vacTxGain->setValue(ti);
                        vacTxGain->blockSignals(false);
                    }
                    if (vacCombine->isChecked() != engine_->vac1CombineInput()) {
                        vacCombine->blockSignals(true);
                        vacCombine->setChecked(engine_->vac1CombineInput());
                        vacCombine->blockSignals(false);
                    }
                });

        auto *vnote = new QLabel(tr(
            "Sets up the digital-mode feed.  The level is the RX gain here "
            "plus the app's own input — independent of your monitor Volume; "
            "keep AF Gain at a normal setting."), grp);
        vnote->setWordWrap(true);
        vnote->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
        vf->addRow(vnote);

        form->addRow(grp);
    }

    // ── #89 — Voice keyer recording options ──────────────────────────
    if (voiceKeyer_) {
        auto *grp = new QGroupBox(tr("Voice keyer recording"), page);
        auto *gf  = new QFormLayout(grp);

        auto *maxSpin = new QSpinBox(grp);
        maxSpin->setRange(5, 300);
        maxSpin->setSingleStep(5);
        maxSpin->setSuffix(tr(" s"));
        maxSpin->setValue(voiceKeyer_->recordMaxSec());
        maxSpin->setToolTip(tr("Recording auto-stops and saves at this length "
                               "(applies to both Mic messages and RX recordings). "
                               "Up to 5 minutes."));
        connect(maxSpin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int v) { voiceKeyer_->setRecordMaxSec(v); });
        connect(voiceKeyer_, &lyra::tx::VoiceKeyer::recordMaxSecChanged, maxSpin,
                [this, maxSpin]() {
                    if (maxSpin->value() != voiceKeyer_->recordMaxSec())
                        maxSpin->setValue(voiceKeyer_->recordMaxSec());
                });
        gf->addRow(tr("Max recorded length"), maxSpin);

        auto *delayCombo = new QComboBox(grp);
        delayCombo->addItem(tr("Off — record immediately"), 0);
        delayCombo->addItem(tr("5 s countdown"),  5);
        delayCombo->addItem(tr("10 s countdown"), 10);
        {
            const int idx = delayCombo->findData(voiceKeyer_->recordDelaySec());
            delayCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        }
        delayCombo->setToolTip(tr("A “get set” countdown before recording starts, "
                                  "so you can position the mic / take a breath. The "
                                  "countdown shows on the Voice Keyer panel."));
        connect(delayCombo, qOverload<int>(&QComboBox::activated), this,
                [this, delayCombo](int) {
                    voiceKeyer_->setRecordDelaySec(delayCombo->currentData().toInt());
                });
        gf->addRow(tr("Record countdown"), delayCombo);

        form->addRow(grp);
    }

    return page;
}

QWidget *SettingsDialog::buildBandsTab() {
    // A nested tab set so Time Stations + SW Database (EiBi) can join
    // Memory here later, matching old Lyra's Bands → (Memory / Time / SW).
    auto *page = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    auto *sub = new QTabWidget(page);

    // ---- Memory subtab ----
    if (memory_) {
    auto *mem = new QWidget(sub);
    auto *v = new QVBoxLayout(mem);

    auto *table = new QTableWidget(0, 7, mem);
    table->setHorizontalHeaderLabels(
        {tr("Name"), tr("Freq (MHz)"), tr("Mode"), tr("RX BW (Hz)"),
         tr("Offset (kHz)"), tr("CTCSS (Hz)"), tr("Notes")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto refreshing = std::make_shared<bool>(false);
    auto refresh = [this, table, refreshing]() {
        *refreshing = true;
        const auto &ps = memory_->presets();
        table->setRowCount(ps.size());
        for (int i = 0; i < ps.size(); ++i) {
            const auto &p = ps[i];
            table->setItem(i, 0, new QTableWidgetItem(p.name));
            table->setItem(i, 1, new QTableWidgetItem(
                QString::number(p.freq / 1.0e6, 'f', 6)));
            table->setItem(i, 2, new QTableWidgetItem(p.mode));
            table->setItem(i, 3, new QTableWidgetItem(
                p.rxBw > 0 ? QString::number(p.rxBw) : QString()));
            // Offset shown in kHz (signed; blank = simplex); CTCSS in Hz.
            table->setItem(i, 4, new QTableWidgetItem(
                p.offsetHz != 0 ? QString::number(p.offsetHz / 1000.0)
                                : QString()));
            table->setItem(i, 5, new QTableWidgetItem(
                p.ctcssToneHz > 0.0 ? QString::number(p.ctcssToneHz, 'f', 1)
                                    : QString()));
            table->setItem(i, 6, new QTableWidgetItem(p.notes));
        }
        *refreshing = false;
    };
    refresh();
    connect(memory_, &MemoryStore::changed, table, refresh);
    connect(table, &QTableWidget::itemChanged, table,
            [this, table, refreshing](QTableWidgetItem *) {
        if (*refreshing) return;
        const int r = table->currentRow() >= 0 ? table->currentRow() : 0;
        Q_UNUSED(r);
        // Rebuild every row from the table on any edit (simple + robust).
        for (int i = 0; i < table->rowCount() && i < memory_->count(); ++i) {
            MemoryStore::Preset p;
            p.name  = table->item(i, 0) ? table->item(i, 0)->text() : QString();
            p.freq  = qint64(qRound(
                (table->item(i, 1) ? table->item(i, 1)->text().toDouble() : 0.0)
                * 1.0e6));
            p.mode  = table->item(i, 2) ? table->item(i, 2)->text().toUpper()
                                        : QString();
            p.rxBw  = table->item(i, 3) ? table->item(i, 3)->text().toInt() : 0;
            p.offsetHz = table->item(i, 4)
                ? int(qRound(table->item(i, 4)->text().toDouble() * 1000.0)) : 0;
            p.ctcssToneHz = table->item(i, 5)
                ? table->item(i, 5)->text().toDouble() : 0.0;
            p.notes = table->item(i, 6) ? table->item(i, 6)->text() : QString();
            if (p.freq > 0) memory_->setPreset(i, p);
        }
    });
    v->addWidget(table);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("Store current"), mem);
    connect(addBtn, &QPushButton::clicked, mem, [this]() {
        if (!memory_->addCurrent(QString()))
            QMessageBox::information(this, tr("Memory"),
                tr("The memory bank is full (%1 presets).").arg(MemoryStore::kMax));
    });
    auto *delBtn = new QPushButton(tr("Delete"), mem);
    connect(delBtn, &QPushButton::clicked, mem, [this, table]() {
        const int r = table->currentRow();
        if (r >= 0) memory_->remove(r);
    });
    auto *clrBtn = new QPushButton(tr("Clear all"), mem);
    connect(clrBtn, &QPushButton::clicked, mem, [this]() {
        if (QMessageBox::question(this, tr("Memory"),
                tr("Delete all memory presets?")) == QMessageBox::Yes)
            memory_->clearAll();
    });
    auto *impBtn = new QPushButton(tr("Import CSV…"), mem);
    connect(impBtn, &QPushButton::clicked, mem, [this]() {
        const QString fn = QFileDialog::getOpenFileName(this,
            tr("Import memory CSV"), QString(),
            tr("CSV files (*.csv);;All files (*)"));
        if (fn.isEmpty()) return;
        const auto b = QMessageBox::question(this, tr("Import"),
            tr("Merge with existing presets, or replace them?\n\n"
               "Yes = merge,  No = replace."),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (b == QMessageBox::Cancel) return;
        const auto r = memory_->importCsv(fn, b == QMessageBox::No);
        if (!r.error.isEmpty())
            QMessageBox::warning(this, tr("Import"), r.error);
        else
            QMessageBox::information(this, tr("Import"),
                tr("Imported %1 preset(s); skipped %2.").arg(r.added).arg(r.skipped));
    });
    auto *expBtn = new QPushButton(tr("Export CSV…"), mem);
    connect(expBtn, &QPushButton::clicked, mem, [this]() {
        const QString fn = QFileDialog::getSaveFileName(this,
            tr("Export memory CSV"), QStringLiteral("lyra-memory.csv"),
            tr("CSV files (*.csv)"));
        if (fn.isEmpty()) return;
        if (!memory_->exportCsv(fn))
            QMessageBox::warning(this, tr("Export"),
                                 tr("Could not write the file."));
    });
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(clrBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(impBtn);
    btnRow->addWidget(expBtn);
    v->addLayout(btnRow);

    auto *hint = new QLabel(
        tr("Edit cells directly. Use the “Mem” button on the Band panel to "
           "store the current frequency and recall presets. RX BW blank = "
           "the mode default. For a REPEATER, set Offset (the TX shift in "
           "kHz, e.g. −100 for 10 m, −1000 = −1 MHz for 6 m) and CTCSS "
           "(access tone in Hz); recall arms SPLIT to the input + sends the "
           "tone. Blank Offset/CTCSS = simplex. Up to %1 presets.")
            .arg(MemoryStore::kMax), mem);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
    v->addWidget(hint);

    sub->addTab(mem, tr("Memory"));
    }  // if (memory_)

    // ---- SW Database (EiBi) subtab ----
    if (eibi_) {
        auto *sw = new QWidget(sub);
        auto *sv = new QVBoxLayout(sw);

        auto *enable = new QCheckBox(tr("Enable EiBi shortwave overlay"), sw);
        enable->setChecked(eibi_->enabled());
        connect(enable, &QCheckBox::toggled, eibi_,
                [this](bool on) { eibi_->setEnabled(on); });
        sv->addWidget(enable);

        auto *hideOff = new QCheckBox(tr("Hide stations that are off-air now"), sw);
        hideOff->setChecked(eibi_->hideOffAir());
        connect(hideOff, &QCheckBox::toggled, eibi_,
                [this](bool on) { eibi_->setHideOffAir(on); });
        sv->addWidget(hideOff);

        auto *forceAll = new QCheckBox(
            tr("Show in all bands (otherwise hidden inside amateur bands)"), sw);
        forceAll->setChecked(eibi_->forceAllBands());
        connect(forceAll, &QCheckBox::toggled, eibi_,
                [this](bool on) { eibi_->setForceAllBands(on); });
        sv->addWidget(forceAll);

        auto *pwrRow = new QHBoxLayout;
        pwrRow->addWidget(new QLabel(tr("Minimum transmitter power:"), sw));
        auto *pwr = new QComboBox(sw);
        pwr->addItems({tr("Any"), tr("≥ 50 kW"), tr("≥ 100 kW"), tr("≥ 250 kW")});
        pwr->setCurrentIndex(qBound(0, eibi_->minPower(), 3));
        connect(pwr, QOverload<int>::of(&QComboBox::currentIndexChanged), eibi_,
                [this](int i) { eibi_->setMinPower(i); });
        pwrRow->addWidget(pwr);
        pwrRow->addStretch(1);
        sv->addLayout(pwrRow);

        auto *status = new QLabel(eibi_->statusText(), sw);
        status->setWordWrap(true);
        status->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
        connect(eibi_, &EibiStore::changed, status,
                [this, status]() { status->setText(eibi_->statusText()); });

        auto *swBtns = new QHBoxLayout;
        auto *upd = new QPushButton(tr("Update database now"), sw);
        connect(upd, &QPushButton::clicked, sw, [this, upd]() {
            upd->setEnabled(false);
            upd->setText(tr("Downloading…"));
            eibi_->update();
        });
        connect(eibi_, &EibiStore::downloadFinished, upd,
                [this, upd](bool ok, const QString &msg) {
            upd->setEnabled(true);
            upd->setText(tr("Update database now"));
            if (ok) QMessageBox::information(this, tr("EiBi"), msg);
            else    QMessageBox::warning(this, tr("EiBi"), msg);
        });
        auto *loadBtn = new QPushButton(tr("Load CSV file…"), sw);
        connect(loadBtn, &QPushButton::clicked, sw, [this]() {
            const QString fn = QFileDialog::getOpenFileName(this,
                tr("Import EiBi CSV"), QString(),
                tr("CSV files (*.csv);;All files (*)"));
            if (fn.isEmpty()) return;
            if (eibi_->importFile(fn))
                QMessageBox::information(this, tr("EiBi"), eibi_->statusText());
            else
                QMessageBox::warning(this, tr("EiBi"),
                    tr("Could not read or parse that file. EiBi CSVs are "
                       "semicolon-delimited (KHZ;TIM;DAY;ITU;STN;…)."));
        });
        auto *page2 = new QPushButton(tr("Open EiBi website…"), sw);
        connect(page2, &QPushButton::clicked, sw, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://eibispace.de/")));
        });
        swBtns->addWidget(upd);
        swBtns->addWidget(loadBtn);
        swBtns->addWidget(page2);
        swBtns->addStretch(1);
        sv->addLayout(swBtns);
        sv->addWidget(status);

        auto *swHint = new QLabel(
            tr("EiBi (eibispace.de) is a free shortwave-broadcast schedule. "
               "Click “Update database now” to download the current season "
               "(~3 MB). If the download fails (their TLS certificate expires "
               "from time to time), download the sked-<season>.csv yourself "
               "from the EiBi site and use “Load CSV file…”. Active stations "
               "then appear on the panadapter; cyan = on-air now, grey = "
               "scheduled but off-air. Click a station to tune it in AM."), sw);
        swHint->setWordWrap(true);
        swHint->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
        sv->addWidget(swHint);
        sv->addStretch(1);

        sub->addTab(sw, tr("SW Database"));
    }

    outer->addWidget(sub);
    return page;
}

// Enumerate available COM ports the way the reference does.  Qt's
// QSerialPortInfo::availablePorts() only lists devices in the Windows "Ports
// (COM & LPT)" class, which MISSES virtual ports (com0com, VSPE, …) created
// outside that class — the #1 reason a user's virtual pair never appears.  The
// reference instead reads the registry HARDWARE\DEVICEMAP\SERIALCOMM (the same
// source .NET's SerialPort.GetPortNames() uses), which lists every device that
// registered a COM name regardless of class.  Read both and merge: SERIALCOMM
// for completeness, QSerialPortInfo for human-readable descriptions.  Sorted
// COMn-first by number, then virtual-named alphabetically.
static QList<std::pair<QString, QString>> enumComPorts() {
    QMap<QString, QString> ports;   // name -> description (QMap keeps it sorted)
#ifdef Q_OS_WIN
    QSettings reg(
        QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM"),
        QSettings::NativeFormat);
    for (const QString &k : reg.allKeys()) {
        const QString name = reg.value(k).toString().trimmed();
        if (!name.isEmpty() && !ports.contains(name))
            ports.insert(name, QString());
    }
#endif
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        ports.insert(info.portName(), info.description());

    QList<std::pair<QString, QString>> out;
    out.reserve(ports.size());
    for (auto it = ports.constBegin(); it != ports.constEnd(); ++it)
        out.append({it.key(), it.value()});

    auto comNum = [](const QString &s) -> int {
        if (s.startsWith(QLatin1String("COM"))) {
            bool ok = false;
            const int n = s.mid(3).toInt(&ok);
            if (ok) return n;
        }
        return -1;
    };
    std::sort(out.begin(), out.end(),
              [comNum](const std::pair<QString, QString> &a,
                       const std::pair<QString, QString> &b) {
        const int na = comNum(a.first), nb = comNum(b.first);
        if (na >= 0 && nb >= 0) return na < nb;   // both COMn → numeric
        if (na >= 0) return true;                 // COMn before virtual-named
        if (nb >= 0) return false;
        return a.first < b.first;                 // both non-COM → alphabetical
    });
    return out;
}

QWidget *SettingsDialog::buildCatSerialTab() {
    // COM-port features.  Today: serial PTT input (a digital app keys Lyra
    // over RTS/DTR).  The Kenwood TS-480 / TS-2000 CAT serial server lands
    // in this same tab next.  See docs/architecture/com_port_design.md.
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    // Resolve the COM-port name from an *editable* picker: a chosen list item
    // carries the real port name in its data; anything the operator typed by
    // hand (a com0com port Windows didn't enumerate, e.g. CNCB0 / COM12) is
    // taken verbatim from the text.  This is the unlock for virtual pairs that
    // never show up in QSerialPortInfo::availablePorts().
    auto resolvePort = [](QComboBox *c) -> QString {
        const int idx = c->currentIndex();
        if (idx >= 0 && c->itemText(idx) == c->currentText())
            return c->itemData(idx).toString();   // a listed item ("(none)" = "")
        return c->currentText().trimmed();         // hand-typed name
    };
    // Cap a field widget's width instead of letting it stretch the full panel.
    auto capW = [](QWidget *w, int px) -> QWidget * {
        w->setMaximumWidth(px);
        return w;
    };
    const QString kPortHint =
        tr("Ports auto-list, including virtual ones (com0com, VSPE, …).  If "
           "yours still isn't shown, type its exact name here — e.g. COM12 "
           "or CNCB0.");

    // Two-column layout: Serial PTT + Kenwood CAT 1 on the left, CAT 2 on the
    // right; the help text spans full width underneath (mirrors the other
    // refactored Settings tabs — 28 px gutter).
    auto *cols     = new QHBoxLayout();
    cols->setSpacing(28);
    auto *leftCol  = new QVBoxLayout();
    auto *rightCol = new QVBoxLayout();
    cols->addLayout(leftCol, 1);
    cols->addLayout(rightCol, 1);
    v->addLayout(cols);

    auto *grp  = new QGroupBox(tr("Serial PTT input"), page);
    auto *form = new QFormLayout(grp);
    form->setVerticalSpacing(10);

    // COM-port picker — enumerate live ports; keep the persisted (possibly
    // virtual) choice selectable even when it isn't present right now.
    auto *portCombo = new QComboBox(grp);
    portCombo->setEditable(true);
    portCombo->setInsertPolicy(QComboBox::NoInsert);
    portCombo->setToolTip(kPortHint);
    portCombo->lineEdit()->setPlaceholderText(tr("type COMx / CNCB0 if not listed"));
    capW(portCombo, 380);
    auto repopulate = [this, portCombo]() {
        const QString cur = serialPtt_->portName();
        QSignalBlocker b(portCombo);
        portCombo->clear();
        portCombo->addItem(tr("(none)"), QString());
        for (const auto &p : enumComPorts())
            portCombo->addItem(p.second.isEmpty()
                                   ? p.first
                                   : QStringLiteral("%1 — %2").arg(p.first, p.second),
                               p.first);
        if (!cur.isEmpty() && portCombo->findData(cur) < 0)
            portCombo->addItem(tr("%1 (not present)").arg(cur), cur);
        const int idx = portCombo->findData(cur);
        portCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    };
    repopulate();
    form->addRow(tr("COM port:"), portCombo);
    auto applyPttPort = [this, portCombo, resolvePort]() {
        serialPtt_->setPortName(resolvePort(portCombo));
    };
    connect(portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            serialPtt_, applyPttPort);
    connect(portCombo->lineEdit(), &QLineEdit::editingFinished,
            serialPtt_, applyPttPort);

    auto *rescan = new QPushButton(tr("Rescan ports"), grp);
    connect(rescan, &QPushButton::clicked, grp,
            [repopulate]() { repopulate(); });
    form->addRow(QString(), rescan);

    auto *lineCombo = new QComboBox(grp);
    lineCombo->addItem(tr("CTS  (app uses RTS)"),
                       int(lyra::cat::SerialPtt::Line::Cts));
    lineCombo->addItem(tr("DSR  (app uses DTR)"),
                       int(lyra::cat::SerialPtt::Line::Dsr));
    lineCombo->setCurrentIndex(int(serialPtt_->watchLine()));
    lineCombo->setToolTip(tr("Which input line carries the app's PTT.  On a "
                             "virtual COM-port pair the app's RTS crosses to "
                             "your CTS and DTR to your DSR.  If PTT seems stuck "
                             "or inverted, try the other line (or Invert)."));
    form->addRow(tr("PTT line:"), capW(lineCombo, 240));
    connect(lineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            serialPtt_, [this, lineCombo](int) {
        serialPtt_->setWatchLine(static_cast<lyra::cat::SerialPtt::Line>(
            lineCombo->currentData().toInt()));
    });

    auto *invert = new QCheckBox(tr("Invert (key on line LOW)"), grp);
    invert->setChecked(serialPtt_->invert());
    connect(invert, &QCheckBox::toggled, serialPtt_,
            [this](bool on) { serialPtt_->setInvert(on); });
    form->addRow(QString(), invert);

    auto *enable = new QCheckBox(tr("Serial PTT enabled"), grp);
    enable->setChecked(serialPtt_->enabled());
    connect(enable, &QCheckBox::toggled, serialPtt_,
            [this](bool on) { serialPtt_->setEnabled(on); });
    form->addRow(QString(), enable);

    auto *status = new QLabel(grp);
    status->setWordWrap(true);
    status->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
    auto refresh = [this, status]() {
        status->setText(serialPtt_->active()
                            ? tr("PTT ASSERTED — transmitting.")
                            : (serialPtt_->enabled() ? tr("Watching for PTT.")
                                                     : tr("Disabled.")));
    };
    refresh();
    connect(serialPtt_, &lyra::cat::SerialPtt::activeChanged, status, refresh);
    connect(serialPtt_, &lyra::cat::SerialPtt::enabledChanged, status, refresh);

    leftCol->addWidget(grp);
    leftCol->addWidget(status);

    // ── Kenwood CAT serial servers (one group per instance) ──────────
    for (int i = 0; i < catServers_.size(); ++i) {
        lyra::cat::CatServer *cat = catServers_.at(i);
        QVBoxLayout *col = (i == 0) ? leftCol : rightCol;
        auto *cg = new QGroupBox(tr("Kenwood CAT %1").arg(i + 1), page);
        auto *cf = new QFormLayout(cg);
        cf->setVerticalSpacing(10);

        auto *clabel = new QLineEdit(cat->label(), cg);
        clabel->setPlaceholderText(tr("what's it for? e.g. N1MM / VarAC / FLDIGI"));
        cf->addRow(tr("Label:"), capW(clabel, 360));
        connect(clabel, &QLineEdit::editingFinished, cat,
                [cat, clabel]() { cat->setLabel(clabel->text().trimmed()); });

        auto *transport = new QComboBox(cg);
        transport->addItem(tr("COM Port"),
                           int(lyra::cat::CatServer::Transport::Serial));
        transport->addItem(tr("TCP  (no virtual port needed)"),
                           int(lyra::cat::CatServer::Transport::Tcp));
        transport->setCurrentIndex(int(cat->transport()));
        cf->addRow(tr("Transport:"), capW(transport, 260));

        // Per-instance guidance: spell out exactly what the operator points
        // their app at for the chosen transport (updated in syncTransport).
        auto *cguide = new QLabel(cg);
        cguide->setWordWrap(true);
        cguide->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
        cf->addRow(cguide);

        auto *cport = new QComboBox(cg);
        cport->setEditable(true);
        cport->setInsertPolicy(QComboBox::NoInsert);
        cport->setToolTip(kPortHint);
        cport->lineEdit()->setPlaceholderText(tr("type COMx / CNCB0 if not listed"));
        auto crepop = [cat, cport]() {
            const QString cur = cat->portName();
            QSignalBlocker b(cport);
            cport->clear();
            cport->addItem(tr("(none)"), QString());
            for (const auto &p : enumComPorts())
                cport->addItem(p.second.isEmpty()
                                   ? p.first
                                   : QStringLiteral("%1 — %2").arg(p.first, p.second),
                               p.first);
            if (!cur.isEmpty() && cport->findData(cur) < 0)
                cport->addItem(tr("%1 (not present)").arg(cur), cur);
            const int idx = cport->findData(cur);
            cport->setCurrentIndex(idx < 0 ? 0 : idx);
        };
        crepop();
        cf->addRow(tr("COM port:"), capW(cport, 380));
        auto applyCatPort = [cat, cport, resolvePort]() {
            cat->setPortName(resolvePort(cport));
        };
        connect(cport, QOverload<int>::of(&QComboBox::currentIndexChanged),
                cat, applyCatPort);
        connect(cport->lineEdit(), &QLineEdit::editingFinished, cat, applyCatPort);

        auto *crescan = new QPushButton(tr("Rescan ports"), cg);
        connect(crescan, &QPushButton::clicked, cg, [crepop]() { crepop(); });
        cf->addRow(QString(), crescan);

        auto *baud = new QComboBox(cg);
        for (int b : {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200})
            baud->addItem(QString::number(b), b);
        if (baud->findData(cat->baud()) < 0)
            baud->addItem(QString::number(cat->baud()), cat->baud());
        baud->setCurrentIndex(baud->findData(cat->baud()));
        cf->addRow(tr("Baud:"), capW(baud, 160));
        connect(baud, QOverload<int>::of(&QComboBox::currentIndexChanged),
                cat, [cat, baud](int) { cat->setBaud(baud->currentData().toInt()); });

        auto *dataCombo = new QComboBox(cg);
        for (int d : {5, 6, 7, 8}) dataCombo->addItem(QString::number(d), d);
        dataCombo->setCurrentIndex(dataCombo->findData(cat->dataBits()));
        cf->addRow(tr("Data bits:"), capW(dataCombo, 120));
        connect(dataCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                cat, [cat, dataCombo](int) {
                    cat->setDataBits(dataCombo->currentData().toInt());
                });

        auto *parity = new QComboBox(cg);
        parity->addItem(tr("None"), 0);
        parity->addItem(tr("Even"), 1);
        parity->addItem(tr("Odd"),  2);
        parity->setCurrentIndex(parity->findData(cat->parity()));
        cf->addRow(tr("Parity:"), capW(parity, 140));
        connect(parity, QOverload<int>::of(&QComboBox::currentIndexChanged),
                cat, [cat, parity](int) { cat->setParity(parity->currentData().toInt()); });

        auto *stop = new QComboBox(cg);
        stop->addItem(QStringLiteral("1"), 1);
        stop->addItem(QStringLiteral("2"), 2);
        stop->setCurrentIndex(stop->findData(cat->stopBits()));
        cf->addRow(tr("Stop bits:"), capW(stop, 120));
        connect(stop, QOverload<int>::of(&QComboBox::currentIndexChanged),
                cat, [cat, stop](int) { cat->setStopBits(stop->currentData().toInt()); });

        auto *host = new QLineEdit(cat->host(), cg);
        host->setPlaceholderText(QStringLiteral("127.0.0.1"));
        cf->addRow(tr("TCP host:"), capW(host, 220));
        connect(host, &QLineEdit::editingFinished, cat,
                [cat, host]() { cat->setHost(host->text().trimmed()); });

        auto *tcpPort = new QSpinBox(cg);
        tcpPort->setRange(1, 65535);
        tcpPort->setValue(cat->tcpPort());
        cf->addRow(tr("TCP port:"), capW(tcpPort, 140));
        connect(tcpPort, QOverload<int>::of(&QSpinBox::valueChanged),
                cat, [cat](int v) { cat->setTcpPort(v); });

        auto *model = new QComboBox(cg);
        model->addItem(tr("TS-480  (ID 020)"),
                       int(lyra::cat::CatServer::RigModel::Ts480));
        model->addItem(tr("TS-2000  (ID 019)"),
                       int(lyra::cat::CatServer::RigModel::Ts2000));
        model->setCurrentIndex(int(cat->rigModel()));
        model->setToolTip(tr("Which Kenwood rig Lyra reports as.  Match this to "
                             "the rig model selected in your logger / digital "
                             "app.  Thetis-flavoured profiles (e.g. VarAC "
                             "“Anan Thetis”) expect TS-2000."));
        cf->addRow(tr("Rig model:"), capW(model, 240));
        connect(model, QOverload<int>::of(&QComboBox::currentIndexChanged),
                cat, [cat, model](int) {
            cat->setRigModel(static_cast<lyra::cat::CatServer::RigModel>(
                model->currentData().toInt()));
        });

        auto *cenable = new QCheckBox(tr("CAT server running"), cg);
        cenable->setChecked(cat->running());
        connect(cenable, &QCheckBox::toggled, cat,
                [cat](bool on) { cat->setEnabled(on); });
        cf->addRow(QString(), cenable);

        auto *cstatus = new QLabel(cg);
        cstatus->setWordWrap(true);
        cstatus->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
        auto crefresh = [cat, cstatus]() {
            cstatus->setText(cat->running() ? tr("Listening for CAT commands.")
                                            : tr("Stopped."));
        };
        crefresh();
        connect(cat, &lyra::cat::CatServer::enabledChanged, cstatus, crefresh);
        // Surface the real open/listen result (success detail OR the failure
        // reason — "cannot open COMx (Access is denied)") in the status line,
        // which the dialog otherwise never showed.
        connect(cat, &lyra::cat::CatServer::statusMessage, cstatus,
                [cstatus](const QString &m) { cstatus->setText(m); });

        // Show only the chosen transport's fields (serial params vs host/port)
        // and spell out exactly what the operator must point their app at —
        // the #1 com0com / COM-vs-TCP trip-up.
        auto syncTransport = [transport, cport, crescan, baud, dataCombo, parity,
                              stop, host, tcpPort, cf, cguide, cat]() {
            const bool tcp = transport->currentData().toInt()
                             == int(lyra::cat::CatServer::Transport::Tcp);
            cf->setRowVisible(cport,   !tcp);
            cf->setRowVisible(crescan, !tcp);
            cf->setRowVisible(baud,    !tcp);
            cf->setRowVisible(dataCombo, !tcp);
            cf->setRowVisible(parity,  !tcp);
            cf->setRowVisible(stop,    !tcp);
            cf->setRowVisible(host,    tcp);
            cf->setRowVisible(tcpPort, tcp);
            if (tcp)
                cguide->setText(tr("Point your logger / digital app at a "
                                   "<b>network (TCP) rig</b> at %1:%2 (e.g. "
                                   "Hamlib NET rigctl). No virtual COM port "
                                   "needed.")
                                    .arg(cat->host().isEmpty()
                                             ? QStringLiteral("127.0.0.1")
                                             : cat->host())
                                    .arg(cat->tcpPort()));
            else
                cguide->setText(tr("This is Lyra's end of a <b>virtual COM-port "
                                   "pair</b> (com0com or another virtual "
                                   "serial-port app). Point your app's CAT/rig "
                                   "port at the <b>OTHER</b> end of the same "
                                   "pair (not this port), rig = Kenwood, "
                                   "matching baud."));
        };
        syncTransport();
        connect(transport, QOverload<int>::of(&QComboBox::currentIndexChanged),
                cat, [cat, transport, syncTransport](int) {
            cat->setTransport(static_cast<lyra::cat::CatServer::Transport>(
                transport->currentData().toInt()));
            syncTransport();
        });
        // Keep the TCP guidance line current when host / port change.
        connect(host, &QLineEdit::editingFinished, cguide, syncTransport);
        connect(tcpPort, QOverload<int>::of(&QSpinBox::valueChanged),
                cguide, [syncTransport](int) { syncTransport(); });

        col->addWidget(cg);
        col->addWidget(cstatus);
    }
    leftCol->addStretch(1);
    rightCol->addStretch(1);

    auto *hint = new QLabel(
        tr("A digital-mode app (WSJT-X, VarAC, fldigi, …) keys Lyra over a "
           "serial PTT line (set the app's PTT method to RTS or DTR) and/or "
           "reads + sets frequency / mode over Kenwood CAT.  On Windows a "
           "virtual COM-port pair (com0com or another virtual serial-port app) "
           "links the app to Lyra — point the app at one end of the pair and "
           "Lyra at the other.  Lyra then transmits / tunes, which keys your "
           "HL2 / ANAN / Brick rig.  Many setups run CAT on one COM port and "
           "RTS/DTR PTT on a second."),
        page);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#9fb6ca;"));
    // Slightly larger than the field text so the how-it-works note stands out.
    QFont hintFont = hint->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() * 1.15);
    hint->setFont(hintFont);
    v->addWidget(hint);
    v->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildNetworkTab() {
    // Two-column layout (task #23, continuation of the Hardware/Visuals
    // multi-column work): TCI server on the left, DX-cluster spots on
    // the right.  The TCI status label hangs directly under the TCI
    // group (left column).  The descriptive hint label spans the full
    // width below both columns.  Same gutter (28 px) + same intra-form
    // row spacing (10 px) as the other refactored tabs.
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *cols   = new QWidget(page);
    auto *colsHb = new QHBoxLayout(cols);
    colsHb->setContentsMargins(0, 0, 0, 0);
    colsHb->setSpacing(28);
    auto *leftCol  = new QWidget(cols);
    auto *leftVb   = new QVBoxLayout(leftCol);
    leftVb->setContentsMargins(0, 0, 0, 0);
    auto *rightCol = new QWidget(cols);
    auto *rightVb  = new QVBoxLayout(rightCol);
    rightVb->setContentsMargins(0, 0, 0, 0);
    colsHb->addWidget(leftCol,  1);
    colsHb->addWidget(rightCol, 1);
    v->addWidget(cols);

    auto *grp = new QGroupBox(tr("TCI server"), leftCol);
    auto *form = new QFormLayout(grp);
    form->setVerticalSpacing(10);

    auto *host = new QLineEdit(tci_->bindHost(), grp);
    host->setPlaceholderText(QStringLiteral("127.0.0.1"));
    form->addRow(tr("Bind address:"), host);

    auto *port = new QSpinBox(grp);
    port->setRange(1, 65535);
    port->setValue(tci_->port());
    form->addRow(tr("Port:"), port);

    auto *rate = new QSpinBox(grp);
    rate->setRange(0, 1000);
    rate->setSuffix(tr(" ms"));
    rate->setValue(tci_->rateLimitMs());
    rate->setToolTip(tr("Minimum interval between repeated broadcasts of the "
                        "same parameter (0 = unlimited)."));
    form->addRow(tr("Rate limit:"), rate);
    connect(rate, QOverload<int>::of(&QSpinBox::valueChanged), tci_,
            [this](int ms) { tci_->setRateLimitMs(ms); });

    auto *initial = new QCheckBox(tr("Send full state to clients on connect"), grp);
    initial->setChecked(tci_->sendInitialState());
    connect(initial, &QCheckBox::toggled, tci_,
            [this](bool on) { tci_->setSendInitialState(on); });
    form->addRow(QString(), initial);

    auto *cwlu = new QCheckBox(tr("Add “CW” to the modulations list (CWL/CWU alias)"), grp);
    cwlu->setChecked(tci_->cwluBecomesCw());
    connect(cwlu, &QCheckBox::toggled, tci_,
            [this](bool on) { tci_->setCwluBecomesCw(on); });
    form->addRow(QString(), cwlu);

    auto *combo = new QCheckBox(tr("SDRLogger+ Combo (share CW Console contact)"), grp);
    combo->setChecked(tci_->comboEnabled());
    combo->setToolTip(tr("Two-way link with SDRLogger+ over this TCI connection.\n"
                         "When on, a callsign grabbed in the CW Decoder / typed in\n"
                         "the CW Console populates the SDRLogger+ log entry (and its\n"
                         "callbook lookup fills the {NAME} token back here). A macro\n"
                         "containing the {LOG} tag logs the QSO in SDRLogger+.\n"
                         "SDRLogger+ shows a “Lyra Combo: Linked” indicator while active."));
    connect(combo, &QCheckBox::toggled, tci_,
            [this](bool on) { tci_->setComboEnabled(on); });
    form->addRow(QString(), combo);

    auto *emuProto = new QCheckBox(tr("Emulate ExpertSDR3 (report protocol "
                                      "name “ExpertSDR3”)"), grp);
    emuProto->setChecked(tci_->emulateExpertSdr3());
    emuProto->setToolTip(tr("Some clients only recognise ExpertSDR3 / SunSDR "
                            "rigs. Enable these to masquerade as one."));
    connect(emuProto, &QCheckBox::toggled, tci_,
            [this](bool on) { tci_->setEmulateExpertSdr3(on); });
    form->addRow(QString(), emuProto);

    auto *emuDev = new QCheckBox(tr("Emulate SunSDR2 PRO (report device name "
                                    "“SunSDR2PRO”)"), grp);
    emuDev->setChecked(tci_->emulateSunSdr2());
    connect(emuDev, &QCheckBox::toggled, tci_,
            [this](bool on) { tci_->setEmulateSunSdr2(on); });
    form->addRow(QString(), emuDev);

    // ── Task #75 — TCI RX-out gain (stopgap until #49/#55 profiles).
    //    Negative dB to attenuate (the common case — 3rd-party
    //    skimmers like MSHV / JTDX often need much less level than
    //    Lyra's RX hands out at unity); positive dB to boost a
    //    quiet client.  Applied in TciServer at audio-block ingress
    //    before binary-frame emit.  Operator drags it live; takes
    //    effect at the next packet boundary (~43 ms at 48 kHz /
    //    2048-sample frames).  Range -40..+10 dB, default 0 (unity).
    if (prefs_) {
        auto *rxGainSpin = new QDoubleSpinBox(grp);
        rxGainSpin->setRange(-40.0, 10.0);
        rxGainSpin->setDecimals(1);
        rxGainSpin->setSingleStep(1.0);
        rxGainSpin->setSuffix(tr(" dB"));
        rxGainSpin->setValue(prefs_->tciRxGainDb());
        rxGainSpin->setToolTip(tr(
            "Attenuate (or boost) the audio Lyra sends to TCI "
            "clients (MSHV, JTDX, WSJT-X, etc.).  Negative dB reduces "
            "the level if Lyra's RX runs hotter than your client "
            "expects (e.g. you need to set the client's RX gain very "
            "low to avoid waterfall overload).  0 dB = byte-identical "
            "to Lyra's RX level.  Live: takes effect on the next "
            "emitted audio packet (~43 ms latency).\n\n"
            "Stopgap until per-mode RX/TX gain profiles ship "
            "(planned tasks #49 + #55)."));
        connect(rxGainSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                grp, [this](double v) {
            if (prefs_) prefs_->setTciRxGainDb(v);
        });
        connect(prefs_, &lyra::ui::Prefs::tciRxGainDbChanged, rxGainSpin,
                [this, rxGainSpin]() {
            const double v = prefs_->tciRxGainDb();
            if (rxGainSpin->value() != v) {
                QSignalBlocker b(rxGainSpin);
                rxGainSpin->setValue(v);
            }
        });
        form->addRow(tr("RX-out gain:"), rxGainSpin);

        // ── Task #108 — symmetric INBOUND TCI gain (operator-flagged
        //    2026-06-03: MSHV / JTDX / WSJT-X overdrive Lyra's TXA
        //    ALC).  Same range, same pattern as the RX-out spinner
        //    above, applied in TciServer at audio-block ingress —
        //    AFTER decode + resample, BEFORE TciMicSource hands the
        //    samples to TxDspWorker.  Stopgap until per-profile TCI
        //    gain ships under #49 + #55.
        auto *txGainSpin = new QDoubleSpinBox(grp);
        txGainSpin->setRange(-40.0, 10.0);
        txGainSpin->setDecimals(1);
        txGainSpin->setSingleStep(1.0);
        txGainSpin->setSuffix(tr(" dB"));
        txGainSpin->setValue(prefs_->tciTxGainDb());
        txGainSpin->setToolTip(tr(
            "Attenuate (or boost) the TX audio Lyra RECEIVES from TCI "
            "clients (MSHV, JTDX, WSJT-X, etc.) before it hits the "
            "WDSP TXA chain.  Negative dB reduces the level if your "
            "client sends audio hotter than Lyra's ALC handles "
            "gracefully (the common case — digital-mode software "
            "often outputs near full-scale by default).  Positive dB "
            "boosts a quiet client.  0 dB = byte-identical to your "
            "client's stream level.  Live: takes effect on the next "
            "received audio packet.\n\n"
            "Mirror of the RX-out gain above.  Stopgap until per-mode "
            "RX/TX gain profiles ship (planned tasks #49 + #55)."));
        connect(txGainSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                grp, [this](double v) {
            if (prefs_) prefs_->setTciTxGainDb(v);
        });
        connect(prefs_, &lyra::ui::Prefs::tciTxGainDbChanged, txGainSpin,
                [this, txGainSpin]() {
            const double v = prefs_->tciTxGainDb();
            if (txGainSpin->value() != v) {
                QSignalBlocker b(txGainSpin);
                txGainSpin->setValue(v);
            }
        });
        form->addRow(tr("TX-in gain:"), txGainSpin);
    }

    auto *enable = new QCheckBox(tr("TCI server running"), grp);
    enable->setChecked(tci_->running());
    form->addRow(QString(), enable);

    auto *status = new QLabel(grp);
    status->setWordWrap(true);
    status->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
    auto refreshStatus = [this, status]() {
        if (tci_->running()) {
            status->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
            status->setText(tr("Listening — %1 client(s) connected.")
                                .arg(tci_->clientCount()));
        } else if (!tci_->bindError().isEmpty()) {
            // Port collision (e.g. another app grabbed the TCI port) — show it
            // instead of a silent "Stopped." so the operator knows to change
            // the port.  40001 (the default) sits outside the Windows ephemeral
            // range, so this should be rare.
            status->setStyleSheet(QStringLiteral("color:#e0736b;"));
            status->setText(tr("Couldn't start — %1").arg(tci_->bindError()));
        } else {
            status->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
            status->setText(tr("Stopped."));
        }
    };
    refreshStatus();
    connect(tci_, &TciServer::runningChanged, status, refreshStatus);
    connect(tci_, &TciServer::clientCountChanged, status, refreshStatus);

    // Apply host/port live before (re)starting; editing them while running
    // rebinds automatically inside TciServer.
    connect(host, &QLineEdit::editingFinished, tci_,
            [this, host]() { tci_->setBindHost(host->text().trimmed()); });
    connect(port, QOverload<int>::of(&QSpinBox::valueChanged), tci_,
            [this](int p) { tci_->setPort(p); });
    connect(enable, &QCheckBox::toggled, tci_,
            [this, host, port](bool on) {
        if (on) {
            tci_->setBindHost(host->text().trimmed());
            tci_->setPort(port->value());
        }
        tci_->setEnabled(on);
    });
    connect(tci_, &TciServer::runningChanged, enable,
            [this, enable]() {
        QSignalBlocker b(enable);
        enable->setChecked(tci_->running());
    });

    leftVb->addWidget(grp);
    leftVb->addWidget(status);
    leftVb->addStretch(1);

    // DX-cluster spot display options.
    if (spots_) {
        auto *sg = new QGroupBox(tr("DX-cluster spots"), rightCol);
        auto *sf = new QFormLayout(sg);
        sf->setVerticalSpacing(10);

        auto *showSpots = new QCheckBox(tr("Show spots on the panadapter"), sg);
        showSpots->setChecked(spots_->showSpots());
        connect(showSpots, &QCheckBox::toggled, spots_,
                [this](bool on) { spots_->setShowSpots(on); });
        sf->addRow(QString(), showSpots);

        auto *maxSp = new QSpinBox(sg);
        maxSp->setRange(1, 5000);
        maxSp->setValue(spots_->maxSpots());
        maxSp->setToolTip(tr("How many spots to keep in memory (the bus). The "
                             "display caps below control how many are drawn."));
        connect(maxSp, QOverload<int>::of(&QSpinBox::valueChanged), spots_,
                [this](int n) { spots_->setMaxSpots(n); });
        sf->addRow(tr("Max spots:"), maxSp);

        // Clutter caps (#182 §5.4) — limit what's drawn without losing the bus.
        auto *dispMax = new QSpinBox(sg);
        dispMax->setRange(0, 1000);
        dispMax->setSpecialValueText(tr("unlimited"));
        dispMax->setValue(spots_->displayMax());
        dispMax->setToolTip(tr("Most spots shown on the panadapter at once "
                               "(newest win). 0 = unlimited."));
        connect(dispMax, QOverload<int>::of(&QSpinBox::valueChanged), spots_,
                [this](int n) { spots_->setDisplayMax(n); });
        sf->addRow(tr("Show at most:"), dispMax);

        auto *bktMax = new QSpinBox(sg);
        bktMax->setRange(0, 100);
        bktMax->setSpecialValueText(tr("off"));
        bktMax->setValue(spots_->bucketMax());
        bktMax->setToolTip(tr("Max spots per frequency before the rest collapse "
                              "into a \"+K\" badge (tames FT8 pile-ups). 0 = off."));
        connect(bktMax, QOverload<int>::of(&QSpinBox::valueChanged), spots_,
                [this](int n) { spots_->setBucketMax(n); });
        sf->addRow(tr("Per-freq max:"), bktMax);

        auto *bktHz = new QSpinBox(sg);
        bktHz->setRange(50, 20000);
        bktHz->setSingleStep(50);
        bktHz->setSuffix(tr(" Hz"));
        bktHz->setValue(spots_->bucketHz());
        bktHz->setToolTip(tr("How close two spots must be to share a bucket."));
        connect(bktHz, QOverload<int>::of(&QSpinBox::valueChanged), spots_,
                [this](int hz) { spots_->setBucketHz(hz); });
        sf->addRow(tr("Bucket width:"), bktHz);

        auto *life = new QSpinBox(sg);
        life->setRange(0, 1440);                 // up to 24 h, in minutes
        life->setSuffix(tr(" min"));
        life->setSpecialValueText(tr("never expire"));
        life->setValue(spots_->lifetimeSec() / 60);
        connect(life, QOverload<int>::of(&QSpinBox::valueChanged), spots_,
                [this](int m) { spots_->setLifetimeSec(m * 60); });
        sf->addRow(tr("Spot lifetime:"), life);

        // Mode display filter — show only these mode families on the
        // panadapter (e.g. "CW" to cut the FT8 clutter). Blank = all.
        auto *modeFilt = new QLineEdit(spots_->modeFilter(), sg);
        modeFilt->setPlaceholderText(tr("CW, SSB, DIGI   (blank = all)"));
        modeFilt->setToolTip(tr("Show only these mode families — CW, SSB, "
                                "DIGI, AM, FM. Blank shows every mode."));
        connect(modeFilt, &QLineEdit::editingFinished, spots_,
                [this, modeFilt]() { spots_->setModeFilter(modeFilt->text()); });
        sf->addRow(tr("Show modes:"), modeFilt);

        // Region filter — continent codes (NA/EU/AS/SA/AF/OC) and/or ISO-2
        // country codes (the codes shown on the markers). Blank = the world.
        auto *regionFilt = new QLineEdit(spots_->regionFilter(), sg);
        regionFilt->setPlaceholderText(tr("NA, EU, US   (blank = all)"));
        regionFilt->setToolTip(tr("Show only these regions — continent codes "
                                  "NA/EU/AS/SA/AF/OC, or country codes like US, "
                                  "CA, DE (the codes shown on the spots). "
                                  "e.g. \"US,EU\" = US plus all of Europe. Blank = all."));
        connect(regionFilt, &QLineEdit::editingFinished, spots_,
                [this, regionFilt]() { spots_->setRegionFilter(regionFilt->text()); });
        sf->addRow(tr("Show regions:"), regionFilt);

        // Marker colouring (#182 §6.1): source/client, single custom, or by
        // mode / region / country.  Single-colour swatch enables for "Single".
        auto *colorMode = new QComboBox(sg);
        colorMode->addItems({ tr("Source / client colour"), tr("Single colour"),
                              tr("By mode"), tr("By region"), tr("By country") });
        colorMode->setCurrentIndex(spots_->colorMode());
        sf->addRow(tr("Marker colour:"), colorMode);

        auto *singleCol = new QPushButton(sg);
        singleCol->setFixedWidth(64);
        auto paintSingle = [singleCol](const QString &hex) {
            singleCol->setStyleSheet(
                QStringLiteral("background:%1;border:1px solid #5a6573;").arg(hex));
        };
        paintSingle(spots_->singleColor());
        singleCol->setEnabled(spots_->colorMode() == 1);
        connect(singleCol, &QPushButton::clicked, sg, [this, paintSingle]() {
            const QColor c = QColorDialog::getColor(QColor(spots_->singleColor()),
                                                    this, tr("Spot colour"));
            if (c.isValid()) { spots_->setSingleColor(c.name()); paintSingle(c.name()); }
        });
        sf->addRow(tr("Single colour:"), singleCol);

        connect(colorMode, QOverload<int>::of(&QComboBox::currentIndexChanged), spots_,
                [this, singleCol](int idx) {
                    spots_->setColorMode(idx);
                    singleCol->setEnabled(idx == 1);
                });

        auto *legend = new QCheckBox(tr("Show colour legend on the panadapter"), sg);
        legend->setChecked(spots_->legendOn());
        connect(legend, &QCheckBox::toggled, spots_,
                [this](bool on) { spots_->setLegendOn(on); });
        sf->addRow(QString(), legend);

        auto *hlOwn = new QCheckBox(tr("Highlight my callsign when spotted"), sg);
        hlOwn->setChecked(spots_->highlightOwn());
        connect(hlOwn, &QCheckBox::toggled, spots_,
                [this](bool on) { spots_->setHighlightOwn(on); });
        sf->addRow(QString(), hlOwn);

        // Colour swatch button that opens a picker for the highlight colour.
        auto *hlColor = new QPushButton(sg);
        hlColor->setFixedWidth(64);
        auto paintSwatch = [hlColor](const QString &hex) {
            hlColor->setStyleSheet(
                QStringLiteral("background:%1;border:1px solid #5a6573;").arg(hex));
        };
        paintSwatch(spots_->highlightColor());
        connect(hlColor, &QPushButton::clicked, sg, [this, paintSwatch]() {
            const QColor c = QColorDialog::getColor(
                QColor(spots_->highlightColor()), this,
                tr("My-callsign highlight colour"));
            if (c.isValid()) {
                const QString hex = c.name();
                spots_->setHighlightColor(hex);
                paintSwatch(hex);
            }
        });
        sf->addRow(tr("Highlight colour:"), hlColor);

        // #172 — New-spot colour: a freshly-arrived spot renders in a distinct
        // colour for N seconds (so you notice it), then settles to its normal
        // colour.  (It's a solid colour, not a blink — hence "new-spot colour".)
        auto *flashNew = new QCheckBox(tr("Colour new spots"), sg);
        flashNew->setChecked(spots_->flashNew());
        flashNew->setToolTip(tr("Show a freshly-arrived spot in its own colour "
                                "for a few seconds so you notice it, then settle "
                                "to its normal colour."));
        connect(flashNew, &QCheckBox::toggled, spots_,
                [this](bool on) { spots_->setFlashNew(on); });
        sf->addRow(QString(), flashNew);

        auto *flashColor = new QPushButton(sg);
        flashColor->setFixedWidth(64);
        auto paintFlash = [flashColor](const QString &hex) {
            flashColor->setStyleSheet(
                QStringLiteral("background:%1;border:1px solid #5a6573;").arg(hex));
        };
        paintFlash(spots_->flashColor());
        connect(flashColor, &QPushButton::clicked, sg, [this, paintFlash]() {
            const QColor c = QColorDialog::getColor(
                QColor(spots_->flashColor()), this, tr("New-spot colour"));
            if (c.isValid()) {
                const QString hex = c.name();
                spots_->setFlashColor(hex);
                paintFlash(hex);
            }
        });
        sf->addRow(tr("New-spot colour:"), flashColor);

        auto *flashDur = new QSpinBox(sg);
        flashDur->setRange(1, 60);
        flashDur->setSuffix(tr(" s"));
        flashDur->setValue(spots_->flashSec());
        connect(flashDur, QOverload<int>::of(&QSpinBox::valueChanged), spots_,
                [this](int s) { spots_->setFlashSec(s); });
        sf->addRow(tr("New for:"), flashDur);

        auto *notify = new QCheckBox(tr("Pop up a notification when I'm spotted"), sg);
        notify->setChecked(spots_->notifyOwn());
        connect(notify, &QCheckBox::toggled, spots_,
                [this](bool on) { spots_->setNotifyOwn(on); });
        sf->addRow(QString(), notify);

        auto *cooldown = new QSpinBox(sg);
        cooldown->setRange(0, 240);
        cooldown->setSuffix(tr(" min"));
        cooldown->setSpecialValueText(tr("every time"));
        cooldown->setValue(spots_->notifyCooldownMin());
        cooldown->setToolTip(tr("Don't notify again about your callsign until "
                                "this long has passed (avoids cluster spam)."));
        connect(cooldown, QOverload<int>::of(&QSpinBox::valueChanged), spots_,
                [this](int m) { spots_->setNotifyCooldownMin(m); });
        sf->addRow(tr("Re-notify after:"), cooldown);

        // SpotHole — a standalone internet spot source (cluster/POTA/skimmer
        // aggregator).  Pours into the same bus as TCI; works with no
        // SDRLogger+ or telnet node configured.
        if (spotHole_) {
            auto *shHdr = new QLabel(tr("<b>SpotHole</b> — internet spot feed "
                                        "(no logger needed)"), sg);
            shHdr->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
            sf->addRow(shHdr);

            auto *shOn = new QCheckBox(tr("Fetch DX spots from SpotHole"), sg);
            shOn->setChecked(spotHole_->enabled());
            connect(shOn, &QCheckBox::toggled, spotHole_,
                    [this](bool on) { spotHole_->setEnabled(on); });
            sf->addRow(QString(), shOn);

            auto *shSrc = new QLineEdit(spotHole_->subSource(), sg);
            shSrc->setPlaceholderText(tr("Cluster,POTA   (blank = all)"));
            shSrc->setToolTip(tr("Which SpotHole sub-feeds to include — "
                                 "e.g. Cluster, POTA, SOTA. Blank = every source."));
            connect(shSrc, &QLineEdit::editingFinished, spotHole_,
                    [this, shSrc]() { spotHole_->setSubSource(shSrc->text().trimmed()); });
            sf->addRow(tr("Sources:"), shSrc);

            auto *shBand = new QCheckBox(tr("Only the band I'm on"), sg);
            shBand->setChecked(spotHole_->currentBandOnly());
            shBand->setToolTip(tr("Request only spots on your current band so "
                                  "the bus stays focused (recommended)."));
            connect(shBand, &QCheckBox::toggled, spotHole_,
                    [this](bool on) { spotHole_->setCurrentBandOnly(on); });
            sf->addRow(QString(), shBand);

            auto *shInt = new QSpinBox(sg);
            shInt->setRange(10, 600);
            shInt->setSuffix(tr(" s"));
            shInt->setValue(spotHole_->intervalSec());
            connect(shInt, QOverload<int>::of(&QSpinBox::valueChanged), spotHole_,
                    [this](int s) { spotHole_->setIntervalSec(s); });
            sf->addRow(tr("Poll every:"), shInt);

            auto *shAge = new QSpinBox(sg);
            shAge->setRange(1, 120);
            shAge->setSuffix(tr(" min"));
            shAge->setValue(spotHole_->maxAgeSec() / 60);
            shAge->setToolTip(tr("On connect, pull spots received within this "
                                 "window."));
            connect(shAge, QOverload<int>::of(&QSpinBox::valueChanged), spotHole_,
                    [this](int m) { spotHole_->setMaxAgeSec(m * 60); });
            sf->addRow(tr("Initial age:"), shAge);

            auto *shStatus = new QLabel(sg);
            shStatus->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
            auto *shNow = new QPushButton(tr("Refresh now"), sg);
            connect(shNow, &QPushButton::clicked, spotHole_,
                    [this]() { spotHole_->refresh(); });
            connect(spotHole_, &SpotHoleFeeder::statusChanged, shStatus,
                    [shStatus](const QString &m) { shStatus->setText(m); });
            auto *shRow = new QHBoxLayout;
            shRow->addWidget(shNow);
            shRow->addWidget(shStatus, 1);
            sf->addRow(QString(), shRow);
        }

        // DX-cluster telnet — connect to a specific node (VE7CC / DXSpider /
        // AR-Cluster).  Standalone, no SpotHole.
        if (dxCluster_) {
            auto *tnHdr = new QLabel(tr("<b>DX-cluster (telnet)</b> — a specific "
                                        "node, e.g. VE7CC"), sg);
            tnHdr->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
            sf->addRow(tnHdr);

            auto *tnOn = new QCheckBox(tr("Connect to a DX-cluster node"), sg);
            tnOn->setChecked(dxCluster_->enabled());
            connect(tnOn, &QCheckBox::toggled, dxCluster_,
                    [this](bool on) { dxCluster_->setEnabled(on); });
            sf->addRow(QString(), tnOn);

            auto *tnHost = new QLineEdit(dxCluster_->host(), sg);
            tnHost->setPlaceholderText(tr("dxc.ve7cc.net"));
            connect(tnHost, &QLineEdit::editingFinished, dxCluster_,
                    [this, tnHost]() { dxCluster_->setHost(tnHost->text()); });
            sf->addRow(tr("Host:"), tnHost);

            auto *tnPort = new QSpinBox(sg);
            tnPort->setRange(1, 65535);
            tnPort->setValue(dxCluster_->port());
            connect(tnPort, QOverload<int>::of(&QSpinBox::valueChanged), dxCluster_,
                    [this](int p) { dxCluster_->setPort(p); });
            sf->addRow(tr("Port:"), tnPort);

            auto *tnCall = new QLineEdit(dxCluster_->loginCall(), sg);
            tnCall->setPlaceholderText(tr("defaults to your callsign"));
            tnCall->setToolTip(tr("Login callsign for the node — leave blank to "
                                  "use your MYCALL, or set a club / friend's / "
                                  "node-registered call."));
            connect(tnCall, &QLineEdit::editingFinished, dxCluster_,
                    [this, tnCall]() { dxCluster_->setLoginCall(tnCall->text()); });
            sf->addRow(tr("Login call:"), tnCall);

            auto *tnCmds = new QLineEdit(dxCluster_->loginCommands(), sg);
            tnCmds->setPlaceholderText(tr("optional, e.g. set/ft8"));
            tnCmds->setToolTip(tr("Commands sent right after login (node-specific "
                                  "filters etc.). Separate multiple with ';' or "
                                  "new lines. Leave blank for node defaults."));
            connect(tnCmds, &QLineEdit::editingFinished, dxCluster_,
                    [this, tnCmds]() {
                        QString c = tnCmds->text();
                        c.replace(QLatin1Char(';'), QLatin1Char('\n'));
                        dxCluster_->setLoginCommands(c);
                    });
            sf->addRow(tr("Login cmds:"), tnCmds);

            auto *tnStatus = new QLabel(sg);
            tnStatus->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
            auto *tnNow = new QPushButton(tr("Reconnect"), sg);
            connect(tnNow, &QPushButton::clicked, dxCluster_,
                    [this]() { dxCluster_->reconnectNow(); });
            connect(dxCluster_, &DxClusterFeeder::statusChanged, tnStatus,
                    [tnStatus](const QString &m) { tnStatus->setText(m); });
            auto *tnRow = new QHBoxLayout;
            tnRow->addWidget(tnNow);
            tnRow->addWidget(tnStatus, 1);
            sf->addRow(QString(), tnRow);
        }

        auto *clearSpots = new QPushButton(tr("Clear spots now"), sg);
        connect(clearSpots, &QPushButton::clicked, spots_,
                [this]() { spots_->clearAll(); });
        sf->addRow(QString(), clearSpots);

        rightVb->addWidget(sg);
        rightVb->addStretch(1);
    }

    auto *hint = new QLabel(
        tr("Lets logging and cluster software (SDRLogger+, Log4OM, WSJT-X, …) "
           "control and read Lyra over a WebSocket — frequency, mode, "
           "start/stop, and DX-cluster spots. The de-facto default port is "
           "<b>50001</b>; bind to 127.0.0.1 for same-PC apps, or 0.0.0.0 to "
           "allow other machines on your network. Lyra is receive-only, so "
           "transmit commands are acknowledged inactive.<br><br>"
           "Implements the public TCI v1.9 / v2.0 specification "
           "(© EESDR Expert Electronics)."), page);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#8fa6ba;"));
    v->addWidget(hint);
    v->addStretch(1);
    return page;
}

// Best-effort locate of an installed SDRLoggerPlus.exe (Rick + Brent's
// companion logger).  The NSIS assisted installer lets the user change the
// install dir, so the Windows uninstall-registry entry is authoritative;
// falls back to the default per-user / Program Files locations.  Returns ""
// if not found (and on non-Windows).
static QString detectSdrLoggerPlusExe() {
#ifdef _WIN32
    const QStringList roots = {
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\"
                       "CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\"
                       "CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\"
                       "Windows\\CurrentVersion\\Uninstall"),
    };
    for (const QString &root : roots) {
        QSettings reg(root, QSettings::NativeFormat);
        const QStringList keys = reg.childGroups();
        for (const QString &key : keys) {
            const QString name = reg.value(key + "/DisplayName").toString();
            if (!name.contains(QStringLiteral("SDRLoggerPlus"),
                               Qt::CaseInsensitive))
                continue;
            const QString loc = reg.value(key + "/InstallLocation").toString();
            if (!loc.isEmpty()) {
                const QString exe =
                    QDir(loc).filePath(QStringLiteral("SDRLoggerPlus.exe"));
                if (QFileInfo::exists(exe))
                    return QDir::toNativeSeparators(exe);
            }
            QString icon = reg.value(key + "/DisplayIcon").toString();
            if (!icon.isEmpty()) {
                icon = icon.section(QLatin1Char(','), 0, 0)
                           .remove(QLatin1Char('"'));
                if (QFileInfo::exists(icon))
                    return QDir::toNativeSeparators(icon);
            }
        }
    }
    const QStringList bases = {
        qEnvironmentVariable("LOCALAPPDATA") + "\\Programs\\SDRLoggerPlus",
        qEnvironmentVariable("PROGRAMFILES")  + "\\SDRLoggerPlus",
        qEnvironmentVariable("ProgramFiles(x86)") + "\\SDRLoggerPlus",
    };
    for (const QString &b : bases) {
        const QString exe = b + "\\SDRLoggerPlus.exe";
        if (QFileInfo::exists(exe)) return QDir::toNativeSeparators(exe);
    }
#endif
    return QString();
}

QWidget *SettingsDialog::buildHardwareTab() {
    // Two-column layout (task #22, operator-flagged 3× — the Hardware
    // tab had grown to ~10 groups + 7 inline form rows + the Transmit
    // group, overflowing the dialog height).  Wider is easier to scan
    // than scrolling.  Implementation is intentionally minimal: two
    // sibling QFormLayouts inside a horizontal split; the local `form`
    // pointer is repointed midway so the existing `form->addRow(...)`
    // call sites stay byte-identical.  No widget construction changes,
    // no signal/slot re-wires, no QSettings churn.
    //
    // Layout (top-down, left then right):
    //   LEFT  : Operator/Station, Band plan, Band panel, Diagnostics
    //   RIGHT : Radio (HL2 discovery), external-HW + BCD inline rows,
    //           Transmit (TX safety + PA enable)
    auto *page  = new QWidget(this);
    auto *outer = new QHBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(28);

    auto *leftCol  = new QWidget(page);
    auto *leftForm = new QFormLayout(leftCol);
    leftForm->setContentsMargins(0, 0, 0, 0);
    leftForm->setVerticalSpacing(10);
    outer->addWidget(leftCol, 1);

    auto *rightCol  = new QWidget(page);
    auto *rightForm = new QFormLayout(rightCol);
    rightForm->setContentsMargins(0, 0, 0, 0);
    rightForm->setVerticalSpacing(10);
    outer->addWidget(rightCol, 1);

    // The body below uses `form->addRow(...)` extensively.  Start it
    // pointing at the left column; switch to the right column just
    // before the Radio group lower down.  Avoids touching ~500 lines
    // of widget code.
    QFormLayout *form = leftForm;

    // --- Operator / Station ---
    // Callsign + Maidenhead grid (+ manual lat/lon fallback).  The
    // location drives the weather sources + solar panel; callsign feeds
    // TCI/logging.  Grid wins over manual when valid.
    {
        auto *grp = new QGroupBox(tr("Operator / Station"), page);
        auto *g = new QGridLayout(grp);
        g->setColumnStretch(1, 1);

        g->addWidget(new QLabel(tr("Callsign"), grp), 0, 0);
        auto *call = new QLineEdit(prefs_->callsign(), grp);
        call->setFixedWidth(140);
        call->setPlaceholderText(tr("e.g. N8SDR"));
        connect(call, &QLineEdit::editingFinished, grp,
                [this, call]() { prefs_->setCallsign(call->text()); });
        connect(prefs_, &Prefs::callsignChanged, call, [this, call]() {
            if (call->text() != prefs_->callsign()) call->setText(prefs_->callsign());
        });
        g->addWidget(call, 0, 1);

        g->addWidget(new QLabel(tr("Grid square"), grp), 1, 0);
        auto *grid = new QLineEdit(prefs_->gridSquare(), grp);
        grid->setFixedWidth(140);
        grid->setPlaceholderText(tr("e.g. EN82"));
        g->addWidget(grid, 1, 1);

        g->addWidget(new QLabel(tr("Computed lat/lon"), grp), 2, 0);
        auto *comp = new QLabel(grp);
        comp->setStyleSheet(QStringLiteral("QLabel{color:#8fa6ba;}"));
        g->addWidget(comp, 2, 1);

        g->addWidget(new QLabel(tr("Manual lat (°)"), grp), 3, 0);
        auto *latE = new QLineEdit(grp);
        latE->setFixedWidth(140);
        latE->setPlaceholderText(tr("e.g. 41.5  (fallback if no grid)"));
        if (!std::isnan(prefs_->manualLat()))
            latE->setText(QString::number(prefs_->manualLat(), 'f', 4));
        g->addWidget(latE, 3, 1);

        g->addWidget(new QLabel(tr("Manual lon (°)"), grp), 4, 0);
        auto *lonE = new QLineEdit(grp);
        lonE->setFixedWidth(140);
        lonE->setPlaceholderText(tr("e.g. -83.6"));
        if (!std::isnan(prefs_->manualLon()))
            lonE->setText(QString::number(prefs_->manualLon(), 'f', 4));
        g->addWidget(lonE, 4, 1);

        auto refreshComputed = [this, comp]() {
            double la = 0, lo = 0;
            if (prefs_->operatorLocation(&la, &lo))
                comp->setText(QStringLiteral("%1, %2")
                                  .arg(la, 0, 'f', 4).arg(lo, 0, 'f', 4));
            else
                comp->setText(tr("— not set —"));
        };
        refreshComputed();
        connect(prefs_, &Prefs::locationChanged, comp, refreshComputed);

        connect(grid, &QLineEdit::editingFinished, grp, [this, grid]() {
            prefs_->setGridSquare(grid->text());
            if (grid->text() != prefs_->gridSquare())   // reflect normalization
                grid->setText(prefs_->gridSquare());
        });
        connect(prefs_, &Prefs::gridSquareChanged, grid, [this, grid]() {
            if (grid->text() != prefs_->gridSquare()) grid->setText(prefs_->gridSquare());
        });

        auto applyManual = [this, latE, lonE]() {
            const QString lt = latE->text().trimmed();
            const QString ln = lonE->text().trimmed();
            if (lt.isEmpty() && ln.isEmpty()) {       // clear override
                prefs_->setManualLatLon(std::nan(""), std::nan(""));
                return;
            }
            bool ok1 = false, ok2 = false;
            const double la = lt.toDouble(&ok1);
            const double lo = ln.toDouble(&ok2);
            if (ok1 && ok2) prefs_->setManualLatLon(la, lo);
        };
        connect(latE, &QLineEdit::editingFinished, grp, applyManual);
        connect(lonE, &QLineEdit::editingFinished, grp, applyManual);

        grp->setToolTip(tr("Your location drives the weather alerts and the "
                           "solar panel. The grid square is used when valid; "
                           "manual lat/lon is the fallback."));
        form->addRow(grp);
    }

    // --- Band plan (region for panadapter overlays) ---
    {
        auto *grp = new QGroupBox(tr("Band plan"), page);
        auto *g = new QGridLayout(grp);
        g->setColumnStretch(1, 1);
        g->addWidget(new QLabel(tr("Region"), grp), 0, 0);
        auto *region = new QComboBox(grp);
        region->addItem(tr("US / IARU Region 2"),      QStringLiteral("US"));
        region->addItem(tr("IARU Region 1 (EU / Africa)"), QStringLiteral("IARU_R1"));
        region->addItem(tr("IARU Region 3 (Asia-Pacific)"), QStringLiteral("IARU_R3"));
        region->addItem(tr("None"),                    QStringLiteral("NONE"));
        region->setCurrentIndex(std::max(0, region->findData(prefs_->bandPlanRegion())));
        region->setToolTip(tr("Region for the amateur band-plan overlay "
                              "(sub-band segments, landmarks, out-of-band "
                              "advisories) on the panadapter."));
        connect(region, &QComboBox::currentIndexChanged, grp, [this, region](int) {
            prefs_->setBandPlanRegion(region->currentData().toString());
        });
        connect(prefs_, &Prefs::bandPlanRegionChanged, grp, [this, region]() {
            const int i = region->findData(prefs_->bandPlanRegion());
            if (i >= 0 && region->currentIndex() != i) region->setCurrentIndex(i);
        });
        g->addWidget(region, 0, 1);

        // Optional country override, layered on the region for bands whose
        // allocation deviates from the IARU region (currently 60m for
        // UK / Canada).  "Auto" = use the region table unchanged.
        g->addWidget(new QLabel(tr("Country"), grp), 1, 0);
        auto *country = new QComboBox(grp);
        country->addItem(tr("Auto (use region)"), QStringLiteral("AUTO"));
        country->addItem(tr("United Kingdom"),    QStringLiteral("UK"));
        country->addItem(tr("Canada"),            QStringLiteral("CA"));
        country->setCurrentIndex(
            std::max(0, country->findData(prefs_->bandPlanCountry())));
        country->setToolTip(tr("Refines the band plan for countries whose "
                               "allocation differs from their IARU region "
                               "(currently the 60m band for UK / Canada). "
                               "Leave on Auto to use the region default."));
        connect(country, &QComboBox::currentIndexChanged, grp,
                [this, country](int) {
                    prefs_->setBandPlanCountry(country->currentData().toString());
                });
        connect(prefs_, &Prefs::bandPlanCountryChanged, grp, [this, country]() {
            const int i = country->findData(prefs_->bandPlanCountry());
            if (i >= 0 && country->currentIndex() != i) country->setCurrentIndex(i);
        });
        g->addWidget(country, 1, 1);

        // Overlay-layer toggles (the panadapter top strip).  Each mirrors
        // a Prefs bool; Region = None hides everything regardless.
        auto addLayer = [this, g](int row, const QString &text,
                                  const QString &tip, bool checked,
                                  std::function<void(bool)> set,
                                  QWidget *parent) -> QCheckBox * {
            auto *ck = new QCheckBox(text, parent);
            ck->setChecked(checked);
            ck->setToolTip(tip);
            QObject::connect(ck, &QCheckBox::toggled, parent, set);
            g->addWidget(ck, row, 0, 1, 2);
            return ck;
        };
        auto *segCk = addLayer(2, tr("Sub-band segments"),
            tr("Coloured CW / digital / SSB / FM sub-band strip."),
            prefs_->bandPlanSegments(),
            [this](bool v){ prefs_->setBandPlanSegments(v); }, grp);
        connect(prefs_, &Prefs::bandPlanSegmentsChanged, segCk, [this, segCk]() {
            if (segCk->isChecked() != prefs_->bandPlanSegments())
                segCk->setChecked(prefs_->bandPlanSegments());
        });
        auto *landCk = addLayer(3, tr("Digital landmarks (FT8 / FT4 / WSPR / PSK)"),
            tr("Markers at the common digital-mode calling frequencies. "
               "Click one to tune there."),
            prefs_->bandPlanLandmarks(),
            [this](bool v){ prefs_->setBandPlanLandmarks(v); }, grp);
        connect(prefs_, &Prefs::bandPlanLandmarksChanged, landCk, [this, landCk]() {
            if (landCk->isChecked() != prefs_->bandPlanLandmarks())
                landCk->setChecked(prefs_->bandPlanLandmarks());
        });
        auto *beaconCk = addLayer(4, tr("NCDXF beacon markers"),
            tr("The 5 NCDXF International Beacon Project frequencies."),
            prefs_->bandPlanBeacons(),
            [this](bool v){ prefs_->setBandPlanBeacons(v); }, grp);
        connect(prefs_, &Prefs::bandPlanBeaconsChanged, beaconCk, [this, beaconCk]() {
            if (beaconCk->isChecked() != prefs_->bandPlanBeacons())
                beaconCk->setChecked(prefs_->bandPlanBeacons());
        });
        auto *edgeCk = addLayer(5, tr("Band-edge warning lines"),
            tr("Red dashed lines at each band's edges."),
            prefs_->bandPlanEdges(),
            [this](bool v){ prefs_->setBandPlanEdges(v); }, grp);
        connect(prefs_, &Prefs::bandPlanEdgesChanged, edgeCk, [this, edgeCk]() {
            if (edgeCk->isChecked() != prefs_->bandPlanEdges())
                edgeCk->setChecked(prefs_->bandPlanEdges());
        });
        auto *classCk = addLayer(6, tr("License-class edges (US)"),
            tr("Amber markers at the US phone sub-band edges where each "
               "license class (Extra / Advanced / General / Tech) gains "
               "privileges.  US-only; the operator is responsible for "
               "knowing their own class limits."),
            prefs_->bandPlanClassEdges(),
            [this](bool v){ prefs_->setBandPlanClassEdges(v); }, grp);
        connect(prefs_, &Prefs::bandPlanClassEdgesChanged, classCk, [this, classCk]() {
            if (classCk->isChecked() != prefs_->bandPlanClassEdges())
                classCk->setChecked(prefs_->bandPlanClassEdges());
        });
        auto *txWarnCk = addLayer(7, tr("Warn on transmit out of band"),
            tr("Posts an advisory at key-down if your transmit signal falls "
               "outside the amateur band for your region.  Checks the whole "
               "occupied bandwidth (mode + TX filter), not just the dial "
               "frequency — so a wide signal that reaches past the band edge "
               "warns even when the carrier is in band; VFO B is used when "
               "split.  Advisory only — it never inhibits transmit.  "
               "Independent of the band-edge lines above so decluttering the "
               "display can't disable the transmit-safety warning."),
            prefs_->bandPlanTxWarn(),
            [this](bool v){ prefs_->setBandPlanTxWarn(v); }, grp);
        connect(prefs_, &Prefs::bandPlanTxWarnChanged, txWarnCk, [this, txWarnCk]() {
            if (txWarnCk->isChecked() != prefs_->bandPlanTxWarn())
                txWarnCk->setChecked(prefs_->bandPlanTxWarn());
        });

        // Per-mode segment colours — a swatch button per kind (click to
        // recolour; picking the default clears the override).
        g->addWidget(new QLabel(tr("Segment colors"), grp), 8, 0);
        auto *colorRow = new QWidget(grp);
        auto *ch = new QHBoxLayout(colorRow);
        ch->setContentsMargins(0, 0, 0, 0);
        ch->setSpacing(6);
        const QStringList kinds = {QStringLiteral("CW"), QStringLiteral("DIG"),
                                   QStringLiteral("SSB"), QStringLiteral("FM")};
        for (const QString &k : kinds) {
            auto *btn = new QPushButton(k, colorRow);
            btn->setFixedWidth(48);
            btn->setCursor(Qt::PointingHandCursor);
            auto applySwatch = [this, btn, k]() {
                btn->setStyleSheet(QStringLiteral(
                    "QPushButton{background:%1;color:#ffffff;font-weight:700;"
                    "border:1px solid #2a3a4a;border-radius:3px;padding:3px;}")
                    .arg(prefs_->bandPlanColor(k)));
            };
            applySwatch();
            connect(btn, &QPushButton::clicked, colorRow, [this, k]() {
                const QColor picked = QColorDialog::getColor(
                    QColor(prefs_->bandPlanColor(k)), this,
                    tr("%1 segment colour").arg(k));
                if (picked.isValid())
                    prefs_->setBandPlanColor(k, picked.name());
            });
            connect(prefs_, &Prefs::bandPlanColorsChanged, btn, applySwatch);
            ch->addWidget(btn);
        }
        auto *resetColors = new QPushButton(tr("Reset"), colorRow);
        resetColors->setToolTip(tr("Restore the default segment colors."));
        connect(resetColors, &QPushButton::clicked, colorRow, [this, kinds]() {
            for (const QString &k : kinds)
                prefs_->setBandPlanColor(k, QString());   // "" → revert to default
        });
        ch->addWidget(resetColors);
        ch->addStretch(1);
        g->addWidget(colorRow, 8, 1);

        form->addRow(grp);
    }

    // --- Band panel options ---
    {
        auto *grp = new QGroupBox(tr("Band panel"), page);
        auto *v = new QVBoxLayout(grp);
        auto *cb = new QCheckBox(
            tr("Show 11m / CB band row (26.965–27.405 MHz)"), grp);
        cb->setChecked(prefs_->cbBandEnabled());
        cb->setToolTip(tr("Adds a CB / 11-meter row to the Band panel for "
                          "Citizens Band listening."));
        connect(cb, &QCheckBox::toggled, grp,
                [this](bool on) { prefs_->setCbBandEnabled(on); });
        connect(prefs_, &Prefs::cbBandEnabledChanged, cb, [this, cb]() {
            if (cb->isChecked() != prefs_->cbBandEnabled())
                cb->setChecked(prefs_->cbBandEnabled());
        });
        v->addWidget(cb);
        form->addRow(grp);
    }

    // --- Diagnostics (debug logging) ---
    // Release builds run without a console window. This toggle turns on
    // verbose capture so the in-app Log viewer / log file have the full
    // detail we need when chasing a problem.
    {
        auto *grp = new QGroupBox(tr("Diagnostics"), page);
        auto *v = new QVBoxLayout(grp);

        auto *dbg = new QCheckBox(tr("Enable verbose debug logging"), grp);
        dbg->setChecked(prefs_->debugLogging());
        dbg->setToolTip(tr("Capture detailed Debug/Info messages (not just "
                           "warnings/errors). Leave off for normal use; turn "
                           "on to reproduce and capture a problem."));
        connect(dbg, &QCheckBox::toggled, grp,
                [this](bool on) { prefs_->setDebugLogging(on); });
        connect(prefs_, &Prefs::debugLoggingChanged, dbg, [this, dbg]() {
            if (dbg->isChecked() != prefs_->debugLogging())
                dbg->setChecked(prefs_->debugLogging());
        });
        v->addWidget(dbg);

        auto *viewRow = new QHBoxLayout;
        auto *viewBtn = new QPushButton(tr("View Log…"), grp);
        viewBtn->setToolTip(tr("Open the diagnostic log — Copy or Save it to "
                              "send to us if something misbehaves."));
        connect(viewBtn, &QPushButton::clicked, grp, [this]() {
            auto *dlg = new LogDialog(prefs_, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
            dlg->raise();
        });
        viewRow->addWidget(viewBtn);
        viewRow->addStretch(1);
        v->addLayout(viewRow);

        form->addRow(grp);
    }

    // --- Startup: auto-launch companion apps when Lyra opens ---
    // Global (per-machine, QSettings "autostart/*"), independent of the
    // per-profile companion launcher.  A promoted SDRLogger+ button (detect /
    // launch / offer the download page) plus two generic browse-to-app slots.
    {
        auto *grp = new QGroupBox(tr("Startup — launch apps when Lyra opens"),
                                  page);
        auto *v = new QVBoxLayout(grp);
        QSettings s;

        // -- promoted SDRLogger+ row --
        const QString sdrExe = detectSdrLoggerPlusExe();
        const bool sdrInstalled = !sdrExe.isEmpty();
        if (sdrInstalled)   // remember the resolved path for the startup hook
            s.setValue(QStringLiteral("autostart/sdrlogger/path"), sdrExe);

        auto *sdrRow = new QHBoxLayout();
        auto *sdrBtn = new QPushButton(tr("SDRLogger+"), grp);
        sdrBtn->setCursor(Qt::PointingHandCursor);
        sdrBtn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#0c3a52; color:#7fe0ff; font-weight:bold;"
            " border:1px solid #1f6f92; border-radius:6px; padding:6px 16px;}"
            "QPushButton:hover{background:#124b69; border-color:#4fd0ff;}"));
        sdrBtn->setToolTip(sdrInstalled
            ? tr("SDRLogger+ is installed — click to launch it now.")
            : tr("SDRLogger+ isn't installed — click to open the download "
                 "page."));
        auto *sdrChk = new QCheckBox(tr("Auto-start with Lyra"), grp);
        sdrChk->setChecked(
            s.value(QStringLiteral("autostart/sdrlogger/enabled"), false)
                .toBool());
        sdrChk->setEnabled(sdrInstalled);
        auto *sdrStatus = new QLabel(sdrInstalled
            ? tr("installed — our companion logger")
            : tr("not installed — click the button to get it"), grp);
        sdrStatus->setStyleSheet(QStringLiteral("QLabel{color:#8fa6ba;}"));
        sdrRow->addWidget(sdrBtn);
        sdrRow->addWidget(sdrChk);
        sdrRow->addWidget(sdrStatus, 1);
        v->addLayout(sdrRow);

        connect(sdrChk, &QCheckBox::toggled, grp, [sdrExe](bool on) {
            QSettings s2;
            s2.setValue(QStringLiteral("autostart/sdrlogger/enabled"), on);
            if (on && !sdrExe.isEmpty())
                s2.setValue(QStringLiteral("autostart/sdrlogger/path"), sdrExe);
        });
        connect(sdrBtn, &QPushButton::clicked, grp, [this, sdrExe]() {
            if (!sdrExe.isEmpty()) {
                lyra::profile::CompanionLauncher::launchDetached(sdrExe);
            } else {
                QMessageBox::information(this, tr("SDRLogger+"),
                    tr("SDRLogger+ doesn't appear to be installed.\n\n"
                       "Opening the download page — install it, then reopen "
                       "this dialog to enable auto-start."));
                QDesktopServices::openUrl(QUrl(QStringLiteral(
                    "https://github.com/N8SDR1/SDRLoggerPlus")));
            }
        });

        // -- two generic auto-launch slots --
        auto addGenericRow = [this, grp, v](int idx) {
            const QString base =
                QStringLiteral("autostart/app%1/").arg(idx);
            QSettings sr;
            auto *row = new QHBoxLayout();
            auto *chk = new QCheckBox(grp);
            chk->setChecked(sr.value(base + "enabled", false).toBool());
            chk->setToolTip(tr("Auto-start this program when Lyra opens."));
            auto *path = new QLineEdit(grp);
            path->setPlaceholderText(
                tr("Path to a program (.exe / .bat / .cmd) to auto-start"));
            path->setText(sr.value(base + "path").toString());
            auto *browse = new QPushButton(tr("Browse…"), grp);
            row->addWidget(chk);
            row->addWidget(path, 1);
            row->addWidget(browse);
            v->addLayout(row);

            auto save = [base, chk, path]() {
                QSettings s2;
                s2.setValue(base + "enabled", chk->isChecked());
                s2.setValue(base + "path", path->text().trimmed());
            };
            connect(chk, &QCheckBox::toggled, grp, [save](bool) { save(); });
            connect(path, &QLineEdit::editingFinished, grp,
                    [save]() { save(); });
            connect(browse, &QPushButton::clicked, grp,
                    [this, path, save]() {
                const QString p = QFileDialog::getOpenFileName(
                    this, tr("Choose a program to auto-start"), path->text(),
                    tr("Programs (*.exe *.bat *.cmd);;All files (*.*)"));
                if (!p.isEmpty()) {
                    path->setText(QDir::toNativeSeparators(p));
                    save();
                }
            });
        };
        addGenericRow(1);
        addGenericRow(2);

        auto *hint = new QLabel(
            tr("Launched a few seconds after Lyra opens.  Stays on this PC — "
               "never part of an exported profile."), grp);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("QLabel{color:#7c8b97;}"));
        { QFont f = hint->font(); f.setPointSizeF(f.pointSizeF() * 0.92);
          hint->setFont(f); }
        v->addWidget(hint);

        form->addRow(grp);
    }

    // ---- Switch to the right column (see scaffolding comment at the
    // top of this function).  Everything below — Radio (HL2 discovery
    // + connect), the external-HW / BCD inline form rows, and the
    // Transmit group — lands in the right column.  The right column
    // is intentionally taller (Radio's discovery list + 7 BCD/filter
    // rows + Transmit add up); the left column gets a vertical spacer
    // at the end so its groups stay top-anchored. -----------------
    auto *leftBottomSpacer = new QWidget(leftCol);
    leftBottomSpacer->setSizePolicy(QSizePolicy::Preferred,
                                    QSizePolicy::Expanding);
    form->addRow(leftBottomSpacer);
    form = rightForm;

    // --- Radio (HL2 discovery + connect) ---
    // LAN scan + found-radios list + Open/Close.  Moved here from the
    // old central dev scaffold (Log / wire-stats banner / Found-radios):
    // discovery is a hardware/connection concern, and relocating it frees
    // the main-window real estate for actual control panels.
    if (discovery_ || stream_) {
        auto *radioBox = new QGroupBox(tr("Radio (Hermes Lite 2 / 2+)"), page);
        auto *rv = new QVBoxLayout(radioBox);

        auto *status = new QLabel(tr("Click \"Discover\" to scan the LAN."),
                                  radioBox);
        status->setWordWrap(true);
        status->setStyleSheet(QStringLiteral("QLabel{color:#8fa6ba;}"));

        auto *list = new QListWidget(radioBox);
        list->setMinimumHeight(96);
        list->setItemDelegate(new PickDelegate(list));   // clear radio-mark selection
        list->setToolTip(tr("Radios found on the LAN. Select one and click "
                            "Open — or double-click — to connect."));

        // Flag the connected radio (green + bold) so it's obvious at a glance
        // which one is live in a multi-radio list.  Re-runs on every
        // connect/disconnect (called from refresh()).  Uses item roles, NOT
        // setText, so it survives addRadio() re-rendering a row.
        auto markConnected = [this, list]() {
            const QString connIp = (stream_ && stream_->isRunning())
                                       ? stream_->targetIp() : QString();
            QFont boldFont = list->font();
            boldFont.setBold(true);
            for (int i = 0; i < list->count(); ++i) {
                QListWidgetItem *it = list->item(i);
                const bool on = !connIp.isEmpty() &&
                                it->data(Qt::UserRole).toString() == connIp;
                it->setData(Qt::ForegroundRole,
                            on ? QBrush(QColor(0x6f, 0xe0, 0x9a)) : QVariant());
                it->setData(Qt::FontRole, on ? QVariant(boldFont) : QVariant());
            }
        };

        // One-line affordance so "how do I pick a radio" is obvious.
        auto *hint = new QLabel(
            tr("Select a radio, then Open (or double-click). To switch radios, "
               "Close the current one first, then Open another."), radioBox);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("QLabel{color:#7c8b97;}"));

        // Add-or-UPDATE a radio (keyed by IP), stashing every field on the
        // item so Open can remember the full record for auto-connect.
        // Update (not skip) on a repeat IP so a unicast probe() reply can
        // replace an "Add by IP" placeholder's "manual" line with the real
        // board/gw/rx info — and a re-Discover refreshes BUSY/rx state.
        auto addRadio = [list](const QString &ip, const QString &mac,
                               const QString &board, int codeVer, int betaVer,
                               bool busy, int numRxs) {
            QListWidgetItem *it = nullptr;
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->data(Qt::UserRole).toString() == ip) {
                    it = list->item(i);
                    break;
                }
            if (!it)
                it = new QListWidgetItem(list);
            it->setText(
                tr("%1  —  %2  (gw v%3.%4, %5 rx)%6")
                    .arg(ip, board).arg(codeVer).arg(betaVer).arg(numRxs)
                    .arg(busy ? tr("  [BUSY]") : QString()));
            it->setData(Qt::UserRole,     ip);
            it->setData(Qt::UserRole + 1, mac);
            it->setData(Qt::UserRole + 2, board);
            it->setData(Qt::UserRole + 3, codeVer);
            it->setData(Qt::UserRole + 4, betaVer);
            it->setData(Qt::UserRole + 5, busy);
            it->setData(Qt::UserRole + 6, numRxs);
        };

        // Show the remembered radio on open so the operator sees what
        // auto-connect attached to.
        if (discovery_) {
            const QVariantMap r = discovery_->savedRadio();
            if (!r.value(QStringLiteral("ip")).toString().isEmpty()) {
                addRadio(r.value(QStringLiteral("ip")).toString(),
                         r.value(QStringLiteral("mac")).toString(),
                         r.value(QStringLiteral("boardName")).toString(),
                         r.value(QStringLiteral("codeVersion")).toInt(),
                         r.value(QStringLiteral("betaVersion")).toInt(),
                         r.value(QStringLiteral("busy")).toBool(),
                         r.value(QStringLiteral("numRxs")).toInt());
            }
        }

        auto *scanBtn   = new QPushButton(tr("Discover"), radioBox);
        auto *openBtn   = new QPushButton(tr("Open"), radioBox);
        auto *closeBtn  = new QPushButton(tr("Close"), radioBox);
        auto *removeBtn = new QPushButton(tr("Remove"), radioBox);
        removeBtn->setToolTip(tr("Remove the selected radio from the list and "
                                 "forget it (it won't auto-connect next "
                                 "launch). Doesn't drop an active connection."));
        auto *btnRow = new QHBoxLayout();
        btnRow->addWidget(scanBtn);
        btnRow->addWidget(openBtn);
        btnRow->addWidget(closeBtn);
        btnRow->addWidget(removeBtn);
        btnRow->addStretch(1);

        // Manual add — type an IP for a radio Discover can't reach (a
        // fixed-IP HL2, a different subnet, broadcast blocked, etc.).
        auto *manualIp = new QLineEdit(radioBox);
        manualIp->setPlaceholderText(tr("Add by IP, e.g. 192.168.1.50"));
        auto *addBtn   = new QPushButton(tr("Add"), radioBox);
        auto *manualRow = new QHBoxLayout();
        manualRow->addWidget(manualIp, 1);
        manualRow->addWidget(addBtn);

        rv->addWidget(status);
        rv->addWidget(list);
        rv->addWidget(hint);
        rv->addLayout(manualRow);
        rv->addLayout(btnRow);

        auto refresh = [this, scanBtn, openBtn, closeBtn, removeBtn,
                        list, status, markConnected]() {
            const bool running = stream_ && stream_->isRunning();
            scanBtn->setEnabled(discovery_ && !running);
            openBtn->setEnabled(stream_ && !running &&
                                list->currentItem() != nullptr);
            closeBtn->setEnabled(running);
            removeBtn->setEnabled(list->currentItem() != nullptr);
            if (running && stream_)
                status->setText(tr("Connected to %1").arg(stream_->targetIp()));
            markConnected();
        };
        refresh();
        connect(list, &QListWidget::currentRowChanged, radioBox,
                [refresh](int) { refresh(); });

        if (discovery_) {
            connect(scanBtn, &QPushButton::clicked, discovery_,
                    [this, status]() {
                        status->setText(tr("Scanning…"));
                        discovery_->scan(1.5, 2);
                    });
            connect(discovery_, &lyra::ipc::HL2Discovery::radioFound, radioBox,
                    [addRadio](QString ip, QString mac, QString board,
                               int cv, int bv, bool busy, int nr) {
                        addRadio(ip, mac, board, cv, bv, busy, nr);
                    });
            connect(discovery_, &lyra::ipc::HL2Discovery::scanFinished, radioBox,
                    [status](int count) {
                        status->setText(
                            tr("Scan complete — %1 radio(s) found.").arg(count));
                    });
        } else {
            scanBtn->setEnabled(false);
        }

        if (stream_) {
            // Open one list item — shared by the Open button AND double-click.
            // No-op while already connected (Close first to switch radios).
            auto openItem = [this](QListWidgetItem *it) {
                if (!it || stream_->isRunning()) return;
                const QString ip = it->data(Qt::UserRole).toString();
                if (discovery_) {
                    discovery_->rememberRadio(
                        ip, it->data(Qt::UserRole + 1).toString(),
                        it->data(Qt::UserRole + 2).toString(),
                        it->data(Qt::UserRole + 3).toInt(),
                        it->data(Qt::UserRole + 4).toInt(),
                        it->data(Qt::UserRole + 5).toBool(),
                        it->data(Qt::UserRole + 6).toInt());
                }
                stream_->open(ip);
            };
            connect(openBtn, &QPushButton::clicked, radioBox,
                    [openItem, list]() { openItem(list->currentItem()); });
            connect(list, &QListWidget::itemDoubleClicked, radioBox,
                    [openItem](QListWidgetItem *it) { openItem(it); });
            connect(closeBtn, &QPushButton::clicked, stream_,
                    [this]() { stream_->close(); });
            connect(stream_, &lyra::ipc::HL2Stream::runningChanged, radioBox,
                    [refresh]() { refresh(); });
        } else {
            openBtn->setEnabled(false);
            closeBtn->setEnabled(false);
        }

        // Manual add — validate an IPv4 address, append a placeholder
        // ("manual"), select it so Open is one click away, then fire a
        // directed unicast probe.  If the radio answers, the
        // radioFound→addRadio path replaces "manual" with its real
        // board/gw/rx info in place; if it doesn't, the placeholder stays
        // and Open still connects.
        auto doAdd = [this, addRadio, manualIp, list, status, refresh]() {
            const QString ip = manualIp->text().trimmed();
            QHostAddress addr;
            if (ip.isEmpty() || !addr.setAddress(ip) ||
                addr.protocol() != QAbstractSocket::IPv4Protocol) {
                status->setText(
                    tr("Enter a valid IPv4 address (e.g. 192.168.1.50)."));
                return;
            }
            addRadio(ip, QString(), tr("manual"), 0, 0, false, 0);
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->data(Qt::UserRole).toString() == ip) {
                    list->setCurrentRow(i);
                    break;
                }
            manualIp->clear();
            if (discovery_) discovery_->probe(ip);
            status->setText(
                tr("Added %1 — probing for radio info; select it and click "
                   "Open to connect.").arg(ip));
            refresh();
        };
        connect(addBtn, &QPushButton::clicked, radioBox, doAdd);
        connect(manualIp, &QLineEdit::returnPressed, radioBox, doAdd);

        // Remove — drop the selected entry from the list and forget it
        // from persistence (so it won't reappear / auto-connect next
        // launch).  Does NOT close an active connection.
        connect(removeBtn, &QPushButton::clicked, radioBox,
                [this, list, status, refresh]() {
            auto *it = list->currentItem();
            if (!it) return;
            const QString ip = it->data(Qt::UserRole).toString();
            if (discovery_) discovery_->forgetRadio(ip);
            delete list->takeItem(list->row(it));
            status->setText(tr("Removed %1.").arg(ip));
            refresh();
        });

        form->addRow(radioBox);
    }

    // #199 — filter-board enable + OC readout + OC editor moved to the
    // "Filters / BCD" tab (buildFiltersBcdTab); the Hardware tab was full.


    // --- Auto-LNA (overload-triggered front-end protection) ---
    // On sustained ADC overload the LNA backs off; when the band clears
    // it creeps gain back up, riding the overload edge.  The master
    // on/off lives on the DSP+Audio panel (Row 1 "Auto"); these are the
    // tuning knobs (mirrors the reference Ant/Filters Auto-ATT options).
    {
        auto *undo = new QCheckBox(
            tr("Creep gain back up when the band clears (Undo)"), page);
        undo->setChecked(stream_ && stream_->autoLnaUndo());
        undo->setToolTip(tr(
            "On: after backing off on overload, Auto-LNA raises the gain "
            "1 dB per hold interval once clipping stops.\n"
            "Off: Auto-LNA only ever backs the gain off and holds it there."));
        if (stream_) {
            connect(undo, &QCheckBox::toggled, stream_,
                    &lyra::ipc::HL2Stream::setAutoLnaUndo);
            connect(stream_, &lyra::ipc::HL2Stream::autoLnaChanged, undo,
                    [this, undo]() {
                        const bool on = stream_->autoLnaUndo();
                        if (undo->isChecked() != on) undo->setChecked(on);
                    });
        }
        form->addRow(tr("Auto-LNA"), undo);

        auto *hold = new QSpinBox(page);
        hold->setRange(1, 60);
        hold->setSuffix(tr(" s"));
        hold->setValue(stream_ ? stream_->autoLnaHoldSec() : 4);
        hold->setToolTip(tr(
            "How long to wait after clipping stops before creeping the gain "
            "back up — and the interval between each +1 dB step."));
        if (stream_) {
            connect(hold, qOverload<int>(&QSpinBox::valueChanged), stream_,
                    &lyra::ipc::HL2Stream::setAutoLnaHoldSec);
            connect(stream_, &lyra::ipc::HL2Stream::autoLnaChanged, hold,
                    [this, hold]() {
                        const int s = stream_->autoLnaHoldSec();
                        if (hold->value() != s) hold->setValue(s);
                    });
        }
        form->addRow(tr("Hold time"), hold);
    }

    // #199 — the entire USB-BCD block moved to the "Filters / BCD" tab
    // (buildFiltersBcdTab), alongside the OC editor.

    // --- TX safety timeout + PA enable (TX-0c-pa-debug) ---
    // The operating-time TX controls (Drive %, MOX) live on the front-
    // facing TX dock (TxPanel.qml) — operator touches them between QSOs
    // and on every key.  This Settings group keeps the deliberate-arm
    // safety controls: the timeout config (set once, leave it) and PA
    // Enable (the "RF possible" gate, deliberately gated behind a trip
    // to Settings so a stray click on the front panel can't put RF on
    // the air).  Per the §15.20 spec: 1..20 min range, 10 min default,
    // bypass for long-form modes.  All persisted via QSettings inside
    // HL2Stream's setters.
    if (stream_) {
        auto *grp = new QGroupBox(tr("Transmit"), page);
        auto *g = new QGridLayout(grp);
        g->setColumnStretch(1, 1);

        g->addWidget(new QLabel(tr("TX safety timeout (min)"), grp), 0, 0);
        auto *toSpin = new QSpinBox(grp);
        toSpin->setRange(lyra::ipc::HL2Stream::kTxTimeoutMinSec / 60,
                         lyra::ipc::HL2Stream::kTxTimeoutMaxSec / 60);
        toSpin->setValue(stream_->txTimeoutSec() / 60);
        toSpin->setFixedWidth(80);
        toSpin->setToolTip(tr(
            "Auto-clears MOX if the radio stays keyed continuously past "
            "this many minutes.  Protects against a stuck PTT, an "
            "operator falling asleep, or a software bug latching MOX. "
            "1..20 min."));
        connect(toSpin, QOverload<int>::of(&QSpinBox::valueChanged), grp,
                [this](int min) {
            if (stream_) stream_->setTxTimeoutSec(min * 60);
        });
        connect(stream_, &lyra::ipc::HL2Stream::txTimeoutSecChanged, toSpin,
                [toSpin](int sec) {
            if (toSpin->value() != sec / 60) toSpin->setValue(sec / 60);
        });
        g->addWidget(toSpin, 0, 1, Qt::AlignLeft);

        auto *bypass = new QCheckBox(tr("Bypass safety timeout"), grp);
        bypass->setChecked(stream_->txTimeoutBypass());
        bypass->setToolTip(tr(
            "Disables the auto-release entirely.  Use for long-form modes "
            "(AM ragchew, slow CW beacon).  When OFF the safety timer "
            "re-arms with a fresh full duration on every keydown."));
        connect(bypass, &QCheckBox::toggled, grp, [this](bool on) {
            if (stream_) stream_->setTxTimeoutBypass(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::txTimeoutBypassChanged,
                bypass, [bypass](bool on) {
            if (bypass->isChecked() != on) bypass->setChecked(on);
        });
        g->addWidget(bypass, 1, 0, 1, 2);

        // Help label — sets expectations for the bench checklist.
        auto *help = new QLabel(grp);
        help->setText(tr(
            "<b>Safety:</b> the timeout clears MOX through the normal "
            "FSM keyup chain (ATT-on-TX restores, T/R relay drops, log "
            "line emitted).  Bypass + a stuck PTT = radio keyed until "
            "you intervene — only set Bypass when you know you need it."));
        help->setWordWrap(true);
        help->setStyleSheet(QStringLiteral("QLabel{color:#8fa6ba;}"));
        g->addWidget(help, 2, 0, 1, 2);

        // TX Drive % + MOX moved to the front-facing TX dock
        // (TxPanel.qml) — operator-tunable / per-key controls belong
        // on a real panel, not buried in a Settings dialog.  PA Enable
        // stays here as the deliberate-arm safety gate (a Settings trip
        // is the friction that keeps a stray front-panel click from
        // putting RF on the air).

        // --- PA enable (the first-RF gate) ---
        // PA enable is a PERSISTENT operator preference (2026-05-29
        // posture relax — was previously force-cleared on every stream
        // open/close as defense-in-depth, but the bench-validated
        // safety timer + gateware watchdog cover the failure modes
        // that motivated that, and re-checking on every restart was
        // operator-flagged hassle).  The checkbox reflects the persisted
        // tx/paEnabled QSettings value; open() emits paEnabledChanged
        // with the actual restored value so the UI stays in sync.
        auto *paBox = new QCheckBox(tr("Enable PA (puts RF on the antenna)"), grp);
        paBox->setChecked(stream_->paEnabled());
        paBox->setToolTip(tr(
            "Sets the gateware PA-enable bit (C2 bit 3 of slot 10).  "
            "When checked AND MOX is keyed, the HL2 PA bias engages — "
            "PA current rises from ~0 to your idle-bias value (~0.2 A "
            "on a typical HL2+).  Combined with TX Drive > 0 % this "
            "puts real RF on the antenna.  Bench safety: use a dummy "
            "load + watt-meter for first key.  PERSISTENT — your last "
            "explicit click is remembered across Lyra launches AND "
            "across stream Stop/Start cycles within a session.  PA "
            "bias alone (without MOX) produces no carrier; the safety "
            "timer + gateware watchdog catch the held-MOX failure "
            "modes if anything goes wrong."));
        connect(paBox, &QCheckBox::toggled, grp, [this](bool on) {
            if (stream_) stream_->setPaEnabled(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::paEnabledChanged, paBox,
                [paBox](bool on) {
            // Mirror the live PA-enable atomic into the UI checkbox.
            // QSignalBlocker prevents the programmatic setChecked from
            // round-tripping through the toggled handler — defensive
            // coding so any future code path that emits paEnabledChanged
            // (open() does, at the actual persisted value) doesn't
            // re-write QSettings with the same value via the toggled
            // → setPaEnabled chain.  After the 2026-05-29 PA persistence
            // posture, the open()/close() defensive clears that
            // PREVIOUSLY emitted paEnabledChanged(false) regardless of
            // operator intent are gone; the blocker now serves only as
            // belt-and-suspenders against future round-trips.
            if (paBox->isChecked() != on) {
                QSignalBlocker b(paBox);
                paBox->setChecked(on);
            }
        });
        g->addWidget(paBox, 3, 0, 1, 2);

        // Red warning label below the PA checkbox — sets the bench
        // expectation explicitly so an operator who just enables it
        // without reading the tooltip still sees the safety guidance.
        auto *paWarn = new QLabel(grp);
        paWarn->setText(tr(
            "<b style='color:#d11515;'>⚠ RF SAFETY:</b>  This puts the "
            "radio's PA on the air on the next key.  USE A DUMMY LOAD "
            "AND WATT-METER for the first session.  Phase-3-EXIT "
            "kill-test before any antenna: while keyed, taskkill /F "
            "lyra.exe and verify PA-current drops within a few seconds "
            "(HL2 gateware watchdog)."));
        paWarn->setWordWrap(true);
        paWarn->setStyleSheet(QStringLiteral("QLabel{color:#cccccc;}"));
        g->addWidget(paWarn, 4, 0, 1, 2);

        // --- Auto-mute RX while transmitting (task #26) ---
        // Default ON.  When the wire MOX bit settles true (post TR-delay),
        // the WdspEngine drops RX audio to silence so the operator
        // doesn't self-deafen off TX coupling — automatically restores
        // when MOX clears.  Operator can turn this off for ESSB self-
        // monitoring or any "listen to my own TX" workflow.
        if (engine_) {
            auto *amBox = new QCheckBox(
                tr("Auto-mute RX audio while transmitting"), grp);
            amBox->setChecked(engine_->autoMuteOnTx());
            amBox->setToolTip(tr(
                "When ON, RX1 audio is silenced for the duration of every "
                "wire-MOX-active window (post TR-delay → post ptt_out_delay), "
                "so you don't hear your own TX coupling back through the "
                "receiver.  Restores to your set Volume the instant MOX "
                "clears.  Turn OFF if you want to monitor your own TX audio "
                "(ESSB rig-pinging, sidetone evaluation, etc.).  This is a "
                "listening convenience, not a safety gate — TX-att-on-TX "
                "and the safety timeout above are the real RF safeties."));
            connect(amBox, &QCheckBox::toggled, grp, [this](bool on) {
                if (engine_) engine_->setAutoMuteOnTx(on);
            });
            connect(engine_, &lyra::dsp::WdspEngine::autoMuteOnTxChanged,
                    amBox, [this, amBox]() {
                const bool on = engine_->autoMuteOnTx();
                if (amBox->isChecked() != on) amBox->setChecked(on);
            });
            g->addWidget(amBox, 5, 0, 1, 2);
        }

        // --- RX-on-unkey delay (queued thud/echo fix) ---
        // After the keyup MOX-off edge, hold RX audio muted this many ms
        // so the TX-coupled tail still in the running RX DSP pipeline
        // drains out as silence + the T/R settles before audio resumes —
        // kills the unkey "thud" + the quick echo of your own TX.  Sits
        // under auto-mute since both shape RX audio around the TX window.
        if (engine_) {
            auto *rxdWrap = new QWidget(grp);
            auto *rxdRow  = new QHBoxLayout(rxdWrap);
            rxdRow->setContentsMargins(0, 0, 0, 0);
            auto *rxdLbl  = new QLabel(tr("RX resume delay after unkey"), rxdWrap);
            auto *rxdSpin = new QSpinBox(rxdWrap);
            rxdSpin->setRange(0, 500);
            rxdSpin->setSingleStep(10);
            rxdSpin->setSuffix(tr(" ms"));
            rxdSpin->setValue(engine_->rxResumeDelayMs());
            const QString rxdTip = tr(
                "How long RX audio stays muted AFTER you unkey, so the "
                "transmit tail still in the receiver's DSP pipeline drains "
                "out as silence and the T/R relay settles before you hear "
                "RX again.  Removes the \"thud\" and the quick echo of your "
                "own voice on unkey.\n\n"
                "Raise it if you still hear a bump/echo; lower it for "
                "snappier break-in.  0 = resume instantly (old behaviour). "
                "Default 50 ms.");
            rxdLbl->setToolTip(rxdTip);
            rxdSpin->setToolTip(rxdTip);
            rxdRow->addWidget(rxdLbl);
            rxdRow->addStretch(1);
            rxdRow->addWidget(rxdSpin);
            connect(rxdSpin, qOverload<int>(&QSpinBox::valueChanged), grp,
                    [this](int v) { if (engine_) engine_->setRxResumeDelayMs(v); });
            connect(engine_, &lyra::dsp::WdspEngine::rxResumeDelayMsChanged,
                    rxdSpin, [this, rxdSpin]() {
                const int v = engine_->rxResumeDelayMs();
                if (rxdSpin->value() != v) {
                    QSignalBlocker b(rxdSpin);
                    rxdSpin->setValue(v);
                }
            });
            g->addWidget(rxdWrap, 6, 0, 1, 2);
        }

        // --- Task #36: Hardware PTT input forwarder (default OFF) ---
        // Foot switch / hand-mic PTT / mic-button keying.  The HL2's
        // hardware PTT pin state is decoded from every EP6 status frame
        // (C0 bit 0); when this opt-in is checked, an edge dispatches
        // through requestMox() exactly like the on-screen MOX button.
        //
        // SAFETY: this is DEFAULT-OFF per the project's §10 Q#1 finding
        // — N8SDR's HL2+/AK4951 unit empirically reads a non-zero
        // ptt_in at RX rest, so an always-on forwarder would fire a
        // spurious press → phantom-TX surge.  Other HL2 revs may be
        // clean; operator MUST bench-verify on their specific unit
        // BEFORE enabling.  Per-unit: hook a scope (or read the live
        // banner under LYRA_TX_DEBUG) and confirm ptt_in is a stable
        // 0 with NO foot-switch plugged in, NO mic on, before checking
        // this box.  If a real foot-switch is wired in and the unit
        // reads dirty at rest, this defect lives upstream of Lyra
        // (HL2 board / connector / wiring) and Lyra's forwarder can't
        // safely paper over it.
        if (prefs_) {
            auto *hwBox = new QCheckBox(
                tr("Enable hardware PTT input (foot switch / hand mic)"), grp);
            hwBox->setChecked(prefs_->hwPttEnabled());
            hwBox->setToolTip(tr(
                "Forwards the HL2's hardware PTT-input pin (EP6 C0 bit 0) "
                "to MOX, so a foot switch or hand-mic PTT keys the rig.\n\n"
                "⚠ DEFAULT-OFF SAFETY:  some HL2 units (and most known "
                "HL2+/AK4951 units) read a non-zero ptt_in at RX rest, so "
                "enabling this WITHOUT bench-verifying clean rest behaviour "
                "produces spurious MOX → phantom TX (loud-surge symptom). "
                "Verify your specific unit reads stable 0 at rest with NO "
                "foot-switch plugged in before enabling.\n\n"
                "If your foot-switch press isn't keying after enabling, the "
                "wire-side ptt_in needs scope verification (HL2 connector / "
                "switch / cable) — Lyra can't safely paper over a dirty "
                "level at rest."));
            connect(hwBox, &QCheckBox::toggled, grp, [this](bool on) {
                if (prefs_) prefs_->setHwPttEnabled(on);
            });
            connect(prefs_, &lyra::ui::Prefs::hwPttEnabledChanged,
                    hwBox, [this, hwBox]() {
                const bool on = prefs_->hwPttEnabled();
                if (hwBox->isChecked() != on) {
                    QSignalBlocker b(hwBox);
                    hwBox->setChecked(on);
                }
            });
            g->addWidget(hwBox, 7, 0, 1, 2);
        }

        // --- Task #157: Space-bar PTT enable/disable ---
        // The space bar keys MOX (push-to-talk) when the main window has
        // focus and no text field is being edited.  Convenient, but easy
        // to fire by accident (operator-flagged: kept hitting space and
        // triggering TX).  This opt-out turns the space-bar PTT off
        // WITHOUT touching the on-screen MOX button, a hardware foot
        // switch, or TCI/CAT keying.  Default ON (the historical
        // behaviour) — sits right under the HW-PTT tickbox so all the
        // PTT-source options live together.
        if (prefs_) {
            auto *sbBox = new QCheckBox(
                tr("Space bar keys PTT (push-to-talk)"), grp);
            sbBox->setChecked(prefs_->spaceBarPttEnabled());
            sbBox->setToolTip(tr(
                "When ON, holding the space bar transmits and releasing "
                "it returns to receive — as long as the main window has "
                "focus and you're not typing in a text field.\n\n"
                "Untick if you keep keying TX by accidentally pressing "
                "space.  The on-screen MOX button, a hardware foot "
                "switch / hand-mic PTT, and TCI/CAT keying are all "
                "unaffected."));
            connect(sbBox, &QCheckBox::toggled, grp, [this](bool on) {
                if (prefs_) prefs_->setSpaceBarPttEnabled(on);
            });
            connect(prefs_, &lyra::ui::Prefs::spaceBarPttEnabledChanged,
                    sbBox, [this, sbBox]() {
                const bool on = prefs_->spaceBarPttEnabled();
                if (sbBox->isChecked() != on) {
                    QSignalBlocker b(sbBox);
                    sbBox->setChecked(on);
                }
            });
            g->addWidget(sbBox, 8, 0, 1, 2);
        }

        // --- Auto-start on launch (opt-out) ---
        // When ON (the historical behaviour) Lyra auto-connects to the
        // last radio at startup so the operator doesn't have to Discover
        // every launch.  Operators who'd rather open Lyra and click Start
        // themselves untick it; Lyra then loads but stays disconnected
        // until an explicit Start.
        if (prefs_) {
            auto *asBox = new QCheckBox(
                tr("Auto-start radio on launch"), grp);
            asBox->setChecked(prefs_->autoStartOnLaunch());
            asBox->setToolTip(tr(
                "When ON, Lyra automatically connects to the last radio "
                "as soon as it starts.\n\n"
                "Untick to have Lyra load without connecting — you then "
                "click Start when you're ready to go on the air."));
            connect(asBox, &QCheckBox::toggled, grp, [this](bool on) {
                if (prefs_) prefs_->setAutoStartOnLaunch(on);
            });
            connect(prefs_, &lyra::ui::Prefs::autoStartOnLaunchChanged,
                    asBox, [this, asBox]() {
                const bool on = prefs_->autoStartOnLaunch();
                if (asBox->isChecked() != on) {
                    QSignalBlocker b(asBox);
                    asBox->setChecked(on);
                }
            });
            g->addWidget(asBox, 9, 0, 1, 2);
        }

        // Mic source picker + Mic Boost checkbox MOVED to the TX
        // tab's "Mic + ALC" group (signal-flow order: source →
        // HW boost → SW gain → ALC).  Hardware tab now carries
        // only true wire/safety controls (HW PTT input, etc.).

        form->addRow(grp);
    }

    return page;
}

QWidget *SettingsDialog::buildFiltersBcdTab() {
    // #199 — external band-following: the editable OC (J16 open-collector)
    // table that drives an external filter board per band (Lyra-native port
    // of Thetis's OC Control) + the USB-BCD amp band-code output.  The OC
    // section edits HL2Stream's OcControl and calls applyOcEdit() (persist
    // oc/* + re-emit the current band).
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(12);

    lyra::oc::OcControl *oc = stream_ ? stream_->ocControl() : nullptr;

    auto section = [](const QString &title) {
        auto *g = new QGroupBox(title);
        g->setStyleSheet(QStringLiteral(
            "QGroupBox{font-weight:bold;color:#cdd9e5;border:1px solid #2a3f4d;"
            "border-radius:8px;margin-top:14px;background:rgba(20,32,40,0.35);}"
            "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 6px;"
            "color:#4fc3f7;}"));
        return g;
    };
    auto colHdr = [](const QString &t, const QString &color) {
        auto *l = new QLabel(t);
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet(
            QStringLiteral("QLabel{color:%1;font-weight:bold;}").arg(color));
        return l;
    };

    // Build the USB-BCD group up front so it can sit either in the right
    // column beside the transmit-pin actions (when the OC editor is present)
    // or full-width (BCD-only, no stream).
    QGroupBox *bcdBox = nullptr;
    if (bcd_) {
        bcdBox = section(tr("USB-BCD amp band output"));
        auto *bf = new QFormLayout(bcdBox);
        if (!bcd_->available()) {
            auto *na = new QLabel(tr("FTDI driver (ftd2xx.dll) not found — "
                "install the FTDI D2XX driver to use USB-BCD."));
            na->setWordWrap(true);
            na->setStyleSheet(QStringLiteral("QLabel{color:#8a9aac;}"));
            bf->addRow(na);
        } else {
            auto *en = new QCheckBox(tr("Enable USB-BCD amp band output"));
            en->setChecked(bcd_->enabled());
            en->setToolTip(tr("Outputs the Yaesu BCD band code on an FTDI cable "
                              "so a linear amp follows the band.\n⚠ Verify "
                              "wiring + low-power test per band before keying "
                              "at full power."));
            connect(en, &QCheckBox::toggled, bcd_, &UsbBcd::setEnabled);
            connect(bcd_, &UsbBcd::enabledChanged, en, [en](bool on) {
                if (en->isChecked() != on) en->setChecked(on);
            });
            bf->addRow(tr("USB-BCD"), en);

            auto *dev = new QComboBox;
            dev->setMaximumWidth(180);
            dev->addItem(tr("(none)"), QString());
            for (const QString &s : bcd_->devices()) dev->addItem(s, s);
            int di = dev->findData(bcd_->serial());
            dev->setCurrentIndex(di >= 0 ? di : 0);
            connect(dev, &QComboBox::currentIndexChanged, bcd_, [this, dev](int) {
                bcd_->setSerial(dev->currentData().toString());
            });
            bf->addRow(tr("BCD cable"), dev);

            auto *sixty = new QCheckBox(tr("60 m uses the 40 m filter (BCD 3)"));
            sixty->setChecked(bcd_->sixtyAsForty());
            sixty->setToolTip(tr("60 m has no standard BCD code. On = use the "
                                 "40 m code; off = bypass."));
            connect(sixty, &QCheckBox::toggled, bcd_, &UsbBcd::setSixtyAsForty);
            connect(bcd_, &UsbBcd::sixtyAsFortyChanged, sixty, [sixty](bool on) {
                if (sixty->isChecked() != on) sixty->setChecked(on);
            });
            bf->addRow(QString(), sixty);

            auto *eleven = new QCheckBox(tr("11 m uses the 10 m filter (BCD 9)"));
            eleven->setChecked(bcd_->elevenAsTen());
            eleven->setToolTip(tr("11 m / CB has no standard BCD code. On = use "
                                  "the 10 m code; off = bypass."));
            connect(eleven, &QCheckBox::toggled, bcd_, &UsbBcd::setElevenAsTen);
            connect(bcd_, &UsbBcd::elevenAsTenChanged, eleven, [eleven](bool on) {
                if (eleven->isChecked() != on) eleven->setChecked(on);
            });
            bf->addRow(QString(), eleven);

            auto *regNote = new QLabel(tr("⚠ Operate only within the maximum "
                "power and band limits permitted by your country / region's "
                "regulations."));
            regNote->setWordWrap(true);
            regNote->setStyleSheet(QStringLiteral("QLabel{color:#c9a23a;}"));
            bf->addRow(regNote);

            auto *code = new QLabel;
            auto setCode = [code](int c) {
                code->setText(tr("BCD code: %1").arg(c));
            };
            setCode(bcd_->currentBcd());
            code->setStyleSheet(QStringLiteral(
                "QLabel{color:#8fa6ba;font-family:Consolas;}"));
            connect(bcd_, &UsbBcd::currentBcdChanged, code, setCode);
            bf->addRow(tr("BCD output"), code);
        }
    }

    if (oc) {
        auto *ocBox = section(tr("Filter board — OC Control (J16 pins)"));
        auto *ocv = new QVBoxLayout(ocBox);
        ocv->setSpacing(10);

        // --- master enable + live hardware pin-state strip ---
        auto *topRow = new QHBoxLayout;
        auto *fb = new QCheckBox(
            tr("Enable external filter board (N2ADR / compatible)"));
        fb->setChecked(stream_->filterBoardEnabled());
        fb->setToolTip(tr("Switches the HL2 open-collector outputs per band to "
                          "drive an external band-pass filter board.\n"
                          "Off = OC pins idle (harmless with no board)."));
        connect(fb, &QCheckBox::toggled, stream_,
                &lyra::ipc::HL2Stream::setFilterBoardEnabled);
        connect(stream_, &lyra::ipc::HL2Stream::filterBoardChanged, fb,
                [fb](bool on) { if (fb->isChecked() != on) fb->setChecked(on); });
        topRow->addWidget(fb);
        topRow->addStretch(1);
        topRow->addWidget(new QLabel(tr("Live pins:")));
        QVector<QLabel *> pinCells;
        for (int p = 1; p <= 7; ++p) {
            auto *cell = new QLabel(QString::number(p));
            cell->setAlignment(Qt::AlignCenter);
            cell->setFixedSize(22, 22);
            pinCells.append(cell);
            topRow->addWidget(cell);
        }
        auto refreshPins = [pinCells](int bits) {
            for (int p = 0; p < 7; ++p) {
                const bool on = (bits >> p) & 1;
                pinCells[p]->setStyleSheet(
                    on ? QStringLiteral("QLabel{background:#9acd32;color:#0b1116;"
                                        "border-radius:4px;font-weight:bold;}")
                       : QStringLiteral("QLabel{background:#26333c;color:#5b6b78;"
                                        "border-radius:4px;}"));
            }
        };
        refreshPins(stream_->ocBits());
        connect(stream_, &lyra::ipc::HL2Stream::ocBitsChanged, page, refreshPins);
        ocv->addLayout(topRow);

        // --- per-band editable grid (HF group v1) ---
        const auto &bands = lyra::amateurBands();
        const int nB = int(bands.size());
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(4);
        grid->setVerticalSpacing(3);
        grid->addWidget(colHdr(tr("Band"), QStringLiteral("#cdd9e5")),
                        0, 0, 2, 1);
        grid->addWidget(colHdr(tr("J16 Receive pins"), QStringLiteral("#7fd4a0")),
                        0, 1, 1, 7, Qt::AlignCenter);
        grid->addWidget(colHdr(tr("J16 Transmit pins"), QStringLiteral("#e08a8a")),
                        0, 9, 1, 7, Qt::AlignCenter);
        for (int p = 1; p <= 7; ++p) {
            grid->addWidget(colHdr(QString::number(p), QStringLiteral("#7fd4a0")),
                            1, p);
            grid->addWidget(colHdr(QString::number(p), QStringLiteral("#e08a8a")),
                            1, 8 + p);
        }
        // Bring the RX and TX halves closer + a thin divider between them.
        grid->setColumnMinimumWidth(8, 9);
        auto *gdiv = new QFrame;
        gdiv->setFrameShape(QFrame::VLine);
        gdiv->setStyleSheet(QStringLiteral("QFrame{color:#5a7c94;}"));
        grid->addWidget(gdiv, 0, 8, 2 + nB, 1);

        QVector<QVector<QCheckBox *>> rxCb(nB), txCb(nB);
        for (int b = 0; b < nB; ++b) {
            auto *bl = new QLabel(QString::fromLatin1(bands[b].name));
            bl->setStyleSheet(QStringLiteral("QLabel{color:#cdd9e5;}"));
            grid->addWidget(bl, 2 + b, 0);
            for (int p = 1; p <= 7; ++p) {
                auto *rx = new QCheckBox;
                auto *tx = new QCheckBox;
                rxCb[b].append(rx);
                txCb[b].append(tx);
                grid->addWidget(rx, 2 + b, p, Qt::AlignCenter);
                grid->addWidget(tx, 2 + b, 8 + p, Qt::AlignCenter);
                const int bit = 1 << (p - 1);
                connect(rx, &QCheckBox::toggled, page,
                        [this, oc, b, bit](bool on) {
                            quint8 m = oc->rxMaskA(b);
                            if (on) m |= bit; else m &= ~bit;
                            oc->setRxMaskA(b, m);
                            stream_->applyOcEdit();
                        });
                connect(tx, &QCheckBox::toggled, page,
                        [this, oc, b, bit](bool on) {
                            quint8 m = oc->txMaskA(b);
                            if (on) m |= bit; else m &= ~bit;
                            oc->setTxMaskA(b, m);
                            stream_->applyOcEdit();
                        });
            }
        }
        auto loadGrid = [oc, rxCb, txCb, nB]() {
            for (int b = 0; b < nB; ++b)
                for (int p = 0; p < 7; ++p) {
                    const int bit = 1 << p;
                    QSignalBlocker br(rxCb[b][p]);
                    QSignalBlocker bt(txCb[b][p]);
                    rxCb[b][p]->setChecked(oc->rxMaskA(b) & bit);
                    txCb[b][p]->setChecked(oc->txMaskA(b) & bit);
                }
        };
        loadGrid();
        ocv->addLayout(grid);

        // --- preset buttons + hot-switch ---
        auto *btnRow = new QHBoxLayout;
        auto *n2 = new QPushButton(tr("N2ADR preset"));
        n2->setToolTip(tr("Load the standard N2ADR filter-board pin map."));
        connect(n2, &QPushButton::clicked, page, [this, oc, loadGrid]() {
            const int n = oc->nBands();
            for (int b = 0; b < n; ++b) {
                oc->setRxMaskA(b, quint8(lyra::n2adrOcPattern(b, false)));
                oc->setTxMaskA(b, quint8(lyra::n2adrOcPattern(b, true)));
            }
            loadGrid();
            stream_->applyOcEdit();
        });
        auto *clr = new QPushButton(tr("Clear all"));
        connect(clr, &QPushButton::clicked, page, [this, oc, loadGrid]() {
            const int n = oc->nBands();
            for (int b = 0; b < n; ++b) {
                oc->setRxMaskA(b, 0);
                oc->setTxMaskA(b, 0);
            }
            loadGrid();
            stream_->applyOcEdit();
        });
        btnRow->addWidget(n2);
        btnRow->addWidget(clr);
        btnRow->addStretch(1);
        auto *hot = new QCheckBox(tr("Allow hot switching"));
        hot->setChecked(oc->allowHotSwitch());
        hot->setToolTip(tr("Allow TX-pin edits while transmitting. Off is safer "
                           "(edits apply on the next key)."));
        connect(hot, &QCheckBox::toggled, page, [this, oc](bool on) {
            oc->setAllowHotSwitch(on);
            stream_->applyOcEdit();
        });
        btnRow->addWidget(hot);
        ocv->addLayout(btnRow);

        // ocBox (enable + live pins + grid + preset buttons) is complete.
        root->addWidget(ocBox);

        // --- HL2 Band-Voltage output (MI0BOT / Ramdor gateware) -----------
        // Separate from the OC/J16 pins: this repurposes the fan-PWM pin to
        // emit a per-band analog voltage (the gateware "band volts" feature,
        // enabled by the C0=0x00 dither bit).  Used by amps / tuners / ant
        // switches that band-follow off a band voltage.  Thetis exposes the
        // identical bit as its "HL2 Band Volts" checkbox.
        {
            auto *bvBox = section(tr("Band-voltage output (fan-PWM pin)"));
            auto *bvv = new QVBoxLayout(bvBox);
            auto *bv = new QCheckBox(
                tr("Output per-band analog voltage on the fan-PWM pin"));
            bv->setChecked(stream_->bandVoltsOutput());
            bv->setToolTip(tr(
                "HL2 gateware \"band volts\" feature (MI0BOT / Ramdor builds).\n"
                "Drives a band-dependent voltage on the fan-PWM pin for amps,\n"
                "tuners, or antenna switches that band-follow off a band voltage\n"
                "(e.g. a HardRock-50 set to Transceiver: None).\n\n"
                "TRADE-OFF: while on, that pin outputs band voltage INSTEAD of\n"
                "fan speed control — leave OFF unless your wiring uses it."));
            connect(bv, &QCheckBox::toggled, stream_,
                    &lyra::ipc::HL2Stream::setBandVoltsOutput);
            connect(stream_, &lyra::ipc::HL2Stream::bandVoltsOutputChanged, bv,
                    [bv](bool on) { if (bv->isChecked() != on) bv->setChecked(on); });
            bvv->addWidget(bv);
            root->addWidget(bvBox);
        }

        // --- lower area: two columns — transmit pin action (left) | BCD (right) ---
        auto *lower = new QHBoxLayout;
        lower->setSpacing(12);

        // LEFT: transmit pin action + external-PA gating (narrowed combos,
        // PA·RX / PA·TX pulled in beside them).
        auto *paBox = section(tr("Transmit pin action + external-PA gating"));
        auto *pav = new QVBoxLayout(paBox);
        auto *paGrid = new QGridLayout;
        paGrid->setHorizontalSpacing(6);
        paGrid->addWidget(colHdr(tr("Pin"), QStringLiteral("#cdd9e5")), 0, 0);
        paGrid->addWidget(colHdr(tr("Transmit action"), QStringLiteral("#cdd9e5")),
                          0, 1);
        paGrid->addWidget(colHdr(tr("PA·RX"), QStringLiteral("#cdd9e5")), 0, 2);
        paGrid->addWidget(colHdr(tr("PA·TX"), QStringLiteral("#cdd9e5")), 0, 3);
        const QStringList actLabels = {
            tr("Mox"),          tr("Tune"),          tr("2-Tone"),
            tr("Mox / Tune"),   tr("Mox / 2-Tone"),  tr("Tune / 2-Tone"),
            tr("Mox / Tune / 2-Tone")};
        for (int p = 1; p <= 7; ++p) {
            paGrid->addWidget(new QLabel(QString::number(p)), p, 0);
            auto *combo = new QComboBox;
            combo->addItems(actLabels);
            combo->setMaximumWidth(180);   // narrow — free space for the BCD column
            combo->setCurrentIndex(int(oc->txPinAction(0, p - 1)));
            connect(combo, &QComboBox::currentIndexChanged, page,
                    [this, oc, p](int idx) {
                        oc->setTxPinAction(
                            0, p, static_cast<lyra::oc::TxPinAction>(idx));
                        stream_->applyOcEdit();
                    });
            paGrid->addWidget(combo, p, 1);
            auto *paRx = new QCheckBox;
            paRx->setChecked(oc->rxPinPa(0, p - 1));
            connect(paRx, &QCheckBox::toggled, page, [this, oc, p](bool on) {
                oc->setRxPinPa(0, p, on);
                stream_->applyOcEdit();
            });
            paGrid->addWidget(paRx, p, 2, Qt::AlignCenter);
            auto *paTx = new QCheckBox;
            paTx->setChecked(oc->txPinPa(0, p - 1));
            connect(paTx, &QCheckBox::toggled, page, [this, oc, p](bool on) {
                oc->setTxPinPa(0, p, on);
                stream_->applyOcEdit();
            });
            paGrid->addWidget(paTx, p, 3, Qt::AlignCenter);
        }
        paGrid->setColumnStretch(4, 1);   // absorb slack right of PA·TX
        pav->addLayout(paGrid);
        pav->addStretch(1);
        lower->addWidget(paBox, 0);

        // RIGHT: USB-BCD (moved up into the space freed by the narrow combos).
        if (bcdBox) {
            auto *rightCol = new QVBoxLayout;
            rightCol->addWidget(bcdBox);
            rightCol->addStretch(1);
            lower->addLayout(rightCol, 1);
        } else {
            lower->addStretch(1);
        }
        root->addLayout(lower);

        // --- full-width beneath the two columns ---
        auto *warn = new QLabel(tr(
            "<b style='color:#d11515;'>⚠ Pre-antenna gate:</b>  verify the "
            "predicted RX / TX pins match your physical board wiring at low "
            "power on every band before keying an amp.  Wrong pins = wrong "
            "filter = out-of-band emissions / blown LPF relay."));
        warn->setWordWrap(true);
        warn->setStyleSheet(QStringLiteral("QLabel{color:#cccccc;}"));
        root->addWidget(warn);

        auto *split = new QCheckBox(tr("Split pins (per-VFO) — activates with RX2"));
        split->setChecked(oc->splitPins());
        split->setEnabled(false);
        split->setToolTip(tr("Per-VFO split-pin routing; enabled when the "
                             "second receiver lands."));
        root->addWidget(split);
    } else if (bcdBox) {
        // No OC editor (BCD-only) — USB-BCD spans the tab.
        root->addWidget(bcdBox);
    }

    root->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildMeterTab() {
    // §15.28 — 3-group restructure (Meter Sources / Power Calibration /
    // Display Behaviour) wraps the previously-flat form rows in proper
    // QGroupBox containers.  The existing `form` pointer pattern lets
    // the ~440 lines of `form->addRow(...)` call sites stay byte-
    // identical — only the form layout it points to changes at section
    // transitions, same idiom the Visuals tab uses.
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);
    root->setSpacing(10);

    // First group: Meter Sources.  The very first addRow() calls go
    // here (the RX source + TX source dropdowns inside `if (meter_)`).
    auto *grpSources = new QGroupBox(tr("Meter Sources"), page);
    auto *formSources = new QFormLayout(grpSources);
    root->addWidget(grpSources);
    QFormLayout *form = formSources;

    // ── Meter source preferences (task #33 — MOX-edge auto-swap) ──
    // The Meter Panel shows ONE source at a time.  Operator picks what
    // appears at rest (rxSource) and what it auto-swaps to when the wire
    // MOX bit settles (txSource).  Swap is instant on every keydown /
    // keyup edge; per-source hold / decay / history state resets so a
    // stale RX peak doesn't render as a bogus TX peak across the edge.
    // The same dropdown list is offered for both — operators on custom
    // setups may genuinely want e.g. PA Current at rest (idle bias
    // visible) and PWR while keyed.
    if (meter_) {
        // Per-MOX-state option lists — mirrors the verified
        // reference's split:
        //
        //   RX meter modes (reference): Signal, Signal Avg, ADC L+R,
        //     ADC2, Off.  All RX-side signals; no TXA chain values
        //     (the chain isn't running at rest, so ALC/MIC/COMP would
        //     read garbage and PWR/SWR have nothing real to measure
        //     with the PTT clear).
        //
        //   TX meter modes (reference): Mic, EQ, Leveler, Lev Gain,
        //     CFC, COMP, COMP+CFC, ALC, ALC Comp, ALC Group, PWR, SWR,
        //     Reverse Power.  Full TX gain-structure chain + on-air
        //     power + match.  Operator picks one to watch during TX —
        //     typical workflow is to sweep through them while setting
        //     gain structure (mic gain to peak ~0 dBFS on MIC, then
        //     ALC pulling no more than -3 dB on voice peaks, etc.).
        //
        // Lyra mapping:
        //   * RX picker: RX S-meter only for now.  HL2 telemetry
        //     (PA Current / PA Volts / Temp) already lives on the
        //     dedicated HL2 banner chip at the top of the window —
        //     operators who want idle-bias-at-rest read it there, the
        //     main meter stays an RX-signal instrument while
        //     listening.  Vertical Ladder style still shows the
        //     telemetry rows alongside the S-meter for the
        //     multi-source-at-a-glance use case.
        //   * TX picker: PWR / SWR / ID / VDD / Temp (HL2 telemetry
        //     stays available here because those quantities are
        //     ACTIVELY changing during TX — PA bias rising, supply
        //     sagging on a hard key, board heating up under sustained
        //     TX — so they belong on the operator's TX-watch list)
        //     plus ALC / MIC / COMP gain-structure meters that
        //     read live from WDSP TXA GetTXAMeter (Task #69 wiring,
        //     2026-06-01).
        struct Opt { int v; const char *label; bool enabled; };
        static const Opt kRxOpts[] = {
            {0, "RX S-meter (signal strength)", true},
        };
        // Lyra TX dynamics-meter set (Task #71 §2; CFC + COMP
        // pruned 2026-06-02 PM — Combinator (Task #51, v0.2.1)
        // replaces both as a Lyra-native pre-processor, so its
        // meters will arrive as Lyra-native sources when #51
        // ships — not WDSP TXA indices).  Ordered top-to-bottom
        // by signal-flow stage: mic → leveler → ALC → wire.
        static const Opt kTxOpts[] = {
            // Wire / safety / telemetry (always meaningful):
            {1,  "PWR (forward power, watts)",              true},
            {2,  "SWR (antenna match)",                     true},
            {3,  "ID — PA Current (HL2 bias)",              true},
            {4,  "VDD — PA Volts (HL2 supply)",             true},
            {5,  "Temp (HL2 board)",                        true},
            // TX dynamics chain — input to output:
            {7,  "MIC (mic peak, dBFS)",                    true},
            {9,  "LEV (leveler output peak, dBFS)",         true},
            {8,  "LVL G (leveler gain, dB)",                true},
            {13, "ALC (ALC output peak, dBFS)",             true},
            {6,  "ALC G (ALC gain reduction, dB)",          true},
            {14, "ALC Σ (ALC_PK + ALC_GAIN, dBFS)",         true},
        };

        auto buildSourceCombo = [this](MeterModel *m, bool isTx) {
            auto *cb = new QComboBox(this);
            const Opt *opts = isTx ? kTxOpts : kRxOpts;
            const int  n    = isTx ? int(std::size(kTxOpts))
                                   : int(std::size(kRxOpts));
            for (int i = 0; i < n; ++i) {
                cb->addItem(tr(opts[i].label), opts[i].v);
                if (!opts[i].enabled) {
                    auto *mdl = qobject_cast<QStandardItemModel*>(cb->model());
                    if (mdl) {
                        QStandardItem *it = mdl->item(cb->count() - 1);
                        if (it) it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
                    }
                }
            }
            // If the persisted selection lives in the OTHER picker's
            // option set (legacy state from before the per-MOX split
            // — e.g. an operator who had picked PWR for the RX side),
            // fall back to the first option in this picker's list so
            // the combo doesn't render with a blank selection.
            const int sel = isTx ? m->txSource() : m->rxSource();
            int matchIdx = -1;
            for (int i = 0; i < cb->count(); ++i)
                if (cb->itemData(i).toInt() == sel) { matchIdx = i; break; }
            if (matchIdx >= 0) {
                cb->setCurrentIndex(matchIdx);
            } else if (cb->count() > 0) {
                cb->setCurrentIndex(0);
                if (m) {
                    const int v = cb->itemData(0).toInt();
                    if (isTx) m->setTxSource(v); else m->setRxSource(v);
                }
            }
            connect(cb, &QComboBox::currentIndexChanged, this,
                    [this, cb, isTx](int) {
                if (!meter_) return;
                const int v = cb->currentData().toInt();
                if (isTx) meter_->setTxSource(v);
                else      meter_->setRxSource(v);
            });
            return cb;
        };
        auto *rxCb = buildSourceCombo(meter_, /*isTx=*/false);
        rxCb->setToolTip(tr(
            "What the Meter Panel shows when the radio is NOT keyed. "
            "Default RX S-meter (signal strength).  Operators on stations "
            "that idle-bias the PA may prefer PA Current at rest so they "
            "can watch the standing draw."));
        form->addRow(tr("RX source (at rest):"), rxCb);
        auto *txCb = buildSourceCombo(meter_, /*isTx=*/true);
        txCb->setToolTip(tr(
            "What the Meter Panel auto-swaps to when the wire MOX bit "
            "settles (post TR-delay) — and back to the RX source above "
            "on keyup.  Default PWR (forward power).  Switching mid-TX "
            "takes effect on the next ~50 ms tick."));
        form->addRow(tr("TX source (on MOX):"), txCb);

        // Separate RX/TX meter STYLE (Arc/Bar/Ladder per state) — auto-swaps
        // on the MOX edge, mirroring the panadapter dB-range swap.  The
        // header Arc|Bar|Ladder buttons stay a live quick override.
        auto *sepStyle = new QCheckBox(tr("Separate RX / TX meter style"), page);
        sepStyle->setChecked(meter_->separateStyle());
        sepStyle->setToolTip(tr(
            "When on, the meter uses a different visual style on transmit "
            "than on receive (e.g. Horizon Arc on RX, Vertical Ladder for a "
            "PWR/SWR read on TX) and swaps automatically the moment you key "
            "up / unkey.  When off, one style is used for both."));
        form->addRow(sepStyle);

        auto buildStyleCombo = [page](int cur) {
            auto *cb = new QComboBox(page);
            cb->addItem(tr("Horizon Arc"),     0);
            cb->addItem(tr("Plasma Bar"),      1);
            cb->addItem(tr("Vertical Ladder"), 2);
            cb->setCurrentIndex(std::clamp(cur, 0, 2));
            return cb;
        };
        auto *rxStyleCb = buildStyleCombo(meter_->style());
        auto *txStyleCb = buildStyleCombo(meter_->txStyle());
        txStyleCb->setEnabled(meter_->separateStyle());
        rxStyleCb->setToolTip(tr("Visual style used on receive (and for both "
                                 "when 'Separate' is off)."));
        txStyleCb->setToolTip(tr("Visual style used on transmit when "
                                 "'Separate RX / TX meter style' is on."));
        form->addRow(tr("RX meter style:"), rxStyleCb);
        form->addRow(tr("TX meter style:"), txStyleCb);

        connect(sepStyle, &QCheckBox::toggled, this,
                [this, txStyleCb](bool on) {
            if (meter_) meter_->setSeparateStyle(on);
            txStyleCb->setEnabled(on);
        });
        connect(rxStyleCb, &QComboBox::currentIndexChanged, this,
                [this, rxStyleCb](int) {
            if (meter_) meter_->setStyle(rxStyleCb->currentData().toInt());
        });
        connect(txStyleCb, &QComboBox::currentIndexChanged, this,
                [this, txStyleCb](int) {
            if (meter_) meter_->setTxStyle(txStyleCb->currentData().toInt());
        });

        // §15.28 — switch to the second group (Power Calibration).
        // The next ~120 lines of operator controls (PWR rated max,
        // TX secondary readouts) live here.  Per-band watt calibration
        // lives on the PA Gain tab, not here.
        auto *grpPwrCal = new QGroupBox(tr("Power Calibration"), page);
        auto *formPwrCal = new QFormLayout(grpPwrCal);
        root->addWidget(grpPwrCal);
        form = formPwrCal;

        // ── PWR calibration (task #34) ──
        // Two knobs the operator typically sets ONCE per amp / per
        // setup:
        //   * "Rated max W" controls where the red zone starts on the
        //     meter face — set it to your expected max forward power
        //     (e.g. ~5 W for a bare HL2+ on-board PA; 100 W with a
        //     typical amp; up to 200 W for legal-limit).
        // Matching the displayed watts to an external watt-meter is done
        // per-band on the PA Gain tab ("PWR meter calibration").
        auto *ratedMax = new QDoubleSpinBox(page);
        ratedMax->setRange(0.5, 200.0);
        ratedMax->setDecimals(1);
        ratedMax->setSingleStep(0.5);
        ratedMax->setSuffix(tr(" W"));
        ratedMax->setValue(meter_->pwrRatedMaxW());
        ratedMax->setToolTip(tr(
            "Forward power at which the PWR meter's red zone starts. "
            "Set this to your expected max output — bare HL2+ on-board "
            "PA is around 5 W, typical amps are 100 W, legal-limit is "
            "up to 200 W.  The meter scale runs 0..2× this value, so "
            "rated max lands at half-scale (matching the cyan/green/"
            "amber/red palette the renderers use)."));
        connect(ratedMax, &QDoubleSpinBox::valueChanged, this,
                [this](double v) {
            if (meter_) meter_->setPwrRatedMaxW(v);
        });
        form->addRow(tr("PWR rated max:"), ratedMax);

        // TX secondary digital readout (task #36).  When a TX source
        // (PWR / SWR / ...) is the active primary, this picks a SECOND
        // source whose value is rendered as a small digital line under
        // the main needle — same slot the RX meter uses for SNR.
        // Lets the operator watch e.g. SWR at a glance while the main
        // needle shows PWR, without taking up another dock slot.
        // Hidden automatically when the secondary == current primary.
        // TX-only option set (mirrors the verified reference's TX
        // meter-mode list + HL2 telemetry that's meaningful during
        // TX).  ALC/MIC/COMP read live from WDSP TXA GetTXAMeter
        // (Task #69 wiring, 2026-06-01).
        auto *txSec = new QComboBox(this);
        const struct { int v; const char *label; bool enabled; } secOpts[] = {
            {-1, "None (hide line)",                            true},
            {1,  "PWR (forward power)",                         true},
            {2,  "SWR (antenna match)",                         true},
            {3,  "ID — PA Current (HL2 bias)",                  true},
            {4,  "VDD — PA Volts (HL2 supply)",                 true},
            {5,  "Temp (HL2 board)",                            true},
            // Lyra TX dynamics set (Task #71 §2; CFC+COMP pruned
            // 2026-06-02 PM — Combinator replaces both):
            {7,  "MIC (mic peak, dBFS)",                        true},
            {9,  "LEV (leveler output peak, dBFS)",             true},
            {8,  "LVL G (leveler gain, dB)",                    true},
            {13, "ALC (ALC output peak, dBFS)",                 true},
            {6,  "ALC G (ALC gain reduction, dB)",              true},
            {14, "ALC Σ (ALC_PK + ALC_GAIN, dBFS)",             true},
        };
        auto fillSecondaryCombo = [](QComboBox *cb,
            const decltype(secOpts) &opts) {
            for (const auto &o : opts) {
                cb->addItem(QObject::tr(o.label), o.v);
                if (!o.enabled) {
                    auto *mdl = qobject_cast<QStandardItemModel*>(cb->model());
                    if (mdl) {
                        QStandardItem *it = mdl->item(cb->count() - 1);
                        if (it) it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
                    }
                }
            }
        };
        fillSecondaryCombo(txSec, secOpts);
        {
            const int sel = meter_->txSecondary();
            for (int i = 0; i < txSec->count(); ++i)
                if (txSec->itemData(i).toInt() == sel) {
                    txSec->setCurrentIndex(i);
                    break;
                }
        }
        txSec->setToolTip(tr(
            "Extra digital readout shown under the main needle when a "
            "TX source is the primary.  Useful for watching SWR while "
            "the needle tracks PWR (or any combination).  Hidden when "
            "the selection equals the current primary."));
        connect(txSec, &QComboBox::currentIndexChanged, this,
                [this, txSec](int) {
            if (meter_) meter_->setTxSecondary(txSec->currentData().toInt());
        });
        form->addRow(tr("TX secondary readout:"), txSec);

        // Second TX digital readout (task #37) — brings TX into RX-parity
        // 3-line layout (primary big number / secondary #1 / secondary #2
        // mirrors RX's S-unit / dBm / SNR).  Same option set as #1; the
        // model hides this slot when its selection equals the primary OR
        // duplicates secondary #1, so the operator sees nothing surprising.
        auto *txSec2 = new QComboBox(this);
        fillSecondaryCombo(txSec2, secOpts);
        {
            const int sel = meter_->txSecondary2();
            for (int i = 0; i < txSec2->count(); ++i)
                if (txSec2->itemData(i).toInt() == sel) {
                    txSec2->setCurrentIndex(i);
                    break;
                }
        }
        txSec2->setToolTip(tr(
            "Third readout line (the second small line under the main "
            "needle).  Hidden when its selection equals the primary or "
            "duplicates the first secondary."));
        connect(txSec2, &QComboBox::currentIndexChanged, this,
                [this, txSec2](int) {
            if (meter_) meter_->setTxSecondary2(txSec2->currentData().toInt());
        });
        form->addRow(tr("TX secondary readout #2:"), txSec2);
        // NOTE: the old global "PWR cal scale" knob was removed here.
        // Per-band PWR-meter calibration (Settings → PA Gain → "PWR
        // meter calibration") is now the single calibration mechanism;
        // a global scale on top of it double-scaled the reading (e.g.
        // a 3.5 W cap showing ~3.9 W).  The per-band trim already
        // matches every band to your external watt-meter.
    }

    // §15.28 — switch to the third group (Display Behaviour).  S-meter
    // cal trim, peak-hold timings, PWR ballistic mode picker, max-peak
    // high-water marker, and the footer hint all live here.  Visible
    // whether `meter_` is set or not (the original code rendered these
    // rows outside the `if (meter_)` guard).
    auto *grpDisplay = new QGroupBox(tr("Display Behaviour"), page);
    auto *formDisplay = new QFormLayout(grpDisplay);
    root->addWidget(grpDisplay);
    form = formDisplay;

    // S-meter calibration trim (calDb) — applied live + persisted by the
    // MeterModel.  The meter reads WDSP RXA_S_PK; this offset lands it on
    // an absolute dBm / S-unit scale.  With RXA_S_PK the trim is small.
    auto *cal = new QDoubleSpinBox(page);
    cal->setRange(-60.0, 60.0);
    cal->setDecimals(1);
    cal->setSingleStep(0.5);
    cal->setSuffix(tr(" dB"));
    cal->setValue(meter_ ? meter_->calDb() : 0.0);
    cal->setToolTip(tr(
        "S-meter calibration trim.\n\n"
        "The meter reads WDSP's in-passband RXA_S_PK level; this offset "
        "lands it on an absolute dBm / S-unit scale. Tune to a known-level "
        "reference — WWV, or a signal "
        "generator at a known dBm — and adjust until the S-reading "
        "matches. With RXA_S_PK the needed trim is only a few dB."));
    connect(cal, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { if (meter_) meter_->setCalDb(v); });
    form->addRow(tr("S-meter calibration:"), cal);

    // Peak-hold dwell — how long the peak marker hangs at a new high
    // before it starts to fall.  Operator-tunable; applied live + persisted.
    auto *hold = new QSpinBox(page);
    hold->setRange(100, 5000);
    hold->setSingleStep(100);
    hold->setSuffix(tr(" ms"));
    hold->setValue(meter_ ? meter_->peakHoldMs() : 800);
    hold->setToolTip(tr(
        "Peak-hold time — how long the meter's peak marker hangs at a new "
        "high before decaying. Raise it to make brief peaks linger longer "
        "(easier to catch a fleeting signal / QSB crest); lower it for a "
        "snappier marker. Default 800 ms."));
    connect(hold, &QSpinBox::valueChanged, this,
            [this](int v) { if (meter_) meter_->setPeakHoldMs(v); });
    form->addRow(tr("Meter peak-hold:"), hold);

    // PWR meter ballistic — three named modes the operator picks
    // (task #57).  PEP = sliding-window MAX, fixed 500 ms hold;
    // Peak = sliding-window MAX, operator-tunable hold (the PWR
    // peak-hold spin box below); Avg = IIR smoother (the spin box
    // is irrelevant in this mode and greys out).
    auto *pwrBallistic = new QComboBox(this);
    pwrBallistic->addItem(tr("PEP — fast peak (500 ms hold)"),
                          int(MeterModel::PWR_PEP));
    pwrBallistic->addItem(tr("Peak — held peak (operator-tunable hold)"),
                          int(MeterModel::PWR_PEAK));
    pwrBallistic->addItem(tr("Avg — running average (calm needle)"),
                          int(MeterModel::PWR_AVG));
    pwrBallistic->setToolTip(tr(
        "Choose the PWR meter's inner needle ballistic.\n\n"
        "• PEP — sliding-window MAX with a fixed 500 ms hold.  Each "
        "voice burst kicks the bar up and it drops back between "
        "syllables.  Best for contest peak watching or any time you "
        "want each peak to read as a distinct event.  Ignores the "
        "PWR peak-hold spin box below.\n\n"
        "• Peak — sliding-window MAX with operator-tunable hold "
        "(default 3000 ms).  Peaks park long enough to read off "
        "the digital face at leisure; matches the typical Bird/"
        "Palstar PEAK ballistic.  General-purpose default.\n\n"
        "• Avg — IIR smoother (~200 ms time constant).  Tracks the "
        "running average of forward power, NOT peaks.  Calm needle "
        "for sustained-tone gain-structure work and ragchew.  The "
        "outside peak pip + max-hold marker still show recent peaks "
        "even in this mode."));
    {
        const int sel = meter_ ? meter_->pwrBallistic()
                               : int(MeterModel::PWR_PEAK);
        for (int i = 0; i < pwrBallistic->count(); ++i)
            if (pwrBallistic->itemData(i).toInt() == sel) {
                pwrBallistic->setCurrentIndex(i);
                break;
            }
    }
    form->addRow(tr("PWR ballistic:"), pwrBallistic);

    // PWR meter sliding-window MAX hold time — operator-tunable per
    // the 2026-05-31 PM bench feedback ("still seems SLOW to react" /
    // "noticed hang time" after the initial 500 ms -> 3 s bump).
    // Distinct from the peak-hold spin box above: that controls the
    // small peak-cap indicator's hang/decay; THIS controls the MAIN
    // needle / fill-bar hold via the MAX detector's window length.
    // Only applies in PWR_PEAK mode (greyed in PEP and Avg below).
    auto *pwrHold = new QSpinBox(page);
    pwrHold->setRange(100, 10000);
    pwrHold->setSingleStep(100);
    pwrHold->setSuffix(tr(" ms"));
    pwrHold->setValue(meter_ ? meter_->pwrPeakHoldMs() : 3000);
    pwrHold->setToolTip(tr(
        "PWR meter peak hold — how long the main needle holds a "
        "captured peak before decaying.  Sliding-window MAX detector: "
        "needle jumps to peak instantly and holds at that value for "
        "this duration before the slot wraps and the next-highest "
        "sample takes over.  Default 3000 ms (3 sec, Bird/Palstar-"
        "PEAK-style ballistic).  Lower for snappier decay; higher "
        "for analog-needle-style long park.\n\n"
        "ONLY APPLIES IN \"Peak\" BALLISTIC MODE — PEP uses a fixed "
        "500 ms hold and Avg uses an IIR smoother (this knob greys "
        "out for those two modes).\n\n"
        "Note: this does NOT change how fast the needle CLIMBS to "
        "peak — that's limited by the HL2 forward-power ADC's "
        "hardware response time (directional coupler analog "
        "integrator), which no software knob can shorten."));
    connect(pwrHold, &QSpinBox::valueChanged, this,
            [this](int v) { if (meter_) meter_->setPwrPeakHoldMs(v); });
    form->addRow(tr("PWR peak-hold:"), pwrHold);

    // Grey-out the peak-hold spin box when the active ballistic
    // doesn't use it (PEP / Avg) so the operator immediately sees
    // which knob applies to the current mode.
    auto syncHoldEnable = [pwrHold](int mode) {
        pwrHold->setEnabled(mode == int(MeterModel::PWR_PEAK));
    };
    syncHoldEnable(meter_ ? meter_->pwrBallistic()
                          : int(MeterModel::PWR_PEAK));
    connect(pwrBallistic, &QComboBox::currentIndexChanged, this,
            [this, pwrBallistic, syncHoldEnable](int) {
        const int m = pwrBallistic->currentData().toInt();
        if (meter_) meter_->setPwrBallistic(m);
        syncHoldEnable(m);
    });
    // Also follow programmatic changes (e.g. a future profile-
    // manager preset that flips the ballistic) so the spin box
    // grey-out stays in sync with the model's actual state.
    if (meter_) {
        connect(meter_, &MeterModel::pwrBallisticChanged, this,
                [this, pwrBallistic, syncHoldEnable]() {
            const int m = meter_->pwrBallistic();
            for (int i = 0; i < pwrBallistic->count(); ++i)
                if (pwrBallistic->itemData(i).toInt() == m) {
                    QSignalBlocker b(pwrBallistic);
                    pwrBallistic->setCurrentIndex(i);
                    break;
                }
            syncHoldEnable(m);
        });
    }

    // Max-hold "high-water mark" — a second, slower marker that latches
    // the highest level seen and eases down gently (distinct red marker
    // on the Arc / Bar).  Enable + its own dwell time.
    auto *maxOn = new QCheckBox(tr("Show max-peak marker"), page);
    maxOn->setChecked(meter_ ? meter_->maxPeakEnabled() : true);
    maxOn->setToolTip(tr(
        "A second meter marker (red) that latches the highest level "
        "reached and falls back gently — a 'high-water mark' that lets you "
        "read a fleeting DX or QSB crest long after the fast peak pip has "
        "dropped."));
    connect(maxOn, &QCheckBox::toggled, this,
            [this](bool on) { if (meter_) meter_->setMaxPeakEnabled(on); });
    form->addRow(QString(), maxOn);

    auto *maxHold = new QSpinBox(page);
    maxHold->setRange(1, 60);
    maxHold->setSingleStep(1);
    maxHold->setSuffix(tr(" s"));
    maxHold->setValue(meter_ ? meter_->maxHoldMs() / 1000 : 3);
    maxHold->setToolTip(tr(
        "How long the max-peak marker holds at a new high before it "
        "begins its gentle fall. Longer = the high-water mark lingers "
        "more before easing down. Default 3 s."));
    maxHold->setEnabled(maxOn->isChecked());
    connect(maxOn, &QCheckBox::toggled, maxHold, &QWidget::setEnabled);
    connect(maxHold, &QSpinBox::valueChanged, this,
            [this](int v) { if (meter_) meter_->setMaxHoldMs(v * 1000); });
    form->addRow(tr("Max-peak hold:"), maxHold);

    auto *hint = new QLabel(tr(
        "Set this once against a known signal. The meter compensates for "
        "the LNA gain automatically, so the reading stays put as you "
        "adjust LNA — you can calibrate at any setting."), page);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#8a9aac;"));
    form->addRow(hint);

    // §15.28 — let groups sit at the top, push trailing space to bottom.
    root->addStretch(1);
    return page;
}

// ─────────────────────────────────────────────────────────────────
// Settings → Tuner — manual-ATU memory editor.  Full maintenance for
// the data the front-panel Tuner panel uses: per-antenna table of
// Input/Output/Inductor points (add / edit / delete / clear), antenna
// rename, and the match window (how close a stored point counts as an
// "exact" green match).  Pure UI + the model's QSettings; RF-safe.
// ─────────────────────────────────────────────────────────────────
QWidget *SettingsDialog::buildCalibrationTab() {
    // Frequency calibration (WWV / time-station).  Stage 2 of the
    // freq_calibration_design.md build: the persisted-factor home + the
    // full recall safety net (manual entry + Reset + Restore previous).
    // The guided A+B visual instrument (Stage 4) launches from here later.
    auto *page = new QWidget(this);
    // Readable directions, matching the PA Gain tab: bright body text,
    // amber warnings, directions paragraphs a touch larger.
    page->setStyleSheet(QStringLiteral(
        "QLabel { color: rgb(205,217,229); }"
        "QLabel[lyraWarn=\"true\"] { color: rgb(242,183,73); }"
        "QLabel[calDir=\"true\"] { font-size: 10pt; }"));
    auto *root = new QVBoxLayout(page);
    root->setSpacing(10);

    auto *intro = new QLabel(tr(
        "Your radio's reference oscillator is a hair off, so a signal on a "
        "known frequency lands a few hertz from where the dial says. "
        "Frequency calibration nudges every RX and TX frequency by a tiny "
        "factor so the dial reads true. "
        "<b>1.00000000 = no correction</b> — exactly how the radio tunes "
        "today."), page);
    intro->setWordWrap(true);
    intro->setProperty("calDir", true);
    root->addWidget(intro);

    auto *grp  = new QGroupBox(tr("Frequency correction"), page);
    auto *form = new QFormLayout(grp);
    root->addWidget(grp);

    const double cur = stream_ ? stream_->freqCorrection() : 1.0;

    // ppm = (factor − 1) × 1e6.  Shown alongside the raw factor so the
    // number means something to the operator.
    auto fmtReadout = [](double f) -> QString {
        const double ppm = (f - 1.0) * 1.0e6;
        return QStringLiteral(
                   "<span style='font-size:15pt; font-weight:500;'>%1</span>"
                   "&nbsp;&nbsp;<span style='color:rgb(138,154,172);'>"
                   "%2%3 ppm</span>")
            .arg(f, 0, 'f', 8)
            .arg(ppm >= 0 ? QStringLiteral("+") : QString())
            .arg(ppm, 0, 'f', 2);
    };

    auto *readout = new QLabel(fmtReadout(cur), page);
    readout->setTextFormat(Qt::RichText);
    form->addRow(tr("Current:"), readout);

    auto *spin = new QDoubleSpinBox(page);
    spin->setDecimals(8);
    spin->setRange(0.999, 1.001);          // ±1000 ppm — see HL2Stream clamp
    spin->setSingleStep(0.0000001);        // arrow nudge = 0.1 ppm
    spin->setKeyboardTracking(false);      // apply on Enter / focus-out only
    spin->setValue(cur);
    spin->setToolTip(tr(
        "Type a factor by hand — e.g. copy the working value from another "
        "app's calibration as a known-good starting point."));
    spin->setMaximumWidth(200);   // don't stretch the whole form width
    form->addRow(tr("Set correction:"), spin);

    auto *btnRow   = new QHBoxLayout();
    auto *resetBtn = new QPushButton(tr("Reset to 1.0 (no correction)"), page);
    auto *prevBtn  = new QPushButton(tr("Restore previous"), page);
    btnRow->addWidget(resetBtn);
    btnRow->addWidget(prevBtn);
    btnRow->addStretch(1);
    form->addRow(btnRow);

    if (stream_) {
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this](double v) { stream_->setFreqCorrection(v); });
        connect(resetBtn, &QPushButton::clicked, this,
                [this] { stream_->setFreqCorrection(1.0); });
        connect(prevBtn, &QPushButton::clicked, this, [this] {
            const double prev = QSettings()
                .value(QStringLiteral("cal/freqCorrectionPrev"), 1.0).toDouble();
            stream_->setFreqCorrection(prev);
        });
        // Keep the spin + readout in sync with any source (manual entry,
        // Reset, Restore, or the visual instrument once it lands).
        connect(stream_, &lyra::ipc::HL2Stream::freqCorrectionChanged,
                spin, [spin, readout, fmtReadout](double f) {
            spin->blockSignals(true);
            spin->setValue(f);
            spin->blockSignals(false);
            readout->setText(fmtReadout(f));
        });
    } else {
        spin->setEnabled(false);
        resetBtn->setEnabled(false);
        prevBtn->setEnabled(false);
    }

    // ── Measure from a time station (Stage 3b) ──
    if (stream_ && engine_) {
        auto *mgrp  = new QGroupBox(tr("Measure from a time station (WWV / CHU)"), page);
        auto *mform = new QFormLayout(mgrp);
        root->addWidget(mgrp);

        auto *mdir = new QLabel(tr(
            "<b>Pick a time station below</b> — Lyra tunes it in USB about 1 kHz "
            "below the carrier and starts measuring automatically. Let the "
            "reading settle, then <b>Apply</b>. Press <b>Measure</b> again (or "
            "close it off) to put the dial and mode back. "
            "Only stations your region can hear are listed; for anything else, "
            "type the exact frequency by hand. "
            "<b>USB is used on every band</b> — even 2.5 / 5 MHz where you'd "
            "normally run LSB (WWV is AM, and USB keeps the sign consistent). "
            "This is the same measurement as the floating <b>Freq Cal</b> panel, "
            "without the visual dial."), page);
        mdir->setWordWrap(true);
        mdir->setProperty("calDir", true);
        mform->addRow(mdir);

        auto *mtip = new QLabel(tr(
            "Best settings: a normal SSB filter (<b>RX BW ≈ 2.4–4 kHz</b>) so the "
            "carrier sits well inside the passband — a tight CW filter can clip "
            "it. <b>Sample rate makes no difference</b>: the measurement reads the "
            "demodulated audio, not the raw spectrum."), page);
        mtip->setWordWrap(true);
        mtip->setProperty("calDir", true);
        mform->addRow(mtip);

        // One-click station picker — region-filtered like the floating Freq
        // Cal panel, so a US op sees WWV/WWVH/CHU and not RWM/BPM/HLA.  Built
        // from the same TimeStations source (a throwaway instance is cheap —
        // it just holds the prefs+stream pointers).  Picking a station tunes
        // USB ~1 kHz below the carrier and arms Measure, exactly like the
        // front panel — this tab is that flow without the A+B visual.
        auto *stationCombo = new QComboBox(page);
        stationCombo->setMaximumWidth(200);
        stationCombo->addItem(tr("— pick a station —"), QVariant());
        {
            lyra::ui::TimeStations ts(prefs_, stream_);
            const QVariantList cals = ts.calStations(
                prefs_ ? prefs_->bandPlanRegion() : QString());
            for (const QVariant &v : cals) {
                const QVariantMap m = v.toMap();
                stationCombo->addItem(m.value(QStringLiteral("label")).toString(),
                                      m.value(QStringLiteral("freqHz")));
            }
        }
        mform->addRow(tr("Time station:"), stationCombo);

        auto *stationSpin = new QDoubleSpinBox(page);
        stationSpin->setDecimals(6);
        stationSpin->setRange(0.1, 30.0);
        stationSpin->setSuffix(tr(" MHz"));
        stationSpin->setValue(
            QSettings().value(QStringLiteral("cal/stationMHz"), 10.0).toDouble());
        stationSpin->setMaximumWidth(200);
        mform->addRow(tr("or exact frequency:"), stationSpin);

        auto *measureBtn = new QPushButton(tr("Measure"), page);
        measureBtn->setCheckable(true);
        mform->addRow(measureBtn);

        // Auto-tune snapshot so we can put the dial + mode back on Stop.
        auto savedFreq = std::make_shared<quint32>(0);
        auto savedMode = std::make_shared<QString>();
        auto autoTuned = std::make_shared<bool>(false);

        auto *live = new QLabel(tr("— idle —"), page);
        live->setTextFormat(Qt::RichText);
        mform->addRow(tr("Carrier:"), live);

        auto *suggestLbl = new QLabel(tr("—"), page);
        suggestLbl->setTextFormat(Qt::RichText);
        mform->addRow(tr("Suggested:"), suggestLbl);

        auto *applyBtn = new QPushButton(tr("Apply suggested correction"), page);
        applyBtn->setEnabled(false);
        mform->addRow(applyBtn);

        auto suggested = std::make_shared<double>(1.0);
        auto valid     = std::make_shared<bool>(false);

        // Pick a station → fill the exact frequency, tune USB ~1 kHz below the
        // carrier, and arm Measure.  Freq first then mode (so the per-band mode
        // memory can't override USB — same order the front panel uses).
        connect(stationCombo, &QComboBox::activated, this,
                [this, stationCombo, stationSpin, measureBtn,
                 savedFreq, savedMode, autoTuned](int idx) {
            const QVariant data = stationCombo->itemData(idx);
            if (!data.isValid()) return;               // "— pick a station —"
            const double freqHz = data.toDouble();
            if (!*autoTuned) {                          // snapshot once
                *savedFreq = stream_->rx1FreqHz();
                *savedMode = prefs_ ? prefs_->mode() : QString();
                *autoTuned = true;
            }
            stationSpin->setValue(freqHz / 1.0e6);
            stream_->setRx1FreqHz(static_cast<quint32>(freqHz - 1000.0));
            if (prefs_) prefs_->setMode(QStringLiteral("USB"));
            measureBtn->setChecked(true);               // arm measuring
        });

        connect(measureBtn, &QPushButton::toggled, this,
                [this, stationSpin, stationCombo, savedFreq, savedMode, autoTuned]
                (bool on) {
            QSettings().setValue(QStringLiteral("cal/stationMHz"), stationSpin->value());
            engine_->setFreqCalMeasuring(on);
            if (!on && *autoTuned) {                    // Stop → put the dial back
                stream_->setRx1FreqHz(*savedFreq);
                if (prefs_ && !savedMode->isEmpty()) prefs_->setMode(*savedMode);
                *autoTuned = false;
                stationCombo->setCurrentIndex(0);
            }
        });

        connect(engine_, &lyra::dsp::WdspEngine::freqCalUpdated, live,
                [this, stationSpin, live, suggestLbl, applyBtn, suggested, valid]
                (double measuredHz, double snrDb, int windows) {
            const double D        = static_cast<double>(stream_->rx1FreqHz());
            const double station  = stationSpin->value() * 1.0e6;
            const double expected = station - D;   // USB: carrier tone = RF − dial
            const bool   strong   = snrDb >= 10.0 && measuredHz > 50.0;
            const QString snrCol  = strong ? QStringLiteral("rgb(120,210,140)")
                                           : QStringLiteral("rgb(138,154,172)");
            live->setText(tr("<span style='font-size:14pt; font-weight:500;'>%1 Hz</span>"
                             "&nbsp;&nbsp;<span style='color:%2;'>SNR %3 dB %4</span>"
                             "&nbsp;&nbsp;<span style='color:rgb(138,154,172);'>%5 win</span>")
                .arg(measuredHz, 0, 'f', 1)
                .arg(snrCol)
                .arg(snrDb, 0, 'f', 1)
                .arg(strong ? QStringLiteral("✓") : QStringLiteral("·"))
                .arg(windows));

            // Sanity: the tone must land near where a (station − dial) carrier
            // should be, so a wrong station/dial can't produce a nonsense factor.
            const bool sane = expected > 100.0 && expected < 3000.0
                              && std::abs(measuredHz - expected) < 500.0;
            if (strong && sane && D > 1.0e6) {
                // ── SIGN GATE (Stage 3c, freq_calibration_design.md) ──
                // Magnitude is certain; the sign depends on the USB / spectral
                // convention.  Verify on live WWV: Measure -> Apply -> the tone
                // should snap toward the expected pitch.  If it moves the WRONG
                // way, flip kMeasureSign to -1.0 and rebuild.
                constexpr double kMeasureSign = +1.0;
                const double e_hw   = kMeasureSign * (station - D - measuredHz) / D;
                const double fDelta = 1.0 / (1.0 + e_hw);
                // Compose onto the correction already in effect: the measured
                // tone already reflects the active factor, so fDelta is the
                // *additional* nudge.  Multiplying makes a re-calibration from
                // any non-1.0 factor exact in one shot — no Reset-first needed.
                // (From 1.0, f == fDelta, so first-time cal is unchanged.)
                const double f = stream_->freqCorrection() * fDelta;
                *suggested = f; *valid = true;
                applyBtn->setEnabled(true);
                const double ppm = (f - 1.0) * 1.0e6;
                const double ae  = std::abs(measuredHz - expected);   // how far off
                const QString col = ae < 2.0  ? QStringLiteral("rgb(120,210,140)")   // spot on
                                  : ae < 20.0 ? QStringLiteral("rgb(242,183,73)")    // a bit off
                                              : QStringLiteral("rgb(230,120,120)");  // notably off
                suggestLbl->setText(tr("<span style='font-size:14pt; font-weight:500; color:%1;'>%2</span>"
                                       "&nbsp;&nbsp;<span style='color:rgb(138,154,172);'>%3%4 ppm</span>"
                                       "&nbsp;&nbsp;<span style='color:%1;'>error %5 Hz</span>")
                    .arg(col)
                    .arg(f, 0, 'f', 8)
                    .arg(ppm >= 0 ? QStringLiteral("+") : QString())
                    .arg(ppm, 0, 'f', 2)
                    .arg(measuredHz - expected, 0, 'f', 1));
            } else {
                *valid = false;
                applyBtn->setEnabled(false);
                suggestLbl->setText(strong
                    ? tr("— tune ~1 kHz below the station in USB —")
                    : tr("— waiting for a strong carrier —"));
            }
        });

        connect(applyBtn, &QPushButton::clicked, this, [this, suggested, valid] {
            if (*valid) stream_->setFreqCorrection(*suggested);
        });
    }

    auto *note = new QLabel(tr(
        "<b>Reset</b> always returns to exactly how the radio tunes today, so "
        "you can never get stuck. <b>Restore previous</b> undoes the last "
        "change. Prefer a visual? The floating <b>Freq Cal</b> panel (its chip "
        "is in the header) shows the carrier walking to a centre null as you "
        "tune — same measurement, guided."), page);
    note->setWordWrap(true);
    note->setProperty("calDir", true);
    root->addWidget(note);

    root->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildTunerTab() {
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);
    root->setSpacing(10);

    // --- Antenna + match window ---
    auto *grpAnt = new QGroupBox(tr("Antenna"), page);
    auto *antForm = new QFormLayout(grpAnt);
    auto *antCombo = new QComboBox(grpAnt);
    antCombo->addItems(tuner_->antennaNames());
    antCombo->setCurrentIndex(tuner_->activeAntenna());
    antForm->addRow(tr("Active antenna"), antCombo);

    auto *renameRow = new QHBoxLayout();
    auto *nameEdit = new QLineEdit(grpAnt);
    nameEdit->setText(tuner_->antennaNames().value(tuner_->activeAntenna()));
    auto *renameBtn = new QPushButton(tr("Rename"), grpAnt);
    renameRow->addWidget(nameEdit, 1);
    renameRow->addWidget(renameBtn);
    antForm->addRow(tr("Name"), renameRow);

    auto *matchSpin = new QSpinBox(grpAnt);
    matchSpin->setRange(50, 20000);
    matchSpin->setSingleStep(50);
    matchSpin->setSuffix(tr(" Hz"));
    matchSpin->setValue(static_cast<int>(tuner_->matchToleranceHz()));
    matchSpin->setToolTip(tr(
        "How close the dial must be to a stored point to count as an exact "
        "(green) match. Tune farther than this and the nearest point is shown "
        "amber with the offset."));
    antForm->addRow(tr("Match window (±)"), matchSpin);
    root->addWidget(grpAnt);

    // --- Stored-points table ---
    auto *grpPts = new QGroupBox(tr("Stored tuner settings"), page);
    auto *ptsLay = new QVBoxLayout(grpPts);
    auto *table = new QTableWidget(0, 6, grpPts);
    table->setHorizontalHeaderLabels({tr("Freq (MHz)"), tr("Band"),
        tr("Input"), tr("Output"), tr("Inductor"), tr("Note")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    ptsLay->addWidget(table);

    auto *btnRow = new QHBoxLayout();
    auto *addBtn = new QPushButton(tr("Add current freq"), grpPts);
    auto *delBtn = new QPushButton(tr("Delete"), grpPts);
    auto *clearBtn = new QPushButton(tr("Clear all"), grpPts);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(clearBtn);
    ptsLay->addLayout(btnRow);
    root->addWidget(grpPts, 1);

    // Repopulate the table from the model (block signals so the setItem
    // calls don't recurse through itemChanged).
    auto repopulate = [this, table]() {
        QSignalBlocker block(table);
        const QVariantList pts = tuner_->points();
        table->setRowCount(static_cast<int>(pts.size()));
        for (int r = 0; r < pts.size(); ++r) {
            const QVariantMap m = pts[r].toMap();
            const double f = m.value(QStringLiteral("freqHz")).toDouble();
            auto mkItem = [](const QString &t, bool editable) {
                auto *it = new QTableWidgetItem(t);
                if (!editable) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
                return it;
            };
            table->setItem(r, 0, mkItem(QString::number(f / 1e6, 'f', 6), true));
            table->setItem(r, 1, mkItem(m.value(QStringLiteral("band")).toString(), false));
            table->setItem(r, 2, mkItem(m.value(QStringLiteral("input")).toString(), true));
            table->setItem(r, 3, mkItem(m.value(QStringLiteral("output")).toString(), true));
            table->setItem(r, 4, mkItem(m.value(QStringLiteral("inductor")).toString(), true));
            table->setItem(r, 5, mkItem(m.value(QStringLiteral("note")).toString(), true));
        }
    };
    repopulate();

    connect(antCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, nameEdit, repopulate](int i) {
                tuner_->setActiveAntenna(i);
                nameEdit->setText(tuner_->antennaNames().value(i));
                repopulate();
            });
    connect(renameBtn, &QPushButton::clicked, this,
            [this, antCombo, nameEdit]() {
                const int i = antCombo->currentIndex();
                tuner_->renameAntenna(i, nameEdit->text());
                QSignalBlocker b(antCombo);
                antCombo->setItemText(i, tuner_->antennaNames().value(i));
            });
    connect(matchSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) { tuner_->setMatchToleranceHz(static_cast<double>(v)); });

    // Cell edit → write the whole row back (Freq cell parsed MHz→Hz; a bad
    // frequency reverts the edit).  editPoint re-sorts + emits pointsChanged
    // → the table refreshes via the connection below.
    connect(table, &QTableWidget::itemChanged, this,
            [this, table, repopulate](QTableWidgetItem *item) {
                const int r = item->row();
                if (r < 0 || r >= tuner_->pointCount()) return;
                bool ok = false;
                const double mhz = table->item(r, 0)
                    ? table->item(r, 0)->text().toDouble(&ok) : 0.0;
                if (!ok || mhz <= 0) { repopulate(); return; }
                auto cell = [table, r](int c) {
                    return table->item(r, c) ? table->item(r, c)->text() : QString();
                };
                tuner_->editPoint(r, mhz * 1e6, cell(2), cell(3), cell(4), cell(5));
            });
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        double f = tuner_->currentFreqHz();
        if (f <= 0) f = 14'100'000.0;   // sensible default if there's no dial
        tuner_->storePoint(f, QString(), QString(), QString(), QString());
    });
    connect(delBtn, &QPushButton::clicked, this, [this, table]() {
        const int r = table->currentRow();
        if (r >= 0) tuner_->deletePoint(r);
    });
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("Clear tuner memory"),
                tr("Delete all stored points for this antenna?"))
            == QMessageBox::Yes)
            tuner_->clearActiveAntenna();
    });
    // Any model change (here or from the front panel) refreshes the table.
    connect(tuner_, &TunerMemory::pointsChanged, table,
            [repopulate]() { repopulate(); });

    return page;
}

// ─────────────────────────────────────────────────────────────────
// TX-1 component 5b: Settings → TX tab.
//
// Operator-facing UI for the TR-sequencing + cos² amplitude envelope
// knobs that ship as load-bearing hot-switch protection for external
// solid-state HF linear amplifiers.  Defaults are the operator's
// bench-validated working-station config (matches the existing TR-
// sequencing values + cos² envelope durations the engine ships at).
//
// ⚠ HOT-SWITCH WARNING: tooltips on RF Delay + Fade-In Duration carry
// the explicit warning that reducing these below the external amp's
// T/R relay settle spec can destroy the PA.  Settings UI honours the
// operator's value (clamped to [1, 500] ms) — no internal floor —
// because the operator's specific amp is the authority.  An informed
// low value is an operator choice, not a UI lockout.
// ─────────────────────────────────────────────────────────────────
QWidget *SettingsDialog::buildCwTab() {
    // #105 CW-1a-2 — dedicated CW tab.  CW spans RX + TX as one
    // operating mode, so its controls live together here rather than
    // scattered across the TX/DSP tabs.  Today it holds the TX keyer
    // config (cw_tx_design.md §7); RX-side CW pitch / peaking and a
    // future decoder consolidate here over time.  Each control
    // persists (tx/cw/*) and pushes to prn->cw (composer cases
    // 12/13/14 consume it).  Brief inert window (operator-accepted):
    // CW transmit keying lands in the paddle commit (C-2); these
    // settings are live + persisted now.
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);

    if (stream_) {
        auto *grp = new QGroupBox(tr("CW Keyer"), page);
        auto *form = new QFormLayout(grp);

        auto *spd = new QSpinBox(grp);
        spd->setRange(1, 60);
        spd->setSuffix(tr(" WPM"));
        spd->setValue(stream_->cwKeyerSpeedWpm());
        spd->setToolTip(tr(
            "CW keyer speed in words-per-minute (PARIS).  Drives the "
            "gateware iambic keyer (paddle) and the host keyer (computer "
            "/ TCI CW).  Range 1-60; default 25."));
        connect(spd, qOverload<int>(&QSpinBox::valueChanged), grp,
                [this](int v) { stream_->setCwKeyerSpeedWpm(v); });
        connect(stream_, &lyra::ipc::HL2Stream::cwKeyerSpeedWpmChanged,
                spd, [spd](int v) { if (spd->value()!=v) spd->setValue(v); });
        form->addRow(tr("Keyer speed:"), spd);

        // #105 — ONE CW pitch.  This control edits the SAME value as the
        // Tuning-panel "Pitch" (WdspEngine::cwPitchHz) — change one, both
        // move.  It sets the RX beat, the marker, the TX keyed-carrier offset,
        // AND the gateware HW sidetone (which now follows the CW pitch).  No
        // separate sidetone-frequency control.  (engine_ is the WDSP engine,
        // the single source of truth for the pitch.)
        if (engine_) {
            auto *pitch = new QSpinBox(grp);
            pitch->setRange(200, 1500);
            pitch->setSingleStep(10);
            pitch->setSuffix(tr(" Hz"));
            pitch->setValue(engine_->cwPitchHz());
            pitch->setToolTip(tr(
                "CW pitch — the single value shared by RX and TX: the RX beat "
                "note, the carrier marker, the TX keyed-carrier offset, and "
                "the HW sidetone all use it.  Same control as the Tuning "
                "panel Pitch.  Range 200-1500 Hz; default 600."));
            connect(pitch, qOverload<int>(&QSpinBox::valueChanged), grp,
                    [this](int v) { engine_->setCwPitchHz(v); });
            connect(engine_, &lyra::dsp::WdspEngine::cwPitchChanged,
                    pitch, [this, pitch] {
                        const int v = engine_->cwPitchHz();
                        if (pitch->value() != v) pitch->setValue(v);
                    });
            form->addRow(tr("CW pitch:"), pitch);
        }

        auto *wt = new QSpinBox(grp);
        wt->setRange(33, 66);
        wt->setSuffix(tr(" %"));
        wt->setValue(stream_->cwKeyerWeight());
        wt->setToolTip(tr(
            "Keyer weight — dot/dash mark-to-space ratio as a percent.  "
            "50 %% = standard.  Higher = heavier (longer marks).  Range "
            "33-66; default 50."));
        connect(wt, qOverload<int>(&QSpinBox::valueChanged), grp,
                [this](int v) { stream_->setCwKeyerWeight(v); });
        connect(stream_, &lyra::ipc::HL2Stream::cwKeyerWeightChanged,
                wt, [wt](int v) { if (wt->value()!=v) wt->setValue(v); });
        form->addRow(tr("Weight:"), wt);

        auto *iamb = new QCheckBox(tr("Iambic keyer  (off = straight key)"), grp);
        iamb->setChecked(stream_->cwIambic());
        iamb->setToolTip(tr(
            "ON = iambic (dual-lever paddle) keyer; OFF = straight key / "
            "single lever.  Sets the gateware keyer mode for the paddle "
            "path."));
        form->addRow(iamb);

        auto *mode = new QComboBox(grp);
        mode->addItem(tr("Iambic A"));
        mode->addItem(tr("Iambic B"));
        mode->setCurrentIndex(stream_->cwModeB() ? 1 : 0);
        mode->setEnabled(stream_->cwIambic());
        mode->setToolTip(tr(
            "Iambic timing mode.  Mode B inserts the opposite element "
            "after both paddles release (common default); Mode A does "
            "not.  Only applies when the iambic keyer is on."));
        connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), grp,
                [this](int idx) { stream_->setCwModeB(idx == 1); });
        connect(stream_, &lyra::ipc::HL2Stream::cwModeBChanged, mode,
                [mode](bool b) {
            const int i = b ? 1 : 0;
            if (mode->currentIndex() != i) { QSignalBlocker s(mode); mode->setCurrentIndex(i); }
        });
        form->addRow(tr("Iambic mode:"), mode);

        connect(iamb, &QCheckBox::toggled, grp, [this, mode](bool on) {
            stream_->setCwIambic(on);
            mode->setEnabled(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::cwIambicChanged, iamb,
                [iamb, mode](bool on) {
            if (iamb->isChecked() != on) { QSignalBlocker s(iamb); iamb->setChecked(on); }
            mode->setEnabled(on);
        });

        auto *rev = new QCheckBox(tr("Reverse paddles  (dot ↔ dash)"), grp);
        rev->setChecked(stream_->cwRevPaddle());
        rev->setToolTip(tr("Swap the dot and dash paddle contacts."));
        form->addRow(rev);
        connect(rev, &QCheckBox::toggled, grp,
                [this](bool on) { stream_->setCwRevPaddle(on); });
        connect(stream_, &lyra::ipc::HL2Stream::cwRevPaddleChanged, rev,
                [rev](bool on) {
            if (rev->isChecked() != on) { QSignalBlocker s(rev); rev->setChecked(on); }
        });

        auto *strict = new QCheckBox(tr("Strict character spacing"), grp);
        strict->setChecked(stream_->cwStrictSpacing());
        strict->setToolTip(tr(
            "Enforce exact inter-element spacing even when sending faster "
            "than the set WPM."));
        form->addRow(strict);
        connect(strict, &QCheckBox::toggled, grp,
                [this](bool on) { stream_->setCwStrictSpacing(on); });
        connect(stream_, &lyra::ipc::HL2Stream::cwStrictSpacingChanged, strict,
                [strict](bool on) {
            if (strict->isChecked() != on) { QSignalBlocker s(strict); strict->setChecked(on); }
        });

        // #105 — break-in mode (QSK / Semi / Manual), matching the reference's
        // BreakIn enum.  QSK = full break-in, gateware keys autonomously and
        // the host stays in RX (no panadapter flip to TX); Semi = host
        // transmits while keying and drops after the hang; Manual = hold PTT
        // (foot switch) and key within.  Default Semi.
        auto *bk = new QComboBox(grp);
        bk->addItem(tr("QSK (full break-in)"));   // 0
        bk->addItem(tr("Semi break-in"));         // 1
        bk->addItem(tr("Manual (hold PTT)"));     // 2
        bk->setCurrentIndex(std::clamp(stream_->cwBreakInMode(), 0, 2));
        bk->setToolTip(tr(
            "CW break-in mode:\n"
            "• QSK — full break-in: the radio's keyer keys the carrier on its "
            "own and the receiver stays live (no display flip to TX).\n"
            "• Semi — the radio transmits while you key and drops back to RX "
            "after the Break-in hang below.\n"
            "• Manual — you hold PTT (e.g. a foot switch) and key within it."));
        form->addRow(tr("Break-in:"), bk);

        auto *hang = new QSpinBox(grp);
        hang->setRange(0, 1000);
        hang->setSuffix(tr(" ms"));
        hang->setValue(stream_->cwHangDelayMs());
        hang->setEnabled(stream_->cwBreakInMode() != 2);   // not Manual
        hang->setToolTip(tr(
            "Break-in hang — how long the radio stays keyed after the last "
            "element before dropping back to RX.  Range 0-1000 ms; default "
            "300.  Applies to QSK / Semi (not Manual)."));
        connect(hang, qOverload<int>(&QSpinBox::valueChanged), grp,
                [this](int v) { stream_->setCwHangDelayMs(v); });
        connect(stream_, &lyra::ipc::HL2Stream::cwHangDelayMsChanged, hang,
                [hang](int v) { if (hang->value()!=v) hang->setValue(v); });
        form->addRow(tr("Break-in hang:"), hang);

        connect(bk, qOverload<int>(&QComboBox::currentIndexChanged), grp,
                [this, hang](int idx) {
            stream_->setCwBreakInMode(idx);
            hang->setEnabled(idx != 2);
        });
        connect(stream_, &lyra::ipc::HL2Stream::cwBreakInModeChanged, bk,
                [bk, hang](int mode) {
            if (bk->currentIndex() != mode) { QSignalBlocker s(bk); bk->setCurrentIndex(mode); }
            hang->setEnabled(mode != 2);
        });

        auto *st = new QCheckBox(tr("Sidetone  (radio hardware)"), grp);
        st->setChecked(stream_->cwSidetoneOn());
        st->setToolTip(tr(
            "Enable the HL2's FPGA-generated CW sidetone on the phones "
            "jack while keying — no host audio path needed; the tone is "
            "made in the radio at the CW pitch above."));
        form->addRow(st);

        auto *stl = new QSpinBox(grp);
        stl->setRange(0, 127);
        stl->setValue(stream_->cwSidetoneLevel());
        stl->setEnabled(stream_->cwSidetoneOn());
        stl->setToolTip(tr("Hardware sidetone level (0-127)."));
        connect(stl, qOverload<int>(&QSpinBox::valueChanged), grp,
                [this](int v) { stream_->setCwSidetoneLevel(v); });
        connect(stream_, &lyra::ipc::HL2Stream::cwSidetoneLevelChanged, stl,
                [stl](int v) { if (stl->value()!=v) stl->setValue(v); });
        form->addRow(tr("Sidetone level:"), stl);

        connect(st, &QCheckBox::toggled, grp, [this, stl](bool on) {
            stream_->setCwSidetoneOn(on);
            stl->setEnabled(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::cwSidetoneOnChanged, st,
                [st, stl](bool on) {
            if (st->isChecked() != on) { QSignalBlocker s(st); st->setChecked(on); }
            stl->setEnabled(on);
        });

        root->addWidget(grp);
    }

    // #171 — external keyer / Winkeyer note (Piece A: the HL2 KEY jack path).
    {
        auto *note = new QLabel(
            tr("<b>External keyer / Winkeyer (paddle):</b> connect its KEY "
               "output to the HL2 <b>KEY jack</b> and turn <b>Iambic OFF</b> "
               "above — the keyer does its own timing, so the radio passes the "
               "key straight through (Iambic ON would re-key it as a paddle).  "
               "A Winkeyer's USB/COM port is for its own config app (WK3tools, "
               "your logger), not Lyra."), page);
        note->setWordWrap(true);
        note->setStyleSheet("color: palette(mid);");
        root->addWidget(note);
    }

    // #171 Piece B — serial CW key input (straight key / bug / external keyer's
    // KEY output wired to a COM port pin → host drives cwx).  Lyra's
    // equivalent of the reference CW "Connections" COM-port option.
    if (serialCwKey_) {
        auto *grp  = new QGroupBox(tr("CW key over COM port"), page);
        auto *form = new QFormLayout(grp);

        auto *en = new QCheckBox(
            tr("Key Lyra's CW from a serial key line"), grp);
        en->setChecked(serialCwKey_->enabled());
        en->setToolTip(tr(
            "A straight key, bug, or external keyer's KEY output wired to a "
            "COM port's CTS or DSR pin keys Lyra's CW (the host keys via CWX).  "
            "CW mode only.  For the tightest timing on a paddle, prefer the HL2 "
            "KEY jack above — a host-polled serial line adds a little jitter."));
        form->addRow(en);

        auto *portCombo = new QComboBox(grp);
        portCombo->setEditable(true);
        portCombo->setInsertPolicy(QComboBox::NoInsert);
        portCombo->lineEdit()->setPlaceholderText(
            tr("type COMx if not listed"));
        portCombo->setMaximumWidth(380);
        auto repopulate = [this, portCombo]() {
            const QString cur = serialCwKey_->portName();
            QSignalBlocker b(portCombo);
            portCombo->clear();
            portCombo->addItem(tr("(none)"), QString());
            for (const auto &p : enumComPorts())
                portCombo->addItem(p.second.isEmpty()
                                       ? p.first
                                       : QStringLiteral("%1 — %2")
                                             .arg(p.first, p.second),
                                   p.first);
            if (!cur.isEmpty() && portCombo->findData(cur) < 0)
                portCombo->addItem(tr("%1 (not present)").arg(cur), cur);
            const int idx = portCombo->findData(cur);
            portCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        };
        repopulate();
        form->addRow(tr("COM port:"), portCombo);

        // Resolve the chosen port from the editable combo: prefer the item's
        // stored port name, else the typed text (strip any " — desc" suffix).
        auto resolvePort = [portCombo]() -> QString {
            const QVariant d = portCombo->currentData();
            if (d.isValid() && !d.toString().isEmpty()) return d.toString();
            return portCombo->currentText().section(QStringLiteral(" — "), 0, 0)
                       .trimmed();
        };
        auto applyPort = [this, resolvePort]() {
            serialCwKey_->setPortName(resolvePort());
        };
        connect(portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                serialCwKey_, applyPort);
        connect(portCombo->lineEdit(), &QLineEdit::editingFinished,
                serialCwKey_, applyPort);

        auto *rescan = new QPushButton(tr("Rescan ports"), grp);
        connect(rescan, &QPushButton::clicked, grp,
                [repopulate]() { repopulate(); });
        form->addRow(QString(), rescan);

        auto *lineCombo = new QComboBox(grp);
        lineCombo->addItem(tr("CTS"), int(lyra::cat::SerialCwKey::Line::Cts));
        lineCombo->addItem(tr("DSR"), int(lyra::cat::SerialCwKey::Line::Dsr));
        lineCombo->setCurrentIndex(int(serialCwKey_->watchLine()));
        lineCombo->setToolTip(tr(
            "Which COM-port input pin the key is wired to (asserted = key "
            "down).  The reference reads dot on DSR / dash on CTS; for a single "
            "straight-key line pick whichever your cable uses."));
        connect(lineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                serialCwKey_, [this, lineCombo](int) {
            serialCwKey_->setWatchLine(static_cast<lyra::cat::SerialCwKey::Line>(
                lineCombo->currentData().toInt()));
        });
        form->addRow(tr("Key line:"), lineCombo);

        auto *inv = new QCheckBox(tr("Invert  (key is active-low)"), grp);
        inv->setChecked(serialCwKey_->invert());
        inv->setToolTip(tr("Flip the sense if your key/keyer pulls the line "
                           "low to key."));
        form->addRow(inv);
        connect(inv, &QCheckBox::toggled, serialCwKey_,
                [this](bool on) { serialCwKey_->setInvert(on); });

        connect(en, &QCheckBox::toggled, serialCwKey_,
                [this](bool on) { serialCwKey_->setEnabled(on); });
        connect(serialCwKey_, &lyra::cat::SerialCwKey::enabledChanged, en,
                [en](bool on) { if (en->isChecked() != on) {
                    QSignalBlocker s(en); en->setChecked(on); } });

        root->addWidget(grp);
    }

    root->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildPaGainTab() {
    // TX power model Stage 3 — the per-band "PA Gain By Band" table
    // (Thetis port).  One number per band; default 100 = neutral.  The
    // operator keys each band into a dummy load, reads the watts on the
    // PWR meter, and nudges that band's number until the dial reads true
    // — lower to tame a hot band, raise to push a weak one.  Each HL2's
    // full output varies and varies by band, so nothing is computed: the
    // table holds measured corrections.  Writes go live through
    // HL2Stream (persisted to QSettings pa_gain/<band>/gain) and re-apply
    // immediately if you're transmitting on that band.
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);

    // Readability (operator ask 2026-07-03): lift the muted body text to the
    // bright foreground colour and make the ⚠ warnings amber (the theme's
    // base QLabel colour is a low-contrast grey on the dark bg, and the
    // lyraWarn property has no rule of its own).  ONLY the directions text is
    // enlarged (via the `paDir` property on each explanatory paragraph) — the
    // band grid / spin boxes / buttons stay native so the tab matches the
    // other panels.
    page->setStyleSheet(QStringLiteral(
        "QLabel { color: rgb(205,217,229); }"
        "QLabel[lyraWarn=\"true\"] { color: rgb(242,183,73); }"
        "QLabel[paDir=\"true\"] { font-size: 10pt; }"));

    auto *intro = new QLabel(
        tr("Per-band TX PA gain.  100 = neutral.  Key each band into a "
           "dummy load, watch the PWR meter, and nudge its number until "
           "the power reads true — lower tames a hot band, higher pushes "
           "a weak one.  Drive % stays the dial; this calibrates what it "
           "means in watts on each band."),
        page);
    intro->setWordWrap(true);
    intro->setProperty("paDir", true);
    root->addWidget(intro);

    // 2-column body — LEFT: PWR meter calibration (do this FIRST);  RIGHT:
    // PA Gain By Band + Max Output.  Left→right follows the workflow order,
    // and the two tall per-band tables side by side keep the tab from
    // scrolling.
    auto *body     = new QHBoxLayout();
    body->setSpacing(16);
    auto *leftCol  = new QVBoxLayout();
    auto *rightCol = new QVBoxLayout();
    body->addLayout(leftCol,  1);
    body->addLayout(rightCol, 1);
    root->addLayout(body);

    auto *grp  = new QGroupBox(tr("PA Gain By Band"), page);
    auto *grid = new QGridLayout(grp);
    // Compact, left-aligned columns — the spinboxes hold "100.0", they
    // don't need to span the panel.  A trailing stretch column soaks up
    // the slack so the table hugs the left.
    grid->setHorizontalSpacing(16);
    grid->setColumnStretch(5, 1);
    grid->addWidget(new QLabel(tr("Band"),            grp), 0, 0);
    grid->addWidget(new QLabel(tr("PA Gain"),         grp), 0, 1);
    grid->addWidget(new QLabel(tr("Full Output (W)"), grp), 0, 2);
    grid->addWidget(new QLabel(tr("Cap tuned"),       grp), 0, 3);

    constexpr int kPaSpinW = 96;   // plenty for "100.0" + the up/down arrows
    const auto &bands = lyra::amateurBands();
    const int n = static_cast<int>(bands.size());
    auto *tunedLabels = new QVector<QLabel *>();   // Stage B — live "tuned" marks
    auto *fullSpins   = new QVector<QDoubleSpinBox *>();  // for the cap-uncalibrated warning
    for (int i = 0; i < n; ++i) {
        grid->addWidget(
            new QLabel(QString::fromUtf8(bands[i].name), grp), i + 1, 0);

        auto *gainSpin = new QDoubleSpinBox(grp);
        gainSpin->setRange(0.0, 200.0);   // Thetis range; 100 = neutral
        gainSpin->setDecimals(1);
        gainSpin->setSingleStep(1.0);
        gainSpin->setFixedWidth(kPaSpinW);
        gainSpin->setValue(stream_ ? stream_->paGainForBand(i) : 100.0);
        grid->addWidget(gainSpin, i + 1, 1);
        connect(gainSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this, i](double v) {
                    if (stream_) stream_->setPaGainForBand(i, v);
                });

        // Stage 3b — measured full output (W) at full drive; feeds the
        // conservative fallback ceiling + the TUN auto-learn servo.
        auto *fullSpin = new QDoubleSpinBox(grp);
        fullSpin->setRange(0.0, 200.0);
        fullSpin->setDecimals(1);
        fullSpin->setSingleStep(0.1);
        fullSpin->setFixedWidth(kPaSpinW);
        fullSpin->setSpecialValueText(tr("—"));   // 0 shows as "not measured"
        fullSpin->setValue(stream_ ? stream_->fullOutputForBand(i) : 0.0);
        grid->addWidget(fullSpin, i + 1, 2);
        connect(fullSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this, i](double v) {
                    if (stream_) stream_->setFullOutputForBand(i, v);
                });
        fullSpins->append(fullSpin);

        // Stage B — per-band "is the cap auto-tuned for the current cap?"
        // indicator; refreshed live so the operator watches each band turn
        // ✓ as they TUN it.
        auto *tuned = new QLabel(grp);
        tuned->setMinimumWidth(56);
        tunedLabels->append(tuned);
        grid->addWidget(tuned, i + 1, 3);

        // Per-row Clear (operator ask 2026-07-03) — wipe this band's PA-Gain
        // calibration: PA Gain → 100, Full Output → 0 (which also drops the
        // arm-enable if it was the only calibrated band), and un-learn the
        // TUN cap ceiling so it re-tunes clean.
        auto *clrPa = new QPushButton(tr("Clear"), grp);
        clrPa->setFixedWidth(64);
        connect(clrPa, &QPushButton::clicked, this,
                [this, i, gainSpin, fullSpin]() {
                    if (gainSpin) gainSpin->setValue(100.0);
                    if (fullSpin) fullSpin->setValue(0.0);
                    if (stream_)  stream_->clearCapLearnForBand(i);
                });
        grid->addWidget(clrPa, i + 1, 4);
    }
    rightCol->addWidget(grp);

    // Stage B — the watts Max cap.  One number; the TUN servo walks each
    // band's drive ceiling up to it and locks it, so it's band-correct +
    // exact with no model.
    auto *capGrp  = new QGroupBox(tr("Max Output (amp protection)"), page);
    auto *capForm = new QFormLayout(capGrp);
    auto *capRow  = new QHBoxLayout();
    auto *capChk  = new QCheckBox(tr("Limit TX output to"), capGrp);
    auto *capSpin = new QDoubleSpinBox(capGrp);
    capSpin->setRange(0.1, 200.0);
    capSpin->setDecimals(1);
    capSpin->setSingleStep(0.5);
    capSpin->setSuffix(tr(" W"));
    capSpin->setFixedWidth(110);
    const double curCap = stream_ ? stream_->maxOutputW() : 0.0;
    capChk->setChecked(curCap > 0.0);
    capSpin->setValue(curCap > 0.0 ? curCap : 2.5);
    capSpin->setEnabled(curCap > 0.0);
    capRow->addWidget(capChk);
    capRow->addWidget(capSpin);
    capRow->addStretch(1);
    capForm->addRow(capRow);

    // Cap ARM gate (2026-07-03) — a cap VALUE alone no longer limits.  The
    // operator arms it as a separate, deliberate step AFTER calibrating each
    // band's Full Output (in "PWR meter calibration" below), so an
    // uncalibrated cap can NEVER silently hold power at the ~30 % fallback
    // (the trap Pierre HS0ZRT hit).  Enabled only once ≥1 band is calibrated.
    auto *armRow = new QHBoxLayout();
    auto *armChk = new QCheckBox(tr("Enable Max cap (arm)"), capGrp);
    armChk->setChecked(stream_ && stream_->capArmed());
    armRow->addWidget(armChk);
    armRow->addStretch(1);
    capForm->addRow(armRow);
    if (stream_) {
        connect(armChk, &QCheckBox::toggled, stream_,
                &lyra::ipc::HL2Stream::setCapArmed);
        connect(stream_, &lyra::ipc::HL2Stream::capArmedChanged, armChk,
                [armChk](bool on) {
                    if (armChk->isChecked() != on) armChk->setChecked(on);
                });
    }

    auto *warn = new QLabel(
        tr("⚠  Calibrate into a DUMMY LOAD with your amplifier OUT of "
           "line.  Tuning a band walks the power UP from below to find your "
           "cap — verify it settles correctly on every band first, THEN put "
           "your amp back in."),
        capGrp);
    warn->setWordWrap(true);
    warn->setProperty("lyraWarn", true);
    warn->setProperty("paDir", true);
    capForm->addRow(warn);

    auto *capNote = new QLabel(
        tr("Set-up (into a DUMMY LOAD, amp OUT, PA Gain left at 100):\n"
           "1.  Calibrate each band first — see \"PWR meter calibration\" "
           "below.  TUN at FULL drive, read your external meter, type it in, "
           "and press \"Calibrate this band\".  At full drive that one step "
           "sets the meter trim AND fills the band's Full Output above.\n"
           "2.  Tick \"Limit TX output to __ W\" and set your cap.\n"
           "3.  Tick \"Enable Max cap (arm)\" — the cap does nothing until "
           "you do.\n"
           "4.  Key TUN on each band — Lyra walks the power up and parks it "
           "just UNDER your cap (the band shows ✓).  Do every band you use.\n"
           "SSB and the other modes then hold each tuned band at that level "
           "automatically (it caps voice peaks without chasing them).  An "
           "un-tuned band runs conservatively UNDER the cap until you tune "
           "it.  Change the cap → re-key TUN on each band to re-learn."),
        capGrp);
    capNote->setWordWrap(true);
    capNote->setProperty("paDir", true);

    // Coarse-DAC note — the cap deliberately lands UNDER the set value.
    auto *capUnder = new QLabel(
        tr("Why it lands a little under:  the Hermes Lite's drive control is "
           "coarse (≈16 hardware steps), and each step is a different number "
           "of watts on each band.  Your cap usually falls BETWEEN two steps "
           "(e.g. 3.0 W and 3.7 W with nothing in between), so Lyra always "
           "picks the step just UNDER your cap — the safe side for your amp.  "
           "A band may therefore sit noticeably below the number you set; that "
           "is intentional protection, not an error.  Landing exactly on the "
           "cap needs finer-than-hardware drive control (a planned future "
           "option)."),
        capGrp);
    capUnder->setWordWrap(true);
    capUnder->setProperty("paDir", true);

    // Live warning for the exact trap Pierre HS0ZRT hit: cap ON but no band
    // has a Full Output reference, so every band falls back to the
    // conservative ~30 % drive clamp → TX runs LOW ("6 W cap, only 3 W out").
    auto *capUncal = new QLabel(
        tr("⚠  Cap armed, but no band is calibrated — so Lyra limits TX to a "
           "safe conservative ~30% drive (power reads LOW on every band) until "
           "you calibrate.  Calibrate each band's Full Output (\"PWR meter "
           "calibration\" below), then TUN it to lock the cap at your exact "
           "watts.  If you don't run an amplifier, just untick the cap."),
        capGrp);
    capUncal->setWordWrap(true);
    capUncal->setProperty("lyraWarn", true);
    capUncal->setProperty("paDir", true);
    capForm->addRow(capUncal);

    // Operator-requested: explain the "Cap tuned" marks so "my ticks
    // disappeared" (usually after nudging the cap value, which correctly
    // clears them) stops confusing users.
    auto *capTicks = new QLabel(
        tr("“Cap tuned” marks:  green ✓ = this band is locked to your cap; "
           "red — = not tuned for the current cap yet.  Note that changing "
           "the cap value clears every ✓ — re-key TUN on each band to "
           "re-learn (that is expected, not lost calibration).  While you "
           "transmit, the TX panel also shows a live CAP chip (amber = "
           "uncalibrated ~30% fallback, cyan = holding a tuned band at your "
           "cap), so you can see the cap's state at a glance."),
        capGrp);
    capTicks->setWordWrap(true);
    capTicks->setProperty("paDir", true);
    // The three reference blocks (set-up steps · why-it-lands-under ·
    // cap-tuned marks) side by side, so this section stays SHORT and the whole
    // tab fits a normal window — all text still visible on open, no scrolling
    // or oversized window needed (operator ask 2026-07-03).
    auto *capText = new QHBoxLayout();
    capText->setSpacing(24);
    capText->addWidget(capNote,  1, Qt::AlignTop);
    capText->addWidget(capUnder, 1, Qt::AlignTop);
    capText->addWidget(capTicks, 1, Qt::AlignTop);
    capForm->addRow(capText);

    auto pushCap = [this, capChk, capSpin]() {
        if (stream_)
            stream_->setMaxOutputW(capChk->isChecked() ? capSpin->value() : 0.0);
    };
    connect(capChk, &QCheckBox::toggled, this,
            [capSpin, pushCap](bool on) { capSpin->setEnabled(on); pushCap(); });
    connect(capSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [pushCap](double) { pushCap(); });

    // Show the "cap on but uncalibrated" warning only when it applies: cap
    // ticked AND no band has a Full Output reference yet.  (Each connect
    // lambda copies refreshCapUncal, which itself only holds long-lived
    // widget pointers — safe for the dialog's lifetime.)
    auto refreshCapUncal = [capChk, capUncal, fullSpins, armChk]() {
        bool anyMeasured = false;
        for (auto *fs : *fullSpins)
            if (fs->value() > 0.0) { anyMeasured = true; break; }
        const bool capOn = capChk->isChecked();
        const bool armed = armChk->isChecked();
        // Arm a FRESH cap only after ≥1 band is calibrated (the foolproof
        // gate).  An ALREADY-armed cap — including one migrated from a
        // pre-arm-gate version — stays toggleable, so upgrading never forcibly
        // disarms an operator's existing amp protection.
        armChk->setEnabled(capOn && (anyMeasured || armed));
        // The conservative ~30 % low-power fallback only bites when the cap is
        // actually limiting (armed) on a band with no Full Output reference.
        capUncal->setVisible(capOn && armed && !anyMeasured);
    };
    refreshCapUncal();
    connect(capChk, &QCheckBox::toggled, this,
            [refreshCapUncal](bool) { refreshCapUncal(); });
    for (auto *fs : *fullSpins)
        connect(fs, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [refreshCapUncal](double) { refreshCapUncal(); });
    rightCol->addStretch(1);
    // Max Output spans the FULL width UNDER the two columns (operator ask
    // 2026-07-03 — balances the page vs stacking it under the right column).
    // Added to root after the body so it sits below both per-band tables.
    root->addWidget(capGrp);

    // ── PWR meter calibration (per band) ─────────────────────────────
    // Corrects the DISPLAYED watts to an external watt-meter, per band.
    // Does NOT change transmitted power (that's PA Gain By Band above) —
    // purely a reading fix.  Calibrate mid-band, at normal power.
    if (stream_) {
        auto *mcGrp = new QGroupBox(tr("PWR meter calibration (per band)"), page);
        auto *mcBox = new QVBoxLayout(mcGrp);

        auto *mcIntro = new QLabel(
            tr("Makes the on-screen watts match your external watt-meter, "
               "band by band.  This does NOT change your transmitted power "
               "(use PA Gain By Band above for that) — it only corrects what "
               "the PWR meter reads.  Do this FIRST, then PA Gain."),
            mcGrp);
        mcIntro->setWordWrap(true);
        mcIntro->setProperty("paDir", true);
        mcBox->addWidget(mcIntro);

        auto *mcWarn = new QLabel(
            tr("⚠  Calibrate into a DUMMY LOAD, amp OUT of line, at FULL "
               "drive (100%), with PA Gain left at 100.\n"
               "•  Full drive is the coupler's most accurate point AND lets "
               "this one step also set the band's Full Output for the amp "
               "cap — so you measure each band just once.\n"
               "•  Turn the Max cap OFF while calibrating — an armed cap "
               "holds the drive down and spoils the reading.\n"
               "•  Not in CW — CW won't make a steady tune carrier; use "
               "SSB / AM / FM."),
            mcGrp);
        mcWarn->setWordWrap(true);
        mcWarn->setProperty("lyraWarn", true);
        mcWarn->setProperty("paDir", true);
        mcBox->addWidget(mcWarn);

        // Always-visible TX-drive reminder — calibration needs FULL drive, so
        // surface the current level right here (the "oh, I forgot to set full
        // power" nudge; a tester at reduced drive got a bad reading).  Amber
        // below full, green at 100 %.  Refreshed by the mcTimer below.
        auto *mcDrive = new QLabel(mcGrp);
        mcDrive->setProperty("paDir", true);
        mcBox->addWidget(mcDrive);

        auto *mcLive = new QLabel(mcGrp);
        mcLive->setWordWrap(true);
        mcBox->addWidget(mcLive);

        auto *mcRow  = new QHBoxLayout();
        mcRow->addWidget(new QLabel(tr("Your watt-meter reads:"), mcGrp));
        auto *mcSpin = new QDoubleSpinBox(mcGrp);
        mcSpin->setRange(0.1, 200.0);
        mcSpin->setDecimals(1);
        mcSpin->setSingleStep(0.1);
        mcSpin->setSuffix(tr(" W"));
        mcSpin->setFixedWidth(110);
        mcSpin->setValue(5.0);
        mcRow->addWidget(mcSpin);
        auto *mcBtn = new QPushButton(tr("Calibrate this band"), mcGrp);
        mcRow->addWidget(mcBtn);
        // In-your-face readiness chip, right at the button — testers skip the
        // text warnings, so poke them here: red "IN CW", amber "NOT FULL",
        // green "READY".  Bordered box; set live in mcRefresh below.
        auto *mcChip = new QLabel(mcGrp);
        mcChip->setAlignment(Qt::AlignCenter);
        mcRow->addWidget(mcChip);
        mcRow->addStretch(1);
        mcBox->addLayout(mcRow);

        // Sticky result of the last Calibrate press (the live status line
        // mcLive above is refreshed by a timer and would otherwise clobber it).
        auto *mcResult = new QLabel(mcGrp);
        mcResult->setWordWrap(true);
        mcResult->setStyleSheet(QStringLiteral("color:#4ccf6b;"));
        mcBox->addWidget(mcResult);

        auto *mcGrid = new QGridLayout();
        mcGrid->setHorizontalSpacing(16);
        mcGrid->setColumnStretch(4, 1);
        mcGrid->addWidget(new QLabel(tr("Band"),     mcGrp), 0, 0);
        mcGrid->addWidget(new QLabel(tr("Mid-band"), mcGrp), 0, 1);
        mcGrid->addWidget(new QLabel(tr("Trim"),     mcGrp), 0, 2);
        const auto &pbands = lyra::amateurBands();
        const int   pn     = static_cast<int>(pbands.size());
        auto *trimLabels = new QVector<QLabel *>();
        for (int i = 0; i < pn; ++i) {
            mcGrid->addWidget(
                new QLabel(QString::fromUtf8(pbands[i].name), mcGrp), i + 1, 0);
            const double midMHz = (pbands[i].low + pbands[i].high) / 2.0 / 1e6;
            mcGrid->addWidget(
                new QLabel(QStringLiteral("%1 MHz").arg(midMHz, 0, 'f', 3), mcGrp),
                i + 1, 1);
            auto *tl = new QLabel(mcGrp);
            tl->setMinimumWidth(96);
            trimLabels->append(tl);
            mcGrid->addWidget(tl, i + 1, 2);
            auto *clr = new QPushButton(tr("Clear"), mcGrp);
            clr->setFixedWidth(64);
            connect(clr, &QPushButton::clicked, this,
                    [this, i]() { if (stream_) stream_->setPwrTrimForBand(i, 1.0); });
            mcGrid->addWidget(clr, i + 1, 3);
        }
        mcBox->addLayout(mcGrid);
        leftCol->addWidget(mcGrp);
        leftCol->addStretch(1);

        // Capture-while-you-tune: compute trim = your-meter ÷ raw off the
        // LIVE reading if still keyed, else the last sample Lyra latched while
        // this band was tuned — so there's no forced second key.  At full
        // drive the same reading also fills the band's Full Output (amp cap).
        connect(mcBtn, &QPushButton::clicked, this,
                [this, mcSpin, mcResult, fullSpins]() {
            if (!stream_) return;
            // Amber for every refusal below; flipped to green on a real cal.
            mcResult->setStyleSheet(QStringLiteral("color:#e5a54e;"));
            const int b = lyra::bandIndexForFreq(
                              static_cast<int>(stream_->rx1FreqHz()));
            if (b < 0) {
                mcResult->setText(tr("Tune to an amateur band first."));
                return;
            }
            // Guard — CW won't make a steady tune carrier.
            if (stream_->txModeIsCw()) {
                mcResult->setText(tr("Switch out of CW to calibrate — CW "
                                     "won't produce the tune carrier."));
                return;
            }
            // Guard — an armed cap holds the drive down → bad reading.
            if (stream_->capArmed()) {
                mcResult->setText(tr("Turn the Max cap OFF (un-arm it) to "
                                     "calibrate."));
                return;
            }
            const bool   keyed = stream_->moxActive() || stream_->cwKeyingActive();
            const double raw   = keyed ? stream_->fwdPowerW()
                                       : stream_->capturedRawWForBand(b);
            if (std::isnan(raw) || raw < 0.25) {
                mcResult->setText(tr("No reading yet — TUN this band at full "
                                     "drive first, then Calibrate."));
                return;
            }
            const double entered = mcSpin->value();
            stream_->setPwrTrimForBand(b, entered / raw);
            mcResult->setStyleSheet(QStringLiteral("color:#4ccf6b;"));  // success
            const auto &bs = lyra::amateurBands();
            const QString bn = b < int(bs.size())
                ? QString::fromUtf8(bs[b].name) : QString::number(b);
            // At FULL drive (255) the entered watts IS this band's Full Output
            // — set it too (feeds the amp cap).  Writing the spin fires its
            // valueChanged → setFullOutputForBand + the cap-gate refresh.
            const int drv = keyed ? stream_->txDriveLevel()
                                  : stream_->capturedDriveForBand(b);
            if (drv >= 255 && b < fullSpins->size()) {
                (*fullSpins)[b]->setValue(entered);
                mcResult->setText(
                    tr("✓ %1 calibrated — meter set to %2 W, and Full Output "
                       "set to %2 W (amp-cap reference).")
                        .arg(bn).arg(entered, 0, 'f', 1));
            } else {
                mcResult->setText(
                    tr("✓ %1 meter calibrated to %2 W.  Full Output NOT set — "
                       "TUN at FULL (100%) drive to also set the amp-cap "
                       "reference.").arg(bn).arg(entered, 0, 'f', 1));
            }
        });

        auto mcRefresh = [this, mcLive, mcDrive, mcChip, trimLabels]() {
            if (!stream_) return;
            {
                // Cal-readiness — proactively flag the two setup traps that
                // make a TUN produce no / low RF, BEFORE you key: CW mode (no
                // tune carrier) and less-than-full drive.  Both a text line
                // (mcDrive) AND a bordered box next to the button (mcChip, the
                // in-your-face poke).  Green "ready" only in a voice mode at
                // full drive.
                // Calibration is done by TUN, so the drive that matters is the
                // one TUN will actually key at — which depends on the Tune-drive
                // mode (Settings → TX): the live TX Drive slider (0), the
                // per-band Tune slider (1), or the Fixed tune drive (2).
                // Checking only txDriveLevel missed the Tune-slider / Fixed
                // cases, so a low Tune slider didn't trigger the notice.
                const int tdm = prefs_ ? prefs_->tuneDriveMode() : 0;
                int drvPct; QString drvSrc;
                if (tdm == 1) {           // TuneDriveTune — per-band Tune slider
                    drvPct = prefs_ ? prefs_->tuneDrivePct() : 100;
                    drvSrc = tr("Tune drive");
                } else if (tdm == 2) {    // TuneDriveFixed
                    drvPct = prefs_ ? prefs_->fixedTuneDrivePct() : 100;
                    drvSrc = tr("Fixed tune drive");
                } else {                  // TuneDriveSlider — live TX Drive
                    drvPct = (stream_->txDriveLevel() * 100 + 127) / 255;
                    drvSrc = tr("TX Drive");
                }
                const QString amber = QStringLiteral("color:#e5a54e; font-weight:600;");
                static const char *kChipRed =
                    "background:#3a1512; border:2px solid #ff5a3c; border-radius:4px;"
                    "color:#ff9a80; font-weight:700; padding:3px 10px;";
                static const char *kChipAmber =
                    "background:#3a2a10; border:2px solid #ffb020; border-radius:4px;"
                    "color:#ffcf6b; font-weight:700; padding:3px 10px;";
                static const char *kChipGreen =
                    "background:#12252e; border:2px solid #4ccf6b; border-radius:4px;"
                    "color:#4ccf6b; font-weight:700; padding:3px 10px;";
                if (stream_->txModeIsCw()) {
                    mcDrive->setText(tr("⚠ You're in CW — switch to SSB / AM / FM "
                        "to calibrate (CW won't make a steady tune carrier)."));
                    mcDrive->setStyleSheet(amber);
                    mcChip->setText(tr("⚠  IN CW — CAN'T TUNE"));
                    mcChip->setStyleSheet(QString::fromLatin1(kChipRed));
                } else if (drvPct < 100) {
                    mcDrive->setText(tr("⚠ %1: %2% — set it to 100% (full) before "
                        "you calibrate.").arg(drvSrc).arg(drvPct));
                    mcDrive->setStyleSheet(amber);
                    mcChip->setText(tr("⚠  %1% — NOT FULL").arg(drvPct));
                    mcChip->setStyleSheet(QString::fromLatin1(kChipAmber));
                } else {
                    mcDrive->setText(tr("%1: 100% — full ✓, ready to calibrate.").arg(drvSrc));
                    mcDrive->setStyleSheet(QStringLiteral("color:#4ccf6b; font-weight:600;"));
                    mcChip->setText(tr("READY ✓"));
                    mcChip->setStyleSheet(QString::fromLatin1(kChipGreen));
                }
            }
            const int    b     = lyra::bandIndexForFreq(
                                     static_cast<int>(stream_->rx1FreqHz()));
            const bool   keyed = stream_->moxActive() || stream_->cwKeyingActive();
            const auto  &bs    = lyra::amateurBands();
            if (b < 0) {
                mcLive->setText(
                    tr("Tune to an amateur band, TUN a full-drive carrier, "
                       "then Calibrate."));
            } else {
                const QString bn   = QString::fromUtf8(bs[b].name);
                const double  live = stream_->fwdPowerW();
                if (keyed && !std::isnan(live) && live >= 0.25) {
                    const bool full = stream_->txDriveLevel() >= 255;
                    mcLive->setText(
                        tr("Transmitting on %1 — Lyra reads %2 W raw%3.")
                            .arg(bn).arg(live, 0, 'f', 1)
                            .arg(full ? tr(" — full drive ✓")
                                      : tr(" — go to FULL drive to set Full Output")));
                } else {
                    const double cap = stream_->capturedRawWForBand(b);
                    if (cap > 0.0)
                        mcLive->setText(
                            tr("Captured %1 W raw on %2%3 — enter your "
                               "watt-meter reading and Calibrate.")
                                .arg(cap, 0, 'f', 1).arg(bn)
                                .arg(stream_->capturedDriveForBand(b) >= 255
                                         ? tr(" (full drive ✓)")
                                         : tr(" (NOT full drive)")));
                    else
                        mcLive->setText(
                            tr("On %1 — TUN a full-drive carrier to capture a "
                               "reading.").arg(bn));
                }
            }
            for (int i = 0; i < trimLabels->size(); ++i) {
                const double t   = stream_->pwrTrimForBand(i);
                QLabel      *lab = (*trimLabels)[i];
                if (std::abs(t - 1.0) < 1e-3) {
                    lab->setText(tr("—"));
                    lab->setStyleSheet(QString());
                } else {
                    lab->setText(QStringLiteral("×%1 ✓").arg(t, 0, 'f', 3));
                    lab->setStyleSheet(QStringLiteral("color:#4ccf6b;"));
                }
            }
        };
        mcRefresh();
        auto *mcTimer = new QTimer(page);
        mcTimer->setInterval(500);
        connect(mcTimer, &QTimer::timeout, page, mcRefresh);
        mcTimer->start();
        connect(page, &QObject::destroyed, [trimLabels]() { delete trimLabels; });
    }

    root->addStretch(1);

    // Stage B — refresh the per-band "tuned" marks live (so the operator
    // sees a band lock as they TUN it).  Timer parented to the page so it
    // stops + is destroyed with the dialog; the captured label list is
    // freed on the page's destruction.
    auto refresh = [this, tunedLabels, capChk]() {
        const bool capOn = capChk->isChecked();
        for (int i = 0; i < tunedLabels->size(); ++i) {
            const bool tuned = capOn && stream_ && stream_->capTunedForBand(i);
            QLabel *lab = (*tunedLabels)[i];
            lab->setText(capOn ? (tuned ? tr("✓") : tr("—")) : QString());
            // Green ✓ = locked at the cap; red — = not yet tuned for this cap.
            lab->setStyleSheet(
                !capOn ? QString()
                       : (tuned ? QStringLiteral("color:#4ccf6b;font-weight:bold;")
                                : QStringLiteral("color:#e5564e;font-weight:bold;")));
        }
    };
    refresh();
    auto *timer = new QTimer(page);
    timer->setInterval(600);
    connect(timer, &QTimer::timeout, page, refresh);
    timer->start();
    connect(page, &QObject::destroyed, [tunedLabels]() { delete tunedLabels; });

    return page;
}

QWidget *SettingsDialog::buildTxTab() {
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);

    // §15.28 — 2-column layout with column headers.  LEFT = timing
    // (TR Sequencing + Amplitude Envelope + Tune — set-once values
    // an operator configures for their amp + station and rarely
    // touches again).  RIGHT = audio/gain (Mic + ALC + Leveler —
    // the operating controls an operator actively tunes during a
    // session).  Restore-defaults button + footer description text
    // span both columns at the bottom (root-level layout below the
    // body row).
    {
        // §15.28 — 3-column header row (see the column assignment below).
        auto *colHeaders = new QHBoxLayout();
        colHeaders->setSpacing(14);
        auto addHeader = [page, colHeaders](const QString &title,
                                            const QString &sub) {
            auto *box = new QVBoxLayout();
            box->setSpacing(0);
            auto *h = new QLabel(title, page);
            h->setProperty("lyraColHeader", true);
            box->addWidget(h);
            auto *s = new QLabel(sub, page);
            s->setProperty("lyraColSubtitle", true);
            box->addWidget(s);
            colHeaders->addLayout(box, 1);
        };
        addHeader(tr("TIMING + SAFETY"), tr("set once for your amp + station"));
        addHeader(tr("AUDIO + GAIN"),    tr("operating gain stages"));
        addHeader(tr("TUNE + MODES"),    tr("tune + per-mode settings"));
        root->addLayout(colHeaders);
    }

    // §15.28 — 3-column body.  col1 = TIMING + SAFETY (set-once: TR
    // sequencing, ATT-on-TX, external inhibit, SWR protect); col2 =
    // AUDIO + GAIN (mic + ALC, leveler, PHROT); col3 = TUNE + MODES
    // (tune, WF-ID, AM carrier, FM).  `leftCol`/`rightCol` names kept so
    // the col1/col2 groups didn't have to move.
    auto *bodyRow  = new QHBoxLayout();
    bodyRow->setSpacing(14);
    auto *leftCol  = new QVBoxLayout();   // col 1
    auto *rightCol = new QVBoxLayout();   // col 2
    auto *col3     = new QVBoxLayout();   // col 3
    leftCol->setSpacing(10);
    rightCol->setSpacing(10);
    col3->setSpacing(10);
    bodyRow->addLayout(leftCol,  1);
    bodyRow->addLayout(rightCol, 1);
    bodyRow->addLayout(col3,     1);
    root->addLayout(bodyRow);

    // Common spin-box bounds — match HL2Stream::kMin/kMaxFsmDelayMs
    // (deliberately literal here so the settings UI doesn't have to
    // reach into private class constants).
    constexpr int kMinMs = 1;
    constexpr int kMaxMs = 500;

    // Helper: builds a labelled spin box bound to a stream getter/
    // setter pair + change signal.  Bidirectional — operator value
    // change calls the setter; an external (e.g. Restore Defaults)
    // setter call updates the spin box via the signal.
    auto makeSpin = [this](int initial, int minMs, int maxMs,
                           const QString &tooltip)
        -> QSpinBox * {
        auto *sb = new QSpinBox(this);
        sb->setRange(minMs, maxMs);
        sb->setSuffix(tr(" ms"));
        sb->setValue(initial);
        sb->setToolTip(tooltip);
        return sb;
    };

    // ── TR Sequencing group ────────────────────────────────────
    {
        auto *grp = new QGroupBox(
            tr("TR Sequencing  (PTT → wire-MOX → RF timing)"), page);
        auto *form = new QFormLayout(grp);

        auto *moxSpin = makeSpin(
            stream_->moxDelayMs(), kMinMs, kMaxMs,
            tr("Time from operator-PTT to the wire MOX bit going hot.\n"
               "Gives the RX-protect step-att + external filter-board "
               "relays a window to settle into TX configuration before "
               "any RF appears.\n\n"
               "Default 15 ms (bench-validated for typical HL2+ + "
               "external filter board)."));
        connect(moxSpin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int v) { stream_->setMoxDelayMs(v); });
        connect(stream_, &lyra::ipc::HL2Stream::moxDelayMsChanged,
                moxSpin, [moxSpin](int v) {
                    if (moxSpin->value() != v) moxSpin->setValue(v);
                });
        form->addRow(tr("MOX Delay:"), moxSpin);

        auto *rfSpin = makeSpin(
            stream_->rfDelayMs(), kMinMs, kMaxMs,
            tr("⚠ HOT-SWITCH PROTECTION — load-bearing for external "
               "solid-state HF linear amps.\n\n"
               "Time from wire MOX bit going hot to the 'RF settled' "
               "edge (when actual RF appears on the antenna).  Gives "
               "the external amp's T/R relay enough settle time before "
               "RF hits — typical SS HF linears need 30-50 ms.\n\n"
               "Default 50 ms is bench-safe for typical 1 kW SS HF "
               "linears.  REDUCING THIS BELOW YOUR AMP'S T/R RELAY "
               "SETTLE SPEC CAN DESTROY THE PA via hot-switching "
               "RF into mid-transition relays."));
        connect(rfSpin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int v) { stream_->setRfDelayMs(v); });
        connect(stream_, &lyra::ipc::HL2Stream::rfDelayMsChanged,
                rfSpin, [rfSpin](int v) {
                    if (rfSpin->value() != v) rfSpin->setValue(v);
                });
        form->addRow(tr("RF Delay:"), rfSpin);

        auto *spaceSpin = makeSpin(
            stream_->spaceMoxDelayMs(), kMinMs, kMaxMs,
            tr("Keyup re-key window — time after operator-keyup before "
               "the wire MOX bit actually clears.  Allows mic-clip / CW-"
               "dot-tail re-keying within this window to collapse-stay-"
               "TX (no on-the-air drop, no extra T/R cycle).\n\n"
               "Default 13 ms.  Sets the upper bound on Fade-Out "
               "Duration below — fade-out must fit inside this window "
               "or it gets truncated at MOX-clear."));
        connect(spaceSpin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int v) { stream_->setSpaceMoxDelayMs(v); });
        connect(stream_, &lyra::ipc::HL2Stream::spaceMoxDelayMsChanged,
                spaceSpin, [spaceSpin](int v) {
                    if (spaceSpin->value() != v) spaceSpin->setValue(v);
                });
        form->addRow(tr("Space MOX Delay:"), spaceSpin);

        auto *pttSpin = makeSpin(
            stream_->pttOutDelayMs(), kMinMs, kMaxMs,
            tr("Final cleanup window after wire MOX clears — time "
               "before step-att restores + OC pattern flips back to RX "
               "+ moxActive_=false emits.  Lets external relays finish "
               "switching back before the RX front-end re-opens.\n\n"
               "Default 5 ms (bench-validated)."));
        connect(pttSpin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int v) { stream_->setPttOutDelayMs(v); });
        connect(stream_, &lyra::ipc::HL2Stream::pttOutDelayMsChanged,
                pttSpin, [pttSpin](int v) {
                    if (pttSpin->value() != v) pttSpin->setValue(v);
                });
        form->addRow(tr("PTT-Out Delay:"), pttSpin);

        auto *txStopSpin = makeSpin(
            stream_->txStopDelayMs(), kMinMs, kMaxMs,
            tr("In-flight UDP datagram clear window — time between "
               "the TX-DSP channel stopping (blocking flush) and "
               "the wire MOX bit clearing on keyup.\n\n"
               "Datagrams already-sent or in your OS network buffer "
               "(still carrying MOX=1 + non-zero TX I/Q) need time "
               "to actually reach + be processed by the HL2 BEFORE "
               "the wire MOX state flips — otherwise the gateware "
               "could see momentary MOX=0 with stale TX I/Q from a "
               "prior keyed-state datagram.\n\n"
               "Default 10 ms is the verified-reference value.  "
               "Reduce only on a very low-latency NIC + LAN where "
               "you're confident no datagrams sit in OS buffer."));
        connect(txStopSpin, qOverload<int>(&QSpinBox::valueChanged), this,
                [this](int v) { stream_->setTxStopDelayMs(v); });
        connect(stream_, &lyra::ipc::HL2Stream::txStopDelayMsChanged,
                txStopSpin, [txStopSpin](int v) {
                    if (txStopSpin->value() != v) txStopSpin->setValue(v);
                });
        form->addRow(tr("TX-Stop Delay:"), txStopSpin);

        leftCol->addWidget(grp);   // §15.28 — TIMING column
    }

    // §3.9-5 revert (operator-rejected 2026-06-06): the "Amplitude
    // Envelope" group + Fade-In / Fade-Out spin boxes were removed.
    // Reference does not envelope-shape SSB TX I/Q (WDSP TXAUslewCheck
    // returns 0 for SSB modes).  Hot-switch protection for external
    // linear amps now relies SOLELY on the RF Delay control above —
    // matches the reference's mechanism.

    // ── Tune (TUN drive mode, Task #74 / #95) ───────────────────
    // 3-way selector for what drive level the TUN button keys at:
    //   0 Use TX Drive slider — TUN keys at your live TX Drive %.
    //   1 Use Tune slider     — TUN keys at the per-band Tune Drive %
    //                           (live-adjustable; recalled per band).
    //   2 Use fixed drive     — TUN keys at a single Fixed Drive %,
    //                           the same regardless of band or slider.
    // For modes 1/2 the pre-tune drive is restored on tune-release.
    // Live: changing the active control takes effect immediately while
    // armed, and otherwise on the next TUN arm.
    if (prefs_) {
        auto *grp = new QGroupBox(tr("Tune  (TUN button)"), page);
        auto *form = new QFormLayout(grp);

        auto *modeBox = new QComboBox(grp);
        modeBox->addItem(tr("Use TX Drive slider"),
                         int(lyra::ui::Prefs::TuneDriveSlider));
        modeBox->addItem(tr("Use Tune slider"),
                         int(lyra::ui::Prefs::TuneDriveTune));
        modeBox->addItem(tr("Use fixed drive"),
                         int(lyra::ui::Prefs::TuneDriveFixed));
        modeBox->setCurrentIndex(
            std::max(0, modeBox->findData(prefs_->tuneDriveMode())));
        modeBox->setToolTip(tr(
            "What power the TUN button transmits at:\n"
            "• Use TX Drive slider — same as your normal TX Drive %%.\n"
            "• Use Tune slider — the per-band Tune Drive %% below "
            "(live-adjustable, remembered per band).\n"
            "• Use fixed drive — the Fixed Drive %% below, the same on "
            "every band regardless of either slider.\n\n"
            "Modes other than 'TX Drive slider' restore your TX Drive %% "
            "when you un-key TUN."));
        form->addRow(tr("Tune drive:"), modeBox);

        // TUN can never exceed the Max-TX-drive ceiling (the wire clamps
        // it in HL2Stream::setTxDriveLevel); cap these spinners to that
        // ceiling so the UI never shows a level that can't be reached.
        const int driveCap = stream_ ? stream_->maxDrivePct() : 100;
        auto *tuneSpin = new QSpinBox(grp);
        tuneSpin->setRange(0, driveCap);
        tuneSpin->setSuffix(tr(" %"));
        tuneSpin->setValue(prefs_->tuneDrivePct());
        tuneSpin->setToolTip(tr(
            "Per-band tune slider value, used in 'Use Tune slider' mode. "
            "Bench-safe 25 %% into a dummy load; raise as you trust the "
            "tuner / antenna / amp.  Remembered per band."));
        form->addRow(tr("Tune slider:"), tuneSpin);

        auto *fixedSpin = new QSpinBox(grp);
        fixedSpin->setRange(0, driveCap);
        fixedSpin->setSuffix(tr(" %"));
        fixedSpin->setValue(prefs_->fixedTuneDrivePct());
        fixedSpin->setToolTip(tr(
            "Fixed tune drive, used in 'Use fixed drive' mode.  Applied "
            "on every band regardless of the sliders."));
        form->addRow(tr("Fixed drive:"), fixedSpin);

        // Enable only the spin the current mode actually uses.
        auto applyEnables = [tuneSpin, fixedSpin](int mode) {
            tuneSpin->setEnabled(mode == int(lyra::ui::Prefs::TuneDriveTune));
            fixedSpin->setEnabled(mode == int(lyra::ui::Prefs::TuneDriveFixed));
        };
        applyEnables(prefs_->tuneDriveMode());

        connect(modeBox, qOverload<int>(&QComboBox::currentIndexChanged), grp,
                [this, modeBox, applyEnables](int) {
            const int m = modeBox->currentData().toInt();
            if (prefs_) prefs_->setTuneDriveMode(m);
            applyEnables(m);
        });
        connect(tuneSpin, qOverload<int>(&QSpinBox::valueChanged), grp,
                [this](int v) { if (prefs_) prefs_->setTuneDrivePct(v); });
        connect(fixedSpin, qOverload<int>(&QSpinBox::valueChanged), grp,
                [this](int v) { if (prefs_) prefs_->setFixedTuneDrivePct(v); });

        // External-update mirrors so this dialog, the TxPanel.qml inline
        // stepper, and TX Profile recall (future #49) stay in lockstep.
        connect(prefs_, &lyra::ui::Prefs::tuneDriveModeChanged,
                modeBox, [this, modeBox, applyEnables]() {
            const int i = modeBox->findData(prefs_->tuneDriveMode());
            if (i >= 0 && modeBox->currentIndex() != i) {
                QSignalBlocker b(modeBox);
                modeBox->setCurrentIndex(i);
            }
            applyEnables(prefs_->tuneDriveMode());
        });
        connect(prefs_, &lyra::ui::Prefs::tuneDrivePctChanged,
                tuneSpin, [this, tuneSpin]() {
            const int v = prefs_->tuneDrivePct();
            if (tuneSpin->value() != v) {
                QSignalBlocker b(tuneSpin);
                tuneSpin->setValue(v);
            }
        });
        connect(prefs_, &lyra::ui::Prefs::fixedTuneDrivePctChanged,
                fixedSpin, [this, fixedSpin]() {
            const int v = prefs_->fixedTuneDrivePct();
            if (fixedSpin->value() != v) {
                QSignalBlocker b(fixedSpin);
                fixedSpin->setValue(v);
            }
        });

        // Track the Max-TX-drive ceiling (#170a) live: capping the
        // spinners' maximum here keeps them from showing/entering a level
        // the wire would clamp anyway.  Lowering Max-drive below a tune
        // value pulls that value down (QSpinBox clamps + the valueChanged
        // above writes it back); raising it re-opens the headroom.
        if (stream_) {
            connect(stream_, &lyra::ipc::HL2Stream::maxDrivePctChanged,
                    grp, [tuneSpin, fixedSpin](int cap) {
                tuneSpin->setMaximum(cap);
                fixedSpin->setMaximum(cap);
            });
        }

        col3->addWidget(grp);      // §15.28 — TUNE + MODES column (Tune)
    }

    // ── Mic + ALC (TXA input/output gain stages) group ───────────
    // TX-1 component 8a — operator-tunable WDSP TXA gain stages.
    //
    // Layout mirrors the reference's Setup → Transmit → Mic + ALC
    // grouping: backend spin-box entry that bidirectionally tracks
    // the front-UI slider (TxPanel mic-gain slider) so either
    // surface tunes the same QSettings tx/micGainDb value.  Operator
    // can drag on the panel for quick QSO-time adjustments OR type a
    // precise value here in Settings.  Both are the same control;
    // valueChanged on either side updates the other.
    //
    // ⚠ ALC IS LOAD-BEARING: WDSP create-time default for ALC max-gain
    // is 1.0 linear = 0 dB, which pins the entire TXA output chain at
    // a hard 0-dB ceiling regardless of mic level.  The verified
    // reference bootstraps to +3 dB at first Setup load (its profile
    // default); lyra-cpp never called the setter before component 8a,
    // which is the actual root cause of the 2026-05-31 first-SSB
    // bench result (0.2 W peak at 100 % drive with normal mic levels).
    // Default +3 dB here mirrors the reference's Setup-load value.
    // Operator can tune in [-3, +10] dB; outside that range either
    // clips program material (low) or defeats splatter protection
    // (high).  NOT included in the Restore hot-switch-safe-defaults
    // button below — that button is scoped to the 7 TR-sequencing
    // values; ALC ceiling + Mic Gain are independent operator-
    // tunable settings.
    {
        auto *grp = new QGroupBox(
            tr("Mic + ALC  (TXA input + output gain stages)"), page);
        auto *form = new QFormLayout(grp);

        // Signal-flow order in this group:
        //   source → HW boost (+20 dB) → SW gain → ALC ceiling
        // Mic source picker + Mic Boost live here (not on the
        // Hardware tab) because they are part of the same
        // operator question as Mic Gain / ALC Max Gain: "what's
        // driving the TX chain and at what level?"  Co-locating
        // them under the TX tab matches the audible-signal path.

        // ── Mic source (Task #33: TX Mic Source picker) ──────────
        //
        // Operator picks the audio source driving the TX chain:
        //   Mic In  — HL2/HL2+ codec mic (the v0.2.0..v0.2.2 default)
        //   TCI     — inbound TX_AUDIO_STREAM from a digital-modes
        //             TCI client (MSHV / JTDX / FlDigi / etc.)
        //   Line In / VAC1 / VAC2 — pending v0.2.x (disabled but
        //             visible so the dropdown layout is final;
        //             tooltip explains each).
        //
        // Token strings match the TCI v2 §3.3 TRX source-token enum
        // so a TCI client that sends `trx:0,true,tci` automatically
        // selects the TCI source (the dropdown moves to match — the
        // operator sees the change and can revert via Settings if
        // surprised).
        if (prefs_) {
            auto *combo = new QComboBox(grp);
            const QStringList toks = lyra::ui::Prefs::micSourceTokens();
            for (int i = 0; i < toks.size(); ++i) {
                const QString &t = toks.at(i);
                combo->addItem(lyra::ui::Prefs::micSourceLabel(t), t);
                combo->setItemData(i,
                    lyra::ui::Prefs::micSourceTooltip(t), Qt::ToolTipRole);
                if (!lyra::ui::Prefs::micSourceEnabled(t)) {
                    // Render the disabled items grey + non-selectable.
                    auto *model = qobject_cast<QStandardItemModel *>(combo->model());
                    if (model) {
                        QStandardItem *it = model->item(i);
                        if (it) it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
                    }
                }
            }
            // Set current.
            {
                const int idx = toks.indexOf(prefs_->micSource());
                if (idx >= 0) combo->setCurrentIndex(idx);
            }
            combo->setToolTip(tr(
                "TX audio source.  Pick TCI for digital modes — your TCI "
                "client (MSHV / JTDX / FlDigi) streams audio over the TCI "
                "WebSocket and bypasses the mic.  Line In / VAC1 / VAC2 "
                "are spec'd in v0.2.x; hover any entry for its status."));
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                    grp, [this, combo](int) {
                if (!prefs_) return;
                const QString tok = combo->currentData().toString();
                prefs_->setMicSource(tok);
            });
            connect(prefs_, &lyra::ui::Prefs::micSourceChanged,
                    combo, [this, combo, toks]() {
                if (!prefs_) return;
                const int idx = toks.indexOf(prefs_->micSource());
                if (idx >= 0 && combo->currentIndex() != idx) {
                    QSignalBlocker b(combo);
                    combo->setCurrentIndex(idx);
                }
            });
            form->addRow(tr("Mic source:"), combo);
        }

        // ── Mic Boost (Task #39: HL2 hardware +20 dB) ────────────
        // Single bit on the wire (C0 0x12 C2 bit 0) engaging the HL2
        // codec's analog mic PGA at +20 dB — pure hardware boost
        // ahead of the digital chain.  Useful when your hand mic is
        // genuinely weak; for fine trim use the Mic Gain slider
        // below (it stacks on top of the hardware boost).  No
        // safety implication, no MOX gating; persisted.  Only
        // affects the codec-mic source — PC mic / TCI inputs bypass
        // the codec entirely.
        if (stream_) {
            auto *mbBox = new QCheckBox(
                tr("Mic Boost (+20 dB hardware, codec mic only)"), grp);
            mbBox->setChecked(stream_->micBoost());
            mbBox->setToolTip(tr(
                "Enables the HL2 codec's hardware +20 dB mic preamp "
                "(C0 0x12 C2 bit 0).  Use this when your hand mic or "
                "headset mic is too quiet to hit the WDSP TXA chain "
                "at a reasonable level even with the Mic Gain slider "
                "near max.  Only affects the codec mic input — PC "
                "mic / TCI audio sources bypass the codec PGA "
                "entirely, so this checkbox has no effect on them. "
                "Hardware is 2-state (off / +20 dB); intermediate "
                "trim comes from the Mic Gain slider stacked on "
                "top.  Persistent across launches."));
            connect(mbBox, &QCheckBox::toggled, grp, [this](bool on) {
                if (stream_) stream_->setMicBoost(on);
            });
            connect(stream_, &lyra::ipc::HL2Stream::micBoostChanged, mbBox,
                    [mbBox](bool on) {
                if (mbBox->isChecked() != on) {
                    QSignalBlocker b(mbBox);
                    mbBox->setChecked(on);
                }
            });
            form->addRow(mbBox);
        }

        // ── Mic Gain (mirrors TxPanel slider) ────────────────────
        auto *micSpin = new QDoubleSpinBox(this);
        micSpin->setRange(-90.0, 40.0);   // matches kMinMicGainDb / kMaxMicGainDb
        micSpin->setSingleStep(1.0);
        micSpin->setDecimals(1);
        micSpin->setSuffix(tr(" dB"));
        micSpin->setValue(stream_->micGainDb());
        micSpin->setToolTip(tr(
            "Mic gain into the WDSP TXA modulator (PanelGain1, TXA "
            "chain stage #3 — the only operator-tunable software gain "
            "stage in the chain).  0 dB = unity (WDSP create-time "
            "default).  Typical SSB voice runs +10 to +20 dB; ESSB "
            "with headroom +25 to +35 dB.\n\n"
            "Range -90 dB to +40 dB matches the reference's Default TX "
            "profile.  -90 dB is essentially mute; +40 dB is the top "
            "of the operator-tunable scale.\n\n"
            "Bidirectional binding with the front-UI TxPanel Mic Gain "
            "slider — moving either updates both.  Type here for a "
            "precise value; drag the slider for quick QSO-time "
            "adjustments.  Live-apply: takes effect on the next "
            "~2.6 ms TXA process block."));
        connect(micSpin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double v) {
                    stream_->setMicGainDb(v);
                });
        connect(stream_, &lyra::ipc::HL2Stream::micGainDbChanged,
                micSpin, [micSpin](double v) {
                    if (micSpin->value() != v) micSpin->setValue(v);
                });
        form->addRow(tr("Mic Gain:"), micSpin);

        // ── ALC Max Gain (output limiter ceiling) ────────────────
        // §15.27: WDSP `SetTXAALCMaxGain` takes a LINEAR amplitude
        // factor (NOT dB).  Verified reference exposes this as an
        // integer spinner 0..120 incr 1 default 3, passed straight
        // through with no unit conversion.  Lyra mirrors that
        // EXACTLY — operator coming from a reference-pattern radio
        // sees identical units, range, and default.
        auto *alcSpin = new QDoubleSpinBox(this);
        alcSpin->setRange(0.0, 120.0);
        alcSpin->setSingleStep(1.0);
        alcSpin->setDecimals(0);
        alcSpin->setSuffix(QString());     // no unit suffix — linear factor
        alcSpin->setValue(stream_->alcMaxGainLinear());
        alcSpin->setToolTip(tr(
            "ALC (Automatic Level Control) Max Gain — the maximum "
            "amplitude factor the always-on output limiter will "
            "amplify the TX signal by before clamping.  LINEAR "
            "factor (NOT dB): 1 = unity (limiter cannot amplify, "
            "only attenuate); 3 = the reference's default = 3× "
            "amplitude headroom = +9.54 dB of allowed amplification "
            "before the ALC pulls down.\n\n"
            "Default 3 matches the verified reference's Setup-load "
            "value EXACTLY (integer spinner 0..120 incr 1 default 3 "
            "passed straight through to the WDSP API).  WDSP's "
            "create-time default is 1 (= 0 dB, no amplification "
            "allowed) which pins the entire TX output chain at a "
            "hard 0 dB ceiling regardless of mic level — that was "
            "the root cause of the task #79 whistle-vs-reference "
            "RF gap (see CLAUDE.md §15.27 for the units-mismatch "
            "history).\n\n"
            "Operator tuning: lower (e.g. 1 to 2) for tighter "
            "splatter protection at the cost of headroom; higher "
            "(e.g. 5 to 20) for ESSB-style program-level headroom "
            "at the cost of splatter-protection margin.  Range "
            "0..120 matches the reference's spinner exactly."));
        connect(alcSpin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double v) {
                    stream_->setAlcMaxGainLinear(v);
                });
        connect(stream_, &lyra::ipc::HL2Stream::alcMaxGainLinearChanged,
                alcSpin, [alcSpin](double v) {
                    if (alcSpin->value() != v) alcSpin->setValue(v);
                });
        form->addRow(tr("ALC Max Gain:"), alcSpin);

        // ── ALC Decay (wcpagc release time constant) ─────────────
        // §15.27 Commit B.  Integer spinner 1..50 ms incr 1 default 10
        // — matches the verified reference UI exactly.  Attack stays
        // at the WDSP create-time default (1 ms) and is not exposed
        // (the reference UI doesn't expose attack either).
        auto *alcDecaySpin = new QSpinBox(this);
        alcDecaySpin->setRange(1, 50);
        alcDecaySpin->setSingleStep(1);
        alcDecaySpin->setSuffix(tr(" ms"));
        alcDecaySpin->setValue(stream_->alcDecayMs());
        alcDecaySpin->setToolTip(tr(
            "ALC decay time constant (exponential-curve tau in ms; "
            "NOT an absolute time).  Sets how quickly the always-on "
            "output limiter releases gain reduction after a peak.\n\n"
            "Default 10 ms matches the verified reference's Setup-load "
            "value EXACTLY.  Operator tuning: lower (e.g. 3-5 ms) for "
            "snappier release that lets program material breathe but "
            "can sound 'pumpy' on aggressive content; higher (e.g. "
            "30-50 ms) for smoother release that holds gain steadier "
            "but compresses dynamics more.  Range 1..50 ms matches "
            "the reference's spinner exactly."));
        connect(alcDecaySpin,
                qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v) {
                    stream_->setAlcDecayMs(v);
                });
        connect(stream_, &lyra::ipc::HL2Stream::alcDecayMsChanged,
                alcDecaySpin, [alcDecaySpin](int v) {
                    if (alcDecaySpin->value() != v) alcDecaySpin->setValue(v);
                });
        form->addRow(tr("ALC Decay:"), alcDecaySpin);

        rightCol->addWidget(grp);   // §15.28 — AUDIO + GAIN column
    }

    // ── Leveler (TXA pre-ALC amplifier stage) group ──────────────
    // §15.27 Commit B.  New operator-facing subgroup, sibling of
    // "Mic + ALC", exposing the WDSP TXA Leveler that was wired in
    // the chain but never had Settings controls.  Three controls
    // match the verified reference's primary Leveler surface
    // (Enable + Max Gain + Decay) — Attack + Hang stay at the
    // WDSP create-time defaults (1 ms / 500 ms) and are not
    // exposed.  Defaults: OFF (operator preference override of
    // reference's default ON, per explicit PS/predistortion
    // quality concern); when enabled, 15 LINEAR / 100 ms matches
    // the reference's UI ship-defaults EXACTLY.
    if (stream_) {
        auto *grp = new QGroupBox(
            tr("Leveler  (TXA pre-ALC amplifier stage)"), page);
        auto *form = new QFormLayout(grp);

        // ── Enable checkbox ──────────────────────────────────────
        auto *enBox = new QCheckBox(
            tr("Enabled  (boosts weak signals pre-ALC)"), grp);
        enBox->setChecked(stream_->levelerOn());
        enBox->setToolTip(tr(
            "Leveler is a pre-ALC amplifier stage that boosts weak "
            "input signals up toward unity before the always-on ALC "
            "limiter sees them — particularly helpful for quiet "
            "passages of voice or for ESSB operators who want "
            "consistent on-air loudness without riding the mic "
            "gain.\n\n"
            "Default OFF in Lyra (operator preference: leveler can "
            "interact with predistortion / PureSignal calibration).  "
            "The verified reference's UI ships this checkbox ON by "
            "default — turn ON here to match reference behaviour."));
        connect(enBox, &QCheckBox::toggled, this, [this](bool on) {
            if (stream_) stream_->setLevelerOn(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::levelerOnChanged, enBox,
                [enBox](bool on) {
                    if (enBox->isChecked() != on) enBox->setChecked(on);
                });
        form->addRow(enBox);

        // ── Max Gain spinner (LINEAR) ────────────────────────────
        // Reference UI: integer spinner 0..20 LINEAR incr 1
        // default 15.  Same LINEAR-not-dB semantics as the ALC
        // Max Gain (the API takes LINEAR; reference UI label says
        // "(dB)" but that label is misleading — value is passed
        // straight through with no conversion).
        auto *lvlSpin = new QDoubleSpinBox(grp);
        lvlSpin->setRange(0.0, 20.0);
        lvlSpin->setSingleStep(1.0);
        lvlSpin->setDecimals(0);
        lvlSpin->setSuffix(QString());     // no unit — LINEAR factor
        lvlSpin->setValue(stream_->levelerMaxGainLinear());
        lvlSpin->setToolTip(tr(
            "Leveler Max Gain — the maximum amplitude factor the "
            "pre-ALC leveler will amplify weak signals by.  LINEAR "
            "factor (NOT dB): 1 = unity (no boost); 15 = the "
            "reference's default = 15× amplitude ceiling = +23.5 dB "
            "amplification headroom for weak signals.\n\n"
            "Default 15 matches the verified reference's Setup-load "
            "value EXACTLY (integer spinner 0..20 incr 1 default 15 "
            "passed straight through to the WDSP API).  Range 0..20 "
            "matches the reference's spinner exactly.\n\n"
            "Operator tuning: lower (e.g. 5-10) for gentler boost "
            "that preserves natural mic dynamics; higher (e.g. "
            "15-20) for ESSB-style consistent loudness.  Has effect "
            "only when the Enabled checkbox above is ticked."));
        connect(lvlSpin,
                qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double v) {
                    stream_->setLevelerMaxGainLinear(v);
                });
        connect(stream_, &lyra::ipc::HL2Stream::levelerMaxGainLinearChanged,
                lvlSpin, [lvlSpin](double v) {
                    if (lvlSpin->value() != v) lvlSpin->setValue(v);
                });
        form->addRow(tr("Max Gain:"), lvlSpin);

        // ── Decay spinner (ms) ───────────────────────────────────
        // Reference UI: integer spinner 1..5000 ms incr 1 default
        // 100.  Exponential-curve tau, not absolute time.
        auto *lvlDecaySpin = new QSpinBox(grp);
        lvlDecaySpin->setRange(1, 5000);
        lvlDecaySpin->setSingleStep(1);
        lvlDecaySpin->setSuffix(tr(" ms"));
        lvlDecaySpin->setValue(stream_->levelerDecayMs());
        lvlDecaySpin->setToolTip(tr(
            "Leveler decay time constant (exponential-curve tau in ms; "
            "NOT an absolute time).  Sets how quickly the leveler "
            "releases its boost after a stronger signal subsides.\n\n"
            "Default 100 ms matches the verified reference's Setup-load "
            "value EXACTLY.  Operator tuning: lower (e.g. 30-50 ms) "
            "for faster release that follows voice envelope more "
            "tightly; higher (e.g. 200-500 ms) for smoother release "
            "that holds boost steadier across syllables.  Range "
            "1..5000 ms matches the reference's spinner exactly.\n\n"
            "Has effect only when the Enabled checkbox above is ticked."));
        connect(lvlDecaySpin,
                qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v) {
                    stream_->setLevelerDecayMs(v);
                });
        connect(stream_, &lyra::ipc::HL2Stream::levelerDecayMsChanged,
                lvlDecaySpin, [lvlDecaySpin](int v) {
                    if (lvlDecaySpin->value() != v) lvlDecaySpin->setValue(v);
                });
        form->addRow(tr("Decay:"), lvlDecaySpin);

        rightCol->addWidget(grp);   // §15.28 — AUDIO + GAIN column (Leveler)
    }

    // ── #109 Phase Rotator (PHROT) group ─────────────────────────
    // WDSP TXA speech processor that symmetrizes asymmetric voice
    // waveforms (lowers peak-to-average → more average talk power for the
    // same ALC ceiling).  Mirrors the reference's Setup PHROT checkbox.
    // AUDIO + GAIN column (it's a voice processor).
    if (stream_) {
        auto *grp = new QGroupBox(tr("Phase Rotator (PHROT)"), page);
        auto *form = new QFormLayout(grp);

        auto *enBox = new QCheckBox(
            tr("Enabled  (symmetrize speech for more talk power)"), grp);
        enBox->setChecked(stream_->phrotEnabled());
        enBox->setToolTip(tr(
            "Phase rotator — an all-pass network that evens out the "
            "asymmetry of speech waveforms, lowering their peak-to-average "
            "ratio so more average power gets through for the same ALC/peak "
            "ceiling. Helpful on SSB voice.\n\n"
            "Auto-disabled in digital modes (DIGU/DIGL) — phase rotation "
            "distorts FT8/FT4/RTTY/etc., so this checkbox is your voice-mode "
            "intent and Lyra switches it off for you in digital. Default ON "
            "(matches the reference posture)."));
        connect(enBox, &QCheckBox::toggled, this, [this](bool on) {
            if (stream_) stream_->setPhrotEnabled(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::phrotEnabledChanged, enBox,
                [enBox](bool on) {
                    if (enBox->isChecked() != on) enBox->setChecked(on);
                });
        form->addRow(enBox);

        rightCol->addWidget(grp);   // §15.28 — AUDIO + GAIN column
    }

    // ── Waterfall ID (#175) group ────────────────────────────────
    // Self-keyed callsign raster in the SSB passband.  ARM it from the header
    // WF-ID chip (USB/LSB only — in digital the call is already in the mode's
    // payload).  These are the config knobs; the live Send button + armed
    // indicator live on the TX dock panel.  Your callsign comes from
    // Settings → Hardware → Operator.  AUDIO + GAIN column.
    if (prefs_) {
        auto *grp  = new QGroupBox(
            tr("Waterfall ID  (USB/LSB callsign raster)"), page);
        auto *form = new QFormLayout(grp);

        // Burst level — clamped low ON PURPOSE (full-duty multitone).
        auto *lvlSpin = new QDoubleSpinBox(grp);
        lvlSpin->setRange(0.0, 0.065);
        lvlSpin->setSingleStep(0.005);
        lvlSpin->setDecimals(3);
        lvlSpin->setValue(prefs_->wfIdLevel());
        lvlSpin->setToolTip(tr(
            "Waterfall-ID transmit level.  Held low on purpose: a waterfall ID "
            "is continuous full-duty multitone, so a hot level splatters (IMD) "
            "and stresses a solid-state amp like full-power digital.  The 0.065 "
            "cap ≈ ~¼ digital drive.  Courtesy ID only — does NOT "
            "replace your legal voice/CW station identification."));
        connect(lvlSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double v) {
                    if (prefs_) prefs_->setWfIdLevel(v);
                });
        connect(prefs_, &Prefs::wfIdLevelChanged, lvlSpin, [this, lvlSpin]() {
            if (lvlSpin->value() != prefs_->wfIdLevel())
                lvlSpin->setValue(prefs_->wfIdLevel());
        });
        form->addRow(tr("Level:"), lvlSpin);

        // Auto-cadence interval (0 = once on arm; else every N min).
        auto *ivSpin = new QSpinBox(grp);
        ivSpin->setRange(0, 20);
        ivSpin->setValue(prefs_->wfIdIntervalMin());
        ivSpin->setSuffix(tr(" min"));
        ivSpin->setSpecialValueText(tr("once on arm"));   // shown at value 0
        ivSpin->setToolTip(tr(
            "How often the armed ID repeats.  0 = once when you arm it (no "
            "repeat); 1–20 = re-send every N minutes while armed.  "
            "Deferred while you're keyed; USB/LSB voice only."));
        connect(ivSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v) {
                    if (prefs_) prefs_->setWfIdIntervalMin(v);
                });
        connect(prefs_, &Prefs::wfIdIntervalMinChanged, ivSpin, [this, ivSpin]() {
            if (ivSpin->value() != prefs_->wfIdIntervalMin())
                ivSpin->setValue(prefs_->wfIdIntervalMin());
        });
        form->addRow(tr("Repeat:"), ivSpin);

        auto *warn = new QLabel(
            tr("⚠ Level is held low ON PURPOSE.  A waterfall ID is continuous "
               "full-duty multitone — running it hot splatters (IMD) and "
               "stresses a solid-state amp like full-power digital drive.  The "
               "0.065 cap ≈ ~¼ digital drive.  Courtesy ID only — it does NOT "
               "replace your legal voice/CW station identification."), grp);
        warn->setWordWrap(true);
        warn->setStyleSheet(QStringLiteral("color:#ff9a3c;"));   // amber caution
        form->addRow(warn);

        auto *note = new QLabel(
            tr("Arm from the header WF-ID chip (lights amber; USB/LSB only).  "
               "On arm it sends your call once, then again every “Repeat” "
               "minutes — no manual send button.  Re-arms OFF every session.  "
               "Your callsign comes from Settings → Hardware → Operator."), grp);
        note->setWordWrap(true);
        form->addRow(note);

        col3->addWidget(grp);      // #175 — TUNE + MODES column (WF-ID)
    }

    // ── AM Carrier (AM / SAM modulation) group ───────────────────
    // #93/#106 — operator AM/SAM carrier level.  Splits TX power between
    // the carrier and the sidebands.  DSB is always suppressed-carrier and
    // FM/SSB ignore it, so this only bites in AM/SAM.
    if (stream_) {
        auto *grp = new QGroupBox(tr("AM Carrier  (AM / SAM modulation)"), page);
        auto *form = new QFormLayout(grp);
        auto *carSpin = new QDoubleSpinBox(grp);
        carSpin->setRange(0.0, 100.0);
        carSpin->setSingleStep(5.0);
        carSpin->setDecimals(0);
        carSpin->setSuffix(tr(" %"));
        carSpin->setValue(stream_->amCarrierPct());
        carSpin->setToolTip(tr(
            "AM carrier level as a percent of the STANDARD AM carrier (power).  "
            "100 % = standard AM — carrier at 25 % of PEP, sidebands swing the "
            "envelope toward full power on modulation peaks.  Lower moves toward "
            "reduced / controlled-carrier (more relative sideband, 0 % ≈ DSB); "
            "higher pushes a heavier carrier.  Matches the reference AM carrier "
            "control 1:1.  Affects AM and SAM only — DSB is always "
            "suppressed-carrier and FM/SSB ignore it."));
        connect(carSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double v) {
                    if (stream_) stream_->setAmCarrierPct(v);
                });
        connect(stream_, &lyra::ipc::HL2Stream::amCarrierPctChanged, carSpin,
                [carSpin](double v) {
                    if (carSpin->value() != v) carSpin->setValue(v);
                });
        form->addRow(tr("Carrier:"), carSpin);
        col3->addWidget(grp);      // #93 — TUNE + MODES column (AM Carrier)
    }

    // ── FM (deviation + CTCSS) group ─────────────────────────────
    // #107 — operator FM knobs.  Deviation = peak FM deviation (narrow vs
    // wide).  CTCSS = sub-audible repeater-access tone (enable + tone from
    // the standard 67.0..254.1 Hz table).  All bite only in FM.
    if (stream_) {
        auto *grp  = new QGroupBox(tr("FM  (deviation / CTCSS)"), page);
        auto *form = new QFormLayout(grp);

        auto *devSpin = new QDoubleSpinBox(grp);
        devSpin->setRange(1.0, 6.0);
        devSpin->setSingleStep(0.5);
        devSpin->setDecimals(1);
        devSpin->setValue(stream_->fmDeviationHz() / 1000.0);
        // Flag the two standard presets right in the readout so the operator
        // sees which deviation they're on: 5.0 = Wide (US), 2.5 = Narrow
        // (US narrowband / much of EU); other values just read " kHz".
        const QString sfxWide   = tr(" kHz — Wide (US)");
        const QString sfxNarrow = tr(" kHz — Narrow (US/EU)");
        const QString sfxPlain  = tr(" kHz");
        auto applyDevSuffix = [devSpin, sfxWide, sfxNarrow, sfxPlain](double k) {
            devSpin->setSuffix(k == 5.0 ? sfxWide
                             : k == 2.5 ? sfxNarrow
                                        : sfxPlain);
        };
        applyDevSuffix(devSpin->value());
        devSpin->setToolTip(tr(
            "FM peak deviation.  5.0 kHz = Wide (US ham standard for 10 m / "
            "6 m repeater + simplex FM); 2.5 kHz = Narrow (US narrowband / the "
            "norm across much of Europe & elsewhere).  Too much deviation "
            "splatters into adjacent channels; too little sounds weak/quiet.  "
            "Match your region / the repeater.  Affects FM only."));
        connect(devSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this, applyDevSuffix](double v) {
                    applyDevSuffix(v);
                    if (stream_) stream_->setFmDeviationHz(v * 1000.0);
                });
        connect(stream_, &lyra::ipc::HL2Stream::fmDeviationHzChanged, devSpin,
                [devSpin](double hz) {
                    const double k = hz / 1000.0;
                    if (devSpin->value() != k) devSpin->setValue(k);
                });
        form->addRow(tr("Deviation:"), devSpin);

        // FM pre-emphasis: Off (flat — digital/data + warm HF) vs Comm (the
        // 6 dB/oct 300–3000 Hz communications curve a hardware rig makes;
        // voice default).  A true Off — unlike rigs/SDRs that force it on.
        auto *emphCombo = new QComboBox(grp);
        emphCombo->addItem(tr("Off (flat / data)"), 0);
        emphCombo->addItem(tr("Comm (6 dB/oct voice)"), 1);
        emphCombo->setToolTip(tr(
            "FM pre-emphasis.  Comm = the standard 6 dB/oct (300–3000 Hz) "
            "communications curve for voice (Icom/Kenwood-style).  Off = flat "
            "— use for digital/data (packet, VARA FM) and a warmer HF tone.  "
            "Affects FM only."));
        emphCombo->setCurrentIndex(stream_->fmEmphasisMode() == 1 ? 1 : 0);
        connect(emphCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this, emphCombo](int) {
                    if (stream_) stream_->setFmEmphasisMode(
                        emphCombo->currentData().toInt());
                });
        connect(stream_, &lyra::ipc::HL2Stream::fmEmphasisModeChanged, emphCombo,
                [emphCombo](int mode) {
                    const int idx = mode == 1 ? 1 : 0;
                    if (emphCombo->currentIndex() != idx)
                        emphCombo->setCurrentIndex(idx);
                });
        form->addRow(tr("Pre-emphasis:"), emphCombo);

        auto *ctcssBox = new QCheckBox(tr("CTCSS sub-tone (repeater access)"), grp);
        ctcssBox->setChecked(stream_->ctcssEnabled());
        ctcssBox->setToolTip(tr(
            "Transmit a sub-audible CTCSS tone to open a tone-protected FM "
            "repeater.  Enable + pick your repeater's tone.  Off for simplex.  "
            "Affects FM only."));
        connect(ctcssBox, &QCheckBox::toggled, this, [this](bool on) {
            if (stream_) stream_->setCtcssEnabled(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::ctcssEnabledChanged, ctcssBox,
                [ctcssBox](bool on) {
                    if (ctcssBox->isChecked() != on) ctcssBox->setChecked(on);
                });
        form->addRow(ctcssBox);

        auto *toneCombo = new QComboBox(grp);
        static const double kCtcss[] = {
             67.0,  69.3,  71.9,  74.4,  77.0,  79.7,  82.5,  85.4,  88.5,  91.5,
             94.8,  97.4, 100.0, 103.5, 107.2, 110.9, 114.8, 118.8, 123.0, 127.3,
            131.8, 136.5, 141.3, 146.2, 151.4, 156.7, 159.8, 162.2, 165.5, 167.9,
            171.3, 173.8, 177.3, 179.9, 183.5, 186.2, 189.9, 192.8, 196.6, 199.5,
            203.5, 206.5, 210.7, 218.1, 225.7, 229.1, 233.6, 241.8, 250.3, 254.1};
        for (double t : kCtcss)
            toneCombo->addItem(QStringLiteral("%1 Hz").arg(t, 0, 'f', 1),
                               QVariant(t));
        toneCombo->setToolTip(tr("Standard CTCSS sub-audible tone (Hz)."));
        const double curTone = stream_->ctcssToneHz();
        for (int i = 0; i < toneCombo->count(); ++i)
            if (toneCombo->itemData(i).toDouble() == curTone) {
                toneCombo->setCurrentIndex(i); break;
            }
        connect(toneCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this, toneCombo](int) {
                    if (stream_) stream_->setCtcssToneHz(
                        toneCombo->currentData().toDouble());
                });
        connect(stream_, &lyra::ipc::HL2Stream::ctcssToneHzChanged, toneCombo,
                [toneCombo](double hz) {
                    for (int i = 0; i < toneCombo->count(); ++i)
                        if (toneCombo->itemData(i).toDouble() == hz) {
                            if (toneCombo->currentIndex() != i)
                                toneCombo->setCurrentIndex(i);
                            break;
                        }
                });
        form->addRow(tr("Tone:"), toneCombo);

        col3->addWidget(grp);      // #107 — TUNE + MODES column (FM)
    }

    // ── ATT on TX (RX-ADC protection) group ──────────────────────
    // §15.31.  Mirrors the reference's Setup → General → Ant/Filters →
    // "ATT on Tx" ✓ + "ATT: 31" spin (HL2/HL2+ working posture).  On
    // key-down the FSM forces the HL2 step attenuator to this value so
    // TX coupling can't blind the RX ADC; keyup restores.  Lives in the
    // LEFT (safety / set-once) column.  The PS-A-conditional Force/Auto
    // sub-rows in the reference are deferred to the v0.3 PureSignal
    // dialog.  Front-panel TxPanel "ATT" lamp toggles the same value.
    if (stream_) {
        auto *grp = new QGroupBox(tr("ATT on TX  (RX-ADC protection)"), page);
        auto *form = new QFormLayout(grp);

        auto *enBox = new QCheckBox(
            tr("Enabled  (attenuate RX front end while transmitting)"), grp);
        enBox->setChecked(stream_->attOnTxEnabled());
        enBox->setToolTip(tr(
            "Forces the HL2 step attenuator (drives the AD9866 RX LNA to "
            "minimum gain) for the duration of every transmission, so the "
            "TX carrier coupling into the receiver can't blind the RX ADC "
            "(panadapter going wide / S-meter pegging on key-down).\n\n"
            "Default ON — matches the reference's HL2 working posture "
            "(Setup → General → Ant/Filters → \"ATT on Tx\").  Turning it "
            "OFF removes RX-ADC protection during TX; only do so if you "
            "have a specific reason.  The TxPanel \"ATT\" lamp shows the "
            "live state (orange = armed, red = engaged)."));
        connect(enBox, &QCheckBox::toggled, this, [this](bool on) {
            if (stream_) stream_->setAttOnTxEnabled(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::attOnTxEnabledChanged, enBox,
                [enBox](bool on) {
                    if (enBox->isChecked() != on) enBox->setChecked(on);
                });
        form->addRow(enBox);

        auto *attSpin = new QSpinBox(grp);
        attSpin->setRange(0, 31);
        attSpin->setSingleStep(1);
        attSpin->setSuffix(tr(" dB"));
        attSpin->setValue(stream_->attOnTxDb());
        attSpin->setToolTip(tr(
            "Step-attenuator value forced on the RX front end while "
            "transmitting.  31 dB = maximum attenuation (RX LNA driven to "
            "minimum gain) = the reference default and the strongest "
            "RX-ADC protection.  Range 0..31 matches the reference's spin "
            "exactly.\n\n"
            "Has effect only when Enabled above is ticked."));
        connect(attSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v) {
                    if (stream_) stream_->setAttOnTxDb(v);
                });
        connect(stream_, &lyra::ipc::HL2Stream::attOnTxDbChanged, attSpin,
                [attSpin](int v) {
                    if (attSpin->value() != v) attSpin->setValue(v);
                });
        form->addRow(tr("ATT:"), attSpin);

        leftCol->addWidget(grp);   // §15.31 — SAFETY / set-once column
    }

    // #94 — External TX Inhibit (hard keying lockout).  Lives HERE in
    // Settings → TX by deliberate design — NOT a one-click panel button — so
    // it can't be bumped off accidentally and silently re-enable transmit;
    // releasing it requires opening this dialog.  Blocks ALL keying at the
    // requestMox funnel; persisted fail-safe; the toolbar ⛔ TX INHIBIT badge
    // shows the live state.  Same LEFT safety / set-once column.
    if (stream_) {
        auto *grp = new QGroupBox(tr("External TX Inhibit  (block all transmit)"),
                                  page);
        auto *form = new QFormLayout(grp);

        auto *tiBox = new QCheckBox(
            tr("Inhibit transmit — lock out ALL keying"), grp);
        tiBox->setChecked(stream_->txInhibit());
        tiBox->setToolTip(tr(
            "Hard safety lockout: when ON the radio CANNOT transmit by any "
            "means — MOX button, foot switch / hand-mic, CW, Tune, TCI, and "
            "the waterfall Auto-ID are all blocked.  Use it to protect a "
            "second receiver / SDR / scope sharing your antenna or bench "
            "while it's connected.\n\n"
            "Engaging it while transmitting drops you to receive immediately. "
            "Remembered across restarts (fail-safe).  It lives here in "
            "Settings → TX on purpose — so an accidental click can't release "
            "it; you must open this dialog to turn it off.  A red ⛔ TX "
            "INHIBIT badge in the toolbar shows when it's active."));
        connect(tiBox, &QCheckBox::toggled, this, [this](bool on) {
            if (stream_) stream_->setTxInhibit(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::txInhibitChanged, tiBox,
                [tiBox](bool on) {
                    if (tiBox->isChecked() != on) tiBox->setChecked(on);
                });
        form->addRow(tiBox);

        leftCol->addWidget(grp);   // #94 — SAFETY / set-once column
    }

    // #169 — SWR protection (auto-cut TX on sustained high reflected
    // power).  Sibling safety control to ATT-on-TX above; same LEFT
    // (safety / set-once) column.  Stream-side evaluator cuts TX through
    // the normal keyup chain when reflected power stays above the limit
    // past the key-down blanking + dwell guards (false-trigger-safe), and
    // latches until the operator re-keys.  Front-panel TxPanel "PROT"
    // lamp toggles the same enable + shows the live/tripped state.
    if (stream_) {
        auto *grp = new QGroupBox(tr("SWR protection  (auto-cut TX)"), page);
        auto *form = new QFormLayout(grp);

        auto *enBox = new QCheckBox(
            tr("Enabled  (cut TX on sustained high SWR)"), grp);
        enBox->setChecked(stream_->swrProtectEnabled());
        enBox->setToolTip(tr(
            "Watches reflected power while transmitting and auto-cuts TX "
            "(through the normal keyup chain) when the SWR stays above the "
            "limit below.  Protects the PA / finals against a bad antenna, "
            "a disconnected feedline, or an ATU that didn't latch.\n\n"
            "False-trigger-safe: a key-down blanking window skips the T/R "
            "+ ALC settle, forward/reflected power floors gate out the "
            "low-power regime, and the over-limit condition must persist "
            "for a short dwell before it cuts.  The SWR reading is "
            "calibration-free (it's a power ratio), so it works without "
            "the PWR-meter calibration.  After a cut, re-key (MOX/TUN) to "
            "resume — there's no auto-recovery while still keyed.\n\n"
            "Default ON at 5:1.  The TxPanel \"PROT\" lamp shows the live "
            "state (green = armed, red = tripped)."));
        connect(enBox, &QCheckBox::toggled, this, [this](bool on) {
            if (stream_) stream_->setSwrProtectEnabled(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::swrProtectEnabledChanged, enBox,
                [enBox](bool on) {
                    if (enBox->isChecked() != on) enBox->setChecked(on);
                });
        form->addRow(enBox);

        auto *limSpin = new QDoubleSpinBox(grp);
        limSpin->setRange(lyra::ipc::HL2Stream::kSwrProtectMinLimit,
                          lyra::ipc::HL2Stream::kSwrProtectMaxLimit);
        limSpin->setSingleStep(0.5);
        limSpin->setDecimals(1);
        limSpin->setSuffix(tr(" : 1"));
        limSpin->setValue(stream_->swrProtectLimit());
        limSpin->setToolTip(tr(
            "SWR ratio at (or above) which TX is cut once sustained past "
            "the dwell window.  5:1 is a conservative default — most real "
            "antennas run under 3:1, so 5:1 means something is genuinely "
            "wrong (open feedline, wrong band, ATU not latched).  Lower it "
            "if you want tighter protection; range 1.5..10:1."));
        connect(limSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double v) {
                    if (stream_) stream_->setSwrProtectLimit(v);
                });
        connect(stream_, &lyra::ipc::HL2Stream::swrProtectLimitChanged, limSpin,
                [limSpin](double v) {
                    if (limSpin->value() != v) limSpin->setValue(v);
                });
        form->addRow(tr("Limit:"), limSpin);

        auto *tuneBox = new QCheckBox(
            tr("Protect during tune (TUN)"), grp);
        tuneBox->setChecked(stream_->swrProtectDuringTune());
        tuneBox->setToolTip(tr(
            "When ON (default), the SWR cut also applies to a deliberate "
            "ATU tune carrier (TUN).  Turn OFF only if you tune into a "
            "known-high-SWR load on purpose and don't want the carrier "
            "cut out from under the ATU."));
        connect(tuneBox, &QCheckBox::toggled, this, [this](bool on) {
            if (stream_) stream_->setSwrProtectDuringTune(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::swrProtectDuringTuneChanged,
                tuneBox, [tuneBox](bool on) {
                    if (tuneBox->isChecked() != on) tuneBox->setChecked(on);
                });
        form->addRow(tuneBox);

        // #169 Phase 1b — action picker: Cut (unkey) vs Fold (step the
        // drive down x0.5 toward the floor, escalating to Cut only if the
        // floor can't tame it).
        auto *actCombo = new QComboBox(grp);
        actCombo->addItem(tr("Cut TX (unkey)"));        // index 0
        actCombo->addItem(tr("Fold back power"));       // index 1
        actCombo->setCurrentIndex(stream_->swrProtectAction());
        actCombo->setToolTip(tr(
            "What to do when SWR stays over the limit:\n"
            "• Cut TX — unkey immediately (safest; the radio stops "
            "transmitting until you re-key).\n"
            "• Fold back power — halve TX drive in steps to try to bring "
            "the SWR down while staying on the air; if it hits the fold "
            "floor below and SWR is still over the limit, it escalates to "
            "a hard cut.  Folded drive is restored on your next key-up/"
            "key-down — it never creeps back up on its own."));
        connect(actCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int idx) {
                    if (stream_) stream_->setSwrProtectAction(idx);
                });
        connect(stream_, &lyra::ipc::HL2Stream::swrProtectActionChanged,
                actCombo, [actCombo](int idx) {
                    if (actCombo->currentIndex() != idx)
                        actCombo->setCurrentIndex(idx);
                });
        form->addRow(tr("On high SWR:"), actCombo);

        auto *foldSpin = new QSpinBox(grp);
        foldSpin->setRange(1, 90);
        foldSpin->setSuffix(tr(" %"));
        foldSpin->setValue(stream_->foldMinDrivePct());
        foldSpin->setToolTip(tr(
            "Fold floor — the lowest TX drive (% of full) the Fold action "
            "will step down to before giving up and cutting TX.  Only used "
            "when the action above is \"Fold back power.\"  Default 10 %."));
        connect(foldSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v) {
                    if (stream_) stream_->setFoldMinDrivePct(v);
                });
        connect(stream_, &lyra::ipc::HL2Stream::foldMinDrivePctChanged,
                foldSpin, [foldSpin](int v) {
                    if (foldSpin->value() != v) foldSpin->setValue(v);
                });
        form->addRow(tr("Fold floor:"), foldSpin);

        leftCol->addWidget(grp);   // #169 — SAFETY / set-once column
    }

    // #170 (Max TX drive %) RETIRED — superseded by the Settings → PA Gain
    // tab's watts Max cap (per-band, predictive + reactive).  The backend
    // maxDrivePct_ stays pinned at 100 (no %-clamp); the watts cap is the
    // amp-protection control now.

    // §15.28 — push groups to top of each column so trailing whitespace
    // sits between the body and the Restore button instead of between
    // groups.  Mismatched group counts (3 left vs 2 right) otherwise
    // produce uneven vertical spacing.
    leftCol->addStretch(1);
    rightCol->addStretch(1);
    col3->addStretch(1);

    // ── Restore hot-switch-safe defaults button ──────────────────
    auto *restoreBtn = new QPushButton(
        tr("Restore hot-switch-safe defaults"), page);
    restoreBtn->setProperty("lyraRestore", true);   // §15.28 — amber accent
    restoreBtn->setToolTip(tr(
        "Resets all seven values above to the bench-validated defaults "
        "(MOX 15 / RF 50 / Space-MOX 13 / PTT-Out 5 / Fade-In 50 / "
        "Fade-Out 13 / TX-Stop 10 ms).  These are hot-switch-safe for "
        "typical 1 kW solid-state HF linears and reference-faithful "
        "for the in-flight UDP datagram clear.  Use this to get back "
        "to a known-good starting point if you've experimented with "
        "values and want to return to the safe profile."));
    connect(restoreBtn, &QPushButton::clicked, this, [this]() {
        if (!stream_) return;
        // Bench-validated working-station defaults.
        stream_->setMoxDelayMs(15);
        stream_->setRfDelayMs(50);
        stream_->setSpaceMoxDelayMs(13);
        stream_->setPttOutDelayMs(5);
        stream_->setTxStopDelayMs(10);
    });
    root->addWidget(restoreBtn);

    // ── Operator-facing explainer (italic, dim) ──────────────────
    auto *hint = new QLabel(
        tr("<p style='font-size:11px;color:#8fa6ba'>"
           "The TR-sequencing values control <i>scheduling</i> of the "
           "MOX → step-att → RF → cleanup edges; the amplitude envelope "
           "shapes the <i>I/Q amplitude</i> itself at the EP2 wire-pack "
           "stage.  Both layers compose into a single 'clean PTT onset' "
           "story — at defaults the fade-in completes exactly when "
           "'RF settled' fires (50 ms after wire-MOX hot), and the "
           "fade-out completes exactly when wire-MOX clears "
           "(13 ms after operator-keyup).</p>"
           "<p style='font-size:11px;color:#8fa6ba'>"
           "Live-apply: changes take effect on the next MOX edge — "
           "no restart, no MOX cycle required.  If you change values "
           "while the radio is keyed, the in-flight transition "
           "finishes at the prior rate; the new values apply at the "
           "next keydown or keyup.</p>"), page);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    root->addWidget(hint);

    root->addStretch(1);

    // Cap every spin box on this tab to a compact width (operator prefers
    // short spin boxes — the value + unit suffix never needs more).  One
    // sweep covers QSpinBox + QDoubleSpinBox without touching each site.
    for (auto *sb : page->findChildren<QAbstractSpinBox *>())
        sb->setMaximumWidth(92);

    return page;
}

namespace {
// #91 VOX — compact colored mic-level meter with a threshold marker.
// Plain QWidget (no Q_OBJECT — driven by external setLevel/setThreshold
// calls from the VOX tab's signal connections).  green→yellow→red fill
// grows with the live mic level; a bright cyan line marks where the
// threshold sits, so the operator sets it by eye against their voice.
class VoxLevelBar : public QWidget {
public:
    explicit VoxLevelBar(QWidget *parent = nullptr) : QWidget(parent) {
        // Compact meter — capped length (no full-width stretch) + trimmed
        // height so it reads as a tidy bar next to the number, not a slab.
        setMinimumSize(90, 12);
        setMaximumSize(170, 13);
    }
    QSize sizeHint() const override { return QSize(150, 13); }
    void setLevel(double db)     { level_  = db; update(); }
    void setThreshold(double db) { thresh_ = db; update(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const double w = r.width();
        auto frac = [](double db) {
            constexpr double lo = -60.0, hi = 0.0;   // display window
            double f = (db - lo) / (hi - lo);
            return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
        };
        // track
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#0d1319"));
        p.drawRoundedRect(r, 3, 3);
        // level fill — gradient anchored to full width, clipped to level
        const double lf = frac(level_);
        if (lf > 0.001) {
            QLinearGradient g(r.left(), 0, r.right(), 0);
            g.setColorAt(0.00, QColor("#2ecc71"));   // green
            g.setColorAt(0.55, QColor("#8bd13a"));
            g.setColorAt(0.75, QColor("#f1c40f"));   // yellow
            g.setColorAt(0.90, QColor("#e67e22"));   // orange
            g.setColorAt(1.00, QColor("#e74c3c"));   // red
            p.setBrush(g);
            p.drawRoundedRect(QRectF(r.left(), r.top(), w * lf, r.height()),
                              3, 3);
        }
        // threshold marker — bright cyan vertical line
        const double tx = r.left() + w * frac(thresh_);
        p.setPen(QPen(QColor("#00e5ff"), 2));
        p.drawLine(QPointF(tx, r.top() - 0.5), QPointF(tx, r.bottom() + 0.5));
        // border
        p.setPen(QPen(QColor("#2a3a4a"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, 3, 3);
    }
private:
    double level_  = -200.0;
    double thresh_ = -35.0;
};
}  // namespace

// #91 — VOX (voice-operated TX) on its OWN tab (operator: the TX tab was
// getting cluttered).  A mic-RMS gate keys TX on speech and drops it
// after a hang; anti-VOX suppresses opening on the RX audio your studio
// monitors spill into the mic.  VOX never overrides a manual / foot-
// switch key, keys voice modes only (never CW/digital), and respects TX
// Inhibit.  The front-panel TxPanel "VOX" button mirrors the enable +
// shows keying state.  Single-column form — VOX is a small control set.
QWidget *SettingsDialog::buildVoxTab() {
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);

    if (stream_) {
        auto *grp = new QGroupBox(tr("VOX  (voice-operated transmit)"), page);
        auto *form = new QFormLayout(grp);

        auto *enBox = new QCheckBox(tr("Enabled  (key TX on voice)"), grp);
        enBox->setChecked(stream_->voxEnabled());
        enBox->setToolTip(tr(
            "Voice-operated transmit: keys the radio when your mic level "
            "rises above the threshold and drops back to receive after the "
            "hang time.  Works in voice modes only (never CW or digital) "
            "and is blocked by TX Inhibit.  It never overrides a manual MOX "
            "or foot-switch press — those always win, and VOX only releases "
            "a key it raised itself.  The front-panel VOX button mirrors "
            "this and lights while VOX holds the key."));
        connect(enBox, &QCheckBox::toggled, this, [this](bool on) {
            if (stream_) stream_->setVoxEnabled(on);
        });
        connect(stream_, &lyra::ipc::HL2Stream::voxEnabledChanged, enBox,
                [enBox](bool on) {
                    if (enBox->isChecked() != on) enBox->setChecked(on);
                });
        form->addRow(enBox);

        // Short spin boxes (operator: the full-width ones drove them nuts).
        constexpr int kSpinW = 96;

        // Live mic-level METER — a colored bar with the threshold marked,
        // placed ABOVE the threshold spin so you dial the number while
        // watching your voice hit the bar.  Updates ~20 Hz off the VOX
        // poll (even with VOX disabled) so it's live while you set up.
        auto *bar = new VoxLevelBar(grp);
        bar->setLevel(stream_->voxMicDbfs());
        bar->setThreshold(stream_->voxThresholdDbfs());
        bar->setToolTip(tr(
            "Live mic level (−60…0 dBFS).  Green→red as you get louder; the "
            "cyan line is your Threshold.  Speak normally and set Threshold "
            "just below where your voice peaks — but above where the bar "
            "sits when you're silent (desk/room noise)."));
        auto *num = new QLabel(grp);
        num->setMinimumWidth(58);
        num->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        num->setStyleSheet(QStringLiteral("font-family:Consolas;"));
        auto fmtMic = [](double db) {
            return db <= -199.0 ? QStringLiteral("—")
                                : QString::number(db, 'f', 0) + QStringLiteral(" dBFS");
        };
        num->setText(fmtMic(stream_->voxMicDbfs()));
        auto *meterRow = new QWidget(grp);
        auto *meterH   = new QHBoxLayout(meterRow);
        meterH->setContentsMargins(0, 0, 0, 0);
        meterH->setSpacing(8);
        meterH->addWidget(bar, 0);
        meterH->addWidget(num, 0);
        meterH->addStretch(1);   // keep the short bar + number left-clustered
        connect(stream_, &lyra::ipc::HL2Stream::voxMicDbfsChanged, bar,
                [bar, num, fmtMic](double db) {
                    bar->setLevel(db);
                    num->setText(fmtMic(db));
                });
        form->addRow(tr("Mic level:"), meterRow);

        auto *thrSpin = new QDoubleSpinBox(grp);
        thrSpin->setRange(-80.0, 0.0);
        thrSpin->setSingleStep(1.0);
        thrSpin->setDecimals(0);
        thrSpin->setSuffix(tr(" dBFS"));
        thrSpin->setMaximumWidth(kSpinW);
        thrSpin->setValue(stream_->voxThresholdDbfs());
        thrSpin->setToolTip(tr(
            "Mic level (relative to full scale) your voice must exceed to "
            "key TX.  Higher (toward 0) = you must speak louder / desk "
            "noise won't trip it; lower = more sensitive.  Default −35 dBFS.  "
            "The cyan line on the meter above tracks this."));
        connect(thrSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this, bar](double v) {
                    if (stream_) stream_->setVoxThresholdDbfs(v);
                    bar->setThreshold(v);   // move the marker live
                });
        form->addRow(tr("Threshold:"), thrSpin);

        auto *openSpin = new QSpinBox(grp);
        openSpin->setRange(0, 500);
        openSpin->setSuffix(tr(" ms"));
        openSpin->setMaximumWidth(kSpinW);
        openSpin->setValue(stream_->voxOpenMs());
        openSpin->setToolTip(tr(
            "How long your voice must stay above the threshold before VOX "
            "keys — rejects clicks, thumps and short desk noises.  Default "
            "10 ms."));
        connect(openSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v) { if (stream_) stream_->setVoxOpenMs(v); });
        form->addRow(tr("Open delay:"), openSpin);

        auto *hangSpin = new QSpinBox(grp);
        hangSpin->setRange(0, 3000);
        hangSpin->setSingleStep(50);
        hangSpin->setSuffix(tr(" ms"));
        hangSpin->setMaximumWidth(kSpinW);
        hangSpin->setValue(stream_->voxHangMs());
        hangSpin->setToolTip(tr(
            "How long TX is held after your voice drops below the threshold "
            "— bridges the gaps between words so VOX doesn't chatter mid-"
            "sentence.  Default 300 ms."));
        connect(hangSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int v) { if (stream_) stream_->setVoxHangMs(v); });
        form->addRow(tr("Hang time:"), hangSpin);

        auto *antiBox = new QCheckBox(
            tr("Anti-VOX  (ignore speaker/monitor bleed)"), grp);
        antiBox->setChecked(stream_->voxAntiVoxOn());
        antiBox->setToolTip(tr(
            "When ON (recommended if you run open studio monitors instead "
            "of headphones), VOX will NOT key while the received audio is "
            "above the anti-VOX level below — so a loud RX signal spilling "
            "into your mic can't false-trigger transmit."));
        connect(antiBox, &QCheckBox::toggled, this, [this](bool on) {
            if (stream_) stream_->setVoxAntiVoxOn(on);
        });
        form->addRow(antiBox);

        // Live RX-audio METER — the anti-VOX reference (the "what the
        // operator hears" RMS the gate compares against).  Same idiom as
        // the mic meter above: colored bar (−60…0 dBFS) with the anti-VOX
        // level marked in cyan.  Reuses fmtMic (in scope).
        auto *rxBar = new VoxLevelBar(grp);
        rxBar->setLevel(stream_->voxRxDbfs());
        rxBar->setThreshold(stream_->voxAntiVoxDbfs());
        rxBar->setToolTip(tr(
            "Live received-audio level (what you hear on the speaker).  The "
            "cyan line is the Anti-VOX level below — set that just above "
            "where this bar sits when the radio is receiving voice, so RX "
            "bleed into the mic can't false-key VOX."));
        auto *rxNum = new QLabel(grp);
        rxNum->setMinimumWidth(58);
        rxNum->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rxNum->setStyleSheet(QStringLiteral("font-family:Consolas;"));
        rxNum->setText(fmtMic(stream_->voxRxDbfs()));
        auto *rxRow = new QWidget(grp);
        auto *rxH   = new QHBoxLayout(rxRow);
        rxH->setContentsMargins(0, 0, 0, 0);
        rxH->setSpacing(8);
        rxH->addWidget(rxBar, 0);
        rxH->addWidget(rxNum, 0);
        rxH->addStretch(1);   // keep the short bar + number left-clustered
        connect(stream_, &lyra::ipc::HL2Stream::voxRxDbfsChanged, rxBar,
                [rxBar, rxNum, fmtMic](double db) {
                    rxBar->setLevel(db);
                    rxNum->setText(fmtMic(db));
                });
        form->addRow(tr("RX level:"), rxRow);

        auto *antiSpin = new QDoubleSpinBox(grp);
        antiSpin->setRange(-90.0, 0.0);
        antiSpin->setSingleStep(1.0);
        antiSpin->setDecimals(0);
        antiSpin->setSuffix(tr(" dBFS"));
        antiSpin->setMaximumWidth(kSpinW);
        antiSpin->setValue(stream_->voxAntiVoxDbfs());
        antiSpin->setToolTip(tr(
            "RX audio level above which anti-VOX blocks VOX from keying.  "
            "Lower (toward −90) = more aggressive (even quiet RX blocks "
            "VOX); higher = only loud RX blocks it.  Default −45 dBFS.  "
            "The cyan line on the RX meter above tracks this.  Only used "
            "when Anti-VOX above is ticked."));
        connect(antiSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this, rxBar](double v) {
                    if (stream_) stream_->setVoxAntiVoxDbfs(v);
                    rxBar->setThreshold(v);   // move the marker live
                });
        form->addRow(tr("Anti-VOX level:"), antiSpin);

        // The RX meter + anti-VOX level are only in play when anti-VOX is
        // on — grey them with the toggle so the panel reads clearly.
        const bool antiOn = stream_->voxAntiVoxOn();
        rxRow->setEnabled(antiOn);
        antiSpin->setEnabled(antiOn);
        connect(antiBox, &QCheckBox::toggled, this,
                [rxRow, antiSpin](bool on) {
                    rxRow->setEnabled(on);
                    antiSpin->setEnabled(on);
                });

        root->addWidget(grp);

        // #91 — quick how-to (operator asked: threshold is hard to dial
        // by number alone).  Wrapped, subtle, sits under the controls.
        auto *how = new QLabel(page);
        how->setWordWrap(true);
        how->setProperty("lyraColSubtitle", true);   // muted §15.28 style
        how->setText(tr(
            "How to set it:\n"
            "1.  Pick a voice mode (USB/LSB/AM/FM) — VOX ignores CW/digital.\n"
            "2.  With VOX off, speak normally and watch \"Mic level\" above. "
            "Note your speech peaks, and the quiet level when you stop.\n"
            "3.  Set Threshold BETWEEN the two — a few dB below your speech "
            "peaks, but above the quiet/room-noise level so breathing, the "
            "desk fan, or a keyboard can't key you.\n"
            "4.  Arm VOX and talk. If it clips the start of words, lower "
            "Open delay; if it drops between words, raise Hang time.\n"
            "5.  On open speakers, keep Anti-VOX on and set its level just "
            "above your normal RX audio so received signals can't trip TX.\n"
            "VOX never overrides a manual MOX or foot-switch — those always "
            "win."));
        root->addWidget(how);
    } else {
        // No stream — VOX writes through HL2Stream; show a hint instead
        // of an empty tab (the tab is only added when stream_ exists, so
        // this branch is defensive).
        root->addWidget(new QLabel(
            tr("Connect to the radio to configure VOX."), page));
    }

    root->addStretch(1);
    return page;
}

bool SettingsDialog::selectTopic(const QString &topic) {
    // Map a panel help topic -> the Settings tab that owns it.  ONLY topics
    // whose panel genuinely has a Settings home appear here; a panel whose
    // controls live purely on the front panel (Filters, Tuning, the TX
    // DSP-rack docks, the CW decoder, Solar) is deliberately absent -> we
    // return false and the caller sends the operator to that panel's guide
    // section instead.  Tabs are found by title so indices stay robust.
    static const QHash<QString, QString> kTabFor = {
        {QStringLiteral("panadapter"), QStringLiteral("Visuals")},
        {QStringLiteral("visuals"),    QStringLiteral("Visuals")},
        {QStringLiteral("display"),    QStringLiteral("Visuals")},
        {QStringLiteral("hardware"),   QStringLiteral("Hardware")},
        {QStringLiteral("radio"),      QStringLiteral("Hardware")},
        {QStringLiteral("audio"),      QStringLiteral("Audio")},
        {QStringLiteral("tx"),         QStringLiteral("TX")},
        {QStringLiteral("vox"),        QStringLiteral("VOX")},
        {QStringLiteral("cwconsole"),  QStringLiteral("CW")},
        {QStringLiteral("tuner"),      QStringLiteral("Tuner")},
        {QStringLiteral("recorder"),   QStringLiteral("Recording")},
        {QStringLiteral("profiles"),   QStringLiteral("Profiles")},
        {QStringLiteral("meter"),      QStringLiteral("Meter")},
        {QStringLiteral("band"),       QStringLiteral("Bands")},
        {QStringLiteral("memory"),     QStringLiteral("Bands")},
        {QStringLiteral("bands"),      QStringLiteral("Bands")},
    };
    const QString want = kTabFor.value(topic);
    if (want.isEmpty()) return false;
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->tabText(i) == want) {
            tabs_->setCurrentIndex(i);
            return true;
        }
    }
    return false;   // mapped tab isn't present (gated off) -> fall back
}

QWidget *SettingsDialog::buildVisualsTab() {
    // Three-column layout (task #22, the SECOND big-tab refactor after
    // Hardware).  Visuals had 26 controls / groups stacked vertically
    // — the longest tab in Settings.  Same minimal-surgery technique
    // as Hardware: three sibling QFormLayouts inside a horizontal
    // split, the local `form` pointer repointed twice so the existing
    // ~600 lines of `form->addRow(...)` call sites stay byte-identical.
    // No widget construction changes, no signal/slot re-wires.
    //
    // Layout (top-down, left → middle → right):
    //   LEFT   : Trace colour group, Waterfall palette group
    //   MIDDLE : Waterfall speed + dB (auto/floor/ceiling), Spectrum
    //            fill group, Peak markers group, Noise floor group,
    //            Trace smoothing, Peak glow, Glass sheen, Watermark
    //   RIGHT  : Meteors + Meteor frequency + Gold fireballs, Gridline
    //            brightness, Cursor readout, Frame rate, Spectrum dB
    //            (auto/floor/ceiling), Graphics backend group
    auto *page  = new QWidget(this);
    auto *outer = new QHBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(14);

    auto *leftCol  = new QWidget(page);
    auto *leftForm = new QFormLayout(leftCol);
    leftForm->setContentsMargins(0, 0, 0, 0);
    leftForm->setVerticalSpacing(10);
    outer->addWidget(leftCol, 1);

    auto *midCol  = new QWidget(page);
    auto *midForm = new QFormLayout(midCol);
    midForm->setContentsMargins(0, 0, 0, 0);
    midForm->setVerticalSpacing(10);
    outer->addWidget(midCol, 1);

    auto *rightCol  = new QWidget(page);
    auto *rightForm = new QFormLayout(rightCol);
    rightForm->setContentsMargins(0, 0, 0, 0);
    rightForm->setVerticalSpacing(10);
    outer->addWidget(rightCol, 1);

    // §15.28 — column headers (TRACE & PALETTE / SPECTRUM & WATERFALL /
    // EFFECTS & PERFORMANCE).  Inserted at row 0 of each form as
    // spanning widgets so the body's existing addRow() call sites stay
    // byte-identical — only the row-index offset shifts.  Cyan-bold
    // via the lyraColHeader QSS property marker; italic subtitle via
    // lyraColSubtitle.  Matches the TX tab's column-header treatment
    // for visual consistency across the Settings dialog.
    {
        auto *h = new QLabel(tr("TRACE & PALETTE"), leftCol);
        h->setProperty("lyraColHeader", true);
        leftForm->insertRow(0, h);
        auto *s = new QLabel(tr("trace look + palette ramp"), leftCol);
        s->setProperty("lyraColSubtitle", true);
        leftForm->insertRow(1, s);
    }
    {
        auto *h = new QLabel(tr("SPECTRUM & WATERFALL"), midCol);
        h->setProperty("lyraColHeader", true);
        midForm->insertRow(0, h);
        auto *s = new QLabel(tr("levels, peaks, noise, glow"), midCol);
        s->setProperty("lyraColSubtitle", true);
        midForm->insertRow(1, s);
    }
    {
        auto *h = new QLabel(tr("EFFECTS & PERFORMANCE"), rightCol);
        h->setProperty("lyraColHeader", true);
        rightForm->insertRow(0, h);
        auto *s = new QLabel(tr("eye candy + render"), rightCol);
        s->setProperty("lyraColSubtitle", true);
        rightForm->insertRow(1, s);
    }

    // Body below addresses `form` extensively.  Start at left, switch
    // to middle just before "Waterfall speed", switch to right just
    // before "Meteor streaks".
    QFormLayout *form = leftForm;

    // --- Trace colour: Solid (picked colour) OR By-strength (palette) ---
    // Solid mirrors old Lyra (preset chips + custom chooser).  By-strength
    // is the intensity gradient old Lyra couldn't achieve: the trace
    // colour tracks signal level via a palette LUT.
    {
        auto *box = new QWidget(page);
        auto *bv = new QVBoxLayout(box);
        bv->setContentsMargins(0, 0, 0, 0);
        bv->setSpacing(4);

        // Mode selector.
        auto *mode = new QComboBox(box);
        mode->addItems({tr("Solid color"), tr("By signal strength")});
        mode->setCurrentIndex(prefs_->traceMode());
        bv->addWidget(mode);

        // --- By-strength: palette combo (presets + Custom ramp) ---
        auto *palCombo = new QComboBox(box);
        palCombo->addItems(lyra::palettes::names());
        const int customIdx = palCombo->count();   // index just past presets
        palCombo->addItem(tr("Custom color…"));
        palCombo->setCurrentIndex(prefs_->palette());
        connect(palCombo, &QComboBox::currentIndexChanged,
                prefs_, &Prefs::setPalette);
        connect(prefs_, &Prefs::paletteChanged, box, [this, palCombo]() {
            if (palCombo->currentIndex() != prefs_->palette())
                palCombo->setCurrentIndex(prefs_->palette());
        });
        bv->addWidget(palCombo);

        // Live gradient preview of the selected by-strength palette (or
        // the custom dark->hue->white ramp).  Weak signal at the left,
        // strong at the right — so the operator sees what they're picking
        // instead of choosing a palette blind.  Built from the SAME LUT /
        // ramp the panadapter draws with, so it's a faithful preview.
        auto *palPreview = new QLabel(box);
        palPreview->setFixedHeight(16);
        palPreview->setMinimumWidth(180);
        palPreview->setScaledContents(true);
        palPreview->setToolTip(tr("Weak signal (left) → strong (right)"));
        palPreview->setStyleSheet(QStringLiteral(
            "QLabel{border:1px solid #2a3a4a;border-radius:3px;}"));
        auto updatePreview = [this, palPreview, customIdx](int idx) {
            QImage img(256, 1, QImage::Format_RGB888);
            if (idx == customIdx) {
                const QColor base(prefs_->strengthColor());
                for (int i = 0; i < 256; ++i) {
                    const double t = i / 255.0;
                    int r, g, b;
                    if (t <= 0.65) {                    // black -> base hue
                        const double f = t / 0.65;
                        r = int(base.red()   * f);
                        g = int(base.green() * f);
                        b = int(base.blue()  * f);
                    } else {                            // base -> toward white
                        const double f = (t - 0.65) / 0.35;
                        r = int(base.red()   + (255 - base.red())   * f * 0.7);
                        g = int(base.green() + (255 - base.green()) * f * 0.7);
                        b = int(base.blue()  + (255 - base.blue())  * f * 0.7);
                    }
                    img.setPixelColor(i, 0, QColor(std::clamp(r, 0, 255),
                                                   std::clamp(g, 0, 255),
                                                   std::clamp(b, 0, 255)));
                }
            } else {
                const auto &l = lyra::palettes::lut(idx);
                for (int i = 0; i < 256; ++i)
                    img.setPixelColor(i, 0, QColor(l[i][0], l[i][1], l[i][2]));
            }
            palPreview->setPixmap(QPixmap::fromImage(img));
        };
        updatePreview(prefs_->palette());
        // Combo change (user OR programmatic sync) repaints the preview;
        // a custom-base-colour change does too when "Custom…" is active.
        connect(palCombo, &QComboBox::currentIndexChanged, box, updatePreview);
        connect(prefs_, &Prefs::strengthColorChanged, box,
                [palCombo, updatePreview]() {
                    updatePreview(palCombo->currentIndex());
                });
        bv->addWidget(palPreview);

        // Custom by-strength base colour (the dark->hue->bright ramp,
        // like Amber).  Shown only when "Custom color…" is selected.
        auto *strSwatch = new QPushButton(box);
        strSwatch->setFixedSize(64, 22);
        strSwatch->setToolTip(tr("Base colour for the custom by-strength "
                                 "ramp — click to choose"));
        auto setStr = [strSwatch](const QString &hex) {
            strSwatch->setStyleSheet(QStringLiteral(
                "QPushButton{background:%1;border:1px solid #2a3a4a;"
                "border-radius:3px;}").arg(hex));
        };
        setStr(prefs_->strengthColor());
        connect(strSwatch, &QPushButton::clicked, box, [this, box]() {
            const QColor c = QColorDialog::getColor(
                QColor(prefs_->strengthColor()), box,
                tr("By-strength base colour"));
            if (c.isValid()) prefs_->setStrengthColor(c.name());
        });
        connect(prefs_, &Prefs::strengthColorChanged, box,
                [this, setStr]() { setStr(prefs_->strengthColor()); });
        bv->addWidget(strSwatch);

        // --- Solid: swatch + custom + preset chips (sub-widget) ---
        auto *colorBox = new QWidget(box);
        auto *bvc = new QVBoxLayout(colorBox);
        bvc->setContentsMargins(0, 0, 0, 0);
        bvc->setSpacing(4);

        auto *swatch = new QPushButton(colorBox);
        swatch->setFixedSize(44, 22);
        swatch->setToolTip(tr("Current trace colour"));
        auto setSwatch = [swatch](const QString &hex) {
            swatch->setStyleSheet(QStringLiteral(
                "QPushButton{background:%1;border:1px solid #2a3a4a;"
                "border-radius:3px;}").arg(hex));
        };
        setSwatch(prefs_->traceColor());
        auto *customBtn = new QPushButton(tr("Custom color…"), colorBox);
        customBtn->setFixedWidth(140);

        auto *topRow = new QHBoxLayout();
        topRow->addWidget(swatch);
        topRow->addWidget(customBtn);
        topRow->addStretch(1);
        bvc->addLayout(topRow);

        // 18 preset chips, 3 rows of 6 (warm / cool / accents).
        static const char *kPresets[18] = {
            "#e53935", "#fb8c00", "#ffb300", "#fdd835", "#c0ca33", "#7cb342",
            "#26a69a", "#00acc1", "#039be5", "#1e88e5", "#3949ab", "#8e24aa",
            "#d81b60", "#ff7043", "#6d4c41", "#78909c", "#eceff1", "#ffffff" };
        auto *pg = new QGridLayout();
        pg->setHorizontalSpacing(2);
        pg->setVerticalSpacing(2);
        for (int i = 0; i < 18; ++i) {
            const QString hx = QString::fromLatin1(kPresets[i]);
            auto *chip = new QPushButton(colorBox);
            chip->setFixedSize(26, 18);
            chip->setCursor(Qt::PointingHandCursor);
            chip->setToolTip(hx);
            chip->setStyleSheet(QStringLiteral(
                "QPushButton{background:%1;border:1px solid #2a3a4a;"
                "border-radius:2px;}"
                "QPushButton:hover{border:1px solid #00e5ff;}").arg(hx));
            connect(chip, &QPushButton::clicked, prefs_,
                    [this, hx]() { prefs_->setTraceColor(hx); });
            pg->addWidget(chip, i / 6, i % 6);
        }
        bvc->addLayout(pg);
        bv->addWidget(colorBox);

        connect(customBtn, &QPushButton::clicked, colorBox, [this, colorBox]() {
            const QColor c = QColorDialog::getColor(
                QColor(prefs_->traceColor()), colorBox,
                tr("Spectrum trace colour"));
            if (c.isValid()) prefs_->setTraceColor(c.name());
        });
        connect(prefs_, &Prefs::traceColorChanged, colorBox,
                [this, setSwatch]() { setSwatch(prefs_->traceColor()); });

        // Mode visibility: Solid -> colour box; By-strength -> palette
        // combo (+ the custom base-colour swatch only when "Custom…").
        auto applyMode = [colorBox, palCombo, palPreview, strSwatch,
                          customIdx](int m) {
            const bool byStr = (m == 1);
            colorBox->setVisible(!byStr);
            palCombo->setVisible(byStr);
            palPreview->setVisible(byStr);
            strSwatch->setVisible(byStr && palCombo->currentIndex() == customIdx);
        };
        applyMode(prefs_->traceMode());
        connect(mode, &QComboBox::currentIndexChanged, this,
                [this, applyMode](int m) { prefs_->setTraceMode(m); applyMode(m); });
        connect(prefs_, &Prefs::traceModeChanged, box,
                [this, mode, applyMode]() {
            if (mode->currentIndex() != prefs_->traceMode())
                mode->setCurrentIndex(prefs_->traceMode());
            applyMode(prefs_->traceMode());
        });
        // Palette change can flip to/from Custom -> re-evaluate the swatch.
        connect(palCombo, &QComboBox::currentIndexChanged, box,
                [mode, applyMode](int) { applyMode(mode->currentIndex()); });

        form->addRow(tr("Trace color"), box);
    }

    // --- Waterfall palette (INDEPENDENT of the trace palette) ---
    // The operator can run, e.g., an Ocean trace over a Rainbow
    // waterfall.  New palettes added later become choices for both.
    {
        auto *wbox = new QWidget(page);
        auto *wv = new QVBoxLayout(wbox);
        wv->setContentsMargins(0, 0, 0, 0);
        wv->setSpacing(4);

        auto *wpal = new QComboBox(wbox);
        wpal->addItems(lyra::palettes::names());
        const int wCustomIdx = wpal->count();
        wpal->addItem(tr("Custom color…"));
        wpal->setCurrentIndex(prefs_->waterfallPalette());
        connect(wpal, &QComboBox::currentIndexChanged,
                prefs_, &Prefs::setWaterfallPalette);
        connect(prefs_, &Prefs::waterfallPaletteChanged, wbox, [this, wpal]() {
            if (wpal->currentIndex() != prefs_->waterfallPalette())
                wpal->setCurrentIndex(prefs_->waterfallPalette());
        });
        wv->addWidget(wpal);

        // Live gradient preview (same builder as the trace palette).
        auto *wprev = new QLabel(wbox);
        wprev->setFixedHeight(16);
        wprev->setMinimumWidth(180);
        wprev->setScaledContents(true);
        wprev->setToolTip(tr("Weak signal (left) → strong (right)"));
        wprev->setStyleSheet(QStringLiteral(
            "QLabel{border:1px solid #2a3a4a;border-radius:3px;}"));
        auto wUpdatePreview = [this, wprev, wCustomIdx](int idx) {
            QImage img(256, 1, QImage::Format_RGB888);
            if (idx == wCustomIdx) {
                const QColor base(prefs_->waterfallColor());
                for (int i = 0; i < 256; ++i) {
                    const double t = i / 255.0;
                    int r, g, b;
                    if (t <= 0.65) {
                        const double f = t / 0.65;
                        r = int(base.red() * f);
                        g = int(base.green() * f);
                        b = int(base.blue() * f);
                    } else {
                        const double f = (t - 0.65) / 0.35;
                        r = int(base.red()   + (255 - base.red())   * f * 0.7);
                        g = int(base.green() + (255 - base.green()) * f * 0.7);
                        b = int(base.blue()  + (255 - base.blue())  * f * 0.7);
                    }
                    img.setPixelColor(i, 0, QColor(std::clamp(r, 0, 255),
                                                   std::clamp(g, 0, 255),
                                                   std::clamp(b, 0, 255)));
                }
            } else {
                const auto &l = lyra::palettes::lut(idx);
                for (int i = 0; i < 256; ++i)
                    img.setPixelColor(i, 0, QColor(l[i][0], l[i][1], l[i][2]));
            }
            wprev->setPixmap(QPixmap::fromImage(img));
        };
        wUpdatePreview(prefs_->waterfallPalette());
        connect(wpal, &QComboBox::currentIndexChanged, wbox, wUpdatePreview);
        connect(prefs_, &Prefs::waterfallColorChanged, wbox,
                [wpal, wUpdatePreview]() { wUpdatePreview(wpal->currentIndex()); });
        wv->addWidget(wprev);

        // Custom-ramp base colour (shown only when "Custom color…").
        auto *wswatch = new QPushButton(wbox);
        wswatch->setFixedSize(64, 22);
        wswatch->setToolTip(tr("Base colour for the custom waterfall ramp"));
        auto wSetSwatch = [wswatch](const QString &hex) {
            wswatch->setStyleSheet(QStringLiteral(
                "QPushButton{background:%1;border:1px solid #2a3a4a;"
                "border-radius:3px;}").arg(hex));
        };
        wSetSwatch(prefs_->waterfallColor());
        connect(wswatch, &QPushButton::clicked, wbox, [this, wbox]() {
            const QColor c = QColorDialog::getColor(
                QColor(prefs_->waterfallColor()), wbox,
                tr("Waterfall base colour"));
            if (c.isValid()) prefs_->setWaterfallColor(c.name());
        });
        connect(prefs_, &Prefs::waterfallColorChanged, wbox,
                [this, wSetSwatch]() { wSetSwatch(prefs_->waterfallColor()); });
        wv->addWidget(wswatch);

        auto wApplyVis = [wswatch, wCustomIdx](int idx) {
            wswatch->setVisible(idx == wCustomIdx);
        };
        wApplyVis(prefs_->waterfallPalette());
        connect(wpal, &QComboBox::currentIndexChanged, wbox, wApplyVis);

        form->addRow(tr("Waterfall palette"), wbox);
    }

    // ---- Switch to the middle column (see scaffolding comment at the
    // top of this function). -----------------------------------------
    {
        auto *spacer = new QWidget(leftCol);
        spacer->setSizePolicy(QSizePolicy::Preferred,
                              QSizePolicy::Expanding);
        form->addRow(spacer);
    }
    form = midForm;

    // --- Waterfall speed (history rows per second) ---
    auto *wspeed = new QSpinBox(page);
    wspeed->setRange(1, 120);
    wspeed->setSuffix(tr(" rows/s"));
    wspeed->setValue(prefs_->waterfallSpeed());
    wspeed->setToolTip(tr("How fast the waterfall scrolls (decoupled from "
                          "the render frame rate). Between rows, incoming "
                          "frames are peak-held so brief signals still show "
                          "at slow speeds."));
    connect(wspeed, &QSpinBox::valueChanged, prefs_, &Prefs::setWaterfallSpeed);
    connect(prefs_, &Prefs::waterfallSpeedChanged, page, [this, wspeed]() {
        if (wspeed->value() != prefs_->waterfallSpeed())
            wspeed->setValue(prefs_->waterfallSpeed());
    });
    form->addRow(tr("Waterfall speed"), wspeed);

    // --- Waterfall dB range (INDEPENDENT of the panadapter scale) ---
    // Auto-fit OR a manual floor/ceiling.  The manual spinboxes gray
    // out while Auto is on.
    auto *wfAuto = new QCheckBox(tr("Auto-fit the waterfall dB range"), page);
    wfAuto->setChecked(prefs_->waterfallDbAuto());
    wfAuto->setToolTip(tr("Track the band automatically (noise floor − 15 dB "
                          "to peak + 15 dB). Off = use the floor/ceiling "
                          "below."));
    connect(wfAuto, &QCheckBox::toggled, prefs_, &Prefs::setWaterfallDbAuto);
    form->addRow(tr("Waterfall dB"), wfAuto);

    // §15.30 — TWO independent pairs (RX-state and TX-state) so an
    // operator can tune the TX waterfall scale without keying, and so
    // the RX pair (their tuned dark-with-pop preference) never gets
    // overwritten by a drag during MOX (or vice versa).  Both pairs
    // are always-visible: the RX spinboxes always read+write the RX
    // backing; the TX spinboxes always read+write the TX backing.
    //
    // The live `waterfallDbMin()/Max()` accessors still MOX-route
    // (drag-the-right-edge during MOX updates whichever state is
    // active) — these spinboxes just bypass that and address each
    // backing directly via setRxWaterfallDb*/setTxWaterfallDb*.

    // --- RX-state waterfall (the operator's normal listening scale) ---
    auto *wfMin = new QDoubleSpinBox(page);
    wfMin->setRange(-200.0, 0.0);
    wfMin->setSuffix(tr(" dB"));
    wfMin->setValue(prefs_->rxWaterfallDbMin());
    wfMin->setToolTip(tr("RX-state waterfall floor — levels at/below this "
                         "map to the darkest palette colour.  Active when "
                         "MOX is OFF.  Drag the waterfall's right-edge "
                         "during RX to tune this from the panadapter."));
    connect(wfMin, &QDoubleSpinBox::valueChanged,
            prefs_, &Prefs::setRxWaterfallDbMin);
    connect(prefs_, &Prefs::waterfallDbMinChanged, page, [this, wfMin]() {
        // Mirror RX changes that came from elsewhere (drag during RX,
        // band-memory recall, etc.).  RX backing only — TX changes
        // arrive on txWaterfallDbMinChanged below.
        if (wfMin->value() != prefs_->rxWaterfallDbMin())
            wfMin->setValue(prefs_->rxWaterfallDbMin());
    });
    form->addRow(tr("Waterfall dB — RX floor"), wfMin);

    auto *wfMax = new QDoubleSpinBox(page);
    wfMax->setRange(-200.0, 20.0);
    wfMax->setSuffix(tr(" dB"));
    wfMax->setValue(prefs_->rxWaterfallDbMax());
    wfMax->setToolTip(tr("RX-state waterfall ceiling — levels at/above this "
                         "map to the brightest palette colour.  Active when "
                         "MOX is OFF."));
    connect(wfMax, &QDoubleSpinBox::valueChanged,
            prefs_, &Prefs::setRxWaterfallDbMax);
    connect(prefs_, &Prefs::waterfallDbMaxChanged, page, [this, wfMax]() {
        if (wfMax->value() != prefs_->rxWaterfallDbMax())
            wfMax->setValue(prefs_->rxWaterfallDbMax());
    });
    form->addRow(tr("Waterfall dB — RX ceiling"), wfMax);

    // --- TX-state waterfall (active while MOX is on) ---
    // Defaults −70 / +30 dBFS match the reference TX waterfall
    // Low / High Level for HL2 TX drive levels — a clean tune-carrier
    // line that doesn't blow out the heat map.  Tune from here OR by
    // dragging the waterfall's right-edge during MOX.
    auto *wfTxMin = new QDoubleSpinBox(page);
    wfTxMin->setRange(-200.0, 30.0);
    wfTxMin->setSuffix(tr(" dB"));
    wfTxMin->setValue(prefs_->txWaterfallDbMin());
    wfTxMin->setToolTip(tr("TX-state waterfall floor — active only while "
                           "MOX is ON.  Default −70 dB frames a clean "
                           "TX-tone line; the operator's RX preferences "
                           "are preserved separately."));
    connect(wfTxMin, &QDoubleSpinBox::valueChanged,
            prefs_, &Prefs::setTxWaterfallDbMin);
    connect(prefs_, &Prefs::txWaterfallDbMinChanged, page, [this, wfTxMin]() {
        if (wfTxMin->value() != prefs_->txWaterfallDbMin())
            wfTxMin->setValue(prefs_->txWaterfallDbMin());
    });
    form->addRow(tr("Waterfall dB — TX floor"), wfTxMin);

    auto *wfTxMax = new QDoubleSpinBox(page);
    wfTxMax->setRange(-200.0, 60.0);
    wfTxMax->setSuffix(tr(" dB"));
    wfTxMax->setValue(prefs_->txWaterfallDbMax());
    wfTxMax->setToolTip(tr("TX-state waterfall ceiling — active only while "
                           "MOX is ON.  Default +30 dB matches the "
                           "reference TX waterfall High Level."));
    connect(wfTxMax, &QDoubleSpinBox::valueChanged,
            prefs_, &Prefs::setTxWaterfallDbMax);
    connect(prefs_, &Prefs::txWaterfallDbMaxChanged, page, [this, wfTxMax]() {
        if (wfTxMax->value() != prefs_->txWaterfallDbMax())
            wfTxMax->setValue(prefs_->txWaterfallDbMax());
    });
    form->addRow(tr("Waterfall dB — TX ceiling"), wfTxMax);

    // Gray ALL four manual floor/ceiling spinboxes out while Auto is on
    // (Auto applies to whichever pair is currently live — driven by
    // the panadapter's noise-floor / peak tracking).
    auto applyWfAuto = [wfMin, wfMax, wfTxMin, wfTxMax](bool a) {
        wfMin->setEnabled(!a);
        wfMax->setEnabled(!a);
        wfTxMin->setEnabled(!a);
        wfTxMax->setEnabled(!a);
    };
    applyWfAuto(prefs_->waterfallDbAuto());
    connect(prefs_, &Prefs::waterfallDbAutoChanged, page,
            [this, wfAuto, applyWfAuto]() {
                const bool a = prefs_->waterfallDbAuto();
                if (wfAuto->isChecked() != a) wfAuto->setChecked(a);
                applyWfAuto(a);
            });

    // --- Spectrum fill on/off + fill colour ---
    // The fill colour applies in SOLID trace mode (lets the fill differ
    // from the trace line, old-Lyra parity); in by-strength mode the
    // fill follows the intensity palette.
    {
        auto *fbox = new QWidget(page);
        auto *fh = new QHBoxLayout(fbox);
        fh->setContentsMargins(0, 0, 0, 0);
        fh->setSpacing(8);

        auto *fill = new QCheckBox(tr("Show fill beneath the trace"), fbox);
        fill->setChecked(prefs_->fillEnabled());
        connect(fill, &QCheckBox::toggled, prefs_, &Prefs::setFillEnabled);
        connect(prefs_, &Prefs::fillEnabledChanged, fbox, [this, fill]() {
            if (fill->isChecked() != prefs_->fillEnabled())
                fill->setChecked(prefs_->fillEnabled());
        });
        fh->addWidget(fill);

        fh->addWidget(new QLabel(tr("Colour:"), fbox));
        auto *fswatch = new QPushButton(fbox);
        fswatch->setFixedSize(44, 22);
        fswatch->setToolTip(tr("Spectrum fill colour (solid trace mode)"));
        auto fSet = [fswatch](const QString &hex) {
            fswatch->setStyleSheet(QStringLiteral(
                "QPushButton{background:%1;border:1px solid #2a3a4a;"
                "border-radius:3px;}").arg(hex));
        };
        fSet(prefs_->fillColor());
        connect(fswatch, &QPushButton::clicked, fbox, [this, fbox]() {
            const QColor c = QColorDialog::getColor(
                QColor(prefs_->fillColor()), fbox, tr("Spectrum fill colour"));
            if (c.isValid()) prefs_->setFillColor(c.name());
        });
        connect(prefs_, &Prefs::fillColorChanged, fbox,
                [this, fSet]() { fSet(prefs_->fillColor()); });
        fh->addWidget(fswatch);
        fh->addStretch(1);

        form->addRow(tr("Spectrum fill"), fbox);
    }

    // --- Peak-hold markers (mode/hold-time + Clear live on the Display
    // panel; here are the look-and-feel knobs) ---
    {
        auto *pbox = new QWidget(page);
        auto *pv = new QVBoxLayout(pbox);
        pv->setContentsMargins(0, 0, 0, 0);
        pv->setSpacing(4);

        // Row 1: enable + style + show-dB + colour.
        auto *r1 = new QHBoxLayout();
        auto *pEn = new QCheckBox(tr("Show peak markers"), pbox);
        pEn->setChecked(prefs_->peakEnabled());
        connect(pEn, &QCheckBox::toggled, prefs_, &Prefs::setPeakEnabled);
        connect(prefs_, &Prefs::peakEnabledChanged, pbox, [this, pEn]() {
            if (pEn->isChecked() != prefs_->peakEnabled())
                pEn->setChecked(prefs_->peakEnabled());
        });
        r1->addWidget(pEn);

        r1->addWidget(new QLabel(tr("Style:"), pbox));
        auto *pStyle = new QComboBox(pbox);
        pStyle->addItems({tr("Line"), tr("Dots"), tr("Triangles")});
        pStyle->setCurrentIndex(prefs_->peakStyle());
        connect(pStyle, &QComboBox::currentIndexChanged,
                prefs_, &Prefs::setPeakStyle);
        connect(prefs_, &Prefs::peakStyleChanged, pbox, [this, pStyle]() {
            if (pStyle->currentIndex() != prefs_->peakStyle())
                pStyle->setCurrentIndex(prefs_->peakStyle());
        });
        r1->addWidget(pStyle);

        auto *pShow = new QCheckBox(tr("Show dB"), pbox);
        pShow->setChecked(prefs_->peakShowDb());
        connect(pShow, &QCheckBox::toggled, prefs_, &Prefs::setPeakShowDb);
        connect(prefs_, &Prefs::peakShowDbChanged, pbox, [this, pShow]() {
            if (pShow->isChecked() != prefs_->peakShowDb())
                pShow->setChecked(prefs_->peakShowDb());
        });
        r1->addWidget(pShow);

        r1->addWidget(new QLabel(tr("Colour:"), pbox));
        auto *pSwatch = new QPushButton(pbox);
        pSwatch->setFixedSize(44, 22);
        pSwatch->setToolTip(tr("Peak marker colour"));
        auto pSet = [pSwatch](const QString &hex) {
            pSwatch->setStyleSheet(QStringLiteral(
                "QPushButton{background:%1;border:1px solid #2a3a4a;"
                "border-radius:3px;}").arg(hex));
        };
        pSet(prefs_->peakColor());
        connect(pSwatch, &QPushButton::clicked, pbox, [this, pbox]() {
            const QColor c = QColorDialog::getColor(
                QColor(prefs_->peakColor()), pbox, tr("Peak marker colour"));
            if (c.isValid()) prefs_->setPeakColor(c.name());
        });
        connect(prefs_, &Prefs::peakColorChanged, pbox,
                [this, pSet]() { pSet(prefs_->peakColor()); });
        r1->addWidget(pSwatch);
        r1->addStretch(1);
        pv->addLayout(r1);

        // Row 2: decay rate (dB/s) — applies to the timed hold modes.
        auto *r2 = new QHBoxLayout();
        r2->addWidget(new QLabel(tr("Decay:"), pbox));
        auto *pDecay = new QSpinBox(pbox);
        pDecay->setRange(1, 120);
        pDecay->setSuffix(tr(" dB/s"));
        pDecay->setValue(static_cast<int>(prefs_->peakDecayDbps() + 0.5));
        pDecay->setToolTip(tr("How fast held peaks fall once past their "
                              "hold window (timed modes)."));
        connect(pDecay, &QSpinBox::valueChanged, prefs_,
                [this](int v) { prefs_->setPeakDecayDbps(v); });
        connect(prefs_, &Prefs::peakDecayDbpsChanged, pbox, [this, pDecay]() {
            const int v = static_cast<int>(prefs_->peakDecayDbps() + 0.5);
            if (pDecay->value() != v) pDecay->setValue(v);
        });
        r2->addWidget(pDecay);
        r2->addStretch(1);
        pv->addLayout(r2);

        form->addRow(tr("Peak markers"), pbox);
    }

    // --- Noise-floor reference line (dashed line at the rolling
    // ~20th-percentile floor + an "NF -NN dBFS" label) ---
    {
        auto *nbox = new QWidget(page);
        auto *nh = new QHBoxLayout(nbox);
        nh->setContentsMargins(0, 0, 0, 0);
        nh->setSpacing(8);

        auto *nfEn = new QCheckBox(tr("Show noise-floor line"), nbox);
        nfEn->setChecked(prefs_->noiseFloorEnabled());
        connect(nfEn, &QCheckBox::toggled, prefs_, &Prefs::setNoiseFloorEnabled);
        connect(prefs_, &Prefs::noiseFloorEnabledChanged, nbox, [this, nfEn]() {
            if (nfEn->isChecked() != prefs_->noiseFloorEnabled())
                nfEn->setChecked(prefs_->noiseFloorEnabled());
        });
        nh->addWidget(nfEn);

        nh->addWidget(new QLabel(tr("Colour:"), nbox));
        auto *nfSwatch = new QPushButton(nbox);
        nfSwatch->setFixedSize(44, 22);
        nfSwatch->setToolTip(tr("Noise-floor line colour"));
        auto nfSet = [nfSwatch](const QString &hex) {
            nfSwatch->setStyleSheet(QStringLiteral(
                "QPushButton{background:%1;border:1px solid #2a3a4a;"
                "border-radius:3px;}").arg(hex));
        };
        nfSet(prefs_->noiseFloorColor());
        connect(nfSwatch, &QPushButton::clicked, nbox, [this, nbox]() {
            const QColor c = QColorDialog::getColor(
                QColor(prefs_->noiseFloorColor()), nbox,
                tr("Noise-floor line colour"));
            if (c.isValid()) prefs_->setNoiseFloorColor(c.name());
        });
        connect(prefs_, &Prefs::noiseFloorColorChanged, nbox,
                [this, nfSet]() { nfSet(prefs_->noiseFloorColor()); });
        nh->addWidget(nfSwatch);
        nh->addStretch(1);

        form->addRow(tr("Noise floor"), nbox);
    }

    // --- Trace smoothing (temporal EWMA, 0 = off) ---
    auto *smooth = new QSpinBox(page);
    smooth->setRange(0, 10);
    smooth->setSpecialValueText(tr("Off"));   // shown at 0
    smooth->setValue(prefs_->smoothing());
    smooth->setToolTip(tr("Smooths the trace over time (higher = "
                          "smoother but slower response). 0 = off."));
    connect(smooth, &QSpinBox::valueChanged, prefs_, &Prefs::setSmoothing);
    connect(prefs_, &Prefs::smoothingChanged, page, [this, smooth]() {
        if (smooth->value() != prefs_->smoothing())
            smooth->setValue(prefs_->smoothing());
    });
    form->addRow(tr("Trace smoothing"), smooth);

    // --- Peak glow (GPU bloom around the bright trace, 0 = off) ---
    auto *glow = new QSpinBox(page);
    glow->setRange(0, 100);
    glow->setSuffix(tr(" %"));
    glow->setSpecialValueText(tr("Off"));   // shown at 0
    glow->setValue(prefs_->glow());
    glow->setToolTip(tr("Luminous bloom around the bright parts of the "
                        "trace. 0 = off."));
    connect(glow, &QSpinBox::valueChanged, prefs_, &Prefs::setGlow);
    connect(prefs_, &Prefs::glowChanged, page, [this, glow]() {
        if (glow->value() != prefs_->glow()) glow->setValue(prefs_->glow());
    });
    form->addRow(tr("Peak glow"), glow);

    // --- Glass sheen (diagonal reflection over the panel, 0 = off) ---
    auto *sheen = new QSpinBox(page);
    sheen->setRange(0, 100);
    sheen->setSuffix(tr(" %"));
    sheen->setSpecialValueText(tr("Off"));
    sheen->setValue(prefs_->glassSheen());
    sheen->setToolTip(tr("Subtle diagonal glass reflection over the "
                         "panadapter. 0 = off."));
    connect(sheen, &QSpinBox::valueChanged, prefs_, &Prefs::setGlassSheen);
    connect(prefs_, &Prefs::glassSheenChanged, page, [this, sheen]() {
        if (sheen->value() != prefs_->glassSheen())
            sheen->setValue(prefs_->glassSheen());
    });
    form->addRow(tr("Glass sheen"), sheen);

    // --- Lyra constellation watermark ---
    auto *wm = new QCheckBox(tr("Show Lyra constellation watermark"), page);
    wm->setChecked(prefs_->watermark());
    connect(wm, &QCheckBox::toggled, prefs_, &Prefs::setWatermark);
    connect(prefs_, &Prefs::watermarkChanged, page, [this, wm]() {
        if (wm->isChecked() != prefs_->watermark())
            wm->setChecked(prefs_->watermark());
    });
    form->addRow(tr("Watermark"), wm);

    // ---- Switch to the right column. -------------------------------
    {
        auto *spacer = new QWidget(midCol);
        spacer->setSizePolicy(QSizePolicy::Preferred,
                              QSizePolicy::Expanding);
        form->addRow(spacer);
    }
    form = rightForm;

    // --- Meteor streaks (rare ambient shooting stars) ---
    auto *met = new QCheckBox(tr("Show occasional meteor streaks"), page);
    met->setChecked(prefs_->meteors());
    connect(met, &QCheckBox::toggled, prefs_, &Prefs::setMeteors);
    connect(prefs_, &Prefs::meteorsChanged, page, [this, met]() {
        if (met->isChecked() != prefs_->meteors())
            met->setChecked(prefs_->meteors());
    });
    form->addRow(tr("Meteors"), met);

    // Average seconds between meteors (spread 0.5x..1.6x around this).
    auto *metGap = new QSpinBox(page);
    metGap->setRange(5, 120);
    metGap->setSingleStep(5);
    metGap->setSuffix(tr(" s"));
    metGap->setValue(prefs_->meteorGap());
    metGap->setToolTip(tr("Average gap between meteors; lower = more frequent."));
    connect(metGap, &QSpinBox::valueChanged, prefs_, &Prefs::setMeteorGap);
    connect(prefs_, &Prefs::meteorGapChanged, page, [this, metGap]() {
        if (metGap->value() != prefs_->meteorGap())
            metGap->setValue(prefs_->meteorGap());
    });
    form->addRow(tr("Meteor frequency"), metGap);

    // Percent chance a meteor is a warm-gold fireball (vs cool blue).
    auto *metGold = new QSpinBox(page);
    metGold->setRange(0, 100);
    metGold->setSuffix(tr(" %"));
    metGold->setValue(prefs_->meteorGold());
    metGold->setToolTip(tr("Chance a meteor is a warm-gold fireball; "
                           "0 = always cool blue."));
    connect(metGold, &QSpinBox::valueChanged, prefs_, &Prefs::setMeteorGold);
    connect(prefs_, &Prefs::meteorGoldChanged, page, [this, metGold]() {
        if (metGold->value() != prefs_->meteorGold())
            metGold->setValue(prefs_->meteorGold());
    });
    form->addRow(tr("Gold fireballs"), metGold);

    // --- Gridline brightness (0 = off, 100 = bright) ---
    auto *grid = new QSpinBox(page);
    grid->setRange(0, 100);
    grid->setSpecialValueText(tr("Off"));   // shown at 0
    grid->setValue(prefs_->gridLevel());
    connect(grid, &QSpinBox::valueChanged, prefs_, &Prefs::setGridLevel);
    connect(prefs_, &Prefs::gridLevelChanged, page, [this, grid]() {
        if (grid->value() != prefs_->gridLevel()) grid->setValue(prefs_->gridLevel());
    });
    form->addRow(tr("Gridline brightness"), grid);

    // --- Cursor frequency readout (floats near the pointer) ---
    auto *curRdt = new QCheckBox(
        tr("Show frequency readout at the cursor"), page);
    curRdt->setChecked(prefs_->cursorReadout());
    curRdt->setToolTip(tr("A small frequency label that follows the mouse "
                          "over the panadapter."));
    connect(curRdt, &QCheckBox::toggled, prefs_, &Prefs::setCursorReadout);
    connect(prefs_, &Prefs::cursorReadoutChanged, page, [this, curRdt]() {
        if (curRdt->isChecked() != prefs_->cursorReadout())
            curRdt->setChecked(prefs_->cursorReadout());
    });
    form->addRow(tr("Cursor readout"), curRdt);

    // --- Zero-beat markers (± needle under the freq readout, CW/AM/SAM/FM) ---
    auto *zbeat = new QCheckBox(tr("Show Zero-beat markers"), page);
    zbeat->setChecked(prefs_->zeroBeatMarkers());
    zbeat->setToolTip(tr("A small ± tuning needle under the frequency readout "
                         "showing how far a CW / AM / SAM / FM carrier sits from "
                         "your dialled frequency — centre it to zero-beat."));
    connect(zbeat, &QCheckBox::toggled, prefs_, &Prefs::setZeroBeatMarkers);
    connect(prefs_, &Prefs::zeroBeatMarkersChanged, page, [this, zbeat]() {
        if (zbeat->isChecked() != prefs_->zeroBeatMarkers())
            zbeat->setChecked(prefs_->zeroBeatMarkers());
    });
    form->addRow(tr("Zero-beat"), zbeat);

    // --- Tooltips on/off (global) ---
    auto *tips = new QCheckBox(tr("Show tooltips"), page);
    tips->setChecked(prefs_->tooltipsEnabled());
    tips->setToolTip(tr("Show the hover help bubbles on controls across the "
                        "whole app. Turn off for a quieter, no-popup UI."));
    connect(tips, &QCheckBox::toggled, prefs_, &Prefs::setTooltipsEnabled);
    connect(prefs_, &Prefs::tooltipsEnabledChanged, page, [this, tips]() {
        if (tips->isChecked() != prefs_->tooltipsEnabled())
            tips->setChecked(prefs_->tooltipsEnabled());
    });
    form->addRow(tr("Tooltips"), tips);

    // --- Target frame rate (fps) ---
    auto *fps = new QSpinBox(page);
    fps->setRange(1, 240);
    fps->setSuffix(tr(" fps"));
    fps->setValue(prefs_->targetFps());
    connect(fps, &QSpinBox::valueChanged, prefs_, &Prefs::setTargetFps);
    connect(prefs_, &Prefs::targetFpsChanged, page, [this, fps]() {
        if (fps->value() != prefs_->targetFps()) fps->setValue(prefs_->targetFps());
    });
    form->addRow(tr("Frame rate"), fps);

    // --- dB display range (vertical scale) ---
    // Auto-fit OR a manual floor/ceiling.  The manual spinboxes gray
    // out while Auto is on (the panadapter auto-fits internally).
    auto *pdAuto = new QCheckBox(tr("Auto-fit the spectrum dB range"), page);
    pdAuto->setChecked(prefs_->dbAuto());
    pdAuto->setToolTip(tr("Track the band automatically (noise floor − 15 dB "
                          "to peak + 15 dB). Off = use the floor/ceiling "
                          "below. Dragging the right-edge dB scale also "
                          "turns this off."));
    connect(pdAuto, &QCheckBox::toggled, prefs_, &Prefs::setDbAuto);
    form->addRow(tr("Spectrum dB"), pdAuto);

    auto *dbMin = new QDoubleSpinBox(page);
    dbMin->setRange(-200.0, 0.0);
    dbMin->setSuffix(tr(" dB"));
    dbMin->setValue(prefs_->dbMin());
    connect(dbMin, &QDoubleSpinBox::valueChanged, prefs_, &Prefs::setDbMin);
    connect(prefs_, &Prefs::dbMinChanged, page, [this, dbMin]() {
        if (dbMin->value() != prefs_->dbMin()) dbMin->setValue(prefs_->dbMin());
    });
    form->addRow(tr("dB range — floor"), dbMin);

    auto *dbMax = new QDoubleSpinBox(page);
    dbMax->setRange(-200.0, 30.0);
    dbMax->setSuffix(tr(" dB"));
    dbMax->setValue(prefs_->dbMax());
    connect(dbMax, &QDoubleSpinBox::valueChanged, prefs_, &Prefs::setDbMax);
    connect(prefs_, &Prefs::dbMaxChanged, page, [this, dbMax]() {
        if (dbMax->value() != prefs_->dbMax()) dbMax->setValue(prefs_->dbMax());
    });
    form->addRow(tr("dB range — ceiling"), dbMax);

    // Gray the manual floor/ceiling out while Auto is on, and keep the
    // checkbox in sync if Auto is flipped elsewhere (e.g. a dB-edge drag
    // on the panadapter turns it off).
    auto applyPdAuto = [dbMin, dbMax](bool a) {
        dbMin->setEnabled(!a);
        dbMax->setEnabled(!a);
    };
    applyPdAuto(prefs_->dbAuto());
    connect(prefs_, &Prefs::dbAutoChanged, page,
            [this, pdAuto, applyPdAuto]() {
                const bool a = prefs_->dbAuto();
                if (pdAuto->isChecked() != a) pdAuto->setChecked(a);
                applyPdAuto(a);
            });

    // --- Graphics backend (advanced; restart to apply) ---
    // The Qt RHI backend is fixed at startup, so this writes a QSettings
    // key (read in main.cpp before the app launches) rather than applying
    // live.  "Auto" lets Qt pick per platform; explicit choices still
    // fall back transparently if unavailable.  Stored OUTSIDE Prefs since
    // it isn't a live display option.
    {
        auto *gbox = new QWidget(page);
        auto *gh = new QHBoxLayout(gbox);
        gh->setContentsMargins(0, 0, 0, 0);
        gh->setSpacing(8);

        auto *gfx = new QComboBox(gbox);
        gfx->addItem(tr("Auto (recommended)"), QStringLiteral("auto"));
        gfx->addItem(QStringLiteral("Vulkan"),        QStringLiteral("vulkan"));
        gfx->addItem(QStringLiteral("Direct3D 12"),   QStringLiteral("d3d12"));
        gfx->addItem(QStringLiteral("Direct3D 11"),   QStringLiteral("d3d11"));
        gfx->addItem(QStringLiteral("OpenGL"),        QStringLiteral("opengl"));
        const QString cur = QSettings()
            .value(QStringLiteral("ui/graphicsBackend"),
                   QStringLiteral("auto")).toString().toLower();
        const int idx = std::max(0, gfx->findData(cur));
        gfx->setCurrentIndex(idx);
        connect(gfx, &QComboBox::currentIndexChanged, gfx, [gfx](int i) {
            QSettings s;
            s.setValue(QStringLiteral("ui/graphicsBackend"),
                       gfx->itemData(i).toString());
            // The operator is taking control, so leave graphics safe mode
            // (a prior startup crash may have forced OpenGL) — their pick is
            // honoured next launch.
            s.remove(QStringLiteral("ui/gfxSafeMode"));
            s.remove(QStringLiteral("ui/gfxSafeDepth"));
        });
        gh->addWidget(gfx);

        auto *note = new QLabel(tr("Restart Lyra to apply"), gbox);
        note->setStyleSheet(QStringLiteral("QLabel{color:#8a9aac;}"));
        gh->addWidget(note);
        gh->addStretch(1);

        form->addRow(tr("Graphics backend"), gbox);

        // If a prior startup crash forced graphics safe mode, say so here and
        // point back to this control (the notice at launch does too).
        if (QSettings().value(QStringLiteral("ui/gfxSafeMode"), false).toBool()) {
            auto *sm = new QLabel(
                tr("⚠  Running in graphics safe mode after a startup problem — "
                   "pick a backend above (then restart) to leave it."), page);
            sm->setWordWrap(true);
            sm->setStyleSheet(QStringLiteral("QLabel{color:#ffb74d;}"));
            form->addRow(QString(), sm);
        }
    }

    return page;
}

QWidget *SettingsDialog::buildNoiseTab() {
    // Captured noise-profile manager: list the saved profiles (one .lnp
    // file each) with their rate/FFT/date, and rename / delete (multi) /
    // open the folder.  Live capture + apply + tuning live on the
    // DSP+Audio panel; this tab is curation.
    auto *page = new QWidget(this);
    auto *outer = new QVBoxLayout(page);

    auto *grp = new QGroupBox(tr("Captured noise profiles"), page);
    auto *g = new QVBoxLayout(grp);

    auto *list = new QListWidget(grp);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto refresh = [this, list]() {
        list->clear();
        const QStringList names = engine_->noiseProfiles();
        for (const QString &n : names) {
            const QString info = engine_->noiseProfileInfo(n);
            auto *it = new QListWidgetItem(
                info.isEmpty() ? n : QStringLiteral("%1   —   %2").arg(n, info));
            it->setData(Qt::UserRole, n);
            list->addItem(it);
        }
    };
    refresh();
    connect(engine_, &lyra::dsp::WdspEngine::noiseProfilesChanged, list, refresh);
    g->addWidget(list);

    auto *btnRow = new QHBoxLayout;
    auto *renameBtn = new QPushButton(tr("Rename…"), grp);
    auto *delBtn    = new QPushButton(tr("Delete"), grp);
    auto *revealBtn = new QPushButton(tr("Open folder"), grp);
    btnRow->addWidget(renameBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(revealBtn);
    g->addLayout(btnRow);

    auto selectionUpdate = [list, renameBtn, delBtn]() {
        const int n = list->selectedItems().size();
        renameBtn->setEnabled(n == 1);
        delBtn->setEnabled(n >= 1);
    };
    selectionUpdate();
    connect(list, &QListWidget::itemSelectionChanged, grp, selectionUpdate);
    connect(engine_, &lyra::dsp::WdspEngine::noiseProfilesChanged, grp, selectionUpdate);

    connect(renameBtn, &QPushButton::clicked, grp, [this, list]() {
        const auto items = list->selectedItems();
        if (items.size() != 1) return;
        const QString old = items.first()->data(Qt::UserRole).toString();
        bool ok = false;
        const QString nn = QInputDialog::getText(
            this, tr("Rename profile"), tr("New name:"),
            QLineEdit::Normal, old, &ok);
        if (ok && !nn.trimmed().isEmpty()) {
            if (!engine_->renameNoiseProfile(old, nn))
                QMessageBox::information(this, tr("Rename"),
                    tr("Couldn't rename — that name is already in use."));
        }
    });

    connect(delBtn, &QPushButton::clicked, grp, [this, list]() {
        const auto items = list->selectedItems();
        if (items.isEmpty()) return;
        const int n = items.size();
        if (QMessageBox::question(this, tr("Delete profiles"),
                tr("Delete %1 selected profile%2? This removes the file(s).")
                    .arg(n).arg(n == 1 ? QString() : QStringLiteral("s")))
            != QMessageBox::Yes)
            return;
        QStringList names;
        for (auto *it : items) names << it->data(Qt::UserRole).toString();
        for (const QString &nm : names) engine_->deleteNoiseProfile(nm);
    });

    connect(revealBtn, &QPushButton::clicked, grp, [this]() {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(engine_->noiseProfilesDir()));
    });

    outer->addWidget(grp);

    // ── Auto-load on startup ──
    // Pick a saved profile to load automatically each launch, so NR-C comes
    // up ready instead of an empty field.  If NR-C was left on last session
    // it re-enables too — but only if the chosen profile still matches the
    // current sample rate (loadNoiseProfile self-guards; a mismatch loads
    // nothing and leaves NR-C off).  Restore itself lives in main.cpp.
    auto *autoGrp  = new QGroupBox(tr("Auto-load on startup"), page);
    auto *autoForm = new QFormLayout(autoGrp);
    auto *autoCombo = new QComboBox(autoGrp);
    autoCombo->setMaximumWidth(280);

    auto repopAuto = [this, autoCombo]() {
        const QString saved = QSettings()
            .value(QStringLiteral("dsp/noiseAutoLoadProfile")).toString();
        QSignalBlocker block(autoCombo);
        autoCombo->clear();
        autoCombo->addItem(tr("None (start with NR-C off)"), QString());
        int sel = 0;
        const QStringList names = engine_->noiseProfiles();
        for (const QString &n : names) {
            autoCombo->addItem(n, n);
            if (n == saved) sel = autoCombo->count() - 1;
        }
        autoCombo->setCurrentIndex(sel);
    };
    repopAuto();
    // Keep the picker current as profiles are renamed / deleted / added.
    connect(engine_, &lyra::dsp::WdspEngine::noiseProfilesChanged,
            autoCombo, repopAuto);
    connect(autoCombo, QOverload<int>::of(&QComboBox::activated), autoCombo,
            [autoCombo](int) {
        QSettings().setValue(QStringLiteral("dsp/noiseAutoLoadProfile"),
                             autoCombo->currentData().toString());
    });
    autoForm->addRow(tr("Profile:"), autoCombo);
    auto *autoNote = new QLabel(tr(
        "Loads this profile at launch and re-enables NR-C if it was on last "
        "time. Skipped (NR-C stays off) if the profile no longer matches your "
        "sample rate — recapture it at the new rate."), autoGrp);
    autoNote->setWordWrap(true);
    autoNote->setStyleSheet(QStringLiteral("color:#8a9aac; font-size:11px;"));
    autoForm->addRow(autoNote);
    outer->addWidget(autoGrp);

    auto *path = new QLabel(
        tr("Profiles are stored as .lnp files in:\n%1")
            .arg(engine_->noiseProfilesDir()), page);
    path->setWordWrap(true);
    path->setStyleSheet(QStringLiteral("color:#8a9aac; font-size:11px;"));
    outer->addWidget(path);
    outer->addStretch(1);
    return page;
}

// ── #159 slim DSP — per-mode-family filter type (Linear Phase / Low
// Latency).  OPT-IN: every cell defaults to Linear Phase (the legacy
// behaviour), so an upgrade changes nothing until the operator opts a
// family into Low Latency.  Buffer size stays at the engine default
// (4096) — this surface is filter-TYPE only (group-delay latency lever),
// not buffer/taps.  Writes go straight to WdspEngine, which persists them
// in QSettings (dsp/<fam>_<dir>_mp) and re-applies on mode change / open.
QWidget *SettingsDialog::buildRecordingTab() {
    using lyra::recorder::RecorderConfig;
    QWidget *page = new QWidget;
    QVBoxLayout *outer = new QVBoxLayout(page);

    QLabel *intro = new QLabel(tr(
        "Record the receive audio to a timestamped session folder, with "
        "periodic panadapter/waterfall snapshots.  Click the <b>Recorder</b> "
        "chip in the header to pop out the panel and start/stop; this page sets "
        "where the sessions land and how they are limited, and lists what "
        "you've saved."));
    intro->setWordWrap(true);
    outer->addWidget(intro);

    const RecorderConfig c0 = recorder_->config();

    // Bytes → human-readable MB/GB (used by the sessions list + summary).
    auto human = [](qint64 b) -> QString {
        const double mb = double(b) / (1024.0 * 1024.0);
        return mb >= 1024.0 ? QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 2)
                            : QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
    };

    // ── Folder ──────────────────────────────────────────────────────────
    QGroupBox *folderBox = new QGroupBox(tr("Recordings folder"));
    QGridLayout *fg = new QGridLayout(folderBox);
    QLineEdit *pathEdit = new QLineEdit;
    pathEdit->setReadOnly(true);
    pathEdit->setText(recorder_->recordRoot());
    QPushButton *browseBtn  = new QPushButton(tr("Browse…"));
    QPushButton *defaultBtn = new QPushButton(tr("Default"));
    fg->addWidget(new QLabel(tr("Save to:")), 0, 0);
    fg->addWidget(pathEdit,  0, 1);
    fg->addWidget(browseBtn, 0, 2);
    fg->addWidget(defaultBtn, 0, 3);
    fg->setColumnStretch(1, 1);
    outer->addWidget(folderBox);

    // ── Snapshots ───────────────────────────────────────────────────────
    QGroupBox *snapBox = new QGroupBox(tr("Snapshots"));
    QGridLayout *sg = new QGridLayout(snapBox);
    QCheckBox *snapOn = new QCheckBox(tr("Capture panadapter / waterfall snapshots"));
    snapOn->setChecked(c0.snapshotsOn);
    QSpinBox *snapRate = new QSpinBox;
    snapRate->setRange(1, 30);
    snapRate->setValue(int(c0.snapshotsPerMin + 0.5));
    snapRate->setSuffix(tr(" / min"));
    sg->addWidget(snapOn, 0, 0, 1, 2);
    sg->addWidget(new QLabel(tr("Rate:")), 1, 0);
    sg->addWidget(snapRate, 1, 1);
    sg->setColumnStretch(2, 1);
    outer->addWidget(snapBox);

    // ── Auto-split & hard time limit ────────────────────────────────────
    QGroupBox *limitBox = new QGroupBox(tr("Auto-split & time limit"));
    QGridLayout *lg = new QGridLayout(limitBox);
    QSpinBox *splitMin = new QSpinBox;
    splitMin->setRange(0, 600); splitMin->setValue(c0.splitMinutes);
    splitMin->setSuffix(tr(" min")); splitMin->setSpecialValueText(tr("off"));
    QComboBox *splitMB = new QComboBox;
    splitMB->addItem(tr("off"), 0);
    for (int mb : {5, 10, 15, 25})
        splitMB->addItem(tr("%1 MB").arg(mb), mb);
    {   // select the saved value (keep a legacy custom value if present)
        int idx = splitMB->findData(c0.splitMB);
        if (idx < 0) {
            splitMB->addItem(tr("%1 MB").arg(c0.splitMB), c0.splitMB);
            idx = splitMB->count() - 1;
        }
        splitMB->setCurrentIndex(idx);
    }
    QSpinBox *hardMin = new QSpinBox;
    hardMin->setRange(0, 600); hardMin->setValue(c0.hardLimitMin);
    hardMin->setSuffix(tr(" min")); hardMin->setSpecialValueText(tr("off"));
    lg->addWidget(new QLabel(tr("Split every:")),     0, 0); lg->addWidget(splitMin, 0, 1);
    lg->addWidget(new QLabel(tr("…or every:")),       1, 0); lg->addWidget(splitMB,  1, 1);
    lg->addWidget(new QLabel(tr("Hard stop after:")), 2, 0); lg->addWidget(hardMin,  2, 1);
    lg->setColumnStretch(2, 1);
    QLabel *hardNote = new QLabel(tr("The hard stop is the \"did I leave it running?\" guard."));
    hardNote->setStyleSheet("color: palette(mid);");
    lg->addWidget(hardNote, 3, 0, 1, 3);
    outer->addWidget(limitBox);

    // ── Storage cap ─────────────────────────────────────────────────────
    QGroupBox *storeBox = new QGroupBox(tr("Storage cap"));
    QGridLayout *stg = new QGridLayout(storeBox);
    QSpinBox *capMB = new QSpinBox;
    capMB->setRange(0, 1000000); capMB->setValue(int(c0.capMB));
    capMB->setSuffix(tr(" MB")); capMB->setSpecialValueText(tr("no cap"));
    capMB->setSingleStep(512);
    QCheckBox *autoPrune = new QCheckBox(tr("Auto-delete oldest sessions when over the cap"));
    autoPrune->setChecked(c0.autoPrune);
    stg->addWidget(new QLabel(tr("Max total:")), 0, 0); stg->addWidget(capMB, 0, 1);
    stg->addWidget(autoPrune, 1, 0, 1, 2);
    stg->setColumnStretch(2, 1);
    outer->addWidget(storeBox);

    // ── Recorded sessions ───────────────────────────────────────────────
    QGroupBox *sessBox = new QGroupBox(tr("Recorded sessions"));
    QVBoxLayout *sv = new QVBoxLayout(sessBox);
    QListWidget *list = new QListWidget;
    // Radio-button-style single-select rows (same PickDelegate as the Hardware
    // radio chooser) — pick a session, then Convert / Open / Delete it.
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setItemDelegate(new PickDelegate(list));
    QLabel *summary = new QLabel;
    QHBoxLayout *btnRow = new QHBoxLayout;
    QPushButton *convertBtn = new QPushButton(tr("Convert to MP4"));
    QPushButton *openBtn    = new QPushButton(tr("Open folder"));
    QPushButton *renameBtn  = new QPushButton(tr("Rename"));
    QPushButton *delBtn     = new QPushButton(tr("Delete"));
    QPushButton *refreshBtn = new QPushButton(tr("Refresh"));
    btnRow->addWidget(convertBtn);
    btnRow->addWidget(openBtn);
    btnRow->addWidget(renameBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(refreshBtn);
    // Convert progress (hidden until a conversion runs) + status line.
    QProgressBar *convBar = new QProgressBar;
    convBar->setRange(0, 100);
    convBar->setVisible(false);
    QLabel *convStatus = new QLabel;
    convStatus->setStyleSheet("color: palette(mid);");
    sv->addWidget(list);
    sv->addWidget(summary);
    sv->addLayout(btnRow);
    sv->addWidget(convBar);
    sv->addWidget(convStatus);
    outer->addWidget(sessBox, 1);

    // ── ffmpeg (the Convert dependency) ─────────────────────────────────
    QGroupBox *ffBox = new QGroupBox(tr("Video encoder (ffmpeg)"));
    QGridLayout *ffg = new QGridLayout(ffBox);
    QLabel *ffPath = new QLabel;
    QPushButton *ffBrowse = new QPushButton(tr("Browse…"));
    QLabel *ffHint = new QLabel(tr(
        "Convert-to-MP4 uses ffmpeg.  Put <b>ffmpeg.exe</b> next to Lyra or on "
        "your PATH, or Browse to it.  Free download: "
        "<a href=\"https://ffmpeg.org/download.html\">ffmpeg.org/download</a>."));
    ffHint->setOpenExternalLinks(true);
    ffHint->setWordWrap(true);
    auto refreshFfLabel = [this, ffPath]() {
        const QString p = converter_->ffmpegPath();
        ffPath->setText(p.isEmpty()
            ? tr("<b style='color:#e06666'>not found</b>")
            : tr("<span style='color:#78d28c'>%1</span>").arg(p.toHtmlEscaped()));
    };
    refreshFfLabel();
    ffg->addWidget(new QLabel(tr("ffmpeg:")), 0, 0);
    ffg->addWidget(ffPath, 0, 1);
    ffg->addWidget(ffBrowse, 0, 2);
    ffg->addWidget(ffHint, 1, 0, 1, 3);
    ffg->setColumnStretch(1, 1);
    outer->addWidget(ffBox);
    connect(ffBrowse, &QPushButton::clicked, this,
            [this, refreshFfLabel, convertBtn]() {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("Locate ffmpeg"), QString(),
#ifdef Q_OS_WIN
            tr("ffmpeg (ffmpeg.exe);;All files (*)"));
#else
            tr("ffmpeg (ffmpeg);;All files (*)"));
#endif
        if (f.isEmpty()) return;
        converter_->setFfmpegPath(f);
        refreshFfLabel();
        convertBtn->setEnabled(!converter_->busy() &&
                               converter_->ffmpegAvailable());
    });

    // Repopulate the list + summary from the record root on disk.
    auto refresh = [this, list, summary, human]() {
        list->clear();
        const QString root = recorder_->recordRoot();
        qint64 total = 0;
        int n = 0;
        const auto subs = QDir(root).entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        for (const QFileInfo &sub : subs) {
            qint64 sz = 0;
            QDirIterator it(sub.absoluteFilePath(), QDir::Files,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) { it.next(); sz += it.fileInfo().size(); }
            total += sz;
            ++n;
            auto *item = new QListWidgetItem(
                QStringLiteral("%1   (%2)").arg(sub.fileName(), human(sz)));
            item->setData(Qt::UserRole, sub.absoluteFilePath());
            list->addItem(item);
        }
        const RecorderConfig c = recorder_->config();
        const qint64 capBytes = qint64(c.capMB) * 1024 * 1024;
        const bool over = c.capMB > 0 && total > capBytes;
        QString txt = tr("%n session(s)", "", n) + QStringLiteral(" · ") + human(total);
        if (c.capMB > 0)
            txt += tr(" of %1 cap").arg(human(capBytes));
        summary->setText(txt);
        summary->setStyleSheet(over ? "color: #e06666;" : "color: palette(mid);");
    };
    refresh();

    // Push the numeric/config widgets into the engine (preserving the live
    // snapshotsOn/rate/audioRate the checkbox owns separately).
    auto commit = [this, snapRate, splitMin, splitMB, hardMin, capMB,
                   autoPrune, refresh]() {
        RecorderConfig c = recorder_->config();
        c.snapshotsPerMin = snapRate->value();
        c.splitMinutes    = splitMin->value();
        c.splitMB         = splitMB->currentData().toInt();
        c.hardLimitMin    = hardMin->value();
        c.capMB           = capMB->value();
        c.autoPrune       = autoPrune->isChecked();
        recorder_->setConfig(c);
        refresh();
    };
    for (QSpinBox *sb : {snapRate, splitMin, hardMin, capMB})
        connect(sb, &QSpinBox::valueChanged, this, [commit](int) { commit(); });
    connect(splitMB, &QComboBox::currentIndexChanged, this, [commit](int) { commit(); });
    connect(autoPrune, &QCheckBox::toggled, this, [commit](bool) { commit(); });

    // Snapshot master toggle goes through the live setter (updates the running
    // snapshot timer), and stays in sync if the panel toggles it.
    connect(snapOn, &QCheckBox::toggled, recorder_,
            [this](bool on) { recorder_->setSnapshotsOn(on); });
    connect(recorder_, &lyra::recorder::RecorderEngine::snapshotsOnChanged,
            snapOn, &QCheckBox::setChecked);

    connect(browseBtn, &QPushButton::clicked, this, [this, pathEdit, refresh]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Choose recordings folder"), recorder_->recordRoot());
        if (dir.isEmpty()) return;
        RecorderConfig c = recorder_->config(); c.path = dir; recorder_->setConfig(c);
        pathEdit->setText(recorder_->recordRoot());
        refresh();
    });
    connect(defaultBtn, &QPushButton::clicked, this, [this, pathEdit, refresh]() {
        RecorderConfig c = recorder_->config(); c.path.clear(); recorder_->setConfig(c);
        pathEdit->setText(recorder_->recordRoot());
        refresh();
    });

    connect(refreshBtn, &QPushButton::clicked, this, [refresh]() { refresh(); });
    connect(openBtn, &QPushButton::clicked, this, [this, list]() {
        QListWidgetItem *it = list->currentItem();
        const QString dir = it ? it->data(Qt::UserRole).toString()
                               : recorder_->recordRoot();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(renameBtn, &QPushButton::clicked, this, [this, list, refresh]() {
        QListWidgetItem *it = list->currentItem();
        if (!it) return;
        const QString oldPath = it->data(Qt::UserRole).toString();
        QDir dir(oldPath);
        const QString oldName = dir.dirName();
        bool ok = false;
        const QString newName = QInputDialog::getText(
            this, tr("Rename session"), tr("New folder name:"),
            QLineEdit::Normal, oldName, &ok).trimmed();
        if (!ok || newName.isEmpty() || newName == oldName) return;
        if (newName.contains(QLatin1Char('/')) ||
            newName.contains(QLatin1Char('\\'))) {
            QMessageBox::warning(this, tr("Rename session"),
                                 tr("Use a simple folder name (no slashes)."));
            return;
        }
        QDir parent(oldPath); parent.cdUp();
        const QString newPath = parent.filePath(newName);
        if (QFileInfo::exists(newPath)) {
            QMessageBox::warning(this, tr("Rename session"),
                tr("A session named “%1” already exists.").arg(newName));
            return;
        }
        if (!QDir().rename(oldPath, newPath)) {
            QMessageBox::warning(this, tr("Rename session"),
                tr("Couldn't rename the folder — is it open in another window?"));
            return;
        }
        refresh();
    });
    connect(delBtn, &QPushButton::clicked, this, [this, list, refresh]() {
        QListWidgetItem *it = list->currentItem();
        if (!it) return;
        const QString dir = it->data(Qt::UserRole).toString();
        const auto ret = QMessageBox::question(
            this, tr("Delete session"),
            tr("Permanently delete this session folder?  This cannot be "
               "undone.\n\n%1").arg(QDir(dir).dirName()));
        if (ret != QMessageBox::Yes) return;
        QDir(dir).removeRecursively();
        refresh();
    });

    // ── Convert to MP4 (offline ffmpeg, below-normal priority) ──────────
    using lyra::recorder::SessionConverter;
    convertBtn->setEnabled(converter_->ffmpegAvailable() && !converter_->busy());
    connect(convertBtn, &QPushButton::clicked, this,
            [this, list, convStatus]() {
        QListWidgetItem *it = list->currentItem();
        if (!it) {
            convStatus->setText(tr("Select a session to convert."));
            return;
        }
        convStatus->setText(QString());
        // One at a time; the engine refuses a second start.
        converter_->convert(it->data(Qt::UserRole).toString());
    });
    connect(converter_, &SessionConverter::busyChanged, convertBtn,
            [this, convertBtn, openBtn, renameBtn, delBtn, refreshBtn,
             convBar](bool on) {
        convBar->setVisible(on);
        if (on) convBar->setValue(0);
        convertBtn->setEnabled(!on && converter_->ffmpegAvailable());
        openBtn->setEnabled(!on);
        renameBtn->setEnabled(!on);
        delBtn->setEnabled(!on);
        refreshBtn->setEnabled(!on);
        convertBtn->setText(on ? tr("Converting…") : tr("Convert to MP4"));
    });
    connect(converter_, &SessionConverter::progressChanged, convBar,
            &QProgressBar::setValue);
    connect(converter_, &SessionConverter::finished, this,
            [convStatus, refresh](const QString &, const QString &out) {
        convStatus->setText(tr("Saved %1").arg(QDir::toNativeSeparators(out)));
        refresh();
    });
    connect(converter_, &SessionConverter::error, this,
            [convStatus](const QString &m) {
        convStatus->setText(QStringLiteral("<span style='color:#e06666'>%1</span>")
                                .arg(m.toHtmlEscaped()));
    });
    connect(converter_, &SessionConverter::ffmpegPathChanged, this,
            [this, convertBtn, refreshFfLabel]() {
        refreshFfLabel();
        convertBtn->setEnabled(!converter_->busy() &&
                               converter_->ffmpegAvailable());
    });

    return page;
}

QWidget *SettingsDialog::buildDspTab() {
    using DspFamily = lyra::dsp::WdspEngine::DspFamily;
    QWidget *page = new QWidget;
    QVBoxLayout *outer = new QVBoxLayout(page);

    QLabel *intro = new QLabel(
        tr("Filter type per mode family.  <b>Linear Phase</b> (default) is "
           "the cleanest, symmetric response.  <b>Low Latency</b> uses a "
           "minimum-phase filter that trims group delay — useful for digital "
           "round-trip (VarAC / WSJT-X) and CW monitoring, at the cost of a "
           "little phase asymmetry.  Buffer size is unchanged."));
    intro->setWordWrap(true);
    outer->addWidget(intro);

    QGroupBox *box = new QGroupBox(tr("Mode-family filter type"));
    QGridLayout *grid = new QGridLayout(box);
    grid->addWidget(new QLabel(tr("<b>Family</b>")), 0, 0);
    grid->addWidget(new QLabel(tr("<b>RX</b>")),     0, 1);
    grid->addWidget(new QLabel(tr("<b>TX</b>")),     0, 2);

    struct Row { const char *label; DspFamily fam; bool hasTx; };
    const Row rows[] = {
        { "Phone (SSB / AM / SAM / DSB)", DspFamily::Phone, true  },
        { "FM",                          DspFamily::FM,    true  },
        { "CW",                          DspFamily::CW,    false },  // RX-only
        { "Digital (DIGU / DIGL / DRM)", DspFamily::Dig,   true  },
    };

    auto makeCombo = [this](DspFamily fam, bool tx) {
        QComboBox *c = new QComboBox;
        c->addItem(tr("Linear Phase"));   // index 0 -> mp = false
        c->addItem(tr("Low Latency"));    // index 1 -> mp = true
        c->setCurrentIndex(engine_->dspFilterMinPhase(fam, tx) ? 1 : 0);
        connect(c, &QComboBox::activated, engine_, [this, fam, tx](int idx) {
            engine_->setDspFilterMinPhase(fam, tx, idx == 1);
        });
        return c;
    };

    int r = 1;
    for (const Row &row : rows) {
        grid->addWidget(new QLabel(tr(row.label)), r, 0);
        grid->addWidget(makeCombo(row.fam, /*tx=*/false), r, 1);
        if (row.hasTx) {
            grid->addWidget(makeCombo(row.fam, /*tx=*/true), r, 2);
        } else {
            QLabel *na = new QLabel(tr("—"));
            na->setToolTip(tr("CW transmit is handled by the keyer, not a "
                              "TXA filter, so there is no CW-TX filter type."));
            grid->addWidget(na, r, 2);
        }
        ++r;
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    outer->addWidget(box);

    // Why Lyra's DSP page is so much smaller than Thetis's "DSP → Options"
    // (heads off the "where are my buffer / filter-size / latency knobs?"
    // questions).
    QLabel *why = new QLabel(
        tr("<b>Why only this one control?</b><br>"
           "Thetis exposes buffer size, filter (tap) size and FFT options "
           "because it was built for the PCs of its era, where those were "
           "real CPU-vs-latency trade-offs you had to tune by hand.<br><br>"
           "Lyra targets <b>modern hardware</b> — multi-core CPUs, plenty of "
           "RAM and a Vulkan GPU pipeline — and is a native <b>Qt 6 / C++23</b> "
           "build. It simply runs the DSP at a high-quality fixed buffer all "
           "the time, so there is nothing to trade off: you get the low "
           "latency and the sharp filters at once. The only knob that still "
           "changes the <i>sound/feel</i> on fast machines is filter "
           "<i>type</i> (group delay), which is what this page exposes. "
           "Everything else Thetis made you tune is handled automatically."
           "<br><br>The one latency that <i>is</i> yours to tune is the "
           "digital-mode audio bridge to apps like VarAC / WSJT-X — that "
           "lives on the <b>Audio → VAC</b> page (buffer size + ring "
           "latency), not here."));
    why->setWordWrap(true);
    why->setStyleSheet("color: palette(mid);");
    outer->addWidget(why);

    outer->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildWeatherTab() {
    // Two-column layout (task #23, continuation of the Hardware/Visuals/
    // Network refactor): the disclaimer + master-enable checkboxes stay
    // full-width at the top (they gate every other widget below), then a
    // horizontal split puts Sources + Notifications on the left and
    // Thresholds + API credentials on the right.  Same 28 px gutter +
    // 10 px form row spacing as the other refactored tabs.
    auto *page = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    auto apply = [this]() { if (wx_) wx_->reloadConfig(); };

    // Disclaimer + master enable (enable gated on accepting the disclaimer).
    auto *disc = new QCheckBox(
        tr("I understand these alerts are advisory only and not a "
           "substitute for official warnings."), page);
    disc->setChecked(QSettings().value(QStringLiteral("wx/disclaimer_accepted"),
                                       false).toBool());
    outer->addWidget(disc);

    auto *enable = new QCheckBox(tr("Enable weather alerts"), page);
    enable->setChecked(QSettings().value(QStringLiteral("wx/enabled"), false).toBool());
    enable->setEnabled(disc->isChecked());
    outer->addWidget(enable);

    // Two-column container below the gating checkboxes.
    auto *cols   = new QWidget(page);
    auto *colsHb = new QHBoxLayout(cols);
    colsHb->setContentsMargins(0, 0, 0, 0);
    colsHb->setSpacing(28);
    auto *leftCol  = new QWidget(cols);
    auto *leftVb   = new QVBoxLayout(leftCol);
    leftVb->setContentsMargins(0, 0, 0, 0);
    auto *rightCol = new QWidget(cols);
    auto *rightVb  = new QVBoxLayout(rightCol);
    rightVb->setContentsMargins(0, 0, 0, 0);
    colsHb->addWidget(leftCol,  1);
    colsHb->addWidget(rightCol, 1);
    outer->addWidget(cols);
    QBoxLayout *col = leftVb;   // pointer the four blocks below append to

    connect(disc, &QCheckBox::toggled, page, [enable, apply](bool on) {
        QSettings().setValue(QStringLiteral("wx/disclaimer_accepted"), on);
        enable->setEnabled(on);
        if (!on) enable->setChecked(false);
        apply();
    });
    connect(enable, &QCheckBox::toggled, page, [apply](bool on) {
        QSettings().setValue(QStringLiteral("wx/enabled"), on);
        apply();
    });

    // --- Sources ---
    {
        auto *grp = new QGroupBox(tr("Sources"), page);
        auto *v = new QVBoxLayout(grp);
        auto mkSrc = [v, page, apply](const QString &label,
                                      const QString &key, bool def) {
            auto *cb = new QCheckBox(label, page);
            cb->setChecked(QSettings().value(key, def).toBool());
            QObject::connect(cb, &QCheckBox::toggled, page,
                             [key, apply](bool on) {
                QSettings().setValue(key, on);
                apply();
            });
            v->addWidget(cb);
        };
        mkSrc(tr("Blitzortung — global lightning (free, no key)"),
              QStringLiteral("wx/src_blitzortung"), true);
        mkSrc(tr("NWS / weather.gov — wind + severe alerts (free)"),
              QStringLiteral("wx/src_nws"), true);
        mkSrc(tr("NWS METAR — live wind from a station (free)"),
              QStringLiteral("wx/src_nws_metar"), true);
        mkSrc(tr("Ambient Weather — your station (needs keys below)"),
              QStringLiteral("wx/src_ambient"), false);
        mkSrc(tr("Ecowitt — your station (needs keys below)"),
              QStringLiteral("wx/src_ecowitt"), false);
        col->addWidget(grp);
    }

    // --- Thresholds + station/units ---
    col = rightVb;   // switch to RIGHT column
    {
        auto *grp = new QGroupBox(tr("Thresholds"), page);
        auto *f = new QFormLayout(grp);
        f->setVerticalSpacing(10);

        // Distance unit drives the lightning-range display; range is
        // always STORED in km (wx/lightning_range_km), shown in mi/km.
        auto *unit = new QComboBox(grp);
        unit->addItem(tr("Miles"), QStringLiteral("mi"));
        unit->addItem(tr("Kilometres"), QStringLiteral("km"));
        unit->setCurrentIndex(std::max(0, unit->findData(QSettings()
            .value(QStringLiteral("wx/distance_unit"),
                   QStringLiteral("mi")).toString())));
        auto isKm = [unit]() {
            return unit->currentData().toString() == QStringLiteral("km");
        };

        auto *range = new QSpinBox(grp);
        range->setRange(3, 600);
        auto syncRange = [range, isKm]() {
            const double km = QSettings()
                .value(QStringLiteral("wx/lightning_range_km"), 80.0).toDouble();
            range->blockSignals(true);
            range->setSuffix(isKm() ? tr(" km") : tr(" mi"));
            range->setValue(qRound(isKm() ? km : km / 1.60934));
            range->blockSignals(false);
        };
        syncRange();
        connect(range, &QSpinBox::valueChanged, grp, [isKm, apply](int v) {
            const double km = isKm() ? double(v) : v * 1.60934;
            QSettings().setValue(QStringLiteral("wx/lightning_range_km"), km);
            apply();
        });
        f->addRow(tr("Lightning range"), range);

        auto *ws = new QSpinBox(grp);
        ws->setRange(5, 100);
        ws->setSuffix(tr(" mph"));
        ws->setValue(qRound(QSettings()
            .value(QStringLiteral("wx/wind_sustained_mph"), 30.0).toDouble()));
        connect(ws, &QSpinBox::valueChanged, grp, [apply](int v) {
            QSettings().setValue(QStringLiteral("wx/wind_sustained_mph"),
                                 double(v));
            apply();
        });
        f->addRow(tr("Wind sustained ≥"), ws);

        auto *wg = new QSpinBox(grp);
        wg->setRange(10, 150);
        wg->setSuffix(tr(" mph"));
        wg->setValue(qRound(QSettings()
            .value(QStringLiteral("wx/wind_gust_mph"), 40.0).toDouble()));
        connect(wg, &QSpinBox::valueChanged, grp, [apply](int v) {
            QSettings().setValue(QStringLiteral("wx/wind_gust_mph"), double(v));
            apply();
        });
        f->addRow(tr("Wind gust ≥"), wg);

        auto *metar = new QLineEdit(
            QSettings().value(QStringLiteral("wx/nws_metar_station")).toString(), grp);
        metar->setPlaceholderText(tr("ICAO, e.g. KTOL"));
        connect(metar, &QLineEdit::editingFinished, grp, [metar, apply]() {
            QSettings().setValue(QStringLiteral("wx/nws_metar_station"),
                                 metar->text().trimmed().toUpper());
            apply();
        });
        f->addRow(tr("METAR station"), metar);

        connect(unit, &QComboBox::currentIndexChanged, grp,
                [unit, apply, syncRange](int) {
            QSettings().setValue(QStringLiteral("wx/distance_unit"),
                                 unit->currentData().toString());
            syncRange();   // re-display the range in the newly-chosen unit
            apply();
        });
        f->addRow(tr("Distance unit"), unit);
        col->addWidget(grp);
    }

    // --- Notifications + test ---
    col = leftVb;   // back to LEFT column
    {
        auto *grp = new QGroupBox(tr("Notifications"), page);
        auto *v = new QVBoxLayout(grp);
        auto *toastCb = new QCheckBox(tr("Desktop notification on alert"), page);
        toastCb->setChecked(QSettings()
            .value(QStringLiteral("wx/desktop_enabled"), true).toBool());
        connect(toastCb, &QCheckBox::toggled, page, [apply](bool on) {
            QSettings().setValue(QStringLiteral("wx/desktop_enabled"), on);
            apply();
        });
        v->addWidget(toastCb);
        auto *audioCb = new QCheckBox(tr("Audible chime on alert"), page);
        audioCb->setChecked(QSettings()
            .value(QStringLiteral("wx/audio_enabled"), true).toBool());
        connect(audioCb, &QCheckBox::toggled, page, [apply](bool on) {
            QSettings().setValue(QStringLiteral("wx/audio_enabled"), on);
            apply();
        });
        v->addWidget(audioCb);
        auto *nwsWindCb = new QCheckBox(
            tr("Pop NWS High/Extreme Wind Warnings"), page);
        nwsWindCb->setChecked(QSettings()
            .value(QStringLiteral("wx/nws_wind_toast"), false).toBool());
        nwsWindCb->setToolTip(tr(
            "When on, an active NWS High/Extreme Wind Warning for your "
            "location pops a wind toast even if your local readings are "
            "below the sustained/gust thresholds above.  When off (default), "
            "the wind toast follows your thresholds only — the header badge "
            "still reflects the NWS warning either way."));
        connect(nwsWindCb, &QCheckBox::toggled, page, [apply](bool on) {
            QSettings().setValue(QStringLiteral("wx/nws_wind_toast"), on);
            apply();
        });
        v->addWidget(nwsWindCb);
        auto *test = new QPushButton(tr("Send test alert"), page);
        test->setToolTip(tr("Light the header badges + fire one toast for "
                            "a few seconds."));
        connect(test, &QPushButton::clicked, page,
                [this]() { if (wx_) wx_->fireTestSnapshot(); });
        v->addWidget(test);
        col->addWidget(grp);
        leftVb->addStretch(1);   // push left column groups to the top
    }

    // --- API credentials (Ambient / Ecowitt) ---
    col = rightVb;   // back to RIGHT column
    {
        auto *grp = new QGroupBox(tr("Station API credentials"), page);
        auto *f = new QFormLayout(grp);
        f->setVerticalSpacing(10);
        auto mkKey = [f, grp, apply](const QString &label, const QString &key) {
            auto *e = new QLineEdit(QSettings().value(key).toString(), grp);
            e->setEchoMode(QLineEdit::Password);
            QObject::connect(e, &QLineEdit::editingFinished, grp,
                             [e, key, apply]() {
                QSettings().setValue(key, e->text().trimmed());
                apply();
            });
            f->addRow(label, e);
        };
        mkKey(tr("Ambient API key"), QStringLiteral("wx/ambient_api_key"));
        mkKey(tr("Ambient Application key"), QStringLiteral("wx/ambient_app_key"));
        mkKey(tr("Ecowitt Application key"), QStringLiteral("wx/ecowitt_app_key"));
        mkKey(tr("Ecowitt API key"), QStringLiteral("wx/ecowitt_api_key"));
        auto *mac = new QLineEdit(
            QSettings().value(QStringLiteral("wx/ecowitt_mac")).toString(), grp);
        mac->setPlaceholderText(tr("e.g. 34:94:54:AB:CD:EF"));
        connect(mac, &QLineEdit::editingFinished, grp, [mac, apply]() {
            QSettings().setValue(QStringLiteral("wx/ecowitt_mac"),
                                 mac->text().trimmed().toUpper());
            apply();
        });
        f->addRow(tr("Ecowitt gateway MAC"), mac);
        col->addWidget(grp);
        rightVb->addStretch(1);   // push right column groups to the top
    }

    outer->addStretch(1);
    return page;
}

// #49 — TX/RX Profile editor.  Whole-set capture/apply via
// ProfileManager (decoupled from Prefs/stream/engine through
// ProfileBindings).  Manual select only — no auto-detect by call;
// the only automatic recall is the per-mode binding table below
// (operator-opt-in, mirrors the Thetis "digital modes auto-switch
// source" idiom).  PA-enable / HW-PTT / space-bar PTT are GLOBAL
// (safety / input-method) and deliberately NOT profile fields.
QWidget *SettingsDialog::buildProfilesTab() {
    auto *page = new QWidget(this);
    auto *outer = new QVBoxLayout(page);

    auto *intro = new QLabel(
        tr("A profile bundles the operator TX/RX chain (mode, bandwidth + "
           "lock, mic source/gain, drive, TCI gains, AGC, auto-mute, TX "
           "timeout) and is recalled as a unit.  Switching is manual; the "
           "per-mode table at the bottom optionally auto-recalls a profile "
           "when you change mode."), page);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    // --- profile list + action buttons ---
    auto *row = new QHBoxLayout();
    auto *list = new QListWidget(page);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setItemDelegate(new ProfilePickDelegate(list));  // radio-mark + ACTIVE pill
    row->addWidget(list, 1);

    auto *btnCol = new QVBoxLayout();
    auto *saveBtn    = new QPushButton(tr("Save"), page);
    auto *saveAsBtn  = new QPushButton(tr("Save As…"), page);
    auto *loadBtn    = new QPushButton(tr("Load"), page);
    auto *renameBtn  = new QPushButton(tr("Rename…"), page);
    auto *deleteBtn  = new QPushButton(tr("Delete"), page);
    auto *defaultBtn = new QPushButton(tr("Set Default"), page);
    saveBtn->setToolTip(tr("Overwrite the active profile with the current "
                           "live settings."));
    saveAsBtn->setToolTip(tr("Capture the current live settings into a new "
                             "named profile."));
    defaultBtn->setToolTip(tr("Apply this profile automatically at startup."));
    for (auto *b : {saveBtn, saveAsBtn, loadBtn, renameBtn, deleteBtn, defaultBtn})
        btnCol->addWidget(b);
    btnCol->addStretch(1);
    row->addLayout(btnCol);
    outer->addLayout(row);

    auto *statusLbl = new QLabel(page);
    outer->addWidget(statusLbl);

    // Name of the currently-selected list profile ("" if none).
    auto selectedName = [list]() -> QString {
        auto *item = list->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    };

    // --- selected-profile summary + quick edit ---
    // Radio-select a profile above; this shows its SAVED core-chain values
    // and lets you edit + re-save WITHOUT going on air.  "Apply changes"
    // writes the profile record only (a pure store mutation, never a live
    // retune / mid-TX concern) — hit Load above to make it live.
    auto *editGrp = new QGroupBox(tr("Selected profile — view & quick edit"), page);
    auto *editV   = new QVBoxLayout(editGrp);
    auto *editHdr = new QLabel(editGrp);
    editHdr->setWordWrap(true);
    editHdr->setStyleSheet(QStringLiteral("QLabel{color:#8fa6ba;}"));
    editV->addWidget(editHdr);

    auto *editGrid = new QGridLayout();
    editGrid->setHorizontalSpacing(14);
    editV->addLayout(editGrid);

    auto *rxBwSpin = new QSpinBox(editGrp);
    rxBwSpin->setRange(0, 20000); rxBwSpin->setSingleStep(50);
    rxBwSpin->setSuffix(tr(" Hz"));
    auto *txBwSpin = new QSpinBox(editGrp);
    txBwSpin->setRange(0, 20000); txBwSpin->setSingleStep(50);
    txBwSpin->setSuffix(tr(" Hz"));
    auto *loSpin = new QSpinBox(editGrp);
    loSpin->setRange(0, 5000); loSpin->setSingleStep(10);
    loSpin->setSuffix(tr(" Hz"));
    // TX filter low mirrors the SAME shared low edge (Profile.filterLow —
    // RX+TX share one low edge today, Task #53).  Shown for RX/TX symmetry;
    // editing either box moves both.
    auto *txLoSpin = new QSpinBox(editGrp);
    txLoSpin->setRange(0, 5000); txLoSpin->setSingleStep(10);
    txLoSpin->setSuffix(tr(" Hz"));
    auto *lockChk = new QCheckBox(tr("Lock TX/RX BW"), editGrp);

    auto *micCombo = new QComboBox(editGrp);
    {
        const QStringList toks = lyra::ui::Prefs::micSourceTokens();
        for (int i = 0; i < toks.size(); ++i) {
            micCombo->addItem(lyra::ui::Prefs::micSourceLabel(toks.at(i)),
                              toks.at(i));
            if (!lyra::ui::Prefs::micSourceEnabled(toks.at(i))) {
                if (auto *m = qobject_cast<QStandardItemModel *>(micCombo->model()))
                    if (auto *it = m->item(i))
                        it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
            }
        }
    }
    auto *micGainSpin = new QDoubleSpinBox(editGrp);
    micGainSpin->setRange(-90.0, 40.0); micGainSpin->setSingleStep(0.5);
    micGainSpin->setDecimals(1); micGainSpin->setSuffix(tr(" dB"));
    auto *micBoostChk = new QCheckBox(tr("20 dB mic boost"), editGrp);

    auto *agcCombo = new QComboBox(editGrp);
    agcCombo->addItem(tr("Off"),    QStringLiteral("off"));
    agcCombo->addItem(tr("Fast"),   QStringLiteral("fast"));
    agcCombo->addItem(tr("Medium"), QStringLiteral("med"));
    agcCombo->addItem(tr("Slow"),   QStringLiteral("slow"));

    auto *levelerChk  = new QCheckBox(tr("Leveler on"), editGrp);
    auto *phrotChk    = new QCheckBox(tr("PHROT on"), editGrp);
    auto *autoMuteChk = new QCheckBox(tr("Auto-mute on TX"), editGrp);

    // Grid: RX (cols 0/1) | TX (cols 2/3) columns, Lock beside the
    // bandwidth row (col 4).
    editGrid->addWidget(new QLabel(tr("RX bandwidth"), editGrp),  0, 0);
    editGrid->addWidget(rxBwSpin,                                 0, 1);
    editGrid->addWidget(new QLabel(tr("TX bandwidth"), editGrp),  0, 2);
    editGrid->addWidget(txBwSpin,                                 0, 3);
    editGrid->addWidget(lockChk,                                  0, 4);
    editGrid->addWidget(new QLabel(tr("RX filter low"), editGrp), 1, 0);
    editGrid->addWidget(loSpin,                                   1, 1);
    editGrid->addWidget(new QLabel(tr("TX filter low"), editGrp), 1, 2);
    editGrid->addWidget(txLoSpin,                                 1, 3);
    editGrid->addWidget(new QLabel(tr("Mic source"), editGrp),    2, 0);
    editGrid->addWidget(micCombo,                                 2, 1);
    editGrid->addWidget(new QLabel(tr("Mic gain"), editGrp),      2, 2);
    editGrid->addWidget(micGainSpin,                             2, 3);
    editGrid->addWidget(new QLabel(tr("AGC"), editGrp),           3, 0);
    editGrid->addWidget(agcCombo,                                 3, 1);
    editGrid->addWidget(micBoostChk,                             3, 3);
    editGrid->addWidget(levelerChk,                             4, 1);
    editGrid->addWidget(phrotChk,                               4, 3);
    editGrid->addWidget(autoMuteChk,                           5, 1);
    editGrid->setColumnStretch(5, 1);

    auto *editNotice = new QLabel(
        tr("Edits the SAVED profile only — click Load above to apply it live."),
        editGrp);
    editNotice->setWordWrap(true);
    {
        QFont ef = editNotice->font();
        ef.setPointSizeF(ef.pointSizeF() * 0.92);
        editNotice->setFont(ef);
        editNotice->setStyleSheet(QStringLiteral("QLabel{color:#7c8b97;}"));
    }
    auto *editDirtyLbl = new QLabel(tr("● You have unsaved changes"), editGrp);
    editDirtyLbl->setStyleSheet(
        QStringLiteral("QLabel{color:#f2b134; font-weight:bold;}"));
    editDirtyLbl->setVisible(false);

    auto *editBtnRow    = new QHBoxLayout();
    auto *revertEditBtn = new QPushButton(tr("Revert"), editGrp);
    auto *applyEditBtn  = new QPushButton(tr("Apply changes"), editGrp);
    revertEditBtn->setToolTip(tr("Discard edits and reload this profile's "
                                 "saved values."));
    applyEditBtn->setToolTip(tr("Write these values back into the profile "
                                "(does not change what's live)."));
    editBtnRow->addWidget(editNotice, 1);
    editBtnRow->addWidget(editDirtyLbl);
    editBtnRow->addWidget(revertEditBtn);
    editBtnRow->addWidget(applyEditBtn);
    editV->addLayout(editBtnRow);
    outer->addWidget(editGrp);

    // Dirty-tracking: the quick-edit widgets differ from the profile's saved
    // record.  Shows the amber notice + enables Apply/Revert only when there's
    // something to save.  syncEdit() re-baselines (clean); the widget-change
    // connects below re-evaluate live.
    auto editDirty = [this, selectedName, rxBwSpin, txBwSpin, loSpin, lockChk,
                      micCombo, micGainSpin, micBoostChk, agcCombo, levelerChk,
                      phrotChk, autoMuteChk]() -> bool {
        const QString name = selectedName();
        if (name.isEmpty()) return false;
        const lyra::profile::Profile p = profiles_->storedProfile(name);
        if (p.name.isEmpty()) return false;
        return rxBwSpin->value()   != p.rxBandwidth
            || txBwSpin->value()   != p.txBandwidth
            || loSpin->value()     != p.filterLow
            || lockChk->isChecked() != p.bwLocked
            || micCombo->currentData().toString() != p.micSource
            || qAbs(micGainSpin->value() - p.micGainDb) > 0.05
            || micBoostChk->isChecked() != p.micBoost
            || agcCombo->currentData().toString() != p.agcMode
            || levelerChk->isChecked()  != p.levelerOn
            || phrotChk->isChecked()    != p.phrotEnabled
            || autoMuteChk->isChecked() != p.autoMuteOnTx;
    };
    auto updateEditDirty = [editDirty, editDirtyLbl, applyEditBtn,
                            revertEditBtn]() {
        const bool d = editDirty();
        editDirtyLbl->setVisible(d);
        applyEditBtn->setEnabled(d);
        revertEditBtn->setEnabled(d);
    };
    // RX/TX filter-low mirror the single shared low edge — editing either
    // moves both so they always match (guarded so it can't loop).
    connect(loSpin, QOverload<int>::of(&QSpinBox::valueChanged), editGrp,
            [txLoSpin](int v) { if (txLoSpin->value() != v) txLoSpin->setValue(v); });
    connect(txLoSpin, QOverload<int>::of(&QSpinBox::valueChanged), editGrp,
            [loSpin](int v) { if (loSpin->value() != v) loSpin->setValue(v); });
    // Re-evaluate dirty on any edit.
    for (QSpinBox *s : {rxBwSpin, txBwSpin, loSpin, txLoSpin})
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), editGrp,
                [updateEditDirty](int) { updateEditDirty(); });
    connect(micGainSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            editGrp, [updateEditDirty](double) { updateEditDirty(); });
    for (QCheckBox *c : {lockChk, micBoostChk, levelerChk, phrotChk, autoMuteChk})
        connect(c, &QCheckBox::toggled, editGrp,
                [updateEditDirty](bool) { updateEditDirty(); });
    for (QComboBox *cb : {micCombo, agcCombo})
        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), editGrp,
                [updateEditDirty](int) { updateEditDirty(); });

    // --- companion-app launch (per-machine, NOT part of the profile) ---
    // SECURITY: this binding lives in QSettings under
    // profileLaunch/<profile>/* and is filtered out of every exported
    // .lyra — a shared profile carries the DSP chain only, never a
    // "run this exe".  It fires ONLY when the operator explicitly picks
    // the profile (Load / the panel combo), never on auto-recall.
    auto *compGrp = new QGroupBox(
        tr("Companion app — launches when you select this profile"), page);
    auto *compForm = new QFormLayout(compGrp);
    auto *compEnable = new QCheckBox(
        tr("Launch a program when this profile is selected"), compGrp);
    compForm->addRow(compEnable);

    // Field widths capped so Name/Program/Arguments don't span the whole
    // (now half-width) companion column.
    constexpr int kCompFieldW = 340;
    auto *compName = new QLineEdit(compGrp);
    compName->setPlaceholderText(tr("e.g. VarAC"));
    compName->setMaximumWidth(kCompFieldW);
    compForm->addRow(tr("Program name"), compName);

    // The path field stays wider than Name/Arguments so a full
    // C:\...\program.exe path is readable.
    auto *cmdRow  = new QHBoxLayout();
    auto *compCmd = new QLineEdit(compGrp);
    compCmd->setPlaceholderText(tr("Full path to the .exe / .bat / .cmd"));
    compCmd->setMinimumWidth(560);
    auto *browseBtn = new QPushButton(tr("Browse…"), compGrp);
    cmdRow->addWidget(compCmd, 1);
    cmdRow->addWidget(browseBtn);
    compForm->addRow(tr("Program"), cmdRow);

    auto *compArgs = new QLineEdit(compGrp);
    compArgs->setPlaceholderText(tr("Optional command-line arguments"));
    compArgs->setMaximumWidth(kCompFieldW);
    compForm->addRow(tr("Arguments"), compArgs);

    auto *delayRow  = new QHBoxLayout();
    auto *compDelay = new QDoubleSpinBox(compGrp);
    compDelay->setRange(0.0, 60.0);
    compDelay->setSingleStep(0.5);
    compDelay->setDecimals(1);
    compDelay->setSuffix(tr(" s"));
    compDelay->setToolTip(tr("Wait this long after the profile applies "
                             "before launching, so the radio settles first."));
    auto *testBtn = new QPushButton(tr("Test launch"), compGrp);
    delayRow->addWidget(compDelay);
    delayRow->addStretch(1);
    delayRow->addWidget(testBtn);
    compForm->addRow(tr("Start delay"), delayRow);

    auto *compHint = new QLabel(
        tr("This binding stays on this PC only — it is never written into an "
           "exported profile.  Close the program yourself when you switch away."),
        compGrp);
    compHint->setWordWrap(true);
    {
        QFont hf = compHint->font();
        hf.setPointSizeF(hf.pointSizeF() * 0.92);
        compHint->setFont(hf);
    }
    compForm->addRow(compHint);

    // --- per-family auto-recall bindings ---
    // Sidebands collapse (USB/LSB -> SSB, CWU/CWL -> CW, DIGU/DIGL ->
    // Digital); AM/SAM/DSB/FM stand alone.  The recalled profile carries
    // no mode field, so an auto-recall changes the chain but leaves the
    // operator on the sideband they switched to (Thetis-faithful).
    // Two compact columns with width-capped combos (no full-width boxes).
    const QStringList kFamilies = lyra::profile::ProfileManager::modeFamilies();
    auto *bindGrp = new QGroupBox(tr("Auto-recall by mode family"), page);
    auto *bindGrid = new QGridLayout(bindGrp);
    auto *modeCombos = new QHash<QString, QComboBox *>();
    int bindRow = 0, bindCol = 0;
    for (const QString &m : kFamilies) {
        auto *lbl = new QLabel(m, bindGrp);
        auto *cb = new QComboBox(bindGrp);
        cb->setMaximumWidth(180);
        cb->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        modeCombos->insert(m, cb);
        bindGrid->addWidget(lbl, bindRow, bindCol * 2);
        bindGrid->addWidget(cb,  bindRow, bindCol * 2 + 1);
        if (++bindCol == 2) { bindCol = 0; ++bindRow; }
    }
    bindGrid->setColumnStretch(4, 1);   // push the 2 columns to the left

    // Companion (left) + Auto-recall (right) share one row so the lower half
    // of the tab isn't two tall stacked bands.  Both top-aligned.
    auto *bottomRow = new QHBoxLayout();
    bottomRow->addWidget(compGrp, 1);
    bottomRow->addWidget(bindGrp, 0, Qt::AlignTop);
    outer->addLayout(bottomRow);
    outer->addStretch(1);

    // Push the selected profile's companion fields into the editor and
    // gate the sub-controls on selection + the enable tick.
    auto syncCompanion = [this, selectedName, compGrp, compEnable, compName,
                          compCmd, browseBtn, compArgs, compDelay, testBtn]() {
        const QString name = selectedName();
        const bool haveSel = !name.isEmpty();
        compGrp->setEnabled(haveSel);
        const lyra::profile::CompanionLauncher::Config c =
            haveSel ? companion_->config(name)
                    : lyra::profile::CompanionLauncher::Config{};
        const QSignalBlocker b1(compEnable), b2(compName), b3(compCmd),
            b4(compArgs), b5(compDelay);
        compEnable->setChecked(c.enabled);
        compName->setText(c.name);
        compCmd->setText(c.command);
        compArgs->setText(c.args);
        compDelay->setValue(c.delayMs / 1000.0);
        const bool on = haveSel && c.enabled;
        for (QWidget *w : {static_cast<QWidget *>(compName),
                           static_cast<QWidget *>(compCmd),
                           static_cast<QWidget *>(browseBtn),
                           static_cast<QWidget *>(compArgs),
                           static_cast<QWidget *>(compDelay),
                           static_cast<QWidget *>(testBtn)})
            w->setEnabled(on);
    };

    // Persist the editor fields back onto the selected profile's binding.
    auto saveCompanion = [this, selectedName, compEnable, compName, compCmd,
                          compArgs, compDelay]() {
        const QString name = selectedName();
        if (name.isEmpty()) return;
        lyra::profile::CompanionLauncher::Config c;
        c.enabled = compEnable->isChecked();
        c.name    = compName->text().trimmed();
        c.command = compCmd->text().trimmed();
        c.args    = compArgs->text().trimmed();
        c.delayMs = static_cast<int>(qRound(compDelay->value() * 1000.0));
        companion_->setConfig(name, c);
    };

    // Pull the SELECTED profile's saved core-chain values into the quick-edit
    // widgets; disable the group when nothing is selected.
    auto syncEdit = [this, selectedName, editGrp, editHdr, rxBwSpin, txBwSpin,
                     loSpin, txLoSpin, lockChk, micCombo, micGainSpin,
                     micBoostChk, agcCombo, levelerChk, phrotChk, autoMuteChk,
                     updateEditDirty]() {
        const QString name = selectedName();
        const bool have = !name.isEmpty();
        editGrp->setEnabled(have);
        if (!have) {
            editHdr->setText(tr("No profile selected."));
            updateEditDirty();
            return;
        }
        const lyra::profile::Profile p = profiles_->storedProfile(name);
        const bool active = (name == profiles_->activeName());
        editHdr->setText(active
            ? tr("Editing “%1” — currently active; after Apply, Load it again "
                 "to hear the change.").arg(name)
            : tr("Editing “%1”").arg(name));
        const QSignalBlocker b1(rxBwSpin), b2(txBwSpin), b3(loSpin), b4(lockChk),
            b5(micCombo), b6(micGainSpin), b7(micBoostChk), b8(agcCombo),
            b9(levelerChk), b10(phrotChk), b11(autoMuteChk), b12(txLoSpin);
        rxBwSpin->setValue(p.rxBandwidth);
        txBwSpin->setValue(p.txBandwidth);
        loSpin->setValue(p.filterLow);
        txLoSpin->setValue(p.filterLow);   // shared low edge — mirror
        lockChk->setChecked(p.bwLocked);
        { int i = micCombo->findData(p.micSource);
          micCombo->setCurrentIndex(i < 0 ? 0 : i); }
        micGainSpin->setValue(p.micGainDb);
        micBoostChk->setChecked(p.micBoost);
        { int i = agcCombo->findData(p.agcMode);
          agcCombo->setCurrentIndex(i < 0 ? 2 : i); }  // 2 == "med" default
        levelerChk->setChecked(p.levelerOn);
        phrotChk->setChecked(p.phrotEnabled);
        autoMuteChk->setChecked(p.autoMuteOnTx);
        updateEditDirty();   // freshly synced == clean
    };

    // Write the quick-edit widgets back into the selected profile's stored
    // record (never touches live state).  updateStoredProfile re-evaluates
    // the "● modified" flag when the active profile's baseline moves.
    auto applyEdit = [this, selectedName, rxBwSpin, txBwSpin, loSpin, lockChk,
                      micCombo, micGainSpin, micBoostChk, agcCombo, levelerChk,
                      phrotChk, autoMuteChk, updateEditDirty]() {
        const QString name = selectedName();
        if (name.isEmpty()) return;
        lyra::profile::Profile p = profiles_->storedProfile(name);
        if (p.name.isEmpty()) return;   // profile vanished under us
        p.rxBandwidth  = rxBwSpin->value();
        p.txBandwidth  = txBwSpin->value();
        p.filterLow    = loSpin->value();
        p.bwLocked     = lockChk->isChecked();
        p.micSource    = micCombo->currentData().toString();
        p.micGainDb    = micGainSpin->value();
        p.micBoost     = micBoostChk->isChecked();
        p.agcMode      = agcCombo->currentData().toString();
        p.levelerOn    = levelerChk->isChecked();
        p.phrotEnabled = phrotChk->isChecked();
        p.autoMuteOnTx = autoMuteChk->isChecked();
        profiles_->updateStoredProfile(p);
        updateEditDirty();   // saved == clean again
    };

    // Repopulate the list + per-mode combos + status from the store.
    // Captured raw pointers outlive the closures (children of the dialog).
    auto refresh = [this, list, statusLbl, modeCombos, syncCompanion, syncEdit]() {
        const QStringList names = profiles_->names();
        const QString active = profiles_->activeName();
        const QString def    = profiles_->defaultName();

        list->blockSignals(true);
        list->clear();
        for (const QString &n : names) {
            QString label = n;
            if (n == def)    label += tr("  [default]");
            auto *item = new QListWidgetItem(label, list);
            item->setData(Qt::UserRole, n);            // the raw name
            item->setData(Qt::UserRole + 1, n == active);  // drives ACTIVE pill
            if (n == active) {
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
                list->setCurrentItem(item);
            }
        }
        list->blockSignals(false);

        if (names.isEmpty())
            statusLbl->setText(tr("No profiles yet — set up your chain, then "
                                  "“Save As…”."));
        else
            statusLbl->setText(active.isEmpty()
                ? tr("%n profile(s).", "", names.size())
                : tr("Active: %1%2").arg(active,
                      profiles_->isModified() ? tr("  ● modified") : QString()));

        // Per-mode combos: "(none)" + every profile name.
        for (auto it = modeCombos->cbegin(); it != modeCombos->cend(); ++it) {
            QComboBox *cb = it.value();
            cb->blockSignals(true);
            cb->clear();
            cb->addItem(tr("(none)"));
            cb->addItems(names);
            const QString bound = profiles_->modeBinding(it.key());
            const int idx = bound.isEmpty() ? 0 : cb->findText(bound);
            cb->setCurrentIndex(idx < 0 ? 0 : idx);
            cb->blockSignals(false);
        }

        syncCompanion();   // selection may have moved with the list rebuild
        syncEdit();        // re-sync the quick-edit panel to the selection
    };

    refresh();

    // Keep the editor live as profiles change (here or elsewhere).
    connect(profiles_, &lyra::profile::ProfileManager::namesChanged,
            this, [refresh]() { refresh(); });
    connect(profiles_, &lyra::profile::ProfileManager::activeChanged,
            this, [refresh](const QString &) { refresh(); });
    connect(profiles_, &lyra::profile::ProfileManager::modifiedChanged,
            this, [refresh](bool) { refresh(); });

    connect(saveBtn, &QPushButton::clicked, this, [this, refresh]() {
        if (profiles_->activeName().isEmpty()) {
            // Nothing to overwrite — fall through to the Save As flow.
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("Save Profile As"), tr("Profile name:"),
                QLineEdit::Normal, QString(), &ok);
            if (ok && !name.trimmed().isEmpty())
                profiles_->saveAs(name.trimmed());
        } else {
            profiles_->saveActive();
        }
        refresh();
    });

    connect(saveAsBtn, &QPushButton::clicked, this, [this, refresh]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Save Profile As"), tr("Profile name:"),
            QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            profiles_->saveAs(name.trimmed());
            refresh();
        }
    });

    connect(loadBtn, &QPushButton::clicked, this,
            [this, selectedName, refresh]() {
        const QString name = selectedName();
        if (name.isEmpty()) return;
        // loadByUser() == explicit operator pick → fires the companion
        // launch (load() alone, used by auto-recall, never does).
        if (!profiles_->loadByUser(name)) {
            QMessageBox::information(
                this, tr("Can’t switch now"),
                tr("Profiles can’t be switched while transmitting.  "
                   "Drop out of transmit and try again."));
        }
        refresh();
    });

    connect(renameBtn, &QPushButton::clicked, this,
            [this, selectedName, refresh]() {
        const QString old = selectedName();
        if (old.isEmpty()) return;
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Rename Profile"), tr("New name:"),
            QLineEdit::Normal, old, &ok);
        const QString trimmed = name.trimmed();
        if (ok && !trimmed.isEmpty() && trimmed != old) {
            profiles_->rename(old, trimmed);
            companion_->renameBinding(old, trimmed);   // keep the launch binding
        }
        refresh();
    });

    connect(deleteBtn, &QPushButton::clicked, this,
            [this, selectedName, refresh]() {
        const QString name = selectedName();
        if (name.isEmpty()) return;
        if (QMessageBox::question(
                this, tr("Delete Profile"),
                tr("Delete profile “%1”?").arg(name))
            == QMessageBox::Yes) {
            profiles_->remove(name);
            companion_->removeBinding(name);   // drop the launch binding too
            refresh();
        }
    });

    connect(defaultBtn, &QPushButton::clicked, this,
            [this, selectedName, refresh]() {
        const QString name = selectedName();
        if (name.isEmpty()) return;
        profiles_->setDefault(name);
        refresh();
    });

    // Per-mode binding combos.
    for (auto it = modeCombos->cbegin(); it != modeCombos->cend(); ++it) {
        const QString mode = it.key();
        QComboBox *cb = it.value();
        connect(cb, &QComboBox::activated, this, [this, mode, cb]() {
            if (cb->currentIndex() <= 0)        // index 0 == "(none)"
                profiles_->unbindMode(mode);
            else
                profiles_->bindMode(mode, cb->currentText());
        });
    }

    // --- companion-app field wiring ---
    // Reload the fields whenever the list selection moves.
    connect(list, &QListWidget::currentItemChanged, this,
            [syncCompanion, syncEdit](QListWidgetItem *, QListWidgetItem *) {
        syncCompanion();
        syncEdit();
    });

    // Quick-edit Apply / Revert.
    connect(applyEditBtn, &QPushButton::clicked, this,
            [applyEdit]() { applyEdit(); });
    connect(revertEditBtn, &QPushButton::clicked, this,
            [syncEdit]() { syncEdit(); });

    // Enable tick: persist + re-gate the sub-controls.
    connect(compEnable, &QCheckBox::toggled, this,
            [saveCompanion, syncCompanion](bool) {
        saveCompanion();
        syncCompanion();
    });

    // Text + delay edits persist on change.
    connect(compName, &QLineEdit::editingFinished, this,
            [saveCompanion]() { saveCompanion(); });
    connect(compCmd, &QLineEdit::editingFinished, this,
            [saveCompanion]() { saveCompanion(); });
    connect(compArgs, &QLineEdit::editingFinished, this,
            [saveCompanion]() { saveCompanion(); });
    connect(compDelay, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [saveCompanion](double) { saveCompanion(); });

    connect(browseBtn, &QPushButton::clicked, this,
            [this, compCmd, saveCompanion]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Choose companion program"), compCmd->text(),
            tr("Programs (*.exe *.bat *.cmd);;All files (*.*)"));
        if (!path.isEmpty()) {
            compCmd->setText(QDir::toNativeSeparators(path));
            saveCompanion();
        }
    });

    connect(testBtn, &QPushButton::clicked, this,
            [this, compCmd, compArgs]() {
        const QString cmd = compCmd->text().trimmed();
        if (cmd.isEmpty()) {
            QMessageBox::information(this, tr("Test launch"),
                                    tr("Enter a program path first."));
            return;
        }
        companion_->testLaunch(cmd, compArgs->text().trimmed());
    });

    return page;
}

// ── Backup & Restore ────────────────────────────────────────────────────────

void restoreBackupInteractive(QWidget *parent, const QString &filePath) {
    const QSet<QString> present = lyra::backup::sectionsPresentInFile(filePath);
    if (present.isEmpty()) {
        QMessageBox::warning(parent, QObject::tr("Restore"),
            QObject::tr("That file doesn't look like a Lyra backup."));
        return;
    }

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Restore settings — choose what to bring back"));
    dlg.resize(560, 620);
    auto *v = new QVBoxLayout(&dlg);
    auto *intro = new QLabel(QObject::tr(
        "Tick the groups to restore from this backup.  Unticked groups stay "
        "exactly as they are now — so you can revert just the part you broke.  "
        "(Your radio address and graphics backend are never touched.)"), &dlg);
    intro->setWordWrap(true);
    v->addWidget(intro);

    // Scrollable checklist — one row per section the file actually contains.
    auto *scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget(scroll);
    auto *iv = new QVBoxLayout(inner);
    QHash<QString, QCheckBox *> boxes;
    auto addRow = [&](const QString &id, const QString &label,
                      const QString &desc, bool isLayout) {
        if (!present.contains(id)) return;
        auto *cb = new QCheckBox(label, inner);
        cb->setChecked(!isLayout);   // layout is disruptive → default OFF
        iv->addWidget(cb);
        if (!desc.isEmpty()) {
            auto *d = new QLabel(QStringLiteral("     ") + desc, inner);
            d->setWordWrap(true);
            d->setStyleSheet(QStringLiteral("color:#8a97a0;font-size:11px;"
                                            "margin-bottom:4px;"));
            iv->addWidget(d);
        }
        boxes.insert(id, cb);
    };
    for (const lyra::backup::Section &s : lyra::backup::sections())
        addRow(s.id, s.label, s.desc, s.layout);
    addRow(lyra::backup::advancedId(),
           QObject::tr("Everything else (advanced)"),
           QObject::tr("Update checks, help-window state and any other stray "
                       "settings not covered above"), false);
    iv->addStretch(1);
    scroll->setWidget(inner);
    v->addWidget(scroll, 1);

    // Select all / none.
    auto *selRow = new QHBoxLayout();
    auto *selAll  = new QPushButton(QObject::tr("Select all"), &dlg);
    auto *selNone = new QPushButton(QObject::tr("Select none"), &dlg);
    selRow->addWidget(selAll);
    selRow->addWidget(selNone);
    selRow->addStretch(1);
    v->addLayout(selRow);
    QObject::connect(selAll, &QPushButton::clicked, &dlg, [&boxes]() {
        for (auto *cb : boxes) cb->setChecked(true);
    });
    QObject::connect(selNone, &QPushButton::clicked, &dlg, [&boxes]() {
        for (auto *cb : boxes) cb->setChecked(false);
    });

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(QObject::tr("Restore"));
    v->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    QSet<QString> chosen;
    for (auto it = boxes.constBegin(); it != boxes.constEnd(); ++it)
        if (it.value()->isChecked()) chosen.insert(it.key());
    if (chosen.isEmpty()) return;

    bool touchedLayout = false;
    const int n = lyra::backup::restoreFromFile(filePath, chosen, &touchedLayout);

    const auto btn = QMessageBox::question(parent,
        QObject::tr("Settings restored"),
        QObject::tr("Restored %1 setting(s) from the backup.\n\n"
                    "Restart Lyra now so every change takes effect cleanly?")
            .arg(n),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (btn == QMessageBox::Yes) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QStringList());
        QCoreApplication::quit();
    }
}

void SettingsDialog::refreshSnapshotList() {
    if (!snapList_) return;
    snapList_->clear();
    const auto snaps = lyra::backup::listSnapshots();
    for (const lyra::backup::SnapInfo &si : snaps) {
        QString label = si.when.toString(QStringLiteral("yyyy-MM-dd  HH:mm"))
                        + QStringLiteral("   ");
        if (!si.name.isEmpty())    label += si.name;
        else if (si.automatic)     label += tr("(automatic)");
        else                       label += tr("(manual)");
        auto *item = new QListWidgetItem(label, snapList_);
        item->setData(Qt::UserRole, si.path);
    }
    if (snapList_->count() == 0) {
        auto *item = new QListWidgetItem(tr("(no snapshots yet)"), snapList_);
        item->setFlags(Qt::NoItemFlags);
    }
}

QWidget *SettingsDialog::buildBackupRestoreTab() {
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);

    // Nudge the descriptive text up a point from the app default so the tab
    // reads a little easier.
    auto bumpFont = [](QLabel *l) {
        QFont f = l->font();
        f.setPointSizeF(f.pointSizeF() + 1.0);
        l->setFont(f);
    };

    auto *intro = new QLabel(tr(
        "Back up your whole Lyra configuration, keep automatic dated "
        "snapshots, and restore just the parts you choose — the clean way to "
        "undo a change that went sideways.\n\n"
        "Backups never include your radio's address or the graphics backend, "
        "so a file is safe to share or carry to another PC."), page);
    intro->setWordWrap(true);
    bumpFont(intro);
    v->addWidget(intro);

    // ── Export ──
    auto *expGrp = new QGroupBox(tr("Export a backup file"), page);
    auto *expV = new QVBoxLayout(expGrp);
    auto *expDesc = new QLabel(
        tr("Save everything to a .lyra file — the button opens a browser so "
           "you choose the folder and name."), expGrp);
    expDesc->setWordWrap(true);
    bumpFont(expDesc);
    expV->addWidget(expDesc);
    // Compact, left-aligned button (not a full-width bar).
    auto *expBtn = new QPushButton(tr("Export to a file…"), expGrp);
    auto *expRow = new QHBoxLayout();
    expRow->addWidget(expBtn);
    expRow->addStretch(1);
    expV->addLayout(expRow);
    v->addWidget(expGrp);
    connect(expBtn, &QPushButton::clicked, this, [this]() {
        const QString fallback = QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);
        const QString dir = QSettings().value(
            QStringLiteral("backup/lastExportDir"), fallback).toString();
        QString path = QFileDialog::getSaveFileName(
            this, tr("Export Lyra settings"),
            dir + QStringLiteral("/lyra-backup.lyra"),
            tr("Lyra backup (*.lyra)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".lyra"), Qt::CaseInsensitive))
            path += QStringLiteral(".lyra");
        if (lyra::backup::exportToFile(path)) {
            QSettings().setValue(QStringLiteral("backup/lastExportDir"),
                                 QFileInfo(path).absolutePath());
            QMessageBox::information(this, tr("Exported"),
                tr("Saved your settings to:\n%1").arg(path));
        } else {
            QMessageBox::warning(this, tr("Export failed"),
                tr("Couldn't write the file:\n%1").arg(path));
        }
    });

    // ── Share a layout ──
    // A "layout" is just the panel arrangement, exported to a small
    // .lyralayout file so operators can swap screen setups (post the file +
    // a screenshot on Discord / email).  Lighter and friendlier than a full
    // backup for the "here's how I arrange my screen" use case.
    auto *layGrp = new QGroupBox(tr("Share a layout"), page);
    auto *layV = new QVBoxLayout(layGrp);
    auto *layDesc = new QLabel(
        tr("Export just your panel arrangement to a small .lyralayout file "
           "you can share, or import one someone sent you.  Only the layout "
           "travels — not your settings, radio address, or window size — so "
           "it drops onto any screen."), layGrp);
    layDesc->setWordWrap(true);
    bumpFont(layDesc);
    layV->addWidget(layDesc);
    auto *layRow = new QHBoxLayout();
    auto *layExpBtn = new QPushButton(tr("Export layout…"), layGrp);
    auto *layImpBtn = new QPushButton(tr("Import layout…"), layGrp);
    layRow->addWidget(layExpBtn);
    layRow->addWidget(layImpBtn);
    layRow->addStretch(1);
    layV->addLayout(layRow);
    v->addWidget(layGrp);
    connect(layExpBtn, &QPushButton::clicked, this, [this]() {
        if (auto *mw = qobject_cast<MainWindow *>(parentWidget()))
            mw->exportLayoutToFile();
    });
    connect(layImpBtn, &QPushButton::clicked, this, [this]() {
        if (auto *mw = qobject_cast<MainWindow *>(parentWidget()))
            mw->importLayoutFromFile();
    });

    // ── Snapshots ──
    auto *snapGrp = new QGroupBox(tr("Snapshots"), page);
    auto *snapV = new QVBoxLayout(snapGrp);
    auto *snapDesc = new QLabel(
        tr("Lyra keeps automatic dated backups plus any you save by hand.  "
           "Restore one to roll back to a known-good setup."), snapGrp);
    snapDesc->setWordWrap(true);
    bumpFont(snapDesc);
    snapV->addWidget(snapDesc);

    auto *cfgRow = new QHBoxLayout();
    auto *everySpin = new QSpinBox(snapGrp);
    everySpin->setRange(0, 100);
    everySpin->setSpecialValueText(tr("Off"));   // 0 = disabled
    everySpin->setValue(QSettings().value(
        QLatin1String(lyra::backup::kAutoEvery),
        lyra::backup::kAutoEveryDefault).toInt());
    everySpin->setToolTip(tr("0 = don't take automatic snapshots"));
    auto *keepSpin = new QSpinBox(snapGrp);
    keepSpin->setRange(1, 50);
    keepSpin->setValue(QSettings().value(
        QLatin1String(lyra::backup::kKeepCount),
        lyra::backup::kKeepCountDefault).toInt());
    keepSpin->setToolTip(tr("Older automatic snapshots beyond this are "
                            "deleted; snapshots you name are always kept."));
    cfgRow->addWidget(new QLabel(tr("Auto-snapshot every"), snapGrp));
    cfgRow->addWidget(everySpin);
    cfgRow->addWidget(new QLabel(tr("launches, keep the newest"), snapGrp));
    cfgRow->addWidget(keepSpin);
    cfgRow->addStretch(1);
    snapV->addLayout(cfgRow);
    connect(everySpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [](int val) {
                QSettings().setValue(QLatin1String(lyra::backup::kAutoEvery), val);
            });
    connect(keepSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [](int val) {
                QSettings().setValue(QLatin1String(lyra::backup::kKeepCount), val);
            });

    snapList_ = new QListWidget(snapGrp);
    snapList_->setMinimumHeight(150);
    snapList_->setItemDelegate(new PickDelegate(snapList_));  // clear selection mark
    snapV->addWidget(snapList_);

    // Where the files live — shown + one-click openable so a user who
    // reinstalls Lyra can find their snapshots.  These live in %APPDATA% and
    // survive an uninstall / reinstall (and the registry-reset), so a fresh
    // install can Restore-from-file straight out of this folder.
    auto *pathRow = new QHBoxLayout();
    auto *pathLbl = new QLabel(
        tr("Stored in:  %1").arg(
            QDir::toNativeSeparators(lyra::backup::snapshotDir())), snapGrp);
    pathLbl->setStyleSheet(QStringLiteral("color:#8a97a0;"));
    pathLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);   // copyable
    auto *openBtn = new QPushButton(tr("Open folder"), snapGrp);
    pathRow->addWidget(pathLbl, 1);
    pathRow->addWidget(openBtn);
    snapV->addLayout(pathRow);
    connect(openBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(lyra::backup::snapshotDir()));
    });

    auto *btnRow = new QHBoxLayout();
    auto *saveNow   = new QPushButton(tr("Save snapshot now…"), snapGrp);
    auto *restoreBtn = new QPushButton(tr("Restore selected…"), snapGrp);
    auto *exportSel = new QPushButton(tr("Export selected…"), snapGrp);
    auto *delBtn    = new QPushButton(tr("Delete"), snapGrp);
    auto *fileBtn   = new QPushButton(tr("Restore from file…"), snapGrp);
    btnRow->addWidget(saveNow);
    btnRow->addWidget(restoreBtn);
    btnRow->addWidget(exportSel);
    btnRow->addWidget(delBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(fileBtn);
    snapV->addLayout(btnRow);
    v->addWidget(snapGrp);

    // ── Maintenance & recovery ──
    // Two "when things go sideways" tools that both take effect on the NEXT
    // launch (deferred, so nothing is torn out from under the running app):
    //   • rebuild the one-time FFT optimization cache (after a CPU change), and
    //   • wipe every setting back to factory defaults.
    auto *recGrp = new QGroupBox(tr("Maintenance & recovery"), page);
    auto *recV = new QVBoxLayout(recGrp);

    auto *wisdomDesc = new QLabel(
        tr("On first run Lyra tunes its FFT math to your CPU and caches it, so "
           "later launches start fast.  If you swap the CPU or move the drive "
           "to a different machine, rebuild it so it re-tunes to the new "
           "hardware."), recGrp);
    wisdomDesc->setWordWrap(true);
    bumpFont(wisdomDesc);
    recV->addWidget(wisdomDesc);
    auto *wisdomRow = new QHBoxLayout();
    auto *wisdomBtn = new QPushButton(tr("Rebuild FFT optimization on next start"),
                                      recGrp);
    wisdomRow->addWidget(wisdomBtn);
    wisdomRow->addStretch(1);
    recV->addLayout(wisdomRow);
    connect(wisdomBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("Rebuild FFT optimization"),
                tr("Delete the cached FFT plans so Lyra rebuilds them the next "
                   "time you start it?\n\nThe rebuild is a one-time step that "
                   "can take several minutes, during which the window stays "
                   "unresponsive — this is normal.")) != QMessageBox::Yes)
            return;
        if (lyra::dsp::WdspNative::deleteWisdom()) {
            QMessageBox::information(this, tr("Rebuild armed"),
                tr("The FFT optimization will rebuild the next time you start "
                   "Lyra."));
        } else {
            QMessageBox::warning(this, tr("Couldn't clear the cache"),
                tr("Lyra couldn't delete the cached file:\n%1")
                    .arg(QDir::toNativeSeparators(
                        lyra::dsp::WdspNative::wisdomFilePath())));
        }
    });

    auto *resetDesc = new QLabel(
        tr("The panic button: put every Lyra setting back to how a fresh "
           "install behaves.  A snapshot is saved first, so you can restore "
           "your old setup from the Snapshots list above if you change your "
           "mind.  Your saved snapshots and the FFT cache are kept."), recGrp);
    resetDesc->setWordWrap(true);
    bumpFont(resetDesc);
    recV->addWidget(resetDesc);
    auto *resetRow = new QHBoxLayout();
    auto *resetBtn = new QPushButton(tr("Reset to factory defaults…"), recGrp);
    resetRow->addWidget(resetBtn);
    resetRow->addStretch(1);
    recV->addLayout(resetRow);
    v->addWidget(recGrp);
    v->addStretch(1);

    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::warning(this, tr("Reset to factory defaults"),
                tr("This puts EVERY Lyra setting back to its default — bands, "
                   "profiles, calibrations, layout, network, all of it.\n\n"
                   "A snapshot is saved first so you can undo this from the "
                   "Snapshots list.  Your saved snapshots and the FFT cache are "
                   "not touched.\n\nContinue?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
        // Safety net: capture a named snapshot BEFORE arming the wipe (it's a
        // file, so it survives the QSettings clear on next launch).
        const int keep = QSettings().value(
            QLatin1String(lyra::backup::kKeepCount),
            lyra::backup::kKeepCountDefault).toInt();
        lyra::backup::saveSnapshot(tr("Before factory reset"),
                                   /*automatic=*/false, keep);
        // Arm the next-launch clear (main.cpp reads this before any pref loads).
        QSettings s;
        s.setValue(QStringLiteral("app/factoryResetPending"), true);
        s.sync();
        QMessageBox::information(this, tr("Reset armed"),
            tr("Close and reopen Lyra to finish the reset.\n\nEverything comes "
               "back at factory defaults.  If it wasn't what you wanted, open "
               "Settings → Backup & Restore and restore the \"Before factory "
               "reset\" snapshot."));
        refreshSnapshotList();
    });

    refreshSnapshotList();

    connect(saveNow, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Save snapshot"),
            tr("Name this snapshot (optional):"), QLineEdit::Normal,
            QString(), &ok).trimmed();
        if (!ok) return;
        const int keep = QSettings().value(
            QLatin1String(lyra::backup::kKeepCount),
            lyra::backup::kKeepCountDefault).toInt();
        if (lyra::backup::saveSnapshot(name, /*automatic=*/false, keep).isEmpty())
            QMessageBox::warning(this, tr("Snapshot"),
                tr("Couldn't save the snapshot."));
        refreshSnapshotList();
    });
    connect(restoreBtn, &QPushButton::clicked, this, [this]() {
        auto *item = snapList_->currentItem();
        const QString path = item ? item->data(Qt::UserRole).toString() : QString();
        if (path.isEmpty()) {
            QMessageBox::information(this, tr("Restore"),
                tr("Select a snapshot in the list first."));
            return;
        }
        restoreBackupInteractive(this, path);
    });
    connect(exportSel, &QPushButton::clicked, this, [this]() {
        auto *item = snapList_->currentItem();
        const QString src = item ? item->data(Qt::UserRole).toString() : QString();
        if (src.isEmpty()) {
            QMessageBox::information(this, tr("Export snapshot"),
                tr("Select a snapshot in the list first."));
            return;
        }
        // Copy the snapshot file out to a location the operator picks — e.g.
        // a USB stick / cloud folder before a reinstall.
        const QString fallback = QSettings().value(
            QStringLiteral("backup/lastExportDir"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .toString();
        QString dst = QFileDialog::getSaveFileName(
            this, tr("Export snapshot to a file"),
            fallback + QLatin1Char('/') + QFileInfo(src).fileName(),
            tr("Lyra backup (*.lyra)"));
        if (dst.isEmpty()) return;
        if (!dst.endsWith(QStringLiteral(".lyra"), Qt::CaseInsensitive))
            dst += QStringLiteral(".lyra");
        if (QFileInfo(src).absoluteFilePath() == QFileInfo(dst).absoluteFilePath()) {
            QMessageBox::information(this, tr("Export snapshot"),
                tr("That's the snapshot's own location — pick a different "
                   "folder or name."));
            return;
        }
        QFile::remove(dst);              // QFile::copy won't overwrite
        if (QFile::copy(src, dst)) {
            QSettings().setValue(QStringLiteral("backup/lastExportDir"),
                                 QFileInfo(dst).absolutePath());
            QMessageBox::information(this, tr("Exported"),
                tr("Copied the snapshot to:\n%1").arg(dst));
        } else {
            QMessageBox::warning(this, tr("Export failed"),
                tr("Couldn't copy to:\n%1").arg(dst));
        }
    });
    connect(delBtn, &QPushButton::clicked, this, [this]() {
        auto *item = snapList_->currentItem();
        const QString path = item ? item->data(Qt::UserRole).toString() : QString();
        if (path.isEmpty()) return;
        if (QMessageBox::question(this, tr("Delete snapshot"),
                tr("Delete this snapshot?\n\n%1").arg(item->text()))
            == QMessageBox::Yes) {
            lyra::backup::deleteSnapshot(path);
            refreshSnapshotList();
        }
    });
    connect(fileBtn, &QPushButton::clicked, this, [this]() {
        const QString docs = QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Restore from a backup file"), docs,
            tr("Lyra backup (*.lyra);;All files (*)"));
        if (!path.isEmpty()) restoreBackupInteractive(this, path);
    });

    return page;
}

} // namespace lyra::ui

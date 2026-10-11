// Lyra — application theme (QtWidgets stylesheet).
//
// Ported from the Python-Lyra theme (lyra/ui/theme.py): the cool-CRT
// dark scheme — deep blue-black surfaces, electric-cyan primary,
// neon-green secondary.  Applied app-wide via qApp->setStyleSheet so
// the QtWidgets shell (menu bar, dock title bars, Settings dialog,
// tabs, spin boxes, sliders, etc.) matches old Lyra instead of the
// default light Windows style.  The panadapter glass background is the
// one deliberate new element; everything else tracks old Lyra.
//
// Tokens (resolved to literals here):
//   BG_APP    rgb(10,13,18)    BG_PANEL  rgb(17,22,32)
//   BG_RECESS rgb(16,20,28)    BG_CTRL   rgb(22,28,40)
//   ACCENT    rgb(0,229,255)   ACCENT_DIM rgb(0,168,200)
//   ACCENT2   rgb(57,255,20)
//   TEXT_PRIMARY rgb(205,217,229)  TEXT_MUTED rgb(138,154,172)
//   TEXT_FAINT rgb(90,112,128)     BORDER rgb(30,42,58)

#pragma once

#include <QString>

#ifdef Q_OS_MACOS
#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionComboBox>
#endif

namespace lyra::ui {

inline QString lyraStyleSheet() {
    return QStringLiteral(R"QSS(
QMainWindow, QWidget {
    background: rgb(10,13,18);
    color: rgb(205,217,229);
}
QLabel { color: rgb(138,154,172); }

QToolTip {
    background: rgb(17,22,32);
    color: rgb(205,217,229);
    border: 1px solid rgb(0,229,255);
    border-radius: 4px;
    padding: 8px 10px;
    font-size: 13pt;
}

QLineEdit, QComboBox {
    background: rgb(22,28,40);
    color: rgb(205,217,229);
    border: 1px solid rgb(30,42,58);
    border-radius: 3px;
    padding: 4px 6px;
    selection-background-color: rgba(0,229,255,80);
}
QLineEdit:focus, QComboBox:focus { border-color: rgb(0,229,255); }
QComboBox QAbstractItemView {
    background: rgb(17,22,32);
    color: rgb(205,217,229);
    border: 1px solid rgb(30,42,58);
    selection-background-color: rgba(0,229,255,80);
}

QPushButton {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 rgb(22,28,40), stop:1 rgb(16,20,28));
    color: rgb(205,217,229);
    border: 1px solid rgb(30,42,58);
    border-radius: 4px;
    padding: 5px 12px;
    font-weight: 600;
    letter-spacing: 0.5px;
}
QPushButton:hover { border-color: rgb(0,229,255); color: rgb(0,229,255); }
QPushButton:checked {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 rgb(0,168,200), stop:1 rgb(22,28,40));
    border-color: rgb(0,229,255);
    color: rgb(10,13,18);
}
QPushButton:pressed { background: rgb(16,20,28); }
QPushButton:disabled {
    background: rgb(16,20,28);
    color: rgb(90,112,128);
    border-color: rgb(30,42,58);
}

QSlider::groove:horizontal {
    height: 4px;
    background: rgb(16,20,28);
    border-radius: 2px;
    border: 1px solid rgb(30,42,58);
}
QSlider::handle:horizontal {
    background: rgb(0,229,255);
    width: 12px; margin: -6px 0;
    border-radius: 2px;
    border: 1px solid rgb(10,13,18);
}
QSlider::handle:horizontal:hover { background: rgb(57,255,20); }

QStatusBar {
    background: rgb(16,20,28);
    color: rgb(138,154,172);
    border-top: 1px solid rgb(30,42,58);
}

/* Solar / Propagation panel — a QtWidgets strip; give its root the same
   surface as the QML panels (#101820 / #2a4a5a) so it doesn't show the
   darker app background.  ID-scoped so child value boxes keep their own. */
QWidget#solarPanel { background: rgb(16,24,32); border: 1px solid rgb(42,74,90); }

/* Dockable panels */
QDockWidget {
    color: rgb(205,217,229);
    font-weight: 600;
    border: 1px solid rgb(30,42,58);
}
QDockWidget::title {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 rgb(17,22,32), stop:0.5 rgb(22,28,40), stop:1 rgb(16,20,28));
    color: rgb(0,229,255);
    padding-left: 8px;
    padding-top: 6px;
    padding-bottom: 6px;
    border-bottom: 1px solid rgba(0,229,255,100);
}
QDockWidget::close-button, QDockWidget::float-button {
    subcontrol-position: right center;
    padding: 1px;
    margin-right: 2px;
    border: 1px solid transparent;
    border-radius: 3px;
    background: transparent;
}
QDockWidget::close-button:hover, QDockWidget::float-button:hover {
    background: rgba(0,229,255,60);
    border: 1px solid rgb(0,229,255);
}

QTabWidget::pane {
    background: rgb(17,22,32);
    border: 1px solid rgb(30,42,58);
    border-radius: 4px;
    top: -1px;
}
QTabBar::tab {
    background: rgb(16,20,28);
    color: rgb(138,154,172);
    padding: 6px 14px;
    margin-right: 2px;
    border: 1px solid rgb(30,42,58);
    border-bottom: none;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    min-width: 70px;
}
QTabBar::tab:hover {
    color: rgb(205,217,229);
    background: rgb(22,28,40);
    border-color: rgb(0,229,255);
}
QTabBar::tab:selected {
    background: rgb(17,22,32);
    color: rgb(0,229,255);
    border-color: rgb(0,229,255);
    border-bottom: 1px solid rgb(17,22,32);
    font-weight: 700;
}

QGroupBox {
    color: rgb(0,229,255);
    font-weight: 700;
    border: 1px solid rgb(30,42,58);
    border-radius: 4px;
    margin-top: 14px;
    padding: 8px 6px 6px 6px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    padding: 0 6px;
    background: rgb(10,13,18);
    color: rgb(0,229,255);
}

QCheckBox { color: rgb(205,217,229); spacing: 7px; }
QCheckBox::indicator {
    width: 16px; height: 16px;
    background: rgb(16,20,28);
    border: 1px solid rgb(138,154,172);
    border-radius: 3px;
}
QCheckBox::indicator:hover { border-color: rgb(0,229,255); background: rgb(17,22,32); }
QCheckBox::indicator:checked { background: rgb(0,229,255); border-color: rgb(0,229,255); }

QRadioButton { color: rgb(205,217,229); spacing: 6px; padding: 3px 0; }
QRadioButton::indicator {
    width: 16px; height: 16px;
    background: rgb(16,20,28);
    border: 1px solid rgb(138,154,172);
    border-radius: 9px;
}
QRadioButton::indicator:hover { border-color: rgb(0,229,255); background: rgb(17,22,32); }
QRadioButton::indicator:checked {
    background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,
        stop:0 rgb(0,229,255), stop:0.45 rgb(0,229,255),
        stop:0.55 rgb(16,20,28), stop:1 rgb(16,20,28));
    border-color: rgb(0,229,255);
}

QAbstractSpinBox {
    background: rgb(22,28,40);
    color: rgb(205,217,229);
    border: 1px solid rgb(30,42,58);
    border-radius: 3px;
    padding: 3px 20px 3px 4px;
}
QAbstractSpinBox:focus { border-color: rgb(0,229,255); }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
    subcontrol-origin: border;
    width: 16px;
    background: rgb(22,28,40);
    border-left: 1px solid rgb(30,42,58);
}
QAbstractSpinBox::up-button { subcontrol-position: top right; border-bottom: 1px solid rgb(30,42,58); }
QAbstractSpinBox::down-button { subcontrol-position: bottom right; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: rgb(0,168,200); }
QAbstractSpinBox::up-arrow {
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-bottom: 5px solid rgb(205,217,229);
}
QAbstractSpinBox::up-arrow:hover { border-bottom-color: rgb(0,229,255); }
QAbstractSpinBox::down-arrow {
    width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid rgb(205,217,229);
}
QAbstractSpinBox::down-arrow:hover { border-top-color: rgb(0,229,255); }

QSplitter::handle { background: rgb(16,20,28); border: 1px solid rgb(30,42,58); }
QSplitter::handle:hover { background: rgba(0,229,255,120); border-color: rgb(0,229,255); }

QMenuBar { background: rgb(10,13,18); color: rgb(205,217,229); border-bottom: 1px solid rgb(30,42,58); }
QMenuBar::item { background: transparent; padding: 4px 10px; }
QMenuBar::item:selected { background: rgba(0,229,255,50); color: rgb(0,229,255); }
QMenu { background: rgb(17,22,32); color: rgb(205,217,229); border: 1px solid rgb(30,42,58); }
QMenu::item { padding: 5px 24px 5px 12px; }
QMenu::item:selected { background: rgba(0,229,255,50); color: rgb(0,229,255); }
QDialog { background: rgb(10,13,18); }
QToolBar { background: rgb(10,13,18); border: none; spacing: 4px; }

/* §15.28 — Settings dialog design language additions.  Opt-in via
   widget->setProperty("lyra<name>", true).  Cascades through every
   QWidget that uses the qApp stylesheet, so these markers are
   available from anywhere in the app, not just the Settings dialog. */

/* Column header — "TIMING", "AUDIO + GAIN", etc. at the top of a
   multi-column tab.  Larger + bolder than a group title, with a thin
   cyan underline so the columns read as visually distinct sections. */
QLabel[lyraColHeader="true"] {
    color: rgb(0,229,255);
    font-weight: 800;
    font-size: 13pt;
    letter-spacing: 1.5px;
    padding-bottom: 4px;
    border-bottom: 1px solid rgba(0,229,255,80);
    margin-bottom: 6px;
}

/* Column subtitle — italic muted descriptor under the column header
   ("set once", "operating", etc.). */
QLabel[lyraColSubtitle="true"] {
    color: rgb(138,154,172);
    font-style: italic;
    font-size: 9pt;
    padding-bottom: 10px;
}

/* Group subtitle — italic muted one-liner directly under a QGroupBox
   title row inside a form.  Use sparingly (only where the group title
   alone isn't self-evident). */
QLabel[lyraGroupSubtitle="true"] {
    color: rgb(138,154,172);
    font-style: italic;
    font-size: 9pt;
}

/* Restore-defaults button — amber accent so it visually pops out of
   the regular cyan-on-dark control field.  Lyra's "engaged orange"
   token (rgb(255,154,60), same as old-Lyra dsp_btn checked state). */
QPushButton[lyraRestore="true"] {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 rgb(58,42,20), stop:1 rgb(42,26,10));
    color: rgb(255,154,60);
    border: 1px solid rgb(90,58,26);
}
QPushButton[lyraRestore="true"]:hover {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 rgb(90,58,26), stop:1 rgb(58,42,20));
    border-color: rgb(255,154,60);
    color: rgb(255,184,90);
}

/* Footer description text — small italic muted, typically a paragraph
   under the Restore button explaining live-apply / methodology notes. */
QLabel[lyraFooter="true"] {
    color: rgb(138,154,172);
    font-style: italic;
    font-size: 9pt;
    padding: 6px 4px 0 4px;
}
)QSS");
}

#ifdef Q_OS_MACOS
// macOS combo popups clip their items under the app stylesheet.
//
// The macOS style paints a QComboBox popup as a native-looking menu: every
// item's text starts after an ~18 px checkmark column.  But QComboBox sizes
// that popup from the CLOSED combo (popup = combo width - 16, widened only by
// the combo's own width hint), and once the stylesheet above styles QComboBox
// the closed box is just text + 6 px padding + arrow (~text + 30) instead of
// the native bezel's ~text + 52.  The popup therefore comes out ~text + 14
// wide while the text is painted from x = 18, so the tail of the longest
// items is cut off ("Vertic").  Fusion / Windows paint popup text near the
// left edge (x = 7 / 3) with no -16 popup inset, so only macOS needs this.
//
// Fix, once for every combo: when a combo's popup window is about to show,
// grow it to the native width (checkmark column + widest item + the same
// trailing margin the un-styled native popup leaves), keeping it on screen.
// Runs before the popup is mapped (QShowEvent precedes the platform show).
class ComboPopupWidthFix final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (ev->type() != QEvent::Show || !obj->isWidgetType())
            return false;
        auto *popup = static_cast<QWidget *>(obj);
        auto *combo = qobject_cast<QComboBox *>(popup->parentWidget());
        if (!combo || !popup->isWindow() || combo->view()->window() != popup)
            return false;
        // Only the menu-style popup has the checkmark column; an editable
        // combo drops a plain list that already fits.
        QStyleOptionComboBox opt;
        opt.initFrom(combo);
        opt.editable = combo->isEditable();
        if (!combo->style()->styleHint(QStyle::SH_ComboBox_Popup, &opt, combo))
            return false;

        // Measured against QMacStyle's menu-item painter (Qt 6.11): text
        // starts at x = 18; the un-styled native popup leaves 18 px after
        // the widest item.
        constexpr int kCheckColumn = 18;
        constexpr int kTrailing = 18;
        QAbstractItemView *view = combo->view();
        const QFontMetrics fm = combo->fontMetrics();
        const QFontMetrics vfm = view->fontMetrics();
        int text = 0;
        for (int i = 0; i < combo->count(); ++i) {
            const QString t = combo->itemText(i);
            text = qMax(text, qMax(fm.horizontalAdvance(t),
                                   vfm.horizontalAdvance(t)));
        }
        // Frame + any vertical scroll bar: whatever the popup spends
        // outside the item viewport.
        const int chrome = popup->width() - view->viewport()->width();
        const int need = kCheckColumn + text + kTrailing + chrome;
        if (popup->width() >= need)
            return false;
        QRect g = popup->geometry();
        g.setWidth(need);
        // Clamp to the monitor the combo is on.  popup->screen() is not
        // usable here: the popup's native window doesn't exist yet on its
        // first show, so it reports the primary screen and the clamp would
        // throw the list onto the other monitor.
        const QScreen *s = QGuiApplication::screenAt(
            combo->mapToGlobal(combo->rect().center()));
        if (!s) s = combo->screen();
        if (s) {
            const QRect avail = s->availableGeometry();
            g.setWidth(qMin(g.width(), avail.width()));   // never wider than the screen
            if (g.right() > avail.right())
                g.moveRight(avail.right());
            if (g.left() < avail.left())
                g.moveLeft(avail.left());
        }
        popup->setGeometry(g);
        return false;
    }
};
#endif

// Install the app-wide QtWidgets fixes that go with the stylesheet above.
// Call once, right after qApp->setStyleSheet(lyraStyleSheet()).
inline void installStyleFixes(QObject *app) {
#ifdef Q_OS_MACOS
    app->installEventFilter(new ComboPopupWidthFix(app));
#else
    Q_UNUSED(app);
#endif
}

} // namespace lyra::ui

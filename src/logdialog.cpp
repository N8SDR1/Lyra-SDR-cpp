// Lyra — in-app Log viewer.  See logdialog.h.

#include "logdialog.h"

#include "logbuffer.h"
#include "prefs.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>

#include <algorithm>
#include <QProcess>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace lyra::ui {

LogDialog::LogDialog(Prefs *prefs, QWidget *parent)
    : QDialog(parent), prefs_(prefs) {
    setWindowTitle(tr("Lyra — Diagnostic Log"));
    resize(820, 520);

    auto *outer = new QVBoxLayout(this);

    auto *hint = new QLabel(
        tr("Diagnostic messages from this session. If something "
           "misbehaves, turn on <b>verbose debug logging</b>, reproduce "
           "the problem, then <b>Copy</b> or <b>Save</b> this and send it "
           "to us with a description of what happened."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("QLabel{color:#8fa6ba;}"));
    outer->addWidget(hint);

    view_ = new QPlainTextEdit(this);
    view_->setReadOnly(true);
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(9);
    view_->setFont(mono);
    view_->setPlainText(LogBuffer::instance().text());
    view_->moveCursor(QTextCursor::End);
    outer->addWidget(view_, 1);

    auto *row = new QHBoxLayout;
    verbose_ = new QCheckBox(tr("Verbose debug logging"), this);
    verbose_->setToolTip(tr("Also capture detailed Debug/Info messages "
                            "(more verbose). Mirrors Settings → Hardware."));
    if (prefs_) verbose_->setChecked(prefs_->debugLogging());
    connect(verbose_, &QCheckBox::toggled, this, [this](bool on) {
        if (prefs_) prefs_->setDebugLogging(on);
        else        LogBuffer::instance().setVerbose(on);
    });
    if (prefs_) {
        connect(prefs_, &Prefs::debugLoggingChanged, verbose_, [this]() {
            if (verbose_->isChecked() != prefs_->debugLogging())
                verbose_->setChecked(prefs_->debugLogging());
        });
    }
    row->addWidget(verbose_);
    row->addStretch(1);

    auto *copyBtn = new QPushButton(tr("&Copy all"), this);
    connect(copyBtn, &QPushButton::clicked, this, &LogDialog::copyAll);
    row->addWidget(copyBtn);

    auto *saveBtn = new QPushButton(tr("&Save…"), this);
    connect(saveBtn, &QPushButton::clicked, this, &LogDialog::saveToFile);
    row->addWidget(saveBtn);

    auto *folderBtn = new QPushButton(tr("Open log &folder"), this);
    connect(folderBtn, &QPushButton::clicked, this, &LogDialog::openFolder);
    row->addWidget(folderBtn);

    auto *clearBtn = new QPushButton(tr("C&lear"), this);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        LogBuffer::instance().clear();
        view_->clear();
    });
    row->addWidget(clearBtn);

    auto *closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    row->addWidget(closeBtn);

    outer->addLayout(row);

    // Live updates while the dialog is open.
    connect(&LogBuffer::instance(), &LogBuffer::lineAppended,
            this, &LogDialog::appendLine);
}

void LogDialog::showEvent(QShowEvent *e) {
    QDialog::showEvent(e);
    fitToScreen();
    // The title-bar size is only known once Windows has drawn the frame;
    // check again then.
    QTimer::singleShot(0, this, &LogDialog::fitToScreen);
}

// Keep the whole window — title bar to button row — on the visible part
// of the screen it opens on (inside the taskbar).  On some high-DPI setups
// the window opened with its bottom edge off-screen, hiding the Copy /
// Save / Open log folder row.  Only shrinks or moves; a window that
// already fits is left alone.
void LogDialog::fitToScreen() {
    QScreen *scr = screen();
    if (!scr && parentWidget()) scr = parentWidget()->screen();
    if (!scr) scr = QGuiApplication::primaryScreen();
    if (!scr) return;
    const QRect avail = scr->availableGeometry();

    // Frame (title bar + borders) around the client area.
    const QRect frame = frameGeometry();
    const QRect inner = geometry();
    const int extraW = std::max(0, frame.width()  - inner.width());
    const int extraH = std::max(0, frame.height() - inner.height());

    QSize sz = size();
    sz.setWidth(std::min(sz.width(),   avail.width()  - extraW));
    sz.setHeight(std::min(sz.height(), avail.height() - extraH));
    sz = sz.expandedTo(minimumSizeHint().boundedTo(
        QSize(avail.width() - extraW, avail.height() - extraH)));
    if (sz != size()) resize(sz);

    QRect f = frameGeometry();
    QPoint tl = f.topLeft();
    if (f.right()  > avail.right())  tl.rx() -= f.right()  - avail.right();
    if (f.bottom() > avail.bottom()) tl.ry() -= f.bottom() - avail.bottom();
    tl.setX(std::max(tl.x(), avail.left()));
    tl.setY(std::max(tl.y(), avail.top()));
    if (tl != f.topLeft()) move(tl);
}

void LogDialog::appendLine(const QString &line) {
    QScrollBar *sb = view_->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;
    view_->appendPlainText(line);
    if (atBottom) sb->setValue(sb->maximum());
}

void LogDialog::copyAll() {
    QApplication::clipboard()->setText(view_->toPlainText());
}

void LogDialog::saveToFile() {
    const QString fn = QFileDialog::getSaveFileName(
        this, tr("Save log"), QStringLiteral("lyra-log.txt"),
        tr("Text files (*.txt);;All files (*)"));
    if (fn.isEmpty()) return;
    QSaveFile f(fn);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(&f) << view_->toPlainText();
        f.commit();
    }
}

void LogDialog::openFolder() {
    const QString path = LogBuffer::instance().logFilePath();
    if (path.isEmpty()) return;
    const QString dir = QFileInfo(path).absolutePath();
#ifdef Q_OS_WIN
    // Open Explorer on the folder via its NATIVE path.  Two forms fail
    // here: QDesktopServices::openUrl() passes a file:// URL (Explorer
    // rejects it for folders — "Location is not available"), and
    // explorer.exe /select,<file> has finicky comma parsing.  A plain
    // native folder path is the reliable form.
    if (!QProcess::startDetached(QStringLiteral("explorer.exe"),
                                 { QDir::toNativeSeparators(dir) })) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));   // fallback
    }
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
#endif
}

} // namespace lyra::ui

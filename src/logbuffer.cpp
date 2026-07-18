// Lyra — in-app diagnostic log buffer.  See logbuffer.h.

#include "logbuffer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace lyra::ui {

LogBuffer &LogBuffer::instance() {
    static LogBuffer inst;
    return inst;
}

void LogBuffer::install() {
    QMutexLocker lock(&mutex_);
    if (installed_) return;

    verbose_ = QSettings().value(QStringLiteral("debug/logging"), false).toBool();

    // Log file under the app-data dir (e.g.
    // %APPDATA%/N8SDR/Lyra-cpp/logs/lyra-log.txt).  Truncated each
    // launch so the file reflects the current session.
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/logs");
    QDir().mkpath(dir);
    const QString primary = dir + QStringLiteral("/lyra-log.txt");

    // Preserve the previous session's log so a crash trace survives one
    // relaunch (this session truncates the primary below).  Best-effort: if
    // the old file is still held by another Lyra process the rename fails and
    // it stays put — the lock fallback below keeps THIS session logging anyway.
    QFile::remove(dir + QStringLiteral("/lyra-log.prev.txt"));
    QFile::rename(primary, dir + QStringLiteral("/lyra-log.prev.txt"));

    // Robust open (bug fix 2026-07-17 — "log file dead / every tester log is
    // stale").  A WriteOnly|Truncate open FAILS while the file is locked by
    // another Lyra process — an overlapping launch, a second copy, or a
    // lingering/zombie instance from a prior crash (its LogBuffer handle isn't
    // released until that process exits).  Before this fix a failed open left
    // `file_` closed and the WHOLE session logged to nothing, silently, with
    // no fallback and no on-screen sign — so the on-disk file just kept showing
    // an old session's date.  Now: if the primary is locked, fall back to a
    // per-process file so a session ALWAYS logs somewhere the operator can
    // Save + send, and surface it (logFilePath() → the live file, so Help →
    // Show Log and its Save button follow).
    filePath_ = primary;
    file_.setFileName(filePath_);
    bool opened =
        file_.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    if (!opened) {
        const QString alt =
            dir + QStringLiteral("/lyra-log-%1.txt")
                      .arg(QCoreApplication::applicationPid());
        file_.setFileName(alt);
        opened = file_.open(QIODevice::WriteOnly | QIODevice::Truncate
                            | QIODevice::Text);
        if (opened) filePath_ = alt;
    } else {
        // Primary opened cleanly → no collision this launch, so sweep away any
        // stale per-process fallback files a past collision left behind (a
        // still-live holder's file just fails to delete — best-effort).
        const auto stale = QDir(dir).entryList(
            {QStringLiteral("lyra-log-*.txt")}, QDir::Files);
        for (const QString &f : stale)
            QFile::remove(dir + QLatin1Char('/') + f);
    }

    if (opened) {
        stream_.setDevice(&file_);
        stream_ << QStringLiteral("=== Lyra log — %1 ===\n")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        stream_.flush();
    }

    prev_ = qInstallMessageHandler(&LogBuffer::handler);
    installed_ = true;

    // Release the buffer mutex BEFORE surfacing the warning below: qWarning
    // re-enters LogBuffer::handler → append(), which locks mutex_ again, and
    // this QMutex is non-recursive → a self-deadlock if still held here.
    lock.unlock();

    // Surface a locked-primary fallback through the handler (now installed) so
    // it lands in BOTH the file and the live in-app viewer — a visible sign
    // that another Lyra instance was holding the log, instead of silence.
    if (opened && filePath_ != primary)
        qWarning("[log] lyra-log.txt was locked by another Lyra instance — "
                 "this session is logging to %s",
                 qUtf8Printable(QFileInfo(filePath_).fileName()));
    else if (!opened)
        qWarning("[log] could not open any log file in %s — logging to the "
                 "in-app viewer only this session", qUtf8Printable(dir));
}

void LogBuffer::setVerbose(bool on) {
    QMutexLocker lock(&mutex_);
    verbose_ = on;
}

QString LogBuffer::text() const {
    QMutexLocker lock(&mutex_);
    return lines_.join(QLatin1Char('\n'));
}

void LogBuffer::clear() {
    QMutexLocker lock(&mutex_);
    lines_.clear();
}

void LogBuffer::handler(QtMsgType type, const QMessageLogContext &ctx,
                        const QString &msg) {
    LogBuffer &self = instance();

    // Drop benign Qt Quick advisory noise that otherwise floods the log
    // (hundreds of lines per launch) and buries real messages.  Lyra
    // deliberately custom-draws every control's background / contentItem, so
    // the native Controls style logs "does not support customization" for each
    // one — harmless, the customization is honored anyway.  The panadapter /
    // waterfall `palette` property intentionally shadows the base one, logging
    // "overrides a member of the base object".  Neither is actionable; hide
    // both so tester logs stay readable.
    if (msg.contains(QLatin1String("does not support customization"))
        || msg.contains(QLatin1String("overrides a member of the base object")))
        return;

    // Format: HH:mm:ss.zzz  LEVEL  [category]  message
    const char *lvl = "INFO ";
    switch (type) {
    case QtDebugMsg:    lvl = "DEBUG"; break;
    case QtInfoMsg:     lvl = "INFO "; break;
    case QtWarningMsg:  lvl = "WARN "; break;
    case QtCriticalMsg: lvl = "ERROR"; break;
    case QtFatalMsg:    lvl = "FATAL"; break;
    }
    QString line = QStringLiteral("%1  %2  ")
                       .arg(QDateTime::currentDateTime().toString(
                                QStringLiteral("HH:mm:ss.zzz")))
                       .arg(QString::fromLatin1(lvl));
    if (ctx.category && qstrcmp(ctx.category, "default") != 0)
        line += QStringLiteral("[%1] ").arg(QString::fromLatin1(ctx.category));
    line += msg;

    self.append(type, line);

    // Chain to the previous handler (a console-subsystem debug build
    // still prints; release GUI builds have none and this is a no-op).
    if (self.prev_) self.prev_(type, ctx, msg);
}

void LogBuffer::append(QtMsgType type, const QString &line) {
    {
        QMutexLocker lock(&mutex_);
        // Verbose OFF: drop Debug/Info — keep the file small + quiet.
        if (!verbose_ && (type == QtDebugMsg || type == QtInfoMsg))
            return;
        lines_.append(line);
        if (lines_.size() > kMaxLines)
            lines_.remove(0, lines_.size() - kMaxLines);
        if (file_.isOpen()) {
            stream_ << line << '\n';
            stream_.flush();
        }
    }
    emit lineAppended(line);
}

} // namespace lyra::ui

// Lyra — CompanionLauncher (#193).
//
// Optionally launches a profile's companion digital-mode app (VarAC, MSHV,
// WSJT-X, …) when the operator EXPLICITLY selects that profile
// (ProfileManager::userLoaded — NOT on the automatic mode-change auto-recall,
// and NOT on the startup default).
//
// The launch binding is a PER-MACHINE setting — QSettings "profileLaunch/<name>"
// keyed by profile NAME, deliberately SEPARATE from the portable profile JSON.
// It is never written into a profile, never exported in a .lyra, and never
// auto-run on import (MainWindow::isMachineSpecificKey filters the whole
// "profileLaunch/" subtree).  A shared profile carries the DSP chain only —
// never "run this exe".
//
// Behaviour: fire-and-forget (QProcess::startDetached — closing Lyra never
// kills the app); a configurable delay before launch so Lyra's CAT server + VAC
// come up first; skip if the app is already running (best-effort, by exe
// basename).  On a profile SWITCH we do nothing — the operator closes the app.

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace lyra::profile {

class CompanionLauncher : public QObject {
    Q_OBJECT
public:
    explicit CompanionLauncher(QObject *parent = nullptr);

    struct Config {
        bool    enabled = false;
        QString name;          // friendly label (so it's obvious what it is)
        QString command;       // .exe / .bat / .cmd path
        QString args;          // optional command-line arguments
        int     delayMs = 2500;
    };

    Config config(const QString &profile) const;
    void   setConfig(const QString &profile, const Config &c);
    void   renameBinding(const QString &oldName, const QString &newName);
    void   removeBinding(const QString &profile);

    // Explicit pick of <profile> → honour its binding: skip if already running,
    // else launch after the configured delay.  Wired to userLoaded.
    void   launchFor(const QString &profile);
    // Immediate launch (no delay, no already-running guard) for the Settings
    // "Test" button.  Returns false + emits statusMessage on failure.
    bool   testLaunch(const QString &command, const QString &args);

    // Fire-and-forget launch with no instance / no signals — used by the
    // global startup auto-launch (Settings → Hardware → Startup), which has
    // no per-profile launcher.  Same .bat/.cmd-through-cmd.exe + working-dir
    // handling as the profile companion launch.  Returns true on spawn.
    static bool launchDetached(const QString &command,
                               const QString &args = QString());

    // True if a process whose image basename matches `command` is already
    // running on this PC.  Used by the Apps chip / named-app list so a
    // second click does not spawn another copy.
    static bool isRunning(const QString &command);

signals:
    void statusMessage(const QString &msg);

private:
    static QString keyOf(const QString &profile, const char *leaf);
    bool           startApp(const QString &command, const QString &args);

    QTimer  pending_;          // single-shot delay; last pick wins
    QString pendingCommand_;
    QString pendingArgs_;
};

}  // namespace lyra::profile

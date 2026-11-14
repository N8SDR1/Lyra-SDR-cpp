// Named third-party apps (FLDigi, Open-SSTV, WSJT-X, …) — per-PC only.
// Hardware "Startup" (SDRLogger+ + two path slots) is a separate surface.

#pragma once

#include <QString>
#include <QVector>

namespace lyra {

struct AppShortcut {
    QString name;
    QString path;
    QString args;
    bool    autoStart = false;
};

class AppShortcuts {
public:
    static constexpr int kMax = 8;

    static QVector<AppShortcut> load();
    static void save(const QVector<AppShortcut> &list);

    static QString displayName(const AppShortcut &e);
    // Skip-if-running.  Returns false if path empty, already running, or spawn failed.
    static bool launch(const AppShortcut &e, QString *statusOut = nullptr);
};

}  // namespace lyra

#include "appsstore.h"

#include "profile/CompanionLauncher.h"

#include <QFileInfo>
#include <QObject>
#include <QSettings>

namespace lyra {

static QString displayNameOf(const AppShortcut &e) {
    const QString n = e.name.trimmed();
    if (!n.isEmpty()) return n;
    const QString p = e.path.trimmed();
    if (p.isEmpty()) return QString();
    return QFileInfo(p).completeBaseName();
}

QVector<AppShortcut> AppShortcuts::load() {
    QVector<AppShortcut> out;
    QSettings s;
    const int n = s.beginReadArray(QStringLiteral("apps/list"));
    for (int i = 0; i < n && i < kMax; ++i) {
        s.setArrayIndex(i);
        AppShortcut e;
        e.name      = s.value(QStringLiteral("name")).toString();
        e.path      = s.value(QStringLiteral("path")).toString();
        e.args      = s.value(QStringLiteral("args")).toString();
        e.autoStart = s.value(QStringLiteral("autoStart"), false).toBool();
        out.append(e);
    }
    s.endArray();
    while (out.size() < kMax)
        out.append(AppShortcut{});
    return out;
}

void AppShortcuts::save(const QVector<AppShortcut> &list) {
    QSettings s;
    s.remove(QStringLiteral("apps/list"));
    s.beginWriteArray(QStringLiteral("apps/list"), kMax);
    for (int i = 0; i < kMax; ++i) {
        s.setArrayIndex(i);
        const AppShortcut e = i < list.size() ? list[i] : AppShortcut{};
        s.setValue(QStringLiteral("name"), e.name);
        s.setValue(QStringLiteral("path"), e.path);
        s.setValue(QStringLiteral("args"), e.args);
        s.setValue(QStringLiteral("autoStart"), e.autoStart);
    }
    s.endArray();
}

QString AppShortcuts::displayName(const AppShortcut &e) {
    return displayNameOf(e);
}

bool AppShortcuts::launch(const AppShortcut &e, QString *statusOut) {
    const QString path = e.path.trimmed();
    if (path.isEmpty()) {
        if (statusOut)
            *statusOut = QObject::tr("No program path set.");
        return false;
    }
    const QString label = displayNameOf(e);
    if (profile::CompanionLauncher::isRunning(path)) {
        if (statusOut)
            *statusOut = QObject::tr("%1 is already running.").arg(label);
        return false;
    }
    if (!QFileInfo::exists(path)) {
        if (statusOut)
            *statusOut = QObject::tr("Can't find %1.").arg(path);
        return false;
    }
    if (!profile::CompanionLauncher::launchDetached(path, e.args)) {
        if (statusOut)
            *statusOut = QObject::tr("Couldn't start %1.").arg(label);
        return false;
    }
    if (statusOut)
        *statusOut = QObject::tr("Started %1.").arg(label);
    return true;
}

}  // namespace lyra

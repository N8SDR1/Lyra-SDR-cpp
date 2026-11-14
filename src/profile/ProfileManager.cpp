// Lyra — ProfileManager.  See ProfileManager.h.

#include "profile/ProfileManager.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcProfile, "lyra.profile")

namespace lyra::profile {

ProfileManager::ProfileManager(ProfileBindings bindings, ProfileStore store,
                               QObject *parent)
    : QObject(parent), b_(std::move(bindings)), store_(std::move(store)) {
    // Seed the dirty baseline from whatever profile is marked active.
    setBaselineFromActive();
}

void ProfileManager::setBaselineFromActive() {
    const QString a = store_.active();
    if (!a.isEmpty() && store_.contains(a)) {
        baseline_ = store_.get(a);
        hasBaseline_ = true;
    } else {
        hasBaseline_ = false;
    }
    lastModified_ = isModified();
}

bool ProfileManager::isModified() const {
    if (!hasBaseline_ || !b_.capture) return false;
    return !b_.capture().sameValues(baseline_);
}

void ProfileManager::saveActive() {
    const QString a = store_.active();
    if (a.isEmpty()) {
        qCWarning(lcProfile) << "saveActive with no active profile — use saveAs";
        return;
    }
    saveAs(a);
}

void ProfileManager::saveAs(const QString &name) {
    if (name.isEmpty() || !b_.capture) return;
    const bool isNew = !store_.contains(name);
    Profile p = b_.capture();
    p.name = name;
    store_.put(p);
    store_.setActive(name);
    baseline_ = p;
    hasBaseline_ = true;
    if (isNew) emit namesChanged();
    emit activeChanged(name);
    emit modifiedChanged(false);
    lastModified_ = false;
}

bool ProfileManager::load(const QString &name) {
    if (!store_.contains(name) || !b_.apply) return false;
    // Mid-TX guard: never switch source/BW while keyed (§15.25).
    if (b_.isTxActive && b_.isTxActive()) {
        qCWarning(lcProfile) << "load(" << name << ") refused — TX active";
        return false;
    }
    Profile p = store_.get(name);
    applying_ = true;            // setter-storm below must not churn the dirty flag
    b_.apply(p);
    applying_ = false;
    store_.setActive(name);
    baseline_ = p;
    hasBaseline_ = true;
    emit activeChanged(name);
    emit modifiedChanged(false);
    lastModified_ = false;
    return true;
}

bool ProfileManager::loadByUser(const QString &name) {
    const bool ok = load(name);
    if (ok) emit userLoaded(name);   // explicit pick → companion app may launch
    return ok;
}

void ProfileManager::remove(const QString &name) {
    if (!store_.contains(name)) return;
    const bool wasActive = (store_.active() == name);
    store_.remove(name);
    if (wasActive) setBaselineFromActive();   // active cleared by store_.remove
    emit namesChanged();
    if (wasActive) { emit activeChanged(store_.active()); emit modifiedChanged(isModified()); }
}

void ProfileManager::rename(const QString &oldName, const QString &newName) {
    if (oldName == newName || newName.isEmpty() || !store_.contains(oldName)
        || store_.contains(newName)) return;
    store_.rename(oldName, newName);
    if (hasBaseline_ && baseline_.name == oldName) baseline_.name = newName;
    emit namesChanged();
    if (store_.active() == newName) emit activeChanged(newName);
}

void ProfileManager::setDefault(const QString &name) {
    if (store_.contains(name)) store_.setDefault(name);
}

void ProfileManager::applyDefaultAtStartup(const QString &restoredMode) {
    auto tryLoad = [this](const QString &name) -> bool {
        return !name.isEmpty() && store_.contains(name) && load(name);
    };
    if (!restoredMode.isEmpty()) {
        const QString bound = store_.modeBinding(modeFamily(restoredMode));
        if (tryLoad(bound)) return;
    }
    if (tryLoad(store_.active())) return;
    tryLoad(store_.defaultName());
}

void ProfileManager::bindMode(const QString &mode, const QString &name) {
    if (mode.isEmpty() || name.isEmpty()) return;
    store_.setModeBinding(mode, name);
}

void ProfileManager::unbindMode(const QString &mode) {
    store_.clearModeBinding(mode);
}

QString ProfileManager::modeFamily(const QString &mode) {
    const QString m = mode.toUpper();
    if (m == QLatin1String("USB") || m == QLatin1String("LSB"))
        return QStringLiteral("SSB");
    if (m == QLatin1String("CWU") || m == QLatin1String("CWL"))
        return QStringLiteral("CW");
    if (m == QLatin1String("DIGU") || m == QLatin1String("DIGL"))
        return QStringLiteral("Digital");
    return mode;   // AM / SAM / DSB / FM (and any unknown) stand alone
}

QStringList ProfileManager::modeFamilies() {
    return {QStringLiteral("CW"),  QStringLiteral("SSB"),
            QStringLiteral("Digital"), QStringLiteral("AM"),
            QStringLiteral("SAM"), QStringLiteral("DSB"),
            QStringLiteral("FM")};
}

void ProfileManager::onModeChanged(const QString &mode) {
    // Recall by FAMILY: USB and LSB both pull the "SSB" binding, etc.
    // The recalled profile carries no mode field, so the operator stays
    // on the exact sideband they switched to — the chain is what changes.
    const QString bound = store_.modeBinding(modeFamily(mode));
    if (bound.isEmpty() || bound == store_.active() || !store_.contains(bound))
        return;
    // load() applies the mid-TX guard itself.
    load(bound);
}

void ProfileManager::updateStoredProfile(const Profile &p) {
    if (p.name.isEmpty() || !store_.contains(p.name)) return;
    store_.put(p);   // overwrite the record; order/active/default untouched
    // Editing the ACTIVE profile's saved record moves the baseline with it;
    // live state is unchanged, so re-evaluate "● modified" (live vs new
    // baseline).  Non-active edits don't touch the baseline -> no re-eval.
    // Deliberately NO namesChanged emit: the name/order didn't change, and
    // a rebuild would bounce the Settings list selection off the row the
    // operator is editing.
    if (p.name == store_.active()) {
        baseline_ = p;
        hasBaseline_ = true;
        refreshModified();
    }
}

void ProfileManager::refreshModified() {
    if (applying_) return;       // mid-apply: capture is transient, ignore
    const bool m = isModified();
    if (m != lastModified_) {
        lastModified_ = m;
        emit modifiedChanged(m);
    }
}

}  // namespace lyra::profile

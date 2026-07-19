// Lyra — RadioProfileStore: persistence for Layer-2 radio profiles.
//
// One JSON document per radio at
//   <AppData>/profiles/radios/<mac-with-dashes>.json
// (survives uninstall/reinstall like the backup snapshots; human-
// readable; schemaVersion field for forward migration).  No index
// file — the MAC-derived filename IS the index.
//
// Threading: main-thread only (discovery signals + Settings UI +
// P2RxBridge::open all run there).

#pragma once

#include "radioprofile/RadioProfile.h"

#include <QList>
#include <QString>

namespace lyra::radioprofile {

class RadioProfileStore {
public:
    static RadioProfileStore &instance();

    // Find-or-create by MAC.  On first sight seeds hardwareModelKey
    // from the HardwareCatalog protocol default and stamps firstSeen.
    // Always refreshes lastKnownIp (when non-empty), protocol and
    // lastSeen, and persists.  Returns the up-to-date profile.
    RadioProfile touch(const QString &mac, int protocol,
                       const QString &ip);

    // Load by MAC; default-constructed (isValid()==false) if absent.
    RadioProfile forMac(const QString &mac) const;

    void save(const RadioProfile &p);
    // Delete a saved profile (Settings → Radio → Remove).
    void remove(const QString &mac);
    QList<RadioProfile> all() const;
    QString dir() const;

private:
    RadioProfileStore() = default;
    QString pathForMac(const QString &mac) const;
};

} // namespace lyra::radioprofile

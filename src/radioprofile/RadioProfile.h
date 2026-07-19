// Lyra — RadioProfile: Layer 2 of the profile stack (one saved
// PHYSICAL radio).  See docs/P2_HARDWARE_PROFILES_PLAN.md.
//
// Keyed by MAC address — the stable identity.  A DHCP address change
// updates lastKnownIp on the SAME profile; it never forks a new one.
// The hardware model is a HardwareCatalog key (Layer 1), seeded from
// the discovery board default and overridable by the operator.
//
// HL2 retention: an HL2's profile is METADATA ONLY (identity + model
// key "HERMES-LITE") — the P1 path never reads profiles; its
// QSettings-based configuration is unchanged (plan doc §retention).

#pragma once

#include <QJsonObject>
#include <QString>

namespace lyra::radioprofile {

struct RadioProfile {
    int     schemaVersion = 1;
    QString mac;               // "AA:BB:CC:DD:EE:FF" — stable key
    QString nickname;          // operator label; defaults to model name
    QString lastKnownIp;       // refreshed on every discovery reply
    int     protocol = 1;      // wire protocol seen at discovery (1|2)
    QString hardwareModelKey;  // HardwareCatalog key ("ANAN-G2", …)
    int     trxAntenna = 1;    // P2 TRX antenna port (1..3)
    // Per-radio audio routing: "" = follow the global Settings→Audio
    // choice, "hl2" = HL2 jack, "pc" = PC output.  Applied
    // TRANSIENTLY for the session (the global audio/output keys —
    // the HL2's home config — are never rewritten; retention rules).
    // P2 profiles are seeded "pc": their audio can't ride the P1 EP2
    // wire, and the radio-side speaker stream is later work.
    QString audioRoute;
    QString firstSeen;         // ISO-8601
    QString lastSeen;

    bool isValid() const { return !mac.isEmpty(); }

    QJsonObject toJson() const;
    static RadioProfile fromJson(const QJsonObject &o);
};

} // namespace lyra::radioprofile

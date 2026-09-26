// Lyra — Profile JSON (de)serialize.  See Profile.h.

#include "profile/Profile.h"

namespace lyra::profile {

QJsonObject Profile::toJson() const {
    QJsonObject o;
    o["schemaVersion"] = schemaVersion;
    o["rxBandwidth"]   = rxBandwidth;
    o["txBandwidth"]   = txBandwidth;
    o["bwLocked"]      = bwLocked;
    o["filterLow"]     = filterLow;
    o["micSource"]     = micSource;
    o["micGainDb"]     = micGainDb;
    o["micBoost"]      = micBoost;
    o["tuneDriveMode"]    = tuneDriveMode;
    o["fixedTuneDrivePct"] = fixedTuneDrivePct;
    o["tuneDrivePct"]  = tuneDrivePct;
    o["txDriveLevel"]  = txDriveLevel;
    o["tciRxGainDb"]   = tciRxGainDb;
    o["tciTxGainDb"]   = tciTxGainDb;
    o["vac1Enabled"]     = vac1Enabled;
    o["vac1AutoDigital"] = vac1AutoDigital;
    o["vac1RxGainDb"]    = vac1RxGainDb;
    o["vac1TxGainDb"]    = vac1TxGainDb;
    o["vac1LatencyMs"]   = vac1LatencyMs;   // v5 #158
    o["vac1VacSize"]     = vac1VacSize;      // v5 #158
    o["vac2Enabled"]     = vac2Enabled;      // v6 #103
    o["vac2AutoDigital"] = vac2AutoDigital;
    o["vac2RxGainDb"]    = vac2RxGainDb;
    o["vac2TxGainDb"]    = vac2TxGainDb;
    o["vac2LatencyMs"]   = vac2LatencyMs;
    o["vac2VacSize"]     = vac2VacSize;
    o["agcMode"]       = agcMode;
    o["autoMuteOnTx"]  = autoMuteOnTx;
    o["alcMaxGainLinear"]     = alcMaxGainLinear;
    o["levelerOn"]            = levelerOn;
    o["levelerMaxGainLinear"] = levelerMaxGainLinear;
    o["levelerDecayMs"]       = levelerDecayMs;
    o["phrotEnabled"]  = phrotEnabled;       // v4 #109
    o["fmDeviationHz"] = fmDeviationHz;       // v4 #107
    o["ctcssEnabled"]  = ctcssEnabled;        // v4 #107
    o["ctcssToneHz"]   = ctcssToneHz;         // v4 #107
    o["amCarrierPct"]  = amCarrierPct;        // v4 #93
    o["txTimeoutSec"]  = txTimeoutSec;
    o["txTimeoutBypass"] = txTimeoutBypass;
    // Native rack blobs (#49 v3) — omit empties to keep pre-rack profiles
    // byte-clean on disk.
    if (!eq.isEmpty())         o["eq"]         = eq;
    if (!speech.isEmpty())     o["speech"]     = speech;
    if (!combinator.isEmpty()) o["combinator"] = combinator;
    if (!plate.isEmpty())      o["plate"]      = plate;
    return o;
}

Profile Profile::fromJson(const QString &name, const QJsonObject &o) {
    Profile p;
    p.name = name;
    // Tolerant: a missing key keeps the struct default (forward/backward
    // compatible across schemaVersion bumps).
    if (o.contains("schemaVersion")) p.schemaVersion = o["schemaVersion"].toInt(p.schemaVersion);
    if (o.contains("rxBandwidth"))   p.rxBandwidth   = o["rxBandwidth"].toInt(p.rxBandwidth);
    if (o.contains("txBandwidth"))   p.txBandwidth   = o["txBandwidth"].toInt(p.txBandwidth);
    if (o.contains("bwLocked"))      p.bwLocked      = o["bwLocked"].toBool(p.bwLocked);
    if (o.contains("filterLow"))     p.filterLow     = o["filterLow"].toInt(p.filterLow);
    if (o.contains("micSource"))     p.micSource     = o["micSource"].toString(p.micSource);
    if (o.contains("micGainDb"))     p.micGainDb     = o["micGainDb"].toDouble(p.micGainDb);
    if (o.contains("micBoost"))      p.micBoost      = o["micBoost"].toBool(p.micBoost);
    if (o.contains("tuneDriveMode")) p.tuneDriveMode = o["tuneDriveMode"].toInt(p.tuneDriveMode);
    else if (o.contains("useTuneDrive"))   // legacy (#74): true → tune mode
        p.tuneDriveMode = o["useTuneDrive"].toBool() ? 1 : 0;
    if (o.contains("fixedTuneDrivePct")) p.fixedTuneDrivePct = o["fixedTuneDrivePct"].toInt(p.fixedTuneDrivePct);
    if (o.contains("tuneDrivePct"))  p.tuneDrivePct  = o["tuneDrivePct"].toInt(p.tuneDrivePct);
    if (o.contains("txDriveLevel"))  p.txDriveLevel  = o["txDriveLevel"].toInt(p.txDriveLevel);
    if (o.contains("tciRxGainDb"))   p.tciRxGainDb   = o["tciRxGainDb"].toDouble(p.tciRxGainDb);
    if (o.contains("tciTxGainDb"))   p.tciTxGainDb   = o["tciTxGainDb"].toDouble(p.tciTxGainDb);
    if (o.contains("vac1Enabled"))     p.vac1Enabled     = o["vac1Enabled"].toBool(p.vac1Enabled);
    if (o.contains("vac1AutoDigital")) p.vac1AutoDigital = o["vac1AutoDigital"].toBool(p.vac1AutoDigital);
    if (o.contains("vac1RxGainDb"))    p.vac1RxGainDb    = o["vac1RxGainDb"].toDouble(p.vac1RxGainDb);
    if (o.contains("vac1TxGainDb"))    p.vac1TxGainDb    = o["vac1TxGainDb"].toDouble(p.vac1TxGainDb);
    if (o.contains("vac1LatencyMs"))   p.vac1LatencyMs   = o["vac1LatencyMs"].toInt(p.vac1LatencyMs);
    if (o.contains("vac1VacSize"))     p.vac1VacSize     = o["vac1VacSize"].toInt(p.vac1VacSize);
    if (o.contains("vac2Enabled"))     p.vac2Enabled     = o["vac2Enabled"].toBool(p.vac2Enabled);
    if (o.contains("vac2AutoDigital")) p.vac2AutoDigital = o["vac2AutoDigital"].toBool(p.vac2AutoDigital);
    if (o.contains("vac2RxGainDb"))    p.vac2RxGainDb    = o["vac2RxGainDb"].toDouble(p.vac2RxGainDb);
    if (o.contains("vac2TxGainDb"))    p.vac2TxGainDb    = o["vac2TxGainDb"].toDouble(p.vac2TxGainDb);
    if (o.contains("vac2LatencyMs"))   p.vac2LatencyMs   = o["vac2LatencyMs"].toInt(p.vac2LatencyMs);
    if (o.contains("vac2VacSize"))     p.vac2VacSize     = o["vac2VacSize"].toInt(p.vac2VacSize);
    if (o.contains("agcMode"))       p.agcMode       = o["agcMode"].toString(p.agcMode);
    if (o.contains("autoMuteOnTx"))  p.autoMuteOnTx  = o["autoMuteOnTx"].toBool(p.autoMuteOnTx);
    if (o.contains("alcMaxGainLinear"))     p.alcMaxGainLinear     = o["alcMaxGainLinear"].toDouble(p.alcMaxGainLinear);
    if (o.contains("levelerOn"))            p.levelerOn            = o["levelerOn"].toBool(p.levelerOn);
    if (o.contains("levelerMaxGainLinear")) p.levelerMaxGainLinear = o["levelerMaxGainLinear"].toDouble(p.levelerMaxGainLinear);
    if (o.contains("levelerDecayMs"))       p.levelerDecayMs       = o["levelerDecayMs"].toInt(p.levelerDecayMs);
    if (o.contains("phrotEnabled"))  p.phrotEnabled  = o["phrotEnabled"].toBool(p.phrotEnabled);
    if (o.contains("fmDeviationHz")) p.fmDeviationHz = o["fmDeviationHz"].toDouble(p.fmDeviationHz);
    if (o.contains("ctcssEnabled"))  p.ctcssEnabled  = o["ctcssEnabled"].toBool(p.ctcssEnabled);
    if (o.contains("ctcssToneHz"))   p.ctcssToneHz   = o["ctcssToneHz"].toDouble(p.ctcssToneHz);
    if (o.contains("amCarrierPct"))  p.amCarrierPct  = o["amCarrierPct"].toDouble(p.amCarrierPct);
    if (o.contains("txTimeoutSec"))  p.txTimeoutSec  = o["txTimeoutSec"].toInt(p.txTimeoutSec);
    if (o.contains("txTimeoutBypass")) p.txTimeoutBypass = o["txTimeoutBypass"].toBool(p.txTimeoutBypass);
    if (o.contains("eq"))         p.eq         = o["eq"].toObject();
    if (o.contains("speech"))     p.speech     = o["speech"].toObject();
    if (o.contains("combinator")) p.combinator = o["combinator"].toObject();
    if (o.contains("plate"))      p.plate      = o["plate"].toObject();
    return p;
}

bool Profile::sameValues(const Profile &b) const {
    auto dEq = [](double x, double y) {
        // operator-facing dB/levels; 1e-6 is far below any audible step.
        return (x - y) < 1e-6 && (y - x) < 1e-6;
    };
    return rxBandwidth == b.rxBandwidth
        && txBandwidth == b.txBandwidth
        && bwLocked == b.bwLocked
        && filterLow == b.filterLow
        && micSource == b.micSource
        && dEq(micGainDb, b.micGainDb)
        && micBoost == b.micBoost
        && tuneDriveMode == b.tuneDriveMode
        && fixedTuneDrivePct == b.fixedTuneDrivePct
        // tuneDrivePct / txDriveLevel deliberately excluded — per-band
        // (BandMemory), not profile fields (see Profile.h).
        && dEq(tciRxGainDb, b.tciRxGainDb)
        && dEq(tciTxGainDb, b.tciTxGainDb)
        && vac1Enabled == b.vac1Enabled
        && vac1AutoDigital == b.vac1AutoDigital
        && dEq(vac1RxGainDb, b.vac1RxGainDb)
        && dEq(vac1TxGainDb, b.vac1TxGainDb)
        && vac1LatencyMs == b.vac1LatencyMs
        && vac1VacSize == b.vac1VacSize
        && vac2Enabled == b.vac2Enabled
        && vac2AutoDigital == b.vac2AutoDigital
        && dEq(vac2RxGainDb, b.vac2RxGainDb)
        && dEq(vac2TxGainDb, b.vac2TxGainDb)
        && vac2LatencyMs == b.vac2LatencyMs
        && vac2VacSize == b.vac2VacSize
        && agcMode == b.agcMode
        && autoMuteOnTx == b.autoMuteOnTx
        && dEq(alcMaxGainLinear, b.alcMaxGainLinear)
        && levelerOn == b.levelerOn
        && dEq(levelerMaxGainLinear, b.levelerMaxGainLinear)
        && levelerDecayMs == b.levelerDecayMs
        && phrotEnabled == b.phrotEnabled
        && dEq(fmDeviationHz, b.fmDeviationHz)
        && ctcssEnabled == b.ctcssEnabled
        && dEq(ctcssToneHz, b.ctcssToneHz)
        && dEq(amCarrierPct, b.amCarrierPct)
        && txTimeoutSec == b.txTimeoutSec
        && txTimeoutBypass == b.txTimeoutBypass
        // Native rack blobs — QJsonObject has value equality, so any EQ /
        // Speech / Combinator / Plate change flags the profile ● modified
        // (operator-confirmed 2026-06-16: dirty-tracking includes the rack).
        && eq == b.eq
        && speech == b.speech
        && combinator == b.combinator
        && plate == b.plate;
}

}  // namespace lyra::profile

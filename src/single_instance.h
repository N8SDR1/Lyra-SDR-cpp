// Lyra — single-instance guard (header-only; included by src/main.cpp).
//
// Why this exists: Lyra had no single-instance guard, so a second launch
// would happily start a colliding copy — the two fight over the HL2 socket,
// the audio device, and (the operator-visible symptom) the TCI listen port
// 40001, which the first instance already owns → "port in use".  This guard
// makes a second launch RAISE the already-running window and exit, instead
// of starting a second radio.
//
// Forward-compatible with deliberate two-instance operation (two radios, two
// configs — a planned feature): the lock is keyed on an `instanceId`
// (default "default").  Two future config profiles would each carry a
// distinct id, so each gets exactly one instance, while the guard still
// blocks an *accidental* duplicate of the same profile.  A `--new-instance`
// command-line flag bypasses the guard entirely as an explicit escape hatch
// (deliberate second copy for testing) — the ONLY caveat being that today a
// bypassed second copy still shares the default TCI port / radio config, so
// it is a power-user hatch, not the finished two-radio path.
//
// Mechanism (the canonical Qt pattern): a QSharedMemory segment is the
// cross-process lock; a QLocalServer named the same key is the "raise me"
// channel.  On Windows the shared-memory segment is refcounted by the OS and
// released even on a hard crash, so no stale lock survives a crashed primary.
#pragma once

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QSharedMemory>
#include <QString>

#include <functional>

namespace lyra::ui {

// Key namespace: distinct from any other app's, and NOT sharing the QSettings
// scope name, so it can never collide with an unrelated Qt app's guard.
inline QString singleInstanceKey(const QString &instanceId) {
    return QStringLiteral("LyraSDR-cpp-single-instance-") + instanceId;
}

// Returns true  → THIS process is the primary; caller CONTINUES startup.
// Returns false → another Lyra already holds this instanceId; this call has
//                 already pinged it to raise its window and the caller MUST
//                 exit immediately (`return 0` from main), touching nothing
//                 else (no settings, no radio, no ports).
//
// On the primary path the QSharedMemory lock is intentionally leaked — it
// holds the lock for the process lifetime and the OS frees it at exit.
// `serverNameOut` receives the QLocalServer name the primary should listen
// on (see startSingleInstanceServer); it is cleared when the guard is
// bypassed (--new-instance) so the caller starts no server.
inline bool acquireSingleInstance(int argc, char **argv,
                                  const QString &instanceId,
                                  QString *serverNameOut) {
    // Escape hatch: an explicit second copy (deliberate, e.g. testing).
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--new-instance")) {
            if (serverNameOut) serverNameOut->clear();
            return true;
        }
    }

    const QString key = singleInstanceKey(instanceId);
    if (serverNameOut) *serverNameOut = key;

    // create() succeeds for exactly the first process; a second process's
    // create() fails with AlreadyExists → it is the duplicate.  Any OTHER
    // create() failure (permissions, kernel object limit, …) must NOT lock
    // the operator out — fall through and run as primary in that case.
    auto *lock = new QSharedMemory(key);
    if (lock->create(1)) {
        return true;   // primary — leak `lock` to hold the guard for our life
    }
    if (lock->error() != QSharedMemory::AlreadyExists) {
        return true;   // not a real duplicate — start anyway (never lock out)
    }
    delete lock;

    // Duplicate launch: ping the primary to bring its window forward, then
    // tell the caller to exit.  A minimal, short-lived QCoreApplication gives
    // QLocalSocket its event dispatcher; we exit before any QApplication is
    // built, so there is never a second QCoreApplication in the real run.
    QCoreApplication ping(argc, argv);
    QLocalSocket sock;
    sock.connectToServer(key);
    if (sock.waitForConnected(400)) {
        sock.write("raise");
        sock.flush();
        sock.waitForBytesWritten(400);
        sock.disconnectFromServer();
    }
    return false;
}

// Primary-side: listen for future launches' "raise" pings and run `onRaise`
// (bring the window to the front) for each.  Call once, after the main window
// exists.  A stale server name from a crashed primary is cleared first.
inline QLocalServer *startSingleInstanceServer(const QString &serverName,
                                               QObject *parent,
                                               std::function<void()> onRaise) {
    if (serverName.isEmpty()) return nullptr;   // guard was bypassed
    QLocalServer::removeServer(serverName);     // clear any crash-stale pipe
    auto *server = new QLocalServer(parent);
    QObject::connect(server, &QLocalServer::newConnection, server,
                     [server, onRaise]() {
        while (QLocalSocket *c = server->nextPendingConnection()) {
            // Raise immediately on connect (don't depend on the payload
            // arriving) and again on readyRead — cheap + robust.
            if (onRaise) onRaise();
            QObject::connect(c, &QLocalSocket::readyRead, c, [c, onRaise]() {
                c->readAll();
                if (onRaise) onRaise();
            });
            QObject::connect(c, &QLocalSocket::disconnected,
                             c, &QLocalSocket::deleteLater);
        }
    });
    server->listen(serverName);
    return server;
}

}  // namespace lyra::ui

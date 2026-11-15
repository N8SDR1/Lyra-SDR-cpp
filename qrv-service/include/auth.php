<?php
declare(strict_types=1);

function qrv_hash_key(string $key): string
{
    return hash('sha256', $key);
}

function qrv_new_install_key(): string
{
    return bin2hex(random_bytes(32));
}

function qrv_is_banned(PDO $pdo, string $ip, ?string $call, ?string $keyPlain): bool
{
    $checks = [['ip', $ip]];
    if ($call !== null && $call !== '') {
        $checks[] = ['call', $call];
    }
    if ($keyPlain !== null && $keyPlain !== '') {
        $checks[] = ['key', qrv_hash_key($keyPlain)];
        $checks[] = ['key', $keyPlain];
    }
    $sql = 'SELECT 1 FROM bans WHERE kind = ? AND value = ? LIMIT 1';
    $st = $pdo->prepare($sql);
    foreach ($checks as [$kind, $value]) {
        $st->execute([$kind, $value]);
        if ($st->fetchColumn()) {
            return true;
        }
    }
    return false;
}

function qrv_rate_limit(PDO $pdo, string $bucket, int $minIntervalS): bool
{
    $now = time();
    $st = $pdo->prepare('SELECT last_unix FROM rate_limit WHERE bucket = ?');
    $st->execute([$bucket]);
    $row = $st->fetch();
    if ($row && ($now - (int) $row['last_unix']) < $minIntervalS) {
        return false;
    }
    $up = $pdo->prepare(
        'INSERT INTO rate_limit (bucket, last_unix) VALUES (?, ?)
         ON DUPLICATE KEY UPDATE last_unix = VALUES(last_unix)'
    );
    $up->execute([$bucket, $now]);
    return true;
}

function qrv_lookup_key(PDO $pdo, string $plain): ?array
{
    $st = $pdo->prepare('SELECT id, callsign, key_hash FROM install_keys WHERE key_hash = ? LIMIT 1');
    $st->execute([qrv_hash_key($plain)]);
    $row = $st->fetch();
    return $row ?: null;
}

function qrv_touch_key(PDO $pdo, int $id): void
{
    $st = $pdo->prepare('UPDATE install_keys SET last_used_at = UTC_TIMESTAMP() WHERE id = ?');
    $st->execute([$id]);
}

function qrv_issue_key(PDO $pdo, string $callsign): array
{
    $plain = qrv_new_install_key();
    $st = $pdo->prepare(
        'INSERT INTO install_keys (key_hash, callsign, created_at, last_used_at)
         VALUES (?, ?, UTC_TIMESTAMP(), UTC_TIMESTAMP())'
    );
    $st->execute([qrv_hash_key($plain), $callsign]);
    return ['id' => (int) $pdo->lastInsertId(), 'install_key' => $plain];
}

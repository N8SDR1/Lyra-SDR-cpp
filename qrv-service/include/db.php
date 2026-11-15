<?php
declare(strict_types=1);

function qrv_pdo(array $config): PDO
{
    $db = $config['db'] ?? [];
    $name = (string) ($db['name'] ?? '');
    $user = (string) ($db['user'] ?? '');
    $pass = (string) ($db['pass'] ?? '');
    $hosts = $db['hosts'] ?? [];
    if ($name === '' || $user === '' || !is_array($hosts) || $hosts === []) {
        qrv_fail(500, 'db_not_configured');
    }

    $last = null;
    foreach ($hosts as $h) {
        $host = (string) ($h['host'] ?? '');
        $port = (int) ($h['port'] ?? 3306);
        if ($host === '') {
            continue;
        }
        $dsn = sprintf('mysql:host=%s;port=%d;dbname=%s;charset=utf8mb4', $host, $port, $name);
        try {
            $pdo = new PDO($dsn, $user, $pass, [
                PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
                PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
                PDO::ATTR_EMULATE_PREPARES => false,
                PDO::ATTR_TIMEOUT => 5,
            ]);
            $pdo->exec("SET time_zone = '+00:00'");
            return $pdo;
        } catch (PDOException $e) {
            $last = $e;
        }
    }

    error_log('qrv db connect failed: ' . ($last ? $last->getMessage() : 'no hosts'));
    qrv_fail(503, 'db_unavailable');
}

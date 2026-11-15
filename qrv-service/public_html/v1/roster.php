<?php
declare(strict_types=1);

require dirname(__DIR__, 2) . '/include/bootstrap.php';

qrv_require_method('GET');

/** @var array $QRV_CONFIG */
$pdo = qrv_pdo($QRV_CONFIG);
$ip = qrv_client_ip();
$stale = qrv_stale_seconds($QRV_CONFIG);

if (qrv_is_banned($pdo, $ip, null, null)) {
    qrv_json_out(403, ['ok' => false, 'error' => 'forbidden']);
}

if (!qrv_rate_limit($pdo, 'get:' . $ip, 2)) {
    qrv_json_out(429, ['ok' => false, 'error' => 'rate_limited']);
}

$st = $pdo->prepare(
    'SELECT callsign, app, band, mode, freq_hz, note,
            DATE_FORMAT(last_seen, "%Y-%m-%dT%H:%i:%sZ") AS last_seen
     FROM roster
     WHERE last_seen >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL ? SECOND)
     ORDER BY callsign ASC, app ASC'
);
$st->execute([$stale]);
$rows = $st->fetchAll();

$entries = [];
foreach ($rows as $r) {
    $e = [
        'call' => $r['callsign'],
        'app' => $r['app'],
        'band' => $r['band'],
        'mode' => $r['mode'],
        'last_seen' => $r['last_seen'],
    ];
    if ($r['freq_hz'] !== null) {
        $e['hz'] = (int) $r['freq_hz'];
    }
    if ($r['note'] !== null && $r['note'] !== '') {
        $e['note'] = $r['note'];
    }
    $entries[] = $e;
}

qrv_json_out(200, [
    'ok' => true,
    'stale_s' => $stale,
    'entries' => $entries,
]);

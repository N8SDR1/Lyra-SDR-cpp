<?php
declare(strict_types=1);

require dirname(__DIR__, 2) . '/include/bootstrap.php';

qrv_require_method('POST');

/** @var array $QRV_CONFIG */
$pdo = qrv_pdo($QRV_CONFIG);
$body = qrv_json_body();
$ip = qrv_client_ip();
$minIv = (int) ($QRV_CONFIG['heartbeat_min_interval_s'] ?? 10);
if ($minIv < 1) {
    $minIv = 10;
}

$call = qrv_norm_call((string) ($body['call'] ?? ''));
$app = qrv_norm_app($body['app'] ?? null);
$band = qrv_norm_band($body['band'] ?? null);
$mode = qrv_norm_mode($body['mode'] ?? null);
$keyIn = isset($body['install_key']) && is_string($body['install_key'])
    ? trim($body['install_key']) : '';

if ($call === null || $app === null || $band === null || $mode === null) {
    qrv_json_out(400, ['ok' => false, 'error' => 'invalid_fields']);
}

$hz = qrv_norm_hz($body['hz'] ?? null, $band);
if ($hz === -1) {
    qrv_json_out(400, ['ok' => false, 'error' => 'invalid_hz']);
}
$note = qrv_norm_note($body['note'] ?? null);
if ($note === '') {
    $note = null;
}

if (qrv_is_banned($pdo, $ip, $call, $keyIn !== '' ? $keyIn : null)) {
    qrv_json_out(403, ['ok' => false, 'error' => 'forbidden']);
}

if (!qrv_rate_limit($pdo, 'ip:' . $ip, $minIv)) {
    qrv_json_out(429, ['ok' => false, 'error' => 'rate_limited']);
}

$issued = null;
if ($keyIn === '') {
    $issued = qrv_issue_key($pdo, $call);
    $keyId = $issued['id'];
} else {
    if (!qrv_rate_limit($pdo, 'key:' . qrv_hash_key($keyIn), $minIv)) {
        qrv_json_out(429, ['ok' => false, 'error' => 'rate_limited']);
    }
    $row = qrv_lookup_key($pdo, $keyIn);
    if ($row === null) {
        qrv_json_out(401, ['ok' => false, 'error' => 'bad_key']);
    }
    if (strcasecmp((string) $row['callsign'], $call) !== 0) {
        qrv_json_out(403, ['ok' => false, 'error' => 'call_mismatch']);
    }
    $keyId = (int) $row['id'];
    qrv_touch_key($pdo, $keyId);
}

$st = $pdo->prepare(
    'INSERT INTO roster (install_key_id, callsign, app, band, mode, freq_hz, note, last_seen)
     VALUES (?, ?, ?, ?, ?, ?, ?, UTC_TIMESTAMP())
     ON DUPLICATE KEY UPDATE
        callsign = VALUES(callsign),
        app = VALUES(app),
        band = VALUES(band),
        mode = VALUES(mode),
        freq_hz = VALUES(freq_hz),
        note = VALUES(note),
        last_seen = UTC_TIMESTAMP()'
);
$st->execute([$keyId, $call, $app, $band, $mode, $hz, $note]);

$out = ['ok' => true, 'stale_s' => qrv_stale_seconds($QRV_CONFIG)];
if ($issued !== null) {
    $out['install_key'] = $issued['install_key'];
}
qrv_json_out(200, $out);

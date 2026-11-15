<?php
declare(strict_types=1);

require dirname(__DIR__, 2) . '/include/bootstrap.php';

qrv_require_method('POST');

/** @var array $QRV_CONFIG */
$pdo = qrv_pdo($QRV_CONFIG);
$body = qrv_json_body();
$ip = qrv_client_ip();

$call = qrv_norm_call((string) ($body['call'] ?? ''));
$app = qrv_norm_app($body['app'] ?? null);
$keyIn = isset($body['install_key']) && is_string($body['install_key'])
    ? trim($body['install_key']) : '';

if ($call === null || $app === null || $keyIn === '') {
    qrv_json_out(400, ['ok' => false, 'error' => 'invalid_fields']);
}

if (qrv_is_banned($pdo, $ip, $call, $keyIn)) {
    qrv_json_out(403, ['ok' => false, 'error' => 'forbidden']);
}

$row = qrv_lookup_key($pdo, $keyIn);
if ($row === null) {
    qrv_json_out(401, ['ok' => false, 'error' => 'bad_key']);
}
if (strcasecmp((string) $row['callsign'], $call) !== 0) {
    qrv_json_out(403, ['ok' => false, 'error' => 'call_mismatch']);
}

$st = $pdo->prepare(
    'DELETE FROM roster WHERE install_key_id = ? AND callsign = ? AND app = ?'
);
$st->execute([(int) $row['id'], $call, $app]);

qrv_json_out(200, ['ok' => true]);

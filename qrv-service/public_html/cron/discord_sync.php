<?php
/**
 * Hostinger cron: php-cli this file every 5 minutes (or 1 if the plan allows).
 * Edits one Discord #qrv message. Strips Hz and note.
 */
declare(strict_types=1);

require dirname(__DIR__, 2) . '/include/bootstrap.php';
require dirname(__DIR__, 2) . '/include/discord.php';

/** @var array $QRV_CONFIG */

if (PHP_SAPI !== 'cli') {
    $secret = (string) ($QRV_CONFIG['cron_secret'] ?? '');
    $got = (string) ($_GET['key'] ?? '');
    if ($secret === '' || !hash_equals($secret, $got)) {
        qrv_json_out(403, ['ok' => false, 'error' => 'forbidden']);
    }
}

$pdo = qrv_pdo($QRV_CONFIG);
$stale = qrv_stale_seconds($QRV_CONFIG);

$pdo->prepare(
    'DELETE FROM roster WHERE last_seen < DATE_SUB(UTC_TIMESTAMP(), INTERVAL ? SECOND)'
)->execute([$stale]);

$st = $pdo->prepare(
    'SELECT callsign, app, band, mode
     FROM roster
     WHERE last_seen >= DATE_SUB(UTC_TIMESTAMP(), INTERVAL ? SECOND)
     ORDER BY callsign ASC, app ASC'
);
$st->execute([$stale]);
$rows = $st->fetchAll();

$content = qrv_format_discord_message($rows);
$hash = hash('sha256', $content);

$state = $pdo->query('SELECT message_id, last_hash FROM discord_state WHERE id = 1')->fetch();
if (!$state) {
    $pdo->exec('INSERT INTO discord_state (id) VALUES (1)');
    $state = ['message_id' => null, 'last_hash' => null];
}

$dc = $QRV_CONFIG['discord'] ?? [];
$token = (string) ($dc['bot_token'] ?? '');
$channel = (string) ($dc['channel_id'] ?? '');
if ($token === '' || $token === 'CHANGE_ME' || $channel === '' || $channel === 'CHANGE_ME') {
    qrv_fail(500, 'discord_not_configured');
}

$messageId = $state['message_id'] !== null && $state['message_id'] !== ''
    ? (string) $state['message_id'] : null;

if ($messageId !== null && hash_equals((string) ($state['last_hash'] ?? ''), $hash)) {
    $pdo->prepare('UPDATE discord_state SET last_synced_at = UTC_TIMESTAMP() WHERE id = 1')->execute();
    if (PHP_SAPI === 'cli') {
        echo "unchanged\n";
        exit(0);
    }
    qrv_json_out(200, ['ok' => true, 'unchanged' => true]);
}

$base = 'https://discord.com/api/v10/channels/' . rawurlencode($channel) . '/messages';
$payload = ['content' => $content];

if ($messageId !== null) {
    $res = qrv_discord_request($token, 'PATCH', $base . '/' . rawurlencode($messageId), $payload);
    if ($res['status'] === 404) {
        $messageId = null;
    } elseif ($res['status'] < 200 || $res['status'] >= 300) {
        error_log('qrv discord PATCH failed HTTP ' . $res['status']);
        qrv_fail(502, 'discord_patch_failed');
    }
}

if ($messageId === null) {
    $res = qrv_discord_request($token, 'POST', $base, $payload);
    if ($res['status'] < 200 || $res['status'] >= 300 || empty($res['body']['id'])) {
        error_log('qrv discord POST failed HTTP ' . $res['status']);
        qrv_fail(502, 'discord_post_failed');
    }
    $messageId = (string) $res['body']['id'];
}

$up = $pdo->prepare(
    'UPDATE discord_state SET message_id = ?, last_hash = ?, last_synced_at = UTC_TIMESTAMP() WHERE id = 1'
);
$up->execute([$messageId, $hash]);

if (PHP_SAPI === 'cli') {
    echo "synced " . count($rows) . "\n";
    exit(0);
}
qrv_json_out(200, ['ok' => true, 'count' => count($rows)]);

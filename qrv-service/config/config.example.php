<?php
/**
 * Copy this file to ../config/config.php on the Hostinger account
 * (domains/qrvsdr.cc/config/config.php). That folder is NOT web-reachable.
 *
 * Never commit a filled-in config.php. Never put the Discord token in
 * public_html or in the Lyra installer.
 */
declare(strict_types=1);

return [
    'db' => [
        // Hostinger MySQL: name/user look like u604700148_qrv — fill yours.
        'name' => 'uXXXXXXXX_qrv',
        'user' => 'uXXXXXXXX_qrv',
        'pass' => 'CHANGE_ME',
        // PHP on the same account: try localhost first. Remote hostname is
        // the documented fallback (phpMyAdmin / hPanel show this).
        'hosts' => [
            ['host' => 'localhost', 'port' => 3306],
            ['host' => 'srv1454.hstgr.io', 'port' => 3306],
        ],
    ],

    'stale_seconds' => 180,
    'heartbeat_min_interval_s' => 10,

    // Optional: if Hostinger cron is "wget this URL" instead of php-cli,
    // set a long random string and call
    // https://qrvsdr.cc/cron/discord_sync.php?key=THAT_STRING
    // Leave empty to refuse HTTP (CLI-only).
    'cron_secret' => '',

    'discord' => [
        'bot_token' => 'CHANGE_ME',
        'guild_id' => 'CHANGE_ME',
        'channel_id' => 'CHANGE_ME',
    ],
];

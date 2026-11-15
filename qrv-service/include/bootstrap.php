<?php
declare(strict_types=1);

header_remove('X-Powered-By');

$QRV_ROOT = dirname(__DIR__);
$cfgPath = $QRV_ROOT . '/config/config.php';
if (!is_readable($cfgPath)) {
    if (PHP_SAPI === 'cli') {
        fwrite(STDERR, "not_configured\n");
        exit(1);
    }
    http_response_code(500);
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode(['ok' => false, 'error' => 'not_configured']);
    exit;
}

$QRV_CONFIG = require $cfgPath;
if (!is_array($QRV_CONFIG)) {
    if (PHP_SAPI === 'cli') {
        fwrite(STDERR, "bad_config\n");
        exit(1);
    }
    http_response_code(500);
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode(['ok' => false, 'error' => 'bad_config']);
    exit;
}

require $QRV_ROOT . '/include/http.php';
require $QRV_ROOT . '/include/db.php';
require $QRV_ROOT . '/include/validate.php';
require $QRV_ROOT . '/include/auth.php';

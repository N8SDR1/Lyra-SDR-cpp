<?php
declare(strict_types=1);

function qrv_json_out(int $code, array $body): never
{
    http_response_code($code);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($body, JSON_UNESCAPED_SLASHES);
    exit;
}

function qrv_fail(int $code, string $error): never
{
    if (PHP_SAPI === 'cli') {
        fwrite(STDERR, $error . "\n");
        exit(1);
    }
    qrv_json_out($code, ['ok' => false, 'error' => $error]);
}

function qrv_require_method(string $method): void
{
    $got = strtoupper($_SERVER['REQUEST_METHOD'] ?? '');
    if ($got === 'OPTIONS') {
        http_response_code(204);
        header('Allow: ' . $method . ', OPTIONS');
        exit;
    }
    if ($got !== strtoupper($method)) {
        qrv_json_out(405, ['ok' => false, 'error' => 'method_not_allowed']);
    }
}

function qrv_json_body(): array
{
    $raw = file_get_contents('php://input');
    if ($raw === false || $raw === '') {
        qrv_json_out(400, ['ok' => false, 'error' => 'empty_body']);
    }
    $data = json_decode($raw, true);
    if (!is_array($data)) {
        qrv_json_out(400, ['ok' => false, 'error' => 'invalid_json']);
    }
    return $data;
}

function qrv_client_ip(): string
{
    $ip = $_SERVER['REMOTE_ADDR'] ?? '';
    if (!is_string($ip) || $ip === '') {
        return '0.0.0.0';
    }
    return substr($ip, 0, 45);
}

function qrv_stale_seconds(array $config): int
{
    $n = (int) ($config['stale_seconds'] ?? 180);
    return $n > 0 ? $n : 180;
}

<?php
declare(strict_types=1);

function qrv_discord_request(string $token, string $method, string $url, ?array $json): array
{
    $headers = [
        'Authorization: Bot ' . $token,
        'Content-Type: application/json',
        'User-Agent: QRV-service (https://qrvsdr.cc, 1.0)',
    ];
    $opts = [
        'http' => [
            'method' => $method,
            'header' => implode("\r\n", $headers),
            'ignore_errors' => true,
            'timeout' => 15,
        ],
        'ssl' => [
            'verify_peer' => true,
            'verify_peer_name' => true,
        ],
    ];
    if ($json !== null) {
        $opts['http']['content'] = json_encode($json, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
    }
    $ctx = stream_context_create($opts);
    $raw = @file_get_contents($url, false, $ctx);
    $status = 0;
    if (isset($http_response_header[0]) && preg_match('/\s(\d{3})\s/', $http_response_header[0], $m)) {
        $status = (int) $m[1];
    }
    $decoded = [];
    if (is_string($raw) && $raw !== '') {
        $tmp = json_decode($raw, true);
        if (is_array($tmp)) {
            $decoded = $tmp;
        }
    }
    return ['status' => $status, 'body' => $decoded, 'raw' => is_string($raw) ? $raw : ''];
}

/**
 * One Discord message, 2000-char cap. Never include Hz or note.
 */
function qrv_format_discord_message(array $rows): string
{
    $cap = 2000;
    $stamp = gmdate('Y-m-d H:i') . ' UTC';
    $header = '**QRV** — ' . count($rows) . ' on the board (' . $stamp . ")\n";
    if ($rows === []) {
        return $header . '_Nobody QRV._';
    }

    $lines = [];
    foreach ($rows as $r) {
        $call = (string) $r['callsign'];
        $band = (string) $r['band'];
        // Mode is already ≤12 chars from validate.php; do not pad/truncate here.
        $mode = (string) $r['mode'];
        $app = (string) $r['app'];
        $lines[] = $call . '  ' . $band . '  ' . $mode . '  ' . $app;
    }

    $body = $header;
    $used = 0;
    $n = count($lines);
    for ($i = 0; $i < $n; $i++) {
        $remain = $n - $i;
        $more = $remain > 1 ? ("\n+" . ($remain - 1) . ' more') : '';
        $candidate = $body . $lines[$i] . ($i < $n - 1 ? "\n" : '');
        if (strlen($candidate . $more) > $cap) {
            $suffix = "\n+" . $remain . ' more';
            if (strlen($body . $suffix) <= $cap) {
                return $body . $suffix;
            }
            return substr($body, 0, $cap);
        }
        $body = $candidate;
        $used++;
    }
    return $body;
}

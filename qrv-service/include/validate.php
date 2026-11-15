<?php
declare(strict_types=1);

const QRV_APPS = ['Lyra', 'Logger', 'STD'];

/**
 * One allow-list for Lyra + STD + Logger. Match after qrv_norm_mode().
 * Contract: docs/architecture/qrv_client.md
 */
const QRV_MODES = [
    // Lyra radio modes (keep; do not remove)
    'LSB', 'USB', 'CWL', 'CWU', 'DSB', 'AM', 'SAM', 'FM', 'DIGU', 'DIGL', 'SPEC', 'DRM',
    // Logger log/dropdown extras (hamlog templates/index.html + TCI maps)
    'SSB', 'NFM', 'PSK31', 'JT65', 'JT9', 'DIGI', 'DATA', 'VARAC', 'PACKET', 'RTTYR',
    // STD digital menu (and overlapping Logger digital names)
    'FT8', 'FT4', 'FT2',
    'JT65A', 'JT65B', 'JT65C',
    'Q65', 'Q65C', 'Q65D', 'Q65E',
    'MSK144', 'MSKMS',
    'JS8', 'JS8CX2',
    'WSPR', 'FST4', 'FST4W',
    'HELL', 'RTTY', 'PSK', 'QPSK',
    'OLIVIA', 'CONTESTIA', 'MFSK', 'DOMINOEX', 'THOR', 'MT63',
    'VARA', 'VARAHF',
];

/** After strip of non-A-Z0-9, map collapsed labels to the stored token. */
const QRV_MODE_ALIASES = [
    'VARAHFSTD' => 'VARAHF',
    'JS8CX2EXPERIMENTS' => 'JS8CX2',
    'CW' => 'CWU',
    'DIG' => 'DIGI',
];

/** Amateur (+ a few extra) band names and Hz ranges for optional exact-Hz check. */
const QRV_BANDS = [
    '160m' => [1_800_000, 2_000_000],
    '80m'  => [3_500_000, 4_000_000],
    '60m'  => [5_330_000, 5_407_000],
    '40m'  => [7_000_000, 7_300_000],
    '30m'  => [10_100_000, 10_150_000],
    '20m'  => [14_000_000, 14_350_000],
    '17m'  => [18_068_000, 18_168_000],
    '15m'  => [21_000_000, 21_450_000],
    '12m'  => [24_890_000, 24_990_000],
    '10m'  => [28_000_000, 29_700_000],
    '6m'   => [50_000_000, 54_000_000],
    '2m'   => [144_000_000, 148_000_000],
    '70cm' => [420_000_000, 450_000_000],
    'GEN'  => [1, 54_000_000],
];

function qrv_norm_call(string $raw): ?string
{
    $s = strtoupper(trim($raw));
    $s = str_replace(' ', '', $s);
    if ($s === '' || strlen($s) > 16) {
        return null;
    }
    // Base call 3–11, optional /P /M /MM /AM /n portable.
    if (!preg_match('/^[A-Z0-9]{3,11}(\/[A-Z0-9]{1,5})?$/', $s)) {
        return null;
    }
    $base = explode('/', $s, 2)[0];
    if (!preg_match('/[A-Z]/', $base) || !preg_match('/[0-9]/', $base)) {
        return null;
    }
    return $s;
}

function qrv_norm_app(mixed $raw): ?string
{
    if (!is_string($raw)) {
        return null;
    }
    foreach (QRV_APPS as $a) {
        if (strcasecmp($raw, $a) === 0) {
            return $a;
        }
    }
    return null;
}

function qrv_norm_mode(mixed $raw): ?string
{
    if (!is_string($raw)) {
        return null;
    }
    // Trim, uppercase, then drop hyphen/space/punctuation so "JS8C-X2" and
    // "VARA HF STD" become JS8CX2 / VARAHFSTD. No regex allow; exact set.
    $s = strtoupper(trim($raw));
    $s = preg_replace('/[^A-Z0-9]/', '', $s) ?? '';
    if ($s === '' || strlen($s) > 12) {
        return null;
    }
    if (isset(QRV_MODE_ALIASES[$s])) {
        $s = QRV_MODE_ALIASES[$s];
    }
    return in_array($s, QRV_MODES, true) ? $s : null;
}

function qrv_norm_band(mixed $raw): ?string
{
    if (!is_string($raw)) {
        return null;
    }
    $s = trim($raw);
    foreach (array_keys(QRV_BANDS) as $b) {
        if (strcasecmp($s, $b) === 0) {
            return $b;
        }
    }
    return null;
}

function qrv_norm_hz(mixed $raw, string $band): ?int
{
    if ($raw === null || $raw === '') {
        return null;
    }
    if (is_string($raw) && preg_match('/^\d+$/', $raw)) {
        $raw = (int) $raw;
    }
    if (!is_int($raw) && !is_float($raw)) {
        return -1;
    }
    $hz = (int) $raw;
    if ($hz <= 0) {
        return -1;
    }
    [$lo, $hi] = QRV_BANDS[$band];
    if ($hz < $lo || $hz > $hi) {
        return -1;
    }
    return $hz;
}

function qrv_norm_note(mixed $raw): ?string
{
    if ($raw === null) {
        return null;
    }
    if (!is_string($raw)) {
        return '';
    }
    $s = trim($raw);
    $s = preg_replace('/[\x00-\x1F\x7F]/', '', $s) ?? '';
    $s = preg_replace('~https?://\S+~i', '', $s) ?? '';
    $s = preg_replace('/www\.\S+/i', '', $s) ?? '';
    $s = preg_replace('/@[\w.]+/', '', $s) ?? '';
    $s = str_replace(['`', '*', '_', '|', '<', '>'], '', $s);
    $s = trim($s);
    if ($s === '') {
        return null;
    }
    if (function_exists('mb_substr')) {
        $s = mb_substr($s, 0, 40);
    } else {
        $s = substr($s, 0, 40);
    }
    return $s;
}

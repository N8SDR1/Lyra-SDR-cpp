# QRV board — client contract (Lyra, STD, Logger)

Live roster at **https://qrvsdr.cc**. One HTTPS API. One PHP allow-list. **Rick Deploys `include/validate.php` once** on Hostinger (and runs `sql/alter_mode_varchar12.sql` if the live `roster.mode` column is still `VARCHAR(8)`). Until that Deploy, STD/Logger tokens such as `FT8` are rejected as `invalid_fields` (not a separate `invalid_mode` code).

Clients talk **only** to this HTTPS service. They **never** talk to Discord (no bot token, no Gateway, no webhook). Discord `#qrv` is updated by the **server** cron from the same roster. Discord prints `call  band  mode  app` with the stored mode string intact (no 3-char smash). Hz and notes never go to Discord.

---

## LATER / PARKED — logged QSOs and weekly board

**Not this slice. Do not implement PHP, SQL, or Logger upload code from this section.** Presence is live; the logged-QSO / weekly-totals product is parked until Rick says start. Full product lock: `qrv_board_plan.md` **LATER / PARKED**. Hostinger upload notes: `qrv-service/README-UPLOAD.md` **LATER / PARKED**.

| Now (shipped) | Later (parked) |
|---|---|
| Heartbeat / QRT / roster | Weekly **aggregates** of logged QSOs |
| Discord **one** edited `#qrv` message | **Second** Discord channel, **one** edited weekly message |
| `call`, `band`, `mode`, `app` (+ optional Hz/note in-app only) | `call`, **count**, **CW / SSB / DIG** mix |
| Lyra / STD / Logger POST **presence** | **SDRLogger+** POSTs totals; Lyra and STD **do not** POST QSOs |

Locked rules for that future slice (so we do not reinvent it):

- **Do not mix QSO dumps into heartbeat, roster, or `#qrv`.** New `/v1` endpoints, new tables, new cron.
- Count **Lyra-sourced and STD-sourced QSOs only** — not every log sitting in Logger.
- Discord: **no frequency, no ADIF, no Hz.** Not stuffed into `#qrv`.
- Opt-in. Reuse the existing **per-install key** from presence unless a written reason says otherwise.
- PHP on Hostinger is **not ready** for this today. Rebuild **SDRLogger+** when the PHP ingest exists; until then Logger must not POST full logs.

---

Server mode rules (`qrv-service/include/validate.php`): **not a regex allow**. Trim, uppercase, strip every character that is not `A-Z0-9` (hyphens and spaces drop), max **12** chars, apply a short alias table, then **exact** membership in `QRV_MODES`.

---

## 0. One Deploy (operator)

| On the PC (this repo) | On qrvsdr.cc |
|---|---|
| `qrv-service/include/validate.php` | `domains/qrvsdr.cc/include/validate.php` |
| `qrv-service/sql/alter_mode_varchar12.sql` | phpMyAdmin, once, if `mode` is still 8 chars |

Do **not** upload `config.php` from git. Do not put secrets in clients.

---

## 1. Product rules (all clients)

| Rule | Behavior |
|---|---|
| Opt-in | Default **OFF**. No heartbeat until the operator enables QRV. |
| Disconnect | No heartbeats while not actually QRV (radio/session down). |
| Explicit QRT | POST `/v1/qrt.php` on disable, exit, or disconnect. Stale timeout is a safety net only. |
| App badge | JSON `app` is exactly `Lyra`, `Logger`, or `STD`. Wrong app = wrong badge and QRT will not delete the row. |
| Exact Hz | Second checkbox, default **OFF**. Omit `hz` unless opted in. Roster GET can see it; Discord never does. |
| Note | Optional, ≤40 characters. Server strips URLs, `@mentions`, markup. In-app only. |
| Presence only | `call`, `band`, `mode`, `app`, optional `hz`, optional `note`. **Do not** POST QSOs, ADIF, logs, spots, audio, chat, or a leaderboard. |
| Secrets | Store only the issued `install_key`. Never invent a key. Never ship Discord or database credentials. |

**Lyra QRV-panel click-to-tune is LOCKED** — see **§10.1**. Logger Hz share for TCI spots, and mapping STD/Logger modes (e.g. FT8) into Lyra, stay later. Logged-QSO weekly totals are **also later** — see **LATER / PARKED** at the top. This API slice is presence only.

---

## 2. Base URL and endpoints

**Base:** `https://qrvsdr.cc`

Hostinger serves PHP under `/v1/`. Paths **must** include `.php`.

| Method | URL | Purpose |
|---|---|---|
| `POST` | `https://qrvsdr.cc/v1/heartbeat.php` | Publish / refresh this install |
| `POST` | `https://qrvsdr.cc/v1/qrt.php` | Remove this install |
| `GET` | `https://qrvsdr.cc/v1/roster.php` | List currently QRV stations |

```
Content-Type: application/json; charset=utf-8
```

Empty body → `400` `empty_body`. Invalid JSON → `400` `invalid_json`. Wrong method → `405` `method_not_allowed`. `OPTIONS` → 204.

HTTPS. Timeouts ~10–12 s. Do not block radio/audio. On failure: “QRV unavailable” and back off.

| Config | Value | Meaning |
|---|---|---|
| `heartbeat_min_interval_s` | **10** | Faster POSTs rejected per install key. The IP limit applies only when issuing a first key, so Lyra, STD, and Logger on one PC do not share one slot. |
| `stale_seconds` | **180** | Roster GET only returns rows seen within this many seconds. |

Heartbeats **20–30 s**. Extra POST on band/mode change is fine if debounced (~10 s) and still ≥ 10 s. Not on every VFO tick.

Roster GET: **2 s** minimum per IP. Poll only while a QRV list is visible; 20–30 s is enough.

---

## 3. `app` (required)

Canonical (server match is case-insensitive; always send these):

```
Lyra
Logger
STD
```

| Client | Send |
|---|---|
| Lyra-SDR | `"app": "Lyra"` |
| STD | `"app": "STD"` |
| SDRLogger+ | `"app": "Logger"` |

---

## 4. `mode` — one allow-list

After `qrv_norm_mode()`, the stored token is **exactly** one of:

```
LSB USB CWL CWU DSB AM SAM FM DIGU DIGL SPEC DRM
SSB NFM PSK31 JT65 JT9 DIGI DATA VARAC PACKET RTTYR
FT8 FT4 FT2
JT65A JT65B JT65C
Q65 Q65C Q65D Q65E
MSK144 MSKMS
JS8 JS8CX2
WSPR FST4 FST4W
HELL RTTY PSK QPSK
OLIVIA CONTESTIA MFSK DOMINOEX THOR MT63
VARA VARAHF
```

**Aliases** (after strip, before allow-list): `VARAHFSTD` → `VARAHF`, `JS8CX2EXPERIMENTS` → `JS8CX2`, `CW` → `CWU`, `DIG` → `DIGI`.

**Fallback if this PHP is not on the host yet:** send `DIGU` (USB-side digital) or `DIGL` (LSB-side). Prefer the real token so Discord shows **FT8**, not DIGU.

Heartbeat rejects unknown modes as **`400` `invalid_fields`**.

### 4.1 Lyra — POST the radio mode as-is

Lyra already uses these names. Do not invent STD/Logger labels on the Lyra heartbeat.

| Lyra mode | JSON `mode` |
|---|---|
| LSB | `LSB` |
| USB | `USB` |
| CWL | `CWL` |
| CWU | `CWU` |
| DSB | `DSB` |
| AM | `AM` |
| SAM | `SAM` |
| FM | `FM` |
| DIGU | `DIGU` |
| DIGL | `DIGL` |
| SPEC | `SPEC` |
| DRM | `DRM` |

### 4.2 STD Mode menu → JSON `mode`

STD menu labels (operator screenshot). **All of these are digital.** After Deploy, POST the token in the right column (already sanitized: `A-Z0-9` only, ≤12). If PHP is not deployed, POST `DIGU` / `DIGL` from radio sideband.

| STD menu | JSON `mode` to POST |
|---|---|
| FT8 | `FT8` |
| FT4 | `FT4` |
| FT2 | `FT2` |
| JT65A | `JT65A` |
| JT65B | `JT65B` |
| JT65C | `JT65C` |
| Q65 | `Q65` |
| Q65C | `Q65C` |
| Q65D | `Q65D` |
| Q65E | `Q65E` |
| MSK144 | `MSK144` |
| MSKMS | `MSKMS` |
| JS8 | `JS8` |
| WSPR | `WSPR` |
| FST4 | `FST4` |
| FST4W | `FST4W` |
| JS8C-X2 (experiments) | `JS8CX2` |
| Hell | `HELL` |
| RTTY | `RTTY` |
| PSK | `PSK` |
| QPSK | `QPSK` |
| Olivia | `OLIVIA` |
| Contestia | `CONTESTIA` |
| MFSK | `MFSK` |
| DominoEX | `DOMINOEX` |
| Thor | `THOR` |
| MT63 | `MT63` |
| VARA HF STD | `VARAHF` |
| (if STD ever sends `VARA` only) | `VARA` |

Voice/CW on the radio (if STD ever exposes them on QRV): same tokens as Lyra (`LSB` `USB` `CWL` `CWU` `AM` `FM` …). `SSB` → USB or LSB from the radio; do not POST `SSB` from STD unless that is all you know (Logger may POST `SSB`).

### 4.3 SDRLogger+ Mode select → JSON `mode`

From `Y:\Claude local\hamlog\templates\index.html` (log Mode `<select>`) plus names Logger already maps in TCI/flrig (`main.py`). Prefer the **form/rig value** after Logger’s own uppercase. Strip hyphen/space in PHP, so `RTTY-R` → `RTTYR`.

| Logger UI / internal name | JSON `mode` to POST |
|---|---|
| USB | `USB` |
| LSB | `LSB` |
| SSB (cluster / generic) | `SSB` |
| CWU | `CWU` |
| CWL | `CWL` |
| CW (no sideband) | `CW` (stored as `CWU`) |
| AM | `AM` |
| SAM | `SAM` |
| FM | `FM` |
| NFM | `NFM` |
| DIGU | `DIGU` |
| DIGL | `DIGL` |
| FT8 | `FT8` |
| FT4 | `FT4` |
| JS8 | `JS8` |
| RTTY | `RTTY` |
| RTTY-R / RTTYR | `RTTYR` |
| PSK31 | `PSK31` |
| WSPR | `WSPR` |
| JT65 | `JT65` |
| JT9 | `JT9` |
| DIGI | `DIGI` |
| DIG | `DIG` (stored as `DIGI`) |
| DATA | `DATA` |
| VARAC | `VARAC` |
| PACKET | `PACKET` |
| MSK144 | `MSK144` |
| Q65 | `Q65` |
| FST4 | `FST4` |
| FST4W | `FST4W` |
| OLIVIA | `OLIVIA` |
| HELL | `HELL` |
| DSB / SPEC / DRM (TCI radio) | `DSB` / `SPEC` / `DRM` |

Logger must **not** POST ADIF/QSO dumps to this API. Presence only.

---

## 5. Other fields

### 5.1 `band` (required)

| `band` | Hz range if `hz` is sent (inclusive) |
|---|---|
| `160m` | 1_800_000 – 2_000_000 |
| `80m` | 3_500_000 – 4_000_000 |
| `60m` | 5_330_000 – 5_407_000 |
| `40m` | 7_000_000 – 7_300_000 |
| `30m` | 10_100_000 – 10_150_000 |
| `20m` | 14_000_000 – 14_350_000 |
| `17m` | 18_068_000 – 18_168_000 |
| `15m` | 21_000_000 – 21_450_000 |
| `12m` | 24_890_000 – 24_990_000 |
| `10m` | 28_000_000 – 29_700_000 |
| `6m` | 50_000_000 – 54_000_000 |
| `2m` | 144_000_000 – 148_000_000 |
| `70cm` | 420_000_000 – 450_000_000 |
| `GEN` | 1 – 54_000_000 |

Derive `band` from tuned frequency. Prefer a ham band over `GEN` when both match.

### 5.2 `call` (required)

Trim, uppercase, strip spaces. Max 16. Base 3–11 `[A-Z0-9]`, optional `/` + 1–5 `[A-Z0-9]`. Base needs a letter and a digit. Bad call → `invalid_fields`.

### 5.3 `hz` (optional)

Omit unless the operator opted in. If present: integer **> 0** inside the declared `band`. `null` / empty = no Hz. Bad → `400` `invalid_hz`. Do not send `0`.

### 5.4 `note` (optional)

Omit when empty. Server: strip controls, URLs, `www.…`, `@mentions`, `` ` * _ | < > ``; trim; max **40**.

### 5.5 `install_key`

64-character hex issued by the server. Never generate in the client.

---

## 6. JSON examples

Wire fields: `call`, `app`, `band`, `mode`, `install_key`, `hz`, `note`.

### 6.1 First heartbeat (no key)

`POST https://qrvsdr.cc/v1/heartbeat.php`

Lyra:

```json
{"call":"N8SDR","app":"Lyra","band":"40m","mode":"LSB"}
```

STD (after Deploy):

```json
{"call":"N8SDR","app":"STD","band":"20m","mode":"FT8"}
```

Logger (after Deploy):

```json
{"call":"N8SDR","app":"Logger","band":"20m","mode":"FT8"}
```

Optional Hz + note (any app, only if opted in):

```json
{"call":"N8SDR","app":"Logger","band":"40m","mode":"LSB","hz":7250000,"note":"net 0100z"}
```

**Response `200`** (key issued **once**):

```json
{"ok":true,"stale_s":180,"install_key":"<64-hex-chars-from-server>"}
```

Store immediately. Live value is `bin2hex(random_bytes(32))` — 64 lowercase hex.

### 6.2 Later heartbeats

```json
{"call":"N8SDR","app":"STD","band":"20m","mode":"FT8","install_key":"<stored-64-hex>"}
```

```json
{"ok":true,"stale_s":180}
```

### 6.3 QRT

`install_key` required.

```json
{"call":"N8SDR","app":"Logger","install_key":"<stored-64-hex>"}
```

```json
{"ok":true}
```

Deletes the row for this `install_key_id` + `call` + `app`. `app: "Lyra"` will not remove a Logger row.

### 6.4 Roster GET

```json
{
  "ok": true,
  "stale_s": 180,
  "entries": [
    {"call": "N8SDR", "app": "STD", "band": "20m", "mode": "FT8", "last_seen": "2026-11-15T23:04:01Z"},
    {"call": "N8SDR", "app": "Logger", "band": "20m", "mode": "FT8", "last_seen": "2026-11-15T23:04:05Z"},
    {"call": "KC8TYK", "app": "Lyra", "band": "40m", "mode": "LSB", "last_seen": "2026-11-15T23:03:44Z"}
  ]
}
```

Same callsign may appear more than once (separate apps). `hz` / `note` absent when unused. `last_seen` is UTC `YYYY-MM-DDTHH:MM:SSZ`.

---

## 7. `install_key` lifecycle

1. First successful heartbeat with empty/omitted key → server binds the key to **that callsign**.
2. Persist per machine / install (settings, not git).
3. Every later heartbeat and QRT sends that key.
4. Guessed key → `401` `bad_key`.
5. Two PCs → two keys. Later posts must use the **same call** as issuance (`403` `call_mismatch`).
6. Callsign change: QRT old call+key, clear stored key, heartbeat new call with empty key.
7. Truncate in logs (first 8 + `...`).

---

## 8. When to heartbeat and QRT

**Heartbeat** while opted in and actually on the air:

- Timer 20–30 s (≥ 10 s floor).
- Debounced band/mode change.
- Not every Hertz.

**QRT** then stop the timer:

- QRV off, app exit, radio/session disconnect.

Failed QRT ages out at `stale_s` (~180 s). Always try QRT.

---

## 9. Error codes

```json
{"ok":false,"error":"<code>"}
```

| HTTP | `error` | Cause |
|---|---|---|
| 400 | `invalid_fields` | Bad/missing `call`, `app`, `band`, or `mode` (heartbeat); or missing `call`/`app`/`install_key` (QRT). Includes unknown mode. |
| 400 | `invalid_hz` | `hz` not a positive integer in-band. |
| 400 | `empty_body` | No body. |
| 400 | `invalid_json` | Not a JSON object. |
| 401 | `bad_key` | `install_key` unknown. Do not invent a replacement. |
| 403 | `call_mismatch` | Key bound to another call. |
| 403 | `forbidden` | Ban. Stop. |
| 405 | `method_not_allowed` | Wrong verb. |
| 429 | `rate_limited` | Faster than 10 s POST or 2 s roster GET. |
| 500 | `not_configured` / `bad_config` | Server config missing. |

Do not retry 400/401/403 in a loop.

---

## 10. Per-app UX

### 10.1 Lyra (already live)

Keep sending Lyra radio modes. No change required for this allow-list expansion.

#### QRV-panel click-to-tune — LOCKED (Lyra)

The floating QRV list is a **tune surface in Lyra only**. Discord `#qrv`, qrvsdr.cc, and any website embed are **not** click-to-tune for Lyra.

A roster row is a **spot** if and only if:

1. `hz` is present (operator shared exact frequency), **and**
2. `app` is exactly `Lyra` (not `STD`, not `Logger`).

| Gesture | Behavior |
|---|---|
| **Single-click** call or frequency | Select / highlight the row only. **Do not** retune. |
| **Double-click** call or frequency on a spot row | Same path as a TCI / cluster / panadapter spot: **focused VFO only** (green-border RX1 or RX2), carrier Hz with the existing CW-pitch convention, Lyra **mode as posted**. |

Same callsign on two apps is **two rows**. Only the Lyra line tunes. The STD or Logger line is presence.

Not a spot (no `hz`, or `app` is `STD` / `Logger`): not clickable as a tune. Tooltip: tune is only a **Lyra** row that **shared frequency**.

**Focused RX only.** Do not move the other VFO.

**v1 — do not map STD/Logger modes into Lyra.** FT8 (or Logger `USB` / other non-Lyra tokens) must not become a Lyra mode on double-click. A later pass may map those; it is **not** current spec.

### 10.2 STD

- QRV opt-in default OFF.
- `"app": "STD"`.
- Map the Mode menu with §4.2. Do **not** collapse FT8→DIGU after Deploy.
- Share-Hz checkbox default OFF.
- Note cap 40.
- Default URL `https://qrvsdr.cc`.
- In-app roster: call, app, band, mode; Hz only if present.
- Discord is not an STD feature.

### 10.3 SDRLogger+ (`Logger`)

- QRV opt-in default OFF (independent of logging).
- `"app": "Logger"` — not `SDRLogger+`, not `Lyra`.
- Map Mode select with §4.3 so Discord can show `FT8` / `PSK31` / `JT65`.
- Same heartbeat / QRT / roster as STD.
- **Not** a log dump: no ADIF, no QSO rows, no weekly totals on this API (parked — **LATER / PARKED** at the top).
- Hz / TCI spots: **later**. This slice is presence (band + mode).
- No Discord token in Logger.
- When the parked slice starts: Logger POSTs **aggregates** (weekly Lyra/STD-sourced counts), not the logbook. Lyra and STD still do not POST QSOs.

---

## 11. Minimal client algorithm

```
if opt-in is OFF:
    do nothing
    return

on enable (connected / QRV):
    POST heartbeat.php  (install_key omitted if none stored)
    if 200 and response has install_key:
        persist it
    start 20–30 s timer

on timer / debounced band-mode change:
    POST heartbeat.php with stored install_key
    if 429: wait ≥ 10 s
    if 401 bad_key: surface error; do not invent a key

on disable / exit / disconnect:
    POST qrt.php with call + this app + install_key
    stop timer
```

Roster GET: only while the list UI is open, 20–30 s, never faster than 2 s.

---

## 12. What this API is not

- Not Discord from the client.
- Not a logbook, cluster, chat, or ADIF ingest.
- Not callsign verification.
- Not default exact-Hz publish.
- Not a weekly QSO board (parked — **LATER / PARKED** at the top of this file). Heartbeat / roster / `#qrv` stay presence-only.

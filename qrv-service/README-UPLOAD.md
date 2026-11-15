# QRV service — Hostinger upload kit

First-slice PHP API for **qrvsdr.cc**. Not compiled into Lyra. No secrets in this folder.

Lyra, **STD**, and **SDRLogger+** (`app`: `Logger`) HTTPS-POST heartbeats here. One Hostinger **Deploy** of `include/validate.php` (plus the mode-column ALTER if the live DB is still `VARCHAR(8)`). A Hostinger **cron** job PATCHes **one** Discord `#qrv` message. Exact Hz and the operator note are stored for the in-app roster and **never** sent to Discord.

Client contract (all three apps): `docs/architecture/qrv_client.md`.

## Layout on the account

```
domains/qrvsdr.cc/
  config/config.php          ← NOT web-reachable; secrets live only here
  include/                   ← PHP helpers (keep next to config, not in public_html)
  public_html/
    .htaccess
    index.php
    v1/heartbeat.php
    v1/qrt.php
    v1/roster.php
    cron/discord_sync.php
```

## Rick steps

1. **Hostinger 2FA** on the account (before the bot token goes on the server).
2. Discord: create `#qrv`, create a bot, invite it to that channel only. Copy bot token, guild ID, channel ID. Token never goes in GitHub, Lyra, or `public_html`.
3. In phpMyAdmin, select the existing DB (`u…_qrv`). Import `sql/schema.sql` on a **new** DB. On an **existing** roster DB, also run `sql/alter_mode_varchar12.sql` (`mode` `VARCHAR(12)` so names like `CONTESTIA` store in full). **One Deploy** of `include/validate.php` — that file is the Lyra + STD + Logger mode allow-list. Until it is on the host, digital tokens (`FT8`, …) return `invalid_fields`.
4. Copy `config/config.example.php` → `domains/qrvsdr.cc/config/config.php` **on the server**. Fill:
   - MySQL name / user / password (placeholders only in the example)
   - Discord `bot_token`, `guild_id`, `channel_id`
   - Optional `cron_secret` only if you must trigger cron over HTTPS
5. Upload:
   - `qrv-service/include/` → `domains/qrvsdr.cc/include/`
   - `qrv-service/public_html/` → `domains/qrvsdr.cc/public_html/`
   - `config.php` stays in `domains/qrvsdr.cc/config/` (never inside `public_html`)
6. Confirm PHP 8.3 for this domain (hPanel → PHP).
7. Quick check from a PC (not a browser challenge):  
   `GET https://qrvsdr.cc/v1/roster.php` → JSON `{"ok":true,"entries":[]}`
8. Cron (every **5 minutes**; every 1 minute if Premium allows). Use **PHP CLI**, not wget, unless you set `cron_secret`.

### Cron command template

Hostinger → Advanced → Cron Jobs. Command (adjust the home path to the one hPanel shows):

```
/usr/bin/php /home/uXXXXXXXX/domains/qrvsdr.cc/public_html/cron/discord_sync.php
```

If hPanel lists a versioned binary, use that instead, e.g. `/usr/bin/php8.3`.

Optional HTTP cron (only if CLI is unavailable): set `cron_secret` in `config.php`, then wget/curl:

```
https://qrvsdr.cc/cron/discord_sync.php?key=YOUR_CRON_SECRET
```

## MySQL host

PHP on the **same** Hostinger account should use **localhost** first. The example config already tries localhost, then `srv1454.hstgr.io` port **3306**.

## API (JSON)

| Method | Path | Body |
|---|---|---|
| POST | `/v1/heartbeat.php` | `call`, `app` (`Lyra`/`Logger`/`STD`), `band`, `mode`; optional `hz`, `note`, `install_key` |
| POST | `/v1/qrt.php` | `call`, `app`, `install_key` — deletes that row |
| GET | `/v1/roster.php` | live rows (stale ~3 min). Includes `hz`/`note` when stored |

First heartbeat **without** `install_key` returns a new `install_key`. Store it on the install. Later posts for that call must send it. Same call on a second PC or another app gets its own key (row keyed by install). Rick can reset a binding by deleting the `install_keys` row (cascade drops roster).

Bans: insert into `bans` (`kind` = `key` / `call` / `ip`, `value`). Key bans may use the SHA-256 hex of the install key (how it is stored) or the raw key.

Rate limit: ~1 heartbeat per 10 s per IP and per key.

## What Discord gets

Call, band, mode, app badge. Sorted. Truncated with `+N more` under 2000 characters. **No Hz. No note.**

## Bot protection

A POST from Lyra is not a browser. If Hostinger / Cloudflare challenges `/v1/*`, turn bot protection **off** for qrvsdr.cc or the API will look dead.

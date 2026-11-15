-- QRV roster (qrvsdr.cc). Import once in Hostinger phpMyAdmin.
-- Charset: utf8mb4. No secrets in this file.

SET NAMES utf8mb4;
SET time_zone = '+00:00';

CREATE TABLE IF NOT EXISTS install_keys (
    id            INT UNSIGNED NOT NULL AUTO_INCREMENT,
    key_hash      CHAR(64)     NOT NULL,
    callsign      VARCHAR(16)  NOT NULL,
    created_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_used_at  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uq_key_hash (key_hash),
    KEY idx_callsign (callsign)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS roster (
    id              INT UNSIGNED NOT NULL AUTO_INCREMENT,
    install_key_id  INT UNSIGNED NOT NULL,
    callsign        VARCHAR(16)  NOT NULL,
    app             VARCHAR(16)  NOT NULL,
    band            VARCHAR(8)   NOT NULL,
    mode            VARCHAR(12)  NOT NULL,
    freq_hz         BIGINT       NULL,
    note            VARCHAR(40)  NULL,
    last_seen       DATETIME     NOT NULL,
    PRIMARY KEY (id),
    UNIQUE KEY uq_install (install_key_id),
    KEY idx_last_seen (last_seen),
    KEY idx_call_app (callsign, app),
    CONSTRAINT fk_roster_key FOREIGN KEY (install_key_id)
        REFERENCES install_keys (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS bans (
    id          INT UNSIGNED NOT NULL AUTO_INCREMENT,
    kind        ENUM('key','call','ip') NOT NULL,
    value       VARCHAR(128) NOT NULL,
    reason      VARCHAR(255) NOT NULL DEFAULT '',
    created_at  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uq_ban (kind, value)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS rate_limit (
    bucket     VARCHAR(128) NOT NULL,
    last_unix  INT UNSIGNED NOT NULL,
    PRIMARY KEY (bucket)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS discord_state (
    id              TINYINT UNSIGNED NOT NULL,
    message_id      VARCHAR(32)      NULL,
    last_hash       CHAR(64)         NULL,
    last_synced_at  DATETIME         NULL,
    PRIMARY KEY (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT INTO discord_state (id) VALUES (1)
    ON DUPLICATE KEY UPDATE id = id;

-- Existing qrvsdr.cc roster: widen mode so CONTESTIA (9) and similar fit.
-- Run once in phpMyAdmin on the live DB. New installs use schema.sql (already 12).

ALTER TABLE roster MODIFY mode VARCHAR(12) NOT NULL;

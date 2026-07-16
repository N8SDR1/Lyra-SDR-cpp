#!/usr/bin/env python3
"""Fetch RBN daily archive and extract CW spots for a band/time window.

Produces a label CSV (utc, freq_khz, dx_call, wpm, snr_db, spotter) suitable
for aligning against a timestamped IQ recording.

Usage:
  python rbn_fetch.py YYYY-MM-DD [--band 20m] [--from HH:MM] [--to HH:MM]
                      [--fmin kHz] [--fmax kHz] [-o out.csv]

Note: the daily archive for date D is posted after D ends (UTC), so today's
spots are available tomorrow.
"""
import argparse
import csv
import io
import os
import sys
import tempfile
import urllib.request
import zipfile
from datetime import datetime, timezone

ARCHIVE_URL = "https://data.reversebeacon.net/rbn_history/{y:04d}{m:02d}{d:02d}.zip"


def fetch_day(date, cache_dir=None):
    fname = f"{date.year:04d}{date.month:02d}{date.day:02d}.zip"
    cache = os.path.join(cache_dir or tempfile.gettempdir(), "rbn_" + fname)
    if os.path.exists(cache):
        print(f"using cached {cache}", file=sys.stderr)
    else:
        url = ARCHIVE_URL.format(y=date.year, m=date.month, d=date.day)
        print(f"downloading {url} ...", file=sys.stderr)
        req = urllib.request.Request(url, headers={"User-Agent": "lyra-cw-labels/0.1"})
        with urllib.request.urlopen(req, timeout=120) as r:
            data = r.read()
        print(f"  {len(data)/1e6:.1f} MB", file=sys.stderr)
        with open(cache, "wb") as f:
            f.write(data)
    zf = zipfile.ZipFile(cache)
    name = zf.namelist()[0]
    return io.TextIOWrapper(zf.open(name), encoding="utf-8", errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("date", help="UTC date YYYY-MM-DD of the recording")
    ap.add_argument("--band", default="20m")
    ap.add_argument("--from", dest="t_from", default="00:00", help="UTC start HH:MM")
    ap.add_argument("--to", dest="t_to", default="23:59", help="UTC end HH:MM")
    ap.add_argument("--fmin", type=float, default=None, help="min freq kHz")
    ap.add_argument("--fmax", type=float, default=None, help="max freq kHz")
    ap.add_argument("-o", "--out", default=None)
    args = ap.parse_args()

    date = datetime.strptime(args.date, "%Y-%m-%d").replace(tzinfo=timezone.utc)
    t0 = datetime.strptime(args.t_from, "%H:%M").time()
    t1 = datetime.strptime(args.t_to, "%H:%M").time()

    reader = csv.DictReader(fetch_day(date))
    rows = []
    for row in reader:
        # archive columns: 'tx_mode' is the emission mode (CW/RTTY/FT8...);
        # 'mode' is the spot type (CQ/DX/BEACON)
        if (row.get("tx_mode") or "").strip() != "CW":
            continue
        if (row.get("band") or "").strip() != args.band:
            continue
        # archive timestamp format: "2026-07-08 19:00:02"
        ts = datetime.strptime(row["date"].strip(), "%Y-%m-%d %H:%M:%S")
        if not (t0 <= ts.time() <= t1):
            continue
        freq = float(row["freq"])
        if args.fmin is not None and freq < args.fmin:
            continue
        if args.fmax is not None and freq > args.fmax:
            continue
        rows.append({
            "utc": ts.strftime("%H:%M:%S"),
            "freq_khz": f"{freq:.1f}",
            "dx_call": row["dx"].strip(),
            "wpm": row.get("speed", "").strip(),
            "snr_db": row.get("db", "").strip(),
            "spotter": row["callsign"].strip(),
        })

    rows.sort(key=lambda r: (r["utc"], r["freq_khz"]))
    out = open(args.out, "w", newline="") if args.out else sys.stdout
    w = csv.DictWriter(out, fieldnames=["utc", "freq_khz", "dx_call", "wpm", "snr_db", "spotter"])
    w.writeheader()
    w.writerows(rows)
    if args.out:
        out.close()
        uniq = len({r["dx_call"] for r in rows})
        print(f"{len(rows)} spots, {uniq} unique calls -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()

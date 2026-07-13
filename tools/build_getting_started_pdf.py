#!/usr/bin/env python3
"""Regenerate the bundled Getting-Started PDF from GETTING_STARTED.md.

The source markdown (docs/help/GETTING_STARTED.md) is emoji-rich and uses
**bold**/*italic* markers so it renders nicely in the in-app Help and on
GitHub.  The ReportLab PDF path in sync_execution_plan.py has no emoji font
and does not parse inline emphasis, so those would show as tofu boxes and
literal asterisks in the PDF.  This helper strips both into a temp copy,
then hands that to the doc tool.

Run after editing docs/help/GETTING_STARTED.md:

    py -3 tools/build_getting_started_pdf.py

Output: docs/Lyra-Getting-Started.pdf  (bundled by installer/lyra.iss into
{app}\\docs and opened by the first-run 'New to Lyra?' splash link).
"""
from __future__ import annotations

import subprocess
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "help" / "GETTING_STARTED.md"
PDF = ROOT / "docs" / "Lyra-Getting-Started.pdf"
TOOL = ROOT / "tools" / "sync_execution_plan.py"


def pdf_clean(md: str) -> str:
    """Strip **/* emphasis markers and emoji/pictographs (keep -> - . quotes)."""
    md = md.replace("**", "").replace("*", "")
    out = []
    for ch in md:
        if ord(ch) in (0xFE0F, 0x200D):          # VS16 / ZWJ
            continue
        if unicodedata.category(ch) == "So":       # emoji / pictographs
            continue
        out.append(ch)
    return "\n".join(line.rstrip() for line in "".join(out).split("\n"))


def main() -> int:
    if not SRC.exists():
        print(f"[gs-pdf] missing source: {SRC}", file=sys.stderr)
        return 2
    tmp = ROOT / "build" / "_gs_pdf.md"
    tmp.parent.mkdir(parents=True, exist_ok=True)
    tmp.write_text(pdf_clean(SRC.read_text(encoding="utf-8")), encoding="utf-8")
    rc = subprocess.call([
        sys.executable, str(TOOL),
        "--md", str(tmp),
        "--pdf", "--pdf-path", str(PDF),
        "--no-docx",
        "--title", "Lyra - Getting Started",
    ])
    if rc == 0:
        print(f"[gs-pdf] wrote {PDF} ({PDF.stat().st_size:,} bytes)")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())

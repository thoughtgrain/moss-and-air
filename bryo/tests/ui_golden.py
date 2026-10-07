#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Moss: pixel fingerprints of every UI render, so a refactor can prove it changed nothing on screen.

  tests/ui_golden.py check  GOLDEN UI_DIR      exit 1 with the list of screens whose pixels changed
  tests/ui_golden.py update GOLDEN UI_DIR      rewrite GOLDEN (only for reviewed, intentional changes)

UI_DIR is build/ui_new after tests/ui_render.c; the fingerprint is the SHA-256 of each raw PPM in UI_DIR/ppm
(every screen in every palette and style), so it doesn't depend on how Pillow encodes a PNG.
"""
import hashlib
import sys
from pathlib import Path


def fingerprints(ui):
    ppm = Path(ui) / "ppm"
    files = sorted(ppm.rglob("*.ppm"))
    if not files:
        sys.exit(f"ui_golden: no PPMs in {ppm} (run tests/ui_render.c first)")
    return {str(p.relative_to(ppm)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}


def read(golden):
    out = {}
    for line in Path(golden).read_text().splitlines():
        if line and not line.startswith("#"):
            h, name = line.split(None, 1)
            out[name] = h
    return out


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("check", "update"):
        sys.exit(__doc__)
    mode, golden, ui = sys.argv[1:]
    now = fingerprints(ui)
    if mode == "update":
        body = "".join(f"{h}  {n}\n" for n, h in sorted(now.items()))
        Path(golden).write_text("# SPDX-License-Identifier: GPL-3.0-only\n"
                                "# Moss: SHA-256 of every UI render (tests/ui_golden.py)\n" + body)
        print(f"ui_golden: {len(now)} renders written to {golden}")
        return
    if not Path(golden).exists():
        sys.exit(f"ui_golden: no {golden} (tests/ui_golden.py update {golden} {ui})")
    was = read(golden)
    changed = sorted(n for n in now.keys() & was.keys() if now[n] != was[n])
    new, gone = sorted(now.keys() - was.keys()), sorted(was.keys() - now.keys())
    for label, names in (("changed", changed), ("new", new), ("gone", gone)):
        for n in names:
            print(f"ui_golden: {label}: {n}")
    if changed or new or gone:
        print(f"ui_golden: {len(changed)} changed, {len(new)} new, {len(gone)} gone of {len(was)}; "
              f"if intended: tests/ui_golden.py update {golden} {ui}")
        sys.exit(1)
    print(f"ui_golden: {len(now)} renders pixel-identical")


if __name__ == "__main__":
    main()

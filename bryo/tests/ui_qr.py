#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bryo: the ABOUT screen's QR code, read back from the renders, must be the source URL ui_menu.c names.

  tests/ui_qr.py UI_DIR        (after tests/ui_render.c and tests/ui_render.py; needs OpenCV, else skipped)

The URL is the one in the comment above ABOUT_QR in firmware/src/ui_menu.c; the GPL offer of the source on the
device is only real if the code scans to it.
"""
import re
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    ui = Path(sys.argv[1])
    try:
        import cv2
        import numpy as np
        from PIL import Image
    except ImportError:
        print("ui_qr: skipped (pip3 install opencv-python-headless to check the ABOUT QR code)")
        return
    src = (Path(__file__).resolve().parent.parent / "firmware/src/ui_menu.c").read_text(encoding="latin-1")
    m = re.search(r"source[^:]*: (HTTPS://\S+), QR version", src)
    if not m:
        sys.exit("ui_qr: no source URL in the comment above ABOUT_QR (firmware/src/ui_menu.c)")
    want, bad = m.group(1), []
    pngs = sorted(ui.glob("*/about.png"))
    if not pngs:
        sys.exit(f"ui_qr: no {ui}/<palette>/about.png (run tests/ui_render.py first)")
    for p in pngs:
        im = Image.open(p).convert("L").resize((960, 960), Image.NEAREST)
        got, _, _ = cv2.QRCodeDetector().detectAndDecode(np.array(im))
        if got != want:
            bad.append(f"{p.parent.name}: {got!r}")
    if bad:
        sys.exit("ui_qr: the ABOUT QR does not read " + want + ": " + ", ".join(bad))
    print(f"ui_qr: ABOUT QR reads {want} in {len(pngs)} palettes")


if __name__ == "__main__":
    main()

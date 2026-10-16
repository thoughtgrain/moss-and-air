#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bryo's screen font as a web font, so the site's boxy type is the firmware's own.

  python3 site/tools/gen_px_webfont.py            (from the repository root)

Reads PX5, the 5 x 7 face the FM-1's screens are drawn in, straight from bryo/tools/gen_px_font.py (the same table
the firmware's ui_pxfont.h is generated from), and writes two WOFF files into site/fonts/ and the wordmark into site/img/:

  bryo-px5.woff        regular: a dot is a dot, 6 dots a character (5 + a gap), as px_text() draws PXF_5
  bryo-px5-bold.woff   bold: each dot doubled to the right, 7 dots a character, as the screens' titles (PXF_5B)

  img/wordmark.svg     BRYO in PX5, as the boot screen spells it (px_text_big), for the header
  img/favicon.svg      a B on black, the same way

One dot is 100 units on a 1000-unit em, so a capital is 0.7 em tall, centred in the line box. Lower case maps onto the capitals, as on the
screen (px_glyph folds a..z). A character PX5 doesn't draw isn't in the font at all, so the browser falls back to
the next font in the stack instead of printing the firmware's "?". Run it again whenever PX5 changes.

Needs fontTools (pip3 install fonttools). WOFF (zlib) rather than WOFF2 so it doesn't need brotli too.
"""
import importlib.util
import sys
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "bryo" / "tools" / "gen_px_font.py"
OUT = ROOT / "site" / "fonts"

DOT = 100           # font units a dot
UPM = 1000
ROWS = 7
ASC, DESC = 850, 150  # the line box: centred on the capitals (see build())
SEAM = 4            # each row's runs reach this far into the row below: no hairline between rows when antialiased


def load_px5():
    spec = importlib.util.spec_from_file_location("gen_px_font", SRC)
    if spec is None or spec.loader is None:
        sys.exit(f"gen_px_webfont: can't load {SRC}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    px5 = getattr(mod, "PX5", None)
    if not isinstance(px5, dict) or "?" not in px5:
        sys.exit(f"gen_px_webfont: {SRC} has no PX5 table")
    return px5


def runs(row):
    """(start, length) of each run of dots in one row of ASCII art"""
    out, x = [], 0
    while x < len(row):
        if row[x] == "#":
            s = x
            while x < len(row) and row[x] == "#":
                x += 1
            out.append((s, x - s))
        else:
            x += 1
    return out


def glyph(art, bold):
    pen = TTGlyphPen(None)
    for r, row in enumerate(art):
        if bold:                                    # row << 1 | row: each dot doubled to the right
            bits = [c == "#" for c in row] + [False]
            wide = [bits[i] or (i > 0 and bits[i - 1]) for i in range(len(bits))]
            row = "".join("#" if b else "." for b in wide)
        top = (ROWS - r) * DOT                      # the baseline is the bottom of the 7th row
        bottom = top - DOT - (SEAM if r < ROWS - 1 else 0)
        for s, n in runs(row):
            x0, x1 = s * DOT, (s + n) * DOT
            pen.moveTo((x0, bottom))                # clockwise, TrueType's outer contour direction
            pen.lineTo((x0, top))
            pen.lineTo((x1, top))
            pen.lineTo((x1, bottom))
            pen.closePath()
    return pen.glyph()


def build(px5, bold, path):
    adv = (7 if bold else 6) * DOT
    names = {" ": "space"}
    order, glyphs, metrics, cmap = [".notdef"], {}, {".notdef": (adv, 0)}, {}

    glyphs[".notdef"] = glyph(px5["?"], bold)
    for ch, art in sorted(px5.items()):
        name = names.get(ch, f"uni{ord(ch):04X}")
        order.append(name)
        glyphs[name] = glyph(art, bold)
        metrics[name] = (adv, 0)
        cmap[ord(ch)] = name
        if "A" <= ch <= "Z":
            cmap[ord(ch.lower())] = name           # lower case prints as upper case, as on the screen

    family = "Bryo PX5"
    style = "Bold" if bold else "Regular"
    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    glyf = fb.font["glyf"]
    hmtx = {}
    for n in order:                                 # lsb = the glyph's left edge, as hmtx wants it
        g = glyf[n]
        g.recalcBounds(glyf)
        hmtx[n] = (metrics[n][0], getattr(g, "xMin", 0) if g.numberOfContours else 0)
    fb.setupHorizontalMetrics(hmtx)
    # The capitals stand 700 units above the baseline. A browser centres the line box on (ascent - descent) / 2,
    # so ascent 850 and descent 150 put that centre at 350, the capitals' middle: a label in a button or a tab
    # sits exactly in the middle of its box (it was 800/200, 50 units high).
    fb.setupHorizontalHeader(ascent=ASC, descent=-DESC)
    fb.setupNameTable({
        "familyName": family,
        "styleName": style,
        "uniqueFontIdentifier": f"{family} {style}",
        "fullName": f"{family} {style}",
        "psName": f"BryoPX5-{style}",
        "copyright": "Bryo's 5x7 screen face (bryo/tools/gen_px_font.py), GPL-3.0-only",
    })
    fb.setupOS2(sTypoAscender=ASC, sTypoDescender=-DESC, sTypoLineGap=0, usWinAscent=ASC, usWinDescent=DESC,
                usWeightClass=700 if bold else 400, fsSelection=0x20 if bold else 0x40, sCapHeight=700,
                sxHeight=700)
    fb.setupPost(isFixedPitch=1)
    fb.font["head"].macStyle = 1 if bold else 0
    fb.font.flavor = "woff"
    path.parent.mkdir(parents=True, exist_ok=True)
    fb.save(str(path))
    return len(cmap)


def svg_text(px5, text, ink, pad=0, bg=None):
    """text in PX5 as an SVG of 1-unit dots, 6 a character, the firmware's spacing"""
    w, h = len(text) * 6 - 1 + 2 * pad, ROWS + 2 * pad
    rects = []
    for i, ch in enumerate(text):
        for r, row in enumerate(px5.get(ch, px5["?"])):
            for s, n in runs(row):
                rects.append(f"M{pad + i * 6 + s} {pad + r}h{n}v1h-{n}z")
    back = f'<rect width="{w}" height="{h}" fill="{bg}"/>' if bg else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w * 8}" height="{h * 8}">'
            f"<!-- SPDX-License-Identifier: GPL-3.0-only -->"
            f"<!-- generated by site/tools/gen_px_webfont.py from Bryo's PX5 screen font -->{back}"
            f'<path fill="{ink}" shape-rendering="crispEdges" d="{"".join(rects)}"/></svg>\n')


def main():
    px5 = load_px5()
    img = ROOT / "site" / "img"
    (img / "wordmark.svg").write_text(svg_text(px5, "BRYO", "#ffffff"))
    (img / "favicon.svg").write_text(svg_text(px5, "B", "#ffffff", pad=2, bg="#000000"))
    print(f"{img / 'wordmark.svg'}, {img / 'favicon.svg'}")
    for bold, name in ((False, "bryo-px5.woff"), (True, "bryo-px5-bold.woff")):
        n = build(px5, bold, OUT / name)
        print(f"{OUT / name}: {n} characters")


if __name__ == "__main__":
    main()

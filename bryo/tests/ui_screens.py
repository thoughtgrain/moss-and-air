#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bryo: the screens by pattern.

  tests/ui_screens.py MANIFEST UI_DIR [PALETTE ...]

MANIFEST is tests/ui_screens.tsv; UI_DIR is build/ui_new (after tests/ui_render.c and tests/ui_render.py).

The check (exit 1 on a finding): every PNG tests/ui_render.c drew is in the manifest, and every manifest row
was drawn, so a new screen can't land without being given a pattern. The values of each column are from a
fixed set too, and every CUSTOM:<x> card source names a cards_<x>() in firmware/src (the fillers in ui_draw.c and
ui_layer.c), so the map can't drift from the code.

The output, in UI_DIR/patterns/: one contact sheet per pattern (by shell, then cards, panel and footer) for
each palette (GREY by default), and summary.txt with the counts.
"""
import sys
from collections import Counter, defaultdict
from pathlib import Path

SHELLS = {"PAGE", "LAYER", "DIALOG", "MENU", "NAME", "INFO"}
CARDS = {"HOME", "TABLE", "-"}
PANELS = {"SCOPE", "LIST", "NOTE", "STRIPS", "KEYMAP", "-"}
FOOTERS = {"STEPS", "ACTIONS", "GRID", "KEYS", "-"}


def load(path):
    rows, errors = [], []
    for n, line in enumerate(Path(path).read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#") or line.startswith("screen\t"):
            continue
        f = line.split("\t")
        if len(f) != 5:
            errors.append(f"{path}:{n}: {len(f)} columns, expected 5")
            continue
        s, shell, cards, panel, foot = f
        if shell not in SHELLS:
            errors.append(f"{path}:{n}: {s}: shell {shell!r}")
        if cards not in CARDS and not cards.startswith("CUSTOM:"):
            errors.append(f"{path}:{n}: {s}: cards {cards!r}")
        if panel not in PANELS and not panel.split(":")[0] in ("CHART", "EDITOR"):
            errors.append(f"{path}:{n}: {s}: panel {panel!r}")
        if foot not in FOOTERS:
            errors.append(f"{path}:{n}: {s}: footer {foot!r}")
        rows.append(dict(screen=s, shell=shell, cards=cards, panel=panel, footer=foot))
    seen = Counter(r["screen"] for r in rows)
    errors += [f"{path}: {s} listed {k} times" for s, k in seen.items() if k > 1]
    return rows, errors


def sheet(pngs, out, cols=6, pad=8, label_h=14):
    from PIL import Image, ImageDraw
    ims = [(p.stem, Image.open(p).convert("RGB")) for p in pngs]
    w, h = ims[0][1].size
    rows = (len(ims) + cols - 1) // cols
    canvas = Image.new("RGB", (cols * (w + pad) + pad, rows * (h + pad + label_h) + pad), (40, 40, 40))
    d = ImageDraw.Draw(canvas)
    for i, (name, im) in enumerate(ims):
        x = pad + (i % cols) * (w + pad)
        y = pad + (i // cols) * (h + pad + label_h)
        canvas.paste(im, (x, y))
        d.text((x, y + h + 2), name, fill=(200, 200, 200))
    canvas.save(out)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    manifest, ui = sys.argv[1], Path(sys.argv[2])
    palettes = sys.argv[3:] or ["GREY"]
    rows, errors = load(manifest)
    listed = {r["screen"] for r in rows}
    ref = ui / palettes[0]
    if not ref.is_dir():
        sys.exit(f"ui_screens: no {ref} (run tests/ui_render.c and tests/ui_render.py first)")
    drawn = {p.stem for p in ref.glob("*.png")}
    errors += [f"drawn but not in the manifest: {s} (give it a pattern in {manifest})" for s in sorted(drawn - listed)]
    errors += [f"in the manifest but not drawn: {s}" for s in sorted(listed - drawn)]
    src = Path(manifest).resolve().parent.parent / "firmware" / "src"
    code = "".join(f.read_text(errors="replace") for f in sorted(src.glob("ui*.c")))
    fillers = sorted({r["cards"][7:] for r in rows if r["cards"].startswith("CUSTOM:")})
    errors += [f"cards CUSTOM:{x}: no static void cards_{x}( in {src}/ui*.c" for x in fillers
               if f"static void cards_{x}(" not in code]

    out = ui / "patterns"
    out.mkdir(exist_ok=True)
    lines = [f"{len(rows)} screens, {len(drawn)} drawn", ""]
    for col in ("shell", "cards", "panel", "footer"):
        c = Counter(r[col] for r in rows)
        lines.append(f"{col}: " + ", ".join(f"{k} {v}" for k, v in c.most_common()))
    page = [r for r in rows if r["shell"] in ("PAGE", "LAYER")]
    custom = sorted({r["cards"] for r in page if r["cards"].startswith("CUSTOM:")})
    combos = Counter((r["shell"], r["cards"].split(":")[0], r["panel"].split(":")[0], r["footer"]) for r in page)
    lines += ["", f"PAGE frame (PAGE + LAYER): {len(page)} of {len(rows)} screens",
              f"  card sources: TABLE/HOME {sum(1 for r in page if not r['cards'].startswith('CUSTOM'))}, "
              f"hand-written {sum(1 for r in page if r['cards'].startswith('CUSTOM'))} in {len(custom)} branches: "
              + " ".join(c[7:] for c in custom),
              "  shell / cards / panel / footer combinations:"]
    lines += [f"    {k:4d}  {' / '.join(c)}" for c, k in combos.most_common()]

    groups = defaultdict(list)
    for r in rows:
        key = r["shell"] if r["shell"] not in ("PAGE", "LAYER") else f"{r['shell']}_{r['panel'].split(':')[0]}"
        groups[key].append(r["screen"])
    lines += ["", "contact sheets (patterns/<palette>_<group>.png):"]
    try:
        import PIL  # noqa: F401
        have_pil = True
    except ImportError:
        have_pil = False
        lines.append("  (no Pillow: sheets skipped)")
    for g, names in sorted(groups.items()):
        lines.append(f"  {g:14s} {len(names):3d}  {' '.join(names)}")
        if not have_pil:
            continue
        for pal in palettes:
            pngs = [ui / pal / f"{n}.png" for n in names if (ui / pal / f"{n}.png").exists()]
            if pngs:
                sheet(pngs, out / f"{pal}_{g}.png")
    (out / "summary.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    for e in errors:
        print("ui_screens: " + e, file=sys.stderr)
    if errors:
        sys.exit(1)
    print(f"ui_screens: every screen has a pattern; sheets and summary in {out}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site's FM-1 panel, labelled for Bryo as it is now, drawn in the firmware's own language.

  python3 site/tools/gen_panel.py                                (writes site/img/panel.svg)
  python3 site/tools/gen_panel.py --steps site/tools/steps.json  (writes site/img/steps/*.svg, one a step)

From the repository root. A step is the panel with the controls to use lit (press, hold, or a knob turned to an
angle) and the screen that step shows in its screen: the docs' guides use one per step.

Every word is Bryo's PX5 screen face as dots (read from bryo/tools/gen_px_font.py, like the web font), it's a
wireframe like the site (outlines only, dashed where something is a guide or not assigned yet), every colour
is the screen's (firmware/src/ui_px.c: px_page sky blue for the page pads, px_mod reddish purple for the modulator
pads, as the screen colours their pages), and the screen shows a real render (bryo/docs/manual/img/tape.png, embedded
so the SVG works on its own in an <img>). The labels follow docs/controls.tsv: a control marked "planned" there is
drawn dim and says so (none is, since 2026-10-18). Run it again when a control's job changes.

Every control is a group, <g id="ctl-NAME" class="ctl">, its outline the rect with class "hit", and a knob's
pointer a rect with class "ptr" whose transform-origin is the knob's centre. The screen's <image> is
id="panel-screen". The homepage's walkthrough (js/main.js, initTour) lights, presses and turns them by those names
and swaps the screen; as an <img> (the docs) they're inert. Names: MASTER SELECT PRESETS ALGORITHM OCTDN OCTUP,
K1-K4, the pads by their labels (FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC), the black keys OP1-OP6 PIT
BGLO MONO POLY ZERO, the white keys W1-W16.
"""
import base64
import importlib.util
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BRYO = ROOT / "bryo"
OUT = ROOT / "site" / "img" / "panel.svg"

INK, SOFT, DIM = "#ffffff", "#b8b8b8", "#737373"         # ink, a readable grey, the firmware's dim ink
PAGE, MOD, LIVE, REC, NOTE = "#56b4e9", "#cc79a7", "#e69f00", "#d55e00", "#f0e442"
GO = "#3fae5a"                                            # the transport pads are green on the FM-1 itself
EDGE, WIRE = "#3a3a3a", "#8c8c8c"          # the site's wire and a lighter one for the controls


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        sys.exit(f"gen_panel: can't load {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


PX5 = load("gen_px_font", BRYO / "tools" / "gen_px_font.py").PX5


def text(s, x, y, dot=2.0, fill=INK, bold=False, anchor="middle"):
    """s in PX5 as dots; (x, y) is the top of the capitals, x placed by anchor (start, middle, end)"""
    adv = 7 if bold else 6
    w = (len(s) * adv - 1) * dot
    x0 = x - w / 2 if anchor == "middle" else x - w if anchor == "end" else x
    d = []
    for i, ch in enumerate(s.upper()):
        art = PX5.get(ch, PX5["?"])
        for r, row in enumerate(art):
            bits = [c == "#" for c in row]
            if bold:
                bits = [bits[k] or (k > 0 and bits[k - 1]) for k in range(5)] + [bits[4]]
            k = 0
            while k < len(bits):
                if bits[k]:
                    s0 = k
                    while k < len(bits) and bits[k]:
                        k += 1
                    d.append(f"M{x0 + (i * adv + s0) * dot:g} {y + r * dot:g}h{(k - s0) * dot:g}v{dot:g}h{-(k - s0) * dot:g}z")
                else:
                    k += 1
    return f'<path fill="{fill}" d="{"".join(d)}"/>' if d else ""


def lines(s, x, y, dot, fill):
    """a caption, one line per "/": a line is 9 dots (7 and a gap of 2)"""
    return "".join(text(t.strip(), x, y + i * (9 * dot), dot, fill) for i, t in enumerate(s.split("/")))


def group(cid, body):
    return f'<g id="ctl-{cid}" class="ctl">{body}</g>'


def knob(x, y, name, job, job_fill=LIVE, cid=None):
    return group(cid or name, "".join([
        f'<rect class="hit" x="{x - 19}" y="{y - 19}" width="38" height="38" fill="none" stroke="{WIRE}" stroke-width="1.5"/>',
        f'<rect class="ptr" x="{x - 2}" y="{y - 15}" width="4" height="14" fill="{INK}" '
        f'style="transform-origin:{x}px {y}px"/>',
        text(name, x, y + 28, 1.5, INK, bold=True),
        text(job, x, y + 44, 1.5, job_fill) if job else "",
    ]))


def pad(x, y, name, job, edge):
    return group(name, "".join([
        f'<rect class="hit" x="{x - 32}" y="{y}" width="64" height="30" fill="none" stroke="{edge}" stroke-width="2"/>',
        text(name, x, y + 8, 2, INK, bold=True),
        lines(job, x, y + 38, 1.5, SOFT) if job else "",
    ]))


def build(screen_png, title="The FM-1 panel, labelled for Bryo"):
    """the panel as SVG text, with screen_png (bytes) in its screen"""
    screen = base64.b64encode(screen_png).decode("ascii")
    W, H = 960, 540
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
           f'shape-rendering="crispEdges">',
           "<!-- SPDX-License-Identifier: GPL-3.0-only -->",
           "<!-- generated by site/tools/gen_panel.py from bryo/docs/controls.tsv's jobs, in Bryo's PX5 face -->",
           f"<title>{title}</title>",
           f'<rect width="{W}" height="{H}" fill="#000"/>',             # the screen's black, so it reads on any page
           f'<rect x="1" y="1" width="{W - 2}" height="{H - 2}" fill="none" stroke="{EDGE}" stroke-width="1.5"/>']

    # the four labelled knobs, the octave buttons
    out += [knob(70, 46, "MASTER", "VOLUME"), knob(178, 46, "SELECT", "TEMPO"),
            knob(70, 134, "PRESETS", "PROJECTS"), knob(178, 134, "ALGORITHM", "REC IN")]
    for x, name, job, cid in ((70, "OCT-", "OCTAVE DOWN", "OCTDN"), (178, "OCT+", "OCTAVE UP", "OCTUP")):
        out.append(group(cid, f'<rect class="hit" x="{x - 30}" y="200" width="60" height="26" fill="none" '
                              f'stroke="{WIRE}" stroke-width="1.5"/>'
                              + text(name, x, 206, 2, INK, bold=True) + text(job, x, 234, 1.5, SOFT)))

    # the screen, a real render at 0.8x its 240 px
    out += [f'<rect x="252" y="14" width="204" height="204" fill="#000" stroke="{WIRE}" stroke-width="1.5"/>',
            f'<image id="panel-screen" x="258" y="20" width="192" height="192" style="image-rendering:pixelated" '
            f'href="data:image/png;base64,{screen}"/>']

    # KNOB 1-4: the four values on the screen
    for i, x in enumerate((520, 620, 720, 820)):
        out.append(knob(x, 40, f"KNOB {i + 1}", "", cid=f"K{i + 1}"))
    out.append(text("THE FOUR VALUES ON THE SCREEN", 670, 82, 1.5, LIVE))

    # the pads, as they sit on the panel; the edge is the colour the screen gives their pages
    xs = (504, 584, 664, 744, 824, 904)
    row1 = (("FX", "COLOR/CHRONOS", PAGE), ("SCL", "HOLD:/RESET", SOFT), ("ENV", "ENVELOPE/FILTER", MOD),
            ("LFO", "M1 M2 M3", MOD), ("EDIT", "GRAIN/RIPPLE", PAGE), ("GLO", "MIXER/SCENES", PAGE))
    row2 = (("HOME", "SOURCE", PAGE), ("SAVE", "HOLD:/UNDO", SOFT), ("ARP", "ARPEGGIO", MOD),
            ("SEQ", "M4 STEPS", MOD), ("PLAY", "START/STOP", GO), ("REC", "RECORD", GO))
    for x, (n, j, c) in zip(xs, row1):
        out.append(pad(x, 104, n, j, c))
    for x, (n, j, c) in zip(xs, row2):
        out.append(pad(x, 174, n, j, c))

    # the keys
    out.append(f'<rect x="40" y="262" width="880" height="232" fill="none" stroke="{EDGE}" stroke-width="1.5" stroke-dasharray="4 4"/>')
    black = ((104, "OP1", "MUTE/T1", LIVE), (154, "OP2", "MUTE/T2", LIVE), (204, "OP3", "MUTE/T3", LIVE),
             (304, "OP4", "MUTE/T4", LIVE), (354, "OP5", "REV/SCN-", LIVE), (454, "OP6", "HALF/SCN+", LIVE),
             (504, "PIT", "TAP/TEMPO", LIVE), (554, "GLO", "NUDGE/+4%", LIVE), (654, "MONO", "CLEAR/MOD", NOTE),
             (704, "POLY", "CLEAR/TAPE", NOTE), (804, "0", "FREEZE", LIVE))
    for x, n, j, c in black:
        dim = c == DIM
        dash = ' stroke-dasharray="3 3"' if dim else ""
        cid = {"GLO": "BGLO", "0": "ZERO"}.get(n, n)
        out.append(group(cid, lines(j, x, 272, 1.5, c)
                         + f'<rect class="hit" x="{x - 21}" y="304" width="42" height="52" fill="none" '
                           f'stroke="{EDGE if dim else WIRE}" stroke-width="1.5"{dash}/>'
                         + text(n, x, 323 if len(n) < 4 else 325, 2 if len(n) < 4 else 1.5, DIM if dim else INK,
                                bold=len(n) < 4)))
    step = 53
    for k in range(16):
        x = 60 + k * step
        out.append(group(f"W{k + 1}", f'<rect class="hit" x="{x}" y="366" width="{step - 7}" height="112" fill="none" '
                                      f'stroke="{EDGE}" stroke-width="1.5"/>'
                         + text(str(k + 1), x + (step - 7) / 2, 456, 2, INK, bold=True)))

    out.append(text("WHITE KEYS: SLICES (TAPE) / NOTES (SYNTH POLY) / THE KIT (DRUM)", W / 2, 506, 1.5, SOFT))
    out.append(text("GLO HELD + WHITE 1-4: PICK THE TRACK.  OP5 OP6: SCENES OFF TAPE.  PIT HELD: -4%", W / 2, 522, 1.5, DIM))
    out.append("</svg>\n")
    return "".join(out)


# ---- a step: the controls to use, lit ----
# press: an orange outline. hold: an orange outline with an orange fill, so it reads as "keep it down".
# turn: an orange outline, the pointer swung to the angle given (degrees from 12 o'clock) and orange.
MARK_STROKE = 4


def mark(svg, cid, how):
    mode, angle = (how, None) if isinstance(how, str) else (how[0], how[1])
    start = svg.find(f'<g id="ctl-{cid}" class="ctl">')
    if start < 0:
        sys.exit(f"gen_panel: no control {cid}")
    end = svg.find("</g>", start)
    g = svg[start:end]
    hit = re.search(r'<rect class="hit"[^>]*/>', g).group(0)
    lit = re.sub(r'stroke="[^"]*"', f'stroke="{LIVE}"', hit)
    lit = re.sub(r'stroke-width="[^"]*"', f'stroke-width="{MARK_STROKE}"', lit)
    lit = lit.replace(' stroke-dasharray="3 3"', "")
    if mode == "hold":
        lit = lit.replace('fill="none"', f'fill="{LIVE}" fill-opacity="0.35"')
    g = g.replace(hit, lit)
    if mode == "turn":
        ptr = re.search(r'<rect class="ptr"[^>]*/>', g).group(0)
        cx, cy = re.search(r"transform-origin:([\d.]+)px ([\d.]+)px", ptr).groups()
        turned = ptr.replace(f'fill="{INK}"', f'fill="{LIVE}"')
        # the rotation carries its own centre; the style's transform-origin (for the homepage's CSS turns) would
        # be applied on top of it by browsers and swing the pointer off the knob, so a static turn drops it
        turned = re.sub(r' style="transform-origin:[^"]*"', "", turned)
        turned = turned.replace("/>", f' transform="rotate({angle or 0} {cx} {cy})"/>')
        g = g.replace(ptr, turned)
    return svg[:start] + g + svg[end:]


def main():
    img = ROOT / "site" / "img"
    if len(sys.argv) > 2 and sys.argv[1] == "--steps":
        # site/tools/steps.json: [{"id", "screen" (a site/img png), "alt", "marks": {NAME: "press"|"hold"|["turn", deg]}}]
        steps = json.loads(Path(sys.argv[2]).read_text())
        outdir = img / "steps"
        outdir.mkdir(parents=True, exist_ok=True)
        total = 0
        for st in steps:
            try:
                png = (img / f"{st['screen']}.png").read_bytes()
            except OSError as e:
                sys.exit(f"gen_panel: step {st['id']}: {e}")
            svg = build(png, st.get("alt", "The FM-1 panel"))
            for cid, how in st["marks"].items():
                svg = mark(svg, cid, how)
            path = outdir / f"{st['id']}.svg"
            path.write_text(svg)
            total += path.stat().st_size
        print(f"{outdir}: {len(steps)} steps, {total // 1024} KB")
        return
    tape = BRYO / "docs" / "manual" / "img" / "tape.png"
    try:
        png = tape.read_bytes()
    except OSError as e:
        sys.exit(f"gen_panel: {e}")
    OUT.write_text(build(png))
    print(f"{OUT}: {OUT.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()

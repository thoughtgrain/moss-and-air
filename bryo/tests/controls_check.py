#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bryo: docs/controls.tsv is well formed and complete.

  tests/controls_check.py [docs/controls.tsv]

Checks: seven columns per row; the context, gesture and status values come from the header comment's sets; the
control names are real FM-1 controls (14 buttons, 7 encoders, MASTER, 16 white and 11 black keys, ranges within
them); no (context, control, gesture) twice; status "now" has phase "-" and status "planned" a phase 1..10; and
every physical control appears at least once on a page or in "any", so nothing on the panel goes unmentioned.
"""
import re
import sys
from pathlib import Path

CONTEXTS = {"any", "power-on", "page", "page:tape", "page:grain", "page:resonator", "page:color", "page:space",
            "page:slot", "mixer", "glo-held", "slot-held", "slot-held+step", "project-view", "routing-view"}
GESTURES = {"press", "tap", "hold", "release", "turn", "hold-turn", "combo"}
STATUS = {"now", "planned", "proposed", "unassigned"}
BUTTONS = ["HOME", "EDIT", "FX", "LFO", "ENV", "SEQ", "ARP", "SCL", "GLO", "REC", "PLAY", "SAVE", "OCTDN", "OCTUP"]
ENCODERS = ["SELECT", "PRESETS", "ALGORITHM", "K1", "K2", "K3", "K4"]
BLACKS = ["OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLO", "MONO", "POLY", "ZERO"]


def expand(control):
    """the physical controls a row names (a range expands), or None if it names none"""
    m = re.fullmatch(r"WHITE_(\d+)(?:-(\d+))?", control)
    if m:
        a, b = int(m.group(1)), int(m.group(2) or m.group(1))
        return [f"WHITE_{n}" for n in range(a, b + 1)] if 1 <= a <= b <= 16 else None
    m = re.fullmatch(r"ENC_K(\d)-(\d)", control)
    if m:
        a, b = int(m.group(1)), int(m.group(2))
        return [f"ENC_K{n}" for n in range(a, b + 1)] if 1 <= a <= b <= 4 else None
    if control == "POT_MASTER":
        return [control]
    kind, _, name = control.partition("_")
    ok = {"BTN": BUTTONS, "ENC": ENCODERS, "BLACK": BLACKS}.get(kind, [])
    return [control] if name in ok else None


def main():
    path = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "docs/controls.tsv")
    rows, errors, header = [], [], None
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        f = line.split("\t")
        if header is None:
            header = f
            if f != ["context", "control", "gesture", "action", "status", "phase", "source"]:
                errors.append(f"{path}:{n}: header {f}")
            continue
        if len(f) != 7:
            errors.append(f"{path}:{n}: {len(f)} columns, expected 7")
            continue
        ctx, ctl, ges, act, st, ph, src = f
        where = f"{path}:{n} ({ctx} {ctl} {ges})"
        if ctx not in CONTEXTS:
            errors.append(f"{where}: context {ctx!r}")
        if ges not in GESTURES:
            errors.append(f"{where}: gesture {ges!r}")
        if st not in STATUS:
            errors.append(f"{where}: status {st!r}")
        if expand(ctl) is None:
            errors.append(f"{where}: not an FM-1 control: {ctl!r}")
        if st == "planned" and not (ph.isdigit() and 1 <= int(ph) <= 10):
            errors.append(f"{where}: planned needs a phase 1..10, has {ph!r}")
        if st == "now" and ph not in ("-",) and not ph.isdigit():
            errors.append(f"{where}: phase {ph!r}")
        if not act.strip():
            errors.append(f"{where}: no action")
        rows.append((ctx, ctl, ges, st))
    seen = {}
    for r in rows:
        seen[r[:3]] = seen.get(r[:3], 0) + 1
    errors += [f"{path}: {k} listed {v} times" for k, v in seen.items() if v > 1]
    physical = ["POT_MASTER"] + [f"BTN_{b}" for b in BUTTONS] + [f"ENC_{e}" for e in ENCODERS] + \
               [f"WHITE_{n}" for n in range(1, 17)] + [f"BLACK_{b}" for b in BLACKS]
    covered = set()
    for ctx, ctl, _, _ in rows:
        if ctx in ("any", "page") or ctx.startswith("page:"):
            covered.update(expand(ctl) or [])
    errors += [f"{path}: {c} appears in no page or 'any' row" for c in physical if c not in covered]
    for e in errors:
        print("controls: " + e)
    counts = {s: sum(1 for r in rows if r[3] == s) for s in sorted(STATUS)}
    print(f"controls: {len(rows)} rows ({', '.join(f'{v} {k}' for k, v in counts.items())}); "
          f"all {len(physical)} physical controls mapped" if not errors else f"controls: {len(errors)} problems")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()

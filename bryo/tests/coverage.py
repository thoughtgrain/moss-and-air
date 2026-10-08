# SPDX-License-Identifier: GPL-3.0-only
"""Line coverage of Bryo's firmware files under the host tests (run by tests/run_tests.sh when gcov is there).

Builds tests/bryo_host.c, tests/usb_msc_test.c and tests/usb_sie_test.c with --coverage into build/cov, runs them,
and merges what each one executed: a line counts as run when any of them ran it (usb.c and msc.c are in more than
one). usb.c's MIDI, update and audio paths are Felucca's, checked by uac_test, usb_desc_test and ota_test (not
counted here): its share is the drive's glue and EP0. Prints a table and the lines never run, per file, so a gap
shows up where it is. It reports; it doesn't fail on a number (a test is worth what it checks, not the lines it
passes through).

    python3 tests/coverage.py [--lines]
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "cov")
GEN = os.path.join(ROOT, "build", "gen")
FILES = ["mem.c", "tape.c", "synth.c", "poly.c", "grain.c", "reso.c", "color.c", "space.c", "usbrec.c", "reel.c", "vdisk.c", "msc.c", "usb.c", "chain.c", "param.c", "ui.c", "ui_input.c", "ui_px.c",
         "ui_viz.c"]
TESTS = [   # name, source, flags, run args
    ("bryo_host", "tests/bryo_host.c", ["-O0", "-I" + GEN, "-Itests"], [os.path.join(OUT, "ui")]),
    ("usb_msc_0", "tests/usb_msc_test.c", ["-O1", "-DT_UAC=0"], []),   # (-O1: the pi32v2 barriers stay unemitted)
    ("usb_msc_1", "tests/usb_msc_test.c", ["-O1", "-DT_UAC=1"], []),
    ("usb_sie_0", "tests/usb_sie_test.c", ["-O0", "-no-pie", "-DT_UAC=0"], []),
    ("usb_sie_1", "tests/usb_sie_test.c", ["-O0", "-no-pie", "-DT_UAC=1"], []),
]
LINE = re.compile(r"^\s*([^:]+):\s*(\d+):(.*)$")


def gcov_lines(d, obj):
    """{file: (run lines, runnable lines)} from one test's gcov data"""
    out = subprocess.run(["gcov", "-t", "-o", d, obj], cwd=d, capture_output=True, text=True).stdout
    res, cur = {}, None
    for ln in out.splitlines():
        m = LINE.match(ln)
        if not m:
            continue
        cnt, no, src = m.group(1).strip(), int(m.group(2)), m.group(3)
        if no == 0:
            if src.startswith("Source:"):
                cur = os.path.basename(src[7:].strip())
                res.setdefault(cur, (set(), set(), {}))
            continue
        if cur is None or cnt == "-":
            continue
        run, all_, text = res[cur]
        all_.add(no)
        text[no] = src
        if cnt not in ("#####", "====="):
            run.add(no)
    return res


def main():
    cc = os.environ.get("CC", "cc").split()
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(os.path.join(OUT, "ui", "ppm"))
    merged = {}
    for name, src, flags, args in TESTS:
        d = os.path.join(OUT, name)
        os.makedirs(d)
        exe = os.path.join(d, name)
        r = subprocess.run(cc + ["--coverage", "-w"] + flags + ["-o", exe, os.path.join(ROOT, src), "-lm"], cwd=d,
                           capture_output=True, text=True)
        if r.returncode:
            print(r.stderr)
            return 1
        subprocess.run([exe] + args, cwd=ROOT, capture_output=True)   # (their own pass / fail is run_tests.sh's)
        obj = [f for f in os.listdir(d) if f.endswith(".gcno")][0]
        for f, (run, all_, text) in gcov_lines(d, obj).items():
            m = merged.setdefault(f, (set(), set(), {}))
            m[0].update(run)
            m[1].update(all_)
            m[2].update(text)
    tot_run = tot_all = 0
    print("%-12s %7s %7s %6s" % ("file", "run", "lines", "%"))
    for f in FILES:
        if f not in merged:
            print("%-12s (not built by any test)" % f)
            continue
        run, all_, _ = merged[f]
        tot_run += len(run)
        tot_all += len(all_)
        print("%-12s %7d %7d %5.1f%%" % (f, len(run), len(all_), 100.0 * len(run) / max(1, len(all_))))
    print("%-12s %7d %7d %5.1f%%" % ("all", tot_run, tot_all, 100.0 * tot_run / max(1, tot_all)))
    if "--lines" in sys.argv:
        for f in FILES:
            if f in merged:
                run, all_, text = merged[f]
                for no in sorted(all_ - run):
                    print("  %s:%d %s" % (f, no, text[no].strip()))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Moss: the UI renders without the JieLi toolchain or the SDK. tests/run_tests.sh needs ./build.sh first; this
# only needs cc, python3, Pillow and fontTools, so it runs on any Linux or macOS box (and in CI).
#   tests/ui_host.sh [PALETTE ...]      (default GREY; the sheets per pattern for each palette named)
#
# 1. the generated headers (tools/gen_*.py, the same calls as tools/build.py generate())
# 2. tests/ui_render.c: every screen in every palette, the layout lint, alignment, draw cost
# 3. tests/ui_render.py: the PNGs and the contact sheets per palette
# 4. tests/ui_golden.py: every render pixel-identical to tests/ui_golden.txt (a refactor must not move a pixel)
# 5. tests/ui_screens.py: every screen has a pattern (tests/ui_screens.tsv); the sheets per pattern
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
CC="${CC:-cc}"
"$PY" -c 'import PIL, fontTools' 2>/dev/null || {
    echo "ui_host: $PY needs Pillow and fontTools (pip3 install Pillow fonttools)"; exit 1; }
GEN=build/gen
mkdir -p "$GEN" build/host build/ui_new/ppm build/ui_slot
"$PY" tools/gen_aa_font.py "$GEN/ui_fonts.h" --preset inter-tight >/dev/null
for t in gen_aa_icons.py:ui_icons.h gen_aa_keycaps.py:ui_keycaps.h gen_ui_palettes.py:ui_palettes.h \
         gen_tables.py:felucca_tables.h gen_fm6_patches.py:felucca_fm6.h gen_samples.py:felucca_samples.h; do
    "$PY" "tools/${t%%:*}" "$GEN/${t#*:}" >/dev/null || { echo "ui_host: tools/${t%%:*} failed"; exit 1; }
done
$CC -O1 -w -I"$GEN" -Ifirmware/src -Itests -o build/host/ui_render tests/ui_render.c -lm
build/host/ui_render build/ui_new build/ui_slot
fail=0
"$PY" tests/ui_golden.py check tests/ui_golden.txt build/ui_new || fail=1
"$PY" tests/ui_render.py build/ui_new build/ui_slot
"$PY" tests/ui_screens.py tests/ui_screens.tsv build/ui_new "$@" || fail=1
[ $fail -eq 0 ] && echo "UI HOST TESTS PASSED" || { echo "UI HOST TESTS FAILED"; exit 1; }

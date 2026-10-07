#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Bryo's host tests (no hardware). Run from the repo root:
#   tests/run_tests.sh
#
# Without a device build (needs cc, python3 with Pillow + fontTools; clang and node when present):
#   firmware type check  bryo.c with clang -fsyntax-only -m32 in every build-flag combination (a stand-in for the
#                        JieLi compiler's front end: types, declarations, missing symbols); build.py's register rules
#   hardware layer       storage (A/B, torn writes), the input scan (debounce, encoders, LEDs), the TRS MIDI parser,
#                        USB audio (descriptors with and without CDC, ring, packets), every USB descriptor layout
#   display              text against the reference renderer, palettes (contrast, GREY gray, MONO neutral)
#   Bryo                 tests/bryo_host.c: the chain (silence, pitch, mute, release, shedding, the ceiling), the
#                        input mapping, every screen in every palette; tests/ui_golden.py: the screens pixel-identical
#                        to tests/bryo_golden.txt (a change of the screen must be deliberate: update it then);
#                        tests/controls_check.py: docs/controls.tsv well formed, every physical control in it
#   installer            tools/fm1_install.py against a simulated FM-1; the web installer and its backup code
# With ./build.sh's build/felucca.fwsc: the M-UPGRADE entry and the update loader against the real package.
set -e
cd "$(dirname "$0")/.."
OUT=build/host
GEN=build/gen
mkdir -p "$OUT" "$GEN"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
PY="${PYTHON:-python3}"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

# the generated headers (tools/build.py generate() runs the same tools; a device build makes them too)
if [ ! -f "$GEN/felucca_tables.h" ] || [ ! -f "$GEN/ui_icons.h" ] || [ ! -f "$GEN/ui_fonts.h" ] || \
   [ ! -f "$GEN/ui_keycaps.h" ] || [ ! -f "$GEN/ui_palettes.h" ] || [ ! -f "$GEN/ui_pxfont.h" ]; then
    "$PY" tools/gen_aa_font.py "$GEN/ui_fonts.h" --preset inter-tight >/dev/null
    for t in gen_aa_icons.py:ui_icons.h gen_aa_keycaps.py:ui_keycaps.h gen_ui_palettes.py:ui_palettes.h gen_px_font.py:ui_pxfont.h \
             gen_tables.py:felucca_tables.h; do
        "$PY" "tools/${t%%:*}" "$GEN/${t#*:}" >/dev/null
    done
fi

# ---- the firmware, without the JieLi toolchain
if command -v clang >/dev/null 2>&1; then
    tc() {   # flags...: no diagnostic at all
        n=$(clang -fsyntax-only -m32 -ffreestanding -Os -fno-builtin -Wall -Wno-unused-function "$@" \
            -Ifirmware/hal -Ifirmware/src -I"$GEN" firmware/src/bryo.c 2>&1 | tee "$OUT/typecheck.log" | grep -cE "error|warning" || true)
        [ "$n" = 0 ] || { cat "$OUT/typecheck.log"; return 1; }
    }
    run "firmware type check: bryo.c, default flags" tc
    run "firmware type check: no CDC console" tc -DFELUCCA_CDC=0
    run "firmware type check: no USB audio" tc -DFELUCCA_UAC=0
    run "firmware type check: no TRS MIDI" tc -DFELUCCA_UART=0
    run "firmware type check: RAM only (no flash, no OTA)" tc -DFELUCCA_OTA=0 -DFELUCCA_FLASH=0
else
    echo "== skip firmware type check (no clang)"
fi
run "register access only in hal/ (build.py mmio_check)" "$PY" -c "
import sys, importlib.util
spec = importlib.util.spec_from_file_location('b', 'tools/build.py'); b = importlib.util.module_from_spec(spec)
sys.argv = ['build.py']; spec.loader.exec_module(b)
e = b.mmio_check(); print('\n'.join(e)); sys.exit(1 if e else 0)"

# ---- the hardware layer
$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
$CC -o "$OUT/input_test" tests/input_test.c
run "keys and buttons: fast press, long release, bouncy contacts, glitches, encoders, LEDs" "$OUT/input_test"
$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/bryo.h)
$CC -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/uac_test.c
run "USB audio: descriptors (with CDC), ring and packets" "$OUT/uac_test"
$CC -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/uac_test.c
run "USB audio: descriptors (without CDC), ring and packets" "$OUT/uac_test_nocdc"
for v in 1.1.0.1 1.1.1.1 1.1.2.1 1.1.3.1 1.1.0.0 1.1.2.0 1.0.0.1 1.0.1.1 1.0.0.0 0.1.0.1 0.0.0.1; do
    IFS=. read -r t_cdc t_uac t_lay t_on <<EOF
$v
EOF
    $CC -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -o "$OUT/usb_desc_test" tests/usb_desc_test.c
    run "USB descriptors: CDC $t_cdc (presented $t_on), UAC $t_uac, layout $t_lay" "$OUT/usb_desc_test"
done

# ---- the display
$CC -w -I"$GEN" -Ifirmware/src -o "$OUT/text_ref_test" tests/text_ref_test.c -lm
run "text: pens, kerning and pixels equal the reference renderer" "$OUT/text_ref_test"
$CC -w -I"$GEN" -Ifirmware/src -o "$OUT/theme_test" tests/theme_test.c -lm
run "themes: contrast, text blending and font metrics" "$OUT/theme_test"
if "$PY" -c "import PIL" 2>/dev/null; then
    run "text spacing: glyph gaps against the font's own" "$PY" tests/text_spacing_test.py "$GEN/ui_fonts.h"
fi

# ---- Bryo
mkdir -p build/bryo_ui/ppm
$CC -I"$GEN" -Itests -o "$OUT/bryo_host" tests/bryo_host.c -lm
run "Bryo: chain, input mapping, every screen in every palette" "$OUT/bryo_host" build/bryo_ui
run "Bryo screens pixel-identical to tests/bryo_golden.txt" "$PY" tests/ui_golden.py check tests/bryo_golden.txt build/bryo_ui
run "controls map (docs/controls.tsv): well formed, every physical control mapped" "$PY" tests/controls_check.py

# ---- the installer
run "installer CLI (fm1_install.py) against a simulated FM-1" "$PY" tests/install_test.py
if command -v node >/dev/null 2>&1; then
    run "web installer: package builder, update protocol" node web/test_web.mjs
    run "web backup: capture, validation before writes, restore order" node web/test_backup.mjs
else
    echo "== skip web tests (no node)"
fi

# ---- with a device build
if [ -f build/felucca.fwsc ]; then
    $CC -DOWN_PKG=1 -o "$OUT/ota_test" tests/ota_test.c
    run "M-UPGRADE entry (own loader)" "$OUT/ota_test" build/felucca.fwsc
    head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
    "$PY" tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
    $CC -o "$OUT/ldr_test" tests/ldr_test.c
    run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc
else
    echo "== skip the update path tests (run ./build.sh first: they need build/felucca.fwsc)"
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }

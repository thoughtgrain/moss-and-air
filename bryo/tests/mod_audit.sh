#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Bryo: how every knob a modulator can move behaves (tests/mod_audit.c renders, tests/mod_audit.py reports).
#   tests/mod_audit.sh [--cost]     (--cost: each target's instructions too, under callgrind: a few minutes)
# The renders land in build/mod_audit (about 60 MB; delete it after).
set -e
cd "$(dirname "$0")/.."
OUT=build/mod_audit
mkdir -p "$OUT"
sed 's/^int main(int argc, char \*\*argv)/int bh_main(int argc, char **argv)/' tests/bryo_host.c > "$OUT/bh_copy.c"
cc -O2 -g -w -I"$OUT" -Ibuild/gen -Itests -o "$OUT/mod_audit" tests/mod_audit.c -lm
rm -f "$OUT"/*.wav
"$OUT/mod_audit" "$OUT"
if [ "$1" = "--cost" ]; then
    rm -rf "$OUT/cost"
    mkdir -p "$OUT/cost"
    (cd "$OUT/cost" && valgrind --tool=callgrind --dump-instr=no --callgrind-out-file=cg.out ../mod_audit . cost \
        >/dev/null 2>vg.log)
fi
python3 tests/mod_audit.py "$OUT"

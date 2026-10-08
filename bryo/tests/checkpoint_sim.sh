#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Bryo: the hardware checkpoint on the host (tests/checkpoint_sim.c): every run rendered to build/checkpoint/runN.wav,
# each run's cost in host instructions per output sample (callgrind, when valgrind is there), and the interrupt stress.
#   tests/checkpoint_sim.sh [STRESS_SECONDS]
set -e
cd "$(dirname "$0")/.."
OUT=build/checkpoint
GEN=build/gen
mkdir -p "$OUT"
cc -O2 -w -I"$GEN" -Itests -o "$OUT/checkpoint_sim" tests/checkpoint_sim.c -lm -lrt
"$OUT/checkpoint_sim" wav "$OUT"
if command -v valgrind >/dev/null 2>&1; then
    echo "checkpoint: host instructions per output sample, the whole chain (chain_block), each run:"
    for n in 1 2 3 4 5 6 7 8 9 11 12 13 14 15 16 17 18 19 20 21 22 23; do
        valgrind --tool=callgrind --callgrind-out-file="$OUT/cg.$n" "$OUT/checkpoint_sim" run "$n" >/dev/null 2>&1
        ir=$(callgrind_annotate --inclusive=yes "$OUT/cg.$n" 2>/dev/null | grep -m1 'chain_block' | awk '{gsub(",","",$1); print $1}')
        blocks=$(case $n in 7) echo 27562;; 8) echo 16537;; 9) echo 11024;; *) echo 8268;; esac)
        echo "  run $n: $((ir / blocks / 32))"
    done
fi
"$OUT/checkpoint_sim" stress "${1:-20}"

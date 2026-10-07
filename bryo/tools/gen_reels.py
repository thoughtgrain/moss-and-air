#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bryo's factory reels, in the tape's own format, into a C header.

A reel is what a tape plays before you record on it (docs/bryo-architecture.md, "Where TAPE's audio comes from").
The format is the tape's, so a reel plays straight from flash and copying it into a tape is a plain copy:

  IMA ADPCM, 4 bits, mono, 22,050 Hz, low nibble first, in blocks of 256 samples (128 bytes). Each block has the
  decoder state at its start (predictor, step index), so any block decodes on its own: slices, reverse and grains
  start anywhere. Each block also has its peak (0..255), which the screen draws.

The sounds are made here from material already in the repository (no download, nothing third party beyond the
CC0 set): Felucca's generated drums and plucks (tools/gen_waves.py) and the CC0 piano and flute
(assets/samples-cc0, Versilian Studios, see its ATTRIBUTION.txt).

  tools/gen_reels.py OUT.h
"""
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_waves as gw  # noqa: E402
from sampleio import IMA_IDX, IMA_STEP, read_any_wav, resample  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
SR = 22050
BLK = 256
TAPE_SAMPLES = 288 * BLK                     # a tape's capacity (firmware/src/tape.c TAPE_NBLK)
PEAK = 0.8 * 32767


def norm(x, peak=PEAK):
    m = max(1e-9, max(abs(v) for v in x))
    return [v * peak / m for v in x]


def place(buf, at, s, gain=1.0):
    for i, v in enumerate(s):
        if at + i >= len(buf):
            break
        buf[at + i] += v * gain


def pitched(x, sr, semis):
    """x played back semis semitones up (resampled to SR, shorter when up)"""
    return resample(x, sr * 2 ** (semis / 12), SR)


def reel_beat():
    """one bar of 16ths at 120 BPM (2 s): kick, snare, closed and open hats, clap, rim"""
    kit = {k: resample(norm(v, 1.0), gw.SR, SR) for k, v in (
        ("K", gw.drum_kick()), ("S", gw.drum_snare()), ("H", gw.drum_metal_hat(0.045)),
        ("O", gw.drum_metal_hat(0.28)), ("C", gw.drum_clap()), ("R", gw.drum_rim()))}
    pat = {"K": "x.....x.x..x....", "S": "....x.......x...", "H": "x.x.x.x.x.x.x.x.",
           "O": "...............x", "C": "............x...", "R": "..........x...x."}
    gain = {"K": 1.0, "S": 0.8, "H": 0.35, "O": 0.3, "C": 0.6, "R": 0.4}
    step = SR // 8                               # a 16th at 120 BPM
    buf = [0.0] * (16 * step)
    for k, p in pat.items():
        for i, c in enumerate(p):
            if c == "x":
                place(buf, i * step, kit[k], gain[k])
    return buf


def reel_keys():
    """a piano arpeggio in 8ths at 120 BPM (2 s): C E G B C B G E, from the CC0 C4"""
    sr, x = read_any_wav(SRC / "assets/samples-cc0/PIANO/03_GPiano_sus_C4_v2_rr1_Player.wav")
    x = norm(x, 1.0)
    step = SR // 4
    buf = [0.0] * (8 * step + SR // 2)
    for i, semi in enumerate((0, 4, 7, 11, 12, 11, 7, 4)):
        n = pitched(x, sr, semi)[: int(SR * 0.9)]
        n = [v * math.exp(-j / (SR * 0.35)) for j, v in enumerate(n)]
        place(buf, i * step, n, 0.6)
    return buf[: 8 * step]


def reel_air():
    """a flute pad: C5 then G4, each faded in and out (2.6 s), from the CC0 C5"""
    sr, x = read_any_wav(SRC / "assets/samples-cc0/FLUTE/01_LDFlute_susvib_C5_v1_1.wav")
    x = norm(x, 1.0)
    seg = int(SR * 1.3)
    buf = []
    for semi in (0, -5):
        n = pitched(x, sr, semi)
        n = (n * (1 + seg // max(1, len(n))))[:seg]
        f = int(SR * 0.25)
        buf += [v * min(1.0, j / f, (seg - j) / f) for j, v in enumerate(n)]
    return buf


def reel_pluck():
    """plucked strings, a minor pentatonic figure in 16ths at 120 BPM (2 s)"""
    notes = (0, 3, 7, 10, 12, 10, 7, 3, 0, 5, 7, 12, 15, 12, 7, 5)
    step = SR // 8
    buf = [0.0] * (16 * step + SR // 2)
    for i, semi in enumerate(notes):
        f = 220.0 * 2 ** (semi / 12)
        s = resample(gw.pluck(f, 0.6), gw.SR, SR)
        place(buf, i * step, s, 0.5)
    return buf[: 16 * step]


REELS = [("BEAT", reel_beat), ("KEYS", reel_keys), ("AIR", reel_air), ("PLUK", reel_pluck)]


def encode(pcm):
    """-> (bytes, [(pred, idx)] at each block start, [peak 0..255] per block); the encoder is the tape's own
    (firmware/src/tape.c ima_enc), run continuously from 0, 0"""
    n = (len(pcm) + BLK - 1) // BLK * BLK
    pcm = [int(max(-32768, min(32767, round(v)))) for v in pcm] + [0] * (n - len(pcm))
    pred, idx, out, states, peaks = 0, 0, bytearray(), [], []
    for b in range(0, n, BLK):
        states.append((pred, idx))
        peaks.append(min(255, max(abs(v) for v in pcm[b:b + BLK]) >> 7))
        nib = []
        for x in pcm[b:b + BLK]:
            step = IMA_STEP[idx]
            diff, code = x - pred, 0
            if diff < 0:
                code, diff = 8, -diff
            vd = step >> 3
            if diff >= step:
                code |= 4
                diff -= step
                vd += step
            if diff >= step >> 1:
                code |= 2
                diff -= step >> 1
                vd += step >> 1
            if diff >= step >> 2:
                code |= 1
                vd += step >> 2
            pred = max(-32768, min(32767, pred - vd if code & 8 else pred + vd))
            idx = max(0, min(88, idx + IMA_IDX[code & 7]))
            nib.append(code)
        out += bytes(a | (c << 4) for a, c in zip(nib[0::2], nib[1::2]))
    return bytes(out), states, peaks


def c_bytes(name, data, per=24):
    rows = [", ".join(str(b) for b in data[i:i + per]) for i in range(0, len(data), per)]
    return f"static const uint8_t {name}[{len(data)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n"


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    parts = ["/* generated by tools/gen_reels.py: Bryo's factory reels (the tape's format: firmware/src/tape.c) */",
             "#pragma once", "#include <stdint.h>", ""]
    table = []
    for i, (name, make) in enumerate(REELS):
        random.seed(1 + i)                       # the drums' noise (gen_waves uses the global generator): fixed
        pcm = norm(make())[:TAPE_SAMPLES]
        data, st, pk = encode(pcm)
        parts.append(c_bytes(f"REEL{i}_DATA", data))
        parts.append(f"static const int16_t REEL{i}_PRED[{len(st)}] = {{{', '.join(str(p) for p, _ in st)}}};")
        parts.append(f"static const uint8_t REEL{i}_IDX[{len(st)}] = {{{', '.join(str(x) for _, x in st)}}};")
        parts.append(c_bytes(f"REEL{i}_PEAK", bytes(pk)))
        table.append(f'    {{"{name}", {len(st)}u, REEL{i}_DATA, REEL{i}_PRED, REEL{i}_IDX, REEL{i}_PEAK}},')
    parts += [f"#define NREEL {len(REELS)}u",
              "/* reel_t {name, blocks, data, predictor and step index at each block, peak per block} (tape.c) */",
              "#define REELS_INIT \\", "\\\n".join(table).rstrip(",") + "", ""]
    parts.append("#define REEL_NAMES_INIT " + ", ".join(f'"{n}"' for n, _ in REELS))
    Path(sys.argv[1]).write_text("\n".join(parts) + "\n")
    print(f"gen_reels: {len(REELS)} reels")


if __name__ == "__main__":
    main()

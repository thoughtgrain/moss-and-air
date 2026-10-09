#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Bryo: reads what tests/mod_audit.c wrote (DIR: targets.csv, b_G_P.wav and m_G_P.wav, optionally cost/) and prints,
# for every knob a slot can move, how it behaves under modulation against the same 3 s without it:
#   change  the difference against the sound, dB (under -60: the knob moved nothing)
#   edge    how much sharper the sound is at the control blocks' starts than inside them, dB, without -> with. This is
#           the one that finds a knob applied in steps (a block at a time): the sound jumps where the blocks start.
#           It doesn't care what the waveform is.
#   clicks  sharp spots a second, without -> with, and
#   buzz    the blocks' rate (1,378 Hz and harmonics) against the whole sound, dB: for information only. A saw's own
#           edges count as clicks, and a pitch or a filter that moves smears the harmonics into the buzz bands, so
#           both rise on a moving synth that steps nowhere (I learned that the hard way).
#   cost    with cost/ (mod_audit.sh --cost): host instructions a sample, the extra over the same setup unmodulated
# STEPS flags an edge that rises by more than 1 dB. Needs numpy.
#   tests/mod_audit.py DIR
import csv, glob, os, re, sys
import numpy as np

DEV = {'0': 'TAPE', '1': 'GRAIN', '2': 'RESO', '3': 'COLOR', '4': 'SPACE', '5': 'SYNTH', '6': 'POLY', '7': 'CHAN'}


def load(p):
    import wave
    w = wave.open(p)
    return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)


def change(b, m):
    return 20 * np.log10(np.sqrt(((m - b) ** 2).mean()) / (np.sqrt((b ** 2).mean()) + 1e-9) + 1e-12)


def clicks(x):
    y = np.abs(np.diff(x, 2))
    n = len(y) // 32 * 32
    seg = y[:n].reshape(-1, 32).max(1)
    env = np.sqrt(np.convolve(y ** 2, np.ones(2048) / 2048, 'same'))[:n].reshape(-1, 32).mean(1) + 1e-6
    return (seg > 8 * env).sum() / (len(x) / 44100)


def edge(x):
    """the sound's sharpness at the control blocks' starts against the rest of the block, dB: a knob applied once a
    block in steps jumps there (the blocks start at multiples of 32 samples from the render's start)"""
    y = np.abs(np.diff(x, 2))
    n = len(y) // 32 * 32
    ph = y[:n].reshape(-1, 32).mean(0)
    return 20 * np.log10(ph[30:32].mean() / (np.median(ph) + 1e-9) + 1e-9)


def buzz(x):
    s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / 44100)
    e = 0.0
    for k in range(1, 16):
        c = k * 1378.125
        band = (f > c - 2) & (f < c + 2)
        ref = np.median(s[(f > c - 150) & (f < c + 150)])
        e += max(s[band].sum() - ref * band.sum(), 0)
    return 10 * np.log10(e / (s.sum() + 1e-12) + 1e-15)


def costs(d):
    out = {}
    for f in glob.glob(os.path.join(d, 'cost', 'cg.out.*')):
        t = open(f).read()
        m = re.search(r'Client Request: (\w+)', t)
        s = re.search(r'^summary: (\d+)', t, re.M)
        if m and s:
            out[m.group(1)] = int(s.group(1)) / 16000.0      # 500 blocks of 32 samples
    return out


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else '.'
    cg = costs(d)
    flagged = 0
    print('%-15s %7s  %13s  %13s  %15s  %6s  %s' % ('target', 'change', 'edge dB', 'clicks/s', 'buzz dB', 'cost', 'flags'))
    for r in csv.DictReader(open(os.path.join(d, 'targets.csv'))):
        g, a = r['g'], r['arr']
        for p in ('0', '1') if a in '56' else ('0',):
            b = load(os.path.join(d, 'b_%s_%s.wav' % (g, p)))
            m = load(os.path.join(d, 'm_%s_%s.wav' % (g, p)))
            ch, eb, em = change(b, m), edge(b), edge(m)
            cb, cm, bb, bm = clicks(b), clicks(m), buzz(b), buzz(m)
            flags = []
            if ch < -60:
                flags.append('no change' + (' (at the next note)' if p == '1' else ''))
            if em - eb > 1.0:
                flags.append('STEPS')
            c = cg.get('mod_%s_%s' % (g, p), 0) - cg.get('base_%s_%s' % (g, p), 0) if cg else None
            flagged += 'STEPS' in flags
            print('%-15s %7.1f  %5.1f -> %5.1f  %5.1f -> %5.1f  %6.1f -> %6.1f  %6s  %s' % (
                DEV[a] + ' ' + r['label'] + (' held' if p == '1' else ''), ch, eb, em, cb, cm, bb, bm,
                '%+.0f' % c if c is not None else '-', ', '.join(flags)))
    print('mod_audit: %d target(s) step under modulation' % flagged)


if __name__ == '__main__':
    main()

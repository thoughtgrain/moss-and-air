# Bryo

Bryo is firmware for the M-VAVE FM-1 that turns it into a 4-track generative sound-sculpting instrument. Each
track starts with a source (a tape loop, a synth, more to come) and runs through a fixed chain: GRAIN,
RESONATOR, COLOR and SPACE. Four modulator slots per track shape all of it, set by holding a pad and turning a
knob, with no menus.

It's a fork of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita / Hügelton Instruments, and it
keeps Felucca's hardware layer, update path and installer, which is why installing it is as easy as installing
Felucca. Thanks go upstream: see [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) and [NOTICE.md](NOTICE.md).
Felucca's own README is kept in [docs/FELUCCA-README.md](docs/FELUCCA-README.md).

## Where it stands

**Phase 2 of 10: TAPE.** Each of the four tracks has a tape: a 3.3 s loop in RAM (IMA ADPCM at 22.05 kHz) that
plays when you press PLAY. To start, track n plays factory reel n (BEAT, KEYS, AIR, PLUK, made from material in
this repository). The loop window, speed, reverse and half speed work; the white keys play 16 slices of the loop;
REC records the other tracks onto the focused track's tape, with DUB for sound on sound; holding the POLY key clears
a tape and holding SAVE undoes it. The devices after the tape (GRAIN, RESONATOR, COLOR, SPACE) and the modulators
don't make sound yet; that's phases 3 to 7. Phases 1 and 2 are verified on the host only: nothing has been built
for or run on an FM-1 yet. The plan, with what the hardware allows and the order of the work, is in
[docs/bryo-architecture.md](docs/bryo-architecture.md).

What works on the panel now (every control, in every context, with what's planned for it, is in
[docs/controls.tsv](docs/controls.tsv): tab separated, one row per control and gesture, checked by
`tests/controls_check.py`):

| Control | What it does |
| --- | --- |
| HOME | the track's source (TAPE); again for TAPE 2 and TAPE 3 (REEL: the tape or a factory reel) |
| EDIT | GRAIN; again: GRAIN 2, RESONATOR |
| FX | COLOR; again: COLOR 2, SPACE, SPACE 2 |
| LFO ENV SEQ ARP | modulator slots 1 to 4; again for the slot's next page; held + SELECT: the slot's engine (LFO, ADSR, SEQ, FOLLOW) |
| GLO, held | the mixer while held: white keys 1 to 4 pick the track, KNOB 1 to 4 set the levels |
| GLO, tapped | the mixer stays up; tap again or press a page pad to leave |
| EDIT, held on the mixer | KNOB 1 to 4 set the selected track's LOW, HIGH, FILT and PAN |
| KNOB 1 to 4 | the four values on screen |
| SELECT | the tempo |
| PLAY | start and stop: every tape plays its loop from the start |
| REC | arm the focused track: while playing it records the other tracks onto its tape (a reel is copied on first) |
| SAVE, held | undo the last tape clear |
| OCT− / OCT+ | the white keys' octave |
| White keys | the 16 slices of the focused track's loop (stopped: the slice plays once) |
| Black keys OP1 to OP4 | track mutes |
| Black keys OP5, OP6 | the focused tape's reverse and half speed |
| Black key POLY, held 0.5 s | clear the focused track's tape |
| SCL | nothing yet (proposed: hold for the system menu) |

The screen is drawn like a groovebox OLED: a grid of 2 x 2 px dots, bitmap type, white on black (your palette
setting is kept, but these screens don't use it for now).
Each knob is a pictogram that shows its value (a dial's pointer, a fader's fill, a slope's length, a keyboard
with the root lit, a feedback ring that wobbles into chaos as it rises), with a 4-letter label under it. Under the four of them, each page plots what it does in the
same dotted language: a reel-to-reel with the sample on the tape and the loop bracketed on it, the sample lit where grains read
it with one block per grain, the resonator's peaks with a node on each harmonic, a sine through COLOR (with an
inset that follows your last knob), SPACE's echoes as stems and its tail, and each modulator's shape (ADSR names
its stages, SEQ numbers its steps). The last knob you turned has its label inverted in both places.

Not there yet, and on purpose: Felucca's MENU (palette, speaker EQ, LEDs, USB serial, ABOUT and credits). Your
settings from Felucca are kept and still apply; the menu to change them comes back in a later phase.

## Files over USB

Plugged in, the FM-1 also shows up as a small drive named BRYO. Copy `TAPE1.WAV` .. `TAPE4.WAV` off to keep what
each track plays; copy a WAV on to load it: named `TAPE2.WAV` it replaces track 2's tape, any other name goes to
one of six user reels in flash (named after the file). Any WAV works (the FM-1 keeps the first 3.3 s, mono).
New files show up after you eject and plug back in; deleting a file on the computer doesn't delete the sound.
The details, and why it works this way, are in [docs/bryo-architecture.md](docs/bryo-architecture.md) under
"Files over USB".

## Building and testing

The device build needs JieLi's toolchain and SDK, as Felucca's did: see [BUILDING.md](BUILDING.md), then
`./build.sh`. The host tests need only a C compiler and Python (with Pillow and fontTools; clang and node add
checks when they're there):

```sh
pip3 install Pillow fonttools
tests/run_tests.sh
```

They type-check the firmware with clang, run the hardware layer's tests, run Bryo's chain, input and screens on
the host, check every screen against its pixel fingerprint, and test the installer. With a device build they also
test the update path against the real package.

## Licence

GPL-3.0-only, like Felucca. See [LICENSE](LICENSE), [LICENSING.md](LICENSING.md) and [NOTICE.md](NOTICE.md).

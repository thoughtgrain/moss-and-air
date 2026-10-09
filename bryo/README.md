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

**Phases 1 to 7 of 10, on the host** (p-locks still to decide). Each of the four tracks has a tape: a loop in RAM (IMA ADPCM at 22.05 kHz) that plays when
you press PLAY. The tracks share one memory (28 s in all) and each takes what it records: a blank tape grows while
REC records it, and TRACKS (GLO held + SELECT) switches tracks off to give the rest more. To start, track n plays factory reel n (BEAT, KEYS, AIR, PLUK, made from material in
this repository). The loop window, speed, reverse and half speed work; the white keys play 16 slices of the loop;
REC records the other tracks onto the focused track's tape (or one chosen track, or itself: REC IN, turn ALGORITHM),
with DUB for sound on sound; holding the POLY key clears
a tape and holding SAVE undoes it. A track can also start with SYNTH or POLY instead (hold HOME, turn SELECT):
SYNTH is a small subtractive synth, up to three voices; POLY plays any reel (or the track's own tape) across the
keys, up to four notes, each with its own envelope and filter. REC prints either onto the track's tape. GRAIN
makes sound now: turn its WET up and it keeps the last bars of the track's sound in a live buffer and granulates
them, stretched, held at a spot or delayed (SCAN); the 0 black key held freezes the buffer into a loop. RESONATOR
makes sound too: four tuned strings the track rings through (PTCH, SCAL's chord), and on its page the white keys set
the root and pluck them. COLOR
(drive, crush, noise that rides the sound, tilt) and SPACE (a delay and a small room, taking memory only while DLY or
VERB is up) make sound as well. The mixer's channel (LOW, HIGH, FILT, PAN) and a master compressor
(GLO, then EDIT, EDIT again for MASTER) shape the mix. The four modulator slots run (an LFO, an ADSR, a step sequencer,
an envelope follower): hold a slot's pad and turn any knob on a device, source or channel page to set how far that
slot moves it, and the keys, the loop and the tempo drive them. The knobs carry the Torso S-4's names where they do the same job
([docs/s4-alignment.md](docs/s4-alignment.md)). Everything so far is verified on the host only: nothing has been built
for or run on an FM-1 yet. The plan, with what the hardware allows and the order of the work, is in
[docs/bryo-architecture.md](docs/bryo-architecture.md).

What works on the panel now (every control, in every context, with what's planned for it, is in
[docs/controls.tsv](docs/controls.tsv): tab separated, one row per control and gesture, checked by
`tests/controls_check.py`):

| Control | What it does |
| --- | --- |
| HOME | the track's source (TAPE, SYNTH or POLY); again for its next page (TAPE 2, TAPE 3: REEL, ROTATE and LVL; SYNTH's FILTER, AMP, VOICE, LEVEL; POLY's ENV, FILTER, LEVEL). LVL, on every source's last page, is what the track hears of it, and can be modulated |
| HOME held + SELECT | the track's source: TAPE, SYNTH or POLY (each keeps its own knobs) |
| EDIT | GRAIN; again: GRAIN 2, GRAIN 3, GRAIN 4 (SCAN WARP OFST FDBK), RESONATOR, RESONATOR 2 |
| FX | COLOR; again: COLOR 2, COLOR 3, SPACE, SPACE 2, SPACE 3 |
| LFO ENV SEQ ARP, tapped | modulator slots 1 to 4 (the page opens as you let go); again for the slot's next page |
| LFO ENV SEQ ARP, held + a knob | that slot's depth to the knob on the page shown (-100 to 100 %); the strip shows the slot's depths while held |
| LFO ENV SEQ ARP, held + SELECT | the slot's engine (LFO, ADSR, SEQ, FOLLOW); its depths stay |
| White key held + KNOB 1, on a SEQ page | that step's value (the keys pick steps there, they don't play) |
| GLO, held | the mixer while held: white keys 1 to 4 pick the track, KNOB 1 to 4 set the levels, SELECT sets TRACKS (1 to 4 in use) |
| GLO, tapped | the mixer stays up; tap again or press a page pad to leave |
| EDIT on the mixer | KNOB 1 to 4 set the selected track's LOW, HIGH, FILT and PAN: held, while held; tapped, until tapped again |
| KNOB 1 to 4 | the four values on screen |
| SELECT | the tempo, on every view |
| ALGORITHM | REC IN: KNOB 1 to 4 set what each track's REC records (AUTO, the others, one track, itself); turned again, the focused track's |
| PLAY | start and stop: every tape plays its loop from the start |
| REC | arm the focused track: while playing it records onto its tape the other tracks (TAPE) or its own sound (SYNTH, POLY); a reel is copied on first |
| SAVE, held | undo the last clear (the tape, or the track's modulation) |
| OCT− / OCT+ | the white keys' octave |
| White keys | TAPE: the 16 slices of the focused track's loop (stopped: the slice plays once); SYNTH, POLY: notes, a semitone apart from C |
| Black keys OP1 to OP4 | track mutes |
| Black keys OP5, OP6 | the focused tape's reverse and half speed |
| Black key MONO, held 0.5 s | clear the focused track's modulation (every depth; the slots' own knobs stay) |
| Black key POLY, held 0.5 s | clear the focused track's tape |
| Black key 0 | GRAIN's freeze: held, while it's held; tapped, latched until the next tap |
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
one of six user reels in flash (named after the file). Any WAV works (mono; a tape keeps as much as free memory holds, a user reel up to 21.5 s).
New files show up after you eject and plug back in; deleting a file on the computer doesn't delete the sound.
The details, and why it works this way, are in [docs/bryo-architecture.md](docs/bryo-architecture.md) under
"Files over USB".

## Recording from the computer

With the transport stopped, hold REC for a second: the USB record mode. Everything else stops, and the FM-1 plays
whatever the computer sends it (pick Bryo as the computer's sound output). REC starts a take and REC stops it. Then the
take loops while KNOB 1-4 trim it (START, LENGTH), set its level (GAIN, or NORM past +24 dB) and fade its ends
(FADE); the screen shows how much memory it takes and how much the other tracks would have left. White key 1-4 picks
the track, and REC keeps it there and takes you back. HOME goes back a step (a take you don't want, or out). A take
is up to 23.6 s, or what memory is free.

## If it crashes

A crash shows a red BRYO CRASH screen for 4 s and restarts by itself; a hang restarts it after 8 s (the watchdog).
Turning it off and on does the same. What's in RAM is gone (the tapes, the knobs, until projects arrive in phase 8);
the reels saved to flash and the settings stay. If it crashes in the first 30 s twice in a row, it starts in the
chip's update mode instead of looping, and the web installer puts Bryo or the stock firmware back.

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

`tests/mod_audit.sh` (needs numpy; `--cost` adds valgrind) checks how every knob a modulator can move behaves:
whether it changes the sound, whether it steps at the control blocks, and what it costs.

## Licence

GPL-3.0-only, like Felucca. See [LICENSE](LICENSE), [LICENSING.md](LICENSING.md) and [NOTICE.md](NOTICE.md).

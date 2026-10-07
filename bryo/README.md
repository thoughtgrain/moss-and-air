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

**Phase 1 of 10: the skeleton.** Bryo boots on the FM-1, and installing it, going back to official V15, UPDATE
MODE and HARDWARE CALIBRATION all work as they did. The screen shows the PRD's 4-value strip for every device
and modulator slot of every track, the knobs move the values, and the white keys play a test tone through the
whole audio path. None of the devices make sound yet; that's phases 2 to 7. The plan, with what the hardware
allows and the order of the work, is in [docs/bryo-architecture.md](docs/bryo-architecture.md).

What works on the panel now (every control, in every context, with what's planned for it, is in
[docs/controls.tsv](docs/controls.tsv): tab separated, one row per control and gesture, checked by
`tests/controls_check.py`):

| Control | What it does |
| --- | --- |
| HOME | the track's source (TAPE) |
| EDIT | GRAIN, press again for RESONATOR |
| FX | COLOR, press again for SPACE |
| LFO ENV SEQ ARP | modulator slots 1 to 4 |
| GLO, held | the mixer while held: white keys 1 to 4 pick the track, KNOB 1 to 4 set the levels |
| GLO, tapped | the mixer stays up; tap again or press a page pad to leave |
| KNOB 1 to 4 | the four values on screen |
| SELECT | the tempo |
| PLAY, REC | start and stop; arm the track (recording arrives with TAPE) |
| OCT− / OCT+ | the white keys' octave |
| White keys | a test tone on the focused track |
| Black keys OP1 to OP4 | track mutes |
| SCL | nothing yet (proposed: hold for the system menu) |

Under the strip, each page draws what it does from its values: the loop window on TAPE, the grain cloud, the
resonator's response, a sine through COLOR (with an inset that follows the last knob you turned: the drive
curve, the noise or the tone filter), SPACE's echoes and tail, and each modulator's shape in its slot colour.
The last knob you turned is named at the top right of that panel.

Not there yet, and on purpose: Felucca's MENU (palette, speaker EQ, LEDs, USB serial, ABOUT and credits). Your
settings from Felucca are kept and still apply; the menu to change them comes back in a later phase.

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

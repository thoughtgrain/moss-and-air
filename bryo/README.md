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

What works on the panel now:

| Control | What it does |
| --- | --- |
| HOME | the track's source (TAPE) |
| EDIT | GRAIN, press again for RESONATOR |
| FX | COLOR, press again for SPACE |
| LFO ENV SEQ ARP | modulator slots 1 to 4 |
| SCL (the PRD's SEL), held | white keys 1 to 4 pick the track |
| GLO | the mixer: KNOB 1 to 4 are the track levels |
| KNOB 1 to 4 | the four values on screen |
| SELECT | the tempo |
| PLAY, REC | start and stop; arm the track (recording arrives with TAPE) |
| OCT− / OCT+ | the white keys' octave |
| White keys | a test tone on the focused track |
| Black keys OP1 to OP4 | track mutes |

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

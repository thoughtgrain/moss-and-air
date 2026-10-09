# Bryo: where it stands, and how to pick it up

Updated 2026-10-09. This is the page I'd want to read first after a break, or hand to anyone joining: what Bryo
is right now, how it's put together, how to build, test and measure it, what I decided and why, and what's still
open. It points into the longer docs instead of repeating them. When something here and the code disagree, the
code wins, and this page needs fixing.

## The short version

Bryo is replacement firmware for the M-VAVE FM-1, forked from Felucca 1.0.3 (`b22a24b`). It turns the FM-1 into
a four-track sound-sculpting instrument after the PRD (my product spec, which lives outside this repo; the docs
cite its sections as "PRD 2.2" and so on). Each track is a source (TAPE, SYNTH, POLY or DRUM) through a fixed chain:
GRAIN, RESONATOR, COLOR, SPACE, then a channel strip into the mix and a master compressor. Four modulator slots
per track move any of it: hold a slot's pad, turn a knob.

**Phases 1 to 7 of 10 are built, with one piece open (p-locks), and verified on the host only.** Nothing has run on
an FM-1 yet. The next real milestone is the hardware checkpoint (`docs/hardware-checkpoint.md`): flash it, read
the CPU load, listen.

| Phase | State |
| --- | --- |
| 1 Skeleton | done: boots on Felucca's hardware layer; install, UBOOT and calibration kept |
| 2 TAPE + reels | done: ADPCM tapes, factory reels, slices, REC and overdub, clear and undo |
| 3 USB audio in + SYNTH + POLY | done: the USB record mode, the SYNTH and POLY sources |
| 3b GRAIN, 3c memory by usage | done: the live buffer, one grain pool shared by demand; tapes that grow, TRACKS, REC IN |
| 4 RESONATOR | done: four strings, keys set the root |
| 5 COLOR + SPACE | done |
| 6 Mixer + routing | done: channel strips (LOW HIGH FILT PAN), master compressor; routing is REC IN |
| 7 Modulation | done but p-locks: LFO ADSR SEQ FOLLOW, hold-and-turn depths, MONO's clear and undo |
| 7b DRUM | done: a fourth source, a CR-78-inspired drum machine of Bryo's own sounds, patterns, rhythms and SEED |
| 8 Projects | not started: save and recall with reels |
| 9 Screen | mostly early: the dot-grid screens; still to come: modulation arcs, moving dots |
| 10 Tools + docs | not started: reel upload tool, installer text, a manual |

## The instrument on one page

```
 per track (x4):
   source ─► GRAIN ─► RESONATOR ─► COLOR ─► SPACE ─► channel (LOW HIGH FILT, level, PAN) ─┐
   TAPE | SYNTH | POLY | DRUM   (stereo from GRAIN on)                                     │
     ▲ REC IN: what REC prints onto the tape (the others, one track, itself)               ▼
     └──────────────────────────────────────────── sum ─► master compressor ─► MASTER ─► out
 four modulator slots per track: LFO | ADSR | SEQ | FOLLOW, each with a depth to any knob
```

- **Pages and knobs.** KNOB 1 to 4 always edit the four values on screen. HOME is the source (its pages: TAPE 3,
  SYNTH 5, POLY 4, DRUM 4, each ending with LVL); EDIT is GRAIN then RESONATOR; FX is COLOR then SPACE; LFO ENV SEQ ARP are
  the slots; GLO is the mixer (EDIT there: the channel, then MASTER); ALGORITHM turned is REC IN. A pad held +
  SELECT picks that pad's thing (HOME: the source; a slot: its engine; GLO: TRACKS). Every control in every
  context is in `docs/controls.tsv`, checked by `tests/controls_check.py`. GRAIN, RESONATOR and COLOR have ROUT:
  INS (WET crossfades) or SEND (the dry at full, the device added).
- **Modulation.** A slot's pad tapped opens its page as you let go; held, KNOB 1 to 4 set its depth to the knobs on
  the page shown (the strip shows the depths while it's held; a modulated knob gets a mark). MONO held half a
  second clears the track's depths; SAVE held undoes the last clear (tape or modulation).
- **The keys.** White keys play slices (TAPE), notes (SYNTH, POLY) or the drum kit (DRUM: on its STEP page they
  write steps, accents, live hits, or wipe), set RESONATOR's root on its page, move GRAIN's cursor on its page, and
  pick SEQ steps on a SEQ page. Black keys: OP1 to OP4 mute, OP5 reverse, OP6 half
  speed, MONO and POLY clear (held), 0 freezes GRAIN.
- **DRUM.** Sixteen synthesized instruments on the white keys, a pattern of 2 to 4 bars, ten written rhythms
  (PATN), and SEED: any value but 0 plays a version that varies the pattern the way a player would (the backbone
  kept, ghost notes, the hats thinning and filling, fills at the end), the same version every time. OCT- / OCT+ pick
  the bar; POLY held clears the pattern.
- **The USB record mode.** Stopped, REC held a second: the FM-1 plays the computer and records it; trim, level,
  fades, then a white key picks the track.

## How it runs

One core, no RTOS. Three contexts, and the rules between them are most of the architecture:

- **The audio ISR** renders a control block (CTL = 32 samples, 0.73 ms at 44.1 kHz) at a time: `chain_block` in
  `chain.c`, which calls `mod_tick`, then each track's source and devices, the mixer, the master stage. The main loop
  can't interrupt it. It sheds load above 85 % (grains first, then strings; never the tape).
- **TIMER5** scans the input and services USB. It can nest inside the audio ISR only for the USB audio endpoints'
  service (`uac_service`, `uaco_service`); `usb_poll` never runs nested.
- **The main loop** owns everything that allocates: the shared memory, the depth lists, the UI. Its rule for memory
  shared with the ISR: shorten the list (or set the flag) first, publish (`RING_PUBLISH`), then free. For anything
  the ISR reads as a whole (a depth list), build it in the half the ISR isn't reading and flip one byte.

**Memory.** Sound lives in one pool of 152 chunks (28 s of tape-format sound: `mem.c`) handed out by use: tapes as
long as what's on them, GRAIN's live buffers, RESONATOR's strings and SPACE's lines while they're on. Nothing is
set aside per track. When it runs short: a cleared tape's chunks first, then a parked track's tape, then the end of
the longest tape. Main RAM holds 86.7 of 96 KiB (.bss), the pool 326.2 of 336 KiB (2026-10-09 with DRUM, 32-bit
build: the firmware preprocessed, its inline assembly taken out, compiled `-m32 -Os`, `size -A`).

**Parameters.** `param.c` holds every knob as an integer in its own range (`tp[t]`), with a descriptor (name,
range, format) per knob. Up to 20 knobs a device or engine, in pages of four. The main loop writes them; the ISR
reads them once per block. With modulation, the device code reads through `TPD(t, array)` instead: a pointer to
`tp`'s own array, or, when that array has depths, to a copy with the depths added (`mod.c`). Pitches and filter
cutoffs also get a finer value than the knob holds (`mod_pitch16`, `mod_fine8`).

**Fixed point throughout,** no 64-bit arithmetic and no compiler builtins on the target (the JieLi compiler would
turn them into library calls). Q15 audio, Q12 and Q10 gains, Q8 for fractions of a knob or a sample.

## The files

All under `bryo/`. `firmware/src/bryo.c` is the one translation unit (it includes every module in order, as
Felucca's `felucca.c` did); `tests/bryo_host.c` includes the same files for the host.

| File | What |
| --- | --- |
| `firmware/src/bryo.h` | what the main loop and the ISR share (`sys`, `track[]`), the keys' layout |
| `dsp.c` | fixed-point building blocks: the state-variable filter, oscillators, soft clip |
| `master.c` | the output stage: DC block, speaker EQ, limiter, USB level |
| `param.c` | every knob: descriptors, defaults, formats; the device, source and engine tables |
| `mod.c` | modulation: the depths, the lists, the clock, the four engines, the fine values |
| `mem.c` | the shared sound memory: chunks and owners |
| `tape.c` | TAPE: ADPCM tapes, reels, the head, REC and overdub, clear and undo |
| `synth.c`, `poly.c`, `source.c` | SYNTH, POLY, and the source table |
| `drum_voice.c`, `drum.c` | DRUM: the one synthesizer every instrument shares; the kit, the rhythms, SEED's versions, the player |
| `grain.c` | GRAIN: the live buffer, the scheduler, the shared pool of grains |
| `reso.c`, `color.c`, `space.c` | RESONATOR, COLOR, SPACE |
| `mixer.c` | the channel strips and the master compressor |
| `usbrec.c` | the USB record mode (the endpoint itself is in `usb.c`) |
| `chain.c` | a control block of the whole instrument; TRACKS; shedding; the main loop's memory polls |
| `reel.c`, `vdisk.c`, `msc.c` | user reels in flash; the FM-1 as a USB drive (FAT12, WAV in and out) |
| `ui.c`, `ui_input.c`, `ui_px.c`, `ui_viz.c` | the screen's regions; buttons, keys and knobs into actions; the dot grid and pictograms; the pictures |
| kept from Felucca | `main.c`, `audio.c`, `usb.c`, `panel.c`, `settings.c`, `storage*.c`, `console.c`, `ota*.c`, `lcd.c`, `gfx.c`, `midi_uart.c`, the HAL in `firmware/hal/` |

Tests and tools:

| File | What |
| --- | --- |
| `tests/run_tests.sh` | everything below that's quick, plus Felucca's kept hardware and installer tests; ends "ALL HOST TESTS PASSED" |
| `tests/bryo_host.c` | Bryo's chain, input and screens on the host: 473 checks, and every screen rendered |
| `tests/bryo_golden.txt`, `ui_golden.py` | each screen's pixel fingerprint |
| `tests/checkpoint_sim.sh` | the hardware checkpoint on the host: 28 runs rendered to WAV, each one's cost under callgrind, and a stress test with the audio and USB interrupts cutting into the main loop while the memory's books are checked |
| `tests/mod_audit.sh` | every knob a modulator can move: does it change the sound, does it step at the blocks, what it costs (`--cost`) |
| `tests/controls_check.py`, `coverage.py` | the controls map; line coverage of Bryo's files |
| `tools/gen_reels.py`, `gen_tables.py` | the factory reels; the generated tables (`build/gen/`) |

## Building, testing, measuring

```sh
tests/run_tests.sh                       # the host suite (C compiler, Python with Pillow and fontTools)
sh tests/checkpoint_sim.sh 20            # the 28 runs, their cost (valgrind), 20 s of interrupt stress
sh tests/mod_audit.sh [--cost]           # how modulation behaves, knob by knob (numpy)
python3 tests/ui_golden.py update tests/bryo_golden.txt build/bryo_ui   # after a deliberate screen change
./build.sh                               # the device image: needs JieLi's toolchain and SDK (BUILDING.md)
```

The type check the suite runs is worth running alone after any firmware change: `clang -fsyntax-only -m32
-ffreestanding -fno-builtin -Wall -Wno-unused-function -Ifirmware/hal -Ifirmware/src -Ibuild/gen
firmware/src/bryo.c` (no warnings is the bar). For memory-safety, build `tests/bryo_host.c` or
`tests/checkpoint_sim.c` with `-fsanitize=address,undefined -fno-sanitize=shift`; `CHECKPOINT_SLOW=1` slows the
stress test's timers for the sanitizer.

**The unit of cost** is host instructions per output sample of `chain_block`, under callgrind, because I can't
measure the device here. Where it stands (`checkpoint_sim.sh`, 2026-10-09):

| Run | What | Cost |
| --- | --- | ---: |
| 1 | four reels, nothing on | 938 |
| 19 | a realistic four-track groove | 1,615 |
| 24 | the groove, modulated the way I'd play it | 1,798 (84 of it the modulators) |
| 21 | the groove's busiest moment | 2,454 |
| 23 | GRAIN at its densest on one track | 2,986 |
| 25 | every slot on every track, 128 depths | 3,127 |
| 26 | DRUM busy alone (FUNK, VARY 100, FILL 100, long decays), the others plain reels | 1,301 |
| 27 | the groove with a DRUM track in place of its drum reel | 1,885 |
| 28 | four DRUM tracks at once, every voice (20) | 2,474 |
| 15 | everything on, all four tracks | 4,909 |

My target for a realistic four-track scene is 2,000; the device's real ratio of host instructions to CPU load is
the question the hardware checkpoint answers (one reading of run 1 turns all of these into percentages).

**Flash:** the image runs in place from 568 KiB. On the same host measure (LLVM, `-Os`, 32-bit), Bryo is now 139 KB
of code and 159 KB of constants (302 KB; it was 281 on 10-08, before modulation and the LEVEL pages). Allowing the
device's code to be up to 1.34 times bigger, that leaves roughly 230 to 280 KB free. `./build.sh` gives the real
number.

## The rules I work by

- Every new file starts with `/* SPDX-License-Identifier: GPL-3.0-only */` (or `#` for scripts).
- Never touch the update loader's path (`ota_hw.c`, the loader). The HAL is header-only and I only add to it.
- No 64-bit math and no builtins in firmware code.
- The ISR never allocates and never waits; the main loop owns memory (the rule above).
- Measure before optimizing, and measure what's heard, not a proxy. The modulation audit taught me that the hard
  way: my click counter read a saw's own edges as clicks, and I nearly shipped smoothing that cost 25 to 50 a sample
  for nothing. The block-edge test is the one that tells.
- A sound change is deliberate: the screens have fingerprints, the checkpoint has its numbers, and a default that
  changes what you hear gets said in the commit.
- Docs are first person, casual, and say how and why. The architecture doc keeps an "as built" section per part.

## Decisions, and why (the ones that shape everything)

- **2026-10-07: a source per track, a fixed chain after it.** The PRD's chain stays fixed; what starts it is an
  interface (`source_t`), so adding a source doesn't touch the devices. TAPE, SYNTH, POLY and DRUM so far.
- **2026-10-07: tapes are IMA ADPCM, mono, 22.05 kHz.** Four times the time per byte of 16-bit, cheap to decode
  in the ISR; the decoded quality suits a sound-sculpting instrument.
- **2026-10-07: the screen is a dot grid** (2 x 2 px dots, one ink), drawn per region only when it changes.
  Readable at a glance, small in RAM, and it gave back 59.5 KB of the pool.
- **2026-10-07: line up with the Torso S-4** where Bryo does the same job: names, knob order, what a modulator
  has (`docs/s4-alignment.md`). The S-4's WAVE and RANDOM are one LFO here, random a shape of it, so every shape
  gets the random knobs. SEQ is the PRD's own addition.
- **2026-10-08: memory by usage.** No per-track budgets: one shared pool, given by what's in use, taken back in a
  fixed order. Tapes grow as they record (up to 23.6 s).
- **2026-10-08: GRAIN's grains are one pool shared by demand** (32 for one track, 24 for two, 16 for three or
  four), split by what each track's settings ask for.
- **2026-10-08: the USB record mode is its own mode.** Everything else stops while the computer is recorded;
  REC held a second opens it. Audio out to the computer in that mode is future work.
- **2026-10-09: modulation reads a modulated copy only of what's modulated.** A track without depths reads exactly
  what it did; an array with depths is copied and summed once a block. The first version copied whole tracks and
  cost 7 KB of main RAM.
- **2026-10-09: every source has a LEVEL** on its last page, as the S-4 does; that needed five pages a device
  (NPK 20).
- **2026-10-09: GRAIN, RESONATOR and COLOR can be sends** (ROUT: INS or SEND, per device, per track): the dry at
  full, the device added at WET. Per track rather than shared aux buses: no new memory or CPU, every track keeps
  its own effects. SPACE was always a send.
- **2026-10-09: DRUM's sounds and rhythms are Bryo's own.** CR-78-inspired in the instrument list and the
  character, written from scratch: one data-driven voice, a patch row per instrument. A first pass reused
  Felucca's drum voices; I replaced them so the kit is ours. "CR-78" stays in the docs, never on screen.
- **2026-10-09: SEED is a pure function of (version, bar, step, instrument).** No state, so the same SEED always
  plays the same thing, the screen can draw what will play, and an LFO or SEQ on SEED just picks versions. Each
  instrument varies by its role (the backbone kept, the time thinned and filled, the colour ornamented), and only
  with instruments the pattern already uses.
- **2026-10-09: pitches and filter cutoffs modulate finer than their knobs** (1/256 of a step), so vibrato and
  resonant sweeps glide; a SEQ's steps on a pitch still land on semitones.

## Known considerations and open questions

- **P-locks** (a SEQ step's own depth): waiting on a decision. SEQ's step values are in.
- **SPRD on PAN** pulses (a tremolo) at 100 instead of moving the sound, because PAN is a balance. Left for now; the
  options are in the architecture doc ("Modulation, as built", open points). My lean: let SPRD reach every knob of
  COLOR and the channel strip, the S-4's way.
- **VEL** (ADSR) does nothing: the keys have no velocity, and MIDI in doesn't reach the tracks yet.
- **FOLLOW's USB source** listens to nothing until the computer's audio comes in outside the record mode.
- **LFO RATE with SYNC BPM** prints 0..127, not the division it picked (best done with phase 9's strip work).
- **SEQ steps on TUNE** are a percentage of 48 semitones; a step page in semitones would be friendlier.
- **The device itself:** the CPU load is unknown (the hardware checkpoint), the USB OUT endpoint (EP4) is untested
  on hardware, macOS might pick Bryo as its default output when plugged in, and the flash size is an estimate.
- **The channel's FILT bottoms out near -29 dB** at its lowest corner (the filter's precision); inaudible under
  the passband, but it isn't infinite.
- **DRUM's kit is voiced by measurement** (pitch, level, decay) and waits on a listen: the hats and maracas are
  bright (energy to 17 kHz), the bass drum sits low for a small speaker, the cymbal rings 9 s at its design.
- **DRUM's FILL and EVOL** read "no change" in the modulation audit: they act at the pattern's end, past its 3 s
  window. Not a fault, but the audit can't see them.
- Still open from the plan: what the white keys do on a SYNTH track while GRAIN's page is up (they play notes).

## What's next

1. **The hardware checkpoint** (`docs/hardware-checkpoint.md`): build, flash, read the CPU on the run sheet,
   listen. Everything after depends on those numbers.
2. **Phase 8, projects:** save and recall a session with its reels (the flash layout is in the architecture doc).
3. **Phase 9, the screen's modulation:** arcs on the pictograms, the moving dots, the summed value.
4. **DRUM by ear:** listen to the kit and the rhythms, tune `DRM_KIT` and `DRM_PRESET`.
5. Decisions waiting: p-locks, SPRD on PAN.

## Where the details are

| Doc | What it holds |
| --- | --- |
| `README.md` | what Bryo is, what works on the panel, building and testing |
| `docs/bryo-status.md` | this page |
| `docs/bryo-architecture.md` | the plan against the hardware, the budgets, and an "as built" section per part: how each one works, what it costs, how it's tested, and what I changed my mind on |
| `docs/controls.tsv` | every control, in every context, with its status and phase |
| `docs/s4-alignment.md` | Bryo's devices and modulators against the Torso S-4's, knob by knob |
| `docs/hardware-checkpoint.md` | the host's numbers, and the run sheet for the first time on the device |
| `docs/ui-assessment.md` | the screen's design review |
| `docs/screens/bryo_screens.png` | every screen, as the host renders it |
| `docs/FELUCCA-README.md`, `NOTICE.md`, `ACKNOWLEDGEMENTS.md` | where it came from, and the GPL notice |

## How it got here

About 50 commits over three days, each one a working step (`git log --oneline -- .`):

- **2026-10-07:** the plan; phase 1 on Felucca's hardware layer; the dot-grid screen and a picture per page; TAPE
  and the factory reels (phase 2); user reels in flash and the FM-1 as a USB drive; SYNTH and POLY; the S-4
  alignment.
- **2026-10-08:** GRAIN (and its live buffer); memory by usage, TRACKS, REC IN; a review round; the hardware
  checkpoint's run sheet and its host simulation; RESONATOR, COLOR, SPACE; the USB record mode; the mixer;
  realistic load runs; GRAIN's shared pool.
- **2026-10-09:** modulation; TUNE played live; a LEVEL on every source; the modulation audit (RESONATOR's pitch
  glides, fine pitch); fine filter cutoffs; this page; DRUM (its own voice and kit, the source, SEED); sends
  (ROUT).

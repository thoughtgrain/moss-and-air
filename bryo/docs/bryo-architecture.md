# Bryo architecture: from the PRD to the FM-1

Written 2026-10-07. This is my plan for turning the PRD (Bryo: a 4-track generative sound-sculpting
instrument) into firmware on the FM-1. Nothing here is built yet. The PRD is the goal; this doc is where I
check it against what the hardware can actually do, decide what to keep from Felucca, and set the order to
build in.

## Decisions so far (2026-10-07)

| Question | Decision |
| --- | --- |
| Where audio comes from | All of it: reels (factory + uploaded), resampling inside Bryo, **and USB audio in from the computer, early** (phase 3), **plus synth engines**. |
| What starts a track's chain | **A source engine per track**, picked like a modulator engine: TAPE, SYNTH, POLY, and engines still to be decided. The source flows into GRAIN, RESONATOR, COLOR and SPACE. |
| Tape format | IMA ADPCM, mono, 22.05 kHz, about 3.3 s per track. |
| Gestures where the hardware differs | Adapt what makes sense; **ask before changing anything else**. The remaps below stand. |
| MONO and POLY keys | Hold for 0.5 s with a countdown ring, plus one level of undo. |

**POLY (decided):** a polyphonic sample player: the white keys play a sample chromatically, and each voice has its
own envelope and filter before the track's chain. Its three HOME pages and their knobs are under "Source engines".
(The black key named POLY, which clears the tape, is a different thing; the screen always says "POLY SOURCE" for
the engine.)

Still open: what the white keys do on a SYNTH track while GRAIN or another tape-style page is focused.

## What the hardware really gives me

I had the Felucca source read for facts before planning, because a few of them change the design:

| Fact | Where it comes from | Why it matters |
| --- | --- | --- |
| **No audio input.** I2S out to the codec only; the ADC reads only the MASTER pot and the battery; USB audio is device to computer only. | `hal/fm1_audio.h`, `hal/fm1_adc.h`, `src/usb.c` | TAPE can't record a mic or line in. It records what Bryo itself makes (see "Where TAPE's audio comes from"). |
| **RAM: 344 KiB pool + 96 KiB .data/.bss**, no external RAM. | `app.ld` | Four live tapes have to share about 150 KiB. That sets the tape format. |
| **Flash: 1 MiB.** The app area is 0x4000–0x92FFF; Felucca's data region is 0x97000–0xDFFFF (296 KiB). | `hal/fm1_flash.h`, `src/storage.c` | Saved projects with audio compete for about 280 KiB. |
| **Flash writes block interrupts.** Erase and program run with IRQs off; audio drops out. | `hal/fm1_flash.h`, `src/storage_hw.c` | Saving tapes happens with the transport stopped, as Felucca already does. |
| **7 relative encoders, no push switches:** SELECT, ALGORITHM, PRESETS, KNOB 1–4. | `src/panel.c`, `hal/fm1_input.h` | The PRD's "push SELECT", and PRESETS and ALGORITHM as buttons, need another gesture. |
| **14 buttons:** FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC, plus OCT− OCT+. | `src/panel.c` | The PRD's 12 pads match. The PRD's **SEL** pad is the one printed **SCL**. |
| **27 keys:** 16 white, 11 black (F3..G5), one single-colour LED each. | `hal/fm1_input.h` | Colour identity for the modulator slots lives on the screen only. |
| **MASTER:** an analog pot read by the ADC and turned into a digital gain. | `src/main.c` | Works as the PRD says. |
| **Screen:** 240×240 RGB565 over SPI at 12 MHz; anti-aliased text; knob arcs from a precomputed mask. | `src/lcd.c`, `src/gfx.c`, `src/ui_graph.c` | The 4-value strip with arcs is buildable on what's there. |
| **CPU clock:** not documented. Felucca renders 128-frame halves (2.9 ms) and sheds voices at 85% load. | `src/audio.c` | I budget in host instructions per sample (Felucca's method) and measure on the device early. |
| **I can't build device firmware here.** The JieLi toolchain host (pkgman.jieliapp.com) and the SDK host (gitee.com) are blocked by this environment's network policy. | | I verify on the host (tests, renders, instruction counts). You build and flash with `./build.sh`. |

## The PRD, adjusted to the hardware

These are my proposals. Each keeps the PRD's intent and only changes the gesture or the scale.

1. **SELECT has no push.** To assign an engine to a modulator slot, *hold the slot's pad and turn SELECT*:
   the engine list opens and scrolls, and releasing the pad commits the choice. The same gesture
   (hold, turn, release) is how depth is set, so there's one idiom to learn.
2. **PRESETS and ALGORITHM are encoders.** Turning PRESETS opens the project manager and scrolls its slots;
   turning ALGORITHM opens the routing matrix and moves through its cells. Inside both views, **OCT+
   confirms** (load, or toggle a route) and **OCT− backs out**, which is the idiom Felucca users already
   know. Pressing any device or modulator pad also leaves the view.
3. **The black keys, left to right:** F#3 G#3 A#3 C#4 = **OP1–OP4** (mutes), D#4 = **OP5** (reverse),
   F#4 = **OP6** (half speed), G#4 = **PIT** (pitch down), A#4 = **GLO** (pitch up), C#5 = **MONO**,
   D#5 = **POLY**, F#5 = **0** (freeze). That's the PRD's order applied to the physical order. The code
   doesn't record the printed labels, so this needs checking against the panel.
4. **MONO and POLY are destructive** (clear modulation, clear the tape). I'd make them *hold for half a
   second* with a countdown ring on screen, and keep one level of undo (SAVE held, as in Felucca). A brushed
   key shouldn't erase a take.
5. **Colour identity is screen-only.** The LEDs are single colour: a pad's LED shows focus (lit) and hold
   (blinking). Slot colours (cyan, amber, green, magenta) appear on the dials and headers. For colour-blind
   users, each slot also gets a glyph (1–4 in a small pill) wherever its colour appears, so colour is never
   the only cue.
6. **64 grains per track becomes 64 grain slots, with a measured cap on how many sound at once.** Felucca's
   GRAIN engine runs 36 grains in total. 256 at once is very unlikely to fit next to four resonators and
   four Space devices. DENSITY schedules into 64 slots, but only the first *N* sound (N = 16 per track to
   start, tuned on the device) and the scheduler drops the oldest. It sounds like 64-grain density at
   normal settings and stays inside the budget at extremes.

## Source engines: what starts each track

Each track's first device is a **source engine** in the HOME slot. TAPE is one source; SYNTH is another;
more can be added without touching the rest of the chain. In the code it's an interface like Felucca's
`engine_t`, but for a whole track:

```c
typedef struct {
    const char *name;                         /* "TAPE", "SYNTH", … shown in the header */
    void (*init)(track_t *t);
    void (*key)(track_t *t, uint32_t key, uint32_t vel, int on);   /* white keys in this source's mode */
    void (*render)(track_t *t, int32_t *mono, uint32_t n);         /* one control block, mono into GRAIN */
    const param_desc_t *knobs[4];             /* what KNOB 1-4 do on the HOME page for this source */
} source_t;
```

How it works: the HOME pad focuses the source; holding HOME and turning SELECT picks the source engine
(the same hold-and-turn idiom as modulator engines). Why an interface: the PRD's chain stays fixed, the
sound-making front end stays open, and a new engine is one file plus one table row.

- **TAPE:** the ADPCM looper below. White keys are 16 slices.
- **SYNTH:** a simple subtractive voice built from Felucca's ANALOG engine (`eng_analog.c`: oscillators,
  filter, envelope), trimmed to a few voices. White keys are chromatic, and OCT−/OCT+ shift octaves.
- **POLY:** a polyphonic sample player. A key starts a voice (4 per track) that plays a sample from flash at the
  key's pitch, through its own ADSR and its own state-variable filter, which the envelope also sweeps by ENV AMT;
  the voices sum into the track's chain. It reads samples straight from flash, as Felucca's SAMPLE engine did
  (ADPCM through the XIP window), so it costs no tape RAM: about 100 bytes of state per voice. White keys are
  chromatic; OCT-/OCT+ shift. HOME steps through three pages (as EDIT and FX toggle theirs):

  | Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
  | --- | --- | --- | --- | --- |
  | SAMPLE | REEL (which sample) | START | TUNE | VOICES (1..4) |
  | ENV | ATK | DEC | SUS | REL |
  | FILTER | CUTOFF | RES | TYPE (LP, BP, HP) | ENV AMT (-100..100) |

  The modulator slots can still move CUTOFF (or anything else on these pages) for the whole track.
- **Later engines:** to be defined; each is one file plus a row in the `source_t` table.

Any source's output can still be printed onto the track's tape with REC, so every track keeps a tape
buffer even when TAPE isn't its source.

## Where TAPE's audio comes from

With no input, a tape records what the instrument makes. That's still a lot:

- **Reels in flash:** factory sounds built into the app (Felucca's generated drums and the CC0 instrument
  samples are already here), plus your own audio uploaded over USB with `tools/fm1_sample_upload.py` or the
  browser (record in the browser, upload, as Felucca does today). Any reel loads into any tape.
- **Resampling inside Bryo:** REC records the track's own chain output (sound on sound through
  RESONATOR, COLOR and SPACE back onto the tape). ALGORITHM routes another track's output into this track's
  tape, which is the PRD's live overdub between tracks.
- **Sources that make sound from nothing:** RESONATOR played from the keys (Karplus-Strong excited by a
  noise burst) and COLOR's noise generator. A blank tape plus RESONATOR plus REC is a way to start from
  silence.
- **USB audio from the computer (decided: early).** A UAC1 OUT endpoint (computer to FM-1), 44.1 kHz
  16-bit stereo, into a ring buffer the audio ISR reads. The work: a second streaming interface in the
  descriptors (kept apart from the existing IN endpoint, so macOS and Windows still see one class-compliant
  device), clock drift between the computer and the codec handled by nudging the read rate, and an
  underrun that plays silence instead of clicking. It becomes an INPUT choice for any track's tape.

## The tape format, and why

How I'd store it: **IMA ADPCM, 4 bits per sample, mono, 22,050 Hz, in 256-sample blocks with a
predictor/step index per block.**

Why:

- **Length.** 16-bit stereo 44.1 kHz would give each track about a third of a second. ADPCM at 22.05 kHz
  is 11 KB per second, so 36 KiB per track gives about **3.3 s per track** (more at half speed).
- **It's the format Felucca's user samples already use** (`eng_sample.c`, `tools/fm1_sample_upload.py`).
  Loading a reel from flash into a tape, or saving a tape, is a straight copy with no transcoding, and the
  upload tools keep working.
- **Random access.** Slices, grains and reverse playback start decoding at a block boundary (at most 255
  samples to skip), as Felucca's GRAIN engine already does with its seek index.
- **Overdub that readers can't catch half-written.** The write head decodes the old block, mixes in the new
  audio, re-encodes it into a 128-byte staging block, and commits the block and its index entry together.
  A grain reading the same spot sees either the old block or the new one, never a mix.
- **Stereo comes later in the chain.** GRAIN's spread, RESONATOR and SPACE are stereo; the tape itself is
  mono. That's the trade for length.

## Budgets

### RAM (the 344 KiB pool)

| Use | Bytes | How it's sized |
| --- | ---: | --- |
| Tapes | 147,456 | 4 × 36 KiB ADPCM (3.3 s each at 22.05 kHz) plus 4 × 1.3 KiB block index |
| RESONATOR | 25,600 | 4 tracks × 4 strings × 800 samples × 2 B (lowest note A1, 55 Hz) |
| SPACE delay | 65,536 | 4 tracks × 8,192 samples × 2 B (0.37 s at 22.05 kHz, or 0.74 s at half speed) |
| SPACE reverb | 40,000 | 4 small reverbs, Felucca's ROOM structure run at 22.05 kHz |
| Screen canvas | 29,760 | 240×62 strip (Felucca uses 240×124, 59 KiB); the UI draws in strips |
| GRAIN state | 4,096 | 4 × 64 slots × 16 B |
| USB audio in | 16,384 | ring buffer: 4,096 stereo frames × 4 B (93 ms) for drift and jitter |
| SYNTH | ~2,000 | 4 tracks × a few voices of oscillator and filter state |
| Spare | ~13,000 | build.py already enforces at least 8 KiB free; SPACE delay is the first thing to shrink |

What gives way if the budget breaks: SPACE delay length first, then tape length. Both are single
constants (`TAPE_BYTES`, `SPACE_DLY_LEN` in `src/bryo_config.h`).

### Flash (Felucca's 296 KiB data region, rewritten as Bryo's)

| Area | Bytes | What |
| --- | ---: | --- |
| Projects | 8 × 4 KiB × 2 (A/B) = 64 KiB | Machine state: devices, mixer, routing, modulators, p-locks, which reel each tape holds |
| Reel slots | 6 × 36 KiB = 216 KiB | Saved tapes and uploaded audio, all the same format; a project points to reels |
| Settings | unchanged | 0xFC000–0xFEFFF, as Felucca |

A project saves its four tapes into reel slots it owns (so saving with audio needs four free reels), or it
can save "state only" and point at reels that already exist.

### CPU

I'll budget in Felucca's unit: host instructions per 44.1 kHz output sample, measured with callgrind,
because I can't measure the device here. Felucca's heaviest single engine is about 1,500; the device copes
with its mixes. My target for all four tracks, everything on, is **at most 2,000**:

| Stage, per track | Estimate | Notes |
| --- | ---: | --- |
| TAPE read | 20 | ADPCM decode at 22.05 kHz, linear interpolation to 44.1 kHz |
| GRAIN | 240 | 16 sounding grains × about 15 (window table, interpolation, pan) |
| RESONATOR | 60 | 4 strings |
| COLOR | 30 | drive table, crush, follower plus noise |
| SPACE | 80 | delay plus reverb at 22.05 kHz |
| Modulators | 10 | control rate (every 32 samples), not per sample |
| **Track total** | **~440** | **× 4 = ~1,760**, plus the mixer and compressor ~60 |

The audio ISR's 85% guard stays. Instead of shedding voices it sheds *grains* first (lowers the sounding
cap), then RESONATOR strings, and never the tape itself.

## What I keep, reuse, and remove

**Kept as is (the hardware and the install path, untouched):**

- `firmware/hal/*`, `firmware/crt0.S`, `firmware/app.ld` (pool sizes aside)
- `firmware/loader/*`, `src/ota.c`, `src/ota_hw.c`: the update loader and the M-UPGRADE path, byte for
  byte, so installing stays exactly as easy as it is now
- `src/usb.c` (MIDI, USB audio out, console, the UBOOT and M-UPGRADE SysEx entries), `src/storage.c` and
  `src/storage_hw.c` (A/B flash), `src/lcd.c`, `src/gfx.c`, `src/libc.c`, `src/midi_uart.c`,
  `src/midi_clock.c`, `src/console.c`, `src/settings_persist.c`
- `src/main.c`'s boot: watchdog, boot-loop guard, OCT− + OCT+ UBOOT countdown, hardware calibration
- `src/panel.c`: button, key and encoder mapping with calibration
- `src/audio.c`'s DMA and ISR frame, timing and load meter (the render call inside changes)
- Tools: `tools/build.py`, `tools/fm1pkg_make.py`, `tools/fm1_install.py`, the web installer
  (`web/index_pkg.html`, `web/fm1ota.js`, `web/fm1pkg.js`, `web/make_site.py`), the font, icon and palette
  generators

**Reused as material (code moved into the new devices, credits kept):**

| From | Into | What |
| --- | --- | --- |
| `eng_sample.c`, `eng_grain.c` | `tape.c`, `grain.c` | ADPCM decode, the seek index, grain windows |
| `phys_dsp.c` (DaisySP port), `phys_symp.c` (Rings port) | `reso.c` | the Karplus-Strong string, damping, sympathetic coupling |
| `eng_lofi.c`, `dsp.c` | `color.c` | bit and rate reduction, drive tables |
| `fx.c` | `space.c` | the delay and ROOM reverb (SPRING maybe later) |
| `slice_store.c`, `gen_samples.py` | `reel.c`, factory reels | the flash slot format, the bundled sound set |
| `ui_draw.c`'s `card_t`, `ui_graph.c`'s `knob()` | `ui_strip.c` | the 4-value strip and arcs |
| `editor.c` (sample transfer only) | `link.c` | uploading reels over USB SysEx |

**Removed** (they stay in git history; this is a full rewrite and none of it fits Bryo):

- the 13 synth engines (`eng_*.c`, `fm6_core.c`, `fm4_convert.c`, `up_fm6.c`, `drum_voice.c`), `voice.c`
  and its note allocator
- the note sequencer, chords, arpeggiator, song chain, motion, the old mod matrix, perform layer, slicer
  (`seq.c`, `chord.c`, `song_chain.c`, `motion.c`, `mod.c`, `perform.c`, `slicer.c`)
- presets, favorites and user preset banks (`upreset.c`, `favorites.c`, `params.c`'s pages)
- Felucca's UI (`ui*.c`) and the web editor (`web/editor.html`) with its parameter protocol
  (`editor*.c`); the editor protocol is entirely Felucca's parameters
- their tests, the golden sound renders and CPU baselines, replaced by Bryo's own

## The new module map

All under `bryo/firmware/src/`. `bryo.c` replaces `felucca.c` as the single translation unit (the same
unity-build approach), and `tools/build.py` points at it.

```
bryo.c            unity build: config, HAL, then every module in order
bryo_config.h     the budgets as constants (TAPE_BYTES, GRAIN_SLOTS, GRAIN_SOUNDING, …)
bryo.h            shared types: track_t, device params, mod slots, the project struct

audio engine (audio ISR, per 32-sample control block)
  tape.c          ADPCM tape: record, overdub, loop start/length, speed/direction, 16 slices
  grain.c         64-slot grain scheduler reading the tape; size, density, pitch, spread
  reso.c          Karplus-Strong network: root, feedback, damping, mix; chromatic keys
  color.c         drive, crush, follower-driven noise and its tone
  space.c         per-track delay and small reverb
  mixer.c         track level, pan, filter, master compressor, ALGORITHM routing into tapes
  chain.c         one track = source → grain → reso → color → space; the 4 tracks; grain shedding
  source.c        the source_t table; picking a track's source engine
  synth.c         SYNTH: a small subtractive voice (from eng_analog.c)
  usb_in.c        USB audio in: the OUT endpoint ring buffer and drift correction

control
  mod.c           4 slots per track; engines WAVE, RANDOM, ADSR/FOLLOW, SEQUENCER; depths; sums
  plock.c         per-step locks for SEQUENCER slots
  transport.c     PLAY/STOP, tempo, MIDI clock in/out (wraps midi_clock.c)
  param.c         the parameter table: 5 devices × 4 knobs per track + mixer; ranges, curves, names

storage
  reel.c          reel slots in flash and factory reels in the app image; load and save a tape
  project.c       8 project slots (A/B); save and recall full state
  link.c          USB SysEx: reel upload and download (what the sample tools call)

UI (main loop)
  ui.c            focus (device or modulator), holds, key modes, views (project, routing, mixer)
  ui_input.c      buttons, keys and encoders into actions; hold-to-assign; p-lock gestures
  ui_strip.c      the 4-value strip: base arcs, modulation arcs, motion dots, summed white dot
  ui_head.c       the dynamic header ([TRACK 2 : COLOR], [MOD 1 : WAVE]), transport, tempo
  ui_views.c      project manager, routing matrix, global mixer
```

Tests, under `bryo/tests/`, all host-run:

- `tape_test.c`: record, overdub, slices and reverse; the ADPCM round-trip error; a reader never sees a
  half-written block
- `grain_test.c`, `reso_test.c`, `color_test.c`, `space_test.c`: stability at the parameter corners, no
  DC, no clipping, the cost per sample
- `mod_test.c`, `plock_test.c`: each engine's shape, depth summing, locks per step
- `budget_test.c`: callgrind instruction counts for the whole chain, 4 tracks, worst case, against
  `budget.txt`
- `ui_render.c` (new), `ui_golden.py`, `ui_screens.py`: every Bryo screen drawn and fingerprinted, as I did
  for Felucca's
- Kept from Felucca: `input_test`, `storage_test`, `usb_desc_test`, `uac_test`, `ota_test`, `ldr_test`,
  `install_test.py` and the installer's web tests, so the hardware and install paths stay covered

## Phase 1, as built (2026-10-07)

Done: `bryo.c` builds the kept hardware layer with Bryo's core, and Felucca's instrument is removed (96 files).

| File | What it is |
| --- | --- |
| `firmware/src/bryo.c` | the unity build |
| `firmware/src/bryo.h` | the shared state (`sys`), frame sizes, the key layout (white 0..15, black OP1..0) |
| `firmware/src/chain.c` | the four tracks per control block; a test voice per track for now; shedding |
| `firmware/src/master.c` | Felucca's output stage, moved from `fx.c` unchanged |
| `firmware/src/param.c` | five devices and four modulator slots per track, four knobs each, with the PRD's names |
| `firmware/src/settings.c` | the settings record, kept in Felucca's layout so calibration and USB prefs carry over |
| `firmware/src/ui.c`, `ui_input.c` | the header, the 4-value strip with dials, the footer; pads, keys, knobs, LEDs (the track tiles moved into the mixer: see "The screen, revised") |
| `firmware/src/panel.c` | the panel map plus HARDWARE CALIBRATION (moved here from Felucca's UI) |
| `tests/bryo_host.c` | the chain, the input mapping, the settings carry-over, every screen in every palette |
| `tests/bryo_golden.txt` | the screens' pixel fingerprints |
| `tests/run_tests.sh` | everything above plus a clang type check of the firmware and the kept hardware tests |

How I verified it without the JieLi toolchain: `clang -fsyntax-only -m32` over `bryo.c` in every build-flag
combination (zero diagnostics; the unchanged Felucca tree also gives zero, so it's a fair stand-in for the
compiler's front end), `build.py`'s register-access rules, the kept hardware tests, and Bryo's own host tests.
**Not verified: a device build and a run on the FM-1.** That needs `./build.sh` on a machine with the toolchain.

The installer also learned one thing: on a device running Bryo, "Return to official V15" skips Felucca's backup
step (Bryo has no Felucca-format data) and asks for a confirmation instead.

Porting the material files: they're gone from the tree but not from history. For example
`git show 6aa6073:moss/firmware/src/eng_grain.c` prints Felucca's GRAIN engine as imported.

Still open from phase 1: Felucca's MENU is gone (palette, speaker EQ, LEDs, HOLD, USB serial, ABOUT with the
source QR code). The settings it wrote still apply. **Which gesture opens Bryo's system menu is a question for
you** (Felucca used HOME held; Bryo's HOME is a device pad).

## The screen, revised (2026-10-07)

After phase 1, two changes to the screen, from your review:

- **Track picking moved under GLO.** Hold GLO: the mixer comes up and white keys 1 to 4 pick the track (KNOB 1
  to 4 set the levels); let go and you're back on the page. Tap GLO and the mixer stays up. The PRD's SEL (the
  SCL pad) is free; I propose holding it for the system menu.
- **The space the track tiles took is a visualization per page** (`firmware/src/ui_viz.c`), drawn from the
  page's values: TAPE's loop window, the sample lit where GRAIN's grains read it (solid blocks in a stereo lane), RESONATOR's response, COLOR's sine through the device,
  SPACE's echoes and tail, each modulator's shape in its slot colour, and the tracks in the mixer. The page's
  last-turned knob is drawn in the accent and named in the panel's caption with its value, and on COLOR the
  inset follows it (the drive curve, the noise, or the tone filter). The DSP will use the same mappings the
  pictures do, so what's drawn is what you'll hear.
- **The screen is a groovebox OLED now: a dot grid, bitmap type, pictograms that carry the value.** The first
  pass (icons, tags, filled shapes in anti-aliased type) didn't land; what you wanted was the Syntakt/Digitakt
  screen, so I rebuilt the main screens on a 120 x 120 grid of 2 x 2 px dots (`firmware/src/ui_px.c`). Why a
  grid: at 240 px on a 1.5" panel, a 1 px stroke is a hairline, while a 2 px dot is about the size of an
  Elektron OLED's pixel, and drawing everything on the same grid is what makes the type, the pictograms and the
  plots read as one system.
  - **Type** is two bitmap faces I drew as ASCII art in `tools/gen_px_font.py` (generated into
    `build/gen/ui_pxfont.h` like the other headers): 5 x 7 for labels and values (doubled to the right for the
    bold titles) and 3 x 5 for small print. Square corners on purpose.
  - **One ink.** Everything is the palette's THEME colour plus a dim version of it (40 % from the background),
    so the palette still picks the screen's character (amber, green, paper...). Emphasis is inversion, never
    colour: the last-turned knob's label is inverted in the strip and in the plot under it. A modulator slot's
    colour only survives as a 1-dot stripe under its `M1`..`M4` box.
  - **The header** is a box with the track (`T1`) or slot (`M2`), the page in bold, and an inverted tempo bar
    holding REC, the transport and the metronome, like the references.
  - **The strip** is four pictograms, and each one is the value: a dial's pointer, a fader's fill, a slope's
    length, a keyboard with the root key lit, chevrons for tape speed, stacked takes for DUB, a dot field for
    density. FDBK's dial ring turns to chaos as feedback rises: a slow wobble first, then jitter near the top, the
    way a loop close to self-oscillation smears. Every point stays within 2 dots of the circle, so however wild it
    gets it never leaves its box or touches its neighbours. The label sits under it (4 letters, Elektron-style: `STRT`, `TUNE`, `DRIV`, `SWNG`), the value
    under that in the dim ink.
  - **The plot below** speaks the same language (TAPE is a reel-to-reel with the reels close over the middle, so it
    reads in proportion; the run along the bottom is the whole tape with the sample drawn on it, lit inside the
    loop window and dim outside; the first version was a dashed strip that read as a road): 1-dot lines, square nodes where a value sits, dotted drop
    lines and guides, 3 x 5 labels under the stretch they name (the ADSR page is the clearest example of the
    pattern). Messages show as an inverted box over the plot, so the title never moves.
  - **Kept as it was:** the region signatures (a region redraws only when what it shows changes), the
    mappings each plot uses (they're still the DSP's), and the update path. UPDATE MODE moved onto the grid
    too; HARDWARE CALIBRATION keeps Felucca's look since it's a one-time setup screen.
- **The mixer carries its meters in the faders, and each track has a channel strip.** On the mixer the four
  pictograms are faders: two notches show the level you set, and the fill inside is the track's live meter, so
  one glance gives both. That freed the panel below for the selected track's channel: LOW and HIGH shelves
  (+-12 dB), FILT (one knob, DJ-style: low-pass to the left of 0, high-pass to the right, 0 is open) and PAN.
  Below the faders they're drawn as one response curve over 8 octaves with a node on each corner, plus two
  speaker bars for the pan. Hold EDIT on the mixer and KNOB 1-4 set them; let go and you're back on the levels.
  The values live per track in `tp[].ch` (`firmware/src/param.c`) and the curve is `ch_resp()` in
  `firmware/src/ui_px.c`, which is the response the mixer DSP will apply in phase 6. The PRD's master
  compressor, which the old plan had on this page, still needs a home; I left it for phase 6.

The controls are mapped in `docs/controls.tsv` (one row per context, control and gesture, with status and
phase), validated by `tests/controls_check.py`, which also fails if a physical control goes unmentioned.

## Build order

Each phase ends in something you can flash and hear or see, and each is its own commit series.

| Phase | What | Done when |
| --- | --- | --- |
| 1. Skeleton (**done**, host-verified) | `bryo.c` boots on the kept hardware layer; the old app code is removed; silence plus a test tone; the header and an empty strip; install, UBOOT and calibration still work | it installs from the web installer and returns to stock |
| 2. Sources + TAPE + reels | the source_t interface; tapes play factory reels; slices on the white keys; REC and overdub (resampling); the upload tool fills reel slots | you can load, slice, record and overdub a loop |
| 3. USB audio in + SYNTH + POLY | the UAC OUT endpoint, drift handling, INPUT = USB; the SYNTH source; the POLY source (voices with their own envelope and filter, three HOME pages) | you can record your computer, and play the synth and samples onto a tape |
| 3b. GRAIN | the scheduler, the sounding cap, FREEZE (key 0) | grains run on 4 tracks inside the budget |
| 4. RESONATOR | strings, chromatic keys, OCT shifts | tuned feedback chords from the keys |
| 5. COLOR + SPACE | drive, crush, noise; delay and reverb | the full chain on 4 tracks inside the budget |
| 6. Mixer + routing | GLO mixer, filters, compressor; ALGORITHM routes track to tape | track-to-tape overdub between tracks |
| 7. Modulation | the 4 engines, hold-and-turn depth, assigning engines, p-locks | the PRD's §4 workflow end to end |
| 8. Projects | save and recall with reels; quick SAVE; undo for MONO and POLY | a power cycle brings a session back |
| 9. Screen | arcs, slot colours and glyphs, motion dots, the summed white dot, headers | the PRD's §5 |
| 10. Tools + docs | the upload tool for reels, the installer text, a Bryo manual | someone else can use it |

Phases 2 and 7 are the riskiest: the tape format, and whether the modulation sum fits at control rate. If
either needs rethinking, it'll show up early.

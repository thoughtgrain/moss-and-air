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
| Tape format | IMA ADPCM, mono, 22.05 kHz; tapes take what they record from one shared memory (28 s in all; see "Memory by usage"). |
| Gestures where the hardware differs | Adapt what makes sense; **ask before changing anything else**. The remaps below stand. |
| MONO and POLY keys | Hold for 0.5 s with a countdown ring, plus one level of undo. |

**POLY (decided):** a polyphonic sample player: the white keys play a sample chromatically, and each voice has its
own envelope and filter before the track's chain. Its three HOME pages and their knobs are under "Source engines".
(The black key named POLY, which clears the tape, is a different thing; the screen always says "POLY SOURCE" for
the engine.)

Still open: what the white keys do on a SYNTH track while GRAIN or another tape-style page is focused (for
now they play the synth's notes on every page).

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
more can be added without touching the rest of the chain. In the code (`source.c`) it's a table of render
functions, one row per engine; the knobs and the names live in `param.c` beside the devices':

```c
typedef struct {
    /* one block of track t into out; keys: the white keys held (0 when the track isn't the focused one, or
     * the source isn't the track's any more) */
    void (*render)(uint32_t t, uint32_t keys, int32_t *out, uint32_t n);
} source_t;
```

I first sketched it with `init`, `key` and `knobs` hooks too. In practice every engine wants the keys as a
bitmask once per block (the ISR reads them, so a note starts within 0.7 ms), its knobs come from `dev_p()` /
`dev_v()` in `param.c`, and nothing needed an init the zeroed state didn't already give.

How it works: the HOME pad focuses the source; holding HOME and turning SELECT picks the source engine
(the same hold-and-turn idiom as modulator engines). Why an interface: the PRD's chain stays fixed, the
sound-making front end stays open, and a new engine is one file plus one table row.

- **TAPE:** the ADPCM looper below. White keys are 16 slices.
- **SYNTH:** a simple subtractive voice built from Felucca's ANALOG engine (`eng_analog.c`: oscillators,
  filter, envelope), trimmed to a few voices. White keys are chromatic, and OCT−/OCT+ shift octaves.
- **POLY:** a sound played across the keys, up to four notes at once, each through its own ADSR and filter
  (see "POLY, as built").
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
  is 11 KB per second, so 36 KiB per track gave about **3.3 s per track** (more at half speed). Since
  "Memory by usage" the tracks share the RAM instead: 28 s in all, up to 23.6 s on one tape, four tracks whatever
  they record.
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

### RAM (the 344 KiB pool, and main RAM's spare room)

| Use | Bytes | How it's sized |
| --- | ---: | --- |
| Shared sound memory | 270,336 pool + 50,688 main RAM | 152 chunks of tape-format sound (28.2 s in all), handed out by usage: tapes as long as what's on them, GRAIN's live buffers while GRAIN is on, RESONATOR's strings and SPACE's lines while they're on. See "Memory by usage" |
| RESONATOR | 4 chunks a track, while on | 4 strings × 1,056 samples × 2 B (lowest string F1, 43.7 Hz), from the shared memory while WET is up |
| SPACE delay | 8 chunks a track, while DLY is up | 8,192 samples × 2 B at 22.05 kHz (TIME's 370 ms); 4 chunks at 8 bits when memory is short |
| SPACE reverb | 10 chunks a track, while VERB is up | the room (a comb a chunk, the allpasses in a fifth) and the pre-delay's line (5 chunks; 3 at 8 bits) |
| COLOR | 0 | 64 B of state a track |
| Screen canvas | 5,460 (main RAM) | 2 bits a dot (1.6 KB) and two 4-row line buffers (3.8 KB); see "The screen as dots" |
| GRAIN | 9,728 pool + 2,900 main RAM | 32 grains × 304 B (a 128-sample decoded window each), and each track's buffer bookkeeping; the buffers themselves are shared memory |
| USB record mode | 12,288 pool + 900 main RAM | the monitor's ring (1,024 stereo frames, 23 ms) and the take's (4,096 mono samples, 186 ms); the take itself is shared memory |
| SYNTH | 576 | 4 tracks × 3 voices of oscillator and filter state (.bss) |
| POLY | 8,288 | 4 tracks × 4 voices × a 256-sample block reader (pool), plus 448 B of voice state (.bss) |
| The drive | 17,340 | the FAT, root and write cache of the USB drive (the 36 KB WAV inbox is gone: a WAV lands in shared memory) |

Measured now (KiB, 32-bit build): the pool holds 316.7 of 336 (19 spare; build.py keeps 8), main RAM's .bss 80.2
of 96. The USB record mode's two rings first went into main RAM and took it to 92.2, too close to the stack, so
they live in the pool's room that was kept for USB audio in.

What gives way when memory runs short: chunks come off a cleared tape first, then a parked track's tape (TRACKS),
then the end of the longest tape. Nothing is set aside per track, so there's no constant to shrink any more.

### Flash (Felucca's 296 KiB data region, rewritten as Bryo's)

| Area | Bytes | What |
| --- | ---: | --- |
| Projects | 8 × 4 KiB × 2 (A/B) = 64 KiB | Machine state: devices, mixer, routing, modulators, p-locks, which reel each tape holds |
| Reel slots | 6 × 40 KiB = 240 KiB | Saved tapes and uploaded audio, all the same format; a long one spans the slots after it (21.5 s at most); a project points to reels |
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
| GRAIN | 240 | 8 sounding grains × about 30 (decoding, window ramp, interpolation, pan), plus about 100 recording its buffer (measured: see "GRAIN, as built") |
| RESONATOR | 60 | 4 strings (measured nearer 240 a track on the host: see "RESONATOR, as built") |
| COLOR | 30 | drive table, crush, follower plus noise (measured 180 a track with every knob on, 0 at the defaults: see "COLOR, as built") |
| SPACE | 80 | delay plus reverb at 22.05 kHz (measured 220 a track with both on: see "SPACE, as built") |
| Modulators | 10 | control rate (every 32 samples), not per sample |
| **Track total** | **~440** | **× 4 = ~1,760**, plus the mixer and compressor ~60 |

The audio ISR's 85% guard stays. Instead of shedding voices it sheds *grains* first (lowers the sounding
cap), then RESONATOR strings, and never the tape itself. COLOR and SPACE aren't shed (yet; see "SPACE, as built").

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
  usbrec.c        the USB record mode: the OUT endpoint's frames, the monitor, the take (usb.c: the endpoint)

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
| `tests/bryo_host.c` | the chain, the input mapping, the settings carry-over, every screen (one ink: one set) |
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
  - **One ink: white on black, fixed for now.** I first let the palette pick the ink (amber, green, paper...),
    but you wanted the look settled before any colour comes back, so these screens ignore the palette setting
    (it's still stored, so nothing is lost when a palette comes back). There's a dim ink for guides and
    unlit things; values are full ink, since dim numbers were hard to read. Emphasis is inversion and borders,
    never colour: the last-turned knob's label is inverted in the strip and in the plot under it, and REC is a
    solid dot in the tempo bar.
  - **The header** is a box with the track (`T1`) or slot (`M2`), the page in bold, and an inverted tempo bar
    holding REC, the transport and the metronome, like the references.
  - **The strip** is four pictograms, and each one is the value: a dial's pointer, a fader's fill, a slope's
    length, a keyboard with the root key lit, chevrons for tape speed, stacked takes for DUB, a dot field for
    density. FDBK's dial ring turns to chaos as feedback rises: a slow wobble first, then jitter near the top, the
    way a loop close to self-oscillation smears. Every point stays within 2 dots of the circle, so however wild it
    gets it never leaves its box or touches its neighbours. The label sits under it (4 letters, Elektron-style: `STRT`, `TUNE`, `DRIV`, `SWNG`), the value
    under that in the dim ink.
  - **The plot below** speaks the same language (TAPE is a reel-to-reel with the reels close over the middle, so it
    reads in proportion, and no tape path from the reels down, which only cluttered it; the run along the bottom is the whole tape with the sample drawn on it, lit inside the
    loop window and dim outside; the first version was a dashed strip that read as a road): 1-dot lines, square nodes where a value sits, dotted drop
    lines and guides, 3 x 5 labels under the stretch they name (the ADSR page is the clearest example of the
    pattern). Messages show as an inverted box over the plot, so the title never moves.
  - **Kept as it was:** the region signatures (a region redraws only when what it shows changes), the
    mappings each plot uses (they're still the DSP's), and the update path. UPDATE MODE moved onto the grid
    too; HARDWARE CALIBRATION keeps Felucca's look since it's a one-time setup screen.
- **The mixer carries its meters in the faders, and each track has a channel strip.** On the mixer the four
  pictograms are faders: two notches show the level you set, and the fill inside is the track's live meter, so
  one glance gives both. The selected track's fader has a border, and the channel panel below is tagged with the
  same track, so it's clear whose EQ and filter you're looking at. That freed the panel below for the selected track's channel: LOW and HIGH shelves
  (+-12 dB), FILT (one knob, DJ-style: low-pass to the left of 0, high-pass to the right, 0 is open) and PAN.
  Below the faders they're drawn as one response curve over 8 octaves with a node on each corner, plus two
  speaker bars for the pan. Hold EDIT on the mixer and KNOB 1-4 set them; let go and you're back on the levels.
  The values live per track in `tp[].ch` (`firmware/src/param.c`) and the curve is `ch_resp()` in
  `firmware/src/ui_px.c`, which is the response the mixer DSP will apply in phase 6. The PRD's master
  compressor, which the old plan had on this page, still needs a home; I left it for phase 6.
- **SEQ shows its steps' values, and the keyboard got simpler.** The SEQ page drew 16 identical blocks with the word
  SEQUENCE under them, which said nothing the header didn't. Now each step is a bar as tall as its value, with
  the steps past LEN left as dim slots, SLEW drawn as the glide between values and SWING nudging the off-beats.
  For that the slot needs values, so each SEQ slot stores 16 (`tp[].steps`, 0..100) with a default pattern that
  accents the beats; it moves nothing until the slot is assigned a depth, and setting a step's value (hold the
  slot, hold a white key, turn a knob) arrives with phase 7. RESONATOR's ROOT pictogram is now one outline with
  dividers and thin black keys, the root key filled (or a black one hollow), instead of a key-by-key drawing.
- **Second pages, reached by pressing the same pad again.** The PRD gives each device exactly four knobs, and
  all of them were on screen. What was missing is what the sound needs that the PRD never gave a knob, so I
  added a page 2 where it matters. Pressing a pad again steps through its pages: EDIT goes GRAIN, GRAIN 2,
  RESONATOR, GRAIN; FX goes COLOR, COLOR 2, SPACE, SPACE 2; HOME and the slot pads toggle page 1 and 2. The
  header shows the page as a block and a dot after the title.

  | Page 2 | Knobs | Why |
  | --- | --- | --- |
  | TAPE 2 | FADE, REV, HALF, GAIN | the loop's crossfade (no clicks at the seam); OP5/OP6's reverse and half speed, now visible; the record gain |
  | GRAIN 2 | MIX, JIT, WIN, REV | without a dry/wet GRAIN always replaces the tape; where grains land, their window, the chance one plays backwards |
  | COLOR 2 | LVL, MIX, SRR, GATE | drive adds loudness with nothing to take it back; a blend; the rate split off CRSH (which keeps the bits); where the noise's envelope opens |
  | SPACE 2 | DMIX, RMIX, PRE, WIDE | the PRD gives no wet level for the delay or the reverb; the reverb's pre-delay; the stereo width |

  (Since renamed to the Torso S-4's names, with more pages: see `docs/s4-alignment.md`.)

  RESONATOR stays at one page: its four knobs already cover it. (The modulators' pages are under "Modulators".) Every page 2 knob also shows in the
  picture below, not only as a number: GAIN scales (and clips) the sample on the tape, FADE draws ramps at the
  loop's ends, REV and HALF turn and halve the chevrons; WIN rounds the grains, REV makes some hollow, JIT adds
  whiskers; COLOR's curve and wave include MIX and LVL, and GATE draws its line over the noise; SPACE's echoes
  and tail scale by their mixes and the tail waits for PRE; the LFO starts at PHAS, fades in, shows the beat ticks
  when locked, the key when restarted and its next time round dotted; ADSR bends each stage by its curve and
  draws the loop and the softest key's envelope; FOLLOW draws its envelope over what it listens to; SEQ hollows the steps PROB skips and marks STRT. A page 2 knob you turn is tagged with its value.
  Switch-like values (REV, LOOP, TRIG, CLK, MODE, DIR, SHPE) print their names, not numbers.
- **Modulators, lined up with the Torso S-4.** I checked the S-4's modulator reference (Torso's docs for WAVE,
  RANDOM, ADSR and FOLLOW, and its CC map) and gave Bryo's engines the same knobs, with one change you asked for:
  the S-4's WAVE and RANDOM are a single LFO here, and random is one of its shapes. Why: they share everything
  but the shape (rate, phase, depth, offset, stereo spread, sync), so one core is less to learn and less code,
  and it means the random knobs (SMTH, VAR, LEN) work on every shape: a square that glides, a sine whose level
  drifts each cycle. The engines are now LFO, ADSR, SEQ (the PRD's) and FOLLOW; hold the focused slot's pad and
  turn SELECT to pick one (this was phase 7's plan; it's in now because FOLLOW can't be reached any other way).

  | Engine | Page 1 | Page 2 | Page 3 | Page 4 |
  | --- | --- | --- | --- | --- |
  | LFO | RATE, SHPE (SIN TRI SQR SAW RND), SKEW, FOLD | CURV, SMTH, VAR, LEN | AMT, OFS, PHAS, SPRD | SYNC, TRIG, FADE |
  | ADSR | ATK, DEC, SUS, REL | ACRV, DCRV, RCRV, SPRD | VEL, LOOP, AMT, OFS | |
  | SEQ | LEN, RATE, SLEW, SWNG | DIR, TRIG, PROB, STRT | | |
  | FOLLOW | SRC (SELF, T1-T4, USB), GAIN, RISE, FALL | HOLD (sample and hold), AMT, OFS, SPRD | | |

  VAR is the S-4's VARIATION: each time round the loop drifts from the last by up to that much (0: the same loop
  forever); on RND that's the steps themselves, on the other shapes each cycle's level, repeating every LEN
  cycles. SMTH is a slew on any shape. SPRD is the right channel's phase against the left, drawn as a dim second
  trace. AMT scales the slot as a whole (the S-4's AMOUNT); the PRD's hold-and-turn still sets each target's
  depth. The ARP pad's slot starts as the LFO on RND with a little VAR, so the random modulator is still one press
  away. ADSR's MODE/SENS from the last pass went to FOLLOW, where they belong. What I couldn't confirm from the
  S-4 docs: FOLLOW's exact knob names (the docs describe a source, rise and fall times and sample and hold), so
  those four names are mine.

The controls are mapped in `docs/controls.tsv` (one row per context, control and gesture, with status and
phase), validated by `tests/controls_check.py`, which also fails if a physical control goes unmentioned.

## Phase 2, as built (2026-10-07)

The tape works end to end on the host: `firmware/src/tape.c`, with `tests/bryo_host.c` checking the codec round
trip (40 dB on a sine), the loop window, reverse, a slice played while stopped, a recorded take, clear and undo, and
the refusal that protects a take. What it does and why:

- **Each track has a RAM tape and can play a reel instead.** Four factory reels live in flash
  (`tools/gen_reels.py` makes them, in the tape's own format: BEAT from the generated drums, KEYS and AIR from
  the CC0 piano and flute, PLUK from the generated plucks; about 95 KB of flash). TAPE 3's REEL picks TAPE or a
  reel. A reel plays straight from flash, so browsing reels never touches your tape; the first REC copies the reel
  onto the tape (and REEL turns to TAPE), and is refused if the tape already holds a take, so nothing is lost by
  accident.
- **Reading is block-cached.** A reader decodes one 256-sample block and then reads any position in it, so
  speed, reverse and slices cost one block decode per 256 samples. A second reader carries the old head through
  a crossfade whenever the head jumps: at the loop's seam (FADE long, at least 2 ms) and on a slice.
- **Writing commits whole blocks.** REC stages the block under the head, mixes each sample (old x DUB + new x
  GAIN), and re-encodes the block from its own stored state when the head leaves it. Other blocks keep their
  states, and a reader re-decodes a block after it's committed, so it hears the old block or the new one.
- **What REC records, for now: the other three tracks' mix** (one control block late, 0.7 ms). With no audio
  input that's the meaningful source in this phase; ALGORITHM (phase 6) makes the routing a choice, and the
  track's own device output joins when the devices exist.
- **Clearing keeps the data until it's overwritten.** POLY held 0.5 s marks the tape empty (a reel: back to the
  tape, cleared); SAVE held brings it back, as long as REC hasn't written over it. That costs no RAM for an undo
  copy.
- **The keys are slices everywhere for now.** The phase 1 test tone is gone. White key n jumps the head to
  slice n of the loop while playing, and plays just that slice while stopped. RESONATOR (phase 4) and SEQ (phase 7)
  will give the keys their own jobs on their pages.
- **The screen draws the real sound.** The tape view, GRAIN and FOLLOW read each block's peak, which the
  encoder stores with the block; the tape view names what plays (a reel, TAPE or EMPTY), shows REC armed, and
  cuts the moving head through the sound while it plays.

RAM: the four tapes are 152 KB of the pool (data, block states and peaks), the readers and writers 6 KB; with the
screen canvas that's about 218 KB of the 344 KB pool.

Not in this phase, on purpose: **user reel slots in flash and the upload tool** (they need the flash layout that
projects use, so they move to phase 8 with projects), and **the `source_t` interface** (TAPE is the only source
until SYNTH and POLY arrive in phase 3, which is when the interface earns its keep).

## Files over USB: the FM-1 as a drive (2026-10-07)

You asked for projects and sounds to be easy to move when the FM-1 is plugged in, both ways, instead of an upload
tool. So the FM-1 shows up as a small USB drive named BRYO, next to its audio and MIDI ports. Copy files off to
keep them; copy a WAV on to load it. Built and tested on the host; not tried on a computer yet.

What's on it (`firmware/src/vdisk.c` makes the volume on the fly; nothing is stored twice):

| File | What it is |
| --- | --- |
| `TAPE1.WAV` .. `TAPE4.WAV` | what each track plays now (its tape or its reel) |
| `REEL1.WAV` .. `REEL4.WAV` | the factory reels (read-only) |
| `USER1.WAV` .. `USER6.WAV` | your reels, the slots that hold a sound |
| `README.TXT` | how the drive works, in short |

How it's built, and why:

- **No real file system, a made-up one.** The flash has 296 KiB for data and blocks interrupts while it writes, so a
  real FAT on it would be tiny and would stall the audio on every write. Instead the volume (FAT12, 63.5 MiB, 16 KiB
  clusters) is generated when it's read: the WAVs are decoded from the tapes' ADPCM as the computer reads them, at
  22,050 Hz mono 16-bit. Each file gets a 2 MiB region, so a bigger file written over it still lands in one run.
- **Writing is best effort, and I say so on the drive.** A drive never hears "here is a file called X": it sees
  sectors. A sector that starts with a WAV header starts a capture; the sectors after it (and wherever the FAT
  chains the file next) are converted as they arrive (any rate, 8 to 32-bit PCM or float, any channels, to the
  tape format) into the shared memory (it was a 36 KB RAM inbox until "Memory by usage"). The directory entry that points at it names it: `TAPEn.WAV` replaces track n's
  tape; `USERn.WAV` replaces your reel n; any other name goes to the first free user reel, named after the file;
  a file never named in the root (dropped into a folder) goes to a free reel after 3 s. The FAT and directory the
  computer writes are kept in RAM and read back as written, so its view holds until you eject.
- **Your reels live in flash** (`firmware/src/reel.c`): six slots of 40 KiB in Felucca's user-sample area, in the
  tape's format, played straight from flash. A slot's header is written last as the commit record, so a save torn
  by a power cut leaves the slot empty, never half old and half new. TAPE 3's REEL lists them by name.
- **USB:** a Mass Storage interface (SCSI, bulk-only) on EP3, where the serial console was. The console needs EP3
  too, so it's now a debug build (`BRYO_MSC=0`). The transport is `firmware/src/msc.c` (no registers, tested with a
  RAM disk in `tests/usb_msc_test.c`); `usb.c` adds the descriptors and the EP3 glue. The device presents audio +
  MIDI + the drive with device class 0 and its own bcdDevice (3.x9), so a computer doesn't reuse what it learned
  about another layout. The update loader doesn't build any of it and stays byte for byte as it was.

What to expect:

- **Speed:** one 64-byte packet each way per poll (2 kHz): about 128 KB/s, so a 3.3 s WAV copies in a few seconds.
- **New files appear after you eject and plug back in.** The drive doesn't tell the computer its contents changed.
- **Deleting a file in Finder or Explorer doesn't delete the sound.** Clear it on the FM-1 (POLY held).
- **A WAV is kept as long as free memory allows (21.5 s at most as a user reel); stereo is mixed to mono.** That's
  the tape's format. (It was cut to 3.3 s until "Memory by usage".)
- **Saving a user reel stutters the sound for about a second** (the flash turns interrupts off while it writes),
  and the screen says SAVING.
- **What can still go wrong:** a WAV scattered across an old, full volume whose FAT is written after its data can
  arrive garbled. Copying onto a freshly plugged-in drive avoids it.

Recording from the computer (USB audio in) is next, then a Bluetooth feasibility check: the radio's stack is
JieLi's closed binary, and linking it into GPL-3.0 firmware is a licensing question to settle before any code.

## SYNTH, as built (2026-10-07)

SYNTH is the first source engine after TAPE, and it's what made `source_t` real.

**Files:** `synth.c` (the voice), `source.c` (the table), `param.c` (`SYN_P`, `tp[t].src`, `tp[t].syn`,
`dev_p` / `dev_v` / `dev_name`), `chain.c` (`chain_source`: the crossfade and REC), the pictograms in `ui_px.c`,
the picture in `ui_viz.c` (`viz_synth`), the source picked in `ui_input.c`.

**The voice.** It's Felucca's ANALOG engine, trimmed. There are two band-limited oscillators of one wave (SIN TRI
SQR SAW, and PLS, a 25 % pulse), the second DTUN cents up and blended in by MIX, plus white noise (NOIS) and drive
(DRV, up to 3x into the soft clip). Then comes the resonant trapezoidal low-pass, and one ADSR that sets the level
and, by ENV (-100..100), moves the cutoff up to 96 steps of the cutoff scale (about 8 octaves). KTRK makes the
cutoff follow the keys. I kept one envelope because two would need a page I don't have. ENV going negative gives
the "closing" sweeps a second envelope is usually for.

**Pages** (HOME steps through them, as EDIT and FX do theirs):

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| OSC | WAVE | DTUN (ct) | MIX | NOIS |
| FILTER | CUT (shown in Hz) | RES | ENV | KTRK |
| AMP | ATK | DEC | SUS | REL (shown in ms / s) |
| VOICE | VOIC (1..3) | GLID | DRV | TUNE (st) |

The times and the cutoff print in real units. A new pair of formats, `F_TIME` and `F_HZ`, read Felucca's
`TIME_MS_X10` and `CUTOFF_HZ`, so "392Hz" on the strip is the frequency the filter really sits at.

**Voices.** There are three per track (`SYN_NV`). A new key takes a silent voice, else the quietest releasing one,
else the oldest. A stolen voice keeps its phases and filter state and attacks from where its level is, and every
block ramps the level from the last block's, so neither a steal nor a fast envelope clicks. With VOIC at 1 it
plays legato. A second key glides the one voice there (GLID), and letting the top key go glides back to the
highest key still down.

**Keys.** White key k is the semitone k above C of the track's octave (OCT- / OCT+), plus TUNE. That's sixteen
semitones, because the black keys are the PRD's macros. The VOICE page prints the range the keys cover ("C3-D#4").

**Picking it.** Hold HOME and turn SELECT, the same idiom as a modulator slot's engine. Each track keeps both
TAPE's and SYNTH's knobs, so switching back and forth loses nothing.

**Switching never clicks.** `chain_source()` keeps a gain per source per track and ramps the old one out and the
new one in over about 2 ms. A source that isn't the track's renders nothing. A synth note held across a switch
waits, and releases the moment the track switches back and the key is seen up. I tried "keep rendering until it's
silent" first. It meant a TAPE track switched away kept decoding its loop forever while the transport ran, unheard.

**REC on a SYNTH track prints the synth onto its tape.** The tape renders last in `chain_source()`, so it can
take the sum of the other source as its input: play a line, switch to TAPE, and slice it. (On a TAPE track REC
still records the other tracks.)

**A fix to the tape it uncovered.** Recording a sine through the synth showed a click at every 256-sample block
boundary of the take. `tape_commit()` re-encoded a recorded block starting from the decoder state of the sound
that was there before: a different level and step size, so each block began wrong until the encoder caught up.
Now a re-encoded block starts from its own first sample, with the step size of its first move. Phase 2's REC had
the same bug (its test only compared block peaks, so it passed). The synth test now checks the take's pitch to
under 1 Hz, which the bug made 240 Hz instead of 220.

**Cost.** I haven't measured it on the FM-1 yet. Per voice per sample there are two oscillators, a noise step
when NOIS is up, and one filter, with coefficients once per block. That's the same work as a Felucca ANALOG
voice, and Felucca ran eight of those under its 85 % shedding line. Twelve voices (three per track, all four
tracks on SYNTH) is more than that, so `chain_shed()` will need to take the oldest releasing voices once it's
measured. RAM: 44 bytes a voice, 576 bytes for all four tracks.

**Tests** (`tests/bryo_host.c`, `test_synth`): picking the source and its pages; silence without a key; A3 from
white key 10 at OCT 3 (220.0 Hz) and A4 at OCT 4 (440.0 Hz); the release to a free voice; three keys, three voices,
and the fourth taking one; VOIC 1's glide up and back; the filter darkening a saw; REC printing the note onto
the tape (220.0 Hz back off it); and the crossfade from SYNTH to TAPE with no step bigger than the note's own.
The five screens are in the golden set.

## POLY, as built (2026-10-07)

POLY is the third source engine: a sound played across the keys, up to four notes at once.

**Files:** `poly.c` (the voices), `param.c` (`POL_P`, `tp[t].pol`), `source.c` (its row), `tape.c` (`tape_view_of`
and `tape_name_of`: a view of any reel choice, not only the one the track's TAPE plays), `dsp.c` (`tsvf_mode`: the
SVF's band-pass and high-pass outputs), `ui_px.c` (its pictograms), `ui_viz.c` (`viz_poly`, and `vz_filter`, which
SYNTH's filter page now shares).

**What it plays.** REEL is the same list TAPE's REEL is: the track's own tape, the factory reels, your reels. A
voice reads straight from where the sound is: flash through the XIP window, or the RAM tape. So POLY copies nothing
and costs no tape RAM, and choosing TAPE means you can record a phrase onto the track and then play it as an
instrument. Each voice has its own 256-sample block reader (tape.c's), 520 bytes each, 8 KB for all sixteen.

**Pitch.** White key 1 at OCT 3 plays the sound as it was recorded: the head reads 0.5 tape samples per output
sample, as TAPE does at 100 %. Each key above is a semitone higher (the step comes from `pitch_inc`, so it's exact
to the table). TUNE moves the whole range; the SAMPLE page prints it ("C3-D#4").

**Each voice:** starts at STRT, runs the ADSR (the same block-rate envelope as SYNTH's), and passes a
state-variable filter: TYPE LP, BP or HP, with ENV moving the cutoff by the envelope. With the filter wide open as
LP (the default) it's skipped, so an untouched POLY plays the sound bit for bit. At the sound's end a voice fades
over one block and frees itself, even with the key held. Voices are taken the way SYNTH takes them.

**Pages:**

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| SAMPLE | REEL | STRT | TUNE | VOIC (1..4) |
| ENV | ATK | DEC | SUS | REL |
| FILTER | CUT | RES | TYPE (LP BP HP) | ENV (-100..100) |

**Tests** (`test_poly`): a 440 Hz sine saved as a user reel plays at 440.0 Hz on key 1, 659.2 on key 8 and 880.0 on
key 13; four keys take four voices and a fifth takes the oldest; VOIC turned down releases the voices above it;
STRT 50 starts half way, and a held note plays to the end and frees itself; LP and HP each take the sine down; REEL
TAPE plays nothing while the tape is empty and the sound once it holds one. Three screens are in the golden set.

**A budget problem it brought into view.** The pool now holds 282 KB of its 344 KB: the tapes (152 KB), the screen
canvas (59.5 KB, twice what the budget table planned), the drive's WAV inbox (38 KB, which the plan didn't have)
and POLY's readers (8 KB). That leaves about 62 KB, and phases 3b to 5 plan about 151 KB more (RESONATOR, SPACE,
GRAIN, the USB audio ring). Something has to give before RESONATOR lands. The candidates are: the inbox sharing the
canvas's memory (it's only in use while a WAV arrives), a shorter SPACE delay (the plan's first choice), and a
smaller canvas.

## The screen as dots (2026-10-08)

Bryo drew every screen strip into Felucca's full-colour canvas: 240 x 124 pixels of RGB565, 59.5 KB of the pool.
But every Bryo screen is a grid of 2 x 2 px dots in three shades. I checked all 58 renders: four colours in all,
and every dot one colour, except the UPDATE MODE countdown, whose lower half sat one pixel off the grid. So the
canvas stored 16 bits a pixel to hold 2 bits a dot.

**Now:** `ui_px.c` keeps the strip as dots, 2 bits each: background, dim, ink, and a spare fourth colour (the last
other colour drawn). `px_blit` turns them into the LCD's pixels only as they're sent, two dot rows at a time into
one of two small line buffers: one fills while the other goes out (the LCD has one transfer in flight at a time,
so the buffer being filled is never the one being sent). The dots take 1.6 KB, the line buffers 3.8 KB, both in
main RAM. `px_begin(h)` and `px_blit(y)` replace `cv_begin` and `cv_blit`; strips must start and end on even rows.

**What changed on screen:** nothing on the 57 page screens: their renders are byte for byte what they were. UPDATE
MODE's two strips are now 106 + 108 rows, so its lower line lands on the grid (one pixel up). Felucca's
anti-aliased type is gone from the last screens that used it: HARDWARE CALIBRATION is redrawn in `ui.c` on the dot
grid, and the one-line screens (boot, UBOOT, the crash screen, the update's progress) keep their calls and rows,
but `draw_text_line` and `draw_text_box` are Bryo's own dot versions (`GFX_DOT_TEXT`). I didn't touch `ota_hw.c`:
the update path calls the same function names it always did.

**Memory**, measured on a 32-bit build: the pool went from 281.6 KB to 222.1 KB (122 KB free of 336), main RAM from
23.0 KB to 27.2 KB (71 KB free of 96). Together about 193 KB is free against the ~151 KB phases 3b to 5 plan, so
the SPACE delay can stay 16-bit; storing it in 8 bits (32 KB back) stays in reserve.

**Tests:** the canvas clips at its edges, keeps 2 bits a dot and reaches the screen as 2 x 2 pixel dots in the
right colours; the calibration, boot, update and crash screens are in the golden set.

## GRAIN, as built (2026-10-08, the live buffer)

GRAIN is the first device after the source to make sound (`grain.c`). It's the S-4's MOSAIC: grains of a live buffer
of what the source plays.

**The buffer.** My first GRAIN read the track's tape, to save the 22-33 KB a live buffer costs a track. That made
SYNTH and POLY second-class: you had to print them onto the tape before GRAIN could touch them, and there was no
time-stretch of what you'd just played. With the shared memory ("Memory by usage") a buffer only costs anything
while GRAIN is on, so now it keeps the last few bars of the source: the tape as it plays, or the synth as you play
it. It's in the tape's own format, so grains read it the same way, and it's in whole bars:

| TRACKS | Bars | At 120 BPM |
| --- | --- | --- |
| 3 or 4 | 1 | 2 s, 11 chunks |
| 2 | 2 | 4 s, 22 chunks |
| 1 | 4 | 8 s, 44 chunks |

Never past 12 s, so a slow tempo halves the bars until it fits (4 bars at 40 BPM would be 24 s: it holds 2). The
write head starts on the bar line when the transport starts and wraps at the buffer's end, so the buffer is always
whole bars. It records while the transport plays, and always on a SYNTH or POLY track, because you play those without
the transport. A buffer is sized while GRAIN is on (WET above 0) and SCAN reads it; WET 0, SCAN TAPE or a parked
track gives the chunks back. When nothing is free it takes them from the end of the longest tape, like any device
switched on.

**FDBK** (the S-4's FEEDBACK) is how much of the buffer stays as new sound goes in: 0 keeps only the last bars, 100
layers forever. That's also what makes switching the source smooth: the old sound stays in the buffer until the write
head passes, then fades at FDBK each pass, so TAPE to SYNTH crossfades in the grains over a bar instead of cutting.

**Freeze** (the 0 black key: held, while it's held; tapped, latched until the next tap, its LED lit, as a tapped GLO
latches the mixer) stops every buffer recording and holds its bars, and the grains go on playing
them: a loop locked to the tempo. Change the tempo while frozen and the loop's pace follows. It's a transition tool:
freeze, switch the source or mute, let go to carry on.

**SCAN** (page 4, with WARP, OFST and FDBK beside it: set it up left to right) picks what grains read and where they
start, the cursor:

| SCAN | Reads | The cursor |
| --- | --- | --- |
| TAPE | the track's tape, inside TAPE's loop window (no buffer, no memory) | moves at WARP's speed (100 %: the tape's pace); frozen, it stops |
| STR | the buffer | moves at WARP's speed against the write head's: 100 keeps a fixed distance, lower falls behind and stretches time |
| POS | the buffer | stays at OFST of it (0: the bar line) |
| DLY | the buffer | trails the write head by OFST of the buffer: grains of what played that long ago |

STR is the default. On the GRAIN page the white keys move the cursor to their sixteenth (of the tape's loop, or of
the buffer, where a bar's sixteenths are its sixteenth notes); in POS and DLY a key sets the spot or the trail until
OFST is turned again.

**A grain** starts around the cursor (SPRY: up to half the window either way), lasts SIZE, and reads at PTCH plus a
random +-PRND semitones, held to SCAL's scale. REV is the share that plays backwards. CONT shapes the window, from
square with 2 ms ends to a full Hann. SPRD places it in the stereo field, and its level is 1 / sqrt(the expected
overlap). RATE (1..80 a second, square law) and PATN (EVEN, SWNG, CLST, RND) time the starts. Grains start while the
transport plays, on a SYNTH or POLY track, while frozen, or while a white key is held on the GRAIN page.

**WET starts at 0**, so a track sounds as before, and takes no memory, until you turn it up.

**From GRAIN on, the track is stereo** (SPRD needs it). `track_rt[].last`, what the other tracks' REC hears, stays
mono (the two sides' average).

**Reading without a copy.** Each grain decodes the ADPCM itself into a 128-sample window, starting from the stored
decoder state of the window's block. A forward grain slides its window and decodes each sample once; a backward one
re-decodes from its block's start at each refill, which is why the window is 128 samples rather than 64.

**32 grains in all, shared by TRACKS.** They're four groups of 8; each track has its own, and a parked track's group
goes to a track still on, so with 1 or 2 tracks a track sounds up to 16 at once (TRACKS 3: 16, 8, 8). The total never
passes 32, so the worst case costs what four tracks of 8 did. I started at 16 a track, halved it to 8 when four tracks
shared the CPU, and this gives the 16 back when fewer do. Shedding lowers every track's allowance, two of each 8 at a
time down to half; a second without shedding gives one back. A grain due while all its track's allowed grains sound is
skipped, not stolen, so nothing cuts off mid-grain.

**Cost, measured** (instructions per output sample for the whole chain, four tracks, the host's 64-bit build under
callgrind; a ballpark for the FM-1, not its cycles):

| All four tracks | Per sample |
| --- | ---: |
| GRAIN off (tapes only) | 856 |
| WET 100 at the defaults: about 2 grains a track, each buffer recording | 1,528 |
| WET 100, RATE 100, SIZE 500: 8 grains a track, the cap | 3,238 |
| the same, every grain backwards | 3,961 |
| the same with SCAN TAPE (no buffer to record) | 2,869 |
| TRACKS 2: two tracks of 16 grains | 2,598 |

Recording a buffer costs about 100 a track: each tape sample is decoded, mixed with FDBK and encoded again. The
first GRAIN cost 9,760 at a cap of 16. A forward grain re-decoded its whole block every time it left its window, and
the window's shape was a table read per sample. Now the window slides forward, the envelope is taken once a block
and ramped, and the grain's state sits in locals for the block. Whether the cap holds on the FM-1 needs the
hardware: the table above is the case where it matters.

**The picture shows grains, not the sample.** Across is where a grain reads; up and down is its pitch, with the
sound's own pitch dotted through the middle and two octaves to each edge, so PTCH, PRND and SCAL move grains up and
down. A grain shows up only while it sounds. It covers what it reads, so a grain slowed down by PTCH is narrower and
a sped-up one wider. Its height is its window (CONT) swelled by the level there, the played part is solid and the
rest dim, and its playhead is an arrow pointing the way it runs (REV: right to left). With nothing sounding, a dim
outline of one grain from the knobs sits at the cursor.

- With the buffer (STR, POS, DLY) the field is the whole buffer, a measure or a few: the bar lines dotted, the beats
  ticked, the write head a solid line while it records (gone while frozen), the cursor the triangle on top, and the
  sound's level along the bottom. At WET 0 it shows the bars it will hold, empty.
- With SCAN TAPE the field is the stretch of the tape the grains can reach, zoomed around the cursor (SPRY's scatter
  plus a few grains' length, never less than 256 samples), SPRY's reach dotted under it. My first picture drew the
  whole loop window, and at that scale an 80 ms grain was a dot or two wide.

**Tests** (`test_grain`, `test_grain_buffer`): a grain's decoder against the tape's reader, both ways; one grain's
output sample for sample; pitch inside a grain (440.2 Hz at PTCH 0, 880.2 at +12, 440.2 backwards); WET 0 changes
nothing; SPRD 0 centred, 100 spread; the cap and shedding; WARP; SCAL MAJ with PRND 12; the GRAIN page's keys; an
empty tape. The buffer: its bars by TRACKS and tempo (and the 12 s limit), none at WET 0 or SCAN TAPE, the write head
on the bar line, the beat recorded; switching TAPE to SYNTH carries the beat until the write head passes; FDBK 50
halves it a pass; freeze holds the buffer while grains loop it, and the loop's pace follows the tempo; POS and DLY
cursors, a key's spot and OFST taking over again; SYNTH played with the transport stopped, recorded and granulated;
16 grains with TRACKS 2, 16-8-8 with 3; a buffer taking from the longest tape when nothing is free.

## RESONATOR, as built (2026-10-08)

RESONATOR comes after GRAIN (`reso.c`): four tuned strings the track rings through, the S-4's RING with the PRD's
strings. Its picture was drawn before it made sound, with the mappings written next to it, so I built the strings to
those mappings: what the panel draws is what you hear.

**A string** is a Karplus-Strong loop: a delay line one period long, a one-pole low-pass inside the loop, and a
loop gain. TONE is the low-pass (0 dark, 100 bright, nothing taken off). DEC is the ring time to -60 dB, from 80 ms
at 0 to 20 s at 100, and the loop gain comes from it per string, so a low string and a high one ring for the same
time. The low-pass delays the loop a little, which would pull every string flat, so the delay line is shortened by
exactly that: A3 measures 220.0 Hz.

**The four strings** sit on the first four partials SCAL keeps above the root, the same table the picture draws from:

| SCAL | Strings above the root |
| --- | --- |
| HARM | harmonics 1, 2, 3, 4 (each string adds its own overtones, so the whole series rings) |
| MAJ | root, major third, fifth, octave |
| MIN | root, minor third, fifth, octave |
| PEN | root, second, third, fifth |

**What rings them** is the track's sound after GRAIN, through a filter first: CUT, RES and SLOP (low-, band- or
high-pass) on the same state-variable filter POLY uses. The filter is skipped while it's open, so it costs nothing
until you turn it. How much of the sound goes in falls as DEC rises: a long ring builds up louder from the same input,
and this keeps it from building without bound (the loop clamps besides). WET blends the strings with the track,
ramped across each block. The strings sit across the stereo field, 1 and 3 left, 2 and 4 right, two to one.

**The keys on the RESONATOR page** set the root: a semitone a key from C of the track's octave, so OCT-/OCT+ shift
it, and it holds until PTCH is turned again (the way GRAIN's keys hold a spot until OFST is turned). On a TAPE track
a key also plucks the strings, a burst of noise one period long, softened by TONE. That's the PRD's sound from
nothing: a blank tape, RESONATOR and REC. On a SYNTH or POLY track the keys keep playing the source and the strings
take its notes as their root, so the resonance follows what you play. The picture shows a key's root as PTCH's.

**Memory**: a string's delay line is a chunk of the shared memory (1,056 samples, so the lowest string is F1,
43.7 Hz), four a track, taken while WET is above 0 on a track in use and given back otherwise, like GRAIN's buffer.

**Cost, measured** (host instructions per output sample, the whole chain, four tracks): the reels alone 873;
RESONATOR at WET 60 on all four (16 strings) 1,818, so about 950 for the strings, around 50 a string with its share
of the blend. My budget said 60 a track; it's nearer 240. The first version cost 1,160: a division per sample in the
blend, the input's share worked out per string, and each string summed into both sides; now the blend is Q15, the
share is worked out once per track, and the strings are summed once per side. With GRAIN at its cap too: 4,416. That
pair is the number for the FM-1 to settle.

**Shedding** takes strings after grains: once GRAIN's allowance is down to half, each shed takes a string (down to
two), and a second without one gives a string back.

**Tests** (`test_reso`): no strings and no memory at WET 0; four chunks at WET up; a key on a TAPE track plucks at
its note (A3 at 220.0 Hz, measured by autocorrelation so the partials don't fool it); DEC rings longer as it rises;
SCAL MAJ puts the strings on the third and fifth; turning PTCH takes the root back; a reel through DEC 100 and TONE
100 rings loud and never past full scale; on a SYNTH track the keys play the synth and the strings follow; shedding
takes a string and gives it back; a parked track holds no strings; WET 0 gives the chunks back. And
`tests/checkpoint_sim.sh` renders and measures RESONATOR on all four tracks (runs 11 and 12).

## COLOR, as built (2026-10-08)

COLOR is the S-4's DEFORM after RESONATOR (`firmware/src/color.c`). It takes no memory, only a few numbers a track,
and its picture (`viz_color`) calls the same `color_shape()` the sound goes through, so what's drawn is what's
played. In order:

| Knob | What it does | How |
| --- | --- | --- |
| DRIV | gain 1..16 into a soft clip | Felucca's tanh table. At 0 there's no clip at all, so the default leaves the sound sample for sample untouched |
| CRSH, CMOD | 16 down to 2 bits (BIT), a sample held 1..12 samples (RATE), or both | bits cut toward zero, so no DC creeps in |
| NOIS, NDEC, NTON | noise riding the sound's envelope | the envelope opens at once and falls 60 dB over NDEC; NTON is a one-pole filter on the noise. Silence stays silent |
| TILT | the low end against the high around 700 Hz | ±6 dB each way at ±100 |
| WET, LVL | dry against coloured; the output, -24..+6 dB | both ramp across a block |

I changed two things from the picture's first draft while building it. DRIV's gain is now continuous (Q8) instead of
16 integer steps, since 1 → 2 was a 6 dB jump on the first detent; and the clip isn't normalised to full scale
any more, because that made DRIV 1 already 2.4 dB louder. The picture changed with it.

**Load**: nothing while every colouring knob is at 0 and LVL at 0 dB (the default). With everything on, 182 host
instructions a sample a track; the first draft was 205 before I moved its state into locals (the compiler couldn't
know the sample buffers didn't alias it, so it reloaded every field every sample). That's six times my 30 estimate,
but the estimate only counted the drive: it's spread evenly across the features, so there's no single thing to cut.

**Tests** (`test_color`): the defaults leave a sine bit-exact; LVL -6 halves it; DRIV 100 squares a sine (crest
1.04); CRSH 100 leaves 2 bits, RATE holds 12 samples, BOTH does both; NOIS on silence is silence, after a hit it's
there and gone by twice NDEC; NTON -100 against +100; TILT ±100 moves 100 Hz and 8 kHz the right ways; WET 0 is the
dry sound however much drive; every knob at either end with a square past full scale stays bounded.

## SPACE, as built (2026-10-08)

SPACE is the S-4's VAST, last on a track (`firmware/src/space.c`): a delay and Felucca's ROOM reverb, run at half the
rate (22.05 kHz) to halve both their memory and their load. The track goes in as mono (both sides averaged, and two
samples into one), and the wet comes back up to 44.1 kHz by stepping halfway between samples.

**The delay.** TIME (10..370 ms) back, and when TIME turns the read point glides there instead of jumping: a tape
echo's bend, about 90 ms long and never faster than 3/8 of a sample a sample (within a fifth), so there's no click.
FDBK 100 is a loop gain of 0.98. TONE sits in the feedback: below 0 a low-pass (to about 430 Hz), above 0 a
high-pass (to about 650 Hz), so each echo is darker or thinner than the last. DLY 100 puts the first echo at 0.8 of
the sound, which is the height the picture draws. SPRD moves the right echo up to 10 ms ahead of the left.

**The room.** Four damped combs and two allpasses, Freeverb's shape, as Felucca's ROOM. SIZE sets the combs' lengths
(40 % to 100 % of 38..46 ms); DEC the time the tail takes to fall 60 dB (0.2..4.2 s, each comb's gain from
RESONATOR's `rs_gain`); TONE the combs' damping, or a high-pass in front above 0; PRE the pre-delay, up to 200 ms.
For stereo, the right side runs its own pair of allpasses fed the combs with alternating signs, and SPRD blends it in
(0 is mono).

**Memory by usage.** DLY and VERB now default to 0, like GRAIN's and RESONATOR's WET: a device that's off takes
nothing. With DLY up the delay takes 8 chunks (8,192 samples); with VERB up the room takes 5 and its pre-delay 5. A
line is 16-bit when there's room for it with 3 s still free, and starts 8-bit when there isn't: 4 chunks for the
delay and 3 for the pre-delay, companded like A-law (sign, a 3-bit segment, 4 bits inside it), so the noise rides
under the sound, about 46 dB below a quiet sine's echo. When nothing is free it takes from the end of the longest tape,
as GRAIN and RESONATOR do. A line never changes format while it sounds: it's chosen when it's switched on. A parked
track holds nothing. Each line counts how many times it's been handed over, so one given back and taken again between
two audio blocks still starts from silence. The SPACE page draws a part that's off (DLY or VERB at 0) dim, as it
would be at 100, so TIME, FDBK, SIZE and DEC still show what they set.

**A bug the tests found:** the room kept ringing on its own. After the sound had gone, its output sat at a DC of -36
for good: every `>> 15` in a feedback loop rounds toward minus infinity, and four combs feeding back keep that bias
alive. Rounding to nearest still left a 28-step limit cycle. The comb's gain, the delay's feedback and the allpasses
now cut toward zero, which only ever takes energy out, so a loop with nothing in it falls to exactly 0.

**Load**: 220 host instructions a sample a track with both on (the first draft was 283; the same locals fix as COLOR).
The estimate was 80. The combs and allpasses are about 16 each per half-rate sample, the delay with SPRD's second
tap about 60.

**Tests** (`test_space`): the defaults take no memory and leave the sound alone; TIME 100 gives one echo at 100.0 ms
of 0.8; FDBK 50 a second at 200.0 ms at 0.45; TONE either way loses more each pass; SPRD 100 puts the right echo 10.0
ms ahead; TIME turned while echoing glides with no jump; with 20 chunks free the delay starts 8-bit in 4 chunks, its
echo where 16 bits put it; with nothing free it takes its 4 chunks from the longest tape; DEC 100 against 0 at
0.5..1 s; the room's sides decorrelate at SPRD 100 and match at 0; PRE 100 holds the room back 100 ms; every knob at
its top with noise in stays bounded (TONE either way, SIZE 0..100); a parked track's SPACE holds nothing; DLY and VERB
at 0 give every chunk back.

**On the checkpoint** (`tests/checkpoint_sim.sh`, runs 13 to 16, host instructions per output sample, four tracks):

| Run | What | Host cost |
| --- | --- | ---: |
| 1 | four reels, nothing on (the baseline) | 902 |
| 13 | COLOR on all four (DRIV 60, CRSH 40 BOTH, NOIS 30, TILT 30) | 1,630 |
| 14 | SPACE on all four (DLY 50, VERB 40): 72 chunks, all 16-bit, 14.5 s still free | 1,784 |
| 15 | everything on all four: GRAIN at the cap, RESONATOR, COLOR, SPACE | 5,842 |
| 16 | three tracks as I'd play them: TRACKS 3, GRAIN WET 100, DRIV 40, SPACE | 2,310 |

Run 15 is 6.5 times the baseline: the 2,000 target holds only for setups like run 16. That's the hardware
checkpoint's question now, more than before. If the FM-1 can't carry run 15, the shedding order (grains, then
strings) would need a step for SPACE as well (the room at a quarter rate, say), and that's a decision for when I have
the device's number. The stress run cuts the interrupts in while RESONATOR and SPACE take and give back memory as
knobs turn, and the books balance (RESONATOR's strings and SPACE's lines are in the check now too).

## USB record mode (2026-10-08)

Recording from the computer is a mode of its own, not a source a track plays through: you go in, record, put the
take on a track and come out. Nothing else runs meanwhile, which keeps it light and keeps it simple to reason about.

**Going in.** With the transport stopped, hold REC for a second (a hint shows after 0.3 s). REC used to arm on the
way down; now, stopped, it arms on the way up, so a held REC can mean something else. Arming while stopped has no
timing to keep (nothing records until PLAY), so nothing is lost. Playing, REC still arms on the way down, because a
punch-in has to land where you press it; holding it then only says to stop first.

**While it's up.** The transport stops, every track's REC lets go, and the tracks stop rendering: the audio ISR fades
them out over one block and plays the computer instead. Their GRAIN buffers, RESONATOR strings and SPACE lines go back
to the shared memory (their polls see `sys.usbrec`), so the take can use it. The FM-1's own USB input, what the
computer can record from it, sends silence: otherwise whatever the computer plays would come straight back to it.

| Step | REC | HOME | White keys |
| --- | --- | --- | --- |
| READY | start the take | leave | - |
| RECORDING | stop it | stop, throw it away | - |
| CHOOSE | put it on the picked track (its tape replaced) and leave | throw it away (READY) | 1..TRACKS pick the track (lit) |

The screen keeps to what the step needs: the four tracks as numbered boxes with what each holds, the step's word or
the take's seconds large, one level bar, and the room left.

**How the sound gets in.** `usb.c` (behind `BRYO_UAC_OUT`, on in Bryo, off in the update loader, whose descriptors
stay byte for byte) adds a second UAC1 streaming interface: the computer plays into the FM-1, 16-bit stereo at
44.1 kHz on EP4 OUT, isochronous and adaptive. `uaco_service` takes each 1 ms packet in TIMER5, from `usb_poll` and
nested in the render as the input's `uac_service` already was. It hands the frames to `usbrec.c`, which writes two
rings:

- **The take** goes in as mono at 22,050 Hz, the tape's format: two frames, both sides, into one sample, as a WAV
  over the drive does. The main loop encodes it into ADPCM blocks in chunks of the shared memory it takes as it goes
  (free ones, or cleared and parked tapes'; never a tape in use). The take is written on the computer's clock, so it
  never drops or repeats a sample, whatever the DAC's clock does.
- **The monitor** is the frames as they came. The DAC runs at 44,117.6 Hz against the computer's 44,100, so the
  audio ISR reads the ring a hair slower than it plays, steered by how full the ring is (within 1 %, about 0.04 % in
  practice). A dropped or doubled frame every 57 ms would click; this bends the pitch by an amount nobody hears.

A take is at most a tape's 23.6 s, or what memory there is. When either runs out it stops there and moves on to
CHOOSE with a message. Putting it on a track uses `tape_replace()`, which I pulled out of the drive's WAV import so
both hand chunks over the same way.

**Tests.** `test_usbrec` holds REC playing (a hint, no mode) and stopped (the mode, with no arm on the let-go). The
devices give their memory back. A 441 Hz sine from the computer is heard at 440.8 Hz with no jump, the ring holding
steady against the two clocks. A 3 s take in packets of 44 and 45 frames keeps every frame (357 blocks, none lost),
asks for a track, lands on track 3 and plays back at 300.0 Hz. With 3 chunks free the take stops at 48 blocks, and
HOME throws it away and leaves. `usb_sie_test` (UAC in and out) parses Bryo's descriptors as a host would (five
interfaces, the AC header and its terminals, the stream's endpoint) and runs the endpoint against the emulated
controller: a packet before the computer picks the stream is dropped, then 20 packets polled twice a frame all
arrive in order, and a bus reset turns the stream off. The interrupt stress enters and leaves the mode, records and
places takes (2,720 in 20 s) with packets arriving from the TIMER5 signal, and the memory books balance.

**What only the device can tell.** That the JieLi controller's EP4 OUT works as its register map says (the receive
DMA address, isochronous mode, the receive count): I have the SDK's register names, not a run. And how computers
treat a new sound output. macOS may switch to an output it hasn't seen when it's plugged in; if Bryo grabs the Mac's
sound every time it's connected, the next step is to present the output only while the mode is up (a re-enumeration,
which would also drop and remount the drive).

**Future work: audio out.** The other way, the FM-1 as the computer's input, has existed since Felucca (EP4 IN, the
master output at 44.1 kHz). It hasn't been checked against Bryo's chain yet; it's on the hardware run sheet.

## Memory by usage, TRACKS and REC IN (2026-10-08)

Until now every track had a 36 KB tape whether it used it or not, and the plan reserved a delay and a reverb per
track the same way. That's 4 × 3.3 s of tape however you play, with a third of the RAM sitting idle on tracks doing
nothing. Now there's one pool and each track takes what it uses (`mem.c`).

**Chunks.** The pool is 152 chunks of 16 tape blocks (4,096 samples, 186 ms each): 128 in the pool region and 24 in
main RAM's spare room, 28.2 s of tape-format sound in all. A tape is a list of chunks, and a reader looks a block up
through the list (one more load per block decode). The main loop hands chunks out and takes them back; the audio ISR
only reads the lists. Taking a chunk away shortens the list first, then frees it, and that's safe to reuse at once:
the main loop never runs inside the audio ISR, so a block being rendered finishes before the next step, and every
later block sees the shorter list. Nothing in the ISR holds a pointer into a chunk from one block to the next.

**Tapes grow as they record.** REC on a blank tape starts the head at the loop's start when the transport plays; the
tape grows behind it, the main loop keeping a chunk ready ahead (`tape_poll`). Letting go of REC sets the loop's
length. A reel copied in takes exactly the chunks it needs. A track that never records holds nothing. One tape holds
23.6 s at most (127 chunks): the head's position is kept in 1/4096ths of a sample in 32 bits, which runs out at 23.8 s.

**A WAV over USB never allocates.** It arrives in `usb_poll`, which runs in the TIMER5 interrupt and can cut into
the main loop halfway through handing a chunk out. So while the drive is up the main loop keeps two chunks set aside
(free ones, or a cleared or parked tape's), and the capture takes from those through a ring with one writer on each
side, the main loop topping it up between USB packets. A capture dropped halfway keeps its chunks for the next one
to write over, and only the main loop frees them, with the drive held busy. Those two chunks (372 ms) are the price
of the drive being plugged in.

**When nothing is free**, chunks come off the end of a tape, in this order: a cleared tape (its undo goes), a parked
track's tape (the longest), then the longest tape of all, never the one asking and never one that's growing. A WAV
arriving over USB only takes free chunks, cleared and parked tapes, never a tape in use; what doesn't fit is cut and
the message says so. When there's nothing left to take, a growing tape stops and loops what it has.

**TRACKS** (GLO held + SELECT, 1-4) says how many tracks are in use. It's a held pad and SELECT, like HOME + SELECT
picking the source and a slot + SELECT its engine, so plain SELECT stays the tempo on every view and a tempo turn on
the mixer can never park a track mid-song. The ones above it park: they fade out in 2 ms
and stop rendering, REC on them lets go, the focus and GLO's track keys stay below it. Nothing is erased: a parked
tape is just the first taken when memory runs short, and raising TRACKS brings back what's still there. Fewer
tracks also means more for the ones left: GRAIN's buffer holds more bars and a track sounds more grains. The mixer's
levels page shows it all under the faders: each track's seconds of tape (and GRAIN buffer), OFF for a parked one,
the memory as one ribbon (tapes solid, buffers dim, free dotted), TRACKS and the time still free. EDIT (held, or tapped to latch it) still
gives the channel.

**REC IN** (ALGORITHM): what each track's REC records, KNOB 1-4 a track each, ALGORITHM turned again the focused
track's. AUTO is what REC always did: the others' mix on a TAPE track, its own source on a SYNTH or POLY track. OTHR
is the others' mix whatever the source; T1-T4 one track, after its devices; a track's own number reads SELF and
records it back onto itself, DUB setting how much of the last pass stays. So T1 into T2 into T3 into T4 is three
knob turns, and every bounce can add its own effects. The picture draws the routes you've set as lines between the
tracks (solid while that REC is armed) and names the defaults, with the routing of the last-turned track in words.

**Reels across slots.** The six user reel slots are 40 KiB each. A sound up to 3.3 s fits one, laid out exactly as
reels always were (so old reels still read). A longer one runs into the slots after it, its data in one piece, up to
21.5 s across all six; its header says how many it spans. The slots it covers read as empty but aren't free.
Whether a slot holds a sound is checked (header, CRC) once, at power-on and after every save, and kept: the audio
ISR asks every block, and a long reel's CRC takes longer than a block.

**What the review round found** (a reviewer agent read the new code against the single-core model, and I checked
each finding): a tape over 127 chunks overflowed its Q12 head (now capped at 23.6 s); the WAV capture allocated
from the TIMER5 interrupt (now the spares above); a parked track's grains held their slots forever, since a parked
track isn't rendered (they're let go when it parks); every block the ISR re-ran a user reel's flash CRC (now
cached); a block staged on a tape whose list was replaced could be written into the new one (a generation count
drops it). Smaller: a growing tape keeps two chunks ready ahead instead of one, a blank tape ignores DUB (there's
nothing to keep), and a GRAIN buffer shrunk under its write head keeps the head's place in the bars.

**Tests** (`test_memory`): a blank tape grows while REC records it and stops at its length, looping what it recorded;
stealing takes a cleared tape first, then a parked track's, then the longest, never from a tape in use for an import;
a tape that can't grow loops what it has, and REC with nothing anywhere is refused; a 700-block reel spans three
slots and plays from flash, a one-slot reel keeps the old layout, a save into a covered slot breaks the long reel; a
10 s WAV lands whole on a tape, a 6 s one as a reel across two slots, and one with no memory at all is refused; TRACKS
parks and brings back a track; REC IN T1 records track 1 alone, and a SYNTH track on OTHR records the others.

## Build order

Each phase ends in something you can flash and hear or see, and each is its own commit series.

| Phase | What | Done when |
| --- | --- | --- |
| 1. Skeleton (**done**, host-verified) | `bryo.c` boots on the kept hardware layer; the old app code is removed; silence plus a test tone; the header and an empty strip; install, UBOOT and calibration still work | it installs from the web installer and returns to stock |
| 2. TAPE + reels (**done**, host-verified; see "Phase 2, as built") | tapes play factory reels; the loop window, speed, reverse, half speed, FADE; slices on the white keys; REC and overdub (resampling the other tracks); clear and undo | you can load, slice, record and overdub a loop |
| 3. USB audio in + SYNTH + POLY (**done**, host-verified; see "SYNTH, as built", "POLY, as built" and "USB record mode") | the source_t interface; the UAC OUT endpoint, drift handling, a record mode; the SYNTH source; the POLY source (voices with their own envelope and filter, three HOME pages) | you can record your computer, and play the synth and samples onto a tape |
| 3b. GRAIN (**done**, host-verified; see "GRAIN, as built") | the scheduler, the sounding cap, FREEZE (key 0); the live buffer, SCAN, FDBK | grains run on 4 tracks inside the budget |
| 3c. Memory by usage (**done**, host-verified; see "Memory by usage, TRACKS and REC IN") | the shared chunks, growing tapes, TRACKS, REC IN, long reels | memory follows what you use |
| 4. RESONATOR (**done**, host-verified; see "RESONATOR, as built") | strings, chromatic keys, OCT shifts | tuned feedback chords from the keys |
| 5. COLOR + SPACE (**done**, host-verified; see "COLOR, as built" and "SPACE, as built") | drive, crush, noise; delay and reverb | the full chain on 4 tracks inside the budget (on the host it isn't: everything on all four is 5,842; the device decides) |
| 6. Mixer + routing (routing **done** early: REC IN) | GLO mixer, filters, compressor | the mixer's DSP |
| 7. Modulation | the 4 engines, hold-and-turn depth, assigning engines, p-locks | the PRD's §4 workflow end to end |
| 8. Projects | save and recall with reels; user reel slots in flash and the upload tool; quick SAVE; undo for MONO and POLY | a power cycle brings a session back |
| 9. Screen (mostly done early: the dot-grid screens) | the modulation arcs on the pictograms, the motion dots, the summed white dot | the PRD's §5 |
| 10. Tools + docs | the upload tool for reels, the installer text, a Bryo manual | someone else can use it |

Phases 2 and 7 are the riskiest: the tape format, and whether the modulation sum fits at control rate. If
either needs rethinking, it'll show up early.

# Hardware checkpoint: memory by usage and GRAIN's buffer (2026-10-08)

Everything since phase 2 has only run on the host. Before I build RESONATOR, COLOR and SPACE on top, I want to know
three things only the FM-1 can tell me: whether the audio interrupt keeps up with GRAIN at its cap, whether the
shared memory behaves with real timing (USB packets and screen frames cutting in), and whether anything sounds wrong
that the host tests can't hear. This page is the run sheet. It takes about 20 minutes.

## On the host first: `tests/checkpoint_sim.sh`

There's no emulator for the FM-1's pi32v2 core (it's JieLi's own instruction set; QEMU doesn't know it), so the
device's real load can only come from the device. What I can do on the host is run the firmware's own sources
through this run sheet, with the hardware stubbed as the tests do (`tests/checkpoint_sim.c`):

- **Every run rendered to a WAV** (`build/checkpoint/runN.wav`) to listen to before touching the device.
- **Each run's cost** in host instructions per output sample (callgrind): a ratio against what Felucca's engines cost,
  not the FM-1's percentage.
- **The interrupts for real.** The audio interrupt and TIMER5's `usb_poll` fire as asynchronous signals that cut into
  the main loop at any instruction, as on the device (TIMER5 outranks the audio; `usb_poll` never runs nested in it),
  faster than the device does, while the main loop turns knobs, changes TRACKS, records, freezes, draws, and a WAV
  after WAV arrives over the drive. Every so often it stops both and checks the shared memory's books: each chunk
  owned once, listed where its owner says, none lost. I checked the check: with the WAV capture allocating from the
  interrupt again (the bug the review round found), it fails on every try within seconds.

What it gave on 2026-10-08 (host instructions per output sample, the whole chain, four tracks):

| Run | What | Host cost | What the firmware reported |
| --- | --- | ---: | --- |
| 1 | four reels, GRAIN off | 861 | |
| 2 | WET 100, the defaults (each buffer recording) | 1,535 | 4 grains sounding, 44 chunks of buffers |
| 3 | the cap: 8 grains a track | 3,263 | 32 grains (8 8 8 8) |
| 4 | the cap, all backwards | 4,022 | 32 grains |
| 5 | the cap, SCAN TAPE | 2,888 | 32 grains |
| 6 | the cap, TRACKS 2 (16 + 16) | 2,715 | 32 grains (16 16 0 0) |
| 7 | a blank tape recorded 12 s, then looped | 964 | the tape 12.00 s long, 65 chunks |
| 8 | the freeze tapped on and off | 979 | frozen at 3 s, let go at 9 s |
| 9 | SYNTH played, transport stopped, GRAIN on | 618 | grains sounding after the phrase |
| 10 | a 20 s WAV over TAPE3.WAV while playing | - | the tape 20.00 s, TRACK 3'S TAPE REPLACED |

These are measurements, not listening: the WAVs are there to be heard.
No sample past full scale and no sudden jump (over 12,000 between neighbouring samples) in any of them. The stress:
over 20 s, 132,143 audio blocks and 86,245 USB sectors cut into the main loop, and the books balanced at every one
of 10,778 checks.

What that leaves for the device: the load. Run 4 costs 4.7 times run 1 on the host; on the FM-1 that ratio is the
question. And how GRAIN's level sits against the dry sound is worth a listen: at WET 100 the grains peak around a
third of the reels.

## Build and install

1. `tools/get_toolchain.sh` once, if `~/.jieli/toolchain` isn't there, and the AC79 SDK in `~/fw-AC79_AIoT_SDK`
   (BUILDING.md has the details).
2. `./build.sh`. Its last lines print the image size, the RAM and the pool. Note them: I expect the pool at about
   312 KB of 336 and RAM at about 80 KB of 96. The build refuses if the pool's 8 KB of headroom is gone.
3. Install `build/felucca.fwsc` from the web installer, as always. The update loader is untouched, so going back to
   stock works the same way it did.

## Where to read the load

Hold GLO: the mixer shows **CPU** in its bottom-right corner. It's the audio interrupt's load in 5 % steps, and it
turns bright at 85 %, where the firmware starts shedding grains. That's the number I need. (The memory picture
above it shows each track's seconds, the GRAIN buffers and the time free.)

## The runs

Write down the CPU reading for each, and anything you hear.

| # | Set up | What I expect | Write down |
| --- | --- | --- | --- |
| 1 | Power on, PLAY. Four factory reels, GRAIN off | the baseline | CPU |
| 2 | Each track: EDIT for GRAIN, WET 100 (KNOB 1 on GRAIN 2), the rest default | each track records its last bar and grains it | CPU; any click when WET goes up |
| 3 | Each track: RATE 100, SIZE 500 (GRAIN page 1) | 8 grains a track, the cap: the hardest case | CPU; does it crackle, does it settle? |
| 4 | Same, REV 100 (GRAIN 2) | every grain backwards: the dearest read | CPU |
| 5 | Same, SCAN TAPE (GRAIN 4, KNOB 1 all the way left) | no buffer to record | CPU |
| 6 | Hold GLO, turn SELECT down to TRACKS 2 | tracks 3 and 4 fade out; 1 and 2 get 16 grains each | CPU; a click as they go? |
| 7 | TRACKS 4 again, back to the defaults (or power-cycle). Track 2 on a blank tape: hold the POLY key to clear it, REC, let it record 10 to 15 s, REC again | the tape grows while you record and loops what you recorded | does the loop length match? any gap or click at the loop's seam? |
| 8 | Tap the 0 key while track 1's GRAIN plays (WET 100) | the buffer freezes into a 1-bar loop, the 0 key's LED lights; tap again to let go | does it loop in time? |
| 9 | Track 1 to SYNTH (hold HOME, turn SELECT), GRAIN WET 100, transport stopped, play a few notes | what you play is granulated without PLAY | does it sound? |
| 10 | Plug into a computer, copy a 20 s WAV onto the drive as TAPE3.WAV, eject, replug | track 3's tape is the whole 20 s (memory allowing) | the length on TAPE's page; any audio dropout while it copied |

## What I'll do with the numbers

- **Run 3 under about 70 %:** the plan holds; RESONATOR next.
- **Run 3 between 70 and 85 %:** I'll take the cheap savings first. The buffer recording can skip decoding when
  FDBK is 0 (about 100 a track on the host), and the grain cap's default can drop to 6.
- **Run 3 over 85 %, or crackling:** the cap has to come down before anything else gets built, and I'll measure
  where the cycles go with the debug build's console (`BRYO_MSC=0`: `cpu_pct` there is the same number).
- **Anything in runs 7 to 10 wrong:** that's a timing bug the host can't show me, and it comes first. Tell me what
  you did and what you heard.

# Hardware checkpoint: memory by usage, GRAIN's buffer and the full chain (2026-10-08)

Everything since phase 2 has only run on the host, and the whole chain (GRAIN, RESONATOR, COLOR, SPACE) is built now.
I want to know three things only the FM-1 can tell me: whether the audio interrupt keeps up with it all on, whether the
shared memory behaves with real timing (USB packets and screen frames cutting in), and whether anything sounds wrong
that the host tests can't hear. This page is the run sheet. It takes about 30 minutes.

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

What it gives now, after COLOR and SPACE (host instructions per output sample, the whole chain, four tracks; the
first six runs cost about 40 more than at the first checkpoint, the two new devices' calls while they're off):

| Run | What | Host cost | What the firmware reported |
| --- | --- | ---: | --- |
| 1 | four reels, GRAIN off | 902 | |
| 2 | WET 100, the defaults (each buffer recording) | 1,576 | 4 grains sounding, 44 chunks of buffers |
| 3 | dense grains on all four (the pool: 16) | 2,318 | 16 grains (4 4 4 4) |
| 4 | the same, all backwards | 2,698 | 16 grains |
| 5 | the same, SCAN TAPE | 1,960 | 16 grains |
| 6 | dense, TRACKS 2 (the pool: 24) | 2,243 | 24 grains (12 12 0 0) |
| 7 | a blank tape recorded 12 s, then looped | 1,005 | the tape 12.00 s long, 65 chunks |
| 8 | the freeze tapped on and off | 1,020 | frozen at 3 s, let go at 9 s |
| 9 | SYNTH played, transport stopped, GRAIN on | 659 | grains sounding after the phrase |
| 10 | a 20 s WAV over TAPE3.WAV while playing | - | the tape 20.00 s, TRACK 3'S TAPE REPLACED |
| 11 | RESONATOR WET 60 on all four | 1,847 | 16 strings |
| 12 | RESONATOR and GRAIN dense | 3,248 | 16 strings, 16 grains |
| 13 | COLOR on all four (DRIV 60, CRSH 40 BOTH, NOIS 30, TILT 30) | 1,630 | |
| 14 | SPACE on all four (DLY 50, VERB 40) | 1,784 | 72 chunks, all 16-bit, 14.5 s free |
| 15 | everything on all four | 4,859 | 16 grains, 16 strings, 72 chunks of SPACE, 3.3 s free |
| 16 | TRACKS 3, GRAIN WET 100, DRIV 40, SPACE | 2,310 | 54 chunks of SPACE, 11.7 s free |
| 17 | the mixer: LOW HIGH FILT PAN on all four, the compressor at AMT 60 | 1,701 | 5.7 dB taken off |
| 23 | GRAIN dense on one track alone (the pool: 32), the others plain reels | 2,976 | 31 grains |

Runs 3 to 6, 12 and 15 cost less than they did (3,304, 4,063, 2,929, 2,736, 4,228, 5,842) since GRAIN's grains became
one pool that shrinks as more tracks use it (32 for one track, 24 for two, 16 for three or four, shared by what each
asks: docs/bryo-architecture.md, "GRAIN, as built"). Run 23 is the case that grew: one track can now sound 32.

Runs 3 to 17 test each part at its limit. How the instrument will actually be played is a different number, so
runs 18 to 22 are realistic setups (each played for 6 s, a synth track's keys going):

| Run | Setup | Host cost | Against nothing on |
| --- | --- | ---: | ---: |
| 18 | three tracks as I'd play them: a played synth through GRAIN and SPACE, a driven reel with a little EQ, a reel through RESONATOR, the compressor | 1,653 | 1.8x |
| 19 | a four-track groove: crushed drums, a grain track, a filtered synth bass, a reverb, two tracks panned, the compressor | 1,584 | 1.7x |
| 20 | an ambient pad on two tracks: a synth stretched by dense grains into a long reverb, a reel through the strings | 2,077 | 2.2x |
| 21 | the groove's busiest moment: denser grains, strings, a delay, every channel filtered | 2,439 | 2.6x |
| 22 | the groove, bounced onto track 4 as it plays | 1,756 | 1.9x |
| 24 | the groove, modulated: an LFO a beat on the crush, a SEQ on grain size, T1's drums ducking T2's filter (FOLLOW), an ADSR on the bass's cutoff, a stereo LFO on T4's pan | 1,788 | 1.9x |
| 25 | the groove with every slot on every track, 32 depths each (the most there can be) | 3,097 | 3.3x |

Run 24 is run 19 (now 1,595: every run costs about 5 more since the modulation's clock ticks whether or not anything
is modulated) plus the modulation I'd reach for: 83 of the 190 extra are the modulators, the rest the devices
re-tuning as their knobs move. Run 25 is the ceiling, not a scene (docs/bryo-architecture.md, "Modulation, as
built").

The everyday setups sit under the 2,000 target, a dense pad at it, a busy build-up 20 % over; the all-on case
(run 15, 6.3x) isn't one anybody plays. GRAIN's density is what moves the number most. One reading from the device
(the CPU with four reels playing, run 1) turns these ratios into percentages.

These are measurements, not listening: the WAVs are there to be heard.
No sample past full scale and no sudden jump (over 12,000 between neighbouring samples) in any of them. The stress:
over 20 s, 131,965 audio blocks and 86,110 USB sectors cut into the main loop while RESONATOR's and SPACE's knobs
took memory and gave it back too, and the books balanced at every one of 9,839 checks.

What that leaves for the device: the load. Run 4 costs 4.5 times run 1 on the host, and run 15, everything on all
four, 6.5 times; on the FM-1 those ratios are the question. And how GRAIN's level sits against the dry sound is worth a listen: at WET 100 the grains peak around a
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
| 3 | Each track: RATE 100, SIZE 500 (GRAIN page 1) | 4 grains a track (four tracks share 16) | CPU; does it crackle, does it settle? |
| 4 | Same, REV 100 (GRAIN 2) | every grain backwards: the dearest read | CPU |
| 5 | Same, SCAN TAPE (GRAIN 4, KNOB 1 all the way left) | no buffer to record | CPU |
| 6 | Hold GLO, turn SELECT down to TRACKS 2 | tracks 3 and 4 fade out; 1 and 2 share 24, 12 each | CPU; a click as they go? |
| 7 | TRACKS 4 again, back to the defaults (or power-cycle). Track 2 on a blank tape: hold the POLY key to clear it, REC, let it record 10 to 15 s, REC again | the tape grows while you record and loops what you recorded | does the loop length match? any gap or click at the loop's seam? |
| 8 | Tap the 0 key while track 1's GRAIN plays (WET 100) | the buffer freezes into a 1-bar loop, the 0 key's LED lights; tap again to let go | does it loop in time? |
| 9 | Track 1 to SYNTH (hold HOME, turn SELECT), GRAIN WET 100, transport stopped, play a few notes | what you play is granulated without PLAY | does it sound? |
| 10 | Plug into a computer, copy a 20 s WAV onto the drive as TAPE3.WAV, eject, replug | track 3's tape is the whole 20 s (memory allowing) | the length on TAPE's page; any audio dropout while it copied |
| 11 | Power-cycle. Each track: RESONATOR WET 60 (FX until RESONATOR, KNOB 4) | four strings ring through each track | CPU |
| 12 | Same, plus each track's GRAIN at the cap (run 3's settings) | the strings and the grains together | CPU; crackle? |
| 13 | Power-cycle. Each track's COLOR: DRIV 60, CRSH 40, NOIS 30, TILT 30 (FX, KNOB 1-4); CMOD BOTH (COLOR 2) | every track driven, crushed and noisy | CPU |
| 14 | Power-cycle. Each track's SPACE 2: DLY 50, VERB 40 | echoes and a room on every track | CPU; does the mixer's memory ribbon show it? |
| 15 | Runs 12, 13 and 14 together (everything on) | the hardest case there is | CPU; does it crackle, does shedding settle it? |
| 15b | TRACKS 4 again, GRAIN off on tracks 2-4 (WET 0), track 1 still at RATE 100, SIZE 500 | one track with the whole pool: "31 OF 32 GRAINS" on GRAIN's page | CPU |
| 16 | Plugged into a computer: is there an output named Bryo? Pick it, play something; on the FM-1, stopped, hold REC a second | the USB record mode, the level bar moving, the sound in your headphones | did the computer switch its output to Bryo by itself when plugged in? any clicks in what you hear? |
| 17 | REC, 10 s of something you know, REC, white key 2, REC | track 2's tape is the take; PLAY plays it | the length on TAPE's page; does it sound like what the computer played (mono, 22 kHz)? |
| 18 | Record the FM-1 on the computer (Bryo as its input) while it plays | the master output arrives (Felucca's, untested with Bryo) | any dropouts? |
| 19 | GLO, EDIT: each track's LOW +6, FILT -40, PAN apart; EDIT again: MASTER, AMT 60 | the channels shaped and placed, the mix glued, the meter moving | CPU; does the compressor pump? |
| 20 | Power-cycle, PLAY. Track 1 on COLOR: hold LFO, turn KNOB 2 (CRSH) to +40 %, let go. Tap LFO, LFO 4: SYNC BPM. Hold ENV on GRAIN 2, KNOB 1 (WET) +80 %; play a white key | the crush breathing in time; grains swelling in on each key and on each loop | CPU; does the crush step audibly (zipper), or glide? |
| 21 | On the mixer's channel page, hold LFO and turn KNOB 4 (PAN) to +60 %; tap LFO, LFO 3: SPRD 100 | the track moving across the stereo field, left and right apart | does it sound wide, or just wobbly? |

## What I'll do with the numbers

- **Run 3 under about 70 %:** GRAIN fits as it is; then run 15 decides the rest (below).
- **Run 3 between 70 and 85 %:** I'll take the cheap savings first. The buffer recording can skip decoding when
  FDBK is 0 (about 100 a track on the host), and the pools can shrink (32 / 24 / 16 are a table: GR_POOL).
- **Run 3 over 85 %, or crackling:** the cap has to come down before anything else gets built, and I'll measure
  where the cycles go with the debug build's console (`BRYO_MSC=0`: `cpu_pct` there is the same number).
- **Run 15 over 85 %:** grains and strings shed first as they do now; if that isn't enough, SPACE needs a shedding
  step too (the room at a quarter rate is the first idea), and I'd decide it with this number in hand.
- **Anything in runs 7 to 10 wrong:** that's a timing bug the host can't show me, and it comes first. Tell me what
  you did and what you heard.

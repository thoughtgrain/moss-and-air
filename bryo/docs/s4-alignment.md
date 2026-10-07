# Bryo against the Torso S-4

I lined up the modulators with the S-4 earlier (LFO with random as a shape, ADSR, FOLLOW; see "Modulators" in
`bryo-architecture.md`). This page does the same for everything else: the source, the four devices, and the mixer.
The goal isn't a clone. The PRD fixes Bryo's chain, and the FM-1 is a much smaller machine. I want someone who
knows the S-4 to find the controls where they expect them, and I want to spot sound-shaping ideas worth taking.

Where my S-4 facts come from: docs.torsoelectronics.com is blocked from my build machine, so I worked from
search results that quote the official device pages, the Sound On Sound review, the OS changelog, and the
community MIDI CC list at midi.guide. Where those disagree, I say so. Anything marked *(unverified)* needs a look
at the real manual before it's built.

None of GRAIN, RESONATOR, COLOR or SPACE makes sound yet (phases 4 and 5). Changing their knobs now only touches
`param.c`, the pictograms and the screens. Once the DSP exists, the same change costs a rewrite, so now is the
time to settle them.

## The short version

| Area | S-4 | Bryo now | Gap | What I'd do |
| --- | --- | --- | --- | --- |
| Source | TAPE, DISC, POLY, BYPASS (input) | TAPE | SYNTH and POLY are planned; no input; no DISC | Build SYNTH now (the PRD's), POLY next. Skip DISC (no storage to stream from). The input arrives with USB audio in. |
| TAPE | SPEED, START, LENGTH, ROTATE, XFADE, SOS (bipolar), LEVEL; FREE / SYNC timing | STRT LEN SPD DUB / FADE REV HALF GAIN / REEL | ROTATE, bipolar SOS, sync timing | Add ROTATE; make DUB bipolar like SOS; rename FADE to XFADE; sync mode with the sequencer clock (phase 7) |
| GRAIN (MOSAIC) | PITCH, RATE, SIZE, CONTOUR, WARP, SPRAY, PATTERN, WET; pitch scale; random pitch, level, reverse | SIZE DENS TUNE SPRD / MIX JIT WIN REV | WARP, PATTERN, scale quantize, random pitch | Rename to S-4 words; add a page 3 with WARP, PATN, SCAL, PRND |
| RESONATOR (RING) | CUTOFF, RES, DECAY, PITCH, SLOPE, TONE, SCALE, WET | ROOT FDBK DAMP MIX, no page 2 | the whole filter half, the scale | Rename to PITCH DEC TONE WET; add page 2: CUT RES SLOP SCAL |
| COLOR (DEFORM) | DRIVE, CRUSH (BIT / REDUX / both), TILT, NOISE, NOISE DECAY, NOISE TONE, WET | DRIV CRSH NOIS TONE / LVL MIX SRR GATE | noise decay and tone; crush as one knob with modes | Rename TONE to TILT; turn GATE into NDEC, add NTON; fold SRR into CRSH modes |
| SPACE (VAST) | DELAY, TIME, REVERB, SIZE, FEEDBACK, SPREAD (modes), TONE (bipolar), DECAY | TIME FDBK SIZE DEC / DMIX RMIX PRE WIDE | TONE | Add a bipolar TONE in PRE's place; PRE moves to page 3 |
| Mixer | level, pan, DJ filter, 4 sends per track, master compressor, main level | level + meter; LOW HIGH FILT PAN | sends, master compressor | Sends are phase 6's routing; put the compressor on the master (it's an open question in the PRD) |

## Source

The S-4 calls the first slot MATERIAL and offers TAPE, DISC, POLY and BYPASS. Bryo calls it the source, and the
architecture doc already plans TAPE, SYNTH and POLY.

- **SYNTH** isn't on the S-4. It's the PRD's, and I'm building it next (see "SYNTH" in `bryo-architecture.md`).
- **POLY** on the S-4 is an 8-voice chromatic sampler with its own amp and filter envelopes. Bryo's planned POLY
  matches that in spirit (4 voices, ADSR, a state-variable filter). I'm keeping 4 voices because of the CPU.
- **DISC** streams long recordings off the S-4's storage. The FM-1 has 1 MB of flash in total, so there's nothing
  to stream from. Skipped.
- **BYPASS** passes the S-4's audio input through the chain. The FM-1 has no audio input, but USB audio in
  (task #12) gives it one. When that lands, it becomes a source called INPUT. That's cleaner than a record-only
  input on TAPE.

## TAPE

| S-4 | Bryo | Notes |
| --- | --- | --- |
| SPEED | SPD | the same: bipolar, ±200 % |
| START | STRT | the same |
| LENGTH | LEN | On the S-4, LENGTH past the sample adds silence. Bryo's is a % of what's there. Mine is simpler, so I'm keeping it. |
| SOS (bipolar) | DUB (0..100) | SOS sets how much of the old loop survives each overdub pass. *(Unverified: what its negative half does.)* I'd make DUB bipolar -100..100: negative fades the old loop on each pass even without new input, and that's the tape-loop decay people use SOS for. |
| XFADE | FADE | the same idea (the seam's crossfade). Rename to XFADE. |
| ROTATE | none | Shifts the loop's content against the window without moving the window. It's cheap here: an offset added to the read and write position. Goes on page 3 beside REEL. |
| LEVEL | GAIN (record gain) | Different jobs. The mixer fader is the S-4's LEVEL. Keep GAIN. |
| FREE / SYNC timing | none | In SYNC, START and LENGTH are in beats and bars. That needs the sequencer clock (phase 7), so I'll note it there. |
| none | REV, HALF | Bryo's own (OP5 and OP6 flip them). Keep. |

## GRAIN (the S-4's MOSAIC)

| S-4 | Bryo | Notes |
| --- | --- | --- |
| PITCH | TUNE | Rename to PTCH. |
| RATE | DENS | The grain trigger rate. Rename to RATE. Keeping "density" in the docs is fine. |
| SIZE | SIZE | the same |
| CONTOUR | WIN | the grain's window. Rename to CONT. |
| SPRAY | SPRD | the jitter of where grains read. Rename to SPRY. On the S-4 (OS 2.2) WARP's old job moved into SPRAY's modes. |
| WET | MIX | Rename to WET. |
| WARP | none | How fast the read point moves through the buffer: freeze at 0, time-stretch in between, forwards or backwards. That's the big one missing. Page 3. |
| PATTERN | none | The order grains fire in (even, swung, clustered) *(unverified detail)*. Page 3. I'd start with 4 patterns. |
| pitch scale | none | Quantizes grain pitch to a scale. Page 3 (SCAL), sharing RESONATOR's scale list. |
| random pitch / level / reverse | JIT, REV | JIT becomes PRND (random pitch, ± semitones). REV (reverse chance) stays. |

That gives three pages: PTCH RATE SIZE SPRY / WET CONT REV PRND / WARP PATN SCAL (one knob left).

## RESONATOR (the S-4's RING)

The S-4's RING is a tuned filter bank (48 bands) with a resonator. Bryo's PRD resonator is a comb or modal
resonator tuned to a root note. Same family, different engine. The knob names can still match.

| S-4 | Bryo | Notes |
| --- | --- | --- |
| PITCH | ROOT | Keep the note format (A1..A5) but call it PTCH. |
| DECAY | FDBK | how long it rings. Rename to DEC. |
| TONE | DAMP | the brightness of the ringing. Rename to TONE. |
| WET | MIX | Rename to WET. |
| CUTOFF, RES, SLOPE | none | A filter in front of the resonator: cutoff, resonance, and SLOPE morphing LP → BP → HP. One state-variable filter does all three cheaply. Page 2. |
| SCALE | none | Tunes the resonator's partials (or its 4 strings) to a scale: CHR MAJ MIN PEN *(the S-4's list unverified)*. Page 2. |

That gives two pages: PTCH DEC TONE WET / CUT RES SLOP SCAL.

## COLOR (the S-4's DEFORM)

| S-4 | Bryo | Notes |
| --- | --- | --- |
| DRIVE | DRIV | the same |
| CRUSH (modes BIT, REDUX, BIT + REDUX) | CRSH + SRR | The S-4 uses one knob plus a mode. I'd do the same and free a knob: CRSH amount, CMOD (BIT, RATE, BOTH). |
| TILT | TONE | Rename to TILT. |
| NOISE | NOIS | the same |
| NOISE DECAY | none | The S-4's noise follows the input's envelope. DECAY sets how long it hangs on. Bryo's GATE was a threshold for the same envelope, so DECAY replaces it. |
| NOISE TONE | none | the noise's colour. Add. |
| WET | MIX | Rename to WET. |
| none | LVL | Bryo's output trim. Keep (drive adds level). |

That gives two pages: DRIV CRSH NOIS TILT / NDEC NTON CMOD WET, and LVL on page 3.

## SPACE (the S-4's VAST)

| S-4 | Bryo | Notes |
| --- | --- | --- |
| DELAY | DMIX | Rename to DLY. |
| TIME | TIME | the same |
| REVERB | RMIX | Rename to VERB. |
| SIZE | SIZE | the same |
| FEEDBACK | FDBK | the same (the S-4 now goes to 200 %; Bryo's 100 % is safer on a small DSP) |
| DECAY | DEC | the same |
| SPREAD (modes) | WIDE | Rename to SPRD. Modes later. |
| TONE (bipolar HP / LP on the feedback and the reverb) | none | Add: one bipolar knob, the same DJ-style filter as the mixer's FILT. |
| none | PRE | Bryo's pre-delay. Keep, on page 3. |

That gives three pages: TIME FDBK SIZE DEC / DLY VERB TONE SPRD / PRE.

## Mixer

The S-4's master section: each track gets a DJ-style filter, then level and pan, and the sum goes through a
master compressor to the main level. Each track also has four sends into any track, itself included.

- **Level, pan, DJ filter:** Bryo has them (FILT is the same one-knob LP / HP). Its LOW and HIGH shelves go
  beyond the S-4. Keep them.
- **Sends:** this is phase 6's routing ("ROUTING ARRIVES IN PHASE 6"). The S-4's model (four send levels per
  track, into any track) fits the ALGORITHM encoder well. I'll plan phase 6 around it.
- **Master compressor:** one of the PRD's open questions was where it sits. The S-4 answers it: after the sum,
  before the main level. Bryo's master stage already has the soft clip. The compressor goes in front of it.
- **Main level:** Bryo's MASTER does this.

## What I'd change, and when

1. **Now, while it costs only tables:** the renames and the new knobs above, for GRAIN, RESONATOR, COLOR and
   SPACE (`param.c`, `ui_px.c` pictograms, `ui_viz.c`, the golden screens, `docs/controls.tsv`).
2. **Now, with sound:** TAPE's ROTATE, bipolar DUB and XFADE (`tape.c`, its tests).
3. **Phase 4–5:** the DSP behind the new knobs (WARP, PATTERN, the resonator's filter and scale, noise decay,
   SPACE's TONE).
4. **Phase 6:** sends and the master compressor.
5. **Phase 7:** SYNC timing for TAPE.

Items 1 and 2 change what's on screen and what the knobs are called, so I'm holding them until you've looked at
this page. SYNTH doesn't depend on any of it.

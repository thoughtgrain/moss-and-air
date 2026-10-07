/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the source engines, one row each: what starts a track's chain (docs/bryo-architecture.md, "Source
 * engines"). A track's source is tp[t].src (param.c), picked by holding HOME and turning SELECT. Adding an engine is
 * one file, one knob table in param.c and one row here.
 *
 * Every track keeps its tape whatever its source: REC prints onto it. On a TAPE track REC records the other tracks
 * (chain.c); on any other source it records the track's own sound, so you can play the synth onto the tape, then
 * switch back to TAPE and slice it.
 *
 * Switching sources never clicks: chain.c fades the old one out and the new one in over about 2 ms. A source that
 * isn't the track's renders nothing (its notes wait where they were: a held synth note releases the moment its
 * key is seen up, when the track switches back). */

typedef struct {
    /* one block of track t into out: keys are the white keys held (0 when the track isn't the focused one, or the
     * source isn't the track's any more); rec is what REC records while it's armed (0: not armed), which only the
     * tape takes */
    void (*render)(uint32_t t, uint32_t keys, const int32_t *rec, int32_t *out, uint32_t n);
} source_t;

static const source_t SOURCES[NSRC] = {
    {tape_block},                /* TAPE: its loop, the keys play its 16 slices (tape.c) */
    {syn_block},                 /* SYNTH: the keys play notes (synth.c) */
    {pol_block},                 /* POLY: the keys play a sound at their pitches (poly.c) */
};

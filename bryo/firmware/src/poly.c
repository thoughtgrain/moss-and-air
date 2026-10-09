/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: POLY, a source engine (docs/bryo-architecture.md, "Source engines"): a sound played across the keys, up to
 * POL_NV notes at once. The sound is any reel choice (REEL): a factory reel or one of yours in flash, read through
 * the XIP window as TAPE reads them, or the track's own RAM tape, so you can record a phrase and play it as an
 * instrument. Nothing is copied, so POLY costs no tape RAM: each voice has a block reader (tape.c's) and a little
 * state.
 *
 * A voice: its head starts at STRT and runs at the key's pitch against the sound's own (white key 1 at OCT 3, plus
 * TUNE, is the sound as it was recorded: 22.05 kHz read at 0.5 a sample, like TAPE at 100 %); its ADSR shapes the
 * level and, by ENV, the cutoff of its state-variable filter (TYPE: low-, band- or high-pass). At the sound's end it
 * fades over one block and frees itself. Voices are taken as SYNTH's are: a silent one, the quietest releasing one,
 * else the oldest (which keeps its filter state and ramps from its level, so a steal doesn't click).
 *
 * Audio ISR only, like synth.c: the main loop writes tp[t].pol and the octave. */

typedef struct {
    int32_t pos, inc;            /* the head and its step, Q12 tape samples */
    int32_t ic1, ic2;            /* the filter's state */
    int32_t env;                 /* Q24 */
    int32_t amp;                 /* the last block's level, Q15 */
    uint32_t age;
    uint8_t key, stage;          /* 0 silent, 1 attack, 2 decay and sustain, 3 release */
} pol_voice_t;

typedef struct {
    pol_voice_t v[POL_NV];
    uint32_t keys, age;
} pol_t;

static pol_t pol[NTRK];
static tape_rd_t pol_rd[NTRK][POL_NV] __attribute__((section(".pool")));   /* each voice's block (8 KB in all) */

/* the head's step for white key k on track t, Q12 tape samples per output sample: 0.5 at the sound's own pitch */
static int32_t pol_inc(uint32_t t, uint32_t k)
{
    int32_t n = clamp(12 * ((int32_t)track[t].octave + 1) + (int32_t)k + TPD(t, MA_POL)[PL_TUNE], 0, 127);
    return (int32_t)(pitch_inc((uint32_t)n * 16u) / (pitch_inc(48u * 16u) >> 11));   /* (C3: the sound's own) */
}

static void pol_start(uint32_t t, uint32_t k, uint32_t nv, uint32_t len)
{
    pol_t *s = &pol[t];
    pol_voice_t *v = &s->v[0];
    uint32_t j, best = 0xFFFFFFFFu;
    for (j = 0; j < nv && s->v[j].stage; j++)              /* a silent voice */
        ;
    if (j < nv) {
        v = &s->v[j];
    } else {
        for (j = 0; j < nv; j++)                            /* the quietest releasing one */
            if (s->v[j].stage == 3u && (uint32_t)s->v[j].env < best) {
                best = (uint32_t)s->v[j].env;
                v = &s->v[j];
            }
        if (best == 0xFFFFFFFFu)                            /* the oldest */
            for (j = 0, v = &s->v[0]; j < nv; j++)
                if (s->v[j].age - v->age > 0x80000000u)
                    v = &s->v[j];
    }
    if (!v->stage && !v->amp) {                             /* from silence: a clean start */
        v->ic1 = v->ic2 = 0;
        v->env = 0;
    }
    v->pos = (int32_t)(len * (uint32_t)TPD(t, MA_POL)[PL_STRT] / 100u) << 12;
    v->inc = pol_inc(t, k);
    v->key = (uint8_t)k;
    v->stage = 1;
    v->age = ++s->age;
    pol_rd[t][v - s->v].ok = 0;
}

static void pol_voice(uint32_t t, pol_voice_t *v, tape_rd_t *rd, const tape_view_t *vw, int32_t *out, uint32_t n)
{
    const int16_t *p = TPD(t, MA_POL);
    tsvf_t flt;
    uint32_t i, mode = (uint32_t)clamp(p[PL_TYPE], 0, 2);
    int32_t a0 = v->amp, a1, e15, pos = v->pos, ic1 = v->ic1, ic2 = v->ic2;
    switch (v->stage) {                                     /* the envelope, a step per block (as SYNTH's) */
    case 1:
        v->env += (int32_t)ENV_LIN[p[PL_ATK] & 127];
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += mulq16(p[PL_SUS] * ((1 << 24) / 100) - v->env, ENV_EXP[p[PL_DEC] & 127]);
        break;
    case 3:
        v->env -= mulq16(v->env, ENV_EXP[p[PL_REL] & 127]);
        if (v->env < 1 << 12) {
            v->env = 0;
            v->stage = 0;
        }
        break;
    default:
        v->env = 0;
        break;
    }
    e15 = v->env >> 9;
    a1 = e15;
    if ((uint32_t)((pos + v->inc * (int32_t)n) >> 12) >= vw->len) {   /* the sound's end: fade out, done */
        a1 = 0;
        v->stage = 0;
        v->env = 0;
    }
    tsvf_coef(&flt, (p[PL_CUT] << 8) + p[PL_ENV] * 96 * (e15 >> 7) / 100, p[PL_RES] * 127 / 100);
    for (i = 0; i < n; i++) {
        int32_t x = tape_read(vw, rd, pos), y;
        pos += v->inc;
        y = mode || p[PL_CUT] < 127 || p[PL_RES] || p[PL_ENV] ? tsvf_mode(&flt, x >> 1, &ic1, &ic2, mode) << 1 : x;
        out[i] += mulq15(y, a0 + (((a1 - a0) * (int32_t)i) >> CTL_LOG2));
    }
    v->pos = pos;
    v->ic1 = ic1;
    v->ic2 = ic2;
    v->amp = a1;
}

/* audio ISR: one block of track t's POLY into out; keys: the white keys held; rec: unused (REC prints POLY onto the
 * tape, chain.c) */
static void pol_block(uint32_t t, uint32_t keys, const int32_t *rec, int32_t *out, uint32_t n)
{
    pol_t *s = &pol[t];
    tape_view_t vw;
    uint32_t on = keys & ~s->keys, off = s->keys & ~keys, nv = (uint32_t)clamp(TPD(t, MA_POL)[PL_VOIC], 1, POL_NV), j, k;
    (void)rec;
    for (j = 0; j < n; j++)
        out[j] = 0;
    tape_view_of(t, (uint32_t)clamp(tp[t].pol[PL_REEL], 0, (int32_t)(NREEL + USLOT_N)), &vw);
    s->keys = keys;
    for (k = 0; off && k < NWHITE; k++)                     /* keys let go: their voices release */
        if ((off >> k) & 1u)
            for (j = 0; j < POL_NV; j++)
                if (s->v[j].key == k && s->v[j].stage && s->v[j].stage != 3u)
                    s->v[j].stage = 3;
    for (j = nv; j < POL_NV; j++)                           /* VOIC turned down: the voices above it let go */
        if (s->v[j].stage && s->v[j].stage != 3u)
            s->v[j].stage = 3;
    for (k = 0; on && vw.len && k < NWHITE; k++)            /* new keys, lowest first (nothing to play: none) */
        if ((on >> k) & 1u)
            pol_start(t, k, nv, vw.len);
    for (j = 0; j < POL_NV; j++)
        if (s->v[j].stage || s->v[j].amp)
            pol_voice(t, &s->v[j], &pol_rd[t][j], &vw, out, n);
}

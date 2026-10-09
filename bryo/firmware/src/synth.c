/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: SYNTH, a source engine (docs/bryo-architecture.md, "Source engines"). A small subtractive voice taken from
 * Felucca's ANALOG engine: two band-limited oscillators of one wave (the second DTUN cents up), white noise, drive,
 * a resonant trapezoidal low-pass, and one ADSR that sets the level and, by ENV, moves the cutoff. Up to SYN_NV
 * voices per track (VOIC); at 1 it plays legato, gliding by GLID.
 *
 * Keys: the white keys of the focused track, read by the audio ISR once per block (chain.c), so a note starts
 * within one block. White key k is the semitone k above C of the track's octave (OCT-/OCT+): sixteen keys, sixteen
 * semitones, because the black keys are the PRD's macros.
 *
 * A voice for a new key: a silent one, else the quietest releasing one, else the oldest. A stolen voice keeps its
 * phases and filter state and attacks from its current level, so taking it over doesn't click. Every block's
 * level is ramped from the last block's, so the envelope never steps.
 *
 * Cost: per voice per sample, two oscillators, one noise step when NOIS is up, one filter; coefficients per block.
 * Everything runs in the audio ISR; the main loop only writes the knobs (tp[t].syn) and the source (tp[t].src). */

typedef struct {
    uint32_t ph[2];              /* the oscillators' phases */
    int32_t ic1, ic2;            /* the filter's state */
    int32_t nst;                 /* the noise's state */
    int32_t env;                 /* the envelope, Q24 */
    int32_t amp;                 /* the last block's level, Q15 (the next block ramps from it) */
    int32_t p16, to16;           /* the pitch now and where it glides to, 1/16 semitone */
    uint32_t age;                /* when its note started (the oldest is stolen first) */
    uint8_t key, stage;          /* the white key; 0 silent, 1 attack, 2 decay and sustain, 3 release */
} syn_voice_t;

typedef struct {
    syn_voice_t v[SYN_NV];
    uint32_t keys;               /* the white keys held last block */
    uint32_t age;
    int32_t last16;              /* the last note started (a glide starts from it) */
} syn_t;

static syn_t syn[NTRK];

/* white key k of track t as a pitch, 1/16 semitone, before TUNE (added as the voice plays, so a modulated TUNE
 * moves a held note too: mod.c) */
static int32_t syn_note16(uint32_t t, uint32_t k)
{
    int32_t n = 12 * ((int32_t)track[t].octave + 1) + (int32_t)k;
    return clamp(n, 0, 127) * 16;
}

static void syn_start(uint32_t t, uint32_t k, uint32_t nv)
{
    syn_t *s = &syn[t];
    syn_voice_t *v = &s->v[0];
    int32_t note = syn_note16(t, k), glide = TPD(t, MA_SYN)[SY_GLID] > 0;
    uint32_t j;
    if (nv == 1u && (v->stage == 1u || v->stage == 2u)) {   /* one voice, a key still down: legato */
        v->key = (uint8_t)k;
        v->to16 = note;
        if (!glide)
            v->p16 = note;
        s->last16 = note;
        return;
    }
    for (j = 0; j < nv && s->v[j].stage; j++)              /* a silent voice */
        ;
    if (j == nv) {
        uint32_t best = 0xFFFFFFFFu;
        for (j = 0; j < nv; j++)                            /* the quietest releasing one */
            if (s->v[j].stage == 3u && (uint32_t)s->v[j].env < best) {
                best = (uint32_t)s->v[j].env;
                v = &s->v[j];
            }
        if (best == 0xFFFFFFFFu)                            /* the oldest */
            for (j = 0, v = &s->v[0]; j < nv; j++)
                if (s->v[j].age - v->age > 0x80000000u)
                    v = &s->v[j];
    } else {
        v = &s->v[j];
    }
    if (!v->stage && !v->amp) {                             /* from silence: a clean start */
        v->ph[0] = 0;
        v->ph[1] = 0x40000000u;
        v->ic1 = v->ic2 = 0;
        v->env = 0;
        if (!v->nst)
            v->nst = 0x1234567 + (int32_t)(t * 7919u + k);
    }
    v->key = (uint8_t)k;
    v->stage = 1;
    v->age = ++s->age;
    v->to16 = note;
    v->p16 = glide && s->last16 ? s->last16 : note;
    s->last16 = note;
}

/* one block of one voice, added into out */
static void syn_voice(uint32_t t, syn_voice_t *v, int32_t *out, uint32_t n)
{
    const int16_t *p = TPD(t, MA_SYN);
    tsvf_t flt;
    uint32_t wave = (uint32_t)p[SY_WAVE], i, inc1, inc2, ph0 = v->ph[0], ph1 = v->ph[1];
    int32_t e15, a0 = v->amp, a1, cut, m2 = p[SY_MIX] * 327, m1 = 32767 - m2, nz = p[SY_NOIS] * 254;
    int32_t drv = p[SY_DRV], ic1 = v->ic1, ic2 = v->ic2, nst = v->nst, pp;
    switch (v->stage) {                                     /* the envelope: one step per block (as Felucca's) */
    case 1:
        v->env += (int32_t)ENV_LIN[p[SY_ATK] & 127];
        if (v->env >= 1 << 24) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += mulq16(p[SY_SUS] * ((1 << 24) / 100) - v->env, ENV_EXP[p[SY_DEC] & 127]);
        break;
    case 3:
        v->env -= mulq16(v->env, ENV_EXP[p[SY_REL] & 127]);
        if (v->env < 1 << 12) {
            v->env = 0;
            v->stage = 0;
        }
        break;
    default:
        v->env = 0;
        break;
    }
    if (v->p16 != v->to16) {                                /* the glide: GLID's time for an octave and a half */
        int32_t st = p[SY_GLID] ? (int32_t)(ENV_LIN[p[SY_GLID] & 127] >> 14) : 1 << 16, d = v->to16 - v->p16;
        if (st < 1)
            st = 1;
        v->p16 += d > st ? st : d < -st ? -st : d;
    }
    e15 = v->env >> 9;
    a1 = e15;
    pp = clamp(v->p16 + mod_pitch16(t, FN_SYN, p[SY_TUNE]), 0, 2047);   /* (TUNE as it is now, to 1/16 semitone
                                                                           * when it's modulated: mod.c) */
    inc1 = pitch_inc((uint32_t)pp);
    inc2 = p[SY_DTUN] ? cents_inc(pp, p[SY_DTUN], 0) : inc1;
    cut = (p[SY_CUT] << 8) + p[SY_ENV] * 96 * (e15 >> 7) / 100 + p[SY_KTRK] * (pp - 60 * 16) * 16 / 100;
    tsvf_coef(&flt, cut, p[SY_RES] * 127 / 100);
    for (i = 0; i < n; i++) {
        int32_t a, b, s, y, k;
        switch (wave) {
        case 0:
            a = osc_sine(ph0);
            b = osc_sine(ph1);
            break;
        case 1:
            a = osc_tri(ph0);
            b = osc_tri(ph1);
            break;
        case 2:
            a = osc_pulse(ph0, inc1, 0x80000000u);
            b = osc_pulse(ph1, inc2, 0x80000000u);
            break;
        case 4:                                             /* a 25 % pulse: thinner, nasal */
            a = osc_pulse(ph0, inc1, 0x40000000u);
            b = osc_pulse(ph1, inc2, 0x40000000u);
            break;
        default:
            a = osc_saw(ph0, inc1);
            b = osc_saw(ph1, inc2);
            break;
        }
        ph0 += inc1;
        ph1 += inc2;
        s = mulq15(a, m1) + mulq15(b, m2);
        if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (drv)                                            /* drive: up to 3x into the soft clip */
            s = softclip(s * (100 + 2 * drv) / 100);
        y = tsvf_lp(&flt, s >> 1, &ic1, &ic2);             /* linear to half scale, then a soft knee */
        k = y < 0 ? -y : y;
        if (k > 16000) {
            k = 16000 + (softclip((k - 16000) * 2) >> 1);
            y = y < 0 ? -k : k;
        }
        out[i] += mulq15(mulq15(y << 1, a0 + (((a1 - a0) * (int32_t)i) >> CTL_LOG2)), VOICE_FS);
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ic1 = ic1;
    v->ic2 = ic2;
    v->nst = nst;
    v->amp = a1;
}

/* audio ISR: one block of track t's synth into out; keys: the white keys held (bit k = key k); rec: unused (the
 * synth records nothing; REC prints it onto the tape, chain.c) */
static void syn_block(uint32_t t, uint32_t keys, const int32_t *rec, int32_t *out, uint32_t n)
{
    syn_t *s = &syn[t];
    uint32_t on = keys & ~s->keys, off = s->keys & ~keys, nv = (uint32_t)clamp(TPD(t, MA_SYN)[SY_VOIC], 1, SYN_NV), j, k;
    (void)rec;
    for (j = 0; j < n; j++)
        out[j] = 0;
    s->keys = keys;
    for (k = 0; off && k < NWHITE; k++)                     /* keys let go: their voices release */
        if ((off >> k) & 1u)
            for (j = 0; j < SYN_NV; j++) {
                syn_voice_t *v = &s->v[j];
                if (v->key != k || !v->stage || v->stage == 3u)
                    continue;
                if (nv == 1u && keys) {                     /* legato: back to the highest key still down */
                    uint32_t h = NWHITE - 1u;
                    while (!((keys >> h) & 1u))
                        h--;
                    v->key = (uint8_t)h;
                    v->to16 = syn_note16(t, h);
                    if (!TPD(t, MA_SYN)[SY_GLID])
                        v->p16 = v->to16;
                } else {
                    v->stage = 3;
                }
            }
    for (j = nv; j < SYN_NV; j++)                           /* VOIC turned down: the voices above it let go */
        if (s->v[j].stage && s->v[j].stage != 3u)
            s->v[j].stage = 3;
    for (k = 0; on && k < NWHITE; k++)                      /* new keys, lowest first */
        if ((on >> k) & 1u)
            syn_start(t, k, nv);
    for (j = 0; j < SYN_NV; j++)
        if (s->v[j].stage || s->v[j].amp)
            syn_voice(t, &s->v[j], out, n);
}

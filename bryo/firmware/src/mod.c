/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: modulation (phase 7). Each track has four modulator slots (param.c: LFO, ADSR, SEQ or FOLLOW, with their
 * knobs) and a depth from each slot to each knob it can move: hold a slot's pad and turn a knob on a device, source
 * or channel page, and that knob's depth for that slot changes (-100..100 %). A depth of 100 moves the knob across
 * its whole range at the slot's full swing.
 *
 * How it runs (docs/bryo-architecture.md, "Modulation, as built"):
 *   main loop  owns the depths (mdep). Every change rebuilds the track's list of the depths that aren't 0, sorted
 *              by target, into the buffer the ISR isn't reading, then flips mod_cur[t] (one byte). The ISR can't
 *              be interrupted by the main loop, so it always reads a whole list.
 *   audio ISR  once per control block, before any track renders (mod_tick): the clock, then for each track with a
 *              list, the slots the list uses, then a copy of each knob array the list touches (a device's 20, a
 *              source's 20, the channel's 4) with its depths added. The device code reads its knobs through
 *              TPD(t, array), which points at that copy, or at tp[t]'s own array when nothing modulates it. So a
 *              track without modulation, or an array without depths, reads exactly what it always read.
 *
 * Each depth's range and scale are worked out when it changes (main loop), so the ISR's work per depth is a
 * multiply, a shift and a clamp. The ISR's state lives in the pool; the depths themselves (what a project will save)
 * in main RAM.
 *
 * The targets are numbered so a depth table is one flat array per slot (NPK = 20 knobs an array):
 *   0..79    GRAIN, RESONATOR, COLOR, SPACE: (device - 1) x NPK + knob
 *   80..159  the sources: TAPE, SYNTH, POLY, DRUM: 80 + source x NPK + knob (each source keeps its own depths)
 *   160..163 the channel strip: LOW HIGH FILT PAN
 * The reel choices (TAPE's REEL, POLY's REEL) aren't targets: the main loop prepares memory for the reel a track
 * plays, so the ISR mustn't switch it. Nor is a knob marked nomod (DRUM's PATN, which writes the pattern when
 * turned, and its page's own state: LEN, INST, BAR, MODE).
 *
 * Engines: the LFO is bipolar (it swings both ways around the knob), ADSR, SEQ and FOLLOW are unipolar (they push one
 * way: depth's sign says which). AMT scales a slot, OFS shifts it. SPRD is a second, right-hand output; only PAN
 * uses it (the left and right channels then pan apart), the other targets take the left one. */

#define MOD_TSRC (4u * NPK)
#define MOD_TCH (8u * NPK)
#define MOD_NTGT (8u * NPK + NCH)
#define MOD_TG(d, k) (((d) - 1u) * NPK + (k))   /* device d's (GRAIN..SPACE) knob k as a target */
#define MOD_MAX 32u                  /* depths that aren't 0, per track (the main loop refuses more) */
enum { MA_SYN = NDEV, MA_POL, MA_DRM, MA_CH, MOD_NARR };   /* the knob arrays: dev[DEV_*], then SYNTH's, POLY's, DRUM's,
                                                             * the channel */

/* ------------------------------------------------------------ depths --- */
static int8_t mdep[NTRK][NSLOT][MOD_NTGT];   /* main loop only: the depth from slot s to target g, -100..100 */
static struct {                              /* MONO's clear and SAVE's undo of it: one track's depths */
    int8_t dep[NSLOT][MOD_NTGT];
    uint8_t trk, valid;
} mod_undo_buf;

/* the knobs that modulate finer than the knob (below): four pitches, then four filter cutoffs */
enum { FN_SYN, FN_POL, FN_RESO, FN_GRAIN, FN_SCUT, FN_PCUT, FN_RCUT, FN_FILT, FN_N };
#define FN_PITCHES FN_SCUT           /* FN_* under it are pitches (a SEQ's steps land on their semitones) */
/* knob k of array a as a fine knob (FN_*), or FN_N */
static inline uint32_t mod_fine_of(uint32_t a, uint32_t k)
{
    return a == MA_SYN ? (k == SY_TUNE ? FN_SYN : k == SY_CUT ? FN_SCUT : FN_N)
         : a == MA_POL ? (k == PL_TUNE ? FN_POL : k == PL_CUT ? FN_PCUT : FN_N)
         : a == DEV_RESO ? (k == 0u ? FN_RESO : k == 4u ? FN_RCUT : FN_N)   /* (RESONATOR: PTCH knob 1, CUT knob 5) */
         : a == DEV_GRAIN && k == 2u ? FN_GRAIN                             /* (GRAIN's PTCH: knob 3) */
         : a == MA_CH && k == CH_FILT ? FN_FILT : FN_N;
}

typedef struct {                     /* a depth, ready for the ISR */
    uint8_t slot, arr, k, tgt;       /* from slot (bits 0..3; bits 4..7: the knob as a fine one, FN_*), to knob k of array
                                      * arr (target tgt) */
    int16_t lo, hi;                  /* the knob's range */
    int32_t fq;                      /* depth x range, scaled: (out >> 3) x fq >> 19 is the move at out (Q15) */
} mod_ent_t;
typedef struct {
    uint8_t n;                       /* entries, sorted by target */
    uint8_t used;                    /* the slots the entries use, a bit each */
    uint16_t arrs;                   /* the knob arrays they touch, a bit each */
    mod_ent_t e[MOD_MAX];
} mod_list_t;
static mod_list_t mod_l[NTRK][2] __attribute__((section(".pool")));
static volatile uint8_t mod_cur[NTRK];       /* the list the ISR reads */
static uint8_t mod_n[NTRK];                  /* main loop: the depths that aren't 0 on each track */
static volatile uint8_t mod_on;              /* the tracks with a list, a bit each (the ISR skips the rest) */

/* target g's descriptor (0: not a target) */
static const pdesc_t *mod_tdesc(uint32_t g)
{
    const pdesc_t *d;
    uint32_t k = g % NPK;
    if (g < MOD_TSRC)
        d = &DEV_P[1u + g / NPK][k];
    else if (g < MOD_TCH)
        d = (g - MOD_TSRC) / NPK == SRC_TAPE ? &DEV_P[DEV_SRC][k] : (g - MOD_TSRC) / NPK == SRC_SYNTH ? &SYN_P[k]
          : (g - MOD_TSRC) / NPK == SRC_POLY ? &POL_P[k] : &DRM_P[k];
    else if (g < MOD_NTGT)
        d = &CH_P[g - MOD_TCH];
    else
        return 0;
    return pdesc_empty(d) || d->names == N_REEL || d->nomod ? 0 : d;
}

/* target g's knob array (MA_* / DEV_*) and its place in it */
static uint32_t mod_tarr(uint32_t g, uint32_t *k)
{
    uint32_t s = (g - MOD_TSRC) / NPK;
    *k = g < MOD_TCH ? g % NPK : g - MOD_TCH;
    if (g < MOD_TSRC)
        return 1u + g / NPK;
    if (g < MOD_TCH)
        return s == SRC_TAPE ? DEV_SRC : s == SRC_SYNTH ? MA_SYN : s == SRC_POLY ? MA_POL : MA_DRM;
    return MA_CH;
}

/* knob array a of track t, as the main loop sets it */
static int16_t *mod_base(uint32_t t, uint32_t a)
{
    return a < NDEV ? tp[t].dev[a] : a == MA_SYN ? tp[t].syn : a == MA_POL ? tp[t].pol : a == MA_DRM ? tp[t].drm : tp[t].ch;
}

/* main loop: track t's list from its depths, published whole */
static void mod_rebuild(uint32_t t)
{
    uint32_t b = mod_cur[t] ^ 1u, g, s, n = 0;
    mod_list_t *L = &mod_l[t][b];
    L->n = 0;
    L->used = 0;
    L->arrs = 0;
    for (g = 0; g < MOD_NTGT; g++)
        for (s = 0; s < NSLOT; s++)
            if (mdep[t][s][g]) {
                const pdesc_t *d = mod_tdesc(g);
                uint32_t k, a = mod_tarr(g, &k);
                mod_ent_t *e = &L->e[L->n];
                n++;
                if (!d || L->n >= MOD_MAX)
                    continue;
                e->slot = (uint8_t)(s | mod_fine_of(a, k) << 4);
                e->arr = (uint8_t)a;
                e->k = (uint8_t)k;
                e->tgt = (uint8_t)g;
                e->lo = d->min;
                e->hi = d->max;
                e->fq = mdep[t][s][g] * (d->max - d->min) * 1280 / 1000;   /* (2^22 / (100 x 32767) = 1.28) */
                L->used |= (uint8_t)(1u << s);
                L->arrs |= (uint16_t)(1u << a);
                L->n++;
            }
    mod_n[t] = (uint8_t)(n < 255u ? n : 255u);
    RING_PUBLISH();
    mod_cur[t] = (uint8_t)b;
    mod_on = (uint8_t)((mod_on & ~(1u << t)) | (L->n ? 1u << t : 0u));
}

/* main loop: the depths that aren't 0 on track t */
static uint32_t mod_count(uint32_t t) { return t < NTRK ? mod_n[t] : 0u; }

/* main loop: slot s's depth to target g on track t, nudged by d (%). Returns the new depth, or -128 when g can't be
 * modulated or the track already has MOD_MAX depths */
static int32_t mod_nudge(uint32_t t, uint32_t s, uint32_t g, int32_t d)
{
    int32_t v;
    if (t >= NTRK || s >= NSLOT || !mod_tdesc(g))
        return -128;
    v = clamp(mdep[t][s][g] + d, -100, 100);
    if (!mdep[t][s][g] && v && mod_count(t) >= MOD_MAX)
        return -128;
    mdep[t][s][g] = (int8_t)v;
    mod_rebuild(t);
    return v;
}

/* main loop: any slot's depth to target g on track t (the strip marks those knobs) */
static int mod_any(uint32_t t, uint32_t g)
{
    uint32_t s;
    for (s = 0; s < NSLOT && g < MOD_NTGT; s++)
        if (mdep[t][s][g])
            return 1;
    return 0;
}

/* main loop: MONO held. Track t's depths go (kept for SAVE's undo); its slots' knobs stay. 0: there were none. */
static int mod_clear(uint32_t t)
{
    uint32_t s, g;
    if (!mod_count(t))
        return 0;
    for (s = 0; s < NSLOT; s++)
        for (g = 0; g < MOD_NTGT; g++) {
            mod_undo_buf.dep[s][g] = mdep[t][s][g];
            mdep[t][s][g] = 0;
        }
    mod_undo_buf.trk = (uint8_t)t;
    mod_undo_buf.valid = 1;
    mod_rebuild(t);
    return 1;
}

/* main loop: SAVE held after a clear. -1: nothing to undo */
static int mod_undo(void)
{
    uint32_t s, g, t = mod_undo_buf.trk;
    if (!mod_undo_buf.valid || t >= NTRK)
        return -1;
    for (s = 0; s < NSLOT; s++)
        for (g = 0; g < MOD_NTGT; g++)
            mdep[t][s][g] = mod_undo_buf.dep[s][g];
    mod_undo_buf.valid = 0;
    mod_rebuild(t);
    return 0;
}

/* main loop: the highest value target g of track t can reach (its knob, plus every depth's full swing): the memory
 * polls ask this, so a device turned off but modulated up keeps its memory ready */
static int32_t mod_peak(uint32_t t, uint32_t g)
{
    const pdesc_t *d;
    uint32_t k, a = mod_tarr(g, &k);
    int32_t v = mod_base(t, a)[k], s, span;
    if (!mod_n[t] || !(d = mod_tdesc(g)))              /* (the common case: no depths on the track) */
        return v;
    span = 0;
    for (s = 0; s < (int32_t)NSLOT; s++)
        span += mdep[t][s][g] < 0 ? -mdep[t][s][g] : mdep[t][s][g];
    return clamp(v + span * (d->max - d->min) / 100, d->min, d->max);
}

/* ------------------------------------------------------ the LFO shape --- */
static int32_t isqrt(int32_t n)                                       /* floor(sqrt(n)), n >= 0 */
{
    int32_t x = n, y = (n + 1) / 2;
    if (n < 2)
        return n;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* the LFO at phase q (0..999 of a cycle), Q15: the shape (SIN TRI SQR SAW, or RND's step from rnd[]), SKEW's
 * warped phase, FOLD, CURV. The sound and the picture (ui_viz.c viz_wave) both draw from this. */
static int32_t lfo_at(const int16_t *v, int32_t q, const int16_t *rnd)
{
    int32_t sk = clamp(500 + v[2] * 5, 50, 950), sh = clamp(v[1], 0, 4), n = clamp(v[7], 1, 16), y, a;
    q = q < sk ? q * 500 / sk : 500 + (q - sk) * 500 / (1000 - sk);  /* SKEW: squashed to one side */
    if (sh == 0)
        y = sine_i((uint32_t)q * 4294967u);
    else if (sh == 1)
        y = q < 250 ? q * 131 : q < 750 ? (500 - q) * 131 : (q - 1000) * 131;
    else if (sh == 2)
        y = q < 500 ? 30000 : -30000;
    else if (sh == 3)
        y = (q - 500) * 65;                                           /* saw: a ramp up and the drop */
    else
        y = rnd[clamp(q * n / 1000, 0, n - 1)];                       /* RND: LEN held steps a cycle */
    y = y * (100 + v[3] * 3) / 100;                                   /* FOLD: overdrive, then fold back */
    while (y > 32767 || y < -32767)
        y = y > 0 ? 65534 - y : -65534 - y;
    a = y < 0 ? -y : y;                                               /* CURV: + narrows the curves, - widens them */
    if (v[4] > 0)
        a = (a * (100 - v[4]) + (a * a / 32767) * v[4]) / 100;
    else if (v[4] < 0)
        a = (a * (100 + v[4]) + isqrt(a * 32767) * (-v[4])) / 100;
    return y < 0 ? -a : a;
}

/* t (0..1000) along an envelope stage bent by its curve c (-100..100): 0 straight, + sags, - bows (ADSR, and its
 * picture) */
static int32_t env_bend(int32_t t, int32_t c)
{
    return t - c * t / 1000 * (1000 - t) / 400;
}

/* ------------------------------------------------------------- clock --- */
/* The tempo's beat, Q16, counted exactly (the remainder carried): from 0 when PLAY starts, and running on while
 * stopped so the synced LFOs and SEQs still move. */
static struct {
    uint32_t beat;                   /* beats, Q16 */
    uint32_t acc;                    /* the remainder, in 1/MOD_BEAT_DEN */
    uint32_t inc, rem;               /* a block's step at bpm: whole Q16 beats, and the remainder */
    uint16_t bpm;                    /* the tempo inc and rem are for */
    uint8_t was_playing;
    uint8_t started;                 /* PLAY started this block */
} mclk;
#define MOD_BEAT_DEN 2646000u        /* 60 x 44100: bpm x CTL x 65536 / this = beats Q16 a block */

/* the tempo's divisions (LFO SYNC BPM: RATE picks one; faster to the right), beats Q16 */
static const uint32_t MOD_DIV[16] = {64u << 16, 32u << 16, 16u << 16, 12u << 16, 8u << 16, 6u << 16, 4u << 16,
                                     3u << 16, 2u << 16, 3u << 15, 1u << 16, 3u << 14, 1u << 15, 21845u, 1u << 14,
                                     1u << 13};
static const uint32_t SEQ_DIV[6] = {4u << 16, 2u << 16, 1u << 16, 1u << 15, 1u << 14, 1u << 13};   /* 1 .. 1/32 */
static const uint32_t HOLD_DIV[5] = {0, 1u << 13, 1u << 14, 1u << 15, 1u << 16};                   /* FOLLOW HOLD */
#define MOD_PHRASE (16u << 16)       /* a SYNTH or POLY track's "loop": four bars */

/* -------------------------------------------------------- the engines --- */
typedef struct {                     /* ISR only: a slot's running state */
    uint32_t ph;                     /* LFO: phase, Q32; ADSR: the stage's progress, Q16 */
    uint32_t t0;                     /* the beat (Q16) of the last restart (LFO and SEQ synced to it) */
    uint32_t fade;                   /* LFO: cycles since the restart, Q16 (saturates) */
    int32_t sm, smr;                 /* LFO: SMTH's slewed outputs */
    int32_t lv, from;                /* ADSR: the level, and where the stage started (Q15); SEQ: the glide's start */
    int32_t out, outr;               /* this block's outputs, Q15: left, right (SPRD) */
    int32_t env, held;               /* FOLLOW */
    int16_t rnd[16];                 /* LFO: RND's steps, and the other shapes' level per cycle (VAR) */
    uint32_t seed;
    int16_t step;                    /* SEQ: the step playing (-1: none yet); its value */
    int16_t sval;
    uint32_t last_n;                 /* SEQ: the step count last block; FOLLOW: the hold count */
    uint8_t stage;                   /* ADSR: ENV_* */
    uint8_t oneshot;                 /* ADSR: started by the loop, not a key: released after DEC */
    uint8_t cyc;                     /* LFO: cycles, for VAR every LEN */
    uint8_t engine;                  /* the engine this state was set up for */
} mod_rt_t;
enum { ENV_IDLE, ENV_ATK, ENV_DEC, ENV_SUS, ENV_REL };

static mod_rt_t mrt[NTRK][NSLOT] __attribute__((section(".pool")));
static int16_t mda[NTRK][MOD_NARR][NPK] __attribute__((section(".pool")));   /* ISR: the modulated arrays */
static const int16_t *mdv[NTRK][MOD_NARR];     /* what the devices read: mda's array, or tp's own (mod_init) */
#define TPD(t, a) (mdv[t][a])                  /* track t's knob array a (DEV_*, MA_SYN, MA_POL, MA_DRM, MA_CH),
                                                * modulated */
static int16_t mod_pan_r[NTRK];                /* ISR: the right channel's PAN (SPRD), when the channel is modulated */
static uint16_t mod_arrs[NTRK];                /* ISR: the arrays mdv points into mda for */

/* Some knobs modulate finer than they turn: a knob holds whole steps (TUNE semitones, CUT semitones of the filter's
 * scale, FILT 1 %), so a slow LFO on one climbs a staircase: a trill instead of vibrato, a resonant sweep you can
 * count. mod_tick keeps each one's modulated value in 1/256 of its step (mod_f8) and the voices and filters read it
 * through mod_fine8 or mod_pitch16 (the knob itself when nothing modulates it). */
static int32_t mod_f8[NTRK][FN_N];             /* ISR: the modulated value, 1/256 of the knob's step */
static uint8_t mod_fmask[NTRK];                /* ISR: which of them hold one this block */
static inline int32_t mod_fine8(uint32_t t, uint32_t f, int32_t knob)
{
    return (mod_fmask[t] >> f) & 1u ? mod_f8[t][f] : knob * 256;
}
static inline int32_t mod_pitch16(uint32_t t, uint32_t f, int32_t knob)   /* a pitch, 1/16 semitone */
{
    return (mod_fmask[t] >> f) & 1u ? (mod_f8[t][f] + 8) >> 4 : knob * 16;
}
static uint32_t mod_keys_prev[NTRK];
static volatile uint8_t mod_seam;              /* a bit per track: its tape crossed its loop's start (tape.c) */
static uint32_t mod_phrase_n;

static uint32_t mod_rand(uint32_t *s)          /* xorshift32: the RND steps and SEQ's chances */
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}
static int32_t mod_rand_q15(uint32_t *s) { return (int32_t)(mod_rand(s) % 65535u) - 32767; }

/* a hash of a step count: SEQ's RND order and PROB's chance, the same for the same step every time round */
static uint32_t mod_hash(uint32_t a, uint32_t b)
{
    uint32_t h = (a + 0x7F4A7C15u) * 2654435761u ^ b * 40503u;
    return h ^ (h >> 15);
}

/* AMT then OFS, Q15 in, Q15 out (lo: -32767 bipolar, 0 unipolar) */
static int32_t mod_place(int32_t y, const int16_t *v, uint32_t ka, int32_t lo)
{
    return clamp(y * v[ka] / 100 + v[ka + 1u] * 327, lo, 32767);
}

/* a one-pole step toward x by a (Q15) */
static int32_t mod_slew(int32_t y, int32_t x, int32_t a) { return y + (((x - y) >> 1) * a >> 14); }

/* the coefficient that follows a change over a time (TIME_MS_X10's index) per control block, Q15 */
static int32_t mod_coef(int32_t ti)
{
    return (int32_t)(32768u * 726u / (TIME_MS_X10[ti & 127] * 100u + 363u));
}

/* SPRD as a lag on the right output (ADSR, FOLLOW): 0..100 -> up to ~200 ms */
static int32_t mod_lag(int32_t sprd) { return sprd ? (int32_t)(32768u * 726u / ((uint32_t)sprd * 2000u + 363u)) : 32767; }

/* LFO RATE (FREE) 0..127 -> a phase step per block, Q32: 0.02 Hz .. ~20 Hz, 10 octaves, 2^frac by a parabola */
static uint32_t lfo_inc(int32_t r)
{
    uint32_t e = (uint32_t)clamp(r, 0, 127) * 2560u / 127u, f = e & 255u;   /* octaves Q8 */
    uint32_t inc = 62333u << (e >> 8);
    return inc + (inc >> 8) * (f * (168u + (88u * f >> 8)) >> 8);   /* x (1 + f(0.656 + 0.344 f)) */
}

/* the LFO's random steps, new (the seed is the slot's) */
static void lfo_seed(mod_rt_t *m, uint32_t t, uint32_t s)
{
    uint32_t k;
    m->seed = 4242u + t * 977u + s * 131u;
    for (k = 0; k < 16u; k++)
        m->rnd[k] = (int16_t)(mod_rand_q15(&m->seed) * 31 / 32);
}

/* one time round done: VAR drifts the steps (RND), or every LEN cycles each cycle's level (the other shapes) */
static void lfo_round(mod_rt_t *m, const int16_t *v)
{
    uint32_t k, n = (uint32_t)clamp(v[7], 1, 16);
    m->cyc++;
    if (!v[6] || (v[1] != LFO_RND && m->cyc % n))
        return;
    for (k = 0; k < 16u; k++)
        m->rnd[k] = (int16_t)clamp(m->rnd[k] + mod_rand_q15(&m->seed) * v[6] / 100, -32000, 32000);
}

static int32_t lfo_eval(const mod_rt_t *m, const int16_t *v, uint32_t ph)
{
    uint32_t q = ph >> 16, q1 = q * 1000u >> 16, fr = q * 1000u & 65535u;
    int32_t a = lfo_at(v, (int32_t)q1, m->rnd), b = lfo_at(v, (int32_t)(q1 + 1u) % 1000, m->rnd), y;
    y = a + ((((b - a) >> 1) * (int32_t)(fr >> 1)) >> 14);
    if (v[1] != LFO_RND && v[6]) {                     /* VAR on a shape: this cycle's level */
        int32_t r = m->rnd[m->cyc % (uint32_t)clamp(v[7], 1, 16)];
        y = y * (32767 - (r < 0 ? -r : r) * v[6] / 100) >> 15;
    }
    return y;
}

static void mod_lfo(mod_rt_t *m, const int16_t *v, int restart)
{
    uint32_t ph0 = m->ph, inc, pofs = (uint32_t)clamp(v[10], 0, 359) * 11930464u, sofs = (uint32_t)clamp(v[11], 0, 100) * 21474836u;
    int32_t y, yr, a;
    if (v[12]) {                                        /* SYNC BPM: locked to the beat from the restart */
        uint32_t div = MOD_DIV[clamp(v[0], 0, 127) >> 3], rel, fr, cyc;
        if (restart)
            m->t0 = mclk.beat;
        rel = mclk.beat - m->t0;
        fr = (rel % div << 8) / (div >> 8);
        cyc = rel / div;
        if (!restart && cyc != m->last_n)
            lfo_round(m, v);                             /* one time round */
        m->last_n = cyc;
        m->ph = fr << 16;
        m->fade = cyc >= 2u ? 2u << 16 : cyc << 16 | fr;
        inc = 0;
    } else {
        inc = lfo_inc(v[0]);
        if (restart) {
            m->ph = 0;
            m->fade = 0;
            ph0 = 0;
        } else {
            m->ph += inc;
            if (m->fade < (2u << 16))
                m->fade += inc >> 16;
            if (m->ph < ph0)
                lfo_round(m, v);                         /* (wrapped: one time round) */
        }
    }
    if (restart) {
        m->cyc = 0;
        m->sm = m->smr = lfo_eval(m, v, m->ph + pofs);
    }
    y = lfo_eval(m, v, m->ph + pofs);
    yr = v[11] ? lfo_eval(m, v, m->ph + pofs + sofs) : y;
    a = v[5] ? 32767 >> (v[5] / 11) : 32767;            /* SMTH: a slew, ~0.4 s at 100 */
    a -= (a >> 1) * (v[5] % 11) / 11;
    m->sm = mod_slew(m->sm, y, a);
    m->smr = mod_slew(m->smr, yr, a);
    y = m->sm;
    yr = m->smr;
    if (v[14]) {                                        /* FADE: in over FADE % of a cycle from the restart */
        uint32_t len = (uint32_t)v[14] * 655u;
        if (m->fade < len) {
            y = y * (int32_t)(m->fade >> 1) / (int32_t)(len >> 1);
            yr = yr * (int32_t)(m->fade >> 1) / (int32_t)(len >> 1);
        }
    }
    m->out = mod_place(y, v, 8u, -32767);
    m->outr = mod_place(yr, v, 8u, -32767);
}

/* ADSR: gate (a key held on the track), press (a key went down), loop (the track's loop started) */
static void mod_adsr(mod_rt_t *m, const int16_t *v, int gate, int press, int loop)
{
    int32_t to, c, t1000;
    uint32_t inc;
    if (press || (loop && !gate)) {                     /* attack from where it is */
        m->stage = ENV_ATK;
        m->oneshot = (uint8_t)!press;
        m->from = m->lv;
        m->ph = 0;
    } else if (!gate && !m->oneshot && m->stage != ENV_IDLE && m->stage != ENV_REL) {
        m->stage = ENV_REL;
        m->from = m->lv;
        m->ph = 0;
    }
    switch (m->stage) {
    case ENV_ATK: to = 32767; inc = 475544u / TIME_MS_X10[v[0] & 127]; c = -v[4]; break;
    case ENV_DEC: to = v[2] * 32767 / 100; inc = 475544u / TIME_MS_X10[v[1] & 127]; c = v[5]; break;
    case ENV_REL: to = 0; inc = 475544u / TIME_MS_X10[v[3] & 127]; c = v[6]; break;
    case ENV_SUS: m->lv = v[2] * 32767 / 100; inc = 0; to = m->lv; c = 0; break;
    default: m->lv = 0; inc = 0; to = 0; c = 0; break;
    }
    if (inc) {
        m->ph += inc;
        t1000 = m->ph >= 65536u ? 1000 : (int32_t)(m->ph * 1000u >> 16);
        m->lv = m->from + (to - m->from) / 8 * env_bend(t1000, c) / 125;
        if (m->ph >= 65536u) {                          /* the stage's end */
            m->lv = to;
            m->from = to;
            m->ph = 0;
            if (m->stage == ENV_ATK)
                m->stage = ENV_DEC;
            else if (m->stage == ENV_DEC)
                m->stage = v[9] && gate ? ENV_ATK : m->oneshot ? ENV_REL : ENV_SUS;   /* LOOP: again, while held */
            else
                m->stage = ENV_IDLE;
            if (m->stage == ENV_REL)
                m->oneshot = 0;
        }
    }
    if (m->stage == ENV_SUS && v[9] && gate) {          /* (LOOP turned on while sustaining) */
        m->stage = ENV_ATK;
        m->from = m->lv;
        m->ph = 0;
    }
    m->out = mod_place(m->lv, v, 10u, 0);
    m->outr = mod_slew(m->outr, m->out, mod_lag(v[7]));
}

/* SEQ: the step under the beat (from the restart: TRIG KEY, or PLAY), its value glided into by SLEW */
static void mod_seq(mod_rt_t *m, const int16_t *v, const int8_t *steps, int restart)
{
    uint32_t div = SEQ_DIV[clamp(v[1], 0, 5)], len = (uint32_t)clamp(v[0], 1, 16), rel, n, in, c, fr, i;
    uint32_t sw = (uint32_t)clamp(v[3], 0, 100) * 65536u / 300u;     /* SWNG: the off-beats up to 1/3 step late */
    if (restart || m->step < 0) {
        m->t0 = mclk.beat;
        m->last_n = 0xFFFFFFFFu;
    }
    rel = mclk.beat - m->t0;
    n = rel / div;                                                    /* straight steps since the restart */
    in = (n & 1u) << 16 | (rel % div << 8) / (div >> 8);              /* where in its pair of steps, Q16 */
    c = in < 65536u + sw ? n & ~1u : n | 1u;                          /* the step playing, swung */
    fr = c & 1u ? ((in - 65536u - sw) << 8) / ((65536u - sw) >> 8) : (in << 8) / ((65536u + sw) >> 8);   /* (Q16) */
    if (c != m->last_n) {
        uint32_t L = len, p = c % (L > 1u ? 2u * L - 2u : 1u);
        switch (v[4]) {
        case 1: i = L - 1u - c % L; break;                            /* REV */
        case 2: i = p < L ? p : 2u * L - 2u - p; break;               /* PING */
        case 3: i = mod_hash(c, 17u) % L; break;                      /* RND */
        default: i = c % L; break;
        }
        i = (i + (uint32_t)clamp(v[7], 1, 16) - 1u) % 16u;            /* from STRT */
        m->last_n = c;
        m->from = m->out;
        if (mod_hash(c, 91u) % 100u < (uint32_t)v[6] || m->step < 0) {   /* PROB: this step plays (else it holds) */
            m->step = (int16_t)i;
            m->sval = (int16_t)(clamp(steps[i], 0, 100) * 32767 / 100);
        }
    }
    if (v[2] && fr < (uint32_t)v[2] * 655u)                           /* SLEW: a straight glide over SLEW % */
        m->out = m->from + (m->sval - m->from) * (int32_t)(fr >> 4) / (int32_t)((uint32_t)v[2] * 655u >> 4);
    else
        m->out = m->sval;
    m->outr = m->out;
}

/* FOLLOW: the envelope of a block of sound in (0: silence), GAIN into it, RISE and FALL, HOLD to the tempo */
static void mod_follow(mod_rt_t *m, const int16_t *v, const int32_t *in)
{
    static const uint16_t DB_Q12[25] = {1029, 1155, 1296, 1454, 1631, 1830, 2053, 2304, 2585, 2900, 3254, 3651, 4096,
                                        4596, 5157, 5786, 6492, 7284, 8173, 9170, 10289, 11544, 12953, 14533, 16306};
    int32_t pk = 0, i, x;
    uint32_t hd = HOLD_DIV[clamp(v[4], 0, 4)];
    if (in)
        for (i = 0; i < CTL; i++) {
            x = in[i] < 0 ? -in[i] : in[i];
            if (x > pk)
                pk = x;
        }
    pk = clamp(pk * DB_Q12[clamp(v[1], -12, 12) + 12] >> 12, 0, 32767);
    m->env = mod_slew(m->env, pk, mod_coef(pk > m->env ? v[2] : v[3]));
    if (!hd) {
        m->held = m->env;
    } else if (mclk.beat / hd != m->last_n) {
        m->last_n = mclk.beat / hd;
        m->held = m->env;
    }
    m->out = mod_place(m->held, v, 5u, 0);
    m->outr = mod_slew(m->outr, m->out, mod_lag(v[7]));
}

/* power-on (chain_init): no depths, every slot at rest, the clock at 0 */
static void mod_init(void)
{
    uint32_t t, s, g;
    for (t = 0; t < NTRK; t++) {
        for (s = 0; s < NSLOT; s++) {
            mod_rt_t z = {0};
            mrt[t][s] = z;
            for (g = 0; g < MOD_NTGT; g++)
                mdep[t][s][g] = 0;
        }
        mod_l[t][0].n = mod_l[t][1].n = 0;
        mod_l[t][0].used = mod_l[t][1].used = 0;
        mod_l[t][0].arrs = mod_l[t][1].arrs = 0;
        mod_cur[t] = 0;
        mod_n[t] = 0;
        mod_on = 0;
        mod_keys_prev[t] = 0;
        mod_arrs[t] = 0;
        for (s = 0; s < MOD_NARR; s++)
            mdv[t][s] = mod_base(t, s);
    }
    mod_undo_buf.valid = 0;
    mod_seam = 0;
    mod_phrase_n = 0;
    mclk.beat = mclk.acc = 0;
    mclk.was_playing = mclk.started = 0;
    mclk.bpm = 0;
}

/* ------------------------------------------------------------ the ISR --- */
/* audio ISR, once per control block before the tracks: the clock, each modulated track's slots, and its knob arrays
 * with the depths added (TPD). keys: the white keys held on track sel; last: each track's last block (FOLLOW's
 * input) */
static void mod_tick(uint32_t keys, uint32_t sel, const int32_t *const *last)
{
    uint32_t t, s, e, seam = mod_seam, playing = sys.playing, ph;
    mod_seam = 0;
    mclk.started = (uint8_t)(playing && !mclk.was_playing);
    mclk.was_playing = (uint8_t)playing;
    if (mclk.started) {
        mclk.beat = 0;
        mclk.acc = 0;
    } else {
        if (sys.bpm != mclk.bpm) {                       /* (the division only when the tempo moves) */
            uint32_t num = (uint32_t)clamp(sys.bpm, 40, 240) * ((uint32_t)CTL << 16);
            mclk.bpm = sys.bpm;
            mclk.inc = num / MOD_BEAT_DEN;
            mclk.rem = num % MOD_BEAT_DEN;
        }
        mclk.beat += mclk.inc;
        mclk.acc += mclk.rem;
        if (mclk.acc >= MOD_BEAT_DEN) {
            mclk.acc -= MOD_BEAT_DEN;
            mclk.beat++;
        }
    }
    ph = mclk.beat / MOD_PHRASE;                     /* a SYNTH or POLY track's loop: every four bars */
    if (ph != mod_phrase_n || mclk.started) {
        mod_phrase_n = ph;
        for (t = 0; t < NTRK; t++)
            if (tp[t].src != SRC_TAPE)
                seam |= 1u << t;
    }
    for (t = 0; t < NTRK; t++) {
        const mod_list_t *L;
        uint32_t k = t == sel ? keys : 0u, press = k & ~mod_keys_prev[t], i, a, arrs;
        int loop;
        mod_keys_prev[t] = k;
        mod_fmask[t] = 0;
        if (!((mod_on >> t) & 1u) && !mod_arrs[t])    /* nothing modulates the track, nor did last block */
            continue;
        L = &mod_l[t][mod_cur[t] & 1u];
        arrs = L->arrs;
        loop = (int)((seam >> t) & 1u) && playing;
        if (arrs != mod_arrs[t]) {                     /* the arrays read: the copy where there are depths */
            for (a = 0; a < MOD_NARR; a++)
                mdv[t][a] = (arrs >> a) & 1u ? mda[t][a] : mod_base(t, a);
            mod_arrs[t] = (uint16_t)arrs;
        }
        if (!L->n)
            continue;
        for (s = 0; s < NSLOT; s++) {
            mod_rt_t *m = &mrt[t][s];
            const int16_t *v = tp[t].mod[s];
            int trig, fresh = 0;
            if (!((L->used >> s) & 1u))
                continue;
            e = tp[t].engine[s] % NME;
            trig = v[e == ME_SEQ ? 5 : 13] != 0;        /* TRIG KEY (LFO knob 13, SEQ knob 5) */
            if (m->engine != e + 1u) {                   /* a new engine in the slot: its state from rest */
                uint32_t sd = m->seed;
                mod_rt_t z = {0};
                *m = z;
                m->seed = sd;
                m->engine = (uint8_t)(e + 1u);
                m->step = -1;
                if (e == ME_WAVE)
                    lfo_seed(m, t, s);
                fresh = 1;
            }
            switch (e) {
            case ME_WAVE: mod_lfo(m, v, fresh || mclk.started || (trig && press)); break;
            case ME_ADSR: mod_adsr(m, v, k != 0u, press != 0u, loop); break;
            case ME_SEQ: mod_seq(m, v, tp[t].steps[s], fresh || mclk.started || (trig && press)); break;
            default: {
                int32_t src = clamp(v[0], 0, 5);
                mod_follow(m, v, src == 0 ? last[t] : src <= (int32_t)NTRK ? last[src - 1] : 0);   /* (USB: none yet) */
                break;
            }
            }
        }
        for (a = 0; arrs >> a; a++)                     /* the arrays with depths: the knobs as set */
            if ((arrs >> a) & 1u) {
                const int16_t *b = mod_base(t, a);
                int16_t *d = mda[t][a];
                for (i = 0; i < (a == MA_CH ? NCH : NPK); i++)
                    d[i] = b[i];
            }
        mod_pan_r[t] = mda[t][MA_CH][CH_PAN];
        for (i = 0; i < L->n;) {                        /* each target: its depths summed, then clamped once */
            const mod_ent_t *E = &L->e[i];
            int16_t *p = &mda[t][E->arr][E->k];
            int32_t sum = 0, sumr = 0, g = E->tgt, s8 = 0;
            uint32_t fi = E->slot >> 4u;
            if (fi < FN_N) {                            /* a fine knob: 256 times finer too (the same product, 8 bits
                                                         * less shifted), except a SEQ's steps on a pitch, which land on
                                                         * semitones (a step is a note) */
                for (; i < L->n && L->e[i].tgt == g; i++) {
                    const mod_rt_t *m = &mrt[t][L->e[i].slot & 15u];
                    int32_t x = (m->out >> 3) * L->e[i].fq, d = (x + (1 << 18)) >> 19;
                    sum += d;
                    s8 += m->engine == ME_SEQ + 1u && fi < FN_PITCHES ? d * 256 : (x + (1 << 10)) >> 11;
                }
                mod_f8[t][fi] = clamp(*p * 256 + s8, E->lo * 256, E->hi * 256);
                mod_fmask[t] |= (uint8_t)(1u << fi);
            } else {
                for (; i < L->n && L->e[i].tgt == g; i++) {
                    const mod_rt_t *m = &mrt[t][L->e[i].slot & 15u];
                    sum += ((m->out >> 3) * L->e[i].fq + (1 << 18)) >> 19;
                    sumr += ((m->outr >> 3) * L->e[i].fq + (1 << 18)) >> 19;
                }
            }
            if (g == (int32_t)(MOD_TCH + CH_PAN))
                mod_pan_r[t] = (int16_t)clamp(*p + sumr, E->lo, E->hi);
            *p = (int16_t)clamp(*p + sum, E->lo, E->hi);
        }
    }
}

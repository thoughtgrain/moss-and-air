/* SPDX-License-Identifier: GPL-3.0-only */
/* The DRUM kit's voice: one small synthesizer that every instrument shares, set by a patch (drum.c's DRM_KIT).
 *
 * I build every sound from three layers, all optional:
 *   TONE   one or two sine partials, struck: the pitch starts BEND above and falls
 *          back with its own time constant, the level decays from the strike (the body)
 *   NOISE  white noise, the metal (a few square waves at fixed inharmonic ratios), or a mix, through one
 *          filter (low, band or high), under an envelope of three parts: a rise (a fade-in subtracted from the
 *          rest), a HIT (a fast decay, re-struck BURSTS times GAP apart: a clap's hands, a guiro's teeth, a
 *          tambourine's jingles) and a TAIL (a slow decay, from the strike or from the last burst)
 *   DRIVE  a soft clip over the sum, its level kept
 * The two decays use different envelope forms, picked for their range: the fast ones (the hit, the bend, the
 * rise: under ~40 ms) are per-sample products in Q15, e = e k >> 15, which always reach zero; the slow ones (the
 * body, the tail: up to seconds) are Q30 per control block, ramped linearly inside the block (a per-sample Q15
 * factor can't hold a decay that slow).
 *
 * Every strike starts from rest (phases, filter, the noise seed), so a hit renders the same every time: what you
 * program is what plays. A voice that has rung out (-84 dB) stops computing (live 0) and writes zeros.
 *
 * Use: dk_setup when the patch or a knob changes, dk_trigger on a hit (struck at the start of the next block),
 * dk_choke to cut a voice short (1.5 ms), dk_run per block into y: Q15 at its level, linear to -6 dBFS, a soft
 * knee above. */

#define DK_QUIET (1 << 16)                               /* Q30: -84 dB */
#define DK_NMETAL 4

enum { DK_OFF, DK_LOW, DK_BAND, DK_HIGH };              /* the noise layer's filter */
enum { DK_M_NONE, DK_M_CLUSTER, DK_M_PAIR };            /* the metal: four squares, or a bell's two */

/* a patch: the instrument as designed, in musical units */
typedef struct {
    char code[3];                                        /* on screen */
    /* TONE */
    int16_t p16;                                         /* pitch, 1/16 semitone (MIDI x 16); 0: no tone layer */
    uint16_t ratio;                                      /* the second partial over the first, Q12; 0: none */
    uint8_t mix2;                                        /* its level, 0..127 of the first */
    uint8_t bend;                                        /* the strike's pitch above p16, 1/16 semitone */
    uint8_t bend_ms;                                     /* its fall (tau) */
    uint16_t body_ms;                                    /* the body's decay (tau) */
    uint8_t tone_lv;                                     /* 0..127 */
    /* NOISE */
    uint8_t filt;                                        /* DK_LOW / DK_BAND / DK_HIGH; DK_OFF: no noise layer */
    int16_t cut;                                         /* the filter, 1/16 semitone (as a pitch) */
    uint8_t q16;                                         /* its Q x 16 */
    uint8_t mset;                                        /* DK_M_*: the metal */
    int16_t mp16;                                        /* the metal's pitch */
    uint8_t metal;                                       /* the metal's share against the noise, 0..127 */
    uint8_t rise10;                                      /* the rise, 0.1 ms */
    uint8_t hit_ms;                                      /* the hit's decay (tau) */
    uint8_t hit_lv;                                      /* 0..127 */
    uint8_t bursts;                                      /* hits after the strike */
    uint8_t gap10;                                       /* between them, 0.1 ms */
    uint8_t late;                                        /* 1: the tail starts at the last burst */
    uint16_t tail_ms;                                    /* the tail's decay (tau) */
    uint8_t tail_lv;                                     /* 0..127 */
    /* DRIVE, LEVEL */
    uint8_t drive;                                       /* 0..127: x1 .. x5 into the soft clip */
    uint16_t gain;                                       /* Q12: evens the instruments out */
    uint16_t chokes;                                     /* the instruments a hit of this one cuts short */
} dk_patch_t;

/* the knobs over a patch: the kit's and the instrument's own, summed by drum.c */
typedef struct {
    int16_t tune;                                        /* 1/16 semitone, every pitch and filter */
    uint16_t dscale;                                     /* every decay x this, Q8 (256: as designed) */
    int16_t bright;                                      /* -127..127: the filter +-1 octave, the 2nd partial */
    uint16_t level;                                      /* 100: as designed (up to 400: +12 dB) */
    uint8_t accent;                                      /* 0..127: up to +4 dB, more on the hit */
    uint8_t drive;                                       /* 0..127, added to the patch's */
} dk_knobs_t;

typedef struct {                                         /* a patch under its knobs, ready to run */
    uint32_t inc, span;                                  /* the first partial; the bend's extra increment */
    uint32_t ratio;                                      /* Q12 */
    uint32_t kbend, kb_body;                             /* Q15 per sample; Q16 per block */
    int32_t g1, g2;                                      /* the partials, Q15 */
    uint32_t minc[DK_NMETAL];
    int32_t mamp;                                        /* one metal square's amplitude */
    int32_t gnoise, gmetal;                              /* Q15 */
    tsvf_t f;
    uint32_t krise, khit, kb_tail;
    int32_t ghit, gtail;                                 /* Q15 */
    uint16_t gap, bursts;                                /* samples; count */
    uint8_t filt, late, nm, pad;                         /* nm: metal squares in use */
    int32_t drive, dcomp;                                /* Q12; Q15 */
    int32_t out;                                         /* Q12 */
} dk_coef_t;

typedef struct {
    uint8_t live, trig, choke, tail_on;
    uint16_t gap_n, left;                                /* to the next burst; bursts left */
    uint32_t ph1, ph2, mph[DK_NMETAL];
    int32_t ebend, ehit, erise;                          /* Q15 */
    int32_t qbody, qtail;                                /* Q30 */
    int32_t s1, s2;                                      /* the filter */
    int32_t rng;
} dk_voice_t;

/* the metal: square-wave ratios (Q12). The cluster's are spread so no two land near a simple interval (a clang,
 * not a chord); the pair is a bell's two tones, a wide, slightly flat fifth apart */
static const uint16_t DK_METAL_R[3][DK_NMETAL] = {
    {0, 0, 0, 0},
    {4096, 5612, 7332, 10363},                           /* 1, 1.37, 1.79, 2.53 */
    {4096, 6103, 0, 0},                                  /* 1, 1.49 */
};

/* -------------------------------------------------------------- setup (outside the sample loop) --- */
static uint32_t dk_inc(int32_t p16) { return pitch_inc((uint32_t)clamp(p16, 0, 2047)); }

/* a pitch (p16) as the filter's cutoff index (0..127 << 8 over 30 Hz .. 16 kHz, exponential): the index runs
 * 127 / log2(16000 / 30) = 14.02 per octave, the pitch 192 per octave, and 30 Hz is p16 360 */
static int32_t dk_cut(int32_t p16) { return clamp((p16 - 360) * 299 / 16, 0, 127 << 8); }

/* tau (us) -> a per-sample decay, Q15: e^(-1 / (tau fs)) = 1 - x + x^2 / 2 with x = 1 / (tau fs) */
static int32_t dk_kfast(uint32_t us)
{
    uint32_t x = 743039u / (us | 1u);                    /* x Q15: 32768e6 / 44100 / tau_us */
    return x >= 32768u ? 0 : (int32_t)(32768u - x + ((x * x) >> 16));
}
/* tau (us) -> a per-block decay, Q16: e^(-CTL / (tau fs)), to the third order (x <= 0.6 above 1.2 ms) */
static uint32_t dk_kslow(uint32_t us)
{
    uint32_t x = 47554467u / (us | 1u), x2, x3, k;       /* x Q16: 65536e6 x 32 / 44100 / tau_us */
    if (x > 40000u)
        x = 40000u;
    x2 = (x * x) >> 16;
    x3 = (x2 * x) >> 16;
    k = 65536u - x + (x2 >> 1) - x3 / 6u;
    return k > 65535u ? 65535u : k;
}
/* ms x the decay scale (Q8) -> us, held between 0.2 ms and 30 s */
static uint32_t dk_us(uint32_t ms, uint32_t sc)
{
    uint32_t us = ms * 1000u;
    return (uint32_t)clamp((int32_t)(us / 256u * sc + (us % 256u) * sc / 256u), 200, 30000000);
}

static void dk_setup(dk_coef_t *c, const dk_patch_t *p, const dk_knobs_t *k)
{
    uint32_t sc = k->dscale ? k->dscale : 256u, i;
    int32_t br = clamp(k->bright, -127, 127), acc = k->accent;
    for (i = 0; i < sizeof *c / 4u; i++)
        ((uint32_t *)c)[i] = 0;
    /* TONE */
    if (p->p16) {
        int32_t m2 = p->ratio ? clamp(p->mix2 + br / 4, 0, 127) : 0;
        c->inc = dk_inc(p->p16 + k->tune);
        c->span = p->bend ? dk_inc(p->p16 + k->tune + p->bend) - c->inc : 0u;
        c->ratio = p->ratio;
        c->kbend = (uint32_t)dk_kfast((uint32_t)p->bend_ms * 1000u);
        c->kb_body = dk_kslow(dk_us(p->body_ms, sc));
        c->g1 = 32767 * 127 / (127 + m2) * p->tone_lv / 127;   /* the partials sum to tone_lv at most */
        c->g2 = c->g1 * m2 / 127;
    }
    /* NOISE */
    c->filt = p->filt;
    if (p->filt != DK_OFF) {
        const uint16_t *r = DK_METAL_R[p->mset < 3u ? p->mset : 0u];
        uint32_t minc = dk_inc(p->mp16 + k->tune), hit = dk_us(p->hit_ms, 256u);
        for (i = 0; i < DK_NMETAL; i++) {
            c->minc[i] = (minc >> 12) * r[i];
            c->nm += r[i] != 0;
        }
        c->mamp = c->nm ? 32767 / c->nm : 0;
        c->gmetal = c->nm ? p->metal * 32767 / 127 : 0;
        c->gnoise = 32767 - c->gmetal;
        tsvf_coef_k(&c->f, dk_cut(p->cut + k->tune + br * 192 / 127), 4096 * 16 / (p->q16 | 1));
        c->krise = p->rise10 ? (uint32_t)dk_kfast((uint32_t)p->rise10 * 100u) : 0u;
        c->khit = (uint32_t)dk_kfast(hit - hit / 4u + dk_us(p->hit_ms, sc) / 4u);   /* a quarter of DECY's
                                                          * stretch: a hit stays a hit */
        c->ghit = p->hit_lv * 32767 / 127;
        c->ghit += (c->ghit * acc) >> 8;                 /* the accent: up to +6 dB more on the hit */
        c->kb_tail = dk_kslow(dk_us(p->tail_ms, sc));
        c->gtail = p->tail_lv * 32767 / 127;
        c->gap = (uint16_t)((uint32_t)p->gap10 * 441u / 100u);
        c->bursts = p->gap10 ? p->bursts : 0u;
        c->late = p->late;
    }
    /* DRIVE: x1 .. x5 into tanh, then back to the same small-signal level */
    i = (uint32_t)clamp(p->drive + k->drive, 0, 127);
    c->drive = 4096 + (int32_t)i * 4 * 4096 / 127;
    c->dcomp = i ? 32767 * 4096 / c->drive : 32767;
    /* LEVEL and the accent (1 + 0.6 a: +4 dB) */
    c->out = (int32_t)((uint32_t)p->gain * (k->level < 400u ? k->level : 400u) / 100u);
    c->out += (c->out * acc * 77) >> 14;
}

/* ----------------------------------------------------------------------- the voice --- */
static void dk_strike(const dk_coef_t *c, dk_voice_t *v)
{
    uint32_t i;
    for (i = 0; i < sizeof *v / 4u; i++)
        ((uint32_t *)v)[i] = 0;
    v->live = 1;
    v->rng = 0x2B1D5EED;
    v->ebend = c->span ? 32767 : 0;
    v->ehit = c->filt != DK_OFF ? 32767 : 0;
    v->erise = c->krise ? 32767 : 0;
    v->qbody = c->g1 ? 1 << 30 : 0;
    v->tail_on = c->filt == DK_OFF || !(c->late && c->bursts);
    v->qtail = v->tail_on && c->filt != DK_OFF ? 1 << 30 : 0;
    v->left = c->bursts;
    v->gap_n = c->gap;
}

static void dk_trigger(dk_voice_t *v) { v->trig = 1; }
static void dk_choke(dk_voice_t *v) { v->choke = 1; }

/* a slow envelope's block: its start (Q20) and step per sample, then the envelope a block on */
static inline void dk_ramp(int32_t *q, uint32_t k, int32_t *a, int32_t *d)
{
    *a = *q >> 10;
    *q = mulq16(*q, k);
    *d = ((*q >> 10) - *a) >> CTL_LOG2;
}

/* The sample loops, one per layer, each specialised for what's fixed through a block (the filter's mode, the
 * metal's squares), so the loop itself has no decisions in it. A layer that has gone quiet is skipped: a bass
 * drum's thud lasts 2 ms of its second, a hat has no tone at all. */

/* TONE into y (written, not added): the partials under the body's ramp (a: Q20 start, d: step) */
static void dk_tone_loop(const dk_coef_t *c, dk_voice_t *v, int32_t *y, uint32_t n, int32_t a, int32_t d)
{
    uint32_t i, ph1 = v->ph1, ph2 = v->ph2, span = c->span >> 15, ratio = c->ratio;
    int32_t eb = v->ebend, kb = (int32_t)c->kbend, g1 = c->g1, g2 = c->g2;
    for (i = 0; i < n; i++) {
        uint32_t inc = c->inc + span * (uint32_t)eb, inc2 = (inc >> 12) * ratio;
        int32_t o1, o2;
        ph1 += inc;
        ph2 += inc2;
        eb = (eb * kb) >> 15;
        o1 = sine_i(ph1);
        o2 = ratio ? sine_i(ph2) : 0;
        a += d;
        y[i] = ((((o1 * g1) >> 15) + ((o2 * g2) >> 15)) * (a >> 5)) >> 15;
    }
    v->ph1 = ph1, v->ph2 = ph2, v->ebend = eb;
}

/* NOISE added into y: the source (m: the metal's block, or 0) through the filter (mode: 0 low, 1 band, 2 high) under
 * the rise, the hit (re-struck by the bursts) and the tail's ramp (a, d) */
static inline __attribute__((always_inline)) void dk_noise_loop(const dk_coef_t *c, dk_voice_t *v, int32_t *y,
                                                                 uint32_t n, const int32_t *m, int32_t a, int32_t d,
                                                                 const uint32_t mode)
{
    uint32_t i, left = v->left, gap = v->gap_n;
    int32_t eh = v->ehit, er = v->erise, s1 = v->s1, s2 = v->s2, rng = v->rng, gn = c->gnoise, gm = c->gmetal;
    int32_t gh = c->ghit, gt = c->gtail, kh = v->choke ? 31700 : (int32_t)c->khit, kr = (int32_t)c->krise;
    for (i = 0; i < n; i++) {
        int32_t src = (((int32_t)noise32(&rng) >> 16) * gn) >> 15, f, e;
        if (m)
            src += (m[i] * gm) >> 15;
        f = clamp(tsvf_mode(&c->f, src, &s1, &s2, mode), -65535, 65535);
        if (left && !--gap) {                            /* a burst: the hit again */
            eh = 32767;
            gap = c->gap;
            left--;
        }
        a += d;
        e = ((eh * gh) >> 15) + (((a >> 5) * gt) >> 15);
        if (er) {                                        /* (the rise: only until it's done) */
            e = (e * (32767 - er)) >> 15;
            er = (er * kr) >> 15;
        }
        eh = (eh * kh) >> 15;
        y[i] += (f * (clamp(e, 0, 65534) >> 1)) >> 14;
    }
    v->left = (uint16_t)left, v->gap_n = (uint16_t)gap, v->ehit = eh, v->erise = er, v->s1 = s1, v->s2 = s2;
    v->rng = rng;
}

static __attribute__((noinline)) void dk_run(const dk_coef_t *c, dk_voice_t *v, int32_t *y, uint32_t n)
{
    static const uint32_t KB_CHOKE = 40265u;             /* dk_kslow(1500): a 1.5 ms release */
    uint32_t i, j;
    int32_t ab, db, at, dt, m[CTL];
    int tone, noise;
    if (v->trig)
        dk_strike(c, v);
    if (!v->live) {
        for (i = 0; i < n; i++)
            y[i] = 0;
        return;
    }
    if (!v->tail_on && !v->left) {                       /* the last burst struck: the tail starts */
        v->qtail = 1 << 30;
        v->tail_on = 1;
    }
    if (v->choke)
        v->left = 0;
    tone = c->g1 && v->qbody > DK_QUIET;
    noise = c->filt != DK_OFF && (v->qtail > DK_QUIET || v->ehit > 4 || v->left || !v->tail_on);
    dk_ramp(&v->qbody, v->choke ? KB_CHOKE : c->kb_body, &ab, &db);
    dk_ramp(&v->qtail, v->choke ? KB_CHOKE : c->kb_tail, &at, &dt);
    if (tone)
        dk_tone_loop(c, v, y, n, ab, db);
    else
        for (i = 0; i < n; i++)
            y[i] = 0;
    if (noise) {
        if (c->nm) {                                     /* the metal: its squares summed for the block */
            for (i = 0; i < n; i++)
                m[i] = 0;
            for (j = 0; j < c->nm; j++) {
                uint32_t ph = v->mph[j], inc = c->minc[j];
                int32_t amp = c->mamp;
                for (i = 0; i < n; i++) {
                    ph += inc;
                    m[i] += (ph >> 31) ? amp : -amp;
                }
                v->mph[j] = ph;
            }
        }
        switch (c->filt) {
        case DK_LOW: dk_noise_loop(c, v, y, n, c->nm ? m : 0, at, dt, 0u); break;
        case DK_BAND: dk_noise_loop(c, v, y, n, c->nm ? m : 0, at, dt, 1u); break;
        default: dk_noise_loop(c, v, y, n, c->nm ? m : 0, at, dt, 2u); break;
        }
    }
    if (c->drive != 4096)                                /* DRIVE */
        for (i = 0; i < n; i++)
            y[i] = (softclip((clamp(y[i], -65535, 65535) * c->drive) >> 12) * c->dcomp) >> 15;
    for (i = 0; i < n; i++)
        y[i] = soft_knee((clamp(y[i], -65535, 65535) * (c->out >> 1)) >> 11, 16384);   /* (out up to ~42000) */
    v->live = v->qbody > DK_QUIET || v->qtail > DK_QUIET || v->ehit > 4 || v->left || !v->tail_on;
}

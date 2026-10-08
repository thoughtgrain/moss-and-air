/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: SPACE, the last device on a track (docs/bryo-architecture.md, "SPACE, as built"): the S-4's VAST, a delay
 * and a small room, run at half the rate (22.05 kHz) to halve both their memory and their load.
 *
 * The delay: TIME (10..370 ms) back, gliding when TIME turns (a tape echo's pitch bend, no clicks); FDBK what each
 * echo feeds back (100: nearly forever); TONE in the feedback (- a low-pass, + a high-pass, so each echo is darker or
 * thinner than the last); DLY its level (100: the first echo at 0.8 of the sound). The reverb: Felucca's ROOM (four
 * damped combs and two allpasses, Freeverb's shape) with SIZE setting the combs' lengths (15..46 ms), DEC the time
 * the tail takes to fall 60 dB (0.2..4.2 s), TONE the combs' damping (- darker) or a high-pass in front (+), PRE the
 * pre-delay (0..200 ms); VERB its level. SPRD is the stereo: the delay's right echo up to 10 ms ahead of the left, the
 * reverb's right side from a second pair of allpasses fed a different mix of the combs; at 0 both are mono. The
 * picture (ui_viz.c viz_space) draws the same mappings.
 *
 * Memory, from the shared chunks (mem.c), only while it's used: the delay's line while DLY is above 0 (8 chunks,
 * 8,192 samples), the reverb (a chunk per comb, the allpasses in a fifth) and its pre-delay line (5 chunks) while
 * VERB is above 0, on a track in use. A line is 16-bit when there's room for it with 3 s still free; when memory is
 * short it starts 8-bit (companded like A-law: half the chunks, the noise riding under the sound), and when nothing
 * is free it takes from the end of the longest tape, like GRAIN and RESONATOR. A line never changes format while it
 * sounds: it's chosen when it's switched on. The reverb's own buffers are always 16-bit (a feedback loop would grow
 * 8-bit's noise).
 *
 * Audio ISR, except space_poll (the main loop: the memory). Integer only. */

#define SP_CH 1024u                  /* 16-bit samples a line keeps in a chunk (of its 1,056: a power of two) */
#define SP_DLY16 8u                  /* the delay's line, chunks: 8,192 samples, TIME's 370 ms at 22.05 kHz */
#define SP_DLY8 4u                   /* .. 8-bit */
#define SP_PRE16 5u                  /* the pre-delay's line: 5,120 samples, PRE's 200 ms */
#define SP_PRE8 3u                   /* .. 8-bit (6,144) */
#define SP_REV 5u                    /* the reverb: a comb in each of four chunks, the four allpasses in the fifth */
#define SP_KEEP 16u                  /* chunks (3 s) a 16-bit line leaves free, or it starts 8-bit */
#define SP_LMAX 8u
enum { SP_TIME, SP_FDBK, SP_SIZE, SP_DEC, SP_DLY, SP_VERB, SP_TONE, SP_SPRD, SP_PRE };

/* the combs' longest (SIZE 100), and the allpasses (left pair, right pair), samples at 22.05 kHz: Freeverb's
 * lengths, the combs half again as long for a room rather than a box */
static const uint16_t SP_COMB[4] = {837, 891, 958, 1017};
static const uint16_t SP_AP[4] = {278, 220, 290, 232};
static const uint16_t SP_APO[4] = {0, 278, 498, 788};   /* where each allpass sits in the fifth chunk */

typedef struct {                     /* a line in chunks of the shared memory */
    uint8_t map[SP_LMAX];            /* (the main loop's) */
    uint8_t nch;
    uint8_t bits8;                   /* 8-bit companded (set before ready, never while it's ready) */
    volatile uint8_t ready;          /* every chunk is in: the ISR may use it */
    volatile uint8_t gen;            /* counts each time it's handed over (the ISR starts it from silence) */
} sp_line_t;

typedef struct {
    sp_line_t dly, pre, rev;         /* the main loop's (ready: published to the ISR) */
    /* the ISR's */
    uint8_t dlive, rlive;            /* the delay and the reverb running (their state reset when they start) */
    uint8_t dgen, rgen;              /* the lines' gen they started on (a line given back and taken again between
                                      * two blocks starts over too) */
    uint32_t dw, pw;                 /* the lines' write indices */
    int32_t dq;                      /* the delay's distance, Q8 samples (glides toward TIME) */
    int32_t dlp;                     /* TONE's filter in the feedback */
    int32_t rhp;                     /* TONE's high-pass in front of the reverb */
    uint16_t ci[4], ai[4];           /* the combs' and allpasses' indices */
    uint16_t clen[4];                /* the combs' lengths now (SIZE) */
    int32_t clp[4], cg[4];           /* the combs' damping filters, their loop gains (DEC), Q15 */
    int16_t size_seen, dec_seen;     /* what clen and cg were set for (-1: nothing yet) */
    int32_t gd, gv;                  /* DLY's and VERB's gains last block, Q15 (ramped across the next) */
    int32_t pl, pr;                  /* the wet's last half-rate sample, each side (the step back up to 44.1 kHz) */
} space_t;

static space_t space[NTRK];

/* ------------------------------------------------- 8-bit companding --- */
/* sign, a 3-bit segment, a 4-bit step inside it: 16 steps of 16 below 256, then each segment twice the last up to
 * 32,767 (A-law's shape). The segment of |x| >> 8: */
static const uint8_t SP_SEG[128] = {
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};

static inline uint8_t sp_enc8(int32_t x)
{
    uint32_t a = (uint32_t)(x < 0 ? -x : x), s, m;
    if (a > 32767u)
        a = 32767u;
    s = SP_SEG[a >> 8];
    m = s ? (a >> (s + 3u)) & 15u : a >> 4;
    return (uint8_t)((x < 0 ? 128u : 0u) | (s << 4) | m);
}

static inline int32_t sp_dec8(uint8_t c)
{
    uint32_t s = (c >> 4) & 7u, m = c & 15u;
    int32_t a = s ? (int32_t)(((16u + m) << (s + 3u)) + (1u << (s + 2u))) : (int32_t)(m << 4);
    return c & 128u ? -a : a;
}

/* a * b, Q15, toward zero: the loops here run for seconds, and a shift's floor would keep them ringing on a small
 * DC of their own after the sound has gone (-36 at the output, measured; rounding to nearest still left a limit
 * cycle of 28). Cutting the magnitude only ever takes energy out, so a loop with nothing in it falls to 0. */
static inline int32_t sp_mul(int32_t a, int32_t b) { return a * b / 32768; }

/* ----------------------------------------------------------- lines --- */
/* a line's chunks as pointers, for a block */
static inline void sp_bases(const sp_line_t *ln, uint8_t **b)
{
    uint32_t k;
    for (k = 0; k < ln->nch; k++)
        b[k] = (uint8_t *)(void *)mem_at(ln->map[k]);
}

static inline uint32_t sp_len(const sp_line_t *ln) { return (uint32_t)ln->nch * SP_CH * (ln->bits8 ? 2u : 1u); }

static inline int32_t sp_rd(uint8_t *const *b, int bits8, uint32_t i)
{
    if (bits8)
        return sp_dec8(b[i >> 11][i & 2047u]);
    return ((const int16_t *)(void *)b[i >> 10])[i & 1023u];
}

static inline void sp_wr(uint8_t *const *b, int bits8, uint32_t i, int32_t x)
{
    if (bits8)
        b[i >> 11][i & 2047u] = sp_enc8(x);
    else
        ((int16_t *)(void *)b[i >> 10])[i & 1023u] = (int16_t)clamp(x, -32767, 32767);
}

/* main loop: line ln for owner o holds n16 chunks at 16 bits, or n8 at 8 bits when memory is short (n8 0: 16-bit
 * only); returns 1 once it's all in and handed to the ISR. When nothing more can be had, a 16-bit line holding
 * enough for 8 bits becomes one (its chunks are silent either way: a zero sample is a zero code). */
static int sp_line_fill(sp_line_t *ln, uint32_t o, uint32_t n16, uint32_t n8)
{
    uint32_t need;
    if (ln->ready)
        return 1;
    if (!ln->nch)
        ln->bits8 = n8 && mem_count(MEM_FREE) < n16 + SP_KEEP;
    need = ln->bits8 ? n8 : n16;
    while (ln->nch < need) {
        int32_t c = tape_alloc(o, NTRK, 1);
        if (c < 0)
            break;
        ln->map[ln->nch++] = (uint8_t)c;
    }
    if (ln->nch < need && !ln->bits8 && n8 && ln->nch >= n8) {
        while (ln->nch > n8)
            mem_free(ln->map[--ln->nch]);
        ln->bits8 = 1;
        need = n8;
    }
    if (ln->nch < need)
        return 0;
    ln->gen++;
    RING_PUBLISH();                                     /* (the map before the flag that hands it to the ISR) */
    ln->ready = 1;
    return 1;
}

/* main loop: the ISR stops using line ln (from its next block), then its chunks go back */
static void sp_line_free(sp_line_t *ln)
{
    ln->ready = 0;
    RING_PUBLISH();
    while (ln->nch)
        mem_free(ln->map[--ln->nch]);
}

/* main loop, every pass: each track's lines while its SPACE uses them (DLY or VERB above 0, the track in use) */
static void space_poll(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        space_t *S = &space[t];
        const int16_t *v = tp[t].dev[DEV_SPACE];
        int use = t < gr_ntrk();
        if (use && v[SP_DLY] > 0)
            sp_line_fill(&S->dly, MEM_SPACE + t, SP_DLY16, SP_DLY8);
        else if (S->dly.nch)
            sp_line_free(&S->dly);
        if (use && v[SP_VERB] > 0) {
            if (sp_line_fill(&S->rev, MEM_SPACE + t, SP_REV, 0))
                sp_line_fill(&S->pre, MEM_SPACE + t, SP_PRE16, SP_PRE8);
        } else {
            if (S->pre.nch)
                sp_line_free(&S->pre);
            if (S->rev.nch)
                sp_line_free(&S->rev);
        }
    }
}

/* ------------------------------------------------------------ audio --- */
/* the reverb's combs for SIZE and DEC */
static void sp_room(space_t *S, int32_t size, int32_t dec)
{
    uint32_t k, t60 = (uint32_t)(200 + dec * 40);
    for (k = 0; k < 4u; k++) {
        uint32_t len = SP_COMB[k] * (uint32_t)(40 + size * 60 / 100) / 100u;
        S->clen[k] = (uint16_t)len;
        if (S->ci[k] >= len)
            S->ci[k] = 0;
        S->cg[k] = rs_gain(2u * len, t60);              /* (rs_gain counts samples at 44.1 kHz) */
    }
    S->size_seen = (int16_t)size;
    S->dec_seen = (int16_t)dec;
}

/* saturate to the lines' 16 bits (one compare when it's inside) */
static inline int32_t sp_sat(int32_t v)
{
    return (uint32_t)(v + 32767) <= 65534u ? v : v < 0 ? -32767 : 32767;
}

/* the delay over a block at half the rate: m in, its echoes added to wl, wr (the state in locals throughout: the
 * compiler can't know the buffers don't alias it) */
static void sp_delay(space_t *S, uint8_t *const *db, int dlb, uint32_t dl, int32_t tgt, int32_t off, int32_t kl,
                     int32_t kh, int32_t fbg, int32_t v0, int32_t v1, const int32_t *m, int32_t *wl, int32_t *wr)
{
    int32_t q = S->dq, lp = S->dlp, w = (int32_t)S->dw, gq = v0 << (CTL_LOG2 - 1), dg = v1 - v0;
    uint32_t i;
    for (i = 0; i < CTL / 2u; i++, gq += dg) {
        int32_t e, er, f, s0, s1, ix, g = gq >> (CTL_LOG2 - 1);
        q += clamp((tgt - q) >> 11, -96, 96);           /* TIME glides: ~90 ms, at most 3/8 of a sample a sample */
        ix = w - (q >> 8);                              /* (a tape echo's bend: within a fifth) */
        if (ix < 0)
            ix += (int32_t)dl;
        s0 = sp_rd(db, dlb, (uint32_t)ix);
        s1 = sp_rd(db, dlb, ix ? (uint32_t)ix - 1u : dl - 1u);
        e = s0 + (((s1 - s0) * (q & 255)) >> 8);
        er = e;
        if (off) {                                      /* SPRD: the right echo a little ahead */
            int32_t qr = q - off < (2 << 8) ? 2 << 8 : q - off;
            ix = w - (qr >> 8);
            if (ix < 0)
                ix += (int32_t)dl;
            s0 = sp_rd(db, dlb, (uint32_t)ix);
            s1 = sp_rd(db, dlb, ix ? (uint32_t)ix - 1u : dl - 1u);
            er = s0 + (((s1 - s0) * (qr & 255)) >> 8);
        }
        if (kh) {                                       /* TONE in the feedback */
            lp += sp_mul(e - lp, kh);
            f = e - lp;
        } else if (kl < 32767) {
            lp += sp_mul(e - lp, kl);
            f = lp;
        } else
            f = e;
        sp_wr(db, dlb, (uint32_t)w, m[i] + sp_mul(f, fbg));
        if (++w >= (int32_t)dl)
            w = 0;
        wl[i] += (e * g) >> 15;
        wr[i] += (er * g) >> 15;
    }
    S->dq = q;
    S->dlp = lp;
    S->dw = (uint32_t)w;
}

/* one comb: read, damp, feed back, step (o: what it read) */
#define SP_COMB_STEP(k)                                                                                   \
    do {                                                                                                  \
        o = c##k[i##k];                                                                                   \
        lp##k = o + (((lp##k - o) * damp) >> 15);                                                         \
        c##k[i##k] = (int16_t)sp_sat(in + sp_mul(lp##k, cg##k));                                          \
        if (++i##k >= n##k)                                                                               \
            i##k = 0;                                                                                     \
    } while (0)
/* one allpass on a */
#define SP_AP_STEP(k, a)                                                                                  \
    do {                                                                                                  \
        int32_t o_ = ap##k[j##k];                                                                         \
        ap##k[j##k] = (int16_t)sp_sat((a) + o_ / 2);                                                      \
        (a) = o_ - (a);                                                                                   \
        if (++j##k >= SP_AP[k])                                                                           \
            j##k = 0;                                                                                     \
    } while (0)

/* the room over a block at half the rate: m in, through the pre-delay, its tail added to wl, wr */
static void sp_room_run(space_t *S, uint8_t *const *pb, int plb, uint32_t pl, int32_t pre, uint8_t *const *rb,
                        int32_t kh, int32_t damp, int32_t sprd, int32_t v0, int32_t v1, const int32_t *m, int32_t *wl,
                        int32_t *wr)
{
    int16_t *c0 = (int16_t *)(void *)rb[0], *c1 = (int16_t *)(void *)rb[1], *c2 = (int16_t *)(void *)rb[2];
    int16_t *c3 = (int16_t *)(void *)rb[3], *ap0 = (int16_t *)(void *)rb[4] + SP_APO[0];
    int16_t *ap1 = (int16_t *)(void *)rb[4] + SP_APO[1], *ap2 = (int16_t *)(void *)rb[4] + SP_APO[2];
    int16_t *ap3 = (int16_t *)(void *)rb[4] + SP_APO[3];
    uint32_t i0 = S->ci[0], i1 = S->ci[1], i2 = S->ci[2], i3 = S->ci[3], n0 = S->clen[0], n1 = S->clen[1];
    uint32_t n2 = S->clen[2], n3 = S->clen[3], j0 = S->ai[0], j1 = S->ai[1], j2 = S->ai[2], j3 = S->ai[3];
    int32_t lp0 = S->clp[0], lp1 = S->clp[1], lp2 = S->clp[2], lp3 = S->clp[3], cg0 = S->cg[0], cg1 = S->cg[1];
    int32_t cg2 = S->cg[2], cg3 = S->cg[3], hp = S->rhp, w = (int32_t)S->pw, gq = v0 << (CTL_LOG2 - 1), dg = v1 - v0;
    uint32_t i;
    for (i = 0; i < CTL / 2u; i++, gq += dg) {
        int32_t x, in, o, al, ar, ix = w - pre, g = gq >> (CTL_LOG2 - 1);
        if (ix < 0)
            ix += (int32_t)pl;
        sp_wr(pb, plb, (uint32_t)w, m[i]);
        x = pre ? sp_rd(pb, plb, (uint32_t)ix) : m[i];
        if (++w >= (int32_t)pl)
            w = 0;
        if (kh) {                                       /* TONE +: a high-pass in front */
            hp += sp_mul(x - hp, kh);
            x -= hp;
        }
        in = (x * 2580) >> 15;                          /* (Felucca's ROOM level) */
        SP_COMB_STEP(0);                                /* the combs: damped feedback loops; the right side's mix */
        al = ar = o;                                    /* .. alternates their signs (decorrelated from the left) */
        SP_COMB_STEP(1);
        al += o;
        ar -= o;
        SP_COMB_STEP(2);
        al += o;
        ar += o;
        SP_COMB_STEP(3);
        al += o;
        ar -= o;
        SP_AP_STEP(0, al);                              /* the allpasses: two each side */
        SP_AP_STEP(1, al);
        SP_AP_STEP(2, ar);
        SP_AP_STEP(3, ar);
        ar = al + (((ar - al) * sprd) / 100);           /* SPRD: 0 mono */
        wl[i] += (al * g) >> 15;
        wr[i] += (ar * g) >> 15;
    }
    S->ci[0] = (uint16_t)i0;
    S->ci[1] = (uint16_t)i1;
    S->ci[2] = (uint16_t)i2;
    S->ci[3] = (uint16_t)i3;
    S->ai[0] = (uint16_t)j0;
    S->ai[1] = (uint16_t)j1;
    S->ai[2] = (uint16_t)j2;
    S->ai[3] = (uint16_t)j3;
    S->clp[0] = lp0;
    S->clp[1] = lp1;
    S->clp[2] = lp2;
    S->clp[3] = lp3;
    S->rhp = hp;
    S->pw = (uint32_t)w;
}
#undef SP_COMB_STEP
#undef SP_AP_STEP

/* audio ISR: track t's SPACE added to its stereo block (n even) */
static void space_block(uint32_t t, int32_t *l, int32_t *r, uint32_t n)
{
    space_t *S = &space[t];
    const int16_t *v = tp[t].dev[DEV_SPACE];
    uint8_t *db[SP_LMAX], *pb[SP_LMAX], *rb[SP_REV];
    int32_t tone = v[SP_TONE], sprd = clamp(v[SP_SPRD], 0, 100), gd1, gv1, gd0 = S->gd, gv0 = S->gv;
    int32_t kl = 32767, kh = 0, damp = 6554, fbg = clamp(v[SP_FDBK], 0, 100) * 32112 / 100;
    int32_t tgt = clamp(v[SP_TIME], 10, 370) * 5645;   /* ms -> Q8 samples at 22.05 kHz (22.05 x 256) */
    int32_t off = sprd * 220 * 256 / 100, pre = clamp(v[SP_PRE], 0, 200) * 2205 / 100;
    uint32_t i, k, dl = 0, pl = 0, h = n / 2u;
    int dlb = 0, plb = 0;
    if (S->dly.ready) {
        if (!S->dlive || S->dgen != S->dly.gen) {      /* starting: from silence, at TIME already */
            S->dgen = S->dly.gen;
            S->dw = 0;
            S->dq = tgt;
            S->dlp = 0;
            S->dlive = 1;
        }
        sp_bases(&S->dly, db);
        dl = sp_len(&S->dly);
        dlb = S->dly.bits8;
    } else
        S->dlive = 0;
    if (S->rev.ready && S->pre.ready) {
        if (!S->rlive || S->rgen != (uint8_t)(S->rev.gen + S->pre.gen)) {
            S->rgen = (uint8_t)(S->rev.gen + S->pre.gen);
            S->pw = 0;
            S->rhp = 0;
            for (k = 0; k < 4u; k++) {
                S->ci[k] = S->ai[k] = 0;
                S->clp[k] = 0;
            }
            S->size_seen = -1;
            S->rlive = 1;
        }
        sp_bases(&S->pre, pb);
        sp_bases(&S->rev, rb);
        pl = sp_len(&S->pre);
        plb = S->pre.bits8;
        if (v[SP_SIZE] != S->size_seen || v[SP_DEC] != S->dec_seen)
            sp_room(S, clamp(v[SP_SIZE], 0, 100), clamp(v[SP_DEC], 0, 100));
    } else
        S->rlive = 0;
    gd1 = S->dlive ? clamp(v[SP_DLY], 0, 100) * 26214 / 100 : 0;   /* DLY 100: the first echo at 0.8 */
    gv1 = S->rlive ? clamp(v[SP_VERB], 0, 100) * 32767 / 100 : 0;
    S->gd = gd1;
    S->gv = gv1;
    if (!gd0 && !gd1 && !gv0 && !gv1) {                 /* nothing on (or nothing yet) */
        S->pl = S->pr = 0;
        return;
    }
    if (tone < 0) {                                     /* - : a low-pass in the feedback, darker combs */
        kl = 32767 + tone * 287;                        /* (to 0.12 at -100: ~430 Hz) */
        damp = 6554 - tone * 200;
    } else if (tone > 0) {                              /* + : a high-pass in the feedback and before the room */
        kh = tone * 60;                                 /* (to 0.18 at 100: ~650 Hz) */
        damp = 6554 - tone * 49;
    }
    if (dl && tgt > (int32_t)(dl - 4u) << 8)
        tgt = (int32_t)(dl - 4u) << 8;
    if (pl && pre > (int32_t)pl - 2)
        pre = (int32_t)pl - 2;
    {
        int32_t m[CTL / 2], wl[CTL / 2], wr[CTL / 2], ol = S->pl, or_ = S->pr;
        for (i = 0; i < h; i++) {                       /* mono, at half the rate */
            m[i] = sp_sat((l[2 * i] + r[2 * i] + l[2 * i + 1] + r[2 * i + 1]) >> 2);
            wl[i] = wr[i] = 0;
        }
        if (S->dlive)
            sp_delay(S, db, dlb, dl, tgt, off, kl, kh, fbg, gd0, gd1, m, wl, wr);
        if (S->rlive)
            sp_room_run(S, pb, plb, pl, pre, rb, kh, damp, sprd, gv0, gv1, m, wl, wr);
        for (i = 0; i < h; i++) {                       /* back up to 44.1 kHz: halfway, then the sample */
            l[2 * i] += (ol + wl[i]) >> 1;
            r[2 * i] += (or_ + wr[i]) >> 1;
            l[2 * i + 1] += wl[i];
            r[2 * i + 1] += wr[i];
            ol = wl[i];
            or_ = wr[i];
        }
        S->pl = ol;
        S->pr = or_;
    }
}

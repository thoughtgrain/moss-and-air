/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: GRAIN, the first device after the source (docs/bryo-architecture.md, "GRAIN, as built"). It granulates the
 * track's tape inside TAPE's loop window (STRT, LEN): what its picture draws, and what the white keys pick on its
 * page. The grains are blended with the source by WET; from here on the track is stereo (SPRD places each grain).
 *
 * The cursor: where grains start, moving through the loop window at WARP's speed (100 %: as fast as the tape plays,
 * 0: still, below 0: backwards) while the transport plays or a key holds a slice, wrapping at the window's ends. The 0 black key held freezes every track's cursor
 * (PRD 2.3: a momentary global freeze). On the GRAIN page of a TAPE track the white keys move it to their slice
 * (sys.keys_grain) instead of moving the tape's head.
 *
 * Grains start while the transport plays, or while a white key is held on the GRAIN page (stopped, as TAPE's slices
 * play once); those sounding finish either way.
 *
 * A grain: starts around the cursor (SPRY scatters it, up to half the window either way), lasts SIZE ms, reads at
 * PTCH semitones plus a random +-PRND (held to SCAL's scale when it isn't OFF), backwards for REV's share of grains,
 * shaped by a window whose ramps grow with CONT (0: square with 2 ms ends, 100: a full Hann; taken once a block and
 * ramped across it), placed in the stereo
 * field at random within +-SPRD, its level 1 / sqrt(the grains expected to overlap). RATE sets how many start a
 * second (1..80, on a square law), PATN their rhythm: EVEN, SWNG (long-short), CLST (bursts of four), RND.
 *
 * Reading: no copy of the tape. A grain decodes the ADPCM itself into a 64-sample window, from the stored decoder
 * state at the start of the window's block; a forward grain carries its decoder on (one decode per tape sample), a
 * reverse grain refills its window backwards (up to 256 decodes per 64 samples). A grain starting past the tape's end
 * reads silence.
 *
 * Load: at most GR_CAP (8) grains sound per track; a grain due while all of them sound is skipped. The audio ISR's
 * shedding (chain_shed) lowers that cap for every track, two at a time down to 4, and it comes back one a second.
 * Audio ISR only; the main loop writes the knobs. */

#define GR_CAP 8u                    /* grains sounding at once per track, at most */
#define GR_WIN 128u                  /* a grain's decoded window, tape samples (a reverse refill re-decodes from its
                                      * block's start: the wider the window, the rarer) */

/* sin^2 (pi/2 i/128), Q15: a window's rising ramp */
static const int16_t GR_RAMP[129] = {
    0, 5, 20, 44, 79, 123, 177, 241, 315, 398, 491, 593, 705, 827, 958, 1098,
    1247, 1406, 1573, 1749, 1935, 2128, 2331, 2542, 2761, 2989, 3224, 3468, 3719, 3978, 4244, 4518,
    4799, 5086, 5381, 5682, 5990, 6304, 6624, 6950, 7281, 7618, 7961, 8308, 8660, 9017, 9379, 9744,
    10114, 10487, 10864, 11244, 11628, 12014, 12403, 12794, 13187, 13583, 13980, 14378, 14778, 15178, 15580, 15981,
    16383, 16786, 17187, 17589, 17989, 18389, 18787, 19184, 19580, 19973, 20364, 20753, 21139, 21523, 21903, 22280,
    22653, 23023, 23388, 23750, 24107, 24459, 24806, 25149, 25486, 25817, 26143, 26463, 26777, 27085, 27386, 27681,
    27968, 28249, 28523, 28789, 29048, 29299, 29543, 29778, 30006, 30225, 30436, 30639, 30832, 31018, 31194, 31361,
    31520, 31669, 31809, 31940, 32062, 32174, 32276, 32369, 32452, 32526, 32590, 32644, 32688, 32723, 32747, 32762,
    32767,
};
/* 1 / sqrt(n), Q15: a grain's level when n grains are expected to overlap */
static const int16_t GR_NORM[17] = {32767, 32767, 23170, 18918, 16384, 14654, 13377, 12385, 11585, 10922, 10362, 9880, 9459, 9088, 8757, 8460, 8192};
/* the scales SCAL holds pitches to (OFF CHR MAJ MIN PEN): the semitones of an octave they keep */
static const uint16_t GR_SCALE[5] = {0, 0xFFF, 0xAB5, 0x5AD, 0x295};

typedef struct {
    int32_t pos;                     /* the read position, Q12 tape samples */
    int32_t inc;                     /* its step per output sample, Q12 (below 0: backwards) */
    uint32_t left, len, fade;        /* output samples to go, in all, in each ramp */
    int32_t gl, gr;                  /* its level into each side, Q15 */
    int32_t wlo;                     /* the tape sample at w[0] (-1: nothing decoded yet) */
    int32_t dpos, pred, idx;         /* the decoder: the next tape sample it decodes, its state */
    int16_t w[GR_WIN];
    int16_t st16;                    /* its pitch, 1/16 semitone (for the tests and the screen) */
    uint8_t rev;
} grain_t;

typedef struct {
    grain_t g[GR_CAP];
    uint32_t used;                   /* a bit per grain sounding */
    int32_t cur;                     /* the cursor, Q12 tape samples */
    int32_t wait;                    /* output samples until the next grain */
    uint32_t n;                      /* grains started (PATN's beat count) */
    uint32_t keys;                   /* the white keys last block (the GRAIN page) */
    int32_t rng;
} grain_trk_t;

static grain_trk_t grain[NTRK] __attribute__((section(".pool")));
static uint32_t grain_cap = GR_CAP;  /* lowered by chain_shed */
static volatile uint8_t grain_frozen;   /* the 0 key held (the ISR writes it; the screen tags FROZEN) */
static uint32_t grain_calm;          /* blocks since the last shed */

static inline uint32_t gr_rnd(grain_trk_t *G) { return noise32(&G->rng); }

/* decode tape samples dpos.. into out (n of them; silence past the tape), the decoder's state carried in locals;
 * each block starts from its stored state */
static void gr_decode(grain_t *g, const tape_view_t *v, int16_t *out, int32_t n)
{
    int32_t pred = g->pred, idx = g->idx, s = g->dpos, len = (int32_t)v->len, i;
    const uint8_t *d = s < len ? tv_data(v, (uint32_t)s / TAPE_BLK) : 0;
    for (i = 0; i < n; i++, s++) {
        uint32_t o = (uint32_t)s % TAPE_BLK;
        if (s >= len) {
            out[i] = 0;
            continue;
        }
        if (!o) {                                       /* a block's start: its data and its stored state */
            d = tv_data(v, (uint32_t)s / TAPE_BLK);
            pred = tv_pred(v, (uint32_t)s / TAPE_BLK);
            idx = tv_idx(v, (uint32_t)s / TAPE_BLK);
        }
        out[i] = (int16_t)ima_step(&pred, &idx, (d[o >> 1] >> ((o & 1u) * 4u)) & 15u);
    }
    g->pred = pred;
    g->idx = idx;
    g->dpos = s;
}

/* decode the window from tape sample lo (kept inside the tape). Moving forward over what it holds, it keeps the
 * overlap and decodes on from where its decoder is (each tape sample decoded once); otherwise it starts at lo's
 * block (its stored state) and decodes up to lo first. */
static void gr_fill(grain_t *g, const tape_view_t *v, int32_t lo)
{
    static int16_t skip[TAPE_BLK];                      /* (what's decoded on the way to lo: thrown away) */
    int32_t len = (int32_t)v->len, i, keep;
    if (lo > len - (int32_t)GR_WIN)
        lo = len - (int32_t)GR_WIN;
    if (lo < 0)
        lo = 0;
    if (g->wlo >= 0 && lo > g->wlo && lo <= g->dpos && g->dpos == g->wlo + (int32_t)GR_WIN) {
        keep = g->dpos - lo;                            /* the forward slide: the overlap stays */
        for (i = 0; i < keep; i++)
            g->w[i] = g->w[i + lo - g->wlo];
        gr_decode(g, v, g->w + keep, (int32_t)GR_WIN - keep);
        g->wlo = lo;
        return;
    }
    g->dpos = lo / (int32_t)TAPE_BLK * (int32_t)TAPE_BLK;   /* from the block's start */
    if (lo > g->dpos)
        gr_decode(g, v, skip, lo - g->dpos);
    gr_decode(g, v, g->w, (int32_t)GR_WIN);
    g->wlo = lo;
}

/* the tape at Q12 position p for grain g, interpolated (silence outside the tape) */
static inline int32_t gr_read(grain_t *g, const tape_view_t *v, int32_t p)
{
    int32_t i = p >> 12, f = p & 4095, a, b;
    if (i < 0 || i >= (int32_t)v->len)
        return 0;
    if (g->wlo < 0 || i < g->wlo || i + 1 >= g->wlo + (int32_t)GR_WIN)
        gr_fill(g, v, g->inc >= 0 ? i : i - (int32_t)GR_WIN + 2);
    a = g->w[i - g->wlo];
    b = i + 1 - g->wlo < (int32_t)GR_WIN ? g->w[i + 1 - g->wlo] : a;
    return a + (((b - a) * f) >> 12);
}

/* the window at output sample `at` of grain g (0 outside it), Q15: the ramps at both ends, flat between */
static inline int32_t gr_env(const grain_t *g, uint32_t at)
{
    if (at >= g->len)
        return 0;
    if (at < g->fade)
        return GR_RAMP[at * 128u / g->fade];
    if (g->len - at <= g->fade)
        return GR_RAMP[(g->len - at) * 128u / g->fade];
    return 32767;
}

/* PTCH + a random +-PRND, held to SCAL: 1/16 semitones */
static int32_t gr_pitch(grain_trk_t *G, const int16_t *p)
{
    int32_t st16 = p[2] * 16, sc = clamp(p[10], 0, 4), k, best = 0, bd = 1 << 20;
    if (p[11])
        st16 += (int32_t)(gr_rnd(G) % (uint32_t)(p[11] * 32 + 1)) - p[11] * 16;
    if (!sc)
        return st16;
    for (k = -36; k <= 36; k++) {                       /* the nearest semitone the scale keeps */
        int32_t d = k * 16 - st16;
        d = d < 0 ? -d : d;
        if (((GR_SCALE[sc] >> (uint32_t)((k % 12 + 12) % 12)) & 1u) && d < bd) {
            bd = d;
            best = k * 16;
        }
    }
    return best;
}

/* a grain's step for pitch st16 (1/16 semitone), Q12 tape samples per output sample: 2048 at the tape's own pitch */
static int32_t gr_rate(int32_t st16)
{
    return (int32_t)(pitch_inc((uint32_t)clamp(60 * 16 + st16, 0, 2047)) / (pitch_inc(60u * 16u) >> 11));
}

/* the gap before the next grain, output samples: RATE 0..100 -> 1..80 grains a second, in PATN's rhythm */
static int32_t gr_gap(grain_trk_t *G, const int16_t *p)
{
    int32_t r = p[1], gps100 = 100 + r * r * 79 / 100, t = 4410000 / gps100;   /* (grains a second x 100) */
    switch (p[9]) {
    case 0: return t;                                   /* EVEN */
    case 1: return G->n & 1u ? t * 2 / 3 : t * 4 / 3;   /* SWNG: long, short */
    case 2: return G->n % 4u == 3u ? t * 13 / 4 : t / 4;   /* CLST: three close, then a gap */
    default: return t / 4 + (int32_t)(gr_rnd(G) % (uint32_t)(t * 3 / 2 + 1));   /* RND */
    }
}

static void gr_start(uint32_t t, grain_trk_t *G, const int16_t *p, const tape_view_t *v, int32_t ls, int32_t ll)
{
    grain_t *g;
    uint32_t k;
    int32_t at, sp, rate, gps100, ov, pan;
    for (k = 0; k < grain_cap && ((G->used >> k) & 1u); k++)
        ;
    if (k >= grain_cap)
        return;                                         /* all sounding: this one is skipped */
    g = &G->g[k];
    G->used |= 1u << k;
    sp = ll * p[5] / 200;                               /* SPRY: up to half the window either way */
    at = (G->cur >> 12) + (sp ? (int32_t)(gr_rnd(G) % (uint32_t)(2 * sp + 1)) - sp : 0);
    at = ls + ((at - ls) % ll + ll) % ll;
    g->st16 = (int16_t)gr_pitch(G, p);
    rate = gr_rate(g->st16);
    g->rev = (uint8_t)((int32_t)(gr_rnd(G) % 100u) < p[7]);
    g->len = (uint32_t)p[0] * 441u / 10u;              /* SIZE ms -> output samples */
    g->fade = (uint32_t)p[6] * g->len / 200u;          /* CONT: the ramps, up to half the grain each */
    if (g->fade < 88u)
        g->fade = 88u < g->len / 2u ? 88u : g->len / 2u;   /* (2 ms ends at the least: no click) */
    g->left = g->len;
    g->inc = g->rev ? -rate : rate;
    g->pos = (g->rev ? at + (int32_t)((uint32_t)rate * g->len >> 12) : at) << 12;   /* (backwards: from its end) */
    g->wlo = -1;
    gps100 = 100 + p[1] * p[1] * 79 / 100;
    ov = clamp(gps100 * p[0] / 100000, 1, 16);          /* grains overlapping: a second's grains x SIZE */
    pan = p[3] ? (int32_t)(gr_rnd(G) % (uint32_t)(2 * p[3] + 1)) - p[3] : 0;
    g->gl = GR_NORM[ov] * (100 - pan) / 200;            /* (centre: each side half) */
    g->gr = GR_NORM[ov] * (100 + pan) / 200;
    (void)t;
}

/* audio ISR: track t's GRAIN on one block. dry: the source (n samples); keys: the white keys on the GRAIN page (0
 * otherwise); frozen: the 0 key held. The track's stereo out into l and r. */
static void grain_block(uint32_t t, const int32_t *dry, uint32_t keys, int frozen, int32_t *l, int32_t *r, uint32_t n)
{
    grain_trk_t *G = &grain[t];
    const int16_t *p = tp[t].dev[DEV_GRAIN];
    tape_view_t v;
    int32_t ls, ll, wet = p[4], i, warp, len;
    uint32_t k, press = keys & ~G->keys;
    G->keys = keys;
    if (!G->rng)
        G->rng = 0x2545F491 + (int32_t)t * 7919;
    for (i = 0; i < (int32_t)n; i++)                     /* the dry share, centred */
        l[i] = r[i] = dry[i] * (100 - wet) / 100;
    tape_view(t, &v);
    len = (int32_t)v.len;
    if (!len) {                                         /* nothing on the tape: no grains */
        G->used = 0;
        return;
    }
    tape_window(t, v.len, &ls, &ll);
    if (G->cur < ls << 12 || G->cur >= (ls + ll) << 12)
        G->cur = ls << 12;
    for (k = 0; press && k < NWHITE; k++)                /* a key: the cursor to its slice */
        if ((press >> k) & 1u) {
            G->cur = (ls + ll * (int32_t)k / 16) << 12;
            G->wait = 0;
            break;
        }
    grain_frozen = (uint8_t)frozen;
    warp = frozen || !(sys.playing || keys) ? 0 : 2048 * p[8] / 100 * (int32_t)n;   /* WARP: 100 % = the tape's own
                                                                                       * pace (0.5 a sample); stopped,
                                                                                       * still (as the tape's head) */
    G->cur += warp;
    while (G->cur >= (ls + ll) << 12)
        G->cur -= ll << 12;
    while (G->cur < ls << 12)
        G->cur += ll << 12;
    if (!wet) {                                         /* (dry only: the grains stop, the cursor goes on) */
        G->used = 0;
        return;
    }
    G->wait -= (int32_t)n;
    if (G->wait <= 0 && (sys.playing || keys)) {       /* (stopped: only while a key holds a slice) */
        gr_start(t, G, p, &v, ls, ll);
        G->n++;
        G->wait += gr_gap(G, p);
        if (G->wait < (int32_t)n)
            G->wait = (int32_t)n;
    }
    for (k = 0; k < GR_CAP; k++) {
        grain_t *g = &G->g[k];
        int32_t e0, e1, gl, gr, m;
        if (!((G->used >> k) & 1u))
            continue;
        m = (int32_t)(g->left < n ? g->left : n);
        e0 = gr_env(g, g->len - g->left);               /* the window: its value at the block's ends, ramped */
        e1 = gr_env(g, g->len - g->left + (uint32_t)m);
        gl = g->gl * wet / 100;                         /* (WET folded into the pan) */
        gr = g->gr * wet / 100;
        {
            int32_t pos = g->pos, inc = g->inc, wlo = g->wlo, de = ((e1 - e0) * 32) >> CTL_LOG2, e = e0 << 5;
            const int16_t *w = g->w;
            for (i = 0; i < m; i++, e += de, pos += inc) {   /* (the grain's state in locals: l and r can't alias it) */
                int32_t ix = (pos >> 12) - wlo, x, a, b;
                if (wlo >= 0 && ix >= 0 && ix + 1 < (int32_t)GR_WIN) {   /* (the usual case: inside the window) */
                    a = w[ix];
                    b = w[ix + 1];
                    x = a + (((b - a) * (pos & 4095)) >> 12);
                } else {
                    x = gr_read(g, &v, pos);
                    wlo = g->wlo;
                }
                x = (x * (e >> 5)) >> 15;
                l[i] += (x * gl) >> 15;
                r[i] += (x * gr) >> 15;
            }
            g->pos = pos;
        }
        g->left -= (uint32_t)m;
        if (!g->left)
            G->used &= ~(1u << k);
    }
}

/* the grains sounding on track t (the screen, the tests) */
static uint32_t grain_count(uint32_t t)
{
    uint32_t k, c = 0;
    for (k = 0; k < GR_CAP; k++)
        c += (grain[t].used >> k) & 1u;
    return c;
}

/* audio ISR: shedding takes grains first (two off the cap, down to 4); a second without it gives one back */
static void grain_shed(void)
{
    if (grain_cap > 4u)
        grain_cap -= 2u;
    grain_calm = 0;
}
static void grain_recover(void)
{
    if (++grain_calm >= 1378u && grain_cap < GR_CAP) {   /* (1378 blocks: a second) */
        grain_cap++;
        grain_calm = 0;
    }
}

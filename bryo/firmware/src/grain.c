/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: GRAIN, the first device after the source (docs/bryo-architecture.md, "GRAIN, as built"). The S-4's MOSAIC:
 * grains of a live buffer of what the source plays, blended with the source by WET; from here on the track is stereo
 * (SPRD places each grain).
 *
 * The buffer: while GRAIN is on (WET above 0) and SCAN isn't TAPE, it keeps the last few bars of the source (the
 * tape playing, or SYNTH or POLY as you play them) in chunks of the shared memory (mem.c), in the tape's own format.
 * Its length follows TRACKS: 1 bar with 3 or 4 tracks, 2 bars with 2, 4 bars with 1 (whole bars, at most 12 s, so
 * a slow tempo gets fewer). The write head starts at the bar line when the transport starts and wraps at the
 * buffer's end, so the buffer always holds whole bars. It records while the transport plays, or always on a SYNTH
 * or POLY track (you play those without the transport); FDBK keeps that share of what was there (0: the last bars
 * only; 100: layers that never fade). Switching the source carries over: the old sound stays in the buffer until the
 * write head passes, fading at FDBK each pass.
 *
 * Freeze (the 0 black key, held: every track): the buffer stops recording and holds its bars, and the grains go on
 * playing them, a loop locked to the tempo (a tempo change while frozen: the loop's pace follows).
 *
 * SCAN: where grains start (the cursor).
 *   TAPE  grains of the track's tape inside TAPE's loop window, as before the buffer (no memory taken): the cursor
 *         moves at WARP's speed (100 %: the tape's pace; 0 still; below 0 backwards); frozen: it stops.
 *   STR   (stretch) the cursor moves through the buffer at WARP's speed against the write head's: 100 keeps pace
 *         (a fixed distance behind), below falls behind and stretches time, 0 holds a spot.
 *   POS   (position) the cursor stays at OFST of the buffer (0: the bar line).
 *   DLY   (delay) the cursor trails the write head by OFST of the buffer: grains of what played that long ago.
 * On the GRAIN page the white keys move the cursor to their sixteenth of the tape's loop (TAPE) or of the buffer
 * (POS: the spot; DLY: the trail), instead of moving the tape's head; turning OFST takes over again.
 *
 * A grain: starts around the cursor (SPRY scatters it, up to half the window either way), lasts SIZE ms, reads at
 * PTCH semitones plus a random +-PRND (held to SCAL's scale when it isn't OFF), backwards for REV's share of grains,
 * shaped by a window whose ramps grow with CONT (0: square with 2 ms ends, 100: a full Hann; taken once a block and
 * ramped across it), placed in the stereo field at random within +-SPRD, its level 1 / sqrt(the grains expected to
 * overlap). RATE sets how many start a second (1..80, on a square law), PATN their rhythm: EVEN, SWNG (long-short),
 * CLST (bursts of four), RND. Grains start while the transport plays, on a SYNTH or POLY track, while frozen, or
 * while a white key is held on the GRAIN page; those sounding finish either way.
 *
 * Reading: no copy. A grain decodes the ADPCM itself into a 128-sample window, from the stored decoder state at the
 * start of the window's block; a forward grain carries its decoder on (one decode per sample), a reverse grain
 * refills its window backwards. Outside what it reads: silence.
 *
 * Load: 32 grains in all, in four groups of 8. Each track has its group, and a parked track's group goes to a track
 * still on (TRACKS), so with 1 or 2 tracks a track sounds up to 16 at once; the total never passes 32. A grain due
 * while all a track's allowed grains sound is skipped. The audio ISR's shedding (chain_shed) lowers every track's
 * allowance, two of each 8 at a time down to half, and it comes back one a second.
 *
 * Audio ISR, except grain_poll (the main loop: the buffers' memory). The main loop writes the knobs. */

#define GR_SLOTS 32u                 /* grains in all */
#define GR_GROUP 8u                  /* a group: a track's own share */
#define GR_CAP 8u                    /* grains a group sounds at once (shedding lowers it) */
#define GR_MAXT 16u                  /* grains one track sounds at once, at most */
#define GR_WIN 128u                  /* a grain's decoded window, samples (a reverse refill re-decodes from its
                                      * block's start: the wider the window, the rarer) */
#define GR_BUF_MAX (12u * TAPE_SR)   /* a buffer's most, tape samples (12 s) */
enum { GP_SIZE, GP_RATE, GP_PTCH, GP_SPRD, GP_WET, GP_SPRY, GP_CONT, GP_REV, GP_PATN, GP_SCAL, GP_PRND, GP_SCAN = 12,
       GP_WARP, GP_OFST, GP_FDBK };   /* (param.c DEV_P[DEV_GRAIN]: page 3's fourth knob is empty) */
enum { SCAN_TAPE, SCAN_STR, SCAN_POS, SCAN_DLY };

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
    int32_t pos;                     /* the read position, Q12 samples */
    int32_t inc;                     /* its step per output sample, Q12 (below 0: backwards) */
    uint32_t left, len, fade;        /* output samples to go, in all, in each ramp */
    int32_t gl, gr;                  /* its level into each side, Q15 */
    int32_t wlo;                     /* the sample at w[0] (-1: nothing decoded yet) */
    int32_t dpos, pred, idx;         /* the decoder: the next sample it decodes, its state */
    int16_t w[GR_WIN];
    int16_t st16;                    /* its pitch, 1/16 semitone (for the tests and the screen) */
    uint8_t rev;
    uint8_t trk;                     /* whose it is */
} grain_t;

typedef struct {                     /* a track's live buffer */
    uint8_t map[MEM_NC];             /* its chunks (the main loop's) */
    volatile uint8_t nch;
    volatile uint8_t bars;           /* the bars it holds */
    volatile uint32_t len;           /* samples it holds: its bars at the tempo it was sized at (0: none) */
    /* the ISR's */
    int32_t w;                       /* the write head, samples */
    int32_t acc;                     /* two output samples to one buffer sample */
    uint8_t odd;
    uint8_t staged;                  /* stage holds block sblk, being written */
    uint8_t was_playing;
    uint32_t sblk;
    int16_t stage[TAPE_BLK];
} gr_buf_t;

typedef struct {
    int32_t cur;                     /* the cursor, Q12 samples */
    int32_t wait;                    /* output samples until the next grain */
    uint32_t n;                      /* grains started (PATN's beat count) */
    uint32_t keys;                   /* the white keys last block (the GRAIN page) */
    int32_t rng;
    int32_t kofs;                    /* a white key's spot (POS: where; DLY: the trail), samples; -1 none */
    int16_t ofst_seen;               /* OFST when the key was pressed (turning OFST takes over again) */
    uint8_t scan;                    /* SCAN last block (a change: the cursor starts over) */
} grain_trk_t;

static grain_t gslot[GR_SLOTS] __attribute__((section(".pool")));
static uint32_t gr_used;             /* a bit per slot sounding */
static grain_trk_t grain[NTRK];
static gr_buf_t gbuf[NTRK];
static uint32_t grain_cap = GR_CAP;  /* each group's allowance, lowered by chain_shed */
static volatile uint8_t grain_frozen;   /* the 0 key held (the ISR writes it; the screen tags FROZEN) */
static uint32_t grain_calm;          /* blocks since the last shed */

static inline uint32_t gr_rnd(grain_trk_t *G) { return noise32(&G->rng); }

/* TRACKS in use (sys.ntrk, read safely) */
static uint32_t gr_ntrk(void) { return sys.ntrk >= 1u && sys.ntrk <= NTRK ? sys.ntrk : NTRK; }

/* track t's devices may hold memory: it's in use (TRACKS) and the USB record mode isn't up (nothing renders then,
 * and the take can have their memory) */
static int trk_live(uint32_t t) { return t < gr_ntrk() && !sys.usbrec; }

/* the groups track t owns: its own, and those of parked tracks (group g goes to track g mod TRACKS) */
static uint32_t gr_groups(uint32_t t)
{
    uint32_t g, n = 0, nt = gr_ntrk();
    for (g = 0; g < NTRK; g++)
        n += g % nt == t;
    return n;
}

/* how many grains track t may sound at once now */
static uint32_t gr_allow(uint32_t t)
{
    uint32_t a = gr_groups(t) * grain_cap, most = GR_MAXT * grain_cap / GR_CAP;
    return a > most ? most : a;
}

/* the grains sounding on track t (the screen, the tests) */
static uint32_t grain_count(uint32_t t)
{
    uint32_t k, c = 0;
    for (k = 0; k < GR_SLOTS; k++)
        c += ((gr_used >> k) & 1u) && gslot[k].trk == t;
    return c;
}

/* every grain of track t stops */
static void grain_kill(uint32_t t)
{
    uint32_t k;
    for (k = 0; k < GR_SLOTS; k++)
        if (gslot[k].trk == t)
            gr_used &= ~(1u << k);
}

/* the bars a buffer holds: by TRACKS, fewer when they'd pass 12 s at this tempo */
static uint32_t gr_bars(void)
{
    uint32_t nt = gr_ntrk(), b = nt >= 3u ? 1u : nt == 2u ? 2u : 4u, bar = 5292000u / (sys.bpm ? sys.bpm : 120u);
    while (b > 1u && b * bar > GR_BUF_MAX)
        b /= 2u;
    return b;
}

/* a bar in output samples at the tempo now (4 beats) */
static uint32_t gr_bar_out(void) { return 10584000u / (sys.bpm ? sys.bpm : 120u); }

/* track t's buffer as a view (what grains read) */
static void gr_buf_view(uint32_t t, tape_view_t *v)
{
    gr_buf_t *B = &gbuf[t];
    uint32_t cap = (uint32_t)B->nch * TAPE_CHS;
    memset(v, 0, sizeof *v);
    v->map = B->map;
    v->len = B->len < cap ? B->len : cap;
    v->ram = 1;
}

/* ----------------------------------------------------------- the buffer --- */
/* the staged block back into its chunk (dropped when the buffer has shrunk past it meanwhile) */
static void gr_bcommit(gr_buf_t *B)
{
    mem_chunk_t *c;
    uint32_t b = B->sblk;
    if (!B->staged)
        return;
    B->staged = 0;
    if (b / MEM_CB >= B->nch || b * TAPE_BLK >= B->len)
        return;
    c = mem_at(B->map[b / MEM_CB]);
    ima_fit(B->stage, &c->pred[b % MEM_CB], &c->idx[b % MEM_CB]);
    ima_enc(B->stage, c->pred[b % MEM_CB], c->idx[b % MEM_CB], c->data[b % MEM_CB], TAPE_BLK);
    c->peak[b % MEM_CB] = ima_peak(B->stage, TAPE_BLK);
}

/* one sample x into the buffer at the write head: what's there x FDBK % + x */
static void gr_bput(gr_buf_t *B, int32_t x, int32_t fb)
{
    uint32_t b = (uint32_t)B->w / TAPE_BLK, o = (uint32_t)B->w % TAPE_BLK;
    if (!B->staged || B->sblk != b) {
        mem_chunk_t *c = mem_at(B->map[b / MEM_CB]);
        gr_bcommit(B);
        ima_dec(c->data[b % MEM_CB], c->pred[b % MEM_CB], c->idx[b % MEM_CB], B->stage, TAPE_BLK);
        B->sblk = b;
        B->staged = 1;
    }
    B->stage[o] = (int16_t)clamp(B->stage[o] * fb / 100 + x, -32767, 32767);
    if ((uint32_t)++B->w >= B->len)
        B->w = 0;
}

/* the source's block into the buffer (22.05 kHz, as the tape: each pair of output samples averaged) */
static void gr_record(gr_buf_t *B, const int32_t *dry, uint32_t n, int32_t fb)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        B->acc += dry[i];
        B->odd ^= 1u;
        if (!B->odd) {
            gr_bput(B, B->acc / 2, fb);
            B->acc = 0;
        }
    }
}

/* ------------------------------------------------------------- reading --- */
/* decode samples dpos.. of view v into out (n of them; silence past its end), the decoder's state carried in locals;
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

/* decode the window from sample lo (kept inside the view). Moving forward over what it holds, it keeps the overlap
 * and decodes on from where its decoder is (each sample decoded once); otherwise it starts at lo's block (its stored
 * state) and decodes up to lo first. */
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

/* the view at Q12 position p for grain g, interpolated (silence outside it) */
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
    int32_t st16 = p[GP_PTCH] * 16, sc = clamp(p[GP_SCAL], 0, 4), k, best = 0, bd = 1 << 20;
    if (p[GP_PRND])
        st16 += (int32_t)(gr_rnd(G) % (uint32_t)(p[GP_PRND] * 32 + 1)) - p[GP_PRND] * 16;
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

/* a grain's step for pitch st16 (1/16 semitone), Q12 samples per output sample: 2048 at the sound's own pitch */
static int32_t gr_rate(int32_t st16)
{
    return (int32_t)(pitch_inc((uint32_t)clamp(60 * 16 + st16, 0, 2047)) / (pitch_inc(60u * 16u) >> 11));
}

/* the gap before the next grain, output samples: RATE 0..100 -> 1..80 grains a second, in PATN's rhythm */
static int32_t gr_gap(grain_trk_t *G, const int16_t *p)
{
    int32_t r = p[GP_RATE], gps100 = 100 + r * r * 79 / 100, t = 4410000 / gps100;   /* (grains a second x 100) */
    switch (p[GP_PATN]) {
    case 0: return t;                                   /* EVEN */
    case 1: return G->n & 1u ? t * 2 / 3 : t * 4 / 3;   /* SWNG: long, short */
    case 2: return G->n % 4u == 3u ? t * 13 / 4 : t / 4;   /* CLST: three close, then a gap */
    default: return t / 4 + (int32_t)(gr_rnd(G) % (uint32_t)(t * 3 / 2 + 1));   /* RND */
    }
}

/* a new grain of track t around the cursor, in the window [ls, ls + ll) of what it reads */
static void gr_start(uint32_t t, grain_trk_t *G, const int16_t *p, int32_t ls, int32_t ll)
{
    grain_t *g = 0;
    uint32_t k, nt = gr_ntrk();
    int32_t at, sp, rate, gps100, ov, pan;
    if (grain_count(t) >= gr_allow(t))
        return;                                         /* all it may sound are sounding: this one is skipped */
    for (k = 0; k < GR_SLOTS && !g; k++)                /* a free slot in a group it owns */
        if (!((gr_used >> k) & 1u) && (k / GR_GROUP) % nt == t)
            g = &gslot[k];
    if (!g)
        return;
    gr_used |= 1u << (uint32_t)(g - gslot);
    g->trk = (uint8_t)t;
    sp = ll * p[GP_SPRY] / 200;                         /* SPRY: up to half the window either way */
    at = (G->cur >> 12) + (sp ? (int32_t)(gr_rnd(G) % (uint32_t)(2 * sp + 1)) - sp : 0);
    at = ls + ((at - ls) % ll + ll) % ll;
    g->st16 = (int16_t)gr_pitch(G, p);
    rate = gr_rate(g->st16);
    g->rev = (uint8_t)((int32_t)(gr_rnd(G) % 100u) < p[GP_REV]);
    g->len = (uint32_t)p[GP_SIZE] * 441u / 10u;        /* SIZE ms -> output samples */
    g->fade = (uint32_t)p[GP_CONT] * g->len / 200u;    /* CONT: the ramps, up to half the grain each */
    if (g->fade < 88u)
        g->fade = 88u < g->len / 2u ? 88u : g->len / 2u;   /* (2 ms ends at the least: no click) */
    g->left = g->len;
    g->inc = g->rev ? -rate : rate;
    g->pos = (g->rev ? at + (int32_t)((uint32_t)rate * g->len >> 12) : at) << 12;   /* (backwards: from its end) */
    g->wlo = -1;
    gps100 = 100 + p[GP_RATE] * p[GP_RATE] * 79 / 100;
    ov = clamp(gps100 * p[GP_SIZE] / 100000, 1, 16);    /* grains overlapping: a second's grains x SIZE */
    pan = p[GP_SPRD] ? (int32_t)(gr_rnd(G) % (uint32_t)(2 * p[GP_SPRD] + 1)) - p[GP_SPRD] : 0;
    g->gl = GR_NORM[ov] * (100 - pan) / 200;            /* (centre: each side half) */
    g->gr = GR_NORM[ov] * (100 + pan) / 200;
}

/* the buffer's cursor pace at WARP 100, Q12 samples per output sample: the buffer once round per its bars at the
 * tempo now (2048, as the tape, until the tempo changes under a frozen buffer) */
static int32_t gr_pace(const gr_buf_t *B, int32_t len)
{
    uint32_t bars = B->bars ? B->bars : 1u, bo = bars * gr_bar_out();
    return (int32_t)(((uint32_t)len << 4) / (bo >> 8));   /* (len x 4096 / bo, in 32 bits) */
}

/* -------------------------------------------------------------- a block --- */
/* audio ISR: track t's GRAIN on one block. dry: the source (n samples); keys: the white keys on the GRAIN page (0
 * otherwise); frozen: the 0 key held. The track's stereo out into l and r. */
static void grain_block(uint32_t t, const int32_t *dry, uint32_t keys, int frozen, int32_t *l, int32_t *r, uint32_t n)
{
    grain_trk_t *G = &grain[t];
    gr_buf_t *B = &gbuf[t];
    const int16_t *p = tp[t].dev[DEV_GRAIN];
    tape_view_t v;
    int32_t ls = 0, ll = 0, wet = p[GP_WET], i, len, live = tp[t].src != SRC_TAPE, playing = sys.playing;
    uint32_t k, press = keys & ~G->keys, scan = (uint32_t)clamp(p[GP_SCAN], 0, 3);
    G->keys = keys;
    if (!G->rng)
        G->rng = 0x2545F491 + (int32_t)t * 7919;
    grain_frozen = (uint8_t)frozen;
    for (i = 0; i < (int32_t)n; i++)                     /* the dry share, centred */
        l[i] = r[i] = dry[i] * (100 - wet) / 100;
    if (scan != SCAN_TAPE && B->len && B->nch) {        /* the buffer: the bar line at the transport's start */
        if (B->w >= (int32_t)B->len)                    /* (shrunk under it: the same place in the bars) */
            B->w %= (int32_t)B->len;
        if (playing && !B->was_playing) {
            gr_bcommit(B);
            B->w = 0;
            B->odd = 0;
            B->acc = 0;
        }
        if (!frozen && wet && (playing || live))        /* it records: what's there x FDBK + the source */
            gr_record(B, dry, n, clamp(p[GP_FDBK], 0, 100));
        else if (B->staged)
            gr_bcommit(B);
    }
    B->was_playing = (uint8_t)playing;
    if (scan == SCAN_TAPE) {                            /* what grains read: the tape, or the buffer */
        tape_view(t, &v);
        len = (int32_t)v.len;
        if (len)
            tape_window(t, v.len, &ls, &ll);
    } else {
        gr_buf_view(t, &v);
        len = (int32_t)v.len;
        ll = len;
    }
    if (!len) {                                         /* nothing to read: no grains */
        grain_kill(t);
        return;
    }
    if (scan != G->scan) {                              /* a new SCAN: the cursor starts over */
        G->scan = (uint8_t)scan;
        G->cur = ls << 12;
        G->kofs = -1;
    }
    if (G->cur < ls << 12 || G->cur >= (ls + ll) << 12)
        G->cur = ls << 12;
    if (p[GP_OFST] != G->ofst_seen)                     /* OFST turned: it takes over from a key */
        G->kofs = -1;
    for (k = 0; press && k < NWHITE; k++)                /* a key: the cursor to its sixteenth */
        if ((press >> k) & 1u) {
            G->kofs = ll * (int32_t)k / 16;
            G->ofst_seen = p[GP_OFST];
            G->cur = (ls + G->kofs) << 12;
            G->wait = 0;
            break;
        }
    switch (scan) {                                     /* the cursor */
    case SCAN_TAPE:                                     /* WARP 100 %: the tape's own pace; frozen or stopped, still */
        G->cur += frozen || !(playing || keys) ? 0 : 2048 * p[GP_WARP] / 100 * (int32_t)n;
        break;
    case SCAN_STR:                                      /* WARP against the write head; frozen: the loop plays on */
        if (frozen || playing || live || keys)
            G->cur += gr_pace(B, len) * p[GP_WARP] / 100 * (int32_t)n;
        break;
    case SCAN_POS:                                      /* still, at OFST (or a key's spot) */
        G->cur = (G->kofs >= 0 ? G->kofs : len * p[GP_OFST] / 100) << 12;
        break;
    default: {                                          /* DLY: OFST (or a key's sixteenth) behind the write head */
        int32_t lag = G->kofs >= 0 ? G->kofs : len * p[GP_OFST] / 100;
        if (lag < 2 * (int32_t)TAPE_BLK)
            lag = 2 * (int32_t)TAPE_BLK;                /* (not into the block being written) */
        if (frozen)                                     /* (the write head stopped: the trail runs on, a loop) */
            G->cur += gr_pace(B, len) * (int32_t)n;
        else
            G->cur = (B->w - lag) << 12;
        break;
    }
    }
    while (G->cur >= (ls + ll) << 12)
        G->cur -= ll << 12;
    while (G->cur < ls << 12)
        G->cur += ll << 12;
    if (!wet) {                                         /* (dry only: the grains stop, the cursor goes on) */
        grain_kill(t);
        return;
    }
    G->wait -= (int32_t)n;
    if (G->wait <= 0 && (playing || keys || (scan != SCAN_TAPE && (live || frozen)))) {
        gr_start(t, G, p, ls, ll);
        G->n++;
        G->wait += gr_gap(G, p);
        if (G->wait < (int32_t)n)
            G->wait = (int32_t)n;
    }
    for (k = 0; k < GR_SLOTS; k++) {
        grain_t *g = &gslot[k];
        int32_t e0, e1, gl, gr, m;
        if (!((gr_used >> k) & 1u) || g->trk != t)
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
            gr_used &= ~(1u << k);
    }
}

/* audio ISR: shedding takes grains first (two of each group's 8, down to 4); a second without it gives one back */
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

/* ------------------------------------------------------ main loop side --- */
/* track t's buffer to n chunks (taking from tapes as tape_steal allows: a device switched on takes from the end of
 * the longest tape) and len samples (no more than its chunks hold); shrinking: the length first, then the chunks */
static void gr_buf_size(uint32_t t, uint32_t n, uint32_t len, uint32_t bars)
{
    gr_buf_t *B = &gbuf[t];
    while (B->nch < n) {
        int32_t c = tape_alloc(MEM_GRAIN + t, NTRK, 1);
        if (c < 0)
            break;
        B->map[B->nch] = (uint8_t)c;
        RING_PUBLISH();
        B->nch++;
    }
    if (len > (uint32_t)B->nch * TAPE_CHS)
        len = (uint32_t)B->nch * TAPE_CHS;
    if (len > n * TAPE_CHS)
        len = n * TAPE_CHS;
    B->bars = (uint8_t)bars;
    B->len = len;
    RING_PUBLISH();
    while (B->nch > n) {
        uint32_t c = B->map[B->nch - 1u];
        B->nch--;
        mem_free(c);
    }
}

/* main loop, every pass: each track's buffer as long as its bars (1, 2 or 4 by TRACKS, at the tempo) while GRAIN is
 * on and SCAN reads it, none otherwise (a parked track, WET 0, SCAN TAPE). A frozen buffer keeps its size. */
static void grain_poll(void)
{
    uint32_t t, bars = gr_bars(), len = bars * (5292000u / (sys.bpm ? sys.bpm : 120u));
    for (t = 0; t < NTRK; t++) {
        const int16_t *p = tp[t].dev[DEV_GRAIN];
        int on = trk_live(t) && p[GP_WET] > 0 && p[GP_SCAN] != SCAN_TAPE;
        if (grain_frozen && on && gbuf[t].len)
            continue;
        if (on) {
            if (gbuf[t].len != len || gbuf[t].nch * TAPE_CHS < len)
                gr_buf_size(t, (len + TAPE_CHS - 1u) / TAPE_CHS, len, bars);
        } else if (gbuf[t].nch) {
            gr_buf_size(t, 0, 0, bars);
        }
    }
}

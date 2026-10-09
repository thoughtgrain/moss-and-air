/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the mixer's sound (docs/bryo-architecture.md, "The mixer, as built"): each track's channel strip, and the
 * master compressor on the sum.
 *
 * The channel strip (the mixer's CHANNEL page, the selected track): what its picture draws (ui_px.c ch_resp, over
 * 8 octaves from 40 Hz to 10 kHz), in the order the S-4 has it, after the track's devices:
 *
 *   LOW, HIGH  shelves of +-12 dB turning over (half their gain) at 210 Hz and 1.9 kHz (one pole each, so a gentle
 *              slope)
 *   FILT       one knob, DJ style: left of 0 a low-pass whose corner comes down from 10 kHz to 92 Hz, right of 0 a
 *              high-pass going up from 40 Hz to 4.5 kHz; 12 dB an octave past the corner, no resonance (POLY's
 *              filter: with the corner at its lowest it bottoms out near -30 dB rather than falling on forever)
 *   PAN        after the track's level: equal power across, and at the centre both sides at full, so turning
 *              it from the middle only ever takes one side down (a balance, for the tracks are stereo already)
 *
 * The master compressor (the mixer's MASTER page, EDIT tapped twice): on the four tracks' sum, before MASTER and
 * the output stage (master.c: its limiter and soft clip stay last), where the S-4 puts it.
 *
 *   AMT        how hard: the threshold from 0 dB down to -30 dB at 4:1 with a 6 dB soft knee, and the level made up
 *              by half of what a full-scale sound loses, so turning it up glues the mix without making it quieter
 *   ATK, REL   how fast it closes (1..100 ms) and opens (20..990 ms)
 *   MIX        the squeezed against the dry (parallel compression): 100 all squeezed; lower keeps the hits' fronts
 *
 * It follows the sum's peak once a control block (0.73 ms) and works in octaves (log2, Q8): the gain it computes
 * there is ramped across the next block. At AMT 0, and with the channel at its defaults, none of this runs: the
 * sound is what it was, sample for sample. Audio ISR; integer only. */

/* the channel's shelves turn over (half their gain) at 210 Hz and 1.94 kHz: one pole each, trapezoidal (Zavalishin's
 * TPT form: true at the top end too), its corner moved with the gain so the turnover stays put (a first-order
 * shelf's slope spans two octaves: centred on the turnover, it reaches -11 dB of a -12 dB HIGH by 8 kHz) */
#define CH_FLO 210
#define CH_FHI 1940
#define CH_BW 5793                   /* the filter's damping, Q12: sqrt 2 (Butterworth: no bump at the corner) */

typedef struct {                     /* ISR only */
    int32_t lo[2], hi[2];            /* the shelves' low-passes, each side */
    int16_t lo_seen, hi_seen;        /* LOW and HIGH the coefficients were made for */
    int32_t klo, khi;                /* .. the coefficients, Q15 */
    int32_t f1[2], f2[2];            /* the filter's state, each side */
    int16_t filt_seen;               /* FILT the coefficients were made for (-32768: none) */
    tsvf_t fc;
    int32_t pl, pr;                  /* the pan's gains last block, Q15 */
} chan_t;

static chan_t chan[NTRK];

static struct {                      /* the master compressor (the ISR's, but gr for the screen) */
    int32_t env;                     /* the sum's peak, followed */
    int32_t g;                       /* the gain last block, Q12 */
    volatile int32_t gr_q8;          /* what it takes off now, octaves Q8 (the MASTER page's meter) */
} comp = {0, 4096, 0};

/* log2(1 + i/16) and 2^(i/16), Q8 and Q14: the compressor's octaves */
static const uint16_t MX_LOG2[17] = {0, 22, 44, 63, 82, 100, 118, 134, 150, 165, 179, 193, 207, 220, 232, 244, 256};
static const uint16_t MX_EXP2[17] = {16384, 17112, 17873, 18667, 19494, 20360, 21264, 22208, 23170,
                                     24198, 25268, 26390, 27554, 28775, 30048, 31379, 32768};

/* log2(x), Q8, for x >= 1 */
static int32_t mx_log2(uint32_t x)
{
    int32_t n = 0;
    uint32_t m, f, i;
    if (!x)
        return 0;
    while (x >> (n + 1))
        n++;
    m = n >= 12 ? x >> (n - 12) : x << (12 - n);        /* the mantissa: 4096..8191 */
    f = m - 4096u;                                      /* (0..4095: 16 steps of 256) */
    i = f >> 8;
    return n * 256 + MX_LOG2[i] + (int32_t)(((MX_LOG2[i + 1] - MX_LOG2[i]) * (f & 255u)) >> 8);
}

/* 2^(e / 256) as Q12, for e in octaves Q8 (-8 .. just under +3: at most x8, so a gain times a sample stays in 32
 * bits) */
static int32_t mx_exp2(int32_t e)
{
    int32_t n, f, i, v;
    e = clamp(e, -8 * 256, 3 * 256 - 1);
    n = e >> 8;                                         /* (floor, negative too) */
    f = e & 255;
    i = f >> 4;
    v = MX_EXP2[i] + (((MX_EXP2[i + 1] - MX_EXP2[i]) * (f & 15)) >> 4);   /* Q14, 1..2 */
    return n >= 2 ? v << (n - 2) : v >> (2 - n);
}

/* a shelf's one-pole coefficient (g / (1 + g), g = tan(pi f / 44100), Q15) for a turnover fm and a gain of db: the
 * corner at fm / sqrt G for the low shelf, fm x sqrt G for the high (the turnover the geometric middle of its pole
 * and zero) */
static int32_t ch_shelf_k(int32_t fm, int32_t db, int high)
{
    int32_t sg = mx_exp2(db * 256 * 100 / 1204), wp, x, t;   /* sqrt G, Q12 (dB / 12.04 octaves) */
    wp = high ? fm * sg / 4096 : fm * 4096 / sg;
    x = wp * 4669 / 1000;                               /* pi wp / 44100, Q16 */
    t = x + (((x * x) >> 16) * x >> 16) / 3;            /* tan, to its x^3 term (under 0.2 % at 4 kHz) */
    return (int32_t)(((uint32_t)t << 15) / (uint32_t)(65536 + t));
}

/* FILT -100..100 as the filter's corner on tsvf's scale (CUTOFF_HZ: 30 Hz at 0, 14 steps an octave), Q8 */
static int32_t ch_cut(int32_t f)
{
    int32_t oct = f < 0 ? 8000 + f * 68 : f * 68;       /* octaves above 40 Hz, x1000 */
    return 14 * 256 * oct / 1000 + 1490;                /* (+ log2(40 / 30) octaves) */
}

/* audio ISR: track t's channel strip on its stereo block, before its level (LOW HIGH FILT) */
static void mix_channel(uint32_t t, int32_t *l, int32_t *r, uint32_t n)
{
    chan_t *C = &chan[t];
    const int16_t *ch = TPD(t, MA_CH);
    int32_t glo = ch[CH_LOW] ? db_q10(ch[CH_LOW]) * 4 - 4096 : 0, ghi = ch[CH_HIGH] ? db_q10(ch[CH_HIGH]) * 4 - 4096 : 0;
    int32_t f = ch[CH_FILT];
    uint32_t i, c;
    if (!glo && !ghi && !f) {
        C->filt_seen = -32768;                          /* (the filter starts from rest when it's turned again) */
        return;
    }
    if (ch[CH_LOW] != C->lo_seen || !C->klo) {
        C->klo = ch_shelf_k(CH_FLO, ch[CH_LOW], 0);
        C->lo_seen = ch[CH_LOW];
    }
    if (ch[CH_HIGH] != C->hi_seen || !C->khi) {
        C->khi = ch_shelf_k(CH_FHI, ch[CH_HIGH], 1);
        C->hi_seen = ch[CH_HIGH];
    }
    if (f && f != C->filt_seen) {
        if (C->filt_seen == -32768 || (C->filt_seen < 0) != (f < 0))
            C->f1[0] = C->f1[1] = C->f2[0] = C->f2[1] = 0;   /* (off, or LP to HP: start from rest) */
        tsvf_coef_k(&C->fc, ch_cut(f), CH_BW);
        C->filt_seen = (int16_t)f;
    }
    for (c = 0; c < 2u; c++) {                          /* (the state in locals: the compiler can't know the
                                                         *  buffers don't alias it) */
        int32_t *x = c ? r : l, lo = C->lo[c], hi = C->hi[c], s1 = C->f1[c], s2 = C->f2[c];
        int32_t klo = C->klo, khi = C->khi, a1 = C->fc.a1, a2 = C->fc.a2, a3 = C->fc.a3, kd = C->fc.k;
        for (i = 0; i < n; i++) {
            int32_t y = clamp(x[i], -65535, 65535);
            if (glo) {                                  /* the low shelf: y + (G - 1) x its low end */
                int32_t v = ((y - lo) * klo) >> 15, p = v + lo;
                lo = p + v;
                y += (p * glo) >> 12;
            }
            if (ghi) {                                  /* the high shelf: y + (G - 1) x what's above 1.9 kHz */
                int32_t v = ((y - hi) * khi) >> 15, p = v + hi;
                hi = p + v;
                y += (((y - p) >> 1) * ghi) >> 11;
            }
            if (f) {                                    /* the filter (dsp.c's tsvf, unrolled: with the input held
                                                         * to +-65535 and no resonance its states stay under
                                                         * 2 x 65535, so the products fit without clamping them) */
                int32_t v3, v1, v2;
                y = clamp(y, -65535, 65535);
                v3 = y - s2;
                v1 = (a1 * s1 + a2 * v3) >> 13;
                v2 = s2 + ((a2 * s1 + a3 * v3) >> 13);
                s1 = 2 * v1 - s1;
                s2 = 2 * v2 - s2;
                y = f < 0 ? v2 : y - ((kd * v1) >> 12) - v2;
            }
            x[i] = y;
        }
        C->lo[c] = lo;
        C->hi[c] = hi;
        C->f1[c] = s1;
        C->f2[c] = s2;
    }
}

/* PAN as the two sides' gains, Q15: sqrt 2 times cos and sin of the place (0 .. pi/2), at most 1 */
static inline void ch_pan_gains(int32_t pan, int32_t *gl, int32_t *gr)
{
    uint32_t ph = (uint32_t)(clamp(pan, -100, 100) + 100) * (0x40000000u / 200u);   /* (a quarter turn) */
    int32_t s = sine_i(ph), c = sine_i(ph + 0x40000000u);
    *gl = clamp((c * 46341) >> 15, 0, 32767);
    *gr = clamp((s * 46341) >> 15, 0, 32767);
    if (*gl > 32700)                                    /* (the centre and the near side: exactly full, so a */
        *gl = 32767;                                    /*  centred track is untouched, not x 0.9999) */
    if (*gr > 32700)
        *gr = 32767;
}

/* audio ISR: track t's level g (Q12) and pan on its block, the pan ramped from the last block's. Centred, it does
 * nothing and returns 0: the caller applies the level as it sums (one pass, as before there was a pan) */
static int mix_pan(uint32_t t, int32_t *l, int32_t *r, int32_t g, uint32_t n)
{
    chan_t *C = &chan[t];
    int32_t gl, gr, l0 = C->pl, r0 = C->pr, pl = TPD(t, MA_CH)[CH_PAN], pr = TPD(t, MA_CH) != tp[t].ch ? mod_pan_r[t] : pl, u;
    uint32_t i;
    ch_pan_gains(pl, &gl, &gr);
    if (pr != pl)                                       /* SPRD on PAN: the right channel panned apart (mod.c) */
        ch_pan_gains(pr, &u, &gr);
    if (!l0 && !r0) {                                   /* (power-on: nothing to ramp from) */
        l0 = gl;
        r0 = gr;
    }
    C->pl = gl;
    C->pr = gr;
    if (l0 == 32767 && r0 == 32767 && gl == 32767 && gr == 32767)
        return 0;                                       /* the centre */
    for (i = 0; i < n; i++) {
        int32_t a = l0 + (((gl - l0) * (int32_t)i) >> CTL_LOG2), b = r0 + (((gr - r0) * (int32_t)i) >> CTL_LOG2);
        int32_t x = (l[i] * g) >> 12, y = (r[i] * g) >> 12;
        l[i] = (clamp(x, -131071, 131071) >> 1) * a >> 14;
        r[i] = (clamp(y, -131071, 131071) >> 1) * b >> 14;
    }
    return 1;
}

/* the coefficient that follows a change over tau_ms, per control block (1 - e^(-0.726 / tau), as x / (1 + x/2)),
 * Q15 */
static int32_t mx_coef(int32_t tau_ms) { return (int32_t)(32768u * 726u / ((uint32_t)tau_ms * 1000u + 363u)); }

/* the compressor's gain (Q12, MIX included) for a level lv (octaves Q8 above a sample of 1: full scale is 15 x 256),
 * and what it takes off before MIX (*gr, octaves Q8): the sound (mix_comp) and its picture (ui_viz.c viz_master) */
static int32_t mx_curve(int32_t lv, int32_t *gr)
{
    int32_t amt = clamp(mst[MS_AMT], 0, 100), t = 15 * 256 - amt * 1276 / 100, over = lv - t, g = 0, k;
    if (over >= 128)                                    /* past the knee (6 dB wide: 256): 4:1, 3/4 taken off */
        g = over * 3 / 4;
    else if (over > -128)                               /* in the knee: easing in */
        g = 3 * (over + 128) * (over + 128) / (4 * 512);
    *gr = g;
    k = mx_exp2((15 * 256 - t) * 3 / 8 - g);            /* made up by half of what full scale loses */
    return k + (((k - 4096) * (clamp(mst[MS_MIX], 0, 100) - 100)) / 100);   /* MIX: towards the dry */
}

/* audio ISR: the master compressor on the sum (l, r), before MASTER */
static void mix_comp(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t amt = mst[MS_AMT], g0 = comp.g, g1 = 4096, pk = 0, a, gr = 0, i;
    if (!amt && g0 == 4096) {                           /* off: untouched (the meter rests) */
        comp.env = 0;
        comp.gr_q8 = 0;
        return;
    }
    for (i = 0; i < (int32_t)n; i++) {                  /* the block's peak, both sides */
        a = l[i] < 0 ? -l[i] : l[i];
        pk = a > pk ? a : pk;
        a = r[i] < 0 ? -r[i] : r[i];
        pk = a > pk ? a : pk;
    }
    a = pk > comp.env ? mx_coef(clamp(mst[MS_ATK], 1, 100)) : mx_coef(clamp(mst[MS_REL], 20, 990));
    comp.env += ((pk - comp.env) * (a >> 3)) >> 12;    /* (a >> 3: Q12) */
    if (amt)                                            /* (the threshold: 0 dB, 32768, down to -30 dB) */
        g1 = mx_curve(mx_log2((uint32_t)(comp.env > 1 ? comp.env : 1)), &gr);
    comp.g = g1;
    comp.gr_q8 = gr * clamp(mst[MS_MIX], 0, 100) / 100;
    for (i = 0; i < (int32_t)n; i++) {
        int32_t g = g0 + (((g1 - g0) * i) >> CTL_LOG2);
        l[i] = (clamp(l[i], -131071, 131071) >> 2) * g >> 10;
        r[i] = (clamp(r[i], -131071, 131071) >> 2) * g >> 10;
    }
}

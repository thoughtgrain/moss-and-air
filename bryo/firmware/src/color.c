/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: COLOR, the device after RESONATOR (docs/bryo-architecture.md, "COLOR, as built"): the S-4's DEFORM. What
 * it does to a sound, in order:
 *
 *   DRIV   gain 1..16 into a tanh soft clip (0: no clip at all, so the default is the sound untouched)
 *   CRSH   by CMOD: the bits (16 down to 2, BIT), a sample held for 1..12 samples (RATE), or both
 *   NOIS   noise riding the sound's envelope: it opens with the sound and falls over NDEC after it, so a hit gets a
 *          breath of noise and silence stays silent; NTON a one-pole filter on it (- dark .. + bright)
 *   TILT   the low end against the high around 700 Hz, up to 6 dB each way (+ brighter)
 *   WET    the dry sound against the coloured one; LVL the output, -24..+6 dB (drive adds loudness)
 *
 * The picture (ui_viz.c viz_color) draws the same mappings through color_shape(). COLOR keeps no memory, only a
 * few numbers a track. Load: nothing at all while every knob that colours is at 0 and LVL at 0 dB; about 30
 * instructions a sample with everything on. Audio ISR only. Integer only. */

enum { CP_DRIV, CP_CRSH, CP_NOIS, CP_TILT, CP_NDEC, CP_NTON, CP_CMOD, CP_WET, CP_LVL };
enum { CMOD_BIT, CMOD_RATE, CMOD_BOTH };

typedef struct {                     /* ISR only */
    int32_t held[2];                 /* CRSH RATE: the value held, each side */
    uint32_t hcnt;                   /* .. samples since it was taken */
    int32_t env;                     /* the noise's envelope, Q27 (|sample| << 12) */
    int32_t nlp;                     /* NTON's filter on the noise */
    int32_t tlo[2];                  /* TILT's low band, each side */
    int32_t wet, lvl;                /* last block's WET (Q12) and LVL (Q10): ramped across the next */
    int32_t rng;
} color_t;

static color_t color[NTRK];

/* DRIV 0..100 as a gain, Q8: 1..16 */
static inline int32_t col_g8(int32_t driv) { return 256 + driv * 3840 / 100; }

/* the drive and the bits (before the hold, the noise, TILT, WET and LVL): x in, Q15-ish; the picture uses it too */
static inline int32_t col_drive_bits(int32_t x, int32_t g8, uint32_t sh)
{
    int32_t y = g8 > 256 ? softclip(x * g8 >> 8) : x;   /* (|x| up to 65535 * 4096: 32 bits) */
    if (!sh)
        return y;
    return y >= 0 ? (y >> sh) << sh : -((-y >> sh) << sh);   /* toward zero, so no offset creeps in */
}

/* CRSH's bits taken off (0..14), by CMOD */
static inline uint32_t col_bits_off(const int16_t *v)
{
    return v[CP_CMOD] == CMOD_RATE ? 0u : (uint32_t)(v[CP_CRSH] * 14 / 100);
}

/* CRSH's hold, samples (1: none), by CMOD */
static inline uint32_t col_hold(const int16_t *v)
{
    return v[CP_CMOD] >= CMOD_RATE ? 1u + (uint32_t)(v[CP_CRSH] * 11 / 100) : 1u;
}

/* the picture's transfer: x Q15 in, coloured out before WET and LVL */
static int32_t color_shape(int32_t x, const int16_t *v)
{
    return col_drive_bits(x, v[CP_DRIV] > 0 ? col_g8(v[CP_DRIV]) : 256, col_bits_off(v));
}

/* audio ISR: track t's COLOR on its stereo block */
static void color_block(uint32_t t, int32_t *l, int32_t *r, uint32_t n)
{
    color_t *C = &color[t];
    const int16_t *v = TPD(t, DEV_COLOR);
    int32_t g8 = v[CP_DRIV] > 0 ? col_g8(v[CP_DRIV]) : 256, tilt = v[CP_TILT], nois = v[CP_NOIS];
    int32_t w0 = C->wet, w1 = clamp(v[CP_WET], 0, 100) * 4096 / 100, v0 = C->lvl, v1 = db_q10(v[CP_LVL]);
    int32_t gl = 4096, gh = 4096, ng = nois * 32767 / 100, ak = (8 + (v[CP_NTON] + 100) * 75 / 200) * 128 / 100;
    int32_t ts = (int32_t)TIME_MS_X10[v[CP_NDEC] & 127] * 441 / 100, dk;
    uint32_t sh = col_bits_off(v), hold = col_hold(v), i;
    int colours = g8 > 256 || sh || hold > 1u || nois || tilt;
    if (!C->rng)
        C->rng = 0x2545F491 + (int32_t)t;              /* (xorshift from 0 stays 0) */
    if (!v0 && !w0) {                                   /* (power-on: nothing to ramp from) */
        v0 = v1;
        w0 = w1;
    }
    C->wet = w1;
    C->lvl = v1;
    if (!colours) {                                     /* nothing to colour: only LVL, if it's off 0 dB */
        C->env = 0;
        C->nlp = 0;
        if (v0 == 1024 && v1 == 1024)
            return;
        for (i = 0; i < n; i++) {
            int32_t g = v0 + (((v1 - v0) * (int32_t)i) >> CTL_LOG2);
            l[i] = clamp(l[i], -131071, 131071) * g >> 10;
            r[i] = clamp(r[i], -131071, 131071) * g >> 10;
        }
        return;
    }
    if (tilt > 0) {                                     /* + : the highs up, the lows down, 6 dB each at 100 */
        gh = 4096 * (100 + tilt) / 100;
        gl = 4096 * 100 / (100 + tilt);
    } else if (tilt < 0) {
        gl = 4096 * (100 - tilt) / 100;
        gh = 4096 * 100 / (100 - tilt);
    }
    if (ts < 8)
        ts = 8;
    dk = 452198 / ts;                                   /* NDEC: falls 60 dB over it (6.9 / samples, Q16) */
    if (dk > 65535)
        dk = 65535;
    {                                                   /* (the state in locals: the compiler can't know the */
        int32_t env = C->env, nlp = C->nlp, h0 = C->held[0], h1 = C->held[1];   /* buffers don't alias it) */
        int32_t lo0 = C->tlo[0], lo1 = C->tlo[1], rng = C->rng;
        int32_t wq = w0 << CTL_LOG2, gq = v0 << CTL_LOG2, dw = w1 - w0, dg = v1 - v0;
        uint32_t hc = C->hcnt;
        for (i = 0; i < n; i++, wq += dw, gq += dg) {
            int32_t x0 = clamp(l[i], -65535, 65535), x1 = clamp(r[i], -65535, 65535), y0, y1, nz = 0;
            int32_t w = wq >> CTL_LOG2, g = gq >> CTL_LOG2;
            if (nois) {                                 /* the envelope opens at once and falls over NDEC */
                int32_t a = x0 < 0 ? -x0 : x0, b = x1 < 0 ? -x1 : x1, e = a > b ? a : b;
                e = e > 32767 ? 32767 << 12 : e << 12;
                if (e > env)
                    env = e;
                else {
                    env -= (env >> 16) * dk;
                    if (env < (1 << 16))
                        env = 0;
                }
                nz = (int32_t)(int16_t)(noise32(&rng) >> 16) * ng >> 15;
                nlp += ((nz - nlp) * ak) >> 7;          /* NTON */
                nz = (nlp * (env >> 12)) >> 16;
            }
            if (++hc >= hold) {                         /* CRSH RATE: a value held for `hold` samples */
                hc = 0;
                h0 = col_drive_bits(x0, g8, sh);
                h1 = col_drive_bits(x1, g8, sh);
            }
            y0 = clamp(h0 + nz, -65535, 65535);
            y1 = clamp(h1 + nz, -65535, 65535);
            if (tilt) {                                 /* the low band (one pole near 700 Hz) and what's above it */
                lo0 += ((y0 - lo0) * 3267) >> 15;
                lo1 += ((y1 - lo1) * 3267) >> 15;
                y0 = (lo0 * gl + (y0 - lo0) * gh) >> 12;
                y1 = (lo1 * gl + (y1 - lo1) * gh) >> 12;
            }
            y0 = x0 + (((y0 - x0) * w) >> 12);          /* WET */
            y1 = x1 + (((y1 - x1) * w) >> 12);
            l[i] = clamp(y0, -131071, 131071) * g >> 10;   /* LVL */
            r[i] = clamp(y1, -131071, 131071) * g >> 10;
        }
        C->env = env;
        C->nlp = nlp;
        C->held[0] = h0;
        C->held[1] = h1;
        C->tlo[0] = lo0;
        C->tlo[1] = lo1;
        C->rng = rng;
        C->hcnt = hc;
    }
}

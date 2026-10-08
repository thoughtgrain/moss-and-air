/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: RESONATOR, the device after GRAIN (docs/bryo-architecture.md, "RESONATOR, as built"): four tuned strings the
 * track's sound rings through, the S-4's RING with the PRD's strings. Its picture (ui_viz.c viz_reso) draws the
 * same mappings this plays.
 *
 * A string is a Karplus-Strong loop: a delay line one period long, a one-pole low-pass in the loop (TONE: 0 dark,
 * 100 bright), and a loop gain set so it rings DEC long (DEC 0: 80 ms .. 100: 20 s, to -60 dB). The four strings sit
 * at the first four partials SCAL keeps above the root: HARM the root's harmonics 1 2 3 4 (each string adds its own
 * overtones on top, so all the harmonics ring), MAJ root, third, fifth, octave; MIN the same with a minor third; PEN
 * root, second, third, fifth. What excites them is the track's sound after GRAIN, through a filter first (CUT RES
 * SLOP: the same state-variable filter as POLY's, low-, band- or high-pass), and WET blends the strings with it. The
 * strings sit across the stereo field, 1 and 3 to the left, 2 and 4 to the right.
 *
 * The root is PTCH, or the last white key played on the RESONATOR page (until PTCH is turned again). On a TAPE track
 * those keys pluck the strings too, a burst of noise one period long (a sound from nothing: a blank tape, RESONATOR
 * and REC); on a SYNTH or POLY track the keys still play the source and the strings follow its notes.
 *
 * Memory: four chunks of the shared memory (mem.c, a string's delay line in each: 1,056 samples, so the lowest
 * string is F1, 43.7 Hz), taken while WET is above 0 on a track in use and given back otherwise.
 *
 * Load: about 15 instructions a string a sample. Shedding (chain_shed) takes strings after grains: one at a time down
 * to two, one back a second later. Audio ISR, except reso_poll (the main loop: the memory). Integer only. */

#define RS_N 4u                      /* strings a track */
#define RS_LEN (sizeof(mem_chunk_t) / 2u)   /* a string's delay line: a chunk as samples (1,056) */
enum { RP_PTCH, RP_DEC, RP_TONE, RP_WET, RP_CUT, RP_RES, RP_SLOP, RP_SCAL };

/* the strings above the root, 1/16 semitones, by SCAL (HARM MAJ MIN PEN): the first four of ui_viz.c's PART_16 */
static const int16_t RS_PART16[4][RS_N] = {{0, 192, 304, 384}, {0, 64, 112, 192}, {0, 48, 112, 192}, {0, 32, 64, 112}};
/* DEC as a ring time to -60 dB, ms, every 10 */
static const uint16_t RS_T60[11] = {80, 140, 250, 440, 780, 1400, 2400, 4300, 7600, 13000, 20000};
/* 2^(-i/16), Q15: a loop gain from its exponent */
static const uint16_t RS_EXP2N[17] = {32767, 31379, 30048, 28774, 27554, 26386, 25268, 24196, 23170,
                                      22188, 21247, 20347, 19484, 18658, 17867, 17110, 16384};

typedef struct {
    uint8_t map[RS_N];               /* each string's chunk (the main loop's) */
    volatile uint8_t nch;
    /* the ISR's */
    uint32_t w;                      /* the write index, shared by the strings */
    int32_t lp[RS_N];                /* each loop's low-pass */
    int32_t ic1, ic2;                /* the filter in front */
    int32_t root16;                  /* a key's root, 1/16 semitone (0: none, PTCH's; a key's is F1 at least) */
    int16_t ptch_seen;               /* PTCH when the key was played (turning PTCH takes over again) */
    int32_t wet;                     /* WET last block (ramped across the next), 0..100 */
    uint32_t keys;
    int32_t rng;
} reso_t;

static reso_t reso[NTRK];
static uint32_t reso_cap = RS_N;     /* strings that sound, lowered by chain_shed */
static uint32_t reso_calm;

/* a string's delay line */
static inline int16_t *rs_line(const reso_t *R, uint32_t k) { return (int16_t *)(void *)mem_at(R->map[k]); }

/* the root now, 1/16 semitone */
static int32_t reso_root16(uint32_t t)
{
    return reso[t].root16 > 0 ? reso[t].root16 : tp[t].dev[DEV_RESO][RP_PTCH] * 16;
}

/* a period, Q8 samples, for pitch n16 (kept inside a string's line) */
static uint32_t rs_period_q8(int32_t n16)
{
    uint32_t inc = pitch_inc((uint32_t)clamp(n16, 29 * 16, 2047)) >> 8, p;
    p = inc ? 0xFFFFFFFFu / inc : (RS_LEN - 2u) << 8;
    return p > (RS_LEN - 2u) << 8 ? (RS_LEN - 2u) << 8 : p;
}

/* the loop gain for a period of p samples to ring t60 ms to -60 dB: 2^-(9.97 p / (44.1 t60)), Q15 */
static int32_t rs_gain(uint32_t p, uint32_t t60)
{
    uint32_t e = p * 14817u / t60, ip = e >> 16, fr = e & 0xFFFFu, i = fr >> 12, r = fr & 0xFFFu;
    int32_t g = RS_EXP2N[i] + (((int32_t)RS_EXP2N[i + 1] - (int32_t)RS_EXP2N[i]) * (int32_t)r >> 12);
    return ip > 14u ? 0 : g >> ip;
}

/* DEC 0..100 as a ring time, ms */
static uint32_t rs_t60(int32_t dec)
{
    int32_t d = clamp(dec, 0, 100), i = d / 10, f = d % 10;
    return i >= 10 ? RS_T60[10] : RS_T60[i] + (uint32_t)((RS_T60[i + 1] - RS_T60[i]) * f / 10);
}

/* pluck: one period of noise into each string's line, behind the write index (softened by TONE's filter) */
static void rs_pluck(reso_t *R, uint32_t nstr, int32_t root16, int32_t a, int32_t sc)
{
    uint32_t k, i;
    if (!R->rng)
        R->rng = 0x51ED27;
    for (k = 0; k < nstr; k++) {
        int16_t *d = rs_line(R, k);
        uint32_t p = rs_period_q8(root16 + RS_PART16[sc][k]) >> 8, at = R->w;
        int32_t y = 0;
        for (i = 0; i < p + 2u; i++) {
            int32_t x = (int32_t)(noise32(&R->rng) >> 17) - 16384;
            y += (a * (x - y)) >> 15;
            at = at ? at - 1u : RS_LEN - 1u;
            d[at] = (int16_t)clamp(d[at] + y, -32767, 32767);
        }
        R->lp[k] = 0;
    }
}

/* audio ISR: track t's RESONATOR on its stereo block, in place. keys: the white keys on the RESONATOR page (0
 * otherwise); pluck: they pluck (a TAPE track) or only retune (a SYNTH or POLY track, the keys playing the source) */
static void reso_block(uint32_t t, int32_t *l, int32_t *r, uint32_t keys, int pluck, uint32_t n)
{
    reso_t *R = &reso[t];
    const int16_t *p = tp[t].dev[DEV_RESO];
    int32_t wet1 = clamp(p[RP_WET], 0, 100), wet0 = R->wet, e[CTL], acc_e[CTL], acc_o[CTL];
    int32_t a = 32767 * (15 + 85 * clamp(p[RP_TONE], 0, 100) / 100) / 100, sc = clamp(p[RP_SCAL], 0, 3), root16;
    uint32_t k, i, press = keys & ~R->keys, nstr = R->nch < reso_cap ? R->nch : reso_cap, t60 = rs_t60(p[RP_DEC]);
    R->keys = keys;
    R->wet = wet1;
    if (p[RP_PTCH] != R->ptch_seen)                     /* PTCH turned: it takes over from a key */
        R->root16 = 0;
    for (k = 0; press && k < NWHITE; k++)                /* a key: the root (the lowest new one) */
        if ((press >> k) & 1u) {
            R->root16 = clamp(12 * ((int32_t)track[t].octave + 1) + (int32_t)k, 29, 108) * 16;
            R->ptch_seen = p[RP_PTCH];
            if (pluck && nstr)
                rs_pluck(R, nstr, R->root16, a, sc);
            break;
        }
    if (!nstr || (!wet0 && !wet1))                       /* off: the track as it was */
        return;
    root16 = reso_root16(t);
    {   /* what excites the strings: the track, mono, through the filter in front (when it does anything) */
        int32_t mode = clamp(p[RP_SLOP], 0, 2);
        tsvf_t flt;
        int filt = p[RP_CUT] < 127 || p[RP_RES] || mode;
        if (filt)
            tsvf_coef(&flt, p[RP_CUT] << 8, p[RP_RES] * 127 / 100);
        for (i = 0; i < n; i++) {
            int32_t x = (l[i] + r[i]) >> 1;
            e[i] = filt ? tsvf_mode(&flt, x >> 1, &R->ic1, &R->ic2, (uint32_t)mode) << 1 : x;
            acc_e[i] = acc_o[i] = 0;
        }
    }
    {   /* the input's share, by the root string's ring: less as the ring gets longer, so a long DEC rings louder but
         * not without bound (the loop clamps besides) */
        int32_t in = clamp((32768 - rs_gain(rs_period_q8(root16) >> 8, t60)) * 16, 4096, 16384);
        for (i = 0; i < n; i++)
            e[i] = (e[i] * in) >> 15;
    }
    for (k = 0; k < nstr; k++) {                         /* each string: read a period back, damp, ring, write */
        int16_t *d = rs_line(R, k);
        uint32_t pq8 = rs_period_q8(root16 + RS_PART16[sc][k]), comp = ((uint32_t)(32768 - a) << 8) / (uint32_t)a;
        uint32_t dq8 = pq8 > comp + 512u ? pq8 - comp : 512u, di = dq8 >> 8, df = dq8 & 255u, w = R->w;
        int32_t g = rs_gain(pq8 >> 8, t60), lp = R->lp[k], *acc = k & 1u ? acc_o : acc_e;
        uint32_t rd = w >= di ? w - di : w + RS_LEN - di;
        for (i = 0; i < n; i++) {
            uint32_t rp = rd ? rd - 1u : RS_LEN - 1u;
            int32_t x = d[rd] + (((d[rp] - d[rd]) * (int32_t)df) >> 8), y, z;
            lp += (a * (x - lp)) >> 15;
            y = (lp * g) >> 15;
            z = y + e[i];
            d[w] = (int16_t)(z > 32767 ? 32767 : z < -32767 ? -32767 : z);
            acc[i] += y;
            if (++w == RS_LEN)
                w = 0;
            if (++rd == RS_LEN)
                rd = 0;
        }
        R->lp[k] = lp;
    }
    R->w = (R->w + n) % RS_LEN;
    {   /* WET: the strings against the track, ramped across the block (Q15: no division a sample) */
        int32_t w0 = wet0 * 32767 / 100, dw = ((wet1 - wet0) * 32767 / 100) >> CTL_LOG2;
        for (i = 0; i < n; i++, w0 += dw) {
            int32_t wl = soft_knee(((acc_e[i] * 3) >> 2) + ((acc_o[i] * 3) >> 3), 24000);   /* (strings 1 and 3 2 : 1
                                                                                             * left, 2 and 4 right) */
            int32_t wr = soft_knee(((acc_o[i] * 3) >> 2) + ((acc_e[i] * 3) >> 3), 24000);
            l[i] += (int32_t)(((wl - l[i]) >> 1) * (w0 >> 1) >> 13);   /* (halves: |a - b| < 2^17, w0 < 2^15) */
            r[i] += (int32_t)(((wr - r[i]) >> 1) * (w0 >> 1) >> 13);
        }
    }
}

/* audio ISR: shedding takes strings after grains (one at a time, down to two); a second without it gives one back */
static void reso_shed(void)
{
    if (reso_cap > 2u)
        reso_cap--;
    reso_calm = 0;
}
static void reso_recover(void)
{
    if (++reso_calm >= 1378u && reso_cap < RS_N) {
        reso_cap++;
        reso_calm = 0;
    }
}

/* main loop, every pass: a track's strings while its RESONATOR is on (WET above 0, the track in use), taken from the
 * shared memory (from the end of the longest tape when nothing is free); none otherwise */
static void reso_poll(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        reso_t *R = &reso[t];
        int on = t < gr_ntrk() && tp[t].dev[DEV_RESO][RP_WET] > 0;
        while (on && R->nch < RS_N) {
            int32_t c = tape_alloc(MEM_RESO + t, NTRK, 1);
            if (c < 0)
                break;
            R->map[R->nch] = (uint8_t)c;
            RING_PUBLISH();
            R->nch++;
        }
        while (!on && R->nch) {
            uint32_t c = R->map[R->nch - 1u];
            R->nch--;
            mem_free(c);
        }
    }
}

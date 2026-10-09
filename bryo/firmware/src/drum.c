/* SPDX-License-Identifier: GPL-3.0-only */
/* DRUM: a drum machine as a track's source. Sixteen synthesized instruments on the sixteen white keys, played from
 * a pattern of 2..4 bars of sixteenth steps. The kit is CR-78-inspired: its list of instruments and its soft,
 * round character. Every sound is Bryo's own, a patch for drum_voice.c's synthesizer voiced by ear; the rhythms
 * are Bryo's own too.
 *
 * The kit (DRM_KIT): one patch per key, left to right. A hi-hat (closed) and the metal beat cut the open hat
 * short, the way one pedal would. */

#define DRM_NINST 16

enum {
    DI_BD, DI_SD, DI_CP, DI_RS, DI_LC, DI_LB, DI_HB, DI_CL,
    DI_CB, DI_MA, DI_TB, DI_GU, DI_HH, DI_OH, DI_MB, DI_CY
};

/* code, TONE: p16 ratio mix2 bend bend_ms body_ms tone_lv,
 *       NOISE: filt cut q16 mset mp16 metal rise10 hit_ms hit_lv bursts gap10 late tail_ms tail_lv,
 *       drive gain chokes */
static const dk_patch_t DRM_KIT[DRM_NINST] = {
    /* bass drum: a 58 Hz sine that drops 2.5 semitones in its first few ms, a soft 2nd harmonic, a felt thud */
    {"BD", 543, 8192, 22, 40, 6, 110, 127,
     DK_LOW, 1331, 11, DK_M_NONE, 0, 0, 0, 2, 25, 0, 0, 0, 2, 0, 30, 3779, 0},
    /* snare: two shell tones (190 Hz, x1.72) under a band of noise at 4.5 kHz, a quick snap on top */
    {"SD", 871, 7045, 70, 16, 5, 45, 75,
     DK_BAND, 1748, 11, DK_M_NONE, 0, 0, 0, 4, 70, 0, 0, 0, 95, 110, 10, 2644, 0},
    /* clap: four hands 9 ms apart through a band at 1.2 kHz, then the room */
    {"CP", 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1380, 19, DK_M_NONE, 0, 0, 0, 5, 127, 3, 90, 1, 70, 55, 0, 3216, 0},
    /* rim shot: 480 Hz and an inharmonic partial, very short, driven hard */
    {"RS", 1128, 10732, 90, 0, 0, 7, 127,
     DK_HIGH, 1777, 11, DK_M_NONE, 0, 0, 0, 2, 30, 0, 0, 0, 2, 0, 80, 6643, 0},
    /* low conga: 205 Hz, a 1.5-semitone fall, a slap of bright noise */
    {"LC", 892, 0, 0, 24, 20, 170, 127,
     DK_HIGH, 1585, 11, DK_M_NONE, 0, 0, 0, 3, 25, 0, 0, 0, 2, 0, 20, 3143, 0},
    /* low bongo: 330 Hz */
    {"LB", 1024, 0, 0, 24, 15, 120, 127,
     DK_HIGH, 1636, 11, DK_M_NONE, 0, 0, 0, 3, 25, 0, 0, 0, 2, 0, 20, 3143, 0},
    /* high bongo: 470 Hz */
    {"HB", 1122, 0, 0, 20, 12, 90, 127,
     DK_HIGH, 1636, 11, DK_M_NONE, 0, 0, 0, 3, 25, 0, 0, 0, 2, 0, 20, 3143, 0},
    /* claves: 2.5 kHz, a click of wood */
    {"CL", 1585, 0, 0, 0, 0, 10, 127,
     DK_OFF, 0, 0, DK_M_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 3001, 0},
    /* cowbell: two squares (560 Hz, x1.49) through a band at 1.3 kHz, a struck edge and a ring */
    {"CB", 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1404, 24, DK_M_PAIR, 1171, 127, 0, 8, 80, 0, 0, 0, 110, 70, 0, 1522, 0},
    /* maracas: noise in a wide band at 7 kHz that swells in over 2 ms and falls away */
    {"MA", 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1870, 10, DK_M_NONE, 0, 0, 20, 1, 0, 0, 0, 0, 35, 127, 0, 3329, 0},
    /* tambourine: noise and a little metal at 8.5 kHz, the strike and two jingles 16 ms apart */
    {"TB", 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1924, 13, DK_M_CLUSTER, 1236, 50, 5, 9, 90, 2, 160, 0, 140, 70, 0, 3446, 0},
    /* guiro: 23 teeth 4.2 ms apart through a narrow band at 2.4 kHz */
    {"GU", 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1574, 20, DK_M_NONE, 0, 0, 0, 2, 127, 22, 42, 0, 2, 0, 0, 3002, 0},
    /* hi-hat: noise and a little metal above 7 kHz, short */
    {"HH", 0, 0, 0, 0, 0, 0, 0,
     DK_HIGH, 1870, 11, DK_M_CLUSTER, 1236, 40, 3, 0, 0, 0, 0, 0, 28, 127, 0, 2330, 1u << DI_OH},
    /* open hi-hat: the same above 6 kHz, long */
    {"OH", 0, 0, 0, 0, 0, 0, 0,
     DK_HIGH, 1828, 11, DK_M_CLUSTER, 1236, 40, 3, 0, 0, 0, 0, 0, 260, 110, 0, 2469, 0},
    /* metal beat: mostly metal, through a band at 9 kHz, a hard short tick */
    {"MB", 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1940, 16, DK_M_CLUSTER, 1300, 110, 2, 3, 70, 0, 0, 0, 18, 110, 20, 4924, 1u << DI_OH},
    /* cymbal: noise and metal in a wide band at 6 kHz, a splash and a long wash */
    {"CY", 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1828, 10, DK_M_CLUSTER, 1180, 45, 0, 12, 70, 0, 0, 0, 900, 100, 0, 3143, 0},
};

/* the knobs as designed: no offsets, every decay as written, LEVEL 100 */
static void drm_knobs_default(dk_knobs_t *k)
{
    k->tune = 0;
    k->dscale = 256;
    k->bright = 0;
    k->level = 100;
    k->accent = 0;
    k->drive = 0;
}

/* ------------------------------------------------------------------- the written rhythms --- */
/* PATN's rhythms, Bryo's own arrangements of the styles a preset rhythm box offers: a row per instrument, its code
 * then the steps, a bar of sixteen per group ('|' between bars): 'x' a hit, 'X' a hit on an accented step (the
 * accent is the step's, for every instrument on it), '.' a rest */
typedef struct {
    uint8_t len, swing;
    const char *rows[6];
} drm_preset_t;
static const drm_preset_t DRM_PRESET[DRM_NPRESET] = {
    {2, 0, {"BD x.....x.x.......|x.....x.x..x....", "SD ....X.......X...|....X.......X...",   /* ROCK */
            "HH x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x...", "OH ................|..............x."}},
    {2, 0, {"BD x...x...x...x...|x...x...x...x...", "CP ....X.......X...|....X.......X...",   /* DISC */
            "HH x...x...x...x...|x...x...x...x...", "OH ..x...x...x...x.|..x...x...x...x."}},
    {2, 15, {"BD x..x..x...x.....|x..x......x..x..", "SD ....X..x.x..X...|....X..x....X..x",  /* FUNK */
             "HH xxxxxxxxxxxxxxxx|xxxxxxxxxxxxxx.x", "OH ................|..............x."}},
    {2, 60, {"BD x.....x.x.......|x.....x.x.....x.", "SD ....X.......X...|....X.......X...",  /* SHFL */
             "HH x..xx..xx..xx..x|x..xx..xx..xx..x"}},
    {2, 0, {"BD x..xx..xx..xx..x|x..xx..xx..xx..x", "RS x..x..x...x..x..|..x..x..x..x....",   /* BOSA */
            "HH x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x.", "MA ..x...x...x...x.|..x...x...x...x."}},
    {2, 0, {"BD x..x....x..x....|x..x....x..x....", "CL x..x...x..x.x...|x..x...x..x.x...",   /* RMBA */
            "LC ......x.......x.|......x.......x.", "HB ...x.x.....x.x..|...x.x.....x.x..",
            "MA x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x."}},
    {2, 0, {"BD x.......x.......|x.......x.......", "CB x...x...x...x...|x...x...x...x...",   /* CHA */
            "GU x.......x.x.x...|x.......x.x.x...", "LC ......x.......x.|......x.......x.",
            "HB ..x.......x.....|..x.......x.x..."}},
    {2, 0, {"BD x.....x.x.......|x.....x.x.......", "LB x..x..x.........|x..x..x...x.....",   /* BGIN */
            "LC ........x.x.x...|........x.x.x...", "MA x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x.",
            "CL ......x.......x.|......x.......x."}},
    {2, 0, {"BD x..xx.x.x..xx.x.|x..xx.x.x.......", "RS ....x.......x...|....x.......x...",   /* TNGO */
            "SD ................|............xxxX", "MA x.x.x.x.x.x.x.x.|x.x.x.x.x.x.x.x."}},
    {2, 0, {"BD x.......x.......|x.......x.......", "SD X.x.x.x.x.xxx.x.|x.x.x.x.x.x.xxxX",   /* MRCH */
            "CY x...............|................"}},
};

/* main loop: what SAVE held brings back after POLY's clear or a PATN that wrote over an edited pattern */
static struct {
    uint16_t pat[DRM_NBAR][16];
    uint16_t acc[DRM_NBAR];
    int16_t len, swing;
    uint8_t trk, valid;
} drm_undo_buf;
static uint8_t drm_dirty[NTRK];      /* main loop: the pattern was edited since PATN last wrote it */

/* main loop: rhythm n written over track t's pattern (with its length and swing) */
static void drm_load(uint32_t t, uint32_t n)
{
    const drm_preset_t *r = &DRM_PRESET[n < DRM_NPRESET ? n : 0u];
    uint32_t b, s, k, i;
    for (b = 0; b < DRM_NBAR; b++) {
        tp[t].dacc[b] = 0;
        for (s = 0; s < 16u; s++)
            tp[t].dpat[b][s] = 0;
    }
    for (k = 0; k < NELEM(r->rows) && r->rows[k]; k++) {
        const char *q = r->rows[k];
        for (i = 0; i < DRM_NINST && (DRM_KIT[i].code[0] != q[0] || DRM_KIT[i].code[1] != q[1]); i++)
            ;
        for (q += 3, s = 0; *q && s < 16u * DRM_NBAR && i < DRM_NINST; q++) {
            if (*q == '|')
                continue;
            if (*q == 'x' || *q == 'X')
                tp[t].dpat[s >> 4][s & 15u] |= (uint16_t)(1u << i);
            if (*q == 'X')
                tp[t].dacc[s >> 4] |= (uint16_t)(1u << (s & 15u));
            s++;
        }
    }
    tp[t].drm[DM_LEN] = r->len;
    tp[t].drm[DM_SWNG] = r->swing;
    if (tp[t].drm[DM_BAR] > r->len)                      /* (the bar STEP shows stays in the pattern) */
        tp[t].drm[DM_BAR] = r->len;
    drm_dirty[t] = 0;
}

/* param.c: a track's DRUM as it starts: the first rhythm, every instrument as designed */
static void drm_defaults(uint32_t t)
{
    uint32_t i, k;
    for (i = 0; i < DRM_NINST; i++)
        for (k = 0; k < NDIN; k++)
            tp[t].dins[i][k] = 0;
    drm_load(t, 0);
}

/* main loop: track t's pattern into the undo buffer */
static void drm_keep(uint32_t t)
{
    uint32_t b, s;
    for (b = 0; b < DRM_NBAR; b++) {
        drm_undo_buf.acc[b] = tp[t].dacc[b];
        for (s = 0; s < 16u; s++)
            drm_undo_buf.pat[b][s] = tp[t].dpat[b][s];
    }
    drm_undo_buf.len = tp[t].drm[DM_LEN];
    drm_undo_buf.swing = tp[t].drm[DM_SWNG];
    drm_undo_buf.trk = (uint8_t)t;
    drm_undo_buf.valid = 1;
}

/* main loop: PATN turned to n: the rhythm written (an edited pattern kept for SAVE's undo first) */
static void drm_patn(uint32_t t, uint32_t n)
{
    if (drm_dirty[t])
        drm_keep(t);
    drm_load(t, n);
}

/* main loop: POLY held on a DRUM track: every step of its pattern cleared (kept for SAVE's undo). 0: it was empty */
static int drm_clear(uint32_t t)
{
    uint32_t b, s, any = 0;
    for (b = 0; b < DRM_NBAR; b++)
        for (s = 0; s < 16u; s++)
            any |= tp[t].dpat[b][s];
    if (!any)
        return 0;
    drm_keep(t);
    for (b = 0; b < DRM_NBAR; b++) {
        tp[t].dacc[b] = 0;
        for (s = 0; s < 16u; s++)
            tp[t].dpat[b][s] = 0;
    }
    drm_dirty[t] = 1;
    return 1;
}

/* main loop: SAVE held: the pattern before the last clear or PATN. -1: nothing to undo */
static int drm_undo(void)
{
    uint32_t b, s, t = drm_undo_buf.trk;
    if (!drm_undo_buf.valid || t >= NTRK)
        return -1;
    for (b = 0; b < DRM_NBAR; b++) {
        tp[t].dacc[b] = drm_undo_buf.acc[b];
        for (s = 0; s < 16u; s++)
            tp[t].dpat[b][s] = drm_undo_buf.pat[b][s];
    }
    tp[t].drm[DM_LEN] = drm_undo_buf.len;
    tp[t].drm[DM_SWNG] = drm_undo_buf.swing;
    if (tp[t].drm[DM_BAR] > drm_undo_buf.len)
        tp[t].drm[DM_BAR] = drm_undo_buf.len;
    drm_undo_buf.valid = 0;
    drm_dirty[t] = 1;
    return 0;
}

/* main loop: STEP's HITS / ACC: white key k toggles step k of the bar shown (the instrument INST's hit, or the
 * step's accent) */
static void drm_toggle(uint32_t t, uint32_t k)
{
    const int16_t *p = tp[t].drm;
    uint32_t b = (uint32_t)clamp(p[DM_BAR], 1, (int32_t)DRM_NBAR) - 1u, i = (uint32_t)clamp(p[DM_INST], 0, 15);
    if (k >= 16u)
        return;
    if (p[DM_MODE] == DMODE_ACC)
        tp[t].dacc[b] ^= (uint16_t)(1u << k);
    else
        tp[t].dpat[b][k] ^= (uint16_t)(1u << i);
    drm_dirty[t] = 1;
}

/* ------------------------------------------------------------------------- SEED --- */
/* How a version varies from the pattern. SEED picks the version (0: the pattern as written), and everything a
 * version does comes from a hash of the version, the bar, the step and the instrument: the same SEED always plays
 * the same thing, and turning SEED by one gives a different but related version. It isn't noise sprinkled on the
 * grid; each instrument varies the way a player would vary that part:
 *   the backbone (BD, SD, CP)  never dropped. The bass drum picks up a note on the weak sixteenth before one of its
 *              own hits; the snare adds quiet ghost notes on the sixteenths of a beat it already plays in
 *   the time (HH, OH, MA, TB)  thins on the weak sixteenths and fills in on the eighths, at a lower level, and only
 *              where that instrument already plays in that bar; a closed hat on the last eighth may open
 *   the colour (RS, the congas, CL, CB, GU, MB)  ornaments next to its own hits; a conga or bongo hit can move to
 *              the other drums of the family the pattern uses (the melody of the part changes, not its rhythm)
 * VARY sets how far: the first bar strays least and the last bar most, so the loop still reads as its pattern. The
 * last bar can end in a FILL (its chance): a roll (on the snare, else the clap, the rim, the claves or a bongo),
 * a run down the congas, or a stutter of the bass drum against the roll's instrument, picked from what the pattern
 * uses; then a crash on the next downbeat (the cymbal, else an open hat, if the pattern has one). Only instruments
 * the pattern uses come in (the one exception: a closed hat may open), so a rock beat never sprouts a cowbell and a
 * bossa never a snare. EVOL moves on to the next version every 1, 2, 4 or 8 loops. */
enum { RL_BACK, RL_TIME, RL_COLOR, RL_CRASH };
static const uint8_t DRM_ROLE[DRM_NINST] = {
    RL_BACK, RL_BACK, RL_BACK, RL_COLOR, RL_COLOR, RL_COLOR, RL_COLOR, RL_COLOR,
    RL_COLOR, RL_TIME, RL_TIME, RL_COLOR, RL_TIME, RL_TIME, RL_COLOR, RL_CRASH};
#define DRM_CONGAS ((1u << DI_LC) | (1u << DI_LB) | (1u << DI_HB))

typedef struct {
    uint16_t hit;                    /* the instruments that hit */
    uint16_t soft;                   /* .. of them, the quiet ones (ghost notes, fill-ins): -7 dB */
    uint8_t acc;                     /* the step is accented */
} drm_hits_t;

static uint32_t drm_hash(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    return h ^ (h >> 15);
}
/* a roll of 0..99 for version v at (bar, step, instrument), for decision w */
static uint32_t drm_roll(uint32_t v, uint32_t bar, uint32_t s, uint32_t i, uint32_t w)
{
    return drm_hash(v, bar << 12 | s << 8 | i << 4 | w) % 100u;
}
/* a step's place in the bar: 0 the downbeat, 1 a beat, 2 an eighth, 3 a sixteenth */
static uint32_t drm_level(uint32_t s) { return !s ? 0u : !(s & 3u) ? 1u : !(s & 1u) ? 2u : 3u; }

/* the version that plays on pass `pass` (0: as written) */
static uint32_t drm_version(const int16_t *p, uint32_t pass)
{
    static const uint8_t EVERY[5] = {0, 1, 2, 4, 8};
    uint32_t e = EVERY[clamp(p[DM_EVOL], 0, 4)];
    if (p[DM_SEED] <= 0)
        return 0;
    return (uint32_t)p[DM_SEED] + (e ? pass / e : 0u);
}

/* what step s of bar `bar` plays in version v of track t's pattern (knobs p) */
static void drm_step(uint32_t t, const int16_t *p, uint32_t v, uint32_t bar, uint32_t s, drm_hits_t *o)
{
    const track_params_t *P = &tp[t];
    uint32_t len = (uint32_t)clamp(p[DM_LEN], 2, (int32_t)DRM_NBAR), used = 0, inbar = 0, b, k, i, lv = drm_level(s);
    uint32_t w = P->dpat[bar][s], pv, fill, fs, last = bar + 1u == len;
    o->hit = (uint16_t)w;
    o->soft = 0;
    o->acc = (uint8_t)((P->dacc[bar] >> s) & 1u);
    if (!v)
        return;
    for (b = 0; b < len; b++)
        for (k = 0; k < 16u; k++)
            used |= P->dpat[b][k];
    for (k = 0; k < 16u; k++)
        inbar |= P->dpat[bar][k];
    pv = (uint32_t)clamp(p[DM_VARY], 0, 100) * (6u + 4u * bar / (len - 1u)) / 10u;   /* first bar 60 %, last 100 % */
    fill = drm_roll(v, 0, 0, 0, 15) < (uint32_t)clamp(p[DM_FILL], 0, 100);
    fs = fill && drm_roll(v, 0, 0, 1, 15) < (uint32_t)p[DM_FILL] / 3u ? 8u : 12u;   /* (a big fill: two beats) */
    {                                                    /* the fill's instrument: what the pattern can roll on */
        static const uint8_t ROLL[5] = {DI_SD, DI_CP, DI_RS, DI_CL, DI_LB};
        uint32_t roll = DRM_NINST, style = drm_hash(v, 77u) % 3u;
        for (k = 0; k < 5u && roll == DRM_NINST; k++)
            if ((used >> ROLL[k]) & 1u)
                roll = ROLL[k];
        if (style == 1u && !(used & DRM_CONGAS))         /* (a run needs congas, a stutter a bass drum) */
            style = 0;
        if (style == 2u && !(used & (1u << DI_BD)))
            style = 0;
        if (style != 1u && roll == DRM_NINST)            /* (nothing to roll on: no fill) */
            fill = 0;
        if (fill && last && s >= fs) {                   /* the fill */
            uint32_t n = s - fs, span = 16u - fs;
            o->hit = (uint16_t)(w & (1u << DI_BD) & (lv <= 1u ? 0xFFFFu : 0u));   /* (the beats' bass drum stays) */
            if (style == 1u) {                           /* down the pattern's congas: high to low */
                static const uint8_t RUN[3] = {DI_HB, DI_LB, DI_LC};
                uint8_t has[3];
                uint32_t nh = 0;
                for (k = 0; k < 3u; k++)
                    if ((used >> RUN[k]) & 1u)
                        has[nh++] = RUN[k];
                if (n % 4u != 3u || drm_roll(v, bar, s, 0, 9) < 50u)
                    o->hit |= (uint16_t)(1u << has[n * nh / span]);
            } else if (style == 2u) {                    /* a stutter: the bass drum and the roll's instrument */
                o->hit |= (uint16_t)(1u << (s & 1u ? roll : DI_BD));
            } else {                                     /* a roll, rising */
                o->hit |= (uint16_t)(1u << roll);
                if (n < span / 2u)
                    o->soft |= (uint16_t)(1u << roll);
            }
            o->acc = (uint8_t)(s == 15u);
            return;
        }
    }
    if (fill && bar == 0u && s == 0u && (used & (1u << DI_CY | 1u << DI_OH))) {   /* the crash after a fill */
        o->hit |= (uint16_t)(1u << (used & (1u << DI_CY) ? DI_CY : DI_OH));
        o->acc = 1;
    }
    for (i = 0; i < DRM_NINST; i++) {
        uint32_t bit = 1u << i, r = drm_roll(v, bar, s, i, 0);
        if (!(used & bit))
            continue;
        if (w & bit) {                                   /* written: kept, thinned, moved */
            if (DRM_ROLE[i] == RL_TIME && ((lv == 3u && r < pv / 3u) || (lv == 2u && r < pv / 8u)))
                o->hit &= (uint16_t)~bit;
            else if (DRM_ROLE[i] == RL_COLOR && lv >= 2u && r < pv / 6u)
                o->hit &= (uint16_t)~bit;
            if (i == DI_HH && s == 14u && drm_roll(v, bar, s, i, 1) < pv / 2u) {   /* the hat opens */
                o->hit &= (uint16_t)~bit;
                o->hit |= (uint16_t)(1u << DI_OH);
            }
            if ((bit & DRM_CONGAS) && (o->hit & bit) && drm_roll(v, bar, s, i, 2) < pv / 5u) {   /* another drum */
                uint32_t j = i, n;
                for (n = 0; n < 3u; n++) {
                    j = j == DI_HB ? DI_LC : j + 1u;
                    if ((used & (1u << j)) || n == 2u)
                        break;
                }
                o->hit = (uint16_t)((o->hit & ~bit) | (1u << j));
            }
            continue;
        }
        switch (DRM_ROLE[i]) {                           /* not written: what may come in */
        case RL_BACK:
            if (i == DI_BD && lv >= 2u && r < pv / 4u &&
                ((s + 1u < 16u && (P->dpat[bar][s + 1u] & bit)) || (s + 2u < 16u && (P->dpat[bar][s + 2u] & bit)))) {
                o->hit |= (uint16_t)bit;                 /* a pickup into its own next hit */
                if (lv == 3u)
                    o->soft |= (uint16_t)bit;
            } else if (i == DI_SD && lv == 3u && r < pv / 3u &&
                       ((P->dpat[bar][s & ~3u] | P->dpat[bar][s | 3u] | P->dpat[bar][(s & ~3u) + 2u]) & bit)) {
                o->hit |= (uint16_t)bit;                 /* a ghost note in a beat it plays */
                o->soft |= (uint16_t)bit;
            }
            break;
        case RL_TIME:                                    /* (the open hat only opens: it never fills in) */
            if (i != DI_OH && (inbar & bit) && ((lv == 2u && r < pv / 3u) || (lv == 3u && r < pv / 5u))) {
                o->hit |= (uint16_t)bit;
                o->soft |= (uint16_t)bit;
            }
            break;
        case RL_COLOR:
            if ((inbar & bit) && lv >= 2u && r < pv / 4u &&
                ((s && (P->dpat[bar][s - 1u] & bit)) || (s + 1u < 16u && (P->dpat[bar][s + 1u] & bit)))) {
                o->hit |= (uint16_t)bit;                 /* an ornament beside its own hit */
                if (lv == 3u)
                    o->soft |= (uint16_t)bit;
            }
            break;
        default:
            break;
        }
    }
    if ((o->hit & (1u << DI_HH)) && (o->hit & (1u << DI_OH)))   /* (the closed hat would choke the open one) */
        o->hit &= (uint16_t)~(1u << DI_HH);
    o->soft &= o->hit;
}

/* ------------------------------------------------------------------------ the player --- */
#define DRM_NV 5u                    /* voices per track: a voice per instrument while it rings, the oldest taken */
#define DRM_STEP (1u << 14)          /* a sixteenth, in beats Q16 */
typedef struct {                     /* audio ISR only */
    dk_coef_t c[DRM_NV];
    dk_voice_t v[DRM_NV];
    uint32_t age[DRM_NV];
    uint8_t inst[DRM_NV];
    uint8_t running;                 /* the transport was running last block */
    uint8_t skip_s;                  /* LIVE: a step written ahead of the playhead (it sounded at the press) */
    uint16_t skip;                   /* .. its instruments, not struck again when the step comes */
    uint32_t keys, last, n;          /* the keys last block; the last step struck (sixteenths since PLAY); ages */
} drm_rt_t;
static drm_rt_t drm_rt[NTRK] __attribute__((section(".pool")));   /* (3.7 KB, in the pool) */
static volatile uint8_t drm_now[NTRK];   /* ISR -> UI: the step playing in the pattern (0xFF: stopped) */
static volatile uint16_t drm_pass[NTRK]; /* ISR -> UI: the passes of the pattern since PLAY (EVOL's version) */
static volatile uint8_t drm_aud[NTRK];   /* UI -> ISR: an instrument to strike (+1; 0: none; HOME + key) */

/* ISR -> main loop: LIVE's and ERAS's writes (the main loop owns the pattern), a ring of 32 */
typedef struct {
    uint8_t t, bs, i;                /* track; bar << 4 | step; instrument, | 0x80: cleared */
} drm_rq_t;
static drm_rq_t drm_rq[32];
static volatile uint8_t drm_rq_w, drm_rq_r;

static void drm_post(uint32_t t, uint32_t bar, uint32_t s, uint32_t i)
{
    uint32_t w = drm_rq_w, nx = (w + 1u) & 31u;
    if (nx == drm_rq_r)                                  /* (full: dropped; 32 a millisecond isn't playable) */
        return;
    drm_rq[w].t = (uint8_t)t;
    drm_rq[w].bs = (uint8_t)(bar << 4 | s);
    drm_rq[w].i = (uint8_t)i;
    RING_PUBLISH();
    drm_rq_w = (uint8_t)nx;
}

/* main loop, every pass: LIVE's and ERAS's writes into the patterns */
static void drm_poll(void)
{
    uint32_t r = drm_rq_r;
    while (r != drm_rq_w) {
        const drm_rq_t *q = &drm_rq[r];
        uint16_t *st = &tp[q->t % NTRK].dpat[(q->bs >> 4) % DRM_NBAR][q->bs & 15u];
        uint16_t bit = (uint16_t)(1u << (q->i & 15u));
        *st = (uint16_t)(q->i & 0x80u ? *st & ~bit : *st | bit);
        drm_dirty[q->t % NTRK] = 1;
        r = (r + 1u) & 31u;
        RING_PUBLISH();
        drm_rq_r = (uint8_t)r;
    }
}

/* DECY (-150..150 %) as a decay scale, Q8: x2 every 50 (straight between) */
static uint32_t drm_scale(int32_t d)
{
    uint32_t s = 256;
    d = clamp(d, -150, 150);
    for (; d >= 50; d -= 50)
        s <<= 1;
    for (; d < 0; d += 50)
        s >>= 1;
    return s + s * (uint32_t)d / 50u;
}

/* audio ISR: instrument i struck on track t (knobs p), accented, quiet (a ghost note) */
static void drm_strike(uint32_t t, const int16_t *p, uint32_t i, uint32_t acc, uint32_t soft)
{
    drm_rt_t *r = &drm_rt[t];
    const int16_t *in = tp[t].dins[i];
    dk_knobs_t k;
    uint32_t j, best = 0;
    for (j = 0; j < DRM_NV; j++)                        /* what this one chokes */
        if (r->v[j].live && ((DRM_KIT[i].chokes >> r->inst[j]) & 1u))
            dk_choke(&r->v[j]);
    for (j = 0; j < DRM_NV && !(r->inst[j] == i && r->v[j].live); j++)   /* its own voice, still ringing */
        ;
    if (j == DRM_NV)
        for (j = 0; j < DRM_NV && (r->v[j].live || r->v[j].trig); j++)   /* a quiet one */
            ;
    if (j == DRM_NV)                                     /* the oldest */
        for (j = 1; j < DRM_NV; j++)
            if (r->age[j] - r->age[best] > 0x80000000u)
                best = j;
    j = j == DRM_NV ? best : j;
    k.tune = (int16_t)(clamp(p[DM_TUNE] + in[DIN_TUNE], -24, 24) * 16);
    k.dscale = (uint16_t)drm_scale(p[DM_DECY] + in[DIN_DECY]);
    k.bright = (int16_t)clamp((p[DM_TONE] + in[DIN_TONE]) * 127 / 100, -127, 127);
    k.level = (uint16_t)(db_q10(clamp(in[DIN_LVL], -24, 6)) * (soft ? 45 : 100) / 1024);
    k.accent = (uint8_t)(acc ? clamp(p[DM_ACNT], 0, 100) * 127 / 100 : 0);
    k.drive = (uint8_t)(clamp(p[DM_DRV], 0, 100) * 127 / 100);
    dk_setup(&r->c[j], &DRM_KIT[i], &k);
    dk_trigger(&r->v[j]);
    r->inst[j] = (uint8_t)i;
    r->age[j] = ++r->n;
}

/* audio ISR: one block of track t's DRUM into out. keys: the white keys held (they play the kit; on STEP in LIVE
 * they write, in ERAS they wipe); rec: unused (REC prints DRUM onto the tape, chain.c) */
static void drm_block(uint32_t t, uint32_t keys, const int32_t *rec, int32_t *out, uint32_t n)
{
    drm_rt_t *r = &drm_rt[t];
    const int16_t *p = TPD(t, MA_DRM);
    uint32_t press = keys & ~r->keys, j, i, len = (uint32_t)clamp(p[DM_LEN], 2, (int32_t)DRM_NBAR), pl = 16u * len;
    uint32_t mode = sys.drum_mode, a = drm_aud[t];
    (void)rec;
    r->keys = keys;
    if (a) {                                             /* HOME + a key: that instrument, heard */
        drm_aud[t] = 0;
        drm_strike(t, p, (a - 1u) & 15u, 0, 0);
    }
    if (mode == DMODE_ERAS)                              /* (held to wipe, not to play) */
        press = 0;
    if (sys.playing) {
        uint32_t beat = mclk.beat, sw = (uint32_t)clamp(p[DM_SWNG], 0, 100) * DRM_STEP / 300u;   /* up to 1/3 step */
        uint32_t nn = beat >> 14, in = (nn & 1u) << 14 | (beat & (DRM_STEP - 1u));   /* where in its pair of steps */
        uint32_t c = in < DRM_STEP + sw ? nn & ~1u : nn | 1u, s = c % pl, bar = s >> 4;
        if (mclk.started || !r->running) {
            r->last = 0xFFFFFFFFu;
            r->skip = 0;
        }
        r->running = 1;
        if (press && mode == DMODE_LIVE) {               /* LIVE: written at the nearest step */
            uint32_t late = c & 1u ? in - DRM_STEP - sw > (DRM_STEP - sw) / 2u : in > (DRM_STEP + sw) / 2u;
            uint32_t ws = late ? (s + 1u) % pl : s;
            for (i = 0; i < 16u; i++)
                if ((press >> i) & 1u)
                    drm_post(t, ws >> 4, ws & 15u, i);
            if (late) {                                  /* (it sounds now: not again when the step comes) */
                if (r->skip_s != ws)
                    r->skip = 0;
                r->skip_s = (uint8_t)ws;
                r->skip |= (uint16_t)press;
            }
        }
        if (c != r->last) {                              /* a new step */
            drm_hits_t h;
            uint32_t pass = c / pl;
            r->last = c;
            drm_now[t] = (uint8_t)s;
            drm_pass[t] = (uint16_t)pass;
            drm_step(t, p, drm_version(p, pass), bar, s & 15u, &h);
            if (r->skip && r->skip_s == s) {
                h.hit &= (uint16_t)~r->skip;
                r->skip = 0;
            }
            if (mode == DMODE_ERAS && keys) {            /* ERAS: the held instruments wiped as the step passes */
                for (i = 0; i < 16u; i++)
                    if (((keys >> i) & 1u) && ((tp[t].dpat[bar][s & 15u] >> i) & 1u))
                        drm_post(t, bar, s & 15u, i | 0x80u);
                h.hit &= (uint16_t)~keys;
            }
            for (i = 0; h.hit >> i; i++)
                if ((h.hit >> i) & 1u)
                    drm_strike(t, p, i, h.acc, (h.soft >> i) & 1u);
        }
    } else {
        r->running = 0;
        drm_now[t] = 0xFF;
    }
    for (i = 0; press >> i; i++)                         /* the keys: the kit, played */
        if ((press >> i) & 1u)
            drm_strike(t, p, i, 0, 0);
    for (j = 0; j < n; j++)
        out[j] = 0;
    for (j = 0; j < DRM_NV; j++)
        if (r->v[j].live || r->v[j].trig) {
            int32_t y[CTL];
            dk_run(&r->c[j], &r->v[j], y, n);
            for (i = 0; i < n; i++)
                out[i] += y[i];
        }
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the parameter table. Every track has five devices (its source, GRAIN, RESONATOR, COLOR, SPACE) with four
 * knobs each, and four modulator slots with four knobs each. KNOB 1..4 always edit the four values of whatever the
 * screen shows (the PRD's "4-value strip"), so a page is just (device or slot, track).
 *
 * The values live here as integers in each parameter's own range; the DSP reads them once per control block and
 * maps them (the device modules own that mapping). The labels are what the strip prints under each pictogram: at
 * most 4 capitals, so four sit apart across the screen in the 5 x 7 face (ui_px.c), the way groovebox screens
 * abbreviate. */

enum { DEV_SRC, DEV_GRAIN, DEV_RESO, DEV_COLOR, DEV_SPACE, NDEV };
#define NSLOT 4u                 /* modulator slots per track */
enum { F_PCT, F_BIPCT, F_ST, F_NOTE, F_MS, F_NUM, F_DB, F_ENUM };   /* how a value prints */

typedef struct {
    const char *label;
    int16_t min, max, def;
    uint8_t fmt;
    const char *const *names;    /* F_ENUM: a name per value, min..max (at most 4 letters) */
} pdesc_t;

/* the names of the switch-like values */
static const char *const N_OFFON[2] = {"OFF", "ON"};
static const char *const N_TRIG[2] = {"FREE", "KEY"};
static const char *const N_CLK[2] = {"FREE", "BPM"};
static const char *const N_MODE[2] = {"KEYS", "FLLW"};
static const char *const N_DIR[4] = {"FWD", "REV", "PING", "RND"};
static const char *const N_SHAPE[5] = {"SIN", "TRI", "SQR", "SAW", "RND"};
static const char *const N_SRC[6] = {"SELF", "T1", "T2", "T3", "T4", "USB"};
static const char *const N_HOLD[5] = {"OFF", "1/32", "1/16", "1/8", "1/4"};

#define NPK 16u                  /* knobs a device or engine can have: up to four pages of four */

static const char *const DEV_NAME[NDEV] = {"TAPE", "GRAIN", "RESONATOR", "COLOR", "SPACE"};

/* Page 1 of each device is the PRD's four knobs. Page 2 (the same pad pressed again) holds what the sound needs
 * that the PRD left without a knob: wet levels, the switches the black keys flip, the grain's shape. A device
 * without a page 2 has empty labels there. */
static const pdesc_t DEV_P[NDEV][NPK] = {
    {   /* TAPE (the source: other source engines bring their own, phase 2) */
        {"STRT", 0, 100, 0, F_PCT}, {"LEN", 1, 100, 100, F_PCT},
        {"SPD", -200, 200, 100, F_BIPCT}, {"DUB", 0, 100, 50, F_PCT},
        /* 2: the loop's crossfade at its ends, reverse and half speed (OP5 and OP6 flip them too), the record gain */
        {"FADE", 0, 100, 10, F_MS}, {"REV", 0, 1, 0, F_ENUM, N_OFFON},
        {"HALF", 0, 1, 0, F_ENUM, N_OFFON}, {"GAIN", -12, 12, 0, F_DB}},
    {   /* GRAIN */
        {"SIZE", 5, 500, 80, F_MS}, {"DENS", 0, 100, 40, F_PCT},
        {"TUNE", -24, 24, 0, F_ST}, {"SPRD", 0, 100, 30, F_PCT},
        /* 2: dry/wet, the jitter of where grains read, the window (square .. smooth), the chance one plays back */
        {"MIX", 0, 100, 100, F_PCT}, {"JIT", 0, 100, 20, F_PCT},
        {"WIN", 0, 100, 50, F_PCT}, {"REV", 0, 100, 0, F_PCT}},
    {   /* RESONATOR */
        {"ROOT", 33, 81, 45, F_NOTE}, {"FDBK", 0, 100, 60, F_PCT},
        {"DAMP", 0, 100, 40, F_PCT}, {"MIX", 0, 100, 0, F_PCT},
        {""}, {""}, {""}, {""}},
    {   /* COLOR */
        {"DRIV", 0, 100, 0, F_PCT}, {"CRSH", 0, 100, 0, F_PCT},
        {"NOIS", 0, 100, 0, F_PCT}, {"TONE", 0, 100, 50, F_PCT},
        /* 2: the output level (drive adds loudness), dry/wet, the sample-rate reduction (CRSH keeps the bits), and
         * the threshold the noise's envelope opens at */
        {"LVL", -24, 6, 0, F_DB}, {"MIX", 0, 100, 100, F_PCT},
        {"SRR", 0, 100, 0, F_PCT}, {"GATE", 0, 100, 0, F_PCT}},
    {   /* SPACE */
        {"TIME", 10, 370, 250, F_MS}, {"FDBK", 0, 100, 30, F_PCT},
        {"SIZE", 0, 100, 50, F_PCT}, {"DEC", 0, 100, 40, F_PCT},
        /* 2: the delay's and the reverb's wet levels, the reverb's pre-delay, the stereo width */
        {"DMIX", 0, 100, 30, F_PCT}, {"RMIX", 0, 100, 30, F_PCT},
        {"PRE", 0, 200, 20, F_MS}, {"WIDE", 0, 100, 100, F_PCT}},
};

/* an unused knob on a page (no label) */
static int pdesc_empty(const pdesc_t *d) { return !d->label || !d->label[0]; }

/* a device's or an engine's pages: up to the first page whose first knob has no label */
static uint32_t pdesc_pages(const pdesc_t *p)
{
    uint32_t n = 1;
    while (n < NPK / 4u && !pdesc_empty(&p[4u * n]))
        n++;
    return n;
}

/* each track's channel strip, after the chain and before the mix (the mixer's second page: EDIT held under GLO).
 * LOW and HIGH are shelves (+-12 dB); FILT is one knob for two filters, a low-pass turning left of 0 and a
 * high-pass turning right of it (the DJ-mixer way: 0 is open); PAN is the place in the stereo field. The mixer
 * DSP (phase 6) applies them; until then they are values you can set and see. */
enum { CH_LOW, CH_HIGH, CH_FILT, CH_PAN, NCH };
static const pdesc_t CH_P[NCH] = {
    {"LOW", -12, 12, 0, F_DB}, {"HIGH", -12, 12, 0, F_DB}, {"FILT", -100, 100, 0, F_BIPCT}, {"PAN", -100, 100, 0, F_BIPCT},
};

/* The modulator engines (phase 7 runs them; their knobs exist now so a slot's page can be edited), lined up with
 * the Torso S-4's modulators (docs/bryo-architecture.md, "Modulators"). The S-4's WAVE and RANDOM are one LFO
 * here: random is one of its shapes (RND), on the same core, so every shape gets the same rate, placement and
 * timing, and the random knobs (SMTH, VAR, LEN) work on every shape. ADSR and FOLLOW are the S-4's; SEQ is the
 * PRD's. Page by page: the shape's character first, then its refinements, its depth and placement (AMT, OFS,
 * PHAS, SPRD), then the timing switches. The PRD's hold-and-turn sets each target's depth; AMT scales the slot as
 * a whole, as the S-4's AMOUNT does. */
enum { ME_WAVE, ME_ADSR, ME_SEQ, ME_FOLLOW, NME };
static const char *const ME_NAME[NME] = {"LFO", "ADSR", "SEQ", "FOLLOW"};
#define P_AMT {"AMT", 0, 100, 100, F_PCT}
#define P_OFS {"OFS", -100, 100, 0, F_BIPCT}
#define P_PHAS {"PHAS", 0, 359, 0, F_NUM}
#define P_SPRD {"SPRD", 0, 100, 0, F_PCT}            /* the right channel's phase against the left: the S-4's SPREAD */
#define P_TRIG {"TRIG", 0, 1, 0, F_ENUM, N_TRIG}      /* free-running, or restarted by a key */
#define LFO_RND 4                                     /* the LFO's random shape (N_SHAPE) */
static const pdesc_t ME_P[NME][NPK] = {
    {   /* LFO. 1: RATE and the shape (SIN TRI SQR SAW RND), bent by SKEW (squashed to a side) and FOLD (peaks
         * folded back). 2: CURV widens or narrows the curves; SMTH slews it (RND: glides between its steps); VAR
         * lets each time round drift from the last (0: the same forever); LEN is RND's steps per loop, and for
         * the other shapes the cycles before VAR's drift repeats. 3: depth and placement. 4: the clock (free Hz
         * or the tempo's divisions), the key restart, a fade-in. */
        {"RATE", 0, 127, 64, F_NUM}, {"SHPE", 0, 4, 0, F_ENUM, N_SHAPE}, {"SKEW", -100, 100, 0, F_BIPCT},
        {"FOLD", 0, 100, 0, F_PCT},
        {"CURV", -100, 100, 0, F_BIPCT}, {"SMTH", 0, 100, 0, F_PCT}, {"VAR", 0, 100, 0, F_PCT}, {"LEN", 1, 16, 8, F_NUM},
        P_AMT, P_OFS, P_PHAS, P_SPRD,
        {"SYNC", 0, 1, 1, F_ENUM, N_CLK}, P_TRIG, {"FADE", 0, 100, 0, F_PCT}, {""}},
    {   /* ADSR: the four stages, then their curves (0 straight, - logarithmic, + exponential) and SPRD, then the
         * key's velocity, looping (attack and decay cycling), depth and offset */
        {"ATK", 0, 127, 10, F_NUM}, {"DEC", 0, 127, 50, F_NUM}, {"SUS", 0, 100, 60, F_PCT}, {"REL", 0, 127, 50, F_NUM},
        {"ACRV", -100, 100, 0, F_BIPCT}, {"DCRV", -100, 100, 0, F_BIPCT}, {"RCRV", -100, 100, 0, F_BIPCT}, P_SPRD,
        {"VEL", 0, 100, 0, F_PCT}, {"LOOP", 0, 1, 0, F_ENUM, N_OFFON}, P_AMT, P_OFS},
    {   /* SEQ (the PRD's): 16 steps of values, then their order, restart, chance and first step */
        {"LEN", 1, 16, 16, F_NUM}, {"RATE", 0, 5, 2, F_NUM}, {"SLEW", 0, 100, 0, F_PCT}, {"SWNG", 0, 100, 0, F_PCT},
        {"DIR", 0, 3, 0, F_ENUM, N_DIR}, P_TRIG, {"PROB", 0, 100, 100, F_PCT}, {"STRT", 1, 16, 1, F_NUM}},
    {   /* FOLLOW: the envelope of a track's sound (SELF: this track's tape) or USB in; GAIN into it, how fast it
         * rises and falls; then sample-and-hold to the tempo, depth, offset, spread */
        {"SRC", 0, 5, 0, F_ENUM, N_SRC}, {"GAIN", -12, 12, 0, F_DB}, {"RISE", 0, 127, 10, F_NUM},
        {"FALL", 0, 127, 50, F_NUM},
        {"HOLD", 0, 4, 0, F_ENUM, N_HOLD}, P_AMT, P_OFS, P_SPRD},
};
/* a SEQ slot's steps before you set them: a pattern with an accent on each beat, so the page shows the idea
 * (the slot's depth is 0 until it's assigned, so this moves nothing) */
static const int8_t SEQ_DEF[16] = {100, 25, 60, 25, 80, 25, 60, 40, 100, 25, 60, 25, 80, 50, 35, 20};

/* the slots' default engines: the pads are labelled LFO ENV SEQ ARP (PRD 2.2) */
static const uint8_t SLOT_DEF_ENGINE[NSLOT] = {ME_WAVE, ME_ADSR, ME_SEQ, ME_WAVE};   /* slot 4: the LFO, RND */

typedef struct {
    int16_t dev[NDEV][NPK];
    uint8_t engine[NSLOT];       /* the engine each slot runs */
    int16_t mod[NSLOT][NPK];     /* the slot's own knobs */
    int16_t ch[NCH];             /* the channel strip */
    int8_t steps[NSLOT][16];     /* a SEQ slot's step values, 0..100 (set per step from phase 7: slot + key + knob) */
} track_params_t;
static track_params_t tp[NTRK];

/* slot s of track t runs engine e: its knobs start from that engine's defaults */
static void param_engine(uint32_t t, uint32_t s, uint32_t e)
{
    uint32_t k;
    tp[t].engine[s] = (uint8_t)e;
    for (k = 0; k < NPK; k++)
        tp[t].mod[s][k] = pdesc_empty(&ME_P[e][k]) ? 0 : ME_P[e][k].def;
}

static void param_defaults(void)
{
    uint32_t t, d, k, s;
    for (t = 0; t < NTRK; t++) {
        for (d = 0; d < NDEV; d++)
            for (k = 0; k < NPK; k++)
                tp[t].dev[d][k] = pdesc_empty(&DEV_P[d][k]) ? 0 : DEV_P[d][k].def;
        for (k = 0; k < NCH; k++)
            tp[t].ch[k] = CH_P[k].def;
        for (s = 0; s < NSLOT; s++) {
            param_engine(t, s, SLOT_DEF_ENGINE[s]);
            if (s == 3u) {                               /* ARP's slot: the random LFO, a loop that drifts a little */
                tp[t].mod[s][1] = LFO_RND;
                tp[t].mod[s][6] = 20;
            }
            for (k = 0; k < 16u; k++)
                tp[t].steps[s][k] = SEQ_DEF[k];
        }
    }
}

/* a value step for one detent: fine for small ranges, ~1 % of the range for wide ones (the knobs are detented
 * encoders, so a full sweep of a wide range is about two turns) */
static int32_t param_step(const pdesc_t *d)
{
    int32_t span = d->max - d->min;
    return span > 200 ? span / 100 : 1;
}

static int16_t param_nudge(const pdesc_t *d, int32_t v, int32_t detents)
{
    return (int16_t)clamp(v + detents * param_step(d), d->min, d->max);
}

/* the position of v in its range, 0..1000 (the dial's arc) */
static int32_t param_ratio(const pdesc_t *d, int32_t v)
{
    return d->max > d->min ? (v - d->min) * 1000 / (d->max - d->min) : 0;
}

/* v as the strip prints it: value and unit apart (the unit is drawn smaller) */
static void param_format(const pdesc_t *d, int32_t v, char *val, const char **unit)
{
    static const char NOTE[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    *unit = "";
    switch (d->fmt) {
    case F_PCT:
        fmt_int(val, v);
        *unit = "%";
        break;
    case F_BIPCT:
    case F_ST:
        if (v > 0) {
            val[0] = '+';
            fmt_int(val + 1, v);
        } else {
            fmt_int(val, v);
        }
        *unit = d->fmt == F_ST ? "st" : "%";
        break;
    case F_NOTE:
        str_cpy(val, NOTE[(uint32_t)v % 12u], 4);
        fmt_int(val + str_len(val), v / 12 - 1);
        break;
    case F_MS:
        fmt_int(val, v);
        *unit = "ms";
        break;
    case F_DB:
        if (v > 0) {
            val[0] = '+';
            fmt_int(val + 1, v);
        } else {
            fmt_int(val, v);
        }
        *unit = "dB";
        break;
    case F_ENUM:
        str_cpy(val, d->names[v - d->min], 6);
        break;
    default:
        fmt_int(val, v);
        break;
    }
}

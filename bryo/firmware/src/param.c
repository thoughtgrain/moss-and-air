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
static const char *const N_SHAPE[3] = {"SIN", "TRI", "SQR"};

#define NPK 8u                   /* knobs a device or engine can have: two pages of four */

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

/* a device's or an engine's pages: 2 when its page 2 has knobs */
static uint32_t pdesc_pages(const pdesc_t *p) { return p[4].label[0] ? 2u : 1u; }

/* each track's channel strip, after the chain and before the mix (the mixer's second page: EDIT held under GLO).
 * LOW and HIGH are shelves (+-12 dB); FILT is one knob for two filters, a low-pass turning left of 0 and a
 * high-pass turning right of it (the DJ-mixer way: 0 is open); PAN is the place in the stereo field. The mixer
 * DSP (phase 6) applies them; until then they are values you can set and see. */
enum { CH_LOW, CH_HIGH, CH_FILT, CH_PAN, NCH };
static const pdesc_t CH_P[NCH] = {
    {"LOW", -12, 12, 0, F_DB}, {"HIGH", -12, 12, 0, F_DB}, {"FILT", -100, 100, 0, F_BIPCT}, {"PAN", -100, 100, 0, F_BIPCT},
};

/* the modulator engines (phase 7 runs them; their knobs exist now so a slot's page can be edited) */
enum { ME_WAVE, ME_RANDOM, ME_ADSR, ME_SEQ, NME };
static const char *const ME_NAME[NME] = {"WAVE", "RANDOM", "ADSR", "SEQ"};
static const pdesc_t ME_P[NME][NPK] = {
    {{"RATE", 0, 127, 64, F_NUM}, {"SHPE", 0, 2, 0, F_ENUM, N_SHAPE}, {"FOLD", 0, 100, 0, F_PCT},
     {"SKEW", -100, 100, 0, F_BIPCT},
     /* 2: the start phase, free-running or restarted by a key, free or locked to the tempo, a fade-in */
     {"PHAS", 0, 359, 0, F_NUM}, {"TRIG", 0, 1, 0, F_ENUM, N_TRIG}, {"CLK", 0, 1, 0, F_ENUM, N_CLK},
     {"FADE", 0, 100, 0, F_PCT}},
    {{"RATE", 0, 127, 64, F_NUM}, {"SMTH", 0, 100, 0, F_PCT}, {"SPRD", 0, 100, 100, F_PCT},
     {"BIAS", -100, 100, 0, F_BIPCT}, {""}, {""}, {""}, {""}},
    {{"ATK", 0, 127, 10, F_NUM}, {"DEC", 0, 127, 50, F_NUM}, {"SUS", 0, 100, 60, F_PCT}, {"REL", 0, 127, 50, F_NUM},
     /* 2: the PRD's ADSR / FOLLOW: triggered by keys or following the track's tape; the follower's sensitivity;
      * looping (attack-decay cycling); how much a key's velocity scales it */
     {"MODE", 0, 1, 0, F_ENUM, N_MODE}, {"SENS", 0, 100, 50, F_PCT}, {"LOOP", 0, 1, 0, F_ENUM, N_OFFON},
     {"VEL", 0, 100, 0, F_PCT}},
    {{"LEN", 1, 16, 16, F_NUM}, {"RATE", 0, 5, 2, F_NUM}, {"SLEW", 0, 100, 0, F_PCT}, {"SWNG", 0, 100, 0, F_PCT},
     /* 2: the play order, free-running or restarted by a key, the chance a step plays, the step it starts on */
     {"DIR", 0, 3, 0, F_ENUM, N_DIR}, {"TRIG", 0, 1, 0, F_ENUM, N_TRIG}, {"PROB", 0, 100, 100, F_PCT},
     {"STRT", 1, 16, 1, F_NUM}},
};
/* a SEQ slot's steps before you set them: a pattern with an accent on each beat, so the page shows the idea
 * (the slot's depth is 0 until it's assigned, so this moves nothing) */
static const int8_t SEQ_DEF[16] = {100, 25, 60, 25, 80, 25, 60, 40, 100, 25, 60, 25, 80, 50, 35, 20};

/* the slots' default engines: the pads are labelled LFO ENV SEQ ARP (PRD 2.2) */
static const uint8_t SLOT_DEF_ENGINE[NSLOT] = {ME_WAVE, ME_ADSR, ME_SEQ, ME_RANDOM};

typedef struct {
    int16_t dev[NDEV][NPK];
    uint8_t engine[NSLOT];       /* the engine each slot runs */
    int16_t mod[NSLOT][NPK];     /* the slot's own knobs */
    int16_t ch[NCH];             /* the channel strip */
    int8_t steps[NSLOT][16];     /* a SEQ slot's step values, 0..100 (set per step from phase 7: slot + key + knob) */
} track_params_t;
static track_params_t tp[NTRK];

static void param_defaults(void)
{
    uint32_t t, d, k, s;
    for (t = 0; t < NTRK; t++) {
        for (d = 0; d < NDEV; d++)
            for (k = 0; k < NPK; k++)
                tp[t].dev[d][k] = DEV_P[d][k].def;
        for (k = 0; k < NCH; k++)
            tp[t].ch[k] = CH_P[k].def;
        for (s = 0; s < NSLOT; s++) {
            tp[t].engine[s] = SLOT_DEF_ENGINE[s];
            for (k = 0; k < 16u; k++)
                tp[t].steps[s][k] = SEQ_DEF[k];
            for (k = 0; k < NPK; k++)
                tp[t].mod[s][k] = ME_P[SLOT_DEF_ENGINE[s]][k].def;
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

/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the parameter table. Every track has five devices (its source, GRAIN, RESONATOR, COLOR, SPACE) with four
 * knobs each, and four modulator slots with four knobs each. KNOB 1..4 always edit the four values of whatever the
 * screen shows (the PRD's "4-value strip"), so a page is just (device or slot, track).
 *
 * The values live here as integers in each parameter's own range; the DSP reads them once per control block and
 * maps them (the device modules own that mapping). The labels are what the strip prints: at most 6 capitals, so
 * four fit across 240 px in the S face without cutting. */

enum { DEV_SRC, DEV_GRAIN, DEV_RESO, DEV_COLOR, DEV_SPACE, NDEV };
#define NSLOT 4u                 /* modulator slots per track */
enum { F_PCT, F_BIPCT, F_ST, F_NOTE, F_MS, F_NUM };   /* how a value prints */

typedef struct {
    const char *label;
    int16_t min, max, def;
    uint8_t fmt;
} pdesc_t;

static const char *const DEV_NAME[NDEV] = {"TAPE", "GRAIN", "RESONATOR", "COLOR", "SPACE"};

static const pdesc_t DEV_P[NDEV][4] = {
    {   /* TAPE (the source: other source engines bring their own four, phase 2) */
        {"START", 0, 100, 0, F_PCT}, {"LENGTH", 1, 100, 100, F_PCT},
        {"SPEED", -200, 200, 100, F_BIPCT}, {"DUB", 0, 100, 50, F_PCT}},
    {   /* GRAIN */
        {"SIZE", 5, 500, 80, F_MS}, {"DENS", 0, 100, 40, F_PCT},
        {"PITCH", -24, 24, 0, F_ST}, {"SPREAD", 0, 100, 30, F_PCT}},
    {   /* RESONATOR */
        {"ROOT", 33, 81, 45, F_NOTE}, {"FDBK", 0, 100, 60, F_PCT},
        {"DAMP", 0, 100, 40, F_PCT}, {"MIX", 0, 100, 0, F_PCT}},
    {   /* COLOR */
        {"DRIVE", 0, 100, 0, F_PCT}, {"CRUSH", 0, 100, 0, F_PCT},
        {"NOISE", 0, 100, 0, F_PCT}, {"TONE", 0, 100, 50, F_PCT}},
    {   /* SPACE */
        {"TIME", 10, 370, 250, F_MS}, {"FDBK", 0, 100, 30, F_PCT},
        {"SIZE", 0, 100, 50, F_PCT}, {"DECAY", 0, 100, 40, F_PCT}},
};

/* the modulator engines (phase 7 runs them; their knobs exist now so a slot's page can be edited) */
enum { ME_WAVE, ME_RANDOM, ME_ADSR, ME_SEQ, NME };
static const char *const ME_NAME[NME] = {"WAVE", "RANDOM", "ADSR", "SEQ"};
static const pdesc_t ME_P[NME][4] = {
    {{"RATE", 0, 127, 64, F_NUM}, {"SHAPE", 0, 2, 0, F_NUM}, {"FOLD", 0, 100, 0, F_PCT}, {"SKEW", -100, 100, 0, F_BIPCT}},
    {{"RATE", 0, 127, 64, F_NUM}, {"SMOOTH", 0, 100, 0, F_PCT}, {"SPREAD", 0, 100, 100, F_PCT}, {"BIAS", -100, 100, 0, F_BIPCT}},
    {{"ATK", 0, 127, 10, F_NUM}, {"DEC", 0, 127, 50, F_NUM}, {"SUS", 0, 100, 60, F_PCT}, {"REL", 0, 127, 50, F_NUM}},
    {{"STEPS", 1, 16, 16, F_NUM}, {"RATE", 0, 5, 2, F_NUM}, {"SLEW", 0, 100, 0, F_PCT}, {"SWING", 0, 100, 0, F_PCT}},
};
/* the slots' default engines: the pads are labelled LFO ENV SEQ ARP (PRD 2.2) */
static const uint8_t SLOT_DEF_ENGINE[NSLOT] = {ME_WAVE, ME_ADSR, ME_SEQ, ME_RANDOM};

typedef struct {
    int16_t dev[NDEV][4];
    uint8_t engine[NSLOT];       /* the engine each slot runs */
    int16_t mod[NSLOT][4];       /* the slot's own knobs */
} track_params_t;
static track_params_t tp[NTRK];

static void param_defaults(void)
{
    uint32_t t, d, k, s;
    for (t = 0; t < NTRK; t++) {
        for (d = 0; d < NDEV; d++)
            for (k = 0; k < 4u; k++)
                tp[t].dev[d][k] = DEV_P[d][k].def;
        for (s = 0; s < NSLOT; s++) {
            tp[t].engine[s] = SLOT_DEF_ENGINE[s];
            for (k = 0; k < 4u; k++)
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
    default:
        fmt_int(val, v);
        break;
    }
}
